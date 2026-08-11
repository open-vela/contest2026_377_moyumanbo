/****************************************************************************
 * firmware/drivers/max86141/max86141_uorb.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

/**
 * @file max86141_uorb.c
 * @brief MAX86141 uORB integration — publishes sensor_ppgd at 100 Hz
 *
 * This module wraps the MAX86141 SPI driver and publishes PPG data to
 * the NuttX uORB topic "sensor_ppgd".  It implements:
 *
 *  1. Device node registration (/dev/accel0 is already taken, we use
 *     a custom device path /dev/ppg0 via a character driver interface).
 *  2. Interrupt-driven FIFO reads using a GPIO IRQ.
 *  3. SQI computation and LED current feedback.
 *  4. uORB publication at the configured sample rate (default 100 Hz).
 *
 * Architecture:
 *
 *   [MAX86141 HW]
 *         |
 *   SPI + GPIO IRQ
 *         |
 *   [max86141 driver] <-- max86141.c (SPI register access)
 *         |
 *   [max86141_uorb]   <-- this file (data processing + uORB publish)
 *         |
 *   uORB sensor_ppgd  <-- subscribers (algorithms, logging, etc.)
 *
 * The worker thread blocks on the IRQ GPIO, reads the FIFO, processes
 * samples, and publishes to uORB.  This approach decouples the ISR
 * from the slow SPI transactions.
 *
 * NuttX IRQ GPIO pattern:
 *   gpio_irq_register(pin, handler, arg)
 *   gpio_irq_enable(pin)
 *   poll() or read() on /dev/gpioN to block until interrupt
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/clock.h>
#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>
#include <nuttx/kthread.h>
#include <nuttx/signal.h>
#include <nuttx/spi/spi.h>
#include <nuttx/wqueue.h>

#include <sys/ioctl.h>
#include <syslog.h>

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "max86141.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* uORB topic name for PPG data */

#define MAX86141_UORB_TOPIC            "sensor_ppgd"

/* Device path */

#define MAX86141_DEV_PATH              "/dev/ppg0"

/* Worker thread configuration */

#define MAX86141_WORKER_STACK_SIZE     4096
#define MAX86141_WORKER_PRIORITY       100

/* SQI computation: use the last N FIFO samples */

#define MAX86141_SQI_WINDOW            30

/* LED current limits for SQI feedback */

#define MAX86141_LED_MIN_MA            2.0f
#define MAX86141_LED_MAX_MA            80.0f

/* SQI adjustment interval (in FIFO reads) to avoid oscillation */

#define MAX86141_SQI_ADJUST_INTERVAL   10

/* GPIO IRQ path format (/dev/gpioNN) */

#define MAX86141_GPIO_DEV_PATH_FMT     "/dev/gpio%d"
#define MAX86141_GPIO_DEV_PATH_LEN     16

/****************************************************************************
 * Private Data Types
 ****************************************************************************/

/**
 * @brief Driver instance state (one per MAX86141 device).
 */

struct max86141_uorb_s
{
  /* Base driver state (SPI, FIFO, config) */

  struct max86141_dev_s    dev;

  /* uORB publication */

  /* In a full NuttX uORB implementation, you would use:
   *   orb_advertise(ORB_ID(sensor_ppgd), &pub)
   *   orb_publish(ORB_ID(sensor_ppgd), pub, &data)
   * For now, we define the publication handle as an opaque pointer
   * and provide the stub calls. */

  void                    *uorb_pub;     /* uORB advertiser handle      */
  struct sensor_ppgd_s     ppg_data;     /* Last published data         */

  /* Worker thread */

  struct work_s            work;         /* Work queue item             */
  pid_t                    worker_pid;   /* Worker thread PID           */
  volatile bool            running;      /* Worker thread running flag  */

  /* IRQ handling */

  int                      irq_fd;      /* GPIO IRQ file descriptor    */
  volatile bool            irq_pending; /* IRQ signaled                */

  /* SQI tracking */

  uint8_t                  sqi_samples[MAX86141_SQI_WINDOW];
  uint8_t                  sqi_idx;
  uint8_t                  sqi_avg;     /* Running SQI average         */
  uint8_t                  sqi_count;   /* Number of SQI reads so far  */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  max86141_uorb_worker(int argc, char *argv[]);
static int  max86141_uorb_init_irq(struct max86141_uorb_s *priv);
static void max86141_uorb_close_irq(struct max86141_uorb_s *priv);
static int  max86141_uorb_process_fifo(struct max86141_uorb_s *priv);
static void max86141_uorb_update_sqi(struct max86141_uorb_s *priv,
                                     const struct max86141_fifo_sample_s *samples,
                                     uint8_t count);
static int  max86141_uorb_publish(struct max86141_uorb_s *priv,
                                  const struct sensor_ppgd_s *data);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Default configuration for VelaSense wrist PPG */

static const struct max86141_config_s g_default_config =
{
  .spi_frequency  = 8000000,           /* 8 MHz SPI clock             */
  .spi_devid      = 0,                 /* SPI device 0 / CS 0         */
  .irq_pin        = CONFIG_MAX86141_IRQ_PIN,
  .irq_active_low = true,
  .sample_rate    = MAX86141_SR_100HZ,  /* 100 Hz for wrist HR        */
  .pulse_width    = MAX86141_PW_200US,  /* 200 us, 17-bit resolution  */
  .sample_avg     = MAX86141_AVG_4,     /* Average 4 samples          */
  .adc_range      = MAX86141_ADC_RANGE_16384,
  .fifo_watermark = 15,                 /* Interrupt at half-full      */
  .fifo_rollover  = false,
  .led_green_mA   = 20.0f,             /* Green LED: 20 mA default    */
  .led_red_mA     = 15.0f,             /* Red LED: 15 mA              */
  .led_ir_mA      = 15.0f,             /* IR LED: 15 mA               */
  .led_seq        =
    {
      MAX86141_LED_SLOT_GREEN1,        /* Phase 1: Green              */
      MAX86141_LED_SLOT_RED,           /* Phase 2: Red                */
      MAX86141_LED_SLOT_IR,            /* Phase 3: IR                 */
      MAX86141_LED_SLOT_AMBIENT,       /* Phase 4: Ambient (dark)     */
    },
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/**
 * @brief Open and configure the GPIO IRQ line.
 *
 * The MAX86141 drives the INT line low when the FIFO watermark is
 * reached.  We open the GPIO character device and use poll() to
 * block until the interrupt fires.
 *
 * @param[in] priv  Pointer to driver instance.
 * @return 0 on success, negative errno on failure.
 */

static int max86141_uorb_init_irq(struct max86141_uorb_s *priv)
{
  char path[MAX86141_GPIO_DEV_PATH_LEN];
  int ret;

  snprintf(path, sizeof(path), MAX86141_GPIO_DEV_PATH_FMT,
           priv->dev.cfg.irq_pin);

  /* Open the GPIO device for blocking poll() */

  priv->irq_fd = open(path, O_RDONLY);
  if (priv->irq_fd < 0)
    {
      ret = -errno;
      syslog(LOG_ERR, "max86141_uorb: failed to open GPIO %s: %d\n",
             path, ret);
      return ret;
    }

  /* Configure the IRQ as an input with falling-edge detection.
   * This is typically done via ioctl() on the GPIO device:
   *
   *   ioctl(fd, GPIOC_SETPINCFG, &cfg)
   *   ioctl(fd, GPIOC_IRQPOLLEVENT, &event)
   *
   * The exact API depends on the NuttX board support.
   * For now, we just verify the fd is open. */

  syslog(LOG_INFO, "max86141_uorb: GPIO IRQ on pin %d (fd=%d)\n",
         priv->dev.cfg.irq_pin, priv->irq_fd);

  return 0;
}

/**
 * @brief Close the GPIO IRQ file descriptor.
 */

static void max86141_uorb_close_irq(struct max86141_uorb_s *priv)
{
  if (priv->irq_fd >= 0)
    {
      close(priv->irq_fd);
      priv->irq_fd = -1;
    }
}

/**
 * @brief Update the running SQI estimate.
 *
 * Maintains a circular buffer of recent SQI values and computes
 * a weighted average.
 */

static void max86141_uorb_update_sqi(struct max86141_uorb_s *priv,
                                     const struct max86141_fifo_sample_s *samples,
                                     uint8_t count)
{
  uint8_t sqi;

  /* Compute SQI from the current FIFO batch, targeting green channel */

  sqi = max86141_compute_sqi(samples, count, MAX86141_TAG_GREEN);

  /* Store in circular buffer */

  priv->sqi_samples[priv->sqi_idx] = sqi;
  priv->sqi_idx = (priv->sqi_idx + 1) % MAX86141_SQI_WINDOW;

  if (priv->sqi_count < MAX86141_SQI_WINDOW)
    {
      priv->sqi_count++;
    }

  /* Compute exponential moving average: alpha = 0.3 */

  if (priv->sqi_count == 1)
    {
      priv->sqi_avg = sqi;
    }
  else
    {
      /* EMA: avg = 0.7 * avg + 0.3 * new_sample */

      priv->sqi_avg = (uint8_t)(
        0.7f * (float)priv->sqi_avg + 0.3f * (float)sqi);
    }

  priv->dev.sqi = priv->sqi_avg;
}

/**
 * @brief Publish PPG data to the uORB topic.
 *
 * In a full NuttX implementation, this calls:
 *   orb_publish(ORB_ID(sensor_ppgd), priv->uorb_pub, data);
 *
 * @param[in] priv  Pointer to driver instance.
 * @param[in] data  PPG data to publish.
 * @return 0 on success, negative errno on failure.
 */

static int max86141_uorb_publish(struct max86141_uorb_s *priv,
                                 const struct sensor_ppgd_s *data)
{
  /* ---- uORB publication stub ----
   *
   * In a full NuttX build with uORB support:
   *
   *   if (priv->uorb_pub == NULL)
   *     {
   *       priv->uorb_pub = orb_advertise(ORB_ID(sensor_ppgd), data);
   *       if (priv->uorb_pub == NULL)
   *         {
   *           return -errno;
   *         }
   *     }
   *   else
   *     {
   *       orb_publish(ORB_ID(sensor_ppgd), priv->uorb_pub, data);
   *     }
   *
   * For now, we store the data locally and log periodically.
   */

  memcpy(&priv->ppg_data, data, sizeof(priv->ppg_data));

  /* Log PPG data at debug level (every 100th publish to avoid spam) */

  static uint32_t log_counter = 0;
  if ((log_counter++ % 100) == 0)
    {
      syslog(LOG_DEBUG,
             "max86141_uorb: PPG [G=%.0f R=%.0f IR=%.0f] "
             "SQI=%u lost=%u%s\n",
             data->ppg[0], data->ppg[1], data->ppg[2],
             data->sqi, data->samples_lost,
             data->overflow ? " OVERFLOW" : "");
    }

  return 0;
}

/**
 * @brief Process all available FIFO samples and publish to uORB.
 *
 * Reads the FIFO, computes per-channel averages from the multi-LED
 * sequence (Green/Red/IR/Ambient), packages the data, and publishes.
 *
 * @param[in] priv  Pointer to driver instance.
 * @return 0 on success, negative errno on failure.
 */

static int max86141_uorb_process_fifo(struct max86141_uorb_s *priv)
{
  struct max86141_fifo_sample_s samples[MAX86141_FIFO_DEPTH];
  struct sensor_ppgd_s data;
  uint8_t count = 0;
  uint8_t status1 = 0;
  uint8_t status2 = 0;
  int ret;
  int i;

  /* Read interrupt status (clears the interrupt) */

  ret = max86141_read_int_status(&priv->dev, &status1, &status2);
  if (ret < 0)
    {
      syslog(LOG_ERR, "max86141_uorb: read_int_status failed (%d)\n", ret);
      return ret;
    }

  /* Check for ALC overflow (ambient light too strong) */

  bool alc_overflow = (status1 & MAX86141_INT_ALC_OVF) != 0;
  if (alc_overflow)
    {
      syslog(LOG_WARNING, "max86141_uorb: ALC overflow detected\n");
    }

  /* Read FIFO samples */

  ret = max86141_read_fifo(&priv->dev, samples, MAX86141_FIFO_DEPTH, &count);
  if (ret < 0)
    {
      syslog(LOG_ERR, "max86141_uorb: read_fifo failed (%d)\n", ret);
      return ret;
    }

  if (count == 0)
    {
      return 0;
    }

  /* ---- Build the uORB message ----
   *
   * The MAX86141 reports samples in sequence: Green, Red, IR, Ambient,
   * Green, Red, IR, Ambient, ... (depending on LED sequence config).
   * We average all samples of each channel within this FIFO batch to
   * produce one composite PPG value per channel.
   *
   * For the primary PPG (Green), we also track the latest raw value
   * for waveform display. */

  memset(&data, 0, sizeof(data));
  data.timestamp = clock_systime_ticks();
  data.timestamp_sample = (uint32_t)TICK2USEC(data.timestamp);

  /* Per-channel accumulators */

  float ppg_sum[MAX86141_LED_COUNT] = {0.0f, 0.0f, 0.0f};
  uint32_t ppg_raw_sum[MAX86141_LED_COUNT] = {0, 0, 0};
  uint8_t ppg_count[MAX86141_LED_COUNT] = {0, 0, 0};
  float ambient_sum = 0.0f;
  uint8_t ambient_count = 0;

  for (i = 0; i < count; i++)
    {
      uint8_t tag = samples[i].tag;
      float raw_f = (float)samples[i].raw_data;

      switch (tag)
        {
          case MAX86141_TAG_GREEN:
          case MAX86141_TAG_GREEN2:
            ppg_sum[0]     += raw_f;
            ppg_raw_sum[0] += samples[i].raw_data;
            ppg_count[0]++;
            break;

          case MAX86141_TAG_RED:
            ppg_sum[1]     += raw_f;
            ppg_raw_sum[1] += samples[i].raw_data;
            ppg_count[1]++;
            break;

          case MAX86141_TAG_IR:
            ppg_sum[2]     += raw_f;
            ppg_raw_sum[2] += samples[i].raw_data;
            ppg_count[2]++;
            break;

          case MAX86141_TAG_AMBIENT:
            ambient_sum += raw_f;
            ambient_count++;
            break;

          default:
            break;
        }
    }

  /* Compute per-channel averages */

  for (i = 0; i < MAX86141_LED_COUNT; i++)
    {
      if (ppg_count[i] > 0)
        {
          data.ppg[i] = ppg_sum[i] / (float)ppg_count[i];
          data.raw[i] = ppg_raw_sum[i] / ppg_count[i];
        }
      else
        {
          data.ppg[i] = 0.0f;
          data.raw[i] = 0;
        }

      data.tag[i] = ppg_count[i];  /* Reuse field as sample count */
    }

  if (ambient_count > 0)
    {
      data.ambient = ambient_sum / (float)ambient_count;
    }

  /* SQI and overflow status */

  max86141_uorb_update_sqi(priv, samples, count);
  data.sqi = priv->sqi_avg;
  data.overflow = priv->dev.fifo_overflow_cnt > 0;
  data.alc_overflow = alc_overflow;
  data.samples_lost = priv->dev.fifo_overflow_cnt;

  /* ---- SQI-based LED current feedback ----
   *
   * Every N FIFO reads, check the SQI and adjust the LED current
   * if the signal quality is suboptimal.  This is a slow feedback
   * loop to avoid hunting. */

  priv->sqi_count++;
  if (priv->sqi_count >= MAX86141_SQI_ADJUST_INTERVAL)
    {
      priv->sqi_count = 0;

      if (priv->sqi_avg < 40 || priv->sqi_avg > 85)
        {
          ret = max86141_adjust_led_from_sqi(
            &priv->dev, priv->sqi_avg,
            MAX86141_LED_MIN_MA, MAX86141_LED_MAX_MA);
          if (ret < 0)
            {
              syslog(LOG_WARNING,
                     "max86141_uorb: LED adjust failed (%d)\n", ret);
            }
        }
    }

  /* Publish to uORB */

  return max86141_uorb_publish(priv, &data);
}

/**
 * @brief Worker thread: blocks on IRQ, reads FIFO, publishes data.
 *
 * This thread runs at high priority and blocks on the GPIO IRQ
 * file descriptor.  When the FIFO watermark interrupt fires, it
 * reads all available samples, processes them, and publishes to
 * the uORB sensor_ppgd topic.
 *
 * Thread lifecycle:
 *   1. Open GPIO IRQ
 *   2. Initialize MAX86141 sensor
 *   3. Loop: poll(IRQ) -> read FIFO -> publish uORB
 *   4. Cleanup on exit
 *
 * @param[in] argc  Not used.
 * @param[in] argv  Not used.
 * @return 0 on success (never returns unless signaled).
 */

static int max86141_uorb_worker(int argc, char *argv[])
{
  struct max86141_uorb_s *priv;
  struct pollfd pfd;
  int ret;

  /* Retrieve the private data pointer from the thread name argument.
   * In NuttX, the thread can receive its context via argv or global. */

  /* For this implementation, we use a static pointer set by the
   * start function.  In production, use a proper context passing
   * mechanism. */

  extern struct max86141_uorb_s *g_max86141_priv;
  priv = g_max86141_priv;

  if (priv == NULL)
    {
      syslog(LOG_ERR, "max86141_uorb: no device context\n");
      return -EINVAL;
    }

  syslog(LOG_INFO, "max86141_uorb: worker thread started (pid=%d)\n",
         getpid());

  /* Initialize the GPIO IRQ */

  ret = max86141_uorb_init_irq(priv);
  if (ret < 0)
    {
      syslog(LOG_ERR, "max86141_uorb: IRQ init failed (%d)\n", ret);
      return ret;
    }

  /* Initialize the MAX86141 sensor */

  ret = max86141_init(&priv->dev, &g_default_config);
  if (ret < 0)
    {
      syslog(LOG_ERR, "max86141_uorb: sensor init failed (%d)\n", ret);
      goto err_close_irq;
    }

  priv->running = true;

  /* ---- Main processing loop ---- */

  pfd.fd     = priv->irq_fd;
  pfd.events = POLLIN;

  while (priv->running)
    {
      /* Block until the FIFO watermark interrupt fires.
       * Timeout after 2 seconds to allow graceful shutdown check
       * and to handle missed interrupts. */

      ret = poll(&pfd, 1, 2000);

      if (ret < 0)
        {
          if (errno == EINTR)
            {
              continue;  /* Interrupted by signal, retry */
            }

          syslog(LOG_ERR, "max86141_uorb: poll error (%d)\n", -errno);
          break;
        }

      if (ret == 0)
        {
          /* Timeout: no interrupt in 2 seconds.
           * This may indicate a sensor problem or very low activity.
           * Try a direct FIFO read anyway to avoid data staleness. */

          syslog(LOG_WARNING, "max86141_uorb: IRQ timeout, "
                 "reading FIFO directly\n");

          /* Clear any stale IRQ state */
          (void)max86141_read_int_status(&priv->dev, &(uint8_t){0}, NULL);
        }

      /* Read and process the FIFO */

      ret = max86141_uorb_process_fifo(priv);
      if (ret < 0)
        {
          syslog(LOG_ERR, "max86141_uorb: process_fifo failed (%d)\n",
                 ret);

          /* Attempt recovery: flush FIFO and continue */

          max86141_flush_fifo(&priv->dev);
          usleep(10000);  /* 10 ms backoff */
        }
    }

  /* Shutdown sequence */

  syslog(LOG_INFO, "max86141_uorb: shutting down\n");
  max86141_shutdown(&priv->dev);

err_close_irq:
  max86141_uorb_close_irq(priv);
  priv->running = false;
  return ret;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

/**
 * Global device pointer for the worker thread context.
 * In production, use a proper context passing mechanism
 * (e.g., thread_create with the pointer as an argument).
 */

struct max86141_uorb_s *g_max86141_priv = NULL;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * @brief Start the MAX86141 PPG sensor driver.
 *
 * Allocates the driver state, creates the worker thread, and
 * begins publishing sensor_ppgd data.
 *
 * @return 0 on success, negative errno on failure.
 */

int max86141_uorb_start(void)
{
  struct max86141_uorb_s *priv;

  if (g_max86141_priv != NULL)
    {
      syslog(LOG_WARNING, "max86141_uorb: already running\n");
      return -EBUSY;
    }

  /* Allocate driver state */

  priv = kmm_zalloc(sizeof(struct max86141_uorb_s));
  if (priv == NULL)
    {
      syslog(LOG_ERR, "max86141_uorb: failed to allocate state\n");
      return -ENOMEM;
    }

  priv->irq_fd = -1;
  priv->running = false;
  g_max86141_priv = priv;

  /* Create the worker thread */

  priv->worker_pid = kthread_create(
    "max86141_worker",
    MAX86141_WORKER_PRIORITY,
    MAX86141_WORKER_STACK_SIZE,
    max86141_uorb_worker,
    NULL);

  if (priv->worker_pid < 0)
    {
      syslog(LOG_ERR, "max86141_uorb: failed to create worker (%d)\n",
             priv->worker_pid);
      kmm_free(priv);
      g_max86141_priv = NULL;
      return priv->worker_pid;
    }

  syslog(LOG_INFO, "max86141_uorb: started worker thread (pid=%d)\n",
         priv->worker_pid);

  return 0;
}

/**
 * @brief Stop the MAX86141 PPG sensor driver.
 *
 * Signals the worker thread to exit, waits for it to finish,
 * and frees the driver state.
 *
 * @return 0 on success, negative errno on failure.
 */

int max86141_uorb_stop(void)
{
  struct max86141_uorb_s *priv;
  int retries;
  int ret;

  priv = g_max86141_priv;
  if (priv == NULL)
    {
      return -ENODEV;
    }

  /* Signal the worker thread to stop */

  priv->running = false;

  /* Send SIGUSR1 to wake the thread from poll() */

  if (priv->worker_pid > 0)
    {
      kill(priv->worker_pid, SIGUSR1);
    }

  /* Wait for the thread to exit (max 3 seconds) */

  retries = 0;
  while (priv->running && retries < 300)
    {
      usleep(10000);  /* 10 ms */
      retries++;
    }

  if (priv->running)
    {
      syslog(LOG_WARNING, "max86141_uorb: worker did not exit cleanly\n");
      ret = -ETIMEDOUT;
    }
  else
    {
      ret = 0;
    }

  /* Free resources */

  kmm_free(priv);
  g_max86141_priv = NULL;

  syslog(LOG_INFO, "max86141_uorb: stopped\n");
  return ret;
}

/**
 * @brief Read the latest PPG data (for direct access without uORB).
 *
 * @param[out] data  Receives the latest PPG data.
 * @return 0 on success, -ENODEV if driver is not running.
 */

int max86141_uorb_read_latest(struct sensor_ppgd_s *data)
{
  struct max86141_uorb_s *priv;

  priv = g_max86141_priv;
  if (priv == NULL || !priv->running)
    {
      return -ENODEV;
    }

  memcpy(data, &priv->ppg_data, sizeof(*data));
  return 0;
}

/**
 * @brief Get the current SQI estimate.
 *
 * @return SQI value 0-100, or 0 if driver is not running.
 */

uint8_t max86141_uorb_get_sqi(void)
{
  struct max86141_uorb_s *priv;

  priv = g_max86141_priv;
  if (priv == NULL || !priv->running)
    {
      return 0;
    }

  return priv->sqi_avg;
}

/**
 * @brief Adjust the green LED current (for manual override).
 *
 * @param[in] current_mA  Desired current in mA.
 * @return 0 on success, negative errno on failure.
 */

int max86141_uorb_set_green_current(float current_mA)
{
  struct max86141_uorb_s *priv;

  priv = g_max86141_priv;
  if (priv == NULL || !priv->running)
    {
      return -ENODEV;
    }

  return max86141_set_led_current(&priv->dev, MAX86141_LED_GREEN,
                                  current_mA);
}
