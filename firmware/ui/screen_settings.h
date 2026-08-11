/**
 * @file screen_settings.h
 * @brief Settings screen – BLE, privacy, notifications, about
 */

#ifndef SCREEN_SETTINGS_H
#define SCREEN_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* ── Setting IDs ──────────────────────────────────────────────────── */
typedef enum {
    SETTING_BLE_ENABLED = 0,
    SETTING_EVENT_ALERTS,
    SETTING_BREATH_REMIND,
    SETTING_COUNT,
} setting_id_t;

/* ── Lifecycle ────────────────────────────────────────────────────── */

lv_obj_t *screen_settings_create(void);
void      screen_settings_on_show(void);
void      screen_settings_on_hide(void);

/* ── Data ─────────────────────────────────────────────────────────── */

bool screen_settings_get(setting_id_t id);
void screen_settings_set(setting_id_t id, bool value);

/**
 * @brief Register a callback for data-clear / factory-reset actions.
 *        action: 0 = clear data, 1 = factory reset
 */
typedef void (*settings_action_cb_t)(int action, void *user_data);
void screen_settings_set_action_cb(settings_action_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_SETTINGS_H */
