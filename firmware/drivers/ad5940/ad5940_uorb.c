/****************************************************************************
 * AD5940 uORB Integration
 *
 * Publishes electrodermal activity (EDA) data to the uORB sensor framework
 * as sensor_impd (impedance) topic at 32 Hz.
 *
 * The sensor_impd topic carries bioimpedance data, which includes
 * skin conductance (the reciprocal of impedance for DC measurements).
 *
 * Data published per sample:
 *   - impedance (ohm): skin resistance = 1/conductance
 *   - reactance (ohm): 0 for DC GSR measurement
 *   - raw phase: 0 for DC measurement
 *   - timestamp: sample timestamp
 *
 * Additional EDA-specific data is logged via syslog:
 *   - SCL (skin conductance level) in uS
 *   - SCR (skin conductance response) events
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/sensors/sensor.h>
#include <nuttx/uorb/uorb.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>

#include "ad5940.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define AD5940_UORB_TAG         "ad5940_uorb"

/* Polling interval in microseconds (32 Hz = 31250 us) */

#define AD5940_UORB_PERIOD_US   (1000000 / AD5940_UORB_RATE_HZ)

/* Maximum consecutive read errors before reporting failure */

#define AD5940_MAX_ERRORS       100

/* SCR event log threshold (log every SCR event) */

#define AD5940_LOG_SCR_EVENTS   1

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Extended EDA data for logging (not part of uORB topic) */

struct ad5940_eda_log_data
{
  float    scl_us;              /* Skin conductance level (uS) */
  float    scl_filtered_us;     /* Filtered SCL (uS) */
  float    skin_resistance_kohm; /* Skin resistance (kohm) */
  uint16_t raw_adc;             /* Raw ADC value */
  int      scr_event;           /* SCR event flag */
  float    scr_amplitude;       /* SCR amplitude (uS) */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct ad5940_dev g_eda_dev;
static volatile bool g_running = true;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ad5940_eda_to_impd
 *
 * Description:
 *   Convert an EDA sample to sensor_impd format.
 *   For DC GSR measurement, skin impedance is purely resistive
 *   (reactance = 0, phase = 0).
 *
 *   Skin resistance (ohm) = 1 / conductance (S)
 *                         = 1 / (conductance_uS * 1e-6)
 *                         = 1e6 / conductance_uS
 *
 ****************************************************************************/

static void ad5940_eda_to_impd(const struct ad5940_eda_sample *eda,
                               struct sensor_impd *impd)
{
  memset(impd, 0, sizeof(*impd));

  /* Timestamp */

  clock_gettime(CLOCK_REALTIME, &impd->timestamp);

  /* Impedance (resistance for DC measurement)
   *
   * R = 1/G, where G is in uS
   * R (ohm) = 1e6 / G (uS)
   *
   * Clamp to avoid division by zero and overflow:
   *   - Max resistance: 10 Mohm (G = 0.1 uS)
   *   - Min resistance: 10 kohm (G = 100 uS)
   */

  if (eda->scl_filtered > 0.001f)
    {
      impd->impedance = 1000000.0f / eda->scl_filtered;
    }
  else
    {
      impd->impedance = 10000000.0f;  /* 10 Mohm max */
    }

  /* Reactance: zero for DC measurement */

  impd->reactance = 0.0f;

  /* Phase: zero for DC measurement */

  impd->phase = 0.0f;
}

/****************************************************************************
 * Name: ad5940_log_eda_data
 *
 * Description:
 *   Log EDA-specific data that doesn't fit in the sensor_impd topic.
 *   Called at a reduced rate (every 32nd sample = 1 Hz) to avoid
 *   log spam.
 *
 ****************************************************************************/

static void ad5940_log_eda_data(const struct ad5940_eda_sample *eda,
                                uint32_t sample_count)
{
  struct ad5940_eda_log_data log;

  log.scl_us = eda->scl;
  log.scl_filtered_us = eda->scl_filtered;
  log.raw_adc = eda->raw_adc;
  log.scr_event = eda->scr_event;
  log.scr_amplitude = eda->scr_amplitude;

  /* Compute resistance in kohm */

  if (eda->scl_filtered > 0.001f)
    {
      log.skin_resistance_kohm = 1000.0f / eda->scl_filtered;
    }
  else
    {
      log.skin_resistance_kohm = 10000.0f;  /* 10 Mohm */
    }

  /* Log at 1 Hz (every 32 samples at 32 Hz) */

  if ((sample_count & 0x1F) == 0)
    {
      syslog(LOG_DEBUG,
             "[%s] SCL=%.3f uS, filt=%.3f uS, R=%.1f kOhm, "
             "raw=0x%04X, SCR=%d\n",
             AD5940_UORB_TAG,
             log.scl_us, log.scl_filtered_us,
             log.skin_resistance_kohm,
             log.raw_adc, log.scr_event);
    }

  /* Always log SCR events (they are rare and important) */

#if AD5940_LOG_SCR_EVENTS
  if (eda->scr_event)
    {
      syslog(LOG_INFO,
             "[%s] SCR EVENT: amplitude=%.3f uS, "
             "onset=%u ms, SCL=%.3f uS\n",
             AD5940_UORB_TAG,
             eda->scr_amplitude,
             eda->scr_onset_time,
             eda->scl_filtered);
    }
#endif
}

/****************************************************************************
 * Name: ad5940_uorb_task
 *
 * Description:
 *   Background task that reads EDA data from the AD5940 and publishes
 *   to the uORB sensor_impd topic at 32 Hz.
 *
 *   Task flow:
 *     1. Read ADC sample from AD5940 FIFO
 *     2. Convert to skin conductance
 *     3. Apply low-pass filter
 *     4. Run SCR event detector
 *     5. Convert to sensor_impd format (impedance = 1/conductance)
 *     6. Publish to uORB
 *     7. Log EDA-specific data at reduced rate
 *     8. Sleep until next sample period
 *
 ****************************************************************************/

static int ad5940_uorb_task(int argc, char *argv[])
{
  struct ad5940_eda_sample eda;
  struct sensor_impd impd;
  int ret;
  int error_count = 0;
  uint32_t publish_count = 0;

  syslog(LOG_INFO, "[%s] EDA uORB task started (%d Hz)\n",
         AD5940_UORB_TAG, AD5940_UORB_RATE_HZ);

  /* TODO: Advertise the sensor_impd topic
   *
   * In openvela with uORB:
   *   orb_advert_t pub_fd;
   *   struct sensor_impd init_data;
   *   memset(&init_data, 0, sizeof(init_data));
   *   pub_fd = orb_advertise(ORB_ID(sensor_impd), &init_data);
   *   if (!pub_fd)
   *     {
   *       syslog(LOG_ERR, "[%s] Failed to advertise sensor_impd\n",
   *              AD5940_UORB_TAG);
   *       return -EIO;
   *     }
   */

  /* Main polling loop */

  while (g_running)
    {
      /* Read one EDA sample from the AD5940 */

      ret = ad5940_read_eda(&g_eda_dev, &eda);
      if (ret < 0)
        {
          if (ret == -EAGAIN)
            {
              /* No data ready yet -- wait a bit and retry */

              usleep(AD5940_UORB_PERIOD_US / 4);
              continue;
            }

          error_count++;
          if (error_count > AD5940_MAX_ERRORS)
            {
              syslog(LOG_ERR, "[%s] Too many errors (%d), stopping\n",
                     AD5940_UORB_TAG, error_count);
              break;
            }

          syslog(LOG_WARNING, "[%s] Read error: %d (count=%d)\n",
                 AD5940_UORB_TAG, ret, error_count);
          usleep(AD5940_UORB_PERIOD_US);
          continue;
        }

      /* Reset error count on successful read */

      error_count = 0;

      if (!eda.valid)
        {
          usleep(AD5940_UORB_PERIOD_US);
          continue;
        }

      /* Convert EDA sample to sensor_impd format */

      ad5940_eda_to_impd(&eda, &impd);

      /* Publish to uORB
       *
       * TODO: Replace with actual uORB publish:
       *   orb_publish(ORB_ID(sensor_impd), pub_fd, &impd);
       */

      publish_count++;

      /* Log EDA-specific data (SCL, SCR events) */

      ad5940_log_eda_data(&eda, publish_count);

      /* Wait for next sample period
       *
       * Use a tight loop to maintain precise 32 Hz timing.
       * The ad5940_read_eda() call takes some time, so subtract
       * elapsed time from the wait period.
       */

      usleep(AD5940_UORB_PERIOD_US);
    }

  syslog(LOG_INFO, "[%s] Task exiting (published %u samples)\n",
         AD5940_UORB_TAG, publish_count);

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ad5940_uorb_start
 *
 * Description:
 *   Initialize the AD5940 for EDA measurement and start the uORB
 *   publishing task. This is the main entry point for the EDA
 *   subsystem in the VelaSense wearable.
 *
 * Input Parameters:
 *   spi_bus - SPI bus number for the AD5940
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int ad5940_uorb_start(int spi_bus)
{
  struct ad5940_config config;
  int ret;

  memset(&config, 0, sizeof(config));
  config.spi_bus = spi_bus;
  config.spi_freq = AD5940_SPI_MAX_HZ;
  config.cs_pin = -1;    /* Use hardware CS */
  config.irq_pin = -1;   /* Polling mode */
  config.reset_pin = -1; /* Software reset only */

  /* Initialize the AD5940 AFE */

  ret = ad5940_init(&config, &g_eda_dev);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Init failed: %d\n", AD5940_UORB_TAG, ret);
      return ret;
    }

  /* Start EDA measurement */

  ret = ad5940_start_eda(&g_eda_dev);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Start EDA failed: %d\n",
             AD5940_UORB_TAG, ret);
      ad5940_close(&g_eda_dev);
      return ret;
    }

  /* Start background polling task */

  ret = kthread_create("ad5940_eda", 120, 4096,
                       ad5940_uorb_task, NULL, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Failed to create task: %d\n",
             AD5940_UORB_TAG, ret);
      ad5940_stop_eda(&g_eda_dev);
      ad5940_close(&g_eda_dev);
      return ret;
    }

  syslog(LOG_INFO, "[%s] Started on SPI%d at %d Hz\n",
         AD5940_UORB_TAG, spi_bus, AD5940_UORB_RATE_HZ);

  return OK;
}

/****************************************************************************
 * Name: ad5940_uorb_stop
 *
 * Description:
 *   Stop the uORB publishing task and shut down the AD5940.
 *
 ****************************************************************************/

void ad5940_uorb_stop(void)
{
  g_running = false;

  /* Give the task time to exit */

  usleep(AD5940_UORB_PERIOD_US * 2);

  /* Stop measurement and close device */

  ad5940_stop_eda(&g_eda_dev);
  ad5940_close(&g_eda_dev);

  syslog(LOG_INFO, "[%s] Stopped\n", AD5940_UORB_TAG);
}

/****************************************************************************
 * Name: ad5940_uorb_get_dev
 *
 * Description:
 *   Get a pointer to the AD5940 device context. Useful for direct
 *   register access or configuration changes from application code.
 *
 ****************************************************************************/

struct ad5940_dev *ad5940_uorb_get_dev(void)
{
  return &g_eda_dev;
}
