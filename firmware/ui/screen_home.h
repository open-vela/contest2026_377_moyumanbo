/**
 * @file screen_home.h
 * @brief Home screen – real-time HR, activity, SQI, battery
 */

#ifndef SCREEN_HOME_H
#define SCREEN_HOME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* ── Activity states ──────────────────────────────────────────────── */
typedef enum {
    HOME_ACT_SIT  = 0,
    HOME_ACT_STAND,
    HOME_ACT_WALK,
    HOME_ACT_RUN,
    HOME_ACT_COUNT,
} home_activity_t;

/* ── Lifecycle ────────────────────────────────────────────────────── */

lv_obj_t *screen_home_create(void);
void      screen_home_on_show(void);
void      screen_home_on_hide(void);
void      screen_home_tick(void);       /* call ~250 ms */

/* ── Data setters (called from the sensor/health engine) ──────────── */
void screen_home_set_hr(int bpm);
void screen_home_set_activity(home_activity_t act);
void screen_home_set_sqi(int pct);           /* 0-100 */
void screen_home_set_battery(int pct);       /* 0-100 */
void screen_home_set_hr_history(const int *bpm_array, int count);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_HOME_H */
