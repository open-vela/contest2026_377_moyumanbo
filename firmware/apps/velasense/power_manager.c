/****************************************************************************
 * VelaSense Power Manager Implementation
 *
 * Power state machine with battery monitoring via MAX17048.
 *
 * State transitions are driven by smoothed battery SOC and logged as
 * fault events.  The exponential moving average avoids false triggers
 * from transient voltage dips under high-current draw.
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
#include <nuttx/i2c/i2c_master.h>

#include "power_manager.h"
#include "fault_handler.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TAG                     "pwr"

/* Battery capacity for runtime estimation (mAh) */

#define BATTERY_CAPACITY_MAH    200

/* Debounce: require N consecutive readings below threshold before
 * transitioning.  Prevents oscillation at boundaries.
 */

#define TRANSITION_DEBOUNCE     3

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Internal power manager state */

struct pwr_manager_s
{
  int               i2c_fd;              /* I2C file descriptor */
  enum pwr_state    state;               /* Current power state */
  enum pwr_state    forced_state;        /* Forced state (or COUNT = none) */
  int               soc_smooth_x1000;   /* Smoothed SOC * 1000 */
  int               soc_raw;            /* Last raw SOC */
  int               voltage_mv;         /* Last voltage reading */
  bool              charger_present;     /* Charger detected */
  bool              initialized;

  /* Debounce counters for downward transitions */

  int               low_debounce;       /* Consecutive readings below LOW */
  int               critical_debounce;  /* Consecutive readings below CRIT */
  int               shutdown_debounce;  /* Consecutive readings below SHUT */

  /* Timestamp tracking */

  time_t            last_state_change;
  struct pwr_stats  stats;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct pwr_manager_s g_pwr;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: read_max17048_register
 *
 * Description:
 *   Read a 16-bit register from the MAX17048 over I2C.
 *
 ****************************************************************************/

static int read_max17048_register(int fd, uint8_t reg, uint16_t *value)
{
  struct i2c_msg_s          msgs[2];
  struct i2c_transfer_s     xfer;
  uint8_t                   reg_buf[1];
  uint8_t                   data[2];
  int                       ret;

  /* Write register address */

  reg_buf[0] = reg;
  msgs[0].frequency = 400000;
  msgs[0].addr      = MAX17048_I2C_ADDR;
  msgs[0].flags     = I2C_M_NOSTOP;
  msgs[0].buffer    = reg_buf;
  msgs[0].length    = 1;

  /* Read 2 bytes */

  msgs[1].frequency = 400000;
  msgs[1].addr      = MAX17048_I2C_ADDR;
  msgs[1].flags     = I2C_M_READ;
  msgs[1].buffer    = data;
  msgs[1].length    = 2;

  xfer.msgc = 2;
  xfer.msgv = msgs;

  ret = ioctl(fd, I2C_TRANSFER, (unsigned long)&xfer);
  if (ret < 0)
    {
      return ret;
    }

  /* MAX17048 returns MSB first */

  *value = ((uint16_t)data[0] << 8) | data[1];
  return OK;
}

/****************************************************************************
 * Name: read_max17048_soc
 *
 * Description:
 *   Read battery state-of-charge from MAX17048.
 *   Register format: high byte = integer %, low byte = fractional / 256.
 *
 ****************************************************************************/

static int read_max17048_soc(int fd, int *soc_percent)
{
  uint16_t raw;
  int      ret;

  ret = read_max17048_register(fd, MAX17048_REG_SOC, &raw);
  if (ret < 0)
    {
      return ret;
    }

  /* Integer portion is the high byte */

  *soc_percent = (raw >> 8) & 0xff;
  if (*soc_percent > 100)
    {
      *soc_percent = 100;
    }

  return OK;
}

/****************************************************************************
 * Name: read_max17048_voltage
 *
 * Description:
 *   Read cell voltage from MAX17048.
 *   Register format: 16-bit, each count = 78.125 uV (1.25mV / 16).
 *
 ****************************************************************************/

static int read_max17048_voltage(int fd, int *voltage_mv)
{
  uint16_t raw;
  int      ret;

  ret = read_max17048_register(fd, MAX17048_REG_VCELL, &raw);
  if (ret < 0)
    {
      return ret;
    }

  /* Each LSB = 78.125 uV → multiply by 78125 / 1000000 = 0.078125
   * In integer: raw * 78125 / 1000000 = raw * 1250 / 16000
   * Simplified: raw * 5 / 64 gives mV (close enough for display)
   */

  *voltage_mv = (int)((uint32_t)raw * 78125 / 1000000);
  return OK;
}

/****************************************************************************
 * Name: determine_state
 *
 * Description:
 *   Determine the appropriate power state from the smoothed SOC.
 *   Returns the target state; caller applies debounce.
 *
 ****************************************************************************/

static enum pwr_state determine_state(int soc_smooth)
{
  if (soc_smooth < PWR_SOC_SHUTDOWN_THRESH)
    {
      return PWR_STATE_SHUTDOWN;
    }
  else if (soc_smooth < PWR_SOC_CRITICAL_THRESH)
    {
      return PWR_STATE_CRITICAL;
    }
  else if (soc_smooth < PWR_SOC_LOW_THRESH)
    {
      return PWR_STATE_LOW_POWER;
    }
  else
    {
      return PWR_STATE_IDLE;  /* IDLE by default; ACTIVE set by user activity */
    }
}

/****************************************************************************
 * Name: apply_state_transition
 *
 * Description:
 *   Perform a power state transition.  Logs the event and updates
 *   statistics.
 *
 ****************************************************************************/

static void apply_state_transition(enum pwr_state new_state)
{
  time_t now;
  time(&now);

  syslog(LOG_NOTICE, "[%s] State transition: %s -> %s (SOC=%d%%)\n",
         TAG,
         pwr_get_state_name(g_pwr.state),
         pwr_get_state_name(new_state),
         g_pwr.soc_smooth_x1000 / 1000);

  /* Track time in previous state */

  if (g_pwr.last_state_change > 0)
    {
      g_pwr.stats.state_durations[g_pwr.state] +=
          (uint32_t)(now - g_pwr.last_state_change);
    }

  /* Log critical transitions as fault events */

  if (new_state == PWR_STATE_LOW_POWER)
    {
      g_pwr.stats.low_battery_events++;
      fault_report(FAULT_CODE_LOW_BATTERY, TASK_ID_POWER,
                   (uint32_t)g_pwr.soc_smooth_x1000 / 1000);
    }
  else if (new_state == PWR_STATE_CRITICAL)
    {
      g_pwr.stats.critical_battery_events++;
      fault_report(FAULT_CODE_CRITICAL_BATTERY, TASK_ID_POWER,
                   (uint32_t)g_pwr.soc_smooth_x1000 / 1000);
    }
  else if (new_state == PWR_STATE_SHUTDOWN)
    {
      fault_report(FAULT_CODE_SHUTDOWN_IMMINENT, TASK_ID_POWER,
                   (uint32_t)g_pwr.soc_smooth_x1000 / 1000);
    }

  g_pwr.state = new_state;
  g_pwr.last_state_change = now;
  g_pwr.stats.state_transitions++;
}

/****************************************************************************
 * Name: check_state_transition
 *
 * Description:
 *   Evaluate whether a state transition should occur.  Uses debounce
 *   counters to avoid oscillation at threshold boundaries.
 *   Downward transitions (toward SHUTDOWN) require multiple consecutive
 *   readings.  Upward transitions (toward ACTIVE) are immediate.
 *
 ****************************************************************************/

static void check_state_transition(void)
{
  enum pwr_state target;
  int            soc;

  /* If state is forced, skip automatic transitions */

  if (g_pwr.forced_state < PWR_STATE_COUNT)
    {
      if (g_pwr.state != g_pwr.forced_state)
        {
          apply_state_transition(g_pwr.forced_state);
        }

      return;
    }

  soc = g_pwr.soc_smooth_x1000 / 1000;
  target = determine_state(soc);

  /* Handle debounce for downward transitions */

  if (target > g_pwr.state)
    {
      /* Moving toward a lower-power state — debounce required */

      if (target >= PWR_STATE_LOW_POWER &&
          g_pwr.state < PWR_STATE_LOW_POWER)
        {
          if (++g_pwr.low_debounce >= TRANSITION_DEBOUNCE)
            {
              g_pwr.low_debounce = 0;
              apply_state_transition(PWR_STATE_LOW_POWER);
            }
        }
      else if (target >= PWR_STATE_CRITICAL &&
               g_pwr.state < PWR_STATE_CRITICAL)
        {
          if (++g_pwr.critical_debounce >= TRANSITION_DEBOUNCE)
            {
              g_pwr.critical_debounce = 0;
              apply_state_transition(PWR_STATE_CRITICAL);
            }
        }
      else if (target >= PWR_STATE_SHUTDOWN)
        {
          if (++g_pwr.shutdown_debounce >= TRANSITION_DEBOUNCE)
            {
              g_pwr.shutdown_debounce = 0;
              apply_state_transition(PWR_STATE_SHUTDOWN);
            }
        }
    }
  else if (target < g_pwr.state)
    {
      /* Moving toward a higher-power state — immediate transition
       * but only if SOC is clearly above the lower threshold.
       * This provides hysteresis.
       */

      bool can_upgrade = false;

      switch (g_pwr.state)
        {
          case PWR_STATE_LOW_POWER:
            can_upgrade = (soc > PWR_SOC_LOW_THRESH + 3);
            break;
          case PWR_STATE_CRITICAL:
            can_upgrade = (soc > PWR_SOC_CRITICAL_THRESH + 3);
            break;
          case PWR_STATE_SHUTDOWN:
            can_upgrade = (soc > PWR_SOC_SHUTDOWN_THRESH + 3);
            break;
          default:
            break;
        }

      if (can_upgrade)
        {
          /* Reset debounce counters on upward transition */

          g_pwr.low_debounce     = 0;
          g_pwr.critical_debounce = 0;
          g_pwr.shutdown_debounce = 0;
          apply_state_transition(target);
        }
    }

  /* If in IDLE and no sensor-detected motion/activity for a while,
   * the main task can call pwr_force_state(PWR_STATE_IDLE) explicitly.
   * ACTIVE state is entered via pwr_force_state() when events are
   * being actively processed.
   */
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pwr_manager_init
 ****************************************************************************/

int pwr_manager_init(void)
{
  memset(&g_pwr, 0, sizeof(g_pwr));
  g_pwr.forced_state   = PWR_STATE_COUNT;  /* no forced state */
  g_pwr.state          = PWR_STATE_IDLE;
  g_pwr.last_state_change = 0;

  /* Open I2C bus to fuel gauge */

  g_pwr.i2c_fd = open("/dev/i2c-0", O_RDWR);
  if (g_pwr.i2c_fd < 0)
    {
      syslog(LOG_ERR, "[%s] Failed to open I2C bus: %d\n",
             TAG, errno);

      /* Continue without fuel gauge — assume 50% battery */

      g_pwr.soc_smooth_x1000 = 50000;
      g_pwr.soc_raw          = 50;
      g_pwr.voltage_mv       = 3700;
    }
  else
    {
      /* Read initial battery state */

      int soc;
      int mv;
      int ret;

      ret = read_max17048_soc(g_pwr.i2c_fd, &soc);
      if (ret < 0)
        {
          syslog(LOG_WARNING, "[%s] Initial SOC read failed: %d\n",
                 TAG, ret);
          soc = 50;
        }

      ret = read_max17048_voltage(g_pwr.i2c_fd, &mv);
      if (ret < 0)
        {
          mv = 3700;
        }

      g_pwr.soc_raw          = soc;
      g_pwr.soc_smooth_x1000 = soc * 1000;
      g_pwr.voltage_mv       = mv;

      syslog(LOG_INFO, "[%s] Fuel gauge: SOC=%d%% V=%dmV\n",
             TAG, soc, mv);
    }

  /* Set initial state based on SOC */

  g_pwr.state = determine_state(g_pwr.soc_smooth_x1000 / 1000);
  g_pwr.initialized = true;

  syslog(LOG_INFO, "[%s] Power manager initialized, state=%s\n",
         TAG, pwr_get_state_name(g_pwr.state));

  return OK;
}

/****************************************************************************
 * Name: pwr_manager_deinit
 ****************************************************************************/

void pwr_manager_deinit(void)
{
  if (g_pwr.i2c_fd >= 0)
    {
      close(g_pwr.i2c_fd);
      g_pwr.i2c_fd = -1;
    }

  g_pwr.initialized = false;
}

/****************************************************************************
 * Name: pwr_get_state
 ****************************************************************************/

enum pwr_state pwr_get_state(void)
{
  return g_pwr.state;
}

/****************************************************************************
 * Name: pwr_get_state_name
 ****************************************************************************/

const char *pwr_get_state_name(enum pwr_state state)
{
  static const char *names[] =
  {
    "ACTIVE",
    "IDLE",
    "LOW_POWER",
    "CRITICAL",
    "SHUTDOWN",
    "UNKNOWN"
  };

  if (state < PWR_STATE_COUNT)
    {
      return names[state];
    }

  return names[PWR_STATE_COUNT];
}

/****************************************************************************
 * Name: pwr_get_battery_info
 ****************************************************************************/

int pwr_get_battery_info(struct pwr_battery_info *info)
{
  if (info == NULL)
    {
      return -EINVAL;
    }

  info->soc_raw        = g_pwr.soc_raw;
  info->soc_smooth     = g_pwr.soc_smooth_x1000 / 1000;
  info->voltage_mv     = g_pwr.voltage_mv;
  info->charger_present = g_pwr.charger_present;
  info->valid          = (g_pwr.i2c_fd >= 0);

  return OK;
}

/****************************************************************************
 * Name: pwr_get_sensor_rates
 ****************************************************************************/

void pwr_get_sensor_rates(struct pwr_sensor_rates *rates)
{
  if (rates == NULL)
    {
      return;
    }

  switch (g_pwr.state)
    {
      case PWR_STATE_ACTIVE:
        rates->ppg_hz            = PWR_PPG_RATE_ACTIVE;
        rates->imu_hz            = PWR_IMU_RATE_ACTIVE;
        rates->eda_hz            = PWR_EDA_RATE_ACTIVE;
        rates->infer_interval_sec = PWR_INFER_INTERVAL_ACTIVE;
        break;

      case PWR_STATE_IDLE:
        rates->ppg_hz            = PWR_PPG_RATE_IDLE;
        rates->imu_hz            = PWR_IMU_RATE_IDLE;
        rates->eda_hz            = PWR_EDA_RATE_IDLE;
        rates->infer_interval_sec = PWR_INFER_INTERVAL_IDLE;
        break;

      case PWR_STATE_LOW_POWER:
        rates->ppg_hz            = PWR_PPG_RATE_LOW;
        rates->imu_hz            = PWR_IMU_RATE_LOW;
        rates->eda_hz            = PWR_EDA_RATE_LOW;
        rates->infer_interval_sec = PWR_INFER_INTERVAL_LOW;
        break;

      case PWR_STATE_CRITICAL:
        rates->ppg_hz            = PWR_PPG_RATE_CRITICAL;
        rates->imu_hz            = PWR_IMU_RATE_CRITICAL;
        rates->eda_hz            = PWR_EDA_RATE_CRITICAL;
        rates->infer_interval_sec = PWR_INFER_INTERVAL_CRITICAL;
        break;

      default:
        rates->ppg_hz            = 0;
        rates->imu_hz            = 0;
        rates->eda_hz            = 0;
        rates->infer_interval_sec = PWR_INFER_INTERVAL_CRITICAL;
        break;
    }
}

/****************************************************************************
 * Name: pwr_get_stats
 ****************************************************************************/

void pwr_get_stats(struct pwr_stats *stats)
{
  if (stats != NULL)
    {
      memcpy(stats, &g_pwr.stats, sizeof(struct pwr_stats));
    }
}

/****************************************************************************
 * Name: pwr_is_lcd_enabled
 ****************************************************************************/

bool pwr_is_lcd_enabled(void)
{
  return (g_pwr.state == PWR_STATE_ACTIVE);
}

/****************************************************************************
 * Name: pwr_is_ble_enabled
 ****************************************************************************/

bool pwr_is_ble_enabled(void)
{
  return (g_pwr.state == PWR_STATE_ACTIVE ||
          g_pwr.state == PWR_STATE_IDLE   ||
          g_pwr.state == PWR_STATE_LOW_POWER);
}

/****************************************************************************
 * Name: pwr_is_haptic_enabled
 ****************************************************************************/

bool pwr_is_haptic_enabled(void)
{
  return (g_pwr.state == PWR_STATE_ACTIVE ||
          g_pwr.state == PWR_STATE_IDLE);
}

/****************************************************************************
 * Name: pwr_force_state
 ****************************************************************************/

void pwr_force_state(enum pwr_state state)
{
  if (state < PWR_STATE_COUNT)
    {
      syslog(LOG_INFO, "[%s] Force state: %s\n",
             TAG, pwr_get_state_name(state));
      g_pwr.forced_state = state;
      apply_state_transition(state);
    }
  else
    {
      /* Clear forced state, return to automatic */

      syslog(LOG_INFO, "[%s] Clear forced state, returning to auto\n",
             TAG);
      g_pwr.forced_state = PWR_STATE_COUNT;
    }
}

/****************************************************************************
 * Name: pwr_get_estimated_current_ma
 ****************************************************************************/

int pwr_get_estimated_current_ma(void)
{
  switch (g_pwr.state)
    {
      case PWR_STATE_ACTIVE:     return PWR_CURRENT_ACTIVE_MA;
      case PWR_STATE_IDLE:       return PWR_CURRENT_IDLE_MA;
      case PWR_STATE_LOW_POWER:  return PWR_CURRENT_LOW_MA;
      case PWR_STATE_CRITICAL:   return PWR_CURRENT_CRITICAL_MA;
      default:                   return PWR_CURRENT_CRITICAL_MA;
    }
}

/****************************************************************************
 * Name: pwr_get_estimated_runtime_hours
 ****************************************************************************/

int pwr_get_estimated_runtime_hours(void)
{
  int current_ma;
  int soc;

  current_ma = pwr_get_estimated_current_ma();
  if (current_ma <= 0)
    {
      return 0;
    }

  soc = g_pwr.soc_smooth_x1000 / 1000;
  return (BATTERY_CAPACITY_MAH * soc) / (current_ma * 100);
}

/****************************************************************************
 * Name: pwr_battery_task
 *
 * Description:
 *   Battery monitoring task.  Runs every PWR_BATTERY_POLL_SEC.
 *   Reads the MAX17048, applies exponential smoothing, and checks
 *   for state transitions.
 *
 ****************************************************************************/

int pwr_battery_task(int argc, char *argv[])
{
  int soc;
  int mv;
  int ret;

  syslog(LOG_INFO, "[%s] Battery task started (poll=%ds, alpha=%d/1000)\n",
         TAG, PWR_BATTERY_POLL_SEC, PWR_SMOOTH_ALPHA_X1000);

  while (true)
    {
      if (g_pwr.i2c_fd >= 0)
        {
          /* Read fuel gauge */

          ret = read_max17048_soc(g_pwr.i2c_fd, &soc);
          if (ret < 0)
            {
              syslog(LOG_WARNING, "[%s] SOC read failed: %d\n",
                     TAG, ret);
              goto next_poll;
            }

          ret = read_max17048_voltage(g_pwr.i2c_fd, &mv);
          if (ret < 0)
            {
              syslog(LOG_WARNING, "[%s] Voltage read failed: %d\n",
                     TAG, ret);
              mv = g_pwr.voltage_mv;  /* keep previous */
            }

          g_pwr.soc_raw    = soc;
          g_pwr.voltage_mv = mv;
          g_pwr.stats.battery_reads++;

          /* Exponential moving average:
           *   smoothed = smoothed + alpha * (raw - smoothed)
           *   In fixed-point (x1000):
           *   smoothed += alpha * (raw*1000 - smoothed) / 1000
           */

          g_pwr.soc_smooth_x1000 +=
              PWR_SMOOTH_ALPHA_X1000 *
              (soc * 1000 - g_pwr.soc_smooth_x1000) / 1000;

          /* Clamp */

          if (g_pwr.soc_smooth_x1000 < 0)
            {
              g_pwr.soc_smooth_x1000 = 0;
            }
          else if (g_pwr.soc_smooth_x1000 > 100000)
            {
              g_pwr.soc_smooth_x1000 = 100000;
            }

          /* Check for charger insertion (voltage > 4.2V suggests USB) */

          g_pwr.charger_present = (mv > 4200);

          /* Evaluate state transitions */

          check_state_transition();

          /* If in SHUTDOWN, trigger deep sleep */

          if (g_pwr.state == PWR_STATE_SHUTDOWN)
            {
              syslog(LOG_CRIT,
                     "[%s] SHUTDOWN: saving state and entering deep sleep\n",
                     TAG);

              /* TODO: Save critical state to Flash
               * TODO: board_shutdown() or board_sleep()
               */

              /* For now, just block indefinitely */

              while (true)
                {
                  sleep(3600);
                }
            }
        }

next_poll:
      sleep(PWR_BATTERY_POLL_SEC);
    }

  return OK;  /* unreachable */
}
