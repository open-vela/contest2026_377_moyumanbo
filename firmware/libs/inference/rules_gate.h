/**
 * @file rules_gate.h
 * @brief Rule-based gating layer that runs *before* ML inference.
 *
 * The rule gate inspects raw sensor health signals and decides whether
 * the current sample is reliable enough to feed into the arousal classifier.
 * Unreliable samples are rejected early, saving compute and avoiding
 * spurious arousal events.
 *
 * Decision flow (evaluated in priority order):
 *
 *   1. Battery < 5 %           -> SKIP  "low_battery"
 *   2. Skin contact lost        -> SKIP  "not_worn"
 *   3. SQI < 0.70               -> SKIP  "adjust_strap"
 *   4. Temperature slope > 2 C  -> SKIP  "temp_drift"
 *   5. Activity >= running      -> SKIP  "high_activity"
 *   Otherwise                   -> PROCEED
 *
 * @version 1.0.0
 */

#ifndef VELASENSE_RULES_GATE_H
#define VELASENSE_RULES_GATE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Decision codes
 * ---------------------------------------------------------------------------*/
typedef enum {
    RULE_GATE_PROCEED = 0,       /**< All checks passed; safe to run inference */
    RULE_GATE_SKIP    = 1,       /**< At least one rule triggered a skip        */
} rule_gate_decision_t;

/* ---------------------------------------------------------------------------
 * Gate result (decision + machine-readable reason code)
 * ---------------------------------------------------------------------------*/
typedef struct {
    rule_gate_decision_t decision;
    const char          *reason;    /**< Human-readable reason string (static) */
} rule_gate_result_t;

/* ---------------------------------------------------------------------------
 * Sensor health snapshot (filled by the caller before each gate check)
 * ---------------------------------------------------------------------------*/
typedef struct {
    /* ---- PPG / HR quality ------------------------------------------------ */
    float    ppg_sqi;              /**< Signal quality index  [0..1]            */
    bool     skin_contact;         /**< true = electrode/sensor on skin         */

    /* ---- Activity / posture (from IMU) ----------------------------------- */
    uint8_t  activity_intensity;   /**< 0=rest 1=light 2=moderate 3=vigorous    */

    /* ---- Temperature ----------------------------------------------------- */
    float    skin_temp_slope;      /**< deg-C per minute (+/-)                  */

    /* ---- Power ----------------------------------------------------------- */
    uint8_t  battery_pct;          /**< Remaining battery  [0..100]             */
} rule_gate_sensor_t;

/* ---------------------------------------------------------------------------
 * Tunable thresholds (loaded from Kconfig defaults, can be overridden)
 * ---------------------------------------------------------------------------*/
typedef struct {
    float    sqi_min;              /**< Minimum acceptable SQI (default 0.70)   */
    float    temp_slope_max;       /**< Max temp slope in deg-C/min (default 2) */
    uint8_t  activity_max;         /**< Max activity level to proceed (default 2=moderate) */
    uint8_t  battery_min;          /**< Minimum battery % (default 5)           */
} rule_gate_config_t;

/* ---------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------*/

/**
 * @brief Initialise the rule gate with Kconfig defaults (or custom config).
 *
 * @param[in] cfg  Threshold overrides, or NULL for Kconfig defaults.
 */
void rule_gate_init(const rule_gate_config_t *cfg);

/**
 * @brief Evaluate all gating rules against the current sensor snapshot.
 *
 * This function is designed to be called once per sample, immediately
 * before tinyml_predict().  It is allocation-free and takes < 5 us
 * on Cortex-M33.
 *
 * @param[in]  sensor  Current sensor health data.
 * @param[out] result  Decision and reason code.
 */
void rule_gate_evaluate(const rule_gate_sensor_t *sensor,
                        rule_gate_result_t       *result);

/**
 * @brief Return the currently active configuration (read-only).
 */
const rule_gate_config_t *rule_gate_get_config(void);

#ifdef __cplusplus
}
#endif

#endif /* VELASENSE_RULES_GATE_H */
