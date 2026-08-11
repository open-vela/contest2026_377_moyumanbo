/****************************************************************************
 * Unified Sensor Timestamp Synchronization System
 *
 * Hardware timer-backed microsecond clock for the VelaSense sensor suite.
 *
 * On NuttX / openvela we use clock_gettime(CLOCK_MONOTONIC) which on most
 * Cortex-M ports is backed by a cycle-counter or a dedicated hardware
 * timer (SysTick, DWT CYCCNT, or a chip-specific high-res timer).
 *
 * The init call records a reference epoch so that all subsequent
 * timestamps are relative offsets from a single fixed point, keeping
 * them compact for 32-bit delta encoding in ring buffer metadata.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <syslog.h>

#include "sensor_sync.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SENSOR_SYNC_TAG   "sensor_sync"

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Reference epoch captured at init (microseconds since boot) */

static uint64_t g_epoch_us;

/* Set to true once sensor_sync_init() succeeds */

static int g_sync_initialized;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sensor_sync_now_raw
 *
 * Description:
 *   Read the monotonic hardware clock and convert to microseconds.
 *
 ****************************************************************************/

static uint64_t sensor_sync_now_raw(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);

  return (uint64_t)ts.tv_sec * SENSOR_SYNC_US_PER_SEC +
         (uint64_t)ts.tv_nsec / 1000;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int sensor_sync_init(void)
{
  if (g_sync_initialized)
    {
      syslog(LOG_WARNING, "[%s] Already initialized\n", SENSOR_SYNC_TAG);
      return OK;
    }

  /* Record the reference epoch.
   * All subsequent timestamps will be relative to this point.
   * Using a small positive offset avoids returning zero timestamps
   * for the very first samples after init.
   */

  g_epoch_us = sensor_sync_now_raw();

  g_sync_initialized = 1;

  syslog(LOG_INFO, "[%s] Initialized — epoch set at %llu us\n",
         SENSOR_SYNC_TAG, (unsigned long long)g_epoch_us);

  return OK;
}

uint64_t sensor_sync_get_us(void)
{
  if (!g_sync_initialized)
    {
      /* Not yet initialized — return raw monotonic time.
       * This allows very early boot diagnostics to still work.
       */

      return sensor_sync_now_raw();
    }

  return sensor_sync_now_raw() - g_epoch_us;
}

uint64_t sensor_sync_get_ms(void)
{
  return sensor_sync_get_us() / SENSOR_SYNC_US_PER_MS;
}

uint64_t sensor_sync_correct(uint64_t raw_us,
                             const struct sensor_timestamp_corr *corr)
{
  if (!corr)
    {
      return raw_us;
    }

  /* Total delay = bus transaction time + ISR-to-read latency.
   * We subtract this because the measurement was actually taken
   * (bus read register latched) before we captured the timestamp.
   */

  int32_t total_delay = corr->bus_delay_us + corr->irq_to_read_us;

  if (total_delay <= 0)
    {
      /* No correction needed or invalid values */

      return raw_us;
    }

  /* Guard against underflow (shouldn't happen in practice) */

  if (raw_us < (uint64_t)total_delay)
    {
      return 0;
    }

  return raw_us - (uint64_t)total_delay;
}

uint64_t sensor_sync_elapsed_us(uint64_t start_us, uint64_t end_us)
{
  if (end_us >= start_us)
    {
      return end_us - start_us;
    }

  /* Wrap-around case (theoretical for uint64_t but kept for correctness) */

  return UINT64_MAX - start_us + end_us + 1;
}
