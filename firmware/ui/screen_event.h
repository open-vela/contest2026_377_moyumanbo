/**
 * @file screen_event.h
 * @brief Event confirmation screen – vibration alert with 5-choice label
 */

#ifndef SCREEN_EVENT_H
#define SCREEN_EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* ── Event labels (user selection) ────────────────────────────────── */
typedef enum {
    EVENT_LABEL_EXCITEMENT = 0,
    EVENT_LABEL_NERVOUS,
    EVENT_LABEL_SURPRISE,
    EVENT_LABEL_STRESS,
    EVENT_LABEL_FALSE_POS,
    EVENT_LABEL_COUNT,
    EVENT_LABEL_NONE = -1,
} event_label_t;

/* ── Lifecycle ────────────────────────────────────────────────────── */

lv_obj_t *screen_event_create(void);
void      screen_event_reset_timer(void);
void      screen_event_tick(void);          /* call ~100 ms */
bool      screen_event_is_timed_out(void);

/**
 * @brief Returns the user's chosen label, or EVENT_LABEL_NONE if not yet chosen
 *        or if the timer expired.
 */
event_label_t screen_event_get_label(void);

/**
 * @brief Register a callback that fires when the user picks a label
 *        or when the timer expires (label == EVENT_LABEL_NONE).
 */
typedef void (*event_label_cb_t)(event_label_t label, void *user_data);
void screen_event_set_callback(event_label_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_EVENT_H */
