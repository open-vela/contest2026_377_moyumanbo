/**
 * @file drv2605l.c
 * @brief DRV2605L Haptic Motor Driver Implementation — VelaSense
 *
 * Full I2C driver for the TI DRV2605L haptic motor driver.  Provides
 * register-level control, LRA auto-calibration, single / sequenced
 * waveform playback, real-time playback mode, and VelaSense-specific
 * haptic patterns (event alert, notification, breathing guide, alarm).
 *
 * @version  1.0.0
 * @date     2026-08-11
 * @author   VelaSense Firmware Team
 *
 * SPDX-License-Identifier: MIT
 */

#include "drv2605l.h"
#include <string.h>

/* ================================================================== */
/*  Internal Device Handle                                             */
/* ================================================================== */

struct drv2605l_dev {
    drv2605l_config_t   cfg;
    drv2605l_platform_t plat;
    bool                initialised;
    bool                rtp_active;
};

/* ================================================================== */
/*  Helpers                                                            */
/* ================================================================== */

/** Convert a generic I2C return value to a driver error code. */
static inline drv2605l_err_t i2c_to_err(int rc)
{
    return (rc == 0) ? DRV2605L_OK : DRV2605L_ERR_I2C;
}

/** Read a single register — thin wrapper. */
static drv2605l_err_t reg_read(drv2605l_dev_t *dev, uint8_t reg,
                               uint8_t *val)
{
    int rc = dev->plat.i2c_read(dev->cfg.i2c_addr, reg, val, 1);
    return i2c_to_err(rc);
}

/** Write a single register — thin wrapper. */
static drv2605l_err_t reg_write(drv2605l_dev_t *dev, uint8_t reg,
                                uint8_t val)
{
    int rc = dev->plat.i2c_write(dev->cfg.i2c_addr, reg, &val, 1);
    return i2c_to_err(rc);
}

/** Write multiple contiguous registers starting at @p reg. */
static drv2605l_err_t reg_write_burst(drv2605l_dev_t *dev, uint8_t reg,
                                      const uint8_t *data, uint8_t len)
{
    int rc = dev->plat.i2c_write(dev->cfg.i2c_addr, reg, data, len);
    return i2c_to_err(rc);
}

/** Modify selected bits in a register (read-modify-write). */
static drv2605l_err_t reg_rmw(drv2605l_dev_t *dev, uint8_t reg,
                              uint8_t mask, uint8_t value)
{
    uint8_t tmp;
    drv2605l_err_t err = reg_read(dev, reg, &tmp);
    if (err != DRV2605L_OK) return err;
    tmp = (tmp & ~mask) | (value & mask);
    return reg_write(dev, reg, tmp);
}

/** Small convenience wrapper around the platform delay. */
static inline void delay_ms(drv2605l_dev_t *dev, uint32_t ms)
{
    if (dev->plat.delay_ms) {
        dev->plat.delay_ms(ms);
    }
}

/** Wait until the Go bit clears, or @p timeout_ms expires. */
static drv2605l_err_t wait_go_clear(drv2605l_dev_t *dev, uint32_t timeout_ms)
{
    const uint32_t poll_ms = 5;
    uint32_t elapsed = 0;
    uint8_t go;

    while (elapsed < timeout_ms) {
        drv2605l_err_t err = reg_read(dev, DRV2605L_REG_GO, &go);
        if (err != DRV2605L_OK) return err;
        if (go == DRV2605L_STOP) return DRV2605L_OK;
        delay_ms(dev, poll_ms);
        elapsed += poll_ms;
    }
    return DRV2605L_ERR_TIMEOUT;
}

/* ================================================================== */
/*  Core API Implementation                                            */
/* ================================================================== */

drv2605l_err_t drv2605l_init(drv2605l_dev_t *dev,
                             const drv2605l_config_t *config,
                             const drv2605l_platform_t *plat)
{
    if (!dev || !config || !plat) return DRV2605L_ERR_PARAM;
    if (!plat->i2c_write || !plat->i2c_read || !plat->delay_ms)
        return DRV2605L_ERR_PARAM;

    /* Store configuration and platform callbacks */
    memcpy(&dev->cfg,  config, sizeof(drv2605l_config_t));
    memcpy(&dev->plat, plat,   sizeof(drv2605l_platform_t));
    dev->initialised = false;
    dev->rtp_active  = false;

    /* --- Software reset ------------------------------------------------ */
    drv2605l_err_t err = reg_write(dev, DRV2605L_REG_MODE,
                                   DRV2605L_MODE_RESET);
    if (err != DRV2605L_OK) return err;
    delay_ms(dev, 10);

    /* --- Verify device ID ---------------------------------------------- */
    uint8_t status;
    err = reg_read(dev, DRV2605L_REG_STATUS, &status);
    if (err != DRV2605L_OK) return err;

    if ((status & DRV2605L_STATUS_DEVICE_ID_MASK) !=
        DRV2605L_STATUS_DEVICE_ID_2605L) {
        return DRV2605L_ERR_ID;
    }

    /* --- Set mode to internal trigger ---------------------------------- */
    err = reg_write(dev, DRV2605L_REG_MODE,
                    DRV2605L_MODE_INTERNAL_TRIGGER);
    if (err != DRV2605L_OK) return err;

    /* --- Select waveform library --------------------------------------- */
    err = reg_write(dev, DRV2605L_REG_LIBRARY_SEL,
                    (uint8_t)config->library);
    if (err != DRV2605L_OK) return err;

    /* --- Set rated voltage and overdrive clamp ------------------------- */
    err = reg_write(dev, DRV2605L_REG_RATED_VOLTAGE,
                    config->rated_voltage);
    if (err != DRV2605L_OK) return err;

    err = reg_write(dev, DRV2605L_REG_OD_CLAMP, config->od_clamp);
    if (err != DRV2605L_OK) return err;

    /* --- Feedback control: set motor type and BEMF gain ---------------- */
    {
        uint8_t fb = DRV2605L_FB_BEMF_GAIN(3);   /* default BEMF gain */
        if (config->motor_type == DRV2605L_MOTOR_LRA) {
            fb |= DRV2605L_FB_N_ERM_LRA;
        }
        err = reg_write(dev, DRV2605L_REG_FEEDBACK_CTRL, fb);
        if (err != DRV2605L_OK) return err;
    }

    /* --- Control 4: auto-resonance for LRA ----------------------------- */
    {
        uint8_t ctrl4 = 0x00;
        if (config->motor_type == DRV2605L_MOTOR_LRA) {
            ctrl4 |= DRV2605L_CTRL4_AUTO_RES;
        }
        err = reg_write(dev, DRV2605L_REG_CONTROL4, ctrl4);
        if (err != DRV2605L_OK) return err;
    }

    /* --- Control 5: set n_erm_lra bit ---------------------------------- */
    {
        uint8_t ctrl5 = 0x00;
        if (config->motor_type == DRV2605L_MOTOR_LRA) {
            ctrl5 |= DRV2605L_CTRL5_N_ERM_LRA;
        }
        err = reg_write(dev, DRV2605L_REG_CONTROL5, ctrl5);
        if (err != DRV2605L_OK) return err;
    }

    /* --- Control 2: use bi-directional input, default sample time ------ */
    err = reg_write(dev, DRV2605L_REG_CONTROL2, 0xF5);
    if (err != DRV2605L_OK) return err;

    /* --- Control 3: embedded ROM, noise gating off --------------------- */
    err = reg_write(dev, DRV2605L_REG_CONTROL3, 0xA0);
    if (err != DRV2605L_OK) return err;

    /* --- Control 1: AC coupling, default drive time -------------------- */
    err = reg_write(dev, DRV2605L_REG_CONTROL1, 0x93);
    if (err != DRV2605L_OK) return err;

    /* --- Auto-calibration (LRA) ---------------------------------------- */
    if (config->auto_calibrate &&
        config->motor_type == DRV2605L_MOTOR_LRA) {

        err = reg_write(dev, DRV2605L_REG_MODE,
                        DRV2605L_MODE_AUTO_CALIBRATE);
        if (err != DRV2605L_OK) return err;

        /* Trigger calibration */
        err = reg_write(dev, DRV2605L_REG_GO, DRV2605L_GO);
        if (err != DRV2605L_OK) return err;

        /* Wait for calibration to complete (typically 500-800 ms) */
        err = wait_go_clear(dev, 2000);
        if (err != DRV2605L_OK) return err;

        /* Return to internal trigger mode */
        err = reg_write(dev, DRV2605L_REG_MODE,
                        DRV2605L_MODE_INTERNAL_TRIGGER);
        if (err != DRV2605L_OK) return err;
    }

    /* --- Clear sequencer ----------------------------------------------- */
    uint8_t zeroes[DRV2605L_WAVSEQ_MAX_SLOTS];
    memset(zeroes, 0x00, sizeof(zeroes));
    err = reg_write_burst(dev, DRV2605L_REG_WAVSEQ1,
                          zeroes, DRV2605L_WAVSEQ_MAX_SLOTS);
    if (err != DRV2605L_OK) return err;

    /* --- Ensure Go bit is clear ---------------------------------------- */
    err = reg_write(dev, DRV2605L_REG_GO, DRV2605L_STOP);
    if (err != DRV2605L_OK) return err;

    dev->initialised = true;
    return DRV2605L_OK;
}

drv2605l_err_t drv2605l_reset(drv2605l_dev_t *dev)
{
    if (!dev) return DRV2605L_ERR_PARAM;

    drv2605l_err_t err = reg_write(dev, DRV2605L_REG_MODE,
                                   DRV2605L_MODE_RESET);
    if (err != DRV2605L_OK) return err;
    delay_ms(dev, 10);

    dev->initialised = false;
    dev->rtp_active  = false;

    /* Re-run full init with stored configuration */
    return drv2605l_init(dev, &dev->cfg, &dev->plat);
}

drv2605l_err_t drv2605l_standby(drv2605l_dev_t *dev, bool enter)
{
    if (!dev) return DRV2605L_ERR_PARAM;
    return reg_rmw(dev, DRV2605L_REG_MODE,
                   DRV2605L_MODE_STANDBY,
                   enter ? DRV2605L_MODE_STANDBY : 0x00);
}

/* ================================================================== */
/*  Playback API                                                       */
/* ================================================================== */

drv2605l_err_t drv2605l_play_effect(drv2605l_dev_t *dev, uint8_t effect_id)
{
    drv2605l_err_t err = drv2605l_play_effect_async(dev, effect_id);
    if (err != DRV2605L_OK) return err;

    return drv2605l_wait_done(dev, 5000);
}

drv2605l_err_t drv2605l_play_effect_async(drv2605l_dev_t *dev,
                                          uint8_t effect_id)
{
    if (!dev) return DRV2605L_ERR_PARAM;
    if (effect_id < 1 || effect_id > 123) return DRV2605L_ERR_PARAM;

    /* If RTP was active, exit it first */
    if (dev->rtp_active) {
        drv2605l_err_t err = drv2605l_rtp_exit(dev);
        if (err != DRV2605L_OK) return err;
    }

    /* Make sure we are in internal trigger mode */
    drv2605l_err_t err;
    err = reg_write(dev, DRV2605L_REG_MODE,
                    DRV2605L_MODE_INTERNAL_TRIGGER);
    if (err != DRV2605L_OK) return err;

    /* Clear sequencer — put effect in slot 1, terminate at slot 2 */
    uint8_t seq[DRV2605L_WAVSEQ_MAX_SLOTS];
    memset(seq, 0x00, sizeof(seq));
    seq[0] = effect_id;
    /* seq[1] through seq[7] remain 0x00 = end of sequence */
    err = reg_write_burst(dev, DRV2605L_REG_WAVSEQ1,
                          seq, DRV2605L_WAVSEQ_MAX_SLOTS);
    if (err != DRV2605L_OK) return err;

    /* Trigger playback */
    return reg_write(dev, DRV2605L_REG_GO, DRV2605L_GO);
}

drv2605l_err_t drv2605l_play_sequence(drv2605l_dev_t *dev,
                                      const uint8_t *seq,
                                      uint8_t count)
{
    if (!dev || !seq) return DRV2605L_ERR_PARAM;
    if (count == 0 || count > DRV2605L_WAVSEQ_MAX_SLOTS)
        return DRV2605L_ERR_PARAM;

    /* If RTP was active, exit it first */
    if (dev->rtp_active) {
        drv2605l_err_t err = drv2605l_rtp_exit(dev);
        if (err != DRV2605L_OK) return err;
    }

    /* Make sure we are in internal trigger mode */
    drv2605l_err_t err;
    err = reg_write(dev, DRV2605L_REG_MODE,
                    DRV2605L_MODE_INTERNAL_TRIGGER);
    if (err != DRV2605L_OK) return err;

    /* Build sequencer buffer — zero-pad remaining slots */
    uint8_t buf[DRV2605L_WAVSEQ_MAX_SLOTS];
    memset(buf, 0x00, sizeof(buf));
    for (uint8_t i = 0; i < count; i++) {
        buf[i] = seq[i];
    }
    /* The slot after the last entry is already 0x00 = end marker */

    /* Write all 8 sequencer registers in one burst */
    err = reg_write_burst(dev, DRV2605L_REG_WAVSEQ1,
                          buf, DRV2605L_WAVSEQ_MAX_SLOTS);
    if (err != DRV2605L_OK) return err;

    /* Trigger playback */
    return reg_write(dev, DRV2605L_REG_GO, DRV2605L_GO);
}

drv2605l_err_t drv2605l_stop(drv2605l_dev_t *dev)
{
    if (!dev) return DRV2605L_ERR_PARAM;

    /* Writing 0 to the Go register halts playback immediately */
    drv2605l_err_t err = reg_write(dev, DRV2605L_REG_GO, DRV2605L_STOP);

    /* Also clear the sequencer to avoid stale entries */
    uint8_t zeroes[DRV2605L_WAVSEQ_MAX_SLOTS];
    memset(zeroes, 0x00, sizeof(zeroes));
    drv2605l_err_t err2 = reg_write_burst(dev, DRV2605L_REG_WAVSEQ1,
                                           zeroes,
                                           DRV2605L_WAVSEQ_MAX_SLOTS);

    dev->rtp_active = false;
    return (err != DRV2605L_OK) ? err : err2;
}

drv2605l_err_t drv2605l_wait_done(drv2605l_dev_t *dev, uint32_t timeout_ms)
{
    if (!dev) return DRV2605L_ERR_PARAM;
    return wait_go_clear(dev, timeout_ms);
}

drv2605l_err_t drv2605l_is_busy(drv2605l_dev_t *dev, bool *busy)
{
    if (!dev || !busy) return DRV2605L_ERR_PARAM;

    uint8_t go;
    drv2605l_err_t err = reg_read(dev, DRV2605L_REG_GO, &go);
    if (err != DRV2605L_OK) return err;

    *busy = (go != DRV2605L_STOP);
    return DRV2605L_OK;
}

/* ================================================================== */
/*  Real-Time Playback (RTP) API                                       */
/* ================================================================== */

drv2605l_err_t drv2605l_rtp_enter(drv2605l_dev_t *dev)
{
    if (!dev) return DRV2605L_ERR_PARAM;

    /* Stop any current playback */
    drv2605l_err_t err = reg_write(dev, DRV2605L_REG_GO, DRV2605L_STOP);
    if (err != DRV2605L_OK) return err;

    /* Switch to RTP mode */
    err = reg_write(dev, DRV2605L_REG_MODE, DRV2605L_MODE_REAL_TIME);
    if (err != DRV2605L_OK) return err;

    dev->rtp_active = true;
    return DRV2605L_OK;
}

drv2605l_err_t drv2605l_rtp_write(drv2605l_dev_t *dev, uint8_t value)
{
    if (!dev) return DRV2605L_ERR_PARAM;
    if (!dev->rtp_active) return DRV2605L_ERR_BUSY;

    return reg_write(dev, DRV2605L_REG_RTP_INPUT, value);
}

drv2605l_err_t drv2605l_rtp_exit(drv2605l_dev_t *dev)
{
    if (!dev) return DRV2605L_ERR_PARAM;

    /* Zero out the RTP input */
    drv2605l_err_t err = reg_write(dev, DRV2605L_REG_RTP_INPUT, 0x00);
    if (err != DRV2605L_OK) return err;

    /* Return to internal trigger mode */
    err = reg_write(dev, DRV2605L_REG_MODE,
                    DRV2605L_MODE_INTERNAL_TRIGGER);
    if (err != DRV2605L_OK) return err;

    dev->rtp_active = false;
    return DRV2605L_OK;
}

/* ================================================================== */
/*  Low-level Register Access                                          */
/* ================================================================== */

drv2605l_err_t drv2605l_read_reg(drv2605l_dev_t *dev,
                                 uint8_t reg, uint8_t *val)
{
    if (!dev || !val) return DRV2605L_ERR_PARAM;
    return reg_read(dev, reg, val);
}

drv2605l_err_t drv2605l_write_reg(drv2605l_dev_t *dev,
                                  uint8_t reg, uint8_t val)
{
    if (!dev) return DRV2605L_ERR_PARAM;
    return reg_write(dev, reg, val);
}

/* ================================================================== */
/*  Diagnostics                                                        */
/* ================================================================== */

drv2605l_err_t drv2605l_get_status(drv2605l_dev_t *dev,
                                   bool *over_temp,
                                   bool *over_current)
{
    if (!dev) return DRV2605L_ERR_PARAM;

    uint8_t status;
    drv2605l_err_t err = reg_read(dev, DRV2605L_REG_STATUS, &status);
    if (err != DRV2605L_OK) return err;

    if (over_temp)    *over_temp    = (status & DRV2605L_STATUS_OVER_TEMP)    != 0;
    if (over_current) *over_current = (status & DRV2605L_STATUS_OVER_CURRENT) != 0;

    return DRV2605L_OK;
}

drv2605l_err_t drv2605l_get_vbat(drv2605l_dev_t *dev, uint8_t *vbat)
{
    if (!dev || !vbat) return DRV2605L_ERR_PARAM;
    return reg_read(dev, DRV2605L_REG_VBAT, vbat);
}

drv2605l_err_t drv2605l_get_cal_result(drv2605l_dev_t *dev,
                                       uint8_t *comp,
                                       uint8_t *bemf)
{
    if (!dev) return DRV2605L_ERR_PARAM;

    drv2605l_err_t err;
    if (comp) {
        err = reg_read(dev, DRV2605L_REG_CAL_COMP, comp);
        if (err != DRV2605L_OK) return err;
    }
    if (bemf) {
        err = reg_read(dev, DRV2605L_REG_CAL_BEMF, bemf);
        if (err != DRV2605L_OK) return err;
    }
    return DRV2605L_OK;
}

drv2605l_err_t drv2605l_get_lra_period(drv2605l_dev_t *dev,
                                       uint16_t *period)
{
    if (!dev || !period) return DRV2605L_ERR_PARAM;

    uint8_t lo, hi;
    drv2605l_err_t err = reg_read(dev, DRV2605L_REG_LRA_PERIOD_LOW, &lo);
    if (err != DRV2605L_OK) return err;
    err = reg_read(dev, DRV2605L_REG_LRA_PERIOD_HIGH, &hi);
    if (err != DRV2605L_OK) return err;

    *period = (uint16_t)((hi << 8) | lo);
    return DRV2605L_OK;
}

/* ================================================================== */
/*  VelaSense Haptic Patterns                                          */
/* ================================================================== */

drv2605l_err_t vela_haptic_event_alert(drv2605l_dev_t *dev)
{
    if (!dev) return DRV2605L_ERR_PARAM;
    return drv2605l_play_effect(dev, VELA_HAPTIC_EVENT_ALERT);
}

drv2605l_err_t vela_haptic_notification(drv2605l_dev_t *dev)
{
    if (!dev) return DRV2605L_ERR_PARAM;
    return drv2605l_play_effect(dev, VELA_HAPTIC_NOTIFICATION);
}

drv2605l_err_t vela_haptic_breath_guide(drv2605l_dev_t *dev)
{
    if (!dev) return DRV2605L_ERR_PARAM;

    /*
     * 4-4-6 breathing pattern via the waveform sequencer.
     *
     * Slot 1: Pulsing strong 2 (60 %)  — INHALE   (~4 beats)
     * Slot 2: Pulsing strong 2 (60 %)  — HOLD     (~4 beats)
     * Slot 3: Pulsing strong 3 (30 %)  — EXHALE   (~6 beats, softer)
     * Slot 4: 0x00 (end of sequence)
     *
     * Each pulsing ROM effect contains a built-in pulse train whose
     * duration naturally maps to the inhale / hold / exhale timing.
     * Using effect 46 (pulsing strong 2) for the stronger phases and
     * effect 47 (pulsing strong 3) for the gentler exhale produces a
     * perceptible ramp-up-hold-ramp-down feel.
     */
    uint8_t seq[DRV2605L_WAVSEQ_MAX_SLOTS];
    memset(seq, 0x00, sizeof(seq));

    seq[0] = DRV2605L_EFFECT_PULSING_STRONG_2;  /* 46 — inhale pulse  */
    seq[1] = DRV2605L_EFFECT_PULSING_STRONG_2;  /* 46 — hold pulse    */
    seq[2] = DRV2605L_EFFECT_PULSING_STRONG_3;  /* 47 — exhale pulse  */
    /* seq[3]..[7] = 0x00 → end of sequence */

    return drv2605l_play_sequence(dev, seq, 3);
}

drv2605l_err_t vela_haptic_alarm(drv2605l_dev_t *dev)
{
    if (!dev) return DRV2605L_ERR_PARAM;

    /*
     * Play the 1000-ms alert effect (ROM effect 16) for an alarm.
     * This is a strong, sustained vibration suitable for waking or
     * critical alerts.
     */
    return drv2605l_play_effect(dev, VELA_HAPTIC_ALARM);
}

drv2605l_err_t vela_haptic_stress_alert(drv2605l_dev_t *dev)
{
    if (!dev) return DRV2605L_ERR_PARAM;

    /*
     * Ramp-up effect (ROM effect 52) — a smooth intensity increase
     * that draws the user's attention without a sudden jolt.
     */
    return drv2605l_play_effect(dev, VELA_HAPTIC_STRESS_ALERT);
}

void vela_haptic_default_config(drv2605l_config_t *config)
{
    if (!config) return;

    config->i2c_addr      = DRV2605L_I2C_ADDR;
    config->motor_type    = DRV2605L_MOTOR_LRA;
    config->library       = DRV2605L_LIB_LRA;
    config->rated_voltage = 0x53;   /* ~1.8 Vrms typical for small LRA */
    config->od_clamp      = 0x89;   /* Overdrive clamp ~2.8 V */
    config->auto_calibrate = true;
}
