/**
 * @file drv2605l.h
 * @brief DRV2605L Haptic Motor Driver — VelaSense
 *
 * Complete register map, effect library, and API for the Texas Instruments
 * DRV2605L haptic driver over I2C.  Tuned for LRA (Linear Resonant Actuator)
 * operation with auto-calibration and the Energizer waveform ROM (library 6).
 *
 * @version  1.0.0
 * @date     2026-08-11
 * @author   VelaSense Firmware Team
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef DRV2605L_H
#define DRV2605L_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/*  I2C Address                                                        */
/* ------------------------------------------------------------------ */

/** 7-bit I2C slave address (factory default) */
#define DRV2605L_I2C_ADDR              (0x5A)

/* ------------------------------------------------------------------ */
/*  Register Map                                                       */
/* ------------------------------------------------------------------ */

/** Status register — device ID, fault flags */
#define DRV2605L_REG_STATUS            (0x00)

/** Mode register — operating mode select */
#define DRV2605L_REG_MODE              (0x01)

/** Real-Time Playback input value */
#define DRV2605L_REG_RTP_INPUT         (0x02)

/** Library select (1–7) */
#define DRV2605L_REG_LIBRARY_SEL       (0x03)

/** Waveform sequencer slot 1 */
#define DRV2605L_REG_WAVSEQ1           (0x04)

/** Waveform sequencer slot 2 */
#define DRV2605L_REG_WAVSEQ2           (0x05)

/** Waveform sequencer slot 3 */
#define DRV2605L_REG_WAVSEQ3           (0x06)

/** Waveform sequencer slot 4 */
#define DRV2605L_REG_WAVSEQ4           (0x07)

/** Waveform sequencer slot 5 */
#define DRV2605L_REG_WAVSEQ5           (0x08)

/** Waveform sequencer slot 6 */
#define DRV2605L_REG_WAVSEQ6           (0x09)

/** Waveform sequencer slot 7 */
#define DRV2605L_REG_WAVSEQ7           (0x0A)

/** Waveform sequencer slot 8 */
#define DRV2605L_REG_WAVSEQ8           (0x0B)

/** Go register — write 0x01 to start, 0x00 to stop */
#define DRV2605L_REG_GO                (0x0C)

/** Overdrive time (closed-loop) */
#define DRV2605L_REG_OVERDRIVE         (0x0D)

/** Sustain-time positive */
#define DRV2605L_REG_SUSTAIN_POS       (0x0E)

/** Sustain-time negative */
#define DRV2605L_REG_SUSTAIN_NEG       (0x0F)

/** Brake time */
#define DRV2605L_REG_BRAKE             (0x10)

/** Audio-to-haptic control 1 */
#define DRV2605L_REG_AUDIO_CTRL1       (0x11)

/** Audio-to-haptic control 2 */
#define DRV2605L_REG_AUDIO_CTRL2       (0x12)

/** Audio-to-haptic control 3 */
#define DRV2605L_REG_AUDIO_CTRL3       (0x13)

/** Audio-to-haptic minimum input level */
#define DRV2605L_REG_AUDIO_MIN_LV      (0x14)

/** Audio-to-haptic maximum input level */
#define DRV2605L_REG_AUDIO_MAX_LV      (0x15)

/** Rated voltage */
#define DRV2605L_REG_RATED_VOLTAGE     (0x16)

/** Overdrive clamp voltage */
#define DRV2605L_REG_OD_CLAMP          (0x17)

/** Auto-calibration compensation result */
#define DRV2605L_REG_CAL_COMP          (0x18)

/** Auto-calibration back-EMF result */
#define DRV2605L_REG_CAL_BEMF          (0x19)

/** Feedback control */
#define DRV2605L_REG_FEEDBACK_CTRL     (0x1A)

/** Control 1 */
#define DRV2605L_REG_CONTROL1          (0x1B)

/** Control 2 */
#define DRV2605L_REG_CONTROL2          (0x1C)

/** Control 3 */
#define DRV2605L_REG_CONTROL3          (0x1D)

/** Control 4 — auto-resonance, sample time */
#define DRV2605L_REG_CONTROL4          (0x1E)

/** Control 5 — n_erm_lra, blank time, idiss_time */
#define DRV2605L_REG_CONTROL5          (0x1F)

/** LRA open-loop period */
#define DRV2605L_REG_OL_LRA_PERIOD     (0x20)

/** VBAT voltage monitor */
#define DRV2605L_REG_VBAT              (0x21)

/** LRA resonance-period low byte (auto-detected) */
#define DRV2605L_REG_LRA_PERIOD_LOW    (0x22)

/** LRA resonance-period high byte (auto-detected) */
#define DRV2605L_REG_LRA_PERIOD_HIGH   (0x23)

/* ------------------------------------------------------------------ */
/*  Mode Register (0x01) Bit Field                                     */
/* ------------------------------------------------------------------ */

#define DRV2605L_MODE_INTERNAL_TRIGGER (0x00)  /**< Internal trigger (Go bit) */
#define DRV2605L_MODE_EXT_EDGE         (0x01)  /**< External edge trigger */
#define DRV2605L_MODE_EXT_LEVEL        (0x02)  /**< External level trigger */
#define DRV2605L_MODE_PWM_ANALOG       (0x03)  /**< PWM / analog input */
#define DRV2605L_MODE_AUDIO2HAPTIC     (0x04)  /**< Audio-to-haptic */
#define DRV2605L_MODE_REAL_TIME        (0x05)  /**< Real-time playback (RTP) */
#define DRV2605L_MODE_DIAGNOSTICS      (0x06)  /**< Diagnostics / auto-test */
#define DRV2605L_MODE_AUTO_CALIBRATE   (0x07)  /**< Auto-calibration */

/** Software reset (bit 7 of mode register) */
#define DRV2605L_MODE_RESET            (0x80)

/** Standby bit — enter low-power standby */
#define DRV2605L_MODE_STANDBY          (0x40)

/* ------------------------------------------------------------------ */
/*  Status Register (0x00) Bits                                        */
/* ------------------------------------------------------------------ */

#define DRV2605L_STATUS_DEVICE_ID_MASK (0xE0)  /**< Device ID [7:5] */
#define DRV2605L_STATUS_DEVICE_ID_2605L (0xA0) /**< Expected ID for DRV2605L */
#define DRV2605L_STATUS_OVER_TEMP      (0x02)  /**< Over-temperature flag */
#define DRV2605L_STATUS_OVER_CURRENT   (0x01)  /**< Over-current flag */

/* ------------------------------------------------------------------ */
/*  Feedback Control Register (0x1A) Bits                              */
/* ------------------------------------------------------------------ */

/** Bit 7: 1 = LRA mode, 0 = ERM mode */
#define DRV2605L_FB_N_ERM_LRA          (1u << 7)

/** Bits [6:4]: BEMF gain (LRA default = 0b011) */
#define DRV2605L_FB_BEMF_GAIN_SHIFT    (4)
#define DRV2605L_FB_BEMF_GAIN_MASK     (0x70)
#define DRV2605L_FB_BEMF_GAIN(g)       (((g) & 0x07) << DRV2605L_FB_BEMF_GAIN_SHIFT)

/** Bit 3: Loop gain */
#define DRV2605L_FB_LOOP_GAIN_SHIFT    (3)
#define DRV2605L_FB_LOOP_GAIN_MASK     (0x08)

/* ------------------------------------------------------------------ */
/*  Control 1 (0x1B)                                                   */
/* ------------------------------------------------------------------ */

#define DRV2605L_CTRL1_AC_COUPLE       (0x00)  /**< AC coupling (default) */
#define DRV2605L_CTRL1_DC_COUPLE       (0x01)  /**< DC coupling */
#define DRV2605L_CTRL1_DRIVE_TIME_SHIFT (3)
#define DRV2605L_CTRL1_DRIVE_TIME_MASK  (0xF8)

/* ------------------------------------------------------------------ */
/*  Control 2 (0x1C)                                                   */
/* ------------------------------------------------------------------ */

#define DRV2605L_CTRL2_BIDIR_INPUT     (0x00)  /**< Bi-directional input */
#define DRV2605L_CTRL2_UNIDIR_INPUT    (0x01)  /**< Uni-directional input */
#define DRV2605L_CTRL2_SAMPLE_TIME_SHIFT (4)
#define DRV2605L_CTRL2_BLANKING_TIME_SHIFT (0)

/* ------------------------------------------------------------------ */
/*  Control 3 (0x1D)                                                   */
/* ------------------------------------------------------------------ */

#define DRV2605L_CTRL3_NG_THRESH_SHIFT (6)
#define DRV2605L_CTRL3_NG_THRESH_MASK  (0xC0)
#define DRV2605L_CTRL3_EMBEDDED        (0x01)  /**< Use embedded ROM */

/* ------------------------------------------------------------------ */
/*  Control 4 (0x1E)                                                   */
/* ------------------------------------------------------------------ */

/** Auto-resonance enable — set for LRA */
#define DRV2605L_CTRL4_AUTO_RES        (0x01)
#define DRV2605L_CTRL4_ZC_DET_TIME     (0x00)
#define DRV2605L_CTRL4_OL_LRA_PERIOD_SHIFT (7)

/* ------------------------------------------------------------------ */
/*  Control 5 (0x1F)                                                   */
/* ------------------------------------------------------------------ */

/** Bit 0: 1 = LRA, 0 = ERM (redundant with FB register) */
#define DRV2605L_CTRL5_N_ERM_LRA       (1u << 0)
#define DRV2605L_CTRL5_BLANK_TIME_SHIFT (2)
#define DRV2605L_CTRL5_IDISS_TIME_SHIFT (5)

/* ------------------------------------------------------------------ */
/*  Go Register (0x0C) Values                                          */
/* ------------------------------------------------------------------ */

#define DRV2605L_GO                    (0x01)
#define DRV2605L_STOP                  (0x00)

/* ------------------------------------------------------------------ */
/*  Motor Type                                                         */
/* ------------------------------------------------------------------ */

typedef enum {
    DRV2605L_MOTOR_ERM = 0,            /**< Eccentric Rotating Mass */
    DRV2605L_MOTOR_LRA  = 1            /**< Linear Resonant Actuator */
} drv2605l_motor_t;

/* ------------------------------------------------------------------ */
/*  Operating Mode                                                     */
/* ------------------------------------------------------------------ */

typedef enum {
    DRV2605L_MODE_IDLE         = 0x00,
    DRV2605L_MODE_INT_TRIGGER  = 0x00,
    DRV2605L_MODE_EXT_EDGE_TRIG = 0x01,
    DRV2605L_MODE_EXT_LEVEL_TRIG = 0x02,
    DRV2605L_MODE_PWM_ANALOG   = 0x03,
    DRV2605L_MODE_AUDIO_TO_HAP = 0x04,
    DRV2605L_MODE_RTP          = 0x05,
    DRV2605L_MODE_DIAG         = 0x06,
    DRV2605L_MODE_AUTOCAL      = 0x07
} drv2605l_mode_t;

/* ------------------------------------------------------------------ */
/*  Library Selection                                                  */
/* ------------------------------------------------------------------ */

typedef enum {
    DRV2605L_LIB_EMPTY  = 0,            /**< No library (RAM only) */
    DRV2605L_LIB_A      = 1,            /**< Library A — ERM */
    DRV2605L_LIB_B      = 2,            /**< Library B — ERM */
    DRV2605L_LIB_C      = 3,            /**< Library C — ERM */
    DRV2605L_LIB_D      = 4,            /**< Library D — ERM */
    DRV2605L_LIB_E      = 5,            /**< Library E — LRA */
    DRV2605L_LIB_LRA    = 6,            /**< Library 6 — LRA (default) */
    DRV2605L_LIB_F      = 7             /**< Library F — ERM */
} drv2605l_lib_t;

/* ------------------------------------------------------------------ */
/*  Waveform Effect IDs (Energizer / LRA library)                      */
/* ------------------------------------------------------------------ */

/** Strong click — 100 % */
#define DRV2605L_EFFECT_STRONG_CLICK_100   (1)

/** Strong click — 60 % */
#define DRV2605L_EFFECT_STRONG_CLICK_60    (2)

/** Strong click — 30 % */
#define DRV2605L_EFFECT_STRONG_CLICK_30    (3)

/** Sharp click — 100 % */
#define DRV2605L_EFFECT_SHARP_CLICK_100    (4)

/** Sharp click — 60 % */
#define DRV2605L_EFFECT_SHARP_CLICK_60     (5)

/** Sharp click — 30 % */
#define DRV2605L_EFFECT_SHARP_CLICK_30     (6)

/** Soft bump — 100 % */
#define DRV2605L_EFFECT_SOFT_BUMP_100      (7)

/** Soft bump — 60 % */
#define DRV2605L_EFFECT_SOFT_BUMP_60       (8)

/** Soft bump — 30 % */
#define DRV2605L_EFFECT_SOFT_BUMP_30       (9)

/** Double click — 100 % */
#define DRV2605L_EFFECT_DOUBLE_CLICK_100   (10)

/** Double click — 60 % */
#define DRV2605L_EFFECT_DOUBLE_CLICK_60    (11)

/** Triple click — 100 % */
#define DRV2605L_EFFECT_TRIPLE_CLICK_100   (12)

/** Soft fuzz — 60 % */
#define DRV2605L_EFFECT_SOFT_FUZZ_60       (13)

/** Strong buzz — 100 % */
#define DRV2605L_EFFECT_STRONG_BUZZ_100    (14)

/** Alert — 750 ms */
#define DRV2605L_EFFECT_ALERT_750MS        (15)

/** Alert — 1000 ms */
#define DRV2605L_EFFECT_ALERT_1000MS       (16)

/** Strong click 1 — 100 % */
#define DRV2605L_EFFECT_STRONG_CLICK1_100  (17)

/** Strong click 2 — 80 % */
#define DRV2605L_EFFECT_STRONG_CLICK2_80   (18)

/** Strong click 3 — 60 % */
#define DRV2605L_EFFECT_STRONG_CLICK3_60   (19)

/** Strong click 4 — 30 % */
#define DRV2605L_EFFECT_STRONG_CLICK4_30   (20)

/** Medium click 1 — 100 % */
#define DRV2605L_EFFECT_MEDIUM_CLICK1_100  (21)

/** Medium click 2 — 80 % */
#define DRV2605L_EFFECT_MEDIUM_CLICK2_80   (22)

/** Sharp tick 1 — 100 % */
#define DRV2605L_EFFECT_SHARP_TICK1_100    (23)

/** Sharp tick 2 — 80 % */
#define DRV2605L_EFFECT_SHARP_TICK2_80     (24)

/** Short double click strong 1 — 100 % */
#define DRV2605L_EFFECT_SH_DBL_CLICK_STRONG_1 (25)

/** Short double click strong 2 — 60 % */
#define DRV2605L_EFFECT_SH_DBL_CLICK_STRONG_2 (26)

/** Short double click strong 3 — 30 % */
#define DRV2605L_EFFECT_SH_DBL_CLICK_STRONG_3 (27)

/** Short double click medium 1 — 100 % */
#define DRV2605L_EFFECT_SH_DBL_CLICK_MED_1 (28)

/** Short double click medium 2 — 80 % */
#define DRV2605L_EFFECT_SH_DBL_CLICK_MED_2 (29)

/** Short double click medium 3 — 60 % */
#define DRV2605L_EFFECT_SH_DBL_CLICK_MED_3 (30)

/** Short double sharp tick 1 — 100 % */
#define DRV2605L_EFFECT_SH_DBL_SHARP_TICK1 (31)

/** Short double sharp tick 2 — 80 % */
#define DRV2605L_EFFECT_SH_DBL_SHARP_TICK2 (32)

/** Long double sharp click strong — 100 % */
#define DRV2605L_EFFECT_LG_DBL_CLICK_STRONG_1 (33)

/** Long double sharp click strong — 60 % */
#define DRV2605L_EFFECT_LG_DBL_CLICK_STRONG_2 (34)

/** Long double sharp click strong — 30 % */
#define DRV2605L_EFFECT_LG_DBL_CLICK_STRONG_3 (35)

/** Long double sharp click medium 1 — 100 % */
#define DRV2605L_EFFECT_LG_DBL_CLICK_MED_1 (36)

/** Long double sharp click medium 2 — 60 % */
#define DRV2605L_EFFECT_LG_DBL_CLICK_MED_2 (37)

/** Long double sharp tick 1 — 100 % */
#define DRV2605L_EFFECT_LG_DBL_SHARP_TICK1 (38)

/** Long double sharp tick 2 — 80 % */
#define DRV2605L_EFFECT_LG_DBL_SHARP_TICK2 (39)

/** Buzz 1 — 100 % */
#define DRV2605L_EFFECT_BUZZ_100           (40)

/** Buzz 2 — 60 % */
#define DRV2605L_EFFECT_BUZZ_60            (41)

/** Buzz 3 — 30 % */
#define DRV2605L_EFFECT_BUZZ_30            (42)

/** Buzz 4 — 100 % */
#define DRV2605L_EFFECT_BUZZ100_100        (43)

/** Buzz 5 — 60 % */
#define DRV2605L_EFFECT_BUZZ100_60         (44)

/** Pulsing strong 1 — 100 % */
#define DRV2605L_EFFECT_PULSING_STRONG_1   (45)

/** Pulsing strong 2 — 60 % */
#define DRV2605L_EFFECT_PULSING_STRONG_2   (46)

/** Pulsing strong 3 — 30 % */
#define DRV2605L_EFFECT_PULSING_STRONG_3   (47)

/** Pulsing medium 1 — 100 % */
#define DRV2605L_EFFECT_PULSING_MED_1      (48)

/** Pulsing medium 2 — 60 % */
#define DRV2605L_EFFECT_PULSING_MED_2      (49)

/** Pulsing medium 3 — 30 % */
#define DRV2605L_EFFECT_PULSING_MED_3      (50)

/** Transition ramp up long smooth 1 */
#define DRV2605L_EFFECT_TRANS_RAMP_UP_LONG_SMOOTH_1 (51)

/** Transition ramp up long smooth 2 */
#define DRV2605L_EFFECT_TRANS_RAMP_UP_LONG_SMOOTH_2 (52)

/** Transition ramp up medium smooth 1 */
#define DRV2605L_EFFECT_TRANS_RAMP_UP_MED_SMOOTH_1  (53)

/** Transition ramp up medium smooth 2 */
#define DRV2605L_EFFECT_TRANS_RAMP_UP_MED_SMOOTH_2  (54)

/** Transition ramp up short smooth 1 */
#define DRV2605L_EFFECT_TRANS_RAMP_UP_SHORT_SMOOTH_1 (55)

/** Transition ramp up short smooth 2 */
#define DRV2605L_EFFECT_TRANS_RAMP_UP_SHORT_SMOOTH_2 (56)

/** Transition ramp up long sharp 1 */
#define DRV2605L_EFFECT_TRANS_RAMP_UP_LONG_SHARP_1   (57)

/** Transition ramp up long sharp 2 */
#define DRV2605L_EFFECT_TRANS_RAMP_UP_LONG_SHARP_2   (58)

/** Transition ramp up medium sharp 1 */
#define DRV2605L_EFFECT_TRANS_RAMP_UP_MED_SHARP_1    (59)

/** Transition ramp up medium sharp 2 */
#define DRV2605L_EFFECT_TRANS_RAMP_UP_MED_SHARP_2    (60)

/** Transition ramp up short sharp 1 */
#define DRV2605L_EFFECT_TRANS_RAMP_UP_SHORT_SHARP_1  (61)

/** Transition ramp up short sharp 2 */
#define DRV2605L_EFFECT_TRANS_RAMP_UP_SHORT_SHARP_2  (62)

/** Transition ramp down long smooth 1 */
#define DRV2605L_EFFECT_TRANS_RAMP_DN_LONG_SMOOTH_1  (63)

/** Transition ramp down long smooth 2 */
#define DRV2605L_EFFECT_TRANS_RAMP_DN_LONG_SMOOTH_2  (64)

/** Transition ramp down medium smooth 1 */
#define DRV2605L_EFFECT_TRANS_RAMP_DN_MED_SMOOTH_1   (65)

/** Transition ramp down medium smooth 2 */
#define DRV2605L_EFFECT_TRANS_RAMP_DN_MED_SMOOTH_2   (66)

/** Transition ramp down short smooth 1 */
#define DRV2605L_EFFECT_TRANS_RAMP_DN_SHORT_SMOOTH_1 (67)

/** Transition ramp down short smooth 2 */
#define DRV2605L_EFFECT_TRANS_RAMP_DN_SHORT_SMOOTH_2 (68)

/** Transition ramp down long sharp 1 */
#define DRV2605L_EFFECT_TRANS_RAMP_DN_LONG_SHARP_1   (69)

/** Transition ramp down long sharp 2 */
#define DRV2605L_EFFECT_TRANS_RAMP_DN_LONG_SHARP_2   (70)

/** Transition ramp down medium sharp 1 */
#define DRV2605L_EFFECT_TRANS_RAMP_DN_MED_SHARP_1    (71)

/** Transition ramp down medium sharp 2 */
#define DRV2605L_EFFECT_TRANS_RAMP_DN_MED_SHARP_2    (72)

/** Transition ramp down short sharp 1 */
#define DRV2605L_EFFECT_TRANS_RAMP_DN_SHORT_SHARP_1  (73)

/** Transition ramp down short sharp 2 */
#define DRV2605L_EFFECT_TRANS_RAMP_DN_SHORT_SHARP_2  (74)

/** Transition ramp up long smooth 1 — 50 % */
#define DRV2605L_EFFECT_LONG_TRANS_1     (75)

/** Transition ramp up long smooth 2 — 50 % */
#define DRV2605L_EFFECT_LONG_TRANS_2     (76)

/** Smooth hum 1 — 50 % */
#define DRV2605L_EFFECT_SMOOTH_HUM1_50   (101)

/** Smooth hum 2 — 40 % */
#define DRV2605L_EFFECT_SMOOTH_HUM2_40   (102)

/** Smooth hum 3 — 30 % */
#define DRV2605L_EFFECT_SMOOTH_HUM3_30   (103)

/** Smooth hum 4 — 20 % */
#define DRV2605L_EFFECT_SMOOTH_HUM4_20   (104)

/** Smooth hum 5 — 10 % */
#define DRV2605L_EFFECT_SMOOTH_HUM5_10   (105)

/* ------------------------------------------------------------------ */
/*  VelaSense Haptic Effect Aliases                                    */
/* ------------------------------------------------------------------ */

/** Strong click for event alerts */
#define VELA_HAPTIC_EVENT_ALERT         DRV2605L_EFFECT_STRONG_CLICK_100

/** Soft buzz for notifications */
#define VELA_HAPTIC_NOTIFICATION        DRV2605L_EFFECT_SOFT_BUMP_60

/** Pulsing for breathing guide (slot entry) */
#define VELA_HAPTIC_BREATH_PULSE        DRV2605L_EFFECT_PULSING_STRONG_2

/** Long buzz for alarm (1-second alert) */
#define VELA_HAPTIC_ALARM               DRV2605L_EFFECT_ALERT_1000MS

/** Ramp up for stress alert */
#define VELA_HAPTIC_STRESS_ALERT        DRV2605L_EFFECT_TRANS_RAMP_UP_LONG_SMOOTH_2

/* ------------------------------------------------------------------ */
/*  Waveform Sequencer Entry Flags                                     */
/* ------------------------------------------------------------------ */

/**
 * OR this with an effect ID to cause a wait-for-trigger before playing.
 * Only meaningful in sequencer registers 2–8.
 */
#define DRV2605L_WAVSEQ_WAIT_TRIGGER    (0x80)

/**
 * End-of-sequence marker.  OR with 0x00 and write to the next sequencer slot
 * to stop playback after the preceding entry.
 */
#define DRV2605L_WAVSEQ_END             (0x00)

/** Maximum number of sequencer slots */
#define DRV2605L_WAVSEQ_MAX_SLOTS       (8)

/* ------------------------------------------------------------------ */
/*  Return Codes                                                       */
/* ------------------------------------------------------------------ */

typedef enum {
    DRV2605L_OK            =  0,       /**< Success */
    DRV2605L_ERR_I2C       = -1,       /**< I2C transaction failed */
    DRV2605L_ERR_TIMEOUT   = -2,       /**< Operation timed out */
    DRV2605L_ERR_PARAM     = -3,       /**< Invalid parameter */
    DRV2605L_ERR_ID        = -4,       /**< Device ID mismatch */
    DRV2605L_ERR_BUSY      = -5,       /**< Driver busy (playback active) */
    DRV2605L_ERR_FAULT     = -6        /**< Hardware fault detected */
} drv2605l_err_t;

/* ------------------------------------------------------------------ */
/*  Driver Configuration (passed at init time)                         */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t  i2c_addr;                  /**< 7-bit I2C address (default 0x5A) */
    drv2605l_motor_t motor_type;        /**< ERM or LRA */
    drv2605l_lib_t   library;           /**< Waveform library (default LIB_LRA) */
    uint8_t  rated_voltage;             /**< Motor rated voltage (register value) */
    uint8_t  od_clamp;                  /**< Overdrive clamp voltage */
    bool     auto_calibrate;            /**< Run auto-calibration on init */
} drv2605l_config_t;

/* ------------------------------------------------------------------ */
/*  Default Configuration Macro                                        */
/* ------------------------------------------------------------------ */

/**
 * Sensible defaults for a VelaSense LRA motor.
 * Rated-voltage and clamp values assume a typical 2 Vrms LRA part.
 * Adjust for your specific actuator.
 */
#define DRV2605L_DEFAULT_CONFIG() {            \
    .i2c_addr      = DRV2605L_I2C_ADDR,       \
    .motor_type    = DRV2605L_MOTOR_LRA,       \
    .library       = DRV2605L_LIB_LRA,         \
    .rated_voltage = 0x53,                     \
    .od_clamp      = 0x89,                     \
    .auto_calibrate = true                     \
}

/* ------------------------------------------------------------------ */
/*  Opaque Driver Handle                                               */
/* ------------------------------------------------------------------ */

/**
 * Forward declaration — the concrete struct lives in drv2605l.c so that
 * platform-specific I2C state can be hidden.
 */
typedef struct drv2605l_dev drv2605l_dev_t;

/* ------------------------------------------------------------------ */
/*  Platform I2C Abstraction (must be implemented by the BSP)          */
/* ------------------------------------------------------------------ */

/**
 * Write @p len bytes starting at @p reg over I2C.
 * @param addr   7-bit I2C slave address.
 * @param reg    Register address (first byte of payload).
 * @param data   Pointer to data bytes to write.
 * @param len    Number of data bytes (excluding register address).
 * @return 0 on success, negative on error.
 */
typedef int (*drv2605l_i2c_write_fn)(uint8_t addr, uint8_t reg,
                                     const uint8_t *data, uint8_t len);

/**
 * Read @p len bytes starting at @p reg over I2C.
 * @param addr   7-bit I2C slave address.
 * @param reg    Register address.
 * @param data   Buffer to receive read data.
 * @param len    Number of bytes to read.
 * @return 0 on success, negative on error.
 */
typedef int (*drv2605l_i2c_read_fn)(uint8_t addr, uint8_t reg,
                                    uint8_t *data, uint8_t len);

/**
 * Delay for @p ms milliseconds.
 * @param ms  Milliseconds to wait.
 */
typedef void (*drv2605l_delay_fn)(uint32_t ms);

/**
 * Platform callbacks — provide at init time.
 */
typedef struct {
    drv2605l_i2c_write_fn i2c_write;
    drv2605l_i2c_read_fn  i2c_read;
    drv2605l_delay_fn     delay_ms;
} drv2605l_platform_t;

/* ------------------------------------------------------------------ */
/*  Core API                                                           */
/* ------------------------------------------------------------------ */

/**
 * Initialise the DRV2605L device.
 *
 * Performs a software reset, verifies the device ID, selects the motor
 * type and waveform library, configures rated / clamp voltages, and
 * optionally runs LRA auto-calibration.
 *
 * @param[out] dev      Pointer to caller-allocated device handle.
 * @param[in]  config   Driver configuration.
 * @param[in]  plat     Platform I2C / delay callbacks.
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t drv2605l_init(drv2605l_dev_t *dev,
                             const drv2605l_config_t *config,
                             const drv2605l_platform_t *plat);

/**
 * Software-reset the device and re-initialise.
 *
 * @param[in] dev  Device handle.
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t drv2605l_reset(drv2605l_dev_t *dev);

/**
 * Enter low-power standby mode.
 *
 * @param[in] dev    Device handle.
 * @param[in] enter  true = enter standby, false = wake.
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t drv2605l_standby(drv2605l_dev_t *dev, bool enter);

/* ------------------------------------------------------------------ */
/*  Playback API                                                       */
/* ------------------------------------------------------------------ */

/**
 * Play a single waveform effect by its ROM ID (1–123).
 *
 * Uses internal trigger mode — the effect starts immediately and the
 * function blocks until playback completes.
 *
 * @param[in] dev       Device handle.
 * @param[in] effect_id ROM effect ID (1–123).
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t drv2605l_play_effect(drv2605l_dev_t *dev, uint8_t effect_id);

/**
 * Play a single waveform effect by its ROM ID without blocking.
 *
 * The caller must call drv2605l_wait_done() when ready to wait for
 * completion, or use drv2605l_is_busy() to poll.
 *
 * @param[in] dev       Device handle.
 * @param[in] effect_id ROM effect ID (1–123).
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t drv2605l_play_effect_async(drv2605l_dev_t *dev,
                                          uint8_t effect_id);

/**
 * Play a sequence of waveform effects (up to 8 entries).
 *
 * The sequence is written to the waveform sequencer registers 1–8 and
 * played back using internal trigger mode.
 *
 * @param[in] dev       Device handle.
 * @param[in] seq       Array of effect IDs.  Set an element to 0 to
 *                      terminate the sequence early.
 * @param[in] count     Number of entries in @p seq (max 8).
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t drv2605l_play_sequence(drv2605l_dev_t *dev,
                                      const uint8_t *seq,
                                      uint8_t count);

/**
 * Stop any in-progress playback immediately.
 *
 * @param[in] dev  Device handle.
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t drv2605l_stop(drv2605l_dev_t *dev);

/**
 * Block until the current playback finishes.
 *
 * @param[in] dev      Device handle.
 * @param[in] timeout_ms  Maximum wait time in milliseconds.
 * @return DRV2605L_OK on done, DRV2605L_ERR_TIMEOUT if timed out.
 */
drv2605l_err_t drv2605l_wait_done(drv2605l_dev_t *dev, uint32_t timeout_ms);

/**
 * Check whether a playback is in progress.
 *
 * @param[in]  dev   Device handle.
 * @param[out] busy  true if Go bit is still set.
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t drv2605l_is_busy(drv2605l_dev_t *dev, bool *busy);

/* ------------------------------------------------------------------ */
/*  Real-Time Playback (RTP) API                                       */
/* ------------------------------------------------------------------ */

/**
 * Enter real-time playback mode.
 *
 * After calling this, the caller should write raw amplitude values
 * (0x00–0x7F, or 0x00–0xFF signed/unsigned depending on Control 3
 * settings) via drv2605l_rtp_write().
 *
 * @param[in] dev  Device handle.
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t drv2605l_rtp_enter(drv2605l_dev_t *dev);

/**
 * Write a single amplitude value in RTP mode.
 *
 * @param[in] dev     Device handle.
 * @param[in] value   Amplitude byte (0x00–0xFF, signed or unsigned
 *                    depending on control register settings).
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t drv2605l_rtp_write(drv2605l_dev_t *dev, uint8_t value);

/**
 * Exit RTP mode and return to internal trigger mode.
 *
 * @param[in] dev  Device handle.
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t drv2605l_rtp_exit(drv2605l_dev_t *dev);

/* ------------------------------------------------------------------ */
/*  Register Access (low-level, for diagnostics)                       */
/* ------------------------------------------------------------------ */

/**
 * Read a single register.
 *
 * @param[in]  dev   Device handle.
 * @param[in]  reg   Register address.
 * @param[out] val   Pointer to receive register value.
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t drv2605l_read_reg(drv2605l_dev_t *dev,
                                 uint8_t reg, uint8_t *val);

/**
 * Write a single register.
 *
 * @param[in] dev  Device handle.
 * @param[in] reg  Register address.
 * @param[in] val  Value to write.
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t drv2605l_write_reg(drv2605l_dev_t *dev,
                                  uint8_t reg, uint8_t val);

/* ------------------------------------------------------------------ */
/*  Diagnostics                                                        */
/* ------------------------------------------------------------------ */

/**
 * Read the status register and check for faults.
 *
 * @param[in]  dev            Device handle.
 * @param[out] over_temp      true if over-temperature flag is set.
 * @param[out] over_current   true if over-current flag is set.
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t drv2605l_get_status(drv2605l_dev_t *dev,
                                   bool *over_temp,
                                   bool *over_current);

/**
 * Read the supply voltage monitor register (0x21).
 *
 * @param[in]  dev   Device handle.
 * @param[out] vbat  Raw ADC value (0–255 maps to 0–5.6 V).
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t drv2605l_get_vbat(drv2605l_dev_t *dev, uint8_t *vbat);

/**
 * Read the auto-calibration results (compensation and back-EMF).
 *
 * @param[in]  dev    Device handle.
 * @param[out] comp   Calibration compensation value.
 * @param[out] bemf   Back-EMF result.
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t drv2605l_get_cal_result(drv2605l_dev_t *dev,
                                       uint8_t *comp,
                                       uint8_t *bemf);

/**
 * Read the auto-detected LRA resonance period.
 *
 * @param[in]  dev     Device handle.
 * @param[out] period  Period in 137-us units (from registers 0x22–0x23).
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t drv2605l_get_lra_period(drv2605l_dev_t *dev,
                                       uint16_t *period);

/* ------------------------------------------------------------------ */
/*  VelaSense Haptic Patterns                                          */
/* ------------------------------------------------------------------ */

/**
 * Play a strong-click event alert (single short pulse).
 *
 * @param[in] dev  Device handle.
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t vela_haptic_event_alert(drv2605l_dev_t *dev);

/**
 * Play a soft-buzz notification.
 *
 * @param[in] dev  Device handle.
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t vela_haptic_notification(drv2605l_dev_t *dev);

/**
 * Play the 4-4-6 breathing-guide haptic pattern via the sequencer.
 *
 * Sequence: pulse – pause – longer pulse (inhale – hold – exhale).
 * Uses the waveform sequencer to chain four pulsing-effect entries
 * with wait-for-trigger bits cleared so they play back-to-back.
 *
 * @param[in] dev  Device handle.
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t vela_haptic_breath_guide(drv2605l_dev_t *dev);

/**
 * Play a long buzz alarm (approximately 1-second strong vibration).
 *
 * @param[in] dev  Device handle.
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t vela_haptic_alarm(drv2605l_dev_t *dev);

/**
 * Play a ramp-up stress alert (gradual intensity increase).
 *
 * @param[in] dev  Device handle.
 * @return DRV2605L_OK on success.
 */
drv2605l_err_t vela_haptic_stress_alert(drv2605l_dev_t *dev);

/* ------------------------------------------------------------------ */
/*  Initialization Helper                                              */
/* ------------------------------------------------------------------ */

/**
 * Convenience: create a VelaSense-default device with LRA, library 6,
 * auto-calibration enabled, and sensible voltage defaults.
 *
 * This function allocates nothing — it fills @p config with the
 * recommended VelaSense defaults.
 *
 * @param[out] config  Configuration struct to populate.
 */
void vela_haptic_default_config(drv2605l_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* DRV2605L_H */
