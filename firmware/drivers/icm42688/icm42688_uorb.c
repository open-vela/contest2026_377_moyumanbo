/****************************************************************************
 * ICM-42688-P uORB Integration
 *
 * Publishes accelerometer and gyroscope data to the uORB sensor
 * framework. Runs as a dedicated high-priority task that samples
 * the IMU at 100 Hz, either via FIFO burst read or direct polling.
 *
 * Published topics:
 *   - ORB_ID(sensor_accel): 3-axis acceleration in m/s^2
 *   - ORB_ID(sensor_gyro):  3-axis angular velocity in rad/s
 *
 * Integration with VelaSense:
 *   The DSP task subscribes to both topics for motion artifact
 *   removal from PPG signals. The inference task uses IMU data
 *   for activity intensity and posture classification.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/sensors/sensor.h>
#include <nuttx/uorb/uorb.h>
#include <syslog.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <errno.h>

#include "icm42688.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ICM42688_UORB_TAG       "icm42688_uorb"

/* Polling interval: 10ms = 100 Hz */

#define ICM42688_POLL_INTERVAL_US  10000

/* Maximum FIFO packets to read per cycle (prevent overrun) */

#define ICM42688_MAX_FIFO_READ     32

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct icm42688_dev g_imu_dev;
static volatile bool g_imu_running = true;

/* uORB publication handles */

static struct orb_advertiser *g_accel_adv = NULL;
static struct orb_advertiser *g_gyro_adv  = NULL;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: icm42688_publish_accel
 *
 * Description:
 *   Publish a single accelerometer reading to the sensor_accel topic.
 *
 ****************************************************************************/

static void icm42688_publish_accel(const struct icm42688_reading *reading)
{
  struct sensor_accel accel_data;

  memset(&accel_data, 0, sizeof(accel_data));

  /* Timestamp in nanoseconds (uORB convention) */

  accel_data.timestamp = reading->timestamp * 1000;  /* us -> ns */

  /* Acceleration in m/s^2 */

  accel_data.x = reading->accel_ms2.x;
  accel_data.y = reading->accel_ms2.y;
  accel_data.z = reading->accel_ms2.z;

  /* Temperature is not part of sensor_accel, but we can store
   * it in the 'temperature' field if the struct supports it.
   * On NuttX sensor_accel, temperature is available.
   */

  accel_data.temperature = reading->temperature;

  /* Publish */

  if (g_accel_adv != NULL)
    {
      int ret = orb_publish(ORB_ID(sensor_accel),
                            g_accel_adv, &accel_data);
      if (ret < 0)
        {
          syslog(LOG_ERR, "[%s] Accel publish failed: %d\n",
                 ICM42688_UORB_TAG, ret);
        }
    }
}

/****************************************************************************
 * Name: icm42688_publish_gyro
 *
 * Description:
 *   Publish a single gyroscope reading to the sensor_gyro topic.
 *
 ****************************************************************************/

static void icm42688_publish_gyro(const struct icm42688_reading *reading)
{
  struct sensor_gyro gyro_data;

  memset(&gyro_data, 0, sizeof(gyro_data));

  /* Timestamp in nanoseconds */

  gyro_data.timestamp = reading->timestamp * 1000;  /* us -> ns */

  /* Angular velocity in rad/s */

  gyro_data.x = reading->gyro_rads.x;
  gyro_data.y = reading->gyro_rads.y;
  gyro_data.z = reading->gyro_rads.z;

  /* Publish */

  if (g_gyro_adv != NULL)
    {
      int ret = orb_publish(ORB_ID(sensor_gyro),
                            g_gyro_adv, &gyro_data);
      if (ret < 0)
        {
          syslog(LOG_ERR, "[%s] Gyro publish failed: %d\n",
                 ICM42688_UORB_TAG, ret);
        }
    }
}

/****************************************************************************
 * Name: icm42688_publish_reading
 *
 * Description:
 *   Publish a single IMU reading to both accel and gyro uORB topics.
 *
 ****************************************************************************/

static void icm42688_publish_reading(const struct icm42688_reading *reading)
{
  if (!reading || !reading->valid)
    {
      return;
    }

  icm42688_publish_accel(reading);
  icm42688_publish_gyro(reading);
}

/****************************************************************************
 * Name: icm42688_uorb_task_polling
 *
 * Description:
 *   Polling-mode task. Reads IMU data directly from registers at
 *   100 Hz. Used when FIFO is disabled or not available.
 *
 ****************************************************************************/

static int icm42688_uorb_task_polling(void)
{
  struct icm42688_reading reading;
  int ret;

  syslog(LOG_INFO, "[%s] Running in polling mode (100 Hz)\n",
         ICM42688_UORB_TAG);

  while (g_imu_running)
    {
      /* Check if new data is available */

      ret = icm42688_data_ready(&g_imu_dev);
      if (ret < 0)
        {
          syslog(LOG_ERR, "[%s] Data-ready check failed: %d\n",
                 ICM42688_UORB_TAG, ret);
          usleep(ICM42688_POLL_INTERVAL_US);
          continue;
        }

      if (ret == 0)
        {
          /* No new data yet, sleep briefly */

          usleep(ICM42688_POLL_INTERVAL_US / 10);
          continue;
        }

      /* Read all data (temp + accel + gyro) in one burst */

      ret = icm42688_read_all(&g_imu_dev, &reading);
      if (ret < 0)
        {
          syslog(LOG_ERR, "[%s] Read failed: %d\n",
                 ICM42688_UORB_TAG, ret);
          usleep(ICM42688_POLL_INTERVAL_US);
          continue;
        }

      if (reading.valid)
        {
          icm42688_publish_reading(&reading);

          syslog(LOG_DEBUG,
                 "[%s] Accel: (%.2f, %.2f, %.2f) m/s^2 "
                 "Gyro: (%.3f, %.3f, %.3f) rad/s\n",
                 ICM42688_UORB_TAG,
                 reading.accel_ms2.x, reading.accel_ms2.y,
                 reading.accel_ms2.z,
                 reading.gyro_rads.x, reading.gyro_rads.y,
                 reading.gyro_rads.z);
        }

      /* Sleep until next sample period */

      usleep(ICM42688_POLL_INTERVAL_US);
    }

  return OK;
}

/****************************************************************************
 * Name: icm42688_uorb_task_fifo
 *
 * Description:
 *   FIFO-mode task. Reads the hardware FIFO in bursts when the
 *   watermark interrupt fires. This is more efficient than polling
 *   since multiple samples can be read per SPI transaction.
 *
 ****************************************************************************/

static int icm42688_uorb_task_fifo(void)
{
  struct icm42688_reading readings[ICM42688_MAX_FIFO_READ];
  int count;
  int ret;

  syslog(LOG_INFO, "[%s] Running in FIFO mode (watermark=%d)\n",
         ICM42688_UORB_TAG, g_imu_dev.config.fifo_watermark);

  while (g_imu_running)
    {
      /* Read all available packets from FIFO */

      count = 0;
      ret = icm42688_read_fifo(&g_imu_dev, readings,
                               ICM42688_MAX_FIFO_READ, &count);
      if (ret < 0)
        {
          syslog(LOG_ERR, "[%s] FIFO read failed: %d\n",
                 ICM42688_UORB_TAG, ret);
          usleep(ICM42688_POLL_INTERVAL_US);
          continue;
        }

      if (count > 0)
        {
          /* Publish each sample in order */

          for (int i = 0; i < count; i++)
            {
              icm42688_publish_reading(&readings[i]);
            }

          syslog(LOG_DEBUG, "[%s] FIFO: published %d samples\n",
                 ICM42688_UORB_TAG, count);
        }

      /* Sleep until next expected watermark.
       * At 100Hz ODR with watermark=16, watermark fires every 160ms.
       * We poll slightly faster to avoid missing data.
       */

      usleep(ICM42688_POLL_INTERVAL_US * (g_imu_dev.config.fifo_watermark / 2));
    }

  return OK;
}

/****************************************************************************
 * Name: icm42688_uorb_task
 *
 * Description:
 *   Main uORB task entry point. Selects FIFO or polling mode
 *   based on driver configuration.
 *
 ****************************************************************************/

static int icm42688_uorb_task(int argc, char *argv[])
{
  int ret;

  syslog(LOG_INFO, "[%s] IMU uORB task started\n", ICM42688_UORB_TAG);

  /* Advertise uORB topics */

  g_accel_adv = orb_advertise(ORB_ID(sensor_accel), NULL);
  if (g_accel_adv == NULL)
    {
      syslog(LOG_ERR, "[%s] Failed to advertise sensor_accel\n",
             ICM42688_UORB_TAG);
      return -errno;
    }

  g_gyro_adv = orb_advertise(ORB_ID(sensor_gyro), NULL);
  if (g_gyro_adv == NULL)
    {
      syslog(LOG_ERR, "[%s] Failed to advertise sensor_gyro\n",
             ICM42688_UORB_TAG);
      orb_unadvertise(g_accel_adv);
      g_accel_adv = NULL;
      return -errno;
    }

  syslog(LOG_INFO, "[%s] uORB topics advertised: sensor_accel, sensor_gyro\n",
         ICM42688_UORB_TAG);

  /* Run in appropriate mode */

  if (g_imu_dev.config.enable_fifo)
    {
      ret = icm42688_uorb_task_fifo();
    }
  else
    {
      ret = icm42688_uorb_task_polling();
    }

  /* Cleanup */

  if (g_accel_adv != NULL)
    {
      orb_unadvertise(g_accel_adv);
      g_accel_adv = NULL;
    }

  if (g_gyro_adv != NULL)
    {
      orb_unadvertise(g_gyro_adv);
      g_gyro_adv = NULL;
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: icm42688_uorb_start
 *
 * Description:
 *   Initialize the ICM-42688-P and start the uORB publishing task.
 *   Uses the VelaSense default configuration:
 *     - Accel: +/-8g, 100Hz, low-noise
 *     - Gyro:  +/-2000 dps, 100Hz
 *     - FIFO:  stream mode, watermark at 16 samples
 *     - INT1:  data-ready interrupt
 *
 * Input Parameters:
 *   spi_bus   - SPI bus number
 *   cs_index  - Chip-select index
 *   irq_pin   - GPIO pin for interrupt (-1 for polling)
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int icm42688_uorb_start(int spi_bus, int cs_index, int irq_pin)
{
  struct icm42688_config config;
  int ret;

  memset(&config, 0, sizeof(config));

  config.spi_bus     = spi_bus;
  config.cs_index    = cs_index;
  config.irq_pin     = irq_pin;
  config.accel_fsr   = ICM42688_ACCEL_RANGE_8G;      /* +/-8g */
  config.gyro_fsr    = ICM42688_GYRO_RANGE_2000DPS;  /* +/-2000 dps */
  config.accel_odr   = ICM42688_ACCEL_ODR_100HZ;     /* 100 Hz */
  config.gyro_odr    = ICM42688_GYRO_ODR_100HZ;      /* 100 Hz */
  config.fifo_mode   = ICM42688_FIFO_STREAM;         /* stream mode */
  config.fifo_watermark = ICM42688_DEFAULT_FIFO_WM;  /* 16 packets */
  config.enable_fifo = true;

  ret = icm42688_init(&config, &g_imu_dev);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Init failed: %d\n", ICM42688_UORB_TAG, ret);
      return ret;
    }

  /* Start background task at high priority (IMU is time-critical) */

  ret = kthread_create("icm42688", 245, 4096,
                       icm42688_uorb_task, NULL, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Failed to create task: %d\n",
             ICM42688_UORB_TAG, ret);
      icm42688_close(&g_imu_dev);
      return ret;
    }

  syslog(LOG_INFO, "[%s] Started on SPI%d CS%d (100 Hz, %s mode)\n",
         ICM42688_UORB_TAG, spi_bus, cs_index,
         config.enable_fifo ? "FIFO" : "polling");

  return OK;
}

/****************************************************************************
 * Name: icm42688_uorb_stop
 *
 * Description:
 *   Stop the uORB publishing task and power down the IMU.
 *
 ****************************************************************************/

void icm42688_uorb_stop(void)
{
  g_imu_running = false;

  /* Give the task time to exit gracefully */

  usleep(50000);  /* 50ms */

  /* Clean up uORB advertisers */

  if (g_accel_adv != NULL)
    {
      orb_unadvertise(g_accel_adv);
      g_accel_adv = NULL;
    }

  if (g_gyro_adv != NULL)
    {
      orb_unadvertise(g_gyro_adv);
      g_gyro_adv = NULL;
    }

  /* Power down the sensor */

  icm42688_close(&g_imu_dev);

  syslog(LOG_INFO, "[%s] Stopped\n", ICM42688_UORB_TAG);
}
