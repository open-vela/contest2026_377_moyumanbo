/**
 * @file screen_breath.h
 * @brief Breathing guide screen – 4-4-6 pattern with haptic sync
 */

#ifndef SCREEN_BREATH_H
#define SCREEN_BREATH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* ── Breathing phases ─────────────────────────────────────────────── */
typedef enum {
    BREATH_IDLE = 0,
    BREATH_INHALE,      /* 4 s */
    BREATH_HOLD,        /* 4 s */
    BREATH_EXHALE,      /* 6 s */
} breath_phase_t;

/* ── Lifecycle ────────────────────────────────────────────────────── */

lv_obj_t *screen_breath_create(void);
void      screen_breath_on_show(void);
void      screen_breath_on_hide(void);
void      screen_breath_tick(void);         /* call ~100 ms */

/* ── Control ──────────────────────────────────────────────────────── */

void screen_breath_start(void);
void screen_breath_stop(void);
bool screen_breath_is_running(void);

/**
 * @brief Register a haptic callback – called on each phase transition.
 *        phase: BREATH_INHALE / HOLD / EXHALE
 *        duration_ms: how long the vibration should last
 */
typedef void (*breath_haptic_cb_t)(breath_phase_t phase, int duration_ms,
                                    void *user_data);
void screen_breath_set_haptic_cb(breath_haptic_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_BREATH_H */
