/****************************************************************************
 * VelaSense Watchdog Manager Implementation
 *
 * Hardware watchdog feeding and per-task health monitoring.
 * Logs faults on task hang and allows the hardware WDT to expire
 * for a clean system reset.
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
#include <sys/ioctl.h>
#include <nuttx/timers/watchdog.h>

#include "watchdog.h"
#include "fault_handler.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TAG                     "wdt"

/* Path to the hardware watchdog device.
 * Platform-specific; adjust for your board.
 */

#define WDT_DEVICE_PATH        "/dev/watchdog0"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct wdt_status g_wdt;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: wdt_hw_start
 *
 * Description:
 *   Start the hardware watchdog with the configured timeout.
 *
 ****************************************************************************/

static int wdt_hw_start(void)
{
  struct watchdog_info_s info;
  int ret;

  g_wdt.fd = open(WDT_DEVICE_PATH, O_RDWR);
  if (g_wdt.fd < 0)
    {
      syslog(LOG_ERR, "[%s] Failed to open %s: %d\n",
             TAG, WDT_DEVICE_PATH, errno);
      return -errno;
    }

  /* Query watchdog info */

  ret = ioctl(g_wdt.fd, WDIOC_GETINFO, (unsigned long)&info);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "[%s] WDIOC_GETINFO failed: %d\n",
             TAG, errno);
    }
  else
    {
      syslog(LOG_INFO, "[%s] Watchdog: %s, timeout=%ds\n",
             TAG, info.name, info.timeout);
    }

  /* Set timeout */

  int timeout = WDT_HW_TIMEOUT_SEC;
  ret = ioctl(g_wdt.fd, WDIOC_SETTIMEOUT, (unsigned long)&timeout);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "[%s] SETTIMEOUT(%d) failed: %d\n",
             TAG, timeout, errno);
    }

  /* Start the watchdog */

  ret = ioctl(g_wdt.fd, WDIOC_START, 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] WDIOC_START failed: %d\n",
             TAG, errno);
      close(g_wdt.fd);
      g_wdt.fd = -1;
      return -errno;
    }

  g_wdt.hw_wdt_active = true;
  syslog(LOG_INFO, "[%s] Hardware watchdog started (timeout=%ds)\n",
         TAG, WDT_HW_TIMEOUT_SEC);

  return OK;
}

/****************************************************************************
 * Name: wdt_hw_feed
 *
 * Description:
 *   Feed (ping) the hardware watchdog.
 *
 ****************************************************************************/

static int wdt_hw_feed(void)
{
  int ret;

  if (g_wdt.fd < 0)
    {
      return -ENODEV;
    }

  ret = ioctl(g_wdt.fd, WDIOC_KEEPALIVE, 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] KEEPALIVE failed: %d\n",
             TAG, errno);
      return -errno;
    }

  g_wdt.total_feeds++;
  return OK;
}

/****************************************************************************
 * Name: wdt_hw_stop
 *
 * Description:
 *   Stop the hardware watchdog.  On NuttX, closing the device
 *   may or may not stop the WDT depending on the driver.
 *   We also send an explicit stop ioctl.
 *
 ****************************************************************************/

static void wdt_hw_stop(void)
{
  if (g_wdt.fd >= 0)
    {
      ioctl(g_wdt.fd, WDIOC_STOP, 0);
      close(g_wdt.fd);
      g_wdt.fd = -1;
    }

  g_wdt.hw_wdt_active = false;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: wdt_init
 ****************************************************************************/

int wdt_init(void)
{
  int ret;
  int i;

  memset(&g_wdt, 0, sizeof(g_wdt));
  g_wdt.fd = -1;

  for (i = 0; i < TASK_ID_COUNT; i++)
    {
      g_wdt.tasks[i].registered = false;
      g_wdt.tasks[i].heartbeat  = 0;
      g_wdt.tasks[i].last_heartbeat = 0;
      g_wdt.tasks[i].missed_checks = 0;
      g_wdt.tasks[i].name       = "unregistered";
    }

  ret = wdt_hw_start();
  if (ret < 0)
    {
      syslog(LOG_WARNING,
             "[%s] Hardware watchdog unavailable, "
             "health monitoring still active\n", TAG);
    }

  syslog(LOG_INFO, "[%s] Watchdog manager initialized\n", TAG);
  return OK;
}

/****************************************************************************
 * Name: wdt_deinit
 ****************************************************************************/

void wdt_deinit(void)
{
  wdt_hw_stop();
  syslog(LOG_INFO, "[%s] Watchdog manager deinitialized\n", TAG);
}

/****************************************************************************
 * Name: wdt_register_task
 ****************************************************************************/

int wdt_register_task(int task_id, const char *name)
{
  if (task_id < 0 || task_id >= TASK_ID_COUNT)
    {
      syslog(LOG_ERR, "[%s] Invalid task_id: %d\n", TAG, task_id);
      return -EINVAL;
    }

  g_wdt.tasks[task_id].registered     = true;
  g_wdt.tasks[task_id].heartbeat      = 0;
  g_wdt.tasks[task_id].last_heartbeat = 0;
  g_wdt.tasks[task_id].missed_checks  = 0;
  g_wdt.tasks[task_id].name           = name ? name : "unnamed";

  syslog(LOG_INFO, "[%s] Task registered: [%d] %s\n",
         TAG, task_id, g_wdt.tasks[task_id].name);

  return OK;
}

/****************************************************************************
 * Name: wdt_heartbeat
 ****************************************************************************/

void wdt_heartbeat(int task_id)
{
  if (task_id >= 0 && task_id < TASK_ID_COUNT)
    {
      g_wdt.tasks[task_id].heartbeat++;
    }
}

/****************************************************************************
 * Name: wdt_feed
 ****************************************************************************/

int wdt_feed(void)
{
  int ret;

  /* Check task health first */

  ret = wdt_check_health();
  if (ret < 0)
    {
      /* A task is hung — do NOT feed the watchdog.
       * Let it expire and trigger a reset.
       */

      syslog(LOG_CRIT,
             "[%s] NOT feeding WDT — task hung, "
             "reset imminent\n", TAG);
      return -EIO;
    }

  /* All tasks healthy — feed the hardware WDT */

  return wdt_hw_feed();
}

/****************************************************************************
 * Name: wdt_get_status
 ****************************************************************************/

void wdt_get_status(struct wdt_status *status)
{
  if (status != NULL)
    {
      memcpy(status, &g_wdt, sizeof(struct wdt_status));
    }
}

/****************************************************************************
 * Name: wdt_check_health
 ****************************************************************************/

int wdt_check_health(void)
{
  int i;
  int hung_task = -1;

  g_wdt.total_health_checks++;

  for (i = 0; i < TASK_ID_COUNT; i++)
    {
      if (!g_wdt.tasks[i].registered)
        {
          continue;
        }

      if (g_wdt.tasks[i].heartbeat == g_wdt.tasks[i].last_heartbeat)
        {
          /* No heartbeat increment since last check */

          g_wdt.tasks[i].missed_checks++;

          syslog(LOG_WARNING,
                 "[%s] Task [%d] %s: no heartbeat "
                 "(missed %d/%d)\n",
                 TAG, i, g_wdt.tasks[i].name,
                 g_wdt.tasks[i].missed_checks, WDT_MAX_MISSED);

          if (g_wdt.tasks[i].missed_checks >= WDT_MAX_MISSED)
            {
              hung_task = i;
            }
        }
      else
        {
          /* Task is alive — reset missed counter */

          g_wdt.tasks[i].missed_checks = 0;
        }

      g_wdt.tasks[i].last_heartbeat = g_wdt.tasks[i].heartbeat;
    }

  if (hung_task >= 0)
    {
      /* Log the fault with task ID and context */

      g_wdt.total_faults++;

      syslog(LOG_CRIT,
             "[%s] TASK HUNG: [%d] %s — "
             "triggering system reset\n",
             TAG, hung_task, g_wdt.tasks[hung_task].name);

      fault_report(FAULT_CODE_TASK_HUNG, hung_task,
                   g_wdt.tasks[hung_task].heartbeat);

      /* Mark the task as unregistered so we don't re-report */

      g_wdt.tasks[hung_task].registered = false;
      g_wdt.resets_triggered++;

      return -ETIMEDOUT;
    }

  return OK;
}

/****************************************************************************
 * Name: wdt_task
 *
 * Description:
 *   Watchdog management task.  Feeds the hardware WDT and checks
 *   task health every WDT_HEALTH_CHECK_SEC.
 *
 ****************************************************************************/

int wdt_task(int argc, char *argv[])
{
  int feed_counter = 0;

  syslog(LOG_INFO, "[%s] Watchdog task started "
         "(feed=%ds, health=%ds)\n",
         TAG, WDT_FEED_INTERVAL_SEC, WDT_HEALTH_CHECK_SEC);

  while (true)
    {
      sleep(WDT_FEED_INTERVAL_SEC);

      /* Feed the hardware watchdog (also checks health internally) */

      wdt_feed();

      feed_counter++;

      /* Periodic status logging (every 60s approx) */

      if (feed_counter % 12 == 0)
        {
          int i;
          for (i = 0; i < TASK_ID_COUNT; i++)
            {
              if (g_wdt.tasks[i].registered)
                {
                  syslog(LOG_DEBUG,
                         "[%s] Task [%d] %s: hb=%lu missed=%d\n",
                         TAG, i, g_wdt.tasks[i].name,
                         (unsigned long)g_wdt.tasks[i].heartbeat,
                         g_wdt.tasks[i].missed_checks);
                }
            }
        }
    }

  return OK;  /* unreachable */
}
