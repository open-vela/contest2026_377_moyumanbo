/**
 * @file rules_gate.c
 * @brief Rule-based gating implementation.
 *
 * Evaluates five blocking rules in priority order against a sensor health
 * snapshot.  If any rule fires the inference is skipped and a reason
 * string is returned so the UI can display guidance (e.g. "adjust strap").
 *
 * All comparisons use the thresholds stored in the static config struct,
 * which is populated from Kconfig defaults on init but can be overridden
 * at runtime for testing.
 *
 * @version 1.0.0
 */

#include "rules_gate.h"
#include <stddef.h>

/* =========================================================================
 * Kconfig defaults (compiled in; see Kconfig for documentation)
 * ========================================================================= */

#ifndef CONFIG_RULE_SQI_MIN
#define CONFIG_RULE_SQI_MIN         0.70f
#endif

#ifndef CONFIG_RULE_TEMP_SLOPE_MAX
#define CONFIG_RULE_TEMP_SLOPE_MAX  2.0f
#endif

#ifndef CONFIG_RULE_ACTIVITY_MAX
#define CONFIG_RULE_ACTIVITY_MAX    2    /* 0=rest 1=light 2=moderate 3=vigorous */
#endif

#ifndef CONFIG_RULE_BATTERY_MIN
#define CONFIG_RULE_BATTERY_MIN     5    /* percent */
#endif

/* =========================================================================
 * Module state
 * ========================================================================= */

static rule_gate_config_t s_config = {
    .sqi_min         = CONFIG_RULE_SQI_MIN,
    .temp_slope_max  = CONFIG_RULE_TEMP_SLOPE_MAX,
    .activity_max    = CONFIG_RULE_ACTIVITY_MAX,
    .battery_min     = CONFIG_RULE_BATTERY_MIN,
};

/* Reason strings (static storage, returned by pointer) */
static const char REASON_LOW_BATTERY[]  = "low_battery";
static const char REASON_NOT_WORN[]     = "not_worn";
static const char REASON_ADJUST_STRAP[] = "adjust_strap";
static const char REASON_TEMP_DRIFT[]   = "temp_drift";
static const char REASON_HIGH_ACTIVITY[] = "high_activity";
static const char REASON_OK[]           = "ok";

/* =========================================================================
 * Public API
 * ========================================================================= */

void rule_gate_init(const rule_gate_config_t *cfg)
{
    if (cfg) {
        s_config = *cfg;
    }
    /* Otherwise keep the compile-time Kconfig defaults. */
}

const rule_gate_config_t *rule_gate_get_config(void)
{
    return &s_config;
}

void rule_gate_evaluate(const rule_gate_sensor_t *sensor,
                        rule_gate_result_t       *result)
{
    if (!sensor || !result) {
        if (result) {
            result->decision = RULE_GATE_SKIP;
            result->reason   = "null_input";
        }
        return;
    }

    /*
     * Evaluate rules in priority order.  The first rule that matches
     * terminates the check and returns its reason.
     *
     * Priority (highest first):
     *   1. Battery critically low  -> save power, skip everything
     *   2. No skin contact         -> sensor not worn, data is garbage
     *   3. Poor PPG quality        -> prompt user to adjust strap
     *   4. Rapid temperature swing -> likely environmental, not emotional
     *   5. High physical activity  -> arousal is from exertion, not emotion
     */

    /* ---- Rule 1: Low battery -------------------------------------------- */
    if (sensor->battery_pct < s_config.battery_min) {
        result->decision = RULE_GATE_SKIP;
        result->reason   = REASON_LOW_BATTERY;
        return;
    }

    /* ---- Rule 2: Not worn (no skin contact) ----------------------------- */
    if (!sensor->skin_contact) {
        result->decision = RULE_GATE_SKIP;
        result->reason   = REASON_NOT_WORN;
        return;
    }

    /* ---- Rule 3: Poor signal quality ------------------------------------ */
    if (sensor->ppg_sqi < s_config.sqi_min) {
        result->decision = RULE_GATE_SKIP;
        result->reason   = REASON_ADJUST_STRAP;
        return;
    }

    /* ---- Rule 4: Rapid temperature change (environmental artefact) ------ */
    float abs_slope = sensor->skin_temp_slope;
    if (abs_slope < 0.0f) abs_slope = -abs_slope;
    if (abs_slope > s_config.temp_slope_max) {
        result->decision = RULE_GATE_SKIP;
        result->reason   = REASON_TEMP_DRIFT;
        return;
    }

    /* ---- Rule 5: High physical activity --------------------------------- */
    if (sensor->activity_intensity > s_config.activity_max) {
        result->decision = RULE_GATE_SKIP;
        result->reason   = REASON_HIGH_ACTIVITY;
        return;
    }

    /* ---- All checks passed ---------------------------------------------- */
    result->decision = RULE_GATE_PROCEED;
    result->reason   = REASON_OK;
}
