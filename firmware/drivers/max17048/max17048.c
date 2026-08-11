/**
 * @file max17048.c
 * @brief MAX17048 Li-ion Battery Fuel Gauge Driver Implementation
 *
 * Full driver for the MAX17048 voltage-based fuel gauge, targeting the
 * VelaSense battery monitoring subsystem.
 *
 * Copyright (c) 2026 VelaSense Project
 * SPDX-License-Identifier: MIT
 */

#include "max17048.h"

#include <string.h>
#include <math.h>

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------------*/

/** Write a single 16-bit register (big-endian on the wire). */
static max17048_status_t reg_write16(max17048_dev_t *dev,
                                      uint8_t reg, uint16_t val)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)(val >> 8);
    buf[1] = (uint8_t)(val & 0xFFU);

    if (dev->ops.write(MAX17048_I2C_ADDR, reg, buf, 2) != 0) {
        return MAX17048_ERR_I2C;
    }
    return MAX17048_OK;
}

/** Read a single 16-bit register (big-endian on the wire). */
static max17048_status_t reg_read16(max17048_dev_t *dev,
                                     uint8_t reg, uint16_t *val)
{
    uint8_t buf[2] = {0};

    if (dev->ops.read(MAX17048_I2C_ADDR, reg, buf, 2) != 0) {
        return MAX17048_ERR_I2C;
    }
    *val = ((uint16_t)buf[0] << 8) | buf[1];
    return MAX17048_OK;
}

/* ---------------------------------------------------------------------------
 * Initialisation / reset
 * ---------------------------------------------------------------------------*/

max17048_status_t max17048_init(max17048_dev_t *dev,
                                const max17048_i2c_ops_t *ops,
                                bool por)
{
    if (dev == NULL || ops == NULL ||
        ops->read == NULL || ops->write == NULL || ops->delay_ms == NULL) {
        return MAX17048_ERR_PARAM;
    }

    memset(dev, 0, sizeof(*dev));
    dev->ops            = *ops;
    dev->alert_cb       = NULL;
    dev->alert_user_data = NULL;
    dev->config_shadow  = 0;
    dev->status_shadow  = 0;
    dev->initialised    = false;

    /* Optional POR: reset and wait for the device to recover. */
    if (por) {
        max17048_status_t st = max17048_reset(dev);
        if (st != MAX17048_OK) {
            return st;
        }
    }

    /* Verify device identity. */
    bool match = false;
    max17048_status_t st = max17048_verify_device_id(dev, &match);
    if (st != MAX17048_OK) {
        return st;
    }
    if (!match) {
        return MAX17048_ERR_ID;
    }

    /* Cache CONFIG register. */
    st = reg_read16(dev, MAX17048_REG_CONFIG, &dev->config_shadow);
    if (st != MAX17048_OK) {
        return st;
    }

    /* Set VelaSense default: 20 % low-battery alert. */
    st = max17048_set_alert_threshold(dev, MAX17048_VELASENSE_LOW_SOC_PCT);
    if (st != MAX17048_OK) {
        return st;
    }

    /* Clear any stale alert flags. */
    st = max17048_clear_alerts(dev);
    if (st != MAX17048_OK) {
        return st;
    }

    /* Set default voltage alert windows for single Li-ion cell.
     * Upper: disable (0) -- not critical for VelaSense.
     * Lower: 3300 mV (approximate 20 % SOC for LiFePO4-compatible window). */
    st = max17048_set_voltage_alert(dev, 0, MAX17048_VELASENSE_LOW_VOLT_MV);
    if (st != MAX17048_OK) {
        return st;
    }

    /* Set reset voltage for critical condition (~3.1 V). */
    st = max17048_set_reset_voltage(dev, MAX17048_VELASENSE_CRIT_VOLT_MV);
    if (st != MAX17048_OK) {
        return st;
    }

    dev->initialised = true;
    return MAX17048_OK;
}

max17048_status_t max17048_reset(max17048_dev_t *dev)
{
    if (dev == NULL) {
        return MAX17048_ERR_PARAM;
    }

    max17048_status_t st = reg_write16(dev, MAX17048_REG_COMMAND,
                                        MAX17048_COMMAND_POR);
    if (st != MAX17048_OK) {
        return st;
    }

    /* Datasheet: tPOR ~ 500 ms max.  Wait for completion. */
    dev->ops.delay_ms(500);

    /* Verify RI (Reset Indicator) in STATUS register is set after POR. */
    uint16_t status = 0;
    st = reg_read16(dev, MAX17048_REG_STATUS, &status);
    if (st != MAX17048_OK) {
        return st;
    }

    if (status & MAX17048_STATUS_RI) {
        /* Clear RI flag. */
        status &= ~MAX17048_STATUS_RI;
        st = reg_write16(dev, MAX17048_REG_STATUS, status);
        if (st != MAX17048_OK) {
            return st;
        }
    }

    return MAX17048_OK;
}

/* ---------------------------------------------------------------------------
 * Voltage reading
 * ---------------------------------------------------------------------------*/

max17048_status_t max17048_read_voltage(max17048_dev_t *dev, uint16_t *mv)
{
    if (dev == NULL || mv == NULL) {
        return MAX17048_ERR_PARAM;
    }

    uint16_t raw = 0;
    max17048_status_t st = reg_read16(dev, MAX17048_REG_VCELL, &raw);
    if (st != MAX17048_OK) {
        return st;
    }

    /* Upper 12 bits.  1 LSB = 78.125 uV.  Convert to mV:
     *   mv = (raw >> 4) * 78.125 / 1000
     * Using 32-bit integer math to avoid floating-point dependency. */
    uint16_t code = raw >> MAX17048_VCELL_SHIFT;
    *mv = (uint16_t)((uint32_t)code * 78125UL / 1000000UL);

    return MAX17048_OK;
}

/* ---------------------------------------------------------------------------
 * SOC reading
 * ---------------------------------------------------------------------------*/

max17048_status_t max17048_read_soc(max17048_dev_t *dev, uint8_t *percent)
{
    if (dev == NULL || percent == NULL) {
        return MAX17048_ERR_PARAM;
    }

    uint16_t raw = 0;
    max17048_status_t st = reg_read16(dev, MAX17048_REG_SOC, &raw);
    if (st != MAX17048_OK) {
        return st;
    }

    /* Upper 8 bits = integer SOC percentage. */
    *percent = (uint8_t)(raw >> MAX17048_SOC_INT_SHIFT);

    /* Clamp to 100 -- the register can momentarily exceed 100 during
     * charging transients. */
    if (*percent > 100U) {
        *percent = 100U;
    }

    return MAX17048_OK;
}

/* ---------------------------------------------------------------------------
 * Charge / discharge rate
 * ---------------------------------------------------------------------------*/

max17048_status_t max17048_read_crate(max17048_dev_t *dev, float *rate_pct)
{
    if (dev == NULL || rate_pct == NULL) {
        return MAX17048_ERR_PARAM;
    }

    uint16_t raw = 0;
    max17048_status_t st = reg_read16(dev, MAX17048_REG_CRATE, &raw);
    if (st != MAX17048_OK) {
        return st;
    }

    /* CRATE is signed 16-bit.  1 LSB = 0.208 %/hr. */
    int16_t signed_raw = (int16_t)raw;
    *rate_pct = (float)signed_raw * MAX17048_CRATE_LSB_PCT_HR;

    return MAX17048_OK;
}

/* ---------------------------------------------------------------------------
 * Aggregate battery info
 * ---------------------------------------------------------------------------*/

max17048_status_t max17048_read_battery_info(max17048_dev_t *dev,
                                              max17048_battery_info_t *info)
{
    if (dev == NULL || info == NULL) {
        return MAX17048_ERR_PARAM;
    }

    memset(info, 0, sizeof(*info));

    max17048_status_t st;

    /* Voltage. */
    st = max17048_read_voltage(dev, &info->voltage_mv);
    if (st != MAX17048_OK) return st;

    /* SOC (raw + integer). */
    uint16_t soc_raw = 0;
    st = reg_read16(dev, MAX17048_REG_SOC, &soc_raw);
    if (st != MAX17048_OK) return st;

    info->soc_raw_int  = (uint8_t)(soc_raw >> MAX17048_SOC_INT_SHIFT);
    info->soc_raw_frac = (uint8_t)(soc_raw & MAX17048_SOC_FRAC_MASK);
    info->soc_percent  = info->soc_raw_int;
    if (info->soc_percent > 100U) {
        info->soc_percent = 100U;
    }

    /* CRATE. */
    uint16_t crate_u16 = 0;
    st = reg_read16(dev, MAX17048_REG_CRATE, &crate_u16);
    if (st != MAX17048_OK) return st;
    info->crate_raw    = (int16_t)crate_u16;
    info->crate_pct_hr = (float)info->crate_raw * MAX17048_CRATE_LSB_PCT_HR;

    /* VERSION. */
    st = reg_read16(dev, MAX17048_REG_VERSION, &info->version);
    if (st != MAX17048_OK) return st;

    /* CONFIG (alert threshold). */
    uint16_t cfg = 0;
    st = reg_read16(dev, MAX17048_REG_CONFIG, &cfg);
    if (st != MAX17048_OK) return st;
    info->alert_threshold = (uint8_t)(cfg & MAX17048_CONFIG_ATHD_MASK);

    /* STATUS. */
    st = reg_read16(dev, MAX17048_REG_STATUS, &info->status);
    if (st != MAX17048_OK) return st;

    return MAX17048_OK;
}

/* ---------------------------------------------------------------------------
 * Alert threshold configuration
 * ---------------------------------------------------------------------------*/

max17048_status_t max17048_set_alert_threshold(max17048_dev_t *dev,
                                               uint8_t threshold)
{
    if (dev == NULL) {
        return MAX17048_ERR_PARAM;
    }

    /* The ATHD field is 5 bits and represents the threshold as a value
     * from 1--31 %.  A value of N means alert fires when SOC <= N.
     * Writing 0 disables the SOC-low alert. */
    if (threshold > 31U) {
        return MAX17048_ERR_PARAM;
    }

    dev->config_shadow &= ~MAX17048_CONFIG_ATHD_MASK;
    dev->config_shadow |= (uint16_t)(threshold & MAX17048_CONFIG_ATHD_MASK);

    return reg_write16(dev, MAX17048_REG_CONFIG, dev->config_shadow);
}

/* ---------------------------------------------------------------------------
 * Voltage alerts
 * ---------------------------------------------------------------------------*/

max17048_status_t max17048_set_voltage_alert(max17048_dev_t *dev,
                                              uint16_t high_mv,
                                              uint16_t low_mv)
{
    if (dev == NULL) {
        return MAX17048_ERR_PARAM;
    }

    /* VALRT register encoding:
     *   Upper byte: high threshold in 20 mV steps, offset 0.
     *   Lower byte: low threshold  in 20 mV steps, offset 0.
     * Max value per byte: 255 * 20 = 5100 mV. */
    uint8_t high_code = (uint8_t)(high_mv / 20U);
    uint8_t low_code  = (uint8_t)(low_mv  / 20U);

    uint16_t valrt = ((uint16_t)high_code << MAX17048_VALRT_VHIGH_SHIFT) |
                     (uint16_t)low_code;

    return reg_write16(dev, MAX17048_REG_VALRT, valrt);
}

/* ---------------------------------------------------------------------------
 * Alert callback management
 * ---------------------------------------------------------------------------*/

max17048_status_t max17048_register_alert_callback(max17048_dev_t *dev,
                                                    max17048_alert_cb_t cb,
                                                    void *user_data)
{
    if (dev == NULL) {
        return MAX17048_ERR_PARAM;
    }

    dev->alert_cb       = cb;
    dev->alert_user_data = user_data;
    return MAX17048_OK;
}

max17048_status_t max17048_poll_alerts(max17048_dev_t *dev)
{
    if (dev == NULL) {
        return MAX17048_ERR_PARAM;
    }

    uint16_t status = 0;
    max17048_status_t st = reg_read16(dev, MAX17048_REG_STATUS, &status);
    if (st != MAX17048_OK) {
        return st;
    }

    dev->status_shadow = status;

    /* Mask to only the alert flags we care about. */
    uint32_t flags = 0;
    if (status & MAX17048_STATUS_HD)  flags |= MAX17048_ALERT_SOC_LOW;
    if (status & MAX17048_STATUS_VH)  flags |= MAX17048_ALERT_VOLT_HIGH;
    if (status & MAX17048_STATUS_VL)  flags |= MAX17048_ALERT_VOLT_LOW;
    if (status & MAX17048_STATUS_VR)  flags |= MAX17048_ALERT_VOLT_RESET;
    if (status & MAX17048_STATUS_SC)  flags |= MAX17048_ALERT_SOC_CHANGE;

    /* Battery insertion/removal is derived from the BI bit in CONFIG. */
    uint16_t cfg = 0;
    st = reg_read16(dev, MAX17048_REG_CONFIG, &cfg);
    if (st == MAX17048_OK) {
        if (cfg & MAX17048_CONFIG_BI) {
            flags |= MAX17048_ALERT_BAT_INSERT;
        }
    }

    /* Invoke callback if any flags are set and a callback is registered. */
    if (flags != 0U && dev->alert_cb != NULL) {
        max17048_battery_info_t info;
        memset(&info, 0, sizeof(info));
        max17048_read_battery_info(dev, &info);

        dev->alert_cb(flags, &info, dev->alert_user_data);
    }

    /* Clear the alert flags by writing 0 to those bits. */
    if (flags != 0U) {
        st = max17048_clear_alerts(dev);
    }

    return MAX17048_OK;
}

max17048_status_t max17048_clear_alerts(max17048_dev_t *dev)
{
    if (dev == NULL) {
        return MAX17048_ERR_PARAM;
    }

    /* Read current STATUS. */
    uint16_t status = 0;
    max17048_status_t st = reg_read16(dev, MAX17048_REG_STATUS, &status);
    if (st != MAX17048_OK) {
        return st;
    }

    /* Clear all alert bits (bits 0--6), preserve the rest. */
    status &= ~(MAX17048_STATUS_RI  |
                MAX17048_STATUS_VH  |
                MAX17048_STATUS_VL  |
                MAX17048_STATUS_VR  |
                MAX17048_STATUS_HD  |
                MAX17048_STATUS_SC);

    return reg_write16(dev, MAX17048_REG_STATUS, status);
}

/* ---------------------------------------------------------------------------
 * Quick-start (SOC recalibration)
 * ---------------------------------------------------------------------------*/

max17048_status_t max17048_quick_start(max17048_dev_t *dev)
{
    if (dev == NULL) {
        return MAX17048_ERR_PARAM;
    }

    /* Set QUICKSTART bit in MODE register. */
    uint16_t mode = MAX17048_MODE_QUICKSTART;
    return reg_write16(dev, MAX17048_REG_MODE, mode);
}

/* ---------------------------------------------------------------------------
 * Hibernate mode
 * ---------------------------------------------------------------------------*/

max17048_status_t max17048_enter_hibernate(max17048_dev_t *dev,
                                           const max17048_hibrt_cfg_t *cfg)
{
    if (dev == NULL) {
        return MAX17048_ERR_PARAM;
    }

    uint16_t hibrt;

    if (cfg != NULL) {
        hibrt = ((uint16_t)cfg->hib_threshold << MAX17048_HIBRT_HIBTHR_SHIFT) |
                (uint16_t)cfg->act_threshold;
    } else {
        /* Default thresholds from datasheet: hib = 0x00, act = 0x00
         * (hibernate when activity is below CRATE threshold). */
        hibrt = 0x0000U;
    }

    max17048_status_t st = reg_write16(dev, MAX17048_REG_HIBRT, hibrt);
    if (st != MAX17048_OK) {
        return st;
    }

    /* Enable hibernate via MODE register. */
    return reg_write16(dev, MAX17048_REG_MODE, MAX17048_MODE_ENHIB);
}

max17048_status_t max17048_exit_hibernate(max17048_dev_t *dev)
{
    if (dev == NULL) {
        return MAX17048_ERR_PARAM;
    }

    /* Clear ENHIB bit. */
    return reg_write16(dev, MAX17048_REG_MODE, 0x0000U);
}

max17048_status_t max17048_is_hibernating(max17048_dev_t *dev, bool *hib)
{
    if (dev == NULL || hib == NULL) {
        return MAX17048_ERR_PARAM;
    }

    uint16_t mode = 0;
    max17048_status_t st = reg_read16(dev, MAX17048_REG_MODE, &mode);
    if (st != MAX17048_OK) {
        return st;
    }

    *hib = (mode & MAX17048_MODE_HIBSTAT) ? true : false;
    return MAX17048_OK;
}

/* ---------------------------------------------------------------------------
 * Sleep mode
 * ---------------------------------------------------------------------------*/

max17048_status_t max17048_enter_sleep(max17048_dev_t *dev)
{
    if (dev == NULL) {
        return MAX17048_ERR_PARAM;
    }

    return reg_write16(dev, MAX17048_REG_MODE, MAX17048_MODE_ENSLEEP);
}

max17048_status_t max17048_exit_sleep(max17048_dev_t *dev)
{
    if (dev == NULL) {
        return MAX17048_ERR_PARAM;
    }

    return reg_write16(dev, MAX17048_REG_MODE, 0x0000U);
}

/* ---------------------------------------------------------------------------
 * Compensation (RCOMP)
 * ---------------------------------------------------------------------------*/

max17048_status_t max17048_set_compensation(max17048_dev_t *dev,
                                            uint8_t rcomp)
{
    if (dev == NULL) {
        return MAX17048_ERR_PARAM;
    }

    dev->config_shadow &= ~MAX17048_CONFIG_RCOMP_MASK;
    dev->config_shadow |= ((uint16_t)rcomp << MAX17048_CONFIG_RCOMP_SHIFT);

    return reg_write16(dev, MAX17048_REG_CONFIG, dev->config_shadow);
}

/* ---------------------------------------------------------------------------
 * Reset voltage threshold
 * ---------------------------------------------------------------------------*/

max17048_status_t max17048_set_reset_voltage(max17048_dev_t *dev,
                                              uint16_t volt_mv)
{
    if (dev == NULL) {
        return MAX17048_ERR_PARAM;
    }

    /* 1 LSB = 40 mV, 7-bit field.  Max = 127 * 40 = 5080 mV. */
    uint8_t code = (uint8_t)(volt_mv / MAX17048_VRESET_LSB_MV);
    if (code > MAX17048_VRESET_MASK) {
        code = MAX17048_VRESET_MASK;
    }

    /* Preserve the DIS bit, update threshold. */
    uint16_t current = 0;
    max17048_status_t st = reg_read16(dev, MAX17048_REG_VRESET, &current);
    if (st != MAX17048_OK) {
        return st;
    }

    current &= ~MAX17048_VRESET_MASK;
    current |= (uint16_t)code;

    return reg_write16(dev, MAX17048_REG_VRESET, current);
}

/* ---------------------------------------------------------------------------
 * Version / device ID queries
 * ---------------------------------------------------------------------------*/

max17048_status_t max17048_read_version(max17048_dev_t *dev, uint16_t *version)
{
    if (dev == NULL || version == NULL) {
        return MAX17048_ERR_PARAM;
    }

    return reg_read16(dev, MAX17048_REG_VERSION, version);
}

max17048_status_t max17048_verify_device_id(max17048_dev_t *dev, bool *match)
{
    if (dev == NULL || match == NULL) {
        return MAX17048_ERR_PARAM;
    }

    uint16_t id = 0;
    max17048_status_t st = reg_read16(dev, MAX17048_REG_DEVICE_ID, &id);
    if (st != MAX17048_OK) {
        return st;
    }

    *match = (id == MAX17048_DEVICE_ID_EXPECTED);
    return MAX17048_OK;
}
