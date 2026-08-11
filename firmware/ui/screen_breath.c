/**
 * @file screen_breath.c
 * @brief Breathing guide screen implementation
 *
 * Layout (390 x 450):
 *   [    "Breathing Guide"       ]   y=20
 *   [                            ]
 *   [       ◯ expanding circle   ]   center
 *   [       "Inhale" / "Hold"    ]
 *   [                            ]
 *   [   Session: 2:34 / 5:00    ]   y=360
 *   [       [ Start ] button     ]   y=400
 */

#include "screen_breath.h"
#include "ui_theme.h"

#include <stdio.h>

/* ── Private state ────────────────────────────────────────────────── */

static lv_obj_t *root;
static lv_obj_t *circle;           /* animated circle */
static lv_obj_t *lbl_phase;        /* phase text */
static lv_obj_t *lbl_timer;        /* session timer */
static lv_obj_t *btn_start;        /* start/stop toggle */
static lv_obj_t *lbl_btn;

static bool           running = false;
static breath_phase_t phase   = BREATH_IDLE;
static int32_t        phase_ms_left = 0;    /* ms remaining in current phase */
static int32_t        session_ms     = 0;   /* elapsed session time */
static int32_t        session_limit  = UI_BREATH_SESSION_S * 1000;

/* Circle animation */
#define CIRCLE_MIN_R   60
#define CIRCLE_MAX_R   120
static lv_coord_t cur_radius = CIRCLE_MIN_R;

/* Haptic callback */
static breath_haptic_cb_t haptic_cb   = NULL;
static void              *haptic_data = NULL;

/* Phase durations in ms */
static const int phase_durations[] = {
    0,                                    /* IDLE    */
    UI_BREATH_INHALE * 1000,              /* INHALE  */
    UI_BREATH_HOLD   * 1000,              /* HOLD    */
    UI_BREATH_EXHALE * 1000,              /* EXHALE  */
};

/* Phase labels (Chinese + English) */
static const char *phase_texts[] = {
    "",
    "Inhale  \xE5\x90\xB8\xE6\xB0\x94",     /* 吸气 */
    "Hold    \xE5\xB1\x8F\xE6\x81\xAF",     /* 屏息 */
    "Exhale  \xE5\x91\xBC\xE6\xB0\x94",     /* 呼气 */
};

/* Phase colours */
static const lv_color_t phase_colors[] = {
    UI_COLOR_TEXT_DIM,
    UI_COLOR_TEAL,       /* inhale  – calm */
    UI_COLOR_AMBER,      /* hold    – amber */
    UI_COLOR_CORAL,      /* exhale  – warm */
};

/* ── Helpers ──────────────────────────────────────────────────────── */

static breath_phase_t next_phase(breath_phase_t p)
{
    switch (p) {
    case BREATH_INHALE: return BREATH_HOLD;
    case BREATH_HOLD:   return BREATH_EXHALE;
    case BREATH_EXHALE: return BREATH_INHALE;
    default:            return BREATH_INHALE;
    }
}

static void set_phase(breath_phase_t new_phase)
{
    phase = new_phase;
    phase_ms_left = phase_durations[phase];

    /* Update label */
    lv_label_set_text(lbl_phase, phase_texts[phase]);
    lv_obj_set_style_text_color(lbl_phase, phase_colors[phase], 0);

    /* Haptic */
    if (haptic_cb && phase != BREATH_IDLE) {
        haptic_cb(phase, phase_durations[phase], haptic_data);
    }
}

static void update_circle(void)
{
    if (phase == BREATH_IDLE) return;

    int32_t total = phase_durations[phase];
    if (total <= 0) return;

    int32_t elapsed = total - phase_ms_left;
    int32_t progress_1000 = (elapsed * 1000) / total;  /* 0..1000 */

    lv_coord_t r;
    switch (phase) {
    case BREATH_INHALE:
        /* Expand: min → max */
        r = CIRCLE_MIN_R + (lv_coord_t)(((int32_t)(CIRCLE_MAX_R - CIRCLE_MIN_R)
                                          * progress_1000) / 1000);
        break;
    case BREATH_HOLD:
        /* Stay at max */
        r = CIRCLE_MAX_R;
        break;
    case BREATH_EXHALE:
        /* Contract: max → min */
        r = CIRCLE_MAX_R - (lv_coord_t)(((int32_t)(CIRCLE_MAX_R - CIRCLE_MIN_R)
                                          * progress_1000) / 1000);
        break;
    default:
        r = CIRCLE_MIN_R;
        break;
    }

    cur_radius = r;
    lv_obj_set_size(circle, r * 2, r * 2);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, -30);

    /* Colour transition */
    lv_obj_set_style_bg_color(circle, phase_colors[phase], 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_20 + (LV_OPA_60 * progress_1000 / 1000), 0);
    lv_obj_set_style_border_color(circle, phase_colors[phase], 0);
    lv_obj_set_style_border_width(circle, 2, 0);
}

/* ── Button handler ───────────────────────────────────────────────── */

static void btn_click_cb(lv_event_t *e)
{
    (void)e;
    if (running) {
        screen_breath_stop();
    } else {
        screen_breath_start();
    }
}

/* ── Build UI ─────────────────────────────────────────────────────── */

lv_obj_t *screen_breath_create(void)
{
    root = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(root, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Title ────────────────────────────────────────────────────── */
    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "Breathing Guide");
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    /* ── Animated circle ──────────────────────────────────────────── */
    circle = lv_obj_create(root);
    lv_obj_set_size(circle, CIRCLE_MIN_R * 2, CIRCLE_MIN_R * 2);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(circle, UI_COLOR_TEAL, 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(circle, UI_COLOR_TEAL, 0);
    lv_obj_set_style_border_width(circle, 2, 0);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, -30);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);

    /* Phase text inside the circle area (below it) */
    lbl_phase = lv_label_create(root);
    lv_label_set_text(lbl_phase, "Press Start");
    lv_obj_set_style_text_color(lbl_phase, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_phase, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl_phase, LV_ALIGN_CENTER, 0, 60);

    /* ── Session timer ────────────────────────────────────────────── */
    lbl_timer = lv_label_create(root);
    lv_label_set_text(lbl_timer, "0:00 / 5:00");
    lv_obj_set_style_text_color(lbl_timer, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_timer, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_timer, LV_ALIGN_TOP_MID, 0, 360);

    /* ── Start / Stop button ──────────────────────────────────────── */
    btn_start = lv_btn_create(root);
    lv_obj_set_size(btn_start, 140, 44);
    lv_obj_align(btn_start, LV_ALIGN_TOP_MID, 0, 395);
    lv_obj_set_style_bg_color(btn_start, UI_COLOR_TEAL, 0);
    lv_obj_set_style_radius(btn_start, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_border_width(btn_start, 0, 0);
    lv_obj_add_event_cb(btn_start, btn_click_cb, LV_EVENT_CLICKED, NULL);

    lbl_btn = lv_label_create(btn_start);
    lv_label_set_text(lbl_btn, LV_SYMBOL_PLAY "  Start");
    lv_obj_set_style_text_color(lbl_btn, UI_COLOR_BG, 0);
    lv_obj_set_style_text_font(lbl_btn, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_btn);

    return root;
}

void screen_breath_on_show(void)
{
    /* Update timer display */
    int elapsed_s = session_ms / 1000;
    int limit_s   = session_limit / 1000;
    lv_label_set_text_fmt(lbl_timer, "%d:%02d / %d:%02d",
                           elapsed_s / 60, elapsed_s % 60,
                           limit_s / 60, limit_s % 60);
}

void screen_breath_on_hide(void)
{
    /* Keep running in background if desired; for now, stop */
    /* screen_breath_stop(); */
}

void screen_breath_tick(void)
{
    if (!running) return;

    /* Advance session timer */
    session_ms += 100;
    if (session_ms >= session_limit) {
        screen_breath_stop();
        return;
    }

    /* Advance phase */
    phase_ms_left -= 100;
    if (phase_ms_left <= 0) {
        set_phase(next_phase(phase));
    }

    /* Update visuals */
    update_circle();

    /* Session timer label */
    int elapsed_s = session_ms / 1000;
    int limit_s   = session_limit / 1000;
    lv_label_set_text_fmt(lbl_timer, "%d:%02d / %d:%02d",
                           elapsed_s / 60, elapsed_s % 60,
                           limit_s / 60, limit_s % 60);
}

/* ── Control ──────────────────────────────────────────────────────── */

void screen_breath_start(void)
{
    if (running) return;
    running    = true;
    session_ms = 0;
    set_phase(BREATH_INHALE);

    lv_label_set_text(lbl_btn, LV_SYMBOL_PAUSE "  Stop");
    lv_obj_set_style_bg_color(btn_start, UI_COLOR_CORAL, 0);
}

void screen_breath_stop(void)
{
    running = false;
    phase   = BREATH_IDLE;
    phase_ms_left = 0;

    /* Reset circle */
    lv_obj_set_size(circle, CIRCLE_MIN_R * 2, CIRCLE_MIN_R * 2);
    lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, 0);

    lv_label_set_text(lbl_phase, "Press Start");
    lv_obj_set_style_text_color(lbl_phase, UI_COLOR_TEXT_DIM, 0);

    lv_label_set_text(lbl_btn, LV_SYMBOL_PLAY "  Start");
    lv_obj_set_style_bg_color(btn_start, UI_COLOR_TEAL, 0);
}

bool screen_breath_is_running(void)
{
    return running;
}

void screen_breath_set_haptic_cb(breath_haptic_cb_t cb, void *data)
{
    haptic_cb   = cb;
    haptic_data = data;
}
