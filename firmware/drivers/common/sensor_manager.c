/****************************************************************************
 * VelaSense Sensor Manager
 *
 * Orchestrates the six sensor channels of the wearable platform.
 * Each high-rate sensor runs in its own NuttX kernel thread that reads
 * hardware FIFOs via interrupt-driven drivers and pushes samples into
 * lock-free ring buffers.  Lower-rate sensors (temperature, fuel gauge)
 * are polled from a shared slow-poll task.
 *
 * Thread architecture:
 *
 *   ppg_task   (prio 250, 4k)  — MAX86141 SPI, 100 Hz
 *   imu_task   (prio 250, 4k)  — ICM42688 SPI, 100 Hz
 *   eda_task   (prio 248, 4k)  — AD5940 SPI,   32 Hz
 *   slow_task  (prio 200, 2k)  — MAX30208 I2C 1 Hz + MAX17048 1/min
 *
 * Haptic (DRV2605L) is triggered on-demand from any context via
 * sensor_manager_trigger_haptic() — no dedicated task.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <time.h>
#include <pthread.h>

#include "sensor_manager.h"
#include "sensor_sync.h"
#include "ring_buffer.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SM_TAG              "sensor_mgr"

/* Task names */

#define TASK_NAME_PPG       "ppg_task"
#define TASK_NAME_IMU       "imu_task"
#define TASK_NAME_EDA       "eda_task"
#define TASK_NAME_SLOW      "slow_task"

/* Task stack sizes (bytes) */

#define PPG_TASK_STACK      4096
#define IMU_TASK_STACK      4096
#define EDA_TASK_STACK      4096
#define SLOW_TASK_STACK     2048

/* Task priorities */

#define PPG_TASK_PRIO       250
#define IMU_TASK_PRIO       250
#define EDA_TASK_PRIO       248
#define SLOW_TASK_PRIO      200

/* Slow task polling intervals */

#define TEMP_POLL_INTERVAL_MS   1000    /* 1 Hz */
#define FUEL_POLL_INTERVAL_MS   60000   /* 1/min */

/* Haptic defaults */

#define HAPTIC_DEFAULT_DURATION_MS  150

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Per-sensor channel state */

struct sensor_channel
{
  struct ring_buf  rb;              /* Ring buffer context */
  struct sensor_data *rb_storage;   /* Heap-allocated ring buffer storage */
  uint32_t         rb_capacity;     /* Ring buffer capacity */
  volatile bool    running;         /* Is this channel actively sampling? */
  volatile bool    enabled;         /* Is this channel enabled? */
  uint32_t         sample_count;    /* Total samples pushed since start */
  uint32_t         error_count;     /* Total bus errors since start */
  uint32_t         last_sample_ms;  /* Timestamp of last sample (ms) */
  float            sqi;             /* Last SQI value (PPG channel only) */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Singleton manager context */

static struct sensor_manager_ctx
{
  struct sensor_manager_config config;
  struct sensor_channel  channels[SENSOR_ID_COUNT];
  enum sensor_power_state power_state;
  volatile bool running;
  pthread_mutex_t haptic_lock;

  /* Task IDs for cleanup */

  pthread_t ppg_tid;
  pthread_t imu_tid;
  pthread_t eda_tid;
  pthread_t slow_tid;
  bool ppg_tid_valid;
  bool imu_tid_valid;
  bool eda_tid_valid;
  bool slow_tid_valid;
} g_sm;

/****************************************************************************
 * Private Functions — Helpers
 ****************************************************************************/

/****************************************************************************
 * Name: sm_channel_init
 *
 * Description:
 *   Initialize a single sensor channel's ring buffer.
 *
 ****************************************************************************/

static int sm_channel_init(struct sensor_channel *ch,
                           uint32_t capacity,
                           uint32_t elem_size)
{
  ch->rb_storage = (struct sensor_data *)malloc(capacity * elem_size);
  if (!ch->rb_storage)
    {
      syslog(LOG_ERR, "[%s] Failed to allocate ring buffer (%lu bytes)\n",
             SM_TAG, (unsigned long)(capacity * elem_size));
      return -ENOMEM;
    }

  ch->rb_capacity = capacity;

  int ret = ring_buf_init(&ch->rb, ch->rb_storage, capacity, elem_size);
  if (ret < 0)
    {
      free(ch->rb_storage);
      ch->rb_storage = NULL;
      return ret;
    }

  ch->running      = false;
  ch->enabled      = true;
  ch->sample_count = 0;
  ch->error_count  = 0;
  ch->last_sample_ms = 0;
  ch->sqi          = 0.0f;

  return OK;
}

/****************************************************************************
 * Name: sm_channel_cleanup
 *
 * Description:
 *   Free a single sensor channel's ring buffer.
 *
 ****************************************************************************/

static void sm_channel_cleanup(struct sensor_channel *ch)
{
  if (ch->rb_storage)
    {
      free(ch->rb_storage);
      ch->rb_storage = NULL;
    }

  ch->running = false;
  ch->enabled = false;
}

/****************************************************************************
 * Name: sm_push_sample
 *
 * Description:
 *   Push a sensor_data element into the appropriate channel's ring buffer
 *   and update health counters.  Safe to call from any context.
 *
 ****************************************************************************/

static void sm_push_sample(uint8_t sensor_id, const struct sensor_data *data)
{
  if (sensor_id >= SENSOR_ID_COUNT)
    {
      return;
    }

  struct sensor_channel *ch = &g_sm.channels[sensor_id];

  if (!ch->enabled)
    {
      return;
    }

  int ret = ring_buf_push(&ch->rb, data);
  if (ret == -ENOSPC)
    {
      /* Overflow — already counted inside ring_buf_push */
      syslog(LOG_WARNING, "[%s] Sensor %d ring buffer overflow\n",
             SM_TAG, sensor_id);
    }

  ch->sample_count++;
  ch->last_sample_ms = (uint32_t)sensor_sync_get_ms();
}

/****************************************************************************
 * Private Functions — Sensor Hardware Access Stubs
 *
 * These are placeholders for the actual sensor driver calls.
 * Each will be replaced by the corresponding driver's API once
 * the individual drivers (max86141, icm42688, ad5940, max30208,
 * drv2605l, max17048) are implemented.
 *
 * The stubs return synthetic data so the framework can be tested
 * end-to-end before hardware is available.
 ****************************************************************************/

/****************************************************************************
 * Name: sm_ppg_read_fifo
 *
 * Description:
 *   Read PPG samples from MAX86141 FIFO via SPI.
 *   Returns the number of samples read.
 ****************************************************************************/

static int sm_ppg_read_fifo(struct sensor_data *buf, int max)
{
  /* TODO: Replace with actual MAX86141 FIFO read
   *
   * Typical flow:
   *   1. Read FIFO overflow + data count registers
   *   2. Burst-read FIFO data (3 bytes per sample: 19-bit PPG)
   *   3. Convert to float, stamp with corrected timestamp
   *
   * For now, generate a synthetic sample for framework testing.
   */

  if (max < 1)
    {
      return 0;
    }

  buf[0].timestamp_us = sensor_sync_get_us();
  buf[0].sensor_id    = SENSOR_ID_PPG;
  buf[0].data_type    = DATA_TYPE_RAW_PPG;
  buf[0].data_len     = sizeof(float);
  buf[0].data.ppg.value = 0.0f;  /* placeholder */

  return 1;
}

/****************************************************************************
 * Name: sm_imu_read_fifo
 *
 * Description:
 *   Read accel + gyro samples from ICM42688 FIFO via SPI.
 ****************************************************************************/

static int sm_imu_read_fifo(struct sensor_data *buf, int max)
{
  /* TODO: Replace with actual ICM42688 FIFO read.
   *
   * Typical flow:
   *   1. Read FIFO count register
   *   2. Burst-read packetized accel+gyro data
   *   3. Convert raw 16-bit to float (g and dps)
   *   4. Push accel and gyro as separate ring buffer entries
   *
   * Returns up to max*2 entries (accel + gyro per sample).
   */

  if (max < 2)
    {
      return 0;
    }

  uint64_t now = sensor_sync_get_us();

  /* Accel sample */

  buf[0].timestamp_us   = now;
  buf[0].sensor_id      = SENSOR_ID_ACCEL;
  buf[0].data_type      = DATA_TYPE_ACCEL;
  buf[0].data_len       = 3 * sizeof(float);
  buf[0].data.accel.x   = 0.0f;
  buf[0].data.accel.y   = 0.0f;
  buf[0].data.accel.z   = 1.0f;  /* 1g at rest */

  /* Gyro sample */

  buf[1].timestamp_us   = now;
  buf[1].sensor_id      = SENSOR_ID_GYRO;
  buf[1].data_type      = DATA_TYPE_GYRO;
  buf[1].data_len       = 3 * sizeof(float);
  buf[1].data.gyro.x    = 0.0f;
  buf[1].data.gyro.y    = 0.0f;
  buf[1].data.gyro.z    = 0.0f;

  return 2;
}

/****************************************************************************
 * Name: sm_eda_read_sample
 *
 * Description:
 *   Read EDA impedance sample from AD5940 via SPI.
 ****************************************************************************/

static int sm_eda_read_sample(struct sensor_data *data)
{
  /* TODO: Replace with actual AD5940 impedance measurement.
   *
   * Typical flow:
   *   1. Read DFT result registers
   *   2. Compute real/imaginary impedance components
   *   3. Convert to skin conductance (microsiemens)
   */

  data->timestamp_us  = sensor_sync_get_us();
  data->sensor_id     = SENSOR_ID_EDA;
  data->data_type     = DATA_TYPE_EDA;
  data->data_len      = sizeof(float);
  data->data.eda.value = 0.0f;  /* placeholder */

  return OK;
}

/****************************************************************************
 * Name: sm_temp_read
 *
 * Description:
 *   Read temperature from MAX30208 via I2C (polling).
 ****************************************************************************/

static int sm_temp_read(struct sensor_data *data)
{
  /* TODO: Replace with actual MAX30208 read via max30208_read_temp() */

  data->timestamp_us  = sensor_sync_get_us();
  data->sensor_id     = SENSOR_ID_TEMP;
  data->data_type     = DATA_TYPE_TEMP;
  data->data_len      = sizeof(float);
  data->data.temp.value = 36.5f;  /* placeholder */

  return OK;
}

/****************************************************************************
 * Name: sm_fuel_read
 *
 * Description:
 *   Read battery state of charge from MAX17048 via I2C.
 ****************************************************************************/

static int sm_fuel_read(struct sensor_data *data)
{
  /* TODO: Replace with actual MAX17048 read */

  data->timestamp_us    = sensor_sync_get_us();
  data->sensor_id       = SENSOR_ID_FUEL_GAUGE;
  data->data_type       = DATA_TYPE_FUEL_GAUGE;
  data->data_len        = sizeof(float);
  data->data.raw[0]     = 0;
  data->data.raw[1]     = 0;
  data->data.raw[2]     = 0;
  data->data.raw[3]     = 0;

  return OK;
}

/****************************************************************************
 * Private Functions — Sampling Tasks
 ****************************************************************************/

/****************************************************************************
 * Name: ppg_task
 *
 * Description:
 *   PPG sampling task. Reads MAX86141 FIFO at 100 Hz.
 *   Sleeps for the inter-sample period and wakes on interrupt
 *   or timeout to drain the FIFO.
 ****************************************************************************/

static int ppg_task(int argc, char *argv[])
{
  struct sensor_channel *ch = &g_sm.channels[SENSOR_ID_PPG];
  struct sensor_data samples[8];  /* Up to 8 FIFO samples per wakeup */
  int period_us = SENSOR_SYNC_US_PER_SEC / g_sm.config.rate_ppg;

  syslog(LOG_INFO, "[%s] PPG task started (rate=%d Hz)\n",
         SM_TAG, g_sm.config.rate_ppg);

  ch->running = true;

  while (g_sm.running && ch->running)
    {
      /* Read samples from hardware FIFO */

      int n = sm_ppg_read_fifo(samples, 8);
      if (n < 0)
        {
          ch->error_count++;
          syslog(LOG_ERR, "[%s] PPG read error: %d\n", SM_TAG, n);
          usleep(period_us);
          continue;
        }

      /* Push each sample into the ring buffer */

      for (int i = 0; i < n; i++)
        {
          sm_push_sample(SENSOR_ID_PPG, &samples[i]);
        }

      /* TODO: When using interrupt-driven reads, replace the usleep
       * with a semaphore wait that the ISR posts on data-ready.
       * For now, polling at the configured rate.
       */

      usleep(period_us);
    }

  ch->running = false;
  syslog(LOG_INFO, "[%s] PPG task stopped\n", SM_TAG);

  return OK;
}

/****************************************************************************
 * Name: imu_task
 *
 * Description:
 *   IMU sampling task. Reads ICM42688 FIFO at 100 Hz.
 *   Produces both accel and gyro samples into separate ring buffers.
 ****************************************************************************/

static int imu_task(int argc, char *argv[])
{
  struct sensor_channel *ch_accel = &g_sm.channels[SENSOR_ID_ACCEL];
  struct sensor_channel *ch_gyro  = &g_sm.channels[SENSOR_ID_GYRO];
  struct sensor_data samples[16];  /* 8 accel + 8 gyro per wakeup */
  int period_us = SENSOR_SYNC_US_PER_SEC / g_sm.config.rate_imu;

  syslog(LOG_INFO, "[%s] IMU task started (rate=%d Hz)\n",
         SM_TAG, g_sm.config.rate_imu);

  ch_accel->running = true;
  ch_gyro->running  = true;

  while (g_sm.running && ch_accel->running)
    {
      int n = sm_imu_read_fifo(samples, 16);
      if (n < 0)
        {
          ch_accel->error_count++;
          ch_gyro->error_count++;
          syslog(LOG_ERR, "[%s] IMU read error: %d\n", SM_TAG, n);
          usleep(period_us);
          continue;
        }

      for (int i = 0; i < n; i++)
        {
          sm_push_sample(samples[i].sensor_id, &samples[i]);
        }

      usleep(period_us);
    }

  ch_accel->running = false;
  ch_gyro->running  = false;
  syslog(LOG_INFO, "[%s] IMU task stopped\n", SM_TAG);

  return OK;
}

/****************************************************************************
 * Name: eda_task
 *
 * Description:
 *   EDA sampling task. Reads AD5940 impedance at 32 Hz.
 ****************************************************************************/

static int eda_task(int argc, char *argv[])
{
  struct sensor_channel *ch = &g_sm.channels[SENSOR_ID_EDA];
  struct sensor_data sample;
  int period_us = SENSOR_SYNC_US_PER_SEC / g_sm.config.rate_eda;

  syslog(LOG_INFO, "[%s] EDA task started (rate=%d Hz)\n",
         SM_TAG, g_sm.config.rate_eda);

  ch->running = true;

  while (g_sm.running && ch->running)
    {
      int ret = sm_eda_read_sample(&sample);
      if (ret < 0)
        {
          ch->error_count++;
          syslog(LOG_ERR, "[%s] EDA read error: %d\n", SM_TAG, ret);
          usleep(period_us);
          continue;
        }

      sm_push_sample(SENSOR_ID_EDA, &sample);

      usleep(period_us);
    }

  ch->running = false;
  syslog(LOG_INFO, "[%s] EDA task stopped\n", SM_TAG);

  return OK;
}

/****************************************************************************
 * Name: slow_task
 *
 * Description:
 *   Slow-polling task for temperature (1 Hz) and fuel gauge (1/min).
 *   Both are I2C devices — they share a single task to avoid bus
 *   contention and reduce thread overhead.
 ****************************************************************************/

static int slow_task(int argc, char *argv[])
{
  struct sensor_channel *ch_temp = &g_sm.channels[SENSOR_ID_TEMP];
  struct sensor_channel *ch_fuel = &g_sm.channels[SENSOR_ID_FUEL_GAUGE];
  struct sensor_data sample;
  uint32_t last_temp_ms = 0;
  uint32_t last_fuel_ms = 0;

  syslog(LOG_INFO, "[%s] Slow-poll task started\n", SM_TAG);

  ch_temp->running = true;
  ch_fuel->running = true;

  while (g_sm.running && ch_temp->running)
    {
      uint32_t now_ms = (uint32_t)sensor_sync_get_ms();

      /* Temperature @ 1 Hz */

      if (now_ms - last_temp_ms >= TEMP_POLL_INTERVAL_MS)
        {
          int ret = sm_temp_read(&sample);
          if (ret < 0)
            {
              ch_temp->error_count++;
            }
          else
            {
              sm_push_sample(SENSOR_ID_TEMP, &sample);
            }

          last_temp_ms = now_ms;
        }

      /* Fuel gauge @ 1/min */

      if (now_ms - last_fuel_ms >= FUEL_POLL_INTERVAL_MS)
        {
          int ret = sm_fuel_read(&sample);
          if (ret < 0)
            {
              ch_fuel->error_count++;
            }
          else
            {
              sm_push_sample(SENSOR_ID_FUEL_GAUGE, &sample);
            }

          last_fuel_ms = now_ms;
        }

      /* Sleep 100ms — fine-grained enough for 1 Hz temp polling */

      usleep(100000);
    }

  ch_temp->running = false;
  ch_fuel->running = false;
  syslog(LOG_INFO, "[%s] Slow-poll task stopped\n", SM_TAG);

  return OK;
}

/****************************************************************************
 * Private Functions — Task Management
 ****************************************************************************/

/****************************************************************************
 * Name: sm_create_task
 *
 * Description:
 *   Create a NuttX kernel thread for a sensor sampling task.
 *
 ****************************************************************************/

static int sm_create_task(const char *name, int priority, int stack_size,
                          int (*entry)(int, char **),
                          pthread_t *tid_out, bool *valid_out)
{
  struct sched_param param;
  pthread_attr_t attr;
  int ret;

  pthread_attr_init(&attr);
  param.sched_priority = priority;
  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  pthread_attr_setschedparam(&attr, &param);
  pthread_attr_setstacksize(&attr, stack_size);

  ret = pthread_create(tid_out, &attr,
                       (void *(*)(void *))entry, NULL);
  if (ret != 0)
    {
      syslog(LOG_ERR, "[%s] Failed to create %s: %d\n",
             SM_TAG, name, ret);
      pthread_attr_destroy(&attr);
      return -ret;
    }

  *valid_out = true;
  pthread_attr_destroy(&attr);

  syslog(LOG_INFO, "[%s] Created task %s (prio=%d, stack=%d)\n",
         SM_TAG, name, priority, stack_size);

  return OK;
}

/****************************************************************************
 * Name: sm_stop_task
 *
 * Description:
 *   Signal a task to stop and wait for it to exit.
 *
 ****************************************************************************/

static void sm_stop_task(const char *name, pthread_t tid, bool valid)
{
  if (!valid)
    {
      return;
    }

  /* The task checks g_sm.running in its loop — setting it to false
   * will cause it to exit naturally.  Join to wait for cleanup.
   */

  pthread_join(tid, NULL);
  syslog(LOG_INFO, "[%s] Task %s stopped\n", SM_TAG, name);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int sensor_manager_init(const struct sensor_manager_config *config)
{
  int ret;

  if (!config)
    {
      return -EINVAL;
    }

  /* Prevent double-init */

  if (g_sm.running)
    {
      syslog(LOG_WARNING, "[%s] Already initialized\n", SM_TAG);
      return -EALREADY;
    }

  memset(&g_sm, 0, sizeof(g_sm));
  memcpy(&g_sm.config, config, sizeof(*config));
  g_sm.power_state = SENSOR_POWER_NORMAL;

  pthread_mutex_init(&g_sm.haptic_lock, NULL);

  /* Initialize the unified timestamp system */

  ret = sensor_sync_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] sensor_sync_init failed: %d\n", SM_TAG, ret);
      return ret;
    }

  /* Initialize ring buffers for each sensor channel */

  ret = sm_channel_init(&g_sm.channels[SENSOR_ID_PPG],
                        CONFIG_SENSOR_PPG_BUF_DEPTH,
                        sizeof(struct sensor_data));
  if (ret < 0)
    {
      goto errout;
    }

  ret = sm_channel_init(&g_sm.channels[SENSOR_ID_ACCEL],
                        CONFIG_SENSOR_ACCEL_BUF_DEPTH,
                        sizeof(struct sensor_data));
  if (ret < 0)
    {
      goto errout;
    }

  ret = sm_channel_init(&g_sm.channels[SENSOR_ID_GYRO],
                        CONFIG_SENSOR_GYRO_BUF_DEPTH,
                        sizeof(struct sensor_data));
  if (ret < 0)
    {
      goto errout;
    }

  ret = sm_channel_init(&g_sm.channels[SENSOR_ID_EDA],
                        CONFIG_SENSOR_EDA_BUF_DEPTH,
                        sizeof(struct sensor_data));
  if (ret < 0)
    {
      goto errout;
    }

  ret = sm_channel_init(&g_sm.channels[SENSOR_ID_TEMP],
                        CONFIG_SENSOR_TEMP_BUF_DEPTH,
                        sizeof(struct sensor_data));
  if (ret < 0)
    {
      goto errout;
    }

  ret = sm_channel_init(&g_sm.channels[SENSOR_ID_FUEL_GAUGE],
                        CONFIG_SENSOR_FUEL_BUF_DEPTH,
                        sizeof(struct sensor_data));
  if (ret < 0)
    {
      goto errout;
    }

  /* TODO: Initialize actual sensor hardware drivers here.
   *
   * PPG (MAX86141):
   *   max86141_init(config->spi_ppg, config->irq_ppg)
   *   → verify part ID, configure LED currents, set FIFO threshold
   *
   * IMU (ICM42688):
   *   icm42688_init(config->spi_imu, config->irq_imu)
   *   → verify WHO_AM_I, set ODR 100Hz, configure FIFO watermark
   *
   * EDA (AD5940):
   *   ad5940_init(config->spi_eda, config->irq_eda)
   *   → configure impedance measurement, set DFT parameters
   *
   * Temperature (MAX30208):
   *   max30208_init(config->i2c_temp) — already implemented in max30208.c
   *
   * Haptic (DRV2605L):
   *   drv2605l_init(config->i2c_haptic)
   *   → select library, set mode to RTP
   *
   * Fuel gauge (MAX17048):
   *   max17048_init(config->i2c_fuel)
   *   → verify device ID, configure alert threshold
   */

  syslog(LOG_INFO, "[%s] All sensors initialized\n", SM_TAG);
  return OK;

errout:
  syslog(LOG_ERR, "[%s] Init failed: %d\n", SM_TAG, ret);
  sensor_manager_close();
  return ret;
}

int sensor_manager_start(void)
{
  int ret;

  if (g_sm.running)
    {
      syslog(LOG_WARNING, "[%s] Already running\n", SM_TAG);
      return -EALREADY;
    }

  g_sm.running = true;

  /* Start high-rate sensor tasks */

  ret = sm_create_task(TASK_NAME_PPG, PPG_TASK_PRIO, PPG_TASK_STACK,
                       ppg_task, &g_sm.ppg_tid, &g_sm.ppg_tid_valid);
  if (ret < 0)
    {
      goto errout;
    }

  ret = sm_create_task(TASK_NAME_IMU, IMU_TASK_PRIO, IMU_TASK_STACK,
                       imu_task, &g_sm.imu_tid, &g_sm.imu_tid_valid);
  if (ret < 0)
    {
      goto errout;
    }

  ret = sm_create_task(TASK_NAME_EDA, EDA_TASK_PRIO, EDA_TASK_STACK,
                       eda_task, &g_sm.eda_tid, &g_sm.eda_tid_valid);
  if (ret < 0)
    {
      goto errout;
    }

  ret = sm_create_task(TASK_NAME_SLOW, SLOW_TASK_PRIO, SLOW_TASK_STACK,
                       slow_task, &g_sm.slow_tid, &g_sm.slow_tid_valid);
  if (ret < 0)
    {
      goto errout;
    }

  syslog(LOG_INFO, "[%s] All sensor tasks started\n", SM_TAG);
  return OK;

errout:
  syslog(LOG_ERR, "[%s] Failed to start sensor tasks\n", SM_TAG);
  g_sm.running = false;
  sensor_manager_stop();
  return ret;
}

int sensor_manager_stop(void)
{
  if (!g_sm.running)
    {
      return OK;
    }

  syslog(LOG_INFO, "[%s] Stopping sensor tasks...\n", SM_TAG);

  /* Signal all tasks to stop — they check g_sm.running in their loops */

  g_sm.running = false;

  /* Wait for each task to exit cleanly */

  sm_stop_task(TASK_NAME_PPG, g_sm.ppg_tid, g_sm.ppg_tid_valid);
  sm_stop_task(TASK_NAME_IMU, g_sm.imu_tid, g_sm.imu_tid_valid);
  sm_stop_task(TASK_NAME_EDA, g_sm.eda_tid, g_sm.eda_tid_valid);
  sm_stop_task(TASK_NAME_SLOW, g_sm.slow_tid, g_sm.slow_tid_valid);

  g_sm.ppg_tid_valid  = false;
  g_sm.imu_tid_valid  = false;
  g_sm.eda_tid_valid  = false;
  g_sm.slow_tid_valid = false;

  syslog(LOG_INFO, "[%s] All sensor tasks stopped\n", SM_TAG);

  return OK;
}

void sensor_manager_close(void)
{
  /* Stop tasks first if still running */

  if (g_sm.running)
    {
      sensor_manager_stop();
    }

  /* Cleanup all channel ring buffers */

  for (int i = 0; i < SENSOR_ID_COUNT; i++)
    {
      sm_channel_cleanup(&g_sm.channels[i]);
    }

  /* TODO: Close actual sensor hardware drivers
   *
   * max86141_close();
   * icm42688_close();
   * ad5940_close();
   * max30208_close();
   * drv2605l_close();
   * max17048_close();
   */

  pthread_mutex_destroy(&g_sm.haptic_lock);

  syslog(LOG_INFO, "[%s] Sensor manager closed\n", SM_TAG);
}

int sensor_manager_get_latest(uint8_t sensor_id,
                              struct sensor_data *data)
{
  if (sensor_id >= SENSOR_ID_COUNT || !data)
    {
      return -EINVAL;
    }

  struct sensor_channel *ch = &g_sm.channels[sensor_id];

  if (!ch->enabled)
    {
      return -ENODEV;
    }

  /* Drain the ring buffer until only the newest sample remains */

  struct sensor_data latest;
  int found = 0;

  while (ring_buf_pop(&ch->rb, &latest) == OK)
    {
      found = 1;
    }

  if (!found)
    {
      return -EAGAIN;
    }

  memcpy(data, &latest, sizeof(struct sensor_data));
  return OK;
}

int sensor_manager_read_bulk(uint8_t sensor_id,
                             struct sensor_data *buf,
                             uint32_t max_count)
{
  if (sensor_id >= SENSOR_ID_COUNT || !buf || max_count == 0)
    {
      return 0;
    }

  struct sensor_channel *ch = &g_sm.channels[sensor_id];

  if (!ch->enabled)
    {
      return 0;
    }

  return ring_buf_pop_bulk(&ch->rb, buf, max_count);
}

int sensor_manager_health_check(struct sensor_health *health)
{
  if (!health)
    {
      return -EINVAL;
    }

  int unhealthy_count = 0;
  uint32_t now_ms = (uint32_t)sensor_sync_get_ms();

  for (int i = 0; i < SENSOR_ID_COUNT; i++)
    {
      struct sensor_channel *ch = &g_sm.channels[i];
      struct sensor_health *h  = &health[i];

      h->total_samples  = ch->sample_count;
      h->error_count    = ch->error_count;
      h->last_sample_ms = ch->last_sample_ms;
      h->ring_overflow  = ring_buf_overflow(&ch->rb);
      h->sqi            = ch->sqi;

      /* A sensor is healthy if:
       * 1. It is enabled
       * 2. It has produced at least one sample
       * 3. Its last sample is within the timeout window
       */

      if (!ch->enabled)
        {
          h->is_healthy = false;
          unhealthy_count++;
          continue;
        }

      if (ch->sample_count == 0)
        {
          /* No samples yet — healthy only if the system just started */

          h->is_healthy = (now_ms < SENSOR_TIMEOUT_MS);
        }
      else
        {
          uint32_t age = now_ms - ch->last_sample_ms;
          h->is_healthy = (age < SENSOR_TIMEOUT_MS);
        }

      if (!h->is_healthy)
        {
          unhealthy_count++;
          syslog(LOG_WARNING, "[%s] Sensor %d unhealthy "
                 "(last=%lu ms ago, errors=%lu)\n",
                 SM_TAG, i,
                 (unsigned long)(now_ms - ch->last_sample_ms),
                 (unsigned long)ch->error_count);
        }
    }

  return unhealthy_count;
}

int sensor_manager_set_power_state(enum sensor_power_state state)
{
  if (state == g_sm.power_state)
    {
      return OK;
    }

  syslog(LOG_INFO, "[%s] Power state: %d -> %d\n",
         SM_TAG, g_sm.power_state, state);

  switch (state)
    {
      case SENSOR_POWER_NORMAL:
        {
          /* Resume all channels at full rate */

          g_sm.channels[SENSOR_ID_PPG].enabled = true;
          g_sm.channels[SENSOR_ID_ACCEL].enabled = true;
          g_sm.channels[SENSOR_ID_GYRO].enabled = true;
          g_sm.channels[SENSOR_ID_EDA].enabled = true;
          g_sm.channels[SENSOR_ID_TEMP].enabled = true;
          g_sm.channels[SENSOR_ID_FUEL_GAUGE].enabled = true;

          /* TODO: Restore full ODR on each sensor */
        }
        break;

      case SENSOR_POWER_LOW_POWER:
        {
          /* Keep PPG and IMU at half rate, EDA at quarter rate.
           * Temperature and fuel gauge continue unchanged.
           * Haptic disabled.
           *
           * TODO: Adjust sensor ODR registers
           */

          /* Disable haptic in low-power */

          g_sm.channels[SENSOR_ID_HAPTIC].enabled = false;
        }
        break;

      case SENSOR_POWER_SHUTDOWN:
        {
          /* Stop all sampling tasks */

          sensor_manager_stop();

          /* TODO: Put all sensors into hardware shutdown/sleep modes */

          for (int i = 0; i < SENSOR_ID_COUNT; i++)
            {
              g_sm.channels[i].enabled = false;
            }
        }
        break;

      default:
        return -EINVAL;
    }

  g_sm.power_state = state;

  return OK;
}

enum sensor_power_state sensor_manager_get_power_state(void)
{
  return g_sm.power_state;
}

int sensor_manager_trigger_haptic(uint8_t effect_id,
                                  uint16_t duration_ms)
{
  if (g_sm.power_state != SENSOR_POWER_NORMAL)
    {
      return -EPERM;
    }

  if (!g_sm.channels[SENSOR_ID_HAPTIC].enabled &&
      g_sm.power_state == SENSOR_POWER_NORMAL)
    {
      /* Re-enable haptic if we're in normal power */

      g_sm.channels[SENSOR_ID_HAPTIC].enabled = true;
    }

  pthread_mutex_lock(&g_sm.haptic_lock);

  syslog(LOG_INFO, "[%s] Haptic trigger: effect=%d duration=%d ms\n",
         SM_TAG, effect_id, duration_ms);

  /* TODO: Replace with actual DRV2605L commands
   *
   * Typical flow:
   *   1. Select waveform library (if not already set)
   *   2. Write effect ID to waveform sequencer register
   *   3. Trigger playback
   *   4. Optionally wait for GO bit to clear
   */

  struct sensor_data haptic_data;
  haptic_data.timestamp_us = sensor_sync_get_us();
  haptic_data.sensor_id    = SENSOR_ID_HAPTIC;
  haptic_data.data_type    = DATA_TYPE_HAPTIC;
  haptic_data.data_len     = sizeof(uint8_t);
  haptic_data.data.raw[0]  = effect_id;

  sm_push_sample(SENSOR_ID_HAPTIC, &haptic_data);

  pthread_mutex_unlock(&g_sm.haptic_lock);

  return OK;
}

bool sensor_manager_is_running(void)
{
  return g_sm.running;
}
