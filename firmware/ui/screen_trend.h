/**
 * @file screen_trend.h
 * @brief Trend screen – 24h HR chart + event timeline
 */

#ifndef SCREEN_TREND_H
#define SCREEN_TREND_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "screen_event.h"   /* for event_label_t */

/* ── Trend event marker ───────────────────────────────────────────── */
typedef struct {
    uint16_t       minute_of_day;   /* 0-1439 */
    event_label_t  label;
} trend_event_marker_t;

/* ── Lifecycle ────────────────────────────────────────────────────── */

lv_obj_t *screen_trend_create(void);
void      screen_trend_on_show(void);
void      screen_trend_on_hide(void);

/* ── Data setters ─────────────────────────────────────────────────── */

/**
 * @brief Provide 24 hours of HR data (one sample per minute = 1440 pts).
 *        The screen stores a pointer; caller must keep data alive.
 */
void screen_trend_set_hr_24h(const int16_t *hr_per_min, int count);

/**
 * @brief Add an event marker to the timeline.
 */
void screen_trend_add_event(const trend_event_marker_t *evt);

/**
 * @brief Clear all event markers.
 */
void screen_trend_clear_events(void);

/**
 * @brief Set daily summary values.
 */
void screen_trend_set_summary(int event_count, int avg_hr, int stress_pct);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_TREND_H */
