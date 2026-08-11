/****************************************************************************
 * MAX30208 uORB Integration
 *
 * Publishes skin temperature data to the uORB sensor framework.
 * Runs as a dedicated task that polls the sensor at 1 Hz.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/sensors/sensor.h>
#include <nuttx/uorb/uorb.h>
#include <syslog.h>
#include <unistd.h>

#include "max30208.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX30208_UORB_TAG    "max30208_uorb"
#define MAX30208_POLL_MS     1000  /* 1 Hz polling */

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct max30208_dev g_temp_dev;
static volatile bool g_running = true;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: max30208_uorb_task
 *
 * Description:
 *   Background task that reads temperature and publishes to uORB.
 *
 ****************************************************************************/

static int max30208_uorb_task(int argc, char *argv[])
{
  struct max30208_reading reading;
  struct sensor_temp temp_data;
  int ret;

  syslog(LOG_INFO, "[%s] Temperature uORB task started (1 Hz)\n",
         MAX30208_UORB_TAG);

  while (g_running)
    {
      ret = max30208_read_temp(&g_temp_dev, &reading);
      if (ret < 0)
        {
          syslog(LOG_ERR, "[%s] Read failed: %d\n",
                 MAX30208_UORB_TAG, ret);
          usleep(MAX30208_POLL_MS * 1000);
          continue;
        }

      if (reading.valid)
        {
          /* Fill uORB sensor_temp structure */

          memset(&temp_data, 0, sizeof(temp_data));
          clock_gettime(CLOCK_REALTIME, &temp_data.timestamp);
          temp_data.temperature = reading.temperature;

          /* TODO: Publish to uORB topic
           *
           * In openvela with uORB:
           *   orb_advertise(ORB_ID(sensor_temp), &temp_data);
           *   orb_publish(ORB_ID(sensor_temp), &temp_data);
           *
           * For now, log the reading
           */

          syslog(LOG_DEBUG, "[%s] Published: %.2f°C\n",
                 MAX30208_UORB_TAG, reading.temperature);
        }

      usleep(MAX30208_POLL_MS * 1000);
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: max30208_uorb_start
 *
 * Description:
 *   Initialize the MAX30208 and start the uORB publishing task.
 *
 ****************************************************************************/

int max30208_uorb_start(int i2c_bus)
{
  struct max30208_config config;
  int ret;

  memset(&config, 0, sizeof(config));
  config.i2c_bus = i2c_bus;
  config.addr = MAX30208_I2C_ADDR;
  config.irq_pin = -1;  /* Polling mode */

  ret = max30208_init(&config, &g_temp_dev);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Init failed: %d\n", MAX30208_UORB_TAG, ret);
      return ret;
    }

  /* Start background polling task */

  ret = kthread_create("max30208", 120, 2048,
                       max30208_uorb_task, NULL, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Failed to create task: %d\n",
             MAX30208_UORB_TAG, ret);
      max30208_close(&g_temp_dev);
      return ret;
    }

  syslog(LOG_INFO, "[%s] Started on I2C%d\n", MAX30208_UORB_TAG, i2c_bus);

  return OK;
}

/****************************************************************************
 * Name: max30208_uorb_stop
 *
 * Description:
 *   Stop the uORB publishing task and close the sensor.
 *
 ****************************************************************************/

void max30208_uorb_stop(void)
{
  g_running = false;
  max30208_close(&g_temp_dev);
  syslog(LOG_INFO, "[%s] Stopped\n", MAX30208_UORB_TAG);
}
