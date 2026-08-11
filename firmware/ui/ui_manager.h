/**
 * @file ui_manager.h
 * @brief Screen lifecycle manager with swipe navigation for VelaSense
 *
 * Navigation model (horizontal swipe chain):
 *   Settings  <--swipe-right--  Home  --swipe-left-->  Trend
 *                                                       |
 *                                                  Breathing
 *
 * screen_event is a modal overlay triggered by the health engine.
 */

#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* ── Screen IDs ───────────────────────────────────────────────────── */
typedef enum {
    UI_SCREEN_HOME = 0,
    UI_SCREEN_TREND,
    UI_SCREEN_BREATH,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_EVENT,          /* modal – not in the swipe chain */
    UI_SCREEN_COUNT,
} ui_screen_id_t;

/* ── Transition direction ─────────────────────────────────────────── */
typedef enum {
    UI_TRANS_NONE = 0,
    UI_TRANS_LEFT,
    UI_TRANS_RIGHT,
    UI_TRANS_TOP,             /* swipe down overlay */
    UI_TRANS_BOTTOM,          /* swipe up overlay  */
} ui_trans_dir_t;

/* ── Manager lifecycle ────────────────────────────────────────────── */

/**
 * @brief Initialise the manager and create all screens.
 *        Call after ui_theme_init().
 */
void ui_manager_init(void);

/**
 * @brief Navigate to a screen with a slide animation.
 * @param id   Target screen
 * @param dir  Animation direction (UI_TRANS_NONE for instant jump)
 */
void ui_manager_goto(ui_screen_id_t id, ui_trans_dir_t dir);

/**
 * @brief Show the event-confirmation modal (slides up).
 */
void ui_manager_show_event(void);

/**
 * @brief Dismiss the event modal and return to the previous screen.
 */
void ui_manager_dismiss_event(void);

/**
 * @brief Returns the currently active screen ID.
 */
ui_screen_id_t ui_manager_get_current(void);

/**
 * @brief Periodic tick – call from the main loop (~100 ms).
 *        Drives timeouts and background animations.
 */
void ui_manager_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_MANAGER_H */
