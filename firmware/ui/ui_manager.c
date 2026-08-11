/**
 * @file ui_manager.c
 * @brief Screen manager implementation – swipe navigation and lifecycle
 */

#include "ui_manager.h"
#include "ui_theme.h"

#include "screen_home.h"
#include "screen_trend.h"
#include "screen_breath.h"
#include "screen_settings.h"
#include "screen_event.h"

/* ── Private state ────────────────────────────────────────────────── */

static lv_obj_t        *scr_table[UI_SCREEN_COUNT];  /* root obj per screen */
static ui_screen_id_t   cur_screen  = UI_SCREEN_HOME;
static ui_screen_id_t   prev_screen = UI_SCREEN_HOME;
static bool             event_visible = false;

/* Swipe-gesture tracking */
static lv_coord_t  touch_start_x;
static lv_coord_t  touch_start_y;
static bool        touch_active = false;

#define SWIPE_THRESHOLD  50   /* px */

/* Screen order for left/right navigation (event excluded) */
static const ui_screen_id_t nav_chain[] = {
    UI_SCREEN_SETTINGS,
    UI_SCREEN_HOME,
    UI_SCREEN_TREND,
    UI_SCREEN_BREATH,
};
#define NAV_CHAIN_LEN  (sizeof(nav_chain) / sizeof(nav_chain[0]))

/* ── Helpers ──────────────────────────────────────────────────────── */

static int nav_index(ui_screen_id_t id)
{
    for (int i = 0; i < (int)NAV_CHAIN_LEN; i++) {
        if (nav_chain[i] == id) return i;
    }
    return -1;
}

/* Find the current screen's index in nav_chain and return the neighbour */
static ui_screen_id_t nav_neighbour(ui_screen_id_t from, int delta)
{
    int idx = nav_index(from);
    if (idx < 0) return from;
    int next = idx + delta;
    if (next < 0 || next >= (int)NAV_CHAIN_LEN) return from; /* clamp */
    return nav_chain[next];
}

/* Slide animation helper */
static void slide_screen(lv_obj_t *new_scr, ui_trans_dir_t dir)
{
    lv_scr_load_anim_t anim;
    switch (dir) {
    case UI_TRANS_LEFT:   anim = LV_SCR_LOAD_ANIM_MOVE_LEFT;  break;
    case UI_TRANS_RIGHT:  anim = LV_SCR_LOAD_ANIM_MOVE_RIGHT; break;
    case UI_TRANS_TOP:    anim = LV_SCR_LOAD_ANIM_MOVE_TOP;   break;
    case UI_TRANS_BOTTOM: anim = LV_SCR_LOAD_ANIM_MOVE_BOTTOM;break;
    default:              anim = LV_SCR_LOAD_ANIM_NONE;        break;
    }
    lv_scr_load_anim(new_scr, anim, UI_ANIM_NORMAL, 0, false);
}

/* ── Swipe gesture handler ────────────────────────────────────────── */

static void gesture_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        touch_start_x = p.x;
        touch_start_y = p.y;
        touch_active  = true;
        return;
    }

    if (!touch_active) return;

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESSING) {
        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t p;
        lv_indev_get_point(indev, &p);

        lv_coord_t dx = p.x - touch_start_x;
        lv_coord_t dy = p.y - touch_start_y;

        if (code == LV_EVENT_PRESSING) {
            /* Still dragging – ignore until release */
            if (abs(dx) < SWIPE_THRESHOLD && abs(dy) < SWIPE_THRESHOLD) return;
        }

        touch_active = false;

        if (abs(dx) > abs(dy) && abs(dx) >= SWIPE_THRESHOLD) {
            /* Horizontal swipe */
            if (dx < 0) {
                /* Swipe left → next screen */
                ui_screen_id_t next = nav_neighbour(cur_screen, 1);
                ui_manager_goto(next, UI_TRANS_LEFT);
            } else {
                /* Swipe right → previous screen */
                ui_screen_id_t prev = nav_neighbour(cur_screen, -1);
                ui_manager_goto(prev, UI_TRANS_RIGHT);
            }
        } else if (abs(dy) >= SWIPE_THRESHOLD) {
            /* Vertical swipe */
            if (dy < 0) {
                /* Swipe up → event list / notifications (show event modal for now) */
                ui_manager_show_event();
            }
            /* Swipe down → quick settings (not implemented – placeholder) */
        }
    }
}

/* ── Screen table init ────────────────────────────────────────────── */

static void create_all_screens(void)
{
    scr_table[UI_SCREEN_HOME]     = screen_home_create();
    scr_table[UI_SCREEN_TREND]    = screen_trend_create();
    scr_table[UI_SCREEN_BREATH]   = screen_breath_create();
    scr_table[UI_SCREEN_SETTINGS] = screen_settings_create();
    scr_table[UI_SCREEN_EVENT]    = screen_event_create();
}

static void attach_gesture_to_all(void)
{
    for (int i = 0; i < UI_SCREEN_COUNT; i++) {
        if (scr_table[i]) {
            lv_obj_add_event_cb(scr_table[i], gesture_cb, LV_EVENT_PRESSED,   NULL);
            lv_obj_add_event_cb(scr_table[i], gesture_cb, LV_EVENT_RELEASED,  NULL);
            lv_obj_add_event_cb(scr_table[i], gesture_cb, LV_EVENT_PRESSING,  NULL);
        }
    }
}

/* ── Public API ───────────────────────────────────────────────────── */

void ui_manager_init(void)
{
    create_all_screens();
    attach_gesture_to_all();

    /* Load home screen as the default */
    cur_screen  = UI_SCREEN_HOME;
    prev_screen = UI_SCREEN_HOME;
    lv_scr_load(scr_table[UI_SCREEN_HOME]);
}

void ui_manager_goto(ui_screen_id_t id, ui_trans_dir_t dir)
{
    if (id >= UI_SCREEN_COUNT || id == UI_SCREEN_EVENT) return;
    if (id == cur_screen) return;

    prev_screen = cur_screen;
    cur_screen  = id;

    /* Tick the old screen's on_hide */
    switch (prev_screen) {
    case UI_SCREEN_HOME:     screen_home_on_hide();     break;
    case UI_SCREEN_TREND:    screen_trend_on_hide();    break;
    case UI_SCREEN_BREATH:   screen_breath_on_hide();   break;
    case UI_SCREEN_SETTINGS: screen_settings_on_hide(); break;
    default: break;
    }

    /* Tick the new screen's on_show */
    switch (cur_screen) {
    case UI_SCREEN_HOME:     screen_home_on_show();     break;
    case UI_SCREEN_TREND:    screen_trend_on_show();    break;
    case UI_SCREEN_BREATH:   screen_breath_on_show();   break;
    case UI_SCREEN_SETTINGS: screen_settings_on_show(); break;
    default: break;
    }

    slide_screen(scr_table[id], dir);
}

void ui_manager_show_event(void)
{
    if (event_visible) return;
    event_visible = true;
    prev_screen = cur_screen;
    screen_event_reset_timer();
    slide_screen(scr_table[UI_SCREEN_EVENT], UI_TRANS_BOTTOM);
}

void ui_manager_dismiss_event(void)
{
    if (!event_visible) return;
    event_visible = false;
    cur_screen = prev_screen;
    slide_screen(scr_table[prev_screen], UI_TRANS_TOP);
}

ui_screen_id_t ui_manager_get_current(void)
{
    return cur_screen;
}

void ui_manager_tick(void)
{
    /* Drive per-screen periodic updates */
    if (cur_screen == UI_SCREEN_HOME) {
        screen_home_tick();
    } else if (cur_screen == UI_SCREEN_BREATH) {
        screen_breath_tick();
    }

    /* Drive event timeout */
    if (event_visible) {
        screen_event_tick();
        if (screen_event_is_timed_out()) {
            ui_manager_dismiss_event();
        }
    }
}
