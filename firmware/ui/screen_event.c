/**
 * @file screen_event.c
 * @brief Event confirmation screen implementation
 *
 * Full-screen alert with:
 *   - Large arousal icon
 *   - 5 touch buttons in a column (emoji + Chinese label)
 *   - 60-second timer bar at top
 */

#include "screen_event.h"
#include "ui_manager.h"
#include "ui_theme.h"

#include <string.h>

/* ── Private state ────────────────────────────────────────────────── */

static lv_obj_t *root;
static lv_obj_t *timer_bar;
static lv_obj_t *btn_labels[EVENT_LABEL_COUNT];
static lv_obj_t *lbl_countdown;

static int32_t       timer_ms       = UI_EVENT_TIMEOUT_S * 1000;
static event_label_t selected_label = EVENT_LABEL_NONE;
static bool          timed_out      = false;

static event_label_cb_t user_cb   = NULL;
static void            *user_data = NULL;

/* Button definitions */
static const struct {
    const char *emoji;
    const char *text;
    lv_color_t  color;
} btn_defs[EVENT_LABEL_COUNT] = {
    { "\xF0\x9F\x92\x93", "Excitement",  UI_COLOR_CORAL  },
    { "\xF0\x9F\x98\xB0", "Nervous",     UI_COLOR_AMBER  },
    { "\xF0\x9F\x98\xAE", "Surprise",    UI_COLOR_AMBER  },
    { "\xF0\x9F\x98\xA4", "Stress",      UI_COLOR_ERROR  },
    { "\xE2\x9D\x8C",     "False alarm", UI_COLOR_TEXT_DIM },
};

/* ── Button event handler ─────────────────────────────────────────── */

static void btn_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= EVENT_LABEL_COUNT) return;

    selected_label = (event_label_t)idx;

    /* Visual feedback – highlight the chosen button */
    for (int i = 0; i < EVENT_LABEL_COUNT; i++) {
        lv_obj_set_style_bg_opa(btn_labels[i],
                                 i == idx ? LV_OPA_COVER : LV_OPA_30, LV_PART_MAIN);
    }

    /* Fire callback after a short delay to let the user see the highlight */
    if (user_cb) {
        user_cb(selected_label, user_data);
    }
}

/* ── Build UI ─────────────────────────────────────────────────────── */

lv_obj_t *screen_event_create(void)
{
    root = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x060818), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Timer bar (top) ──────────────────────────────────────────── */
    timer_bar = lv_bar_create(root);
    lv_obj_set_size(timer_bar, UI_DISP_HOR - 32, 6);
    lv_obj_align(timer_bar, LV_ALIGN_TOP_MID, 0, 20);
    lv_bar_set_range(timer_bar, 0, UI_EVENT_TIMEOUT_S * 1000);
    lv_bar_set_value(timer_bar, timer_ms, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(timer_bar, UI_COLOR_SURFACE2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(timer_bar, UI_COLOR_AMBER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(timer_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(timer_bar, 3, LV_PART_INDICATOR);

    /* Countdown text */
    lbl_countdown = lv_label_create(root);
    lv_label_set_text_fmt(lbl_countdown, "%ds", timer_ms / 1000);
    lv_obj_set_style_text_color(lbl_countdown, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_countdown, &lv_font_montserrat_14, 0);
    lv_obj_align_to(lbl_countdown, timer_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    /* ── Title icon (large arousal symbol) ────────────────────────── */
    lv_obj_t *lbl_icon = lv_label_create(root);
    lv_label_set_text(lbl_icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(lbl_icon, UI_COLOR_AMBER, 0);
    lv_obj_set_style_text_font(lbl_icon, &lv_font_montserrat_48, 0);
    lv_obj_align(lbl_icon, LV_ALIGN_TOP_MID, 0, 65);

    lv_obj_t *lbl_title = lv_label_create(root);
    lv_label_set_text(lbl_title, "Event Detected");
    lv_obj_set_style_text_color(lbl_title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 120);

    lv_obj_t *lbl_sub = lv_label_create(root);
    lv_label_set_text(lbl_sub, "How are you feeling?");
    lv_obj_set_style_text_color(lbl_sub, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_sub, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_sub, LV_ALIGN_TOP_MID, 0, 150);

    /* ── Choice buttons ───────────────────────────────────────────── */
    for (int i = 0; i < EVENT_LABEL_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(root);
        lv_obj_set_size(btn, UI_DISP_HOR - 64, 46);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 185 + i * 52);
        lv_obj_set_style_bg_color(btn, btn_defs[i].color, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_30, 0);
        lv_obj_set_style_radius(btn, UI_RADIUS_SMALL, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, btn_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        /* Button label: emoji + text */
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text_fmt(lbl, "%s  %s", btn_defs[i].emoji, btn_defs[i].text);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_center(lbl);

        btn_labels[i] = btn;
    }

    return root;
}

void screen_event_reset_timer(void)
{
    timer_ms    = UI_EVENT_TIMEOUT_S * 1000;
    timed_out   = false;
    selected_label = EVENT_LABEL_NONE;

    /* Reset button visuals */
    for (int i = 0; i < EVENT_LABEL_COUNT; i++) {
        lv_obj_set_style_bg_opa(btn_labels[i], LV_OPA_30, LV_PART_MAIN);
    }
}

void screen_event_tick(void)
{
    if (timed_out) return;

    timer_ms -= 100;  /* called every ~100 ms */
    if (timer_ms < 0) timer_ms = 0;

    lv_bar_set_value(timer_bar, timer_ms, LV_ANIM_OFF);
    lv_label_set_text_fmt(lbl_countdown, "%ds", (timer_ms + 500) / 1000);

    /* Pulse the bar colour as time runs out */
    if (timer_ms < 10000) {
        lv_obj_set_style_bg_color(timer_bar, UI_COLOR_ERROR, LV_PART_INDICATOR);
    } else if (timer_ms < 30000) {
        lv_obj_set_style_bg_color(timer_bar, UI_COLOR_AMBER, LV_PART_INDICATOR);
    }

    if (timer_ms <= 0) {
        timed_out = true;
        selected_label = EVENT_LABEL_NONE;
        if (user_cb) {
            user_cb(EVENT_LABEL_NONE, user_data);
        }
    }
}

bool screen_event_is_timed_out(void)
{
    return timed_out;
}

event_label_t screen_event_get_label(void)
{
    return selected_label;
}

void screen_event_set_callback(event_label_cb_t cb, void *data)
{
    user_cb   = cb;
    user_data = data;
}
