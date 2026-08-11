/****************************************************************************
 * VelaSense Watchdog Manager
 *
 * Hardware watchdog management and per-task health monitoring.
 *
 * The watchdog is fed every 5 seconds from the main task.  Each
 * application task reports a heartbeat counter; the watchdog task
 * checks all counters every 10 seconds and resets the system if
 * any task misses 3 consecutive checks.
 *
 * Fault information is logged before reset via the fault handler.
 ****************************************************************************/

#ifndef __FIRMWARE_APPS_VELASENSE_WATCHDOG_H
#define __FIRMWARE_APPS_VELASENSE_WATCHDOG_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <nuttx/config.h>

#include "fault_handler.h"    /* For TASK_ID_* and fault_code */

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Watchdog configuration */

#define WDT_HW_TIMEOUT_SEC      10   /* Hardware watchdog timeout */
#define WDT_FEED_INTERVAL_SEC   5    /* How often we feed */
#define WDT_HEALTH_CHECK_SEC    10   /* How often we check task health */
#define WDT_MAX_MISSED          3    /* Missed checks before fault */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Per-task health information */

struct wdt_task_info
{
  uint32_t    heartbeat;         /* Current heartbeat counter */
  uint32_t    last_heartbeat;    /* Counter at last health check */
  int         missed_checks;     /* Consecutive missed checks */
  bool        registered;        /* Task has registered */
  const char *name;              /* Task name string */
};

/* Watchdog status */

struct wdt_status
{
  bool        hw_wdt_active;                /* Hardware WDT is running */
  int         fd;                           /* WDT file descriptor */
  uint32_t    total_feeds;                  /* Total feed count */
  uint32_t    total_health_checks;          /* Total health checks */
  uint32_t    total_faults;                 /* Tasks that triggered faults */
  uint32_t    resets_triggered;             /* Resets due to hung tasks */
  struct wdt_task_info tasks[TASK_ID_COUNT]; /* Per-task info */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: wdt_init
 *
 * Description:
 *   Initialize the watchdog subsystem.  Opens the hardware watchdog
 *   device and starts the feed timer.  Must be called once before
 *   any other wdt_* function.
 *
 * Returned Value:
 *   OK on success, negative errno on failure.
 *
 ****************************************************************************/

int wdt_init(void);

/****************************************************************************
 * Name: wdt_deinit
 *
 * Description:
 *   Disable the hardware watchdog and release resources.
 *   The watchdog will NOT reset the system after this call.
 *
 ****************************************************************************/

void wdt_deinit(void);

/****************************************************************************
 * Name: wdt_register_task
 *
 * Description:
 *   Register a task for health monitoring.  Each task must call this
 *   once at startup with its assigned task ID.
 *
 * Input Parameters:
 *   task_id - One of TASK_ID_* constants.
 *   name    - Human-readable task name.
 *
 * Returned Value:
 *   OK on success, -EINVAL if task_id is out of range.
 *
 ****************************************************************************/

int wdt_register_task(int task_id, const char *name);

/****************************************************************************
 * Name: wdt_heartbeat
 *
 * Description:
 *   Called by a task to indicate it is alive.  Increments the task's
 *   heartbeat counter.  Should be called at least once per
 *   WDT_HEALTH_CHECK_SEC period.
 *
 * Input Parameters:
 *   task_id - The task's registered ID.
 *
 ****************************************************************************/

void wdt_heartbeat(int task_id);

/****************************************************************************
 * Name: wdt_feed
 *
 * Description:
 *   Feed the hardware watchdog.  Called from the main task every
 *   WDT_FEED_INTERVAL_SEC.  Only feeds if all tasks are healthy.
 *   If any task is hung, logs the fault and lets the WDT expire
 *   to trigger a system reset.
 *
 * Returned Value:
 *   OK if fed, -EIO if a task is hung (WDT not fed).
 *
 ****************************************************************************/

int wdt_feed(void);

/****************************************************************************
 * Name: wdt_get_status
 *
 * Description:
 *   Return current watchdog status including per-task health info.
 *
 * Output Parameters:
 *   status - Filled with current watchdog state.
 *
 ****************************************************************************/

void wdt_get_status(struct wdt_status *status);

/****************************************************************************
 * Name: wdt_check_health
 *
 * Description:
 *   Check all registered tasks for liveness.  If any task has not
 *   incremented its heartbeat since the last check, increment its
 *   missed-check counter.  If a task exceeds WDT_MAX_MISSED, log
 *   a fault and return an error.
 *
 * Returned Value:
 *   OK if all tasks healthy, -ETIMEDOUT if a task is hung.
 *
 ****************************************************************************/

int wdt_check_health(void);

/****************************************************************************
 * Name: wdt_task
 *
 * Description:
 *   Watchdog management task entry point.  Feeds the hardware WDT
 *   and checks task health periodically.  Intended to run as a
 *   dedicated kthread.
 *
 ****************************************************************************/

int wdt_task(int argc, char *argv[]);

#endif /* __FIRMWARE_APPS_VELASENSE_WATCHDOG_H */
