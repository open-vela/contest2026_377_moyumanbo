/**
 * @file max17048.h
 * @brief MAX17048 Li-ion Battery Fuel Gauge Driver for VelaSense
 *
 * Driver for the Maxim Integrated / Analog Devices MAX17048 voltage-based
 * fuel gauge.  Communicates over I2C at address 0x36 (7-bit).  Provides
 * battery voltage, state-of-charge, charge/discharge rate, alert handling,
 * quick-start recalibration, and hibernate mode.
 *
 * Hardware assumptions (VelaSense platform):
 *   - Single-cell Li-ion: 3.0 V -- 4.2 V nominal
 *   - I2C bus speed: 400 kHz (fast mode)
 *   - ALERT line directly wired to a GPIO interrupt
 *
 * @note All register addresses and bit-field masks follow the MAX17048
 *       datasheet (Rev 6, Analog Devices).
 *
 * Copyright (c) 2026 VelaSense Project
 * SPDX-License-Identifier: MIT
 */

#ifndef VELASENSE_MAX17048_H
#define VELASENSE_MAX17048_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * 7-bit I2C slave address
 * ---------------------------------------------------------------------------*/
#define MAX17048_I2C_ADDR              0x36U

/* ---------------------------------------------------------------------------
 * Register map
 * ---------------------------------------------------------------------------*/
#define MAX17048_REG_VCELL             0x00U  /* Battery voltage (read-only)   */
#define MAX17048_REG_SOC               0x02U  /* State of charge  (read-only)  */
#define MAX17048_REG_MODE              0x04U  /* Mode control      (write)     */
#define MAX17048_REG_VERSION           0x06U  /* IC revision       (read-only) */
#define MAX17048_REG_HIBRT             0x08U  /* Hibernate config  (R/W)       */
#define MAX17048_REG_CONFIG            0x0AU  /* Configuration     (R/W)       */
#define MAX17048_REG_VALRT             0x0CU  /* Voltage alerts    (R/W)       */
#define MAX17048_REG_CRATE             0x0EU  /* Charge rate       (read-only) */
#define MAX17048_REG_VRESET            0x10U  /* Reset voltage     (R/W)       */
#define MAX17048_REG_STATUS            0x12U  /* Status / alerts   (R/W)       */
#define MAX17048_REG_TABLE             0x14U  /* Model table  (0x40--0x7F R/W) */
#define MAX17048_REG_DEVICE_ID         0xFEU  /* Device ID         (read-only) */
#define MAX17048_REG_COMMAND           0xFFU  /* Command register  (write)     */

/* ---------------------------------------------------------------------------
 * VCELL register (0x00)  -- read-only
 * Upper 12 bits = battery voltage.  1 LSB = 78.125 uV.
 * ---------------------------------------------------------------------------*/
#define MAX17048_VCELL_LSB_UV          78.125f
#define MAX17048_VCELL_SHIFT           4      /* right-shift 4 to get 12-bit  */

/* ---------------------------------------------------------------------------
 * SOC register (0x02)  -- read-only
 * Upper 8 bits = integer SOC (%).  Lower 8 bits = fractional (1/256 %).
 * ---------------------------------------------------------------------------*/
#define MAX17048_SOC_INT_SHIFT         8
#define MAX17048_SOC_FRAC_MASK         0x00FFU

/* ---------------------------------------------------------------------------
 * MODE register (0x04)  -- write-only
 * ---------------------------------------------------------------------------*/
#define MAX17048_MODE_ENSLEEP          (1U << 13)  /* Enable sleep mode       */
#define MAX17048_MODE_HIBSTAT          (1U << 12)  /* Hibernate status (RO)   */
#define MAX17048_MODE_QUICKSTART       (1U << 6)   /* Quick-start recalibrate */
#define MAX17048_MODE_ENHIB            (1U << 8)   /* Enable hibernate mode   */

/* ---------------------------------------------------------------------------
 * VERSION register (0x06)  -- read-only
 * ---------------------------------------------------------------------------*/
#define MAX17048_VERSION_EXPECTED      0x0048U     /* Expected DEVICE_ID      */
#define MAX17048_VERSION_IC_VER_MASK   0x00FFU     /* IC version field        */

/* ---------------------------------------------------------------------------
 * HIBRT register (0x08)  -- R/W
 * ---------------------------------------------------------------------------*/
#define MAX17048_HIBRT_HIBTHR_MASK     0xFF00U     /* Hibernate threshold     */
#define MAX17048_HIBRT_HIBTHR_SHIFT    8
#define MAX17048_HIBRT_ACTTHR_MASK     0x00FFU     /* Activity threshold      */

/* ---------------------------------------------------------------------------
 * CONFIG register (0x0A)  -- R/W
 * ---------------------------------------------------------------------------*/
#define MAX17048_CONFIG_ALRT           (1U << 5)   /* ALERT flag (sticky)     */
#define MAX17048_CONFIG_ALSC           (1U << 6)   /* SOC change alert enable */
#define MAX17048_CONFIG_BI             (1U << 11)  /* Battery insertion (RO)  */
#define MAX17048_CONFIG_AINH           (1U << 10)  /* Alert inhibit           */
#define MAX17048_CONFIG_SHDN           (1U << 7)   /* Shutdown                */
#define MAX17048_CONFIG_TEX            (1U << 8)   /* Temperature external    */
#define MAX17048_CONFIG_RCOMP_MASK     0xFF00U     /* Compensation value      */
#define MAX17048_CONFIG_RCOMP_SHIFT    8
#define MAX17048_CONFIG_ATHD_MASK      0x001FU     /* Alert threshold (5-bit) */
#define MAX17048_CONFIG_ATHD_SHIFT     0

/* ---------------------------------------------------------------------------
 * VALRT register (0x0C)  -- R/W
 * ---------------------------------------------------------------------------*/
#define MAX17048_VALRT_VHIGH_MASK      0xFF00U     /* Upper voltage alert     */
#define MAX17048_VALRT_VHIGH_SHIFT     8
#define MAX17048_VALRT_VLOW_MASK       0x00FFU     /* Lower voltage alert     */

/* ---------------------------------------------------------------------------
 * CRATE register (0x0E)  -- read-only, signed 16-bit
 * 1 LSB = 0.208 %/hr.
 * ---------------------------------------------------------------------------*/
#define MAX17048_CRATE_LSB_PCT_HR      0.208f

/* ---------------------------------------------------------------------------
 * VRESET register (0x10)  -- R/W
 * Lower 7 bits = reset voltage threshold.  1 LSB = 40 mV.
 * ---------------------------------------------------------------------------*/
#define MAX17048_VRESET_LSB_MV         40
#define MAX17048_VRESET_DIS            (1U << 8)   /* Disable reset comparator */
#define MAX17048_VRESET_MASK           0x007FU

/* ---------------------------------------------------------------------------
 * STATUS register (0x12)  -- R/W
 * ---------------------------------------------------------------------------*/
#define MAX17048_STATUS_RI             (1U << 0)   /* Reset indicator         */
#define MAX17048_STATUS_VH             (1U << 1)   /* Voltage high alert      */
#define MAX17048_STATUS_VL             (1U << 2)   /* Voltage low alert       */
#define MAX17048_STATUS_VR             (1U << 3)   /* Voltage reset alert     */
#define MAX17048_STATUS_HD             (1U << 4)   /* SOC low alert           */
#define MAX17048_STATUS_SC             (1U << 5)   /* SOC change alert        */
#define MAX17048_STATUS_ENVR           (1U << 6)   /* Enable voltage reset    */
#define MAX17048_STATUS_FMODE_MASK     (0x3U << 8) /* Fuel gauge mode (RO)    */

/* ---------------------------------------------------------------------------
 * COMMAND register (0xFF)  -- write-only
 * ---------------------------------------------------------------------------*/
#define MAX17048_COMMAND_POR           0x0054U     /* Power-on reset command  */
#define MAX17048_COMMAND_POR_RECALL    0x0043U     /* Recall model from flash */

/* ---------------------------------------------------------------------------
 * DEVICE_ID register (0xFE)  -- read-only
 * ---------------------------------------------------------------------------*/
#define MAX17048_DEVICE_ID_EXPECTED    0x0048U     /* MAX17048 device ID      */

/* ---------------------------------------------------------------------------
 * VelaSense-specific thresholds
 * ---------------------------------------------------------------------------*/
#define MAX17048_VELASENSE_LOW_SOC_PCT    20   /* Low battery alert: 20 %    */
#define MAX17048_VELASENSE_CRIT_SOC_PCT    5   /* Critical battery: 5 %      */
#define MAX17048_VELASENSE_LOW_VOLT_MV  3300   /* Approx 20 % SOC voltage    */
#define MAX17048_VELASENSE_CRIT_VOLT_MV 3100   /* Approx 5 % SOC voltage     */

/* ---------------------------------------------------------------------------
 * Return codes
 * ---------------------------------------------------------------------------*/
typedef enum {
    MAX17048_OK            =  0,  /* Success                             */
    MAX17048_ERR_I2C       = -1,  /* I2C bus error (NACK, timeout, etc.) */
    MAX17048_ERR_ID        = -2,  /* Unexpected device ID                */
    MAX17048_ERR_PARAM     = -3,  /* Invalid parameter                   */
    MAX17048_ERR_TIMEOUT   = -4,  /* Operation timed out                 */
    MAX17048_ERR_NOT_READY = -5,  /* Device not initialised / POR busy   */
} max17048_status_t;

/* ---------------------------------------------------------------------------
 * Alert types reported through the callback
 * ---------------------------------------------------------------------------*/
typedef enum {
    MAX17048_ALERT_NONE        = 0,       /* No active alert             */
    MAX17048_ALERT_SOC_LOW     = (1 << 0),/* SOC dropped below threshold */
    MAX17048_ALERT_VOLT_HIGH   = (1 << 1),/* Voltage exceeded upper limit*/
    MAX17048_ALERT_VOLT_LOW    = (1 << 2),/* Voltage below lower limit   */
    MAX17048_ALERT_VOLT_RESET  = (1 << 3),/* Voltage below reset thresh  */
    MAX17048_ALERT_SOC_CHANGE  = (1 << 4),/* SOC changed                 */
    MAX17048_ALERT_BAT_INSERT  = (1 << 5),/* Battery inserted            */
    MAX17048_ALERT_BAT_REMOVE  = (1 << 6),/* Battery removed             */
} max17048_alert_t;

/* ---------------------------------------------------------------------------
 * Battery measurement data
 * ---------------------------------------------------------------------------*/
typedef struct {
    uint16_t voltage_mv;     /* Battery voltage in millivolts          */
    uint8_t  soc_percent;    /* Integer SOC 0--100 %                   */
    uint8_t  soc_raw_int;    /* Raw integer portion of SOC register    */
    uint8_t  soc_raw_frac;   /* Raw fractional portion of SOC register */
    int16_t  crate_raw;      /* Raw CRATE register value               */
    float    crate_pct_hr;   /* Charge/discharge rate in %/hour        */
    uint16_t version;        /* VERSION register value                 */
    uint8_t  alert_threshold;/* Current alert threshold from CONFIG    */
    uint16_t status;         /* STATUS register snapshot               */
} max17048_battery_info_t;

/* ---------------------------------------------------------------------------
 * Hibernate configuration
 * ---------------------------------------------------------------------------*/
typedef struct {
    uint8_t hib_threshold;   /* HIBRT upper byte: hibernate threshold  */
    uint8_t act_threshold;   /* HIBRT lower byte: activity threshold   */
} max17048_hibrt_cfg_t;

/* ---------------------------------------------------------------------------
 * Alert callback type
 *
 * @param alert_flags  Combination of max17048_alert_t flags.
 * @param info         Snapshot of battery data at time of alert.
 * @param user_data    Opaque pointer registered with the callback.
 * ---------------------------------------------------------------------------*/
typedef void (*max17048_alert_cb_t)(uint32_t alert_flags,
                                    const max17048_battery_info_t *info,
                                    void *user_data);

/* ---------------------------------------------------------------------------
 * I2C abstraction callbacks
 *
 * The platform must supply these so the driver is hardware-independent.
 * ---------------------------------------------------------------------------*/
typedef struct {
    /**
     * Write bytes to an I2C device.
     *
     * @param addr   7-bit I2C address.
     * @param reg    Register address (first byte of payload).
     * @param data   Buffer to transmit (excluding register byte).
     * @param len    Number of bytes in @p data.
     * @return 0 on success, negative on error.
     */
    int (*write)(uint8_t addr, uint8_t reg, const uint8_t *data, size_t len);

    /**
     * Read bytes from an I2C device.
     *
     * @param addr   7-bit I2C address.
     * @param reg    Register address to read from.
     * @param data   Buffer to receive data.
     * @param len    Number of bytes to read.
     * @return 0 on success, negative on error.
     */
    int (*read)(uint8_t addr, uint8_t reg, uint8_t *data, size_t len);

    /**
     * Delay for the given number of milliseconds.
     *
     * @param ms     Milliseconds to wait.
     */
    void (*delay_ms)(uint32_t ms);
} max17048_i2c_ops_t;

/* ---------------------------------------------------------------------------
 * Device handle (opaque to callers)
 * ---------------------------------------------------------------------------*/
typedef struct {
    max17048_i2c_ops_t  ops;           /* Platform I2C callbacks         */
    max17048_alert_cb_t alert_cb;      /* Registered alert callback      */
    void               *alert_user_data;/* User data for the callback    */
    uint16_t            config_shadow;  /* Cached CONFIG register value  */
    uint16_t            status_shadow;  /* Cached STATUS register value  */
    bool                initialised;    /* True after successful init    */
} max17048_dev_t;

/* ---------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------*/

/**
 * Initialise the MAX17048 device.
 *
 * Verifies the device ID, clears stale alert flags, sets the low-battery
 * alert threshold, and optionally performs a POR recall.
 *
 * @param[out] dev     Device handle to initialise.
 * @param[in]  ops     Platform I2C / delay callbacks (must be valid).
 * @param[in]  por     If true, issue a power-on-reset and wait for recovery.
 * @return MAX17048_OK on success, or a negative error code.
 */
max17048_status_t max17048_init(max17048_dev_t *dev,
                                const max17048_i2c_ops_t *ops,
                                bool por);

/**
 * Reset the MAX17048 to power-on defaults.
 *
 * Sends the POR command (0x0054) and waits for the device to re-initialise.
 * @param dev  Device handle.
 * @return MAX17048_OK on success.
 */
max17048_status_t max17048_reset(max17048_dev_t *dev);

/**
 * Read the current battery voltage in millivolts.
 *
 * @param dev       Device handle.
 * @param[out] mv   Voltage in mV.
 * @return MAX17048_OK on success.
 */
max17048_status_t max17048_read_voltage(max17048_dev_t *dev, uint16_t *mv);

/**
 * Read the state of charge as an integer percentage (0--100).
 *
 * @param dev            Device handle.
 * @param[out] percent   Integer SOC percentage.
 * @return MAX17048_OK on success.
 */
max17048_status_t max17048_read_soc(max17048_dev_t *dev, uint8_t *percent);

/**
 * Read the charge / discharge rate.
 *
 * @param dev             Device handle.
 * @param[out] rate_pct   Rate in %/hour (positive = charging).
 * @return MAX17048_OK on success.
 */
max17048_status_t max17048_read_crate(max17048_dev_t *dev, float *rate_pct);

/**
 * Read all battery information into a single structure.
 *
 * @param dev        Device handle.
 * @param[out] info  Populated battery info.
 * @return MAX17048_OK on success.
 */
max17048_status_t max17048_read_battery_info(max17048_dev_t *dev,
                                              max17048_battery_info_t *info);

/**
 * Configure the low-battery alert threshold in percent.
 *
 * The MAX17048 alerts when SOC <= threshold.
 *
 * @param dev        Device handle.
 * @param threshold  Threshold percentage (0--31).  VelaSense default: 20.
 * @return MAX17048_OK on success, MAX17048_ERR_PARAM if out of range.
 */
max17048_status_t max17048_set_alert_threshold(max17048_dev_t *dev,
                                               uint8_t threshold);

/**
 * Set the voltage alert thresholds.
 *
 * @param dev       Device handle.
 * @param high_mv   Upper voltage alert in mV (0 = disable upper alert).
 * @param low_mv    Lower voltage alert in mV (0 = disable lower alert).
 * @return MAX17048_OK on success.
 */
max17048_status_t max17048_set_voltage_alert(max17048_dev_t *dev,
                                              uint16_t high_mv,
                                              uint16_t low_mv);

/**
 * Register a callback for alert events.
 *
 * @param dev        Device handle.
 * @param cb         Callback function (NULL to unregister).
 * @param user_data  Opaque pointer forwarded to the callback.
 * @return MAX17048_OK.
 */
max17048_status_t max17048_register_alert_callback(max17048_dev_t *dev,
                                                    max17048_alert_cb_t cb,
                                                    void *user_data);

/**
 * Poll the STATUS register and invoke the registered callback if any
 * alert flags are set.  Clears the flags after processing.
 *
 * Intended to be called from a periodic timer or GPIO ISR.
 *
 * @param dev  Device handle.
 * @return MAX17048_OK on success.
 */
max17048_status_t max17048_poll_alerts(max17048_dev_t *dev);

/**
 * Clear all alert flags in the STATUS register.
 *
 * @param dev  Device handle.
 * @return MAX17048_OK on success.
 */
max17048_status_t max17048_clear_alerts(max17048_dev_t *dev);

/**
 * Perform a quick-start SOC recalibration.
 *
 * Initiates an open-circuit voltage measurement and recalculates SOC.
 * The host should wait at least 500 ms before reading SOC afterwards.
 *
 * @param dev  Device handle.
 * @return MAX17048_OK on success.
 */
max17048_status_t max17048_quick_start(max17048_dev_t *dev);

/**
 * Enter hibernate mode for ultra-low-power operation.
 *
 * @param dev   Device handle.
 * @param cfg   Hibernate / activity thresholds (NULL for defaults).
 * @return MAX17048_OK on success.
 */
max17048_status_t max17048_enter_hibernate(max17048_dev_t *dev,
                                           const max17048_hibrt_cfg_t *cfg);

/**
 * Exit hibernate mode.
 *
 * @param dev  Device handle.
 * @return MAX17048_OK on success.
 */
max17048_status_t max17048_exit_hibernate(max17048_dev_t *dev);

/**
 * Check whether the device is currently in hibernate mode.
 *
 * @param dev        Device handle.
 * @param[out] hib   True if in hibernate.
 * @return MAX17048_OK on success.
 */
max17048_status_t max17048_is_hibernating(max17048_dev_t *dev, bool *hib);

/**
 * Enter sleep mode (fuel gauge stops, lower power).
 *
 * @param dev  Device handle.
 * @return MAX17048_OK on success.
 */
max17048_status_t max17048_enter_sleep(max17048_dev_t *dev);

/**
 * Exit sleep mode.
 *
 * @param dev  Device handle.
 * @return MAX17048_OK on success.
 */
max17048_status_t max17048_exit_sleep(max17048_dev_t *dev);

/**
 * Set the compensation (RCOMP) value in the CONFIG register.
 *
 * RCOMP adjusts the fuel gauge model for temperature.  The datasheet
 * recommends tuning this value for the application's thermal profile.
 *
 * @param dev    Device handle.
 * @param rcomp  Compensation byte (default 0x97).
 * @return MAX17048_OK on success.
 */
max17048_status_t max17048_set_compensation(max17048_dev_t *dev,
                                            uint8_t rcomp);

/**
 * Set the reset voltage threshold.
 *
 * Below this voltage the fuel gauge asserts a reset alert, which can be
 * used to signal a near-dead battery condition.
 *
 * @param dev     Device handle.
 * @param volt_mv Reset threshold in mV (quantised to 40 mV steps).
 * @return MAX17048_OK on success.
 */
max17048_status_t max17048_set_reset_voltage(max17048_dev_t *dev,
                                              uint16_t volt_mv);

/**
 * Read the VERSION register.
 *
 * @param dev          Device handle.
 * @param[out] version 16-bit VERSION register value.
 * @return MAX17048_OK on success.
 */
max17048_status_t max17048_read_version(max17048_dev_t *dev, uint16_t *version);

/**
 * Read the DEVICE_ID register and compare against the expected value.
 *
 * @param dev        Device handle.
 * @param[out] match True if DEVICE_ID == 0x0048.
 * @return MAX17048_OK on success.
 */
max17048_status_t max17048_verify_device_id(max17048_dev_t *dev, bool *match);

#ifdef __cplusplus
}
#endif

#endif /* VELASENSE_MAX17048_H */
