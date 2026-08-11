/****************************************************************************
 * VelaSense Power Manager
 *
 * Power state machine for wrist-worn battery-operated operation.
 *
 * Power states:
 *   ACTIVE    — full operation, all sensors + LCD + BLE connected
 *   IDLE      — no active events, LCD off, BLE advertising
 *   LOW_POWER — battery < 20%, reduced sensor rates
 *   CRITICAL  — battery < 5%, PPG only, BLE off
 *   SHUTDOWN  — battery < 2%, save state and deep sleep
 *
 * Battery monitoring via MAX17048 fuel gauge over I2C.
 * Exponential smoothing prevents false low-battery triggers from
 * transient voltage dips under load.
 ****************************************************************************/

#ifndef __FIRMWARE_APPS_VELASENSE_POWER_MANAGER_H
#define __FIRMWARE_APPS_VELASENSE_POWER_MANAGER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Battery SOC thresholds (percent) */

#define PWR_SOC_ACTIVE_THRESH       100  /* Always active above LOW threshold */
#define PWR_SOC_LOW_THRESH          20   /* Enter LOW_POWER below this */
#define PWR_SOC_CRITICAL_THRESH     5    /* Enter CRITICAL below this */
#define PWR_SOC_SHUTDOWN_THRESH     2    /* Enter SHUTDOWN below this */

/* Battery monitoring interval */

#define PWR_BATTERY_POLL_SEC        60   /* Read fuel gauge every 60s */

/* Exponential smoothing factor (x1000).
 * alpha = 0.1 → heavy smoothing, resistant to voltage dips.
 */

#define PWR_SMOOTH_ALPHA_X1000      100

/* Sensor sample rates per power state (Hz) */

#define PWR_PPG_RATE_ACTIVE         100
#define PWR_PPG_RATE_IDLE           100
#define PWR_PPG_RATE_LOW            50
#define PWR_PPG_RATE_CRITICAL       25

#define PWR_IMU_RATE_ACTIVE         100
#define PWR_IMU_RATE_IDLE           100
#define PWR_IMU_RATE_LOW            50
#define PWR_IMU_RATE_CRITICAL       0    /* off */

#define PWR_EDA_RATE_ACTIVE         32
#define PWR_EDA_RATE_IDLE           32
#define PWR_EDA_RATE_LOW            0    /* off */
#define PWR_EDA_RATE_CRITICAL       0    /* off */

/* Inference interval per power state (seconds) */

#define PWR_INFER_INTERVAL_ACTIVE   5
#define PWR_INFER_INTERVAL_IDLE     5
#define PWR_INFER_INTERVAL_LOW      30
#define PWR_INFER_INTERVAL_CRITICAL 60

/* Estimated current draw per state (mA) */

#define PWR_CURRENT_ACTIVE_MA       50
#define PWR_CURRENT_IDLE_MA         30
#define PWR_CURRENT_LOW_MA          15
#define PWR_CURRENT_CRITICAL_MA     5

/* MAX17048 I2C address and registers */

#define MAX17048_I2C_ADDR           0x36
#define MAX17048_REG_VCELL          0x02
#define MAX17048_REG_SOC            0x04
#define MAX17048_REG_MODE           0x06
#define MAX17048_REG_VERSION        0x08
#define MAX17048_REG_CONFIG         0x0c
#define MAX17048_REG_COMMAND        0xfe

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Power states */

enum pwr_state
{
  PWR_STATE_ACTIVE = 0,     /* Full operation */
  PWR_STATE_IDLE,           /* No active events, LCD off */
  PWR_STATE_LOW_POWER,      /* Battery < 20%, reduced rates */
  PWR_STATE_CRITICAL,       /* Battery < 5%, minimal */
  PWR_STATE_SHUTDOWN,       /* Battery < 2%, deep sleep */
  PWR_STATE_COUNT           /* Number of states */
};

/* Battery information */

struct pwr_battery_info
{
  int     soc_raw;          /* Raw SOC from fuel gauge (0-100) */
  int     soc_smooth;       /* Smoothed SOC (0-100) */
  int     voltage_mv;       /* Cell voltage in mV */
  bool    charger_present;  /* USB charger detected */
  bool    valid;            /* Gauge data is valid */
};

/* Sensor rate configuration for the current power state */

struct pwr_sensor_rates
{
  int     ppg_hz;
  int     imu_hz;
  int     eda_hz;
  int     infer_interval_sec;
};

/* Power manager statistics */

struct pwr_stats
{
  uint32_t state_durations[PWR_STATE_COUNT]; /* Seconds in each state */
  uint32_t state_transitions;                /* Total transitions */
  uint32_t battery_reads;                    /* Fuel gauge reads */
  uint32_t low_battery_events;               /* Times LOW_POWER entered */
  uint32_t critical_battery_events;          /* Times CRITICAL entered */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: pwr_manager_init
 *
 * Description:
 *   Initialize the power manager.  Opens the I2C bus to the MAX17048
 *   fuel gauge, reads initial battery state, and sets the starting
 *   power state.  Must be called once before any other pwr_* function.
 *
 * Returned Value:
 *   OK on success, negative errno on failure.
 *
 ****************************************************************************/

int pwr_manager_init(void);

/****************************************************************************
 * Name: pwr_manager_deinit
 *
 * Description:
 *   Release power manager resources (close I2C file descriptor).
 *
 ****************************************************************************/

void pwr_manager_deinit(void);

/****************************************************************************
 * Name: pwr_get_state
 *
 * Description:
 *   Return the current power state.
 *
 ****************************************************************************/

enum pwr_state pwr_get_state(void);

/****************************************************************************
 * Name: pwr_get_state_name
 *
 * Description:
 *   Return a human-readable string for the given power state.
 *
 ****************************************************************************/

const char *pwr_get_state_name(enum pwr_state state);

/****************************************************************************
 * Name: pwr_get_battery_info
 *
 * Description:
 *   Fill in the current battery information structure.
 *
 * Output Parameters:
 *   info - Filled with current battery state.
 *
 * Returned Value:
 *   OK on success, negative errno on failure.
 *
 ****************************************************************************/

int pwr_get_battery_info(struct pwr_battery_info *info);

/****************************************************************************
 * Name: pwr_get_sensor_rates
 *
 * Description:
 *   Return the sensor rate configuration for the current power state.
 *
 * Output Parameters:
 *   rates - Filled with current sensor rates.
 *
 ****************************************************************************/

void pwr_get_sensor_rates(struct pwr_sensor_rates *rates);

/****************************************************************************
 * Name: pwr_get_stats
 *
 * Description:
 *   Return power manager statistics.
 *
 * Output Parameters:
 *   stats - Filled with current statistics.
 *
 ****************************************************************************/

void pwr_get_stats(struct pwr_stats *stats);

/****************************************************************************
 * Name: pwr_is_lcd_enabled
 *
 * Description:
 *   Query whether the LCD should be active in the current power state.
 *
 * Returned Value:
 *   true if LCD should be on, false otherwise.
 *
 ****************************************************************************/

bool pwr_is_lcd_enabled(void);

/****************************************************************************
 * Name: pwr_is_ble_enabled
 *
 * Description:
 *   Query whether BLE should be active in the current power state.
 *
 * Returned Value:
 *   true if BLE should be on, false otherwise.
 *
 ****************************************************************************/

bool pwr_is_ble_enabled(void);

/****************************************************************************
 * Name: pwr_is_haptic_enabled
 *
 * Description:
 *   Query whether haptic feedback is allowed in the current power state.
 *
 * Returned Value:
 *   true if haptic is allowed, false otherwise.
 *
 ****************************************************************************/

bool pwr_is_haptic_enabled(void);

/****************************************************************************
 * Name: pwr_force_state
 *
 * Description:
 *   Force a specific power state (for testing or user override).
 *   The normal battery-based transitions resume once the battery
 *   level crosses a threshold boundary.
 *
 * Input Parameters:
 *   state - The state to force.
 *
 ****************************************************************************/

void pwr_force_state(enum pwr_state state);

/****************************************************************************
 * Name: pwr_get_estimated_current_ma
 *
 * Description:
 *   Return estimated current draw in mA for the current power state.
 *
 ****************************************************************************/

int pwr_get_estimated_current_ma(void);

/****************************************************************************
 * Name: pwr_get_estimated_runtime_hours
 *
 * Description:
 *   Estimate remaining runtime in hours based on smoothed SOC,
 *   assuming a typical 200mAh wrist-wear battery.
 *
 * Returned Value:
 *   Estimated hours remaining (integer).
 *
 ****************************************************************************/

int pwr_get_estimated_runtime_hours(void);

/****************************************************************************
 * Name: pwr_battery_task
 *
 * Description:
 *   Battery monitoring task entry point.  Reads the MAX17048 fuel gauge
 *   periodically, applies exponential smoothing, and triggers power
 *   state transitions on SOC threshold crossings.  Intended to run as
 *   a dedicated kthread.
 *
 ****************************************************************************/

int pwr_battery_task(int argc, char *argv[]);

#endif /* __FIRMWARE_APPS_VELASENSE_POWER_MANAGER_H */
