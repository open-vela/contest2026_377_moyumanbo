/**
 * @file screen_settings.c
 * @brief Settings screen implementation
 *
 * Layout (390 x 450):
 *   [  "Settings"                   ]   y=14
 *   [  ┌──────────────────────────┐ ]
 *   [  │ BLE On/Off        [██]  │ ]
 *   [  │ Event Alerts       [██] │ ]
 *   [  │ Breath Remind      [██] │ ]
 *   [  └──────────────────────────┘ ]   y=170
 *   [  Privacy                      ]
 *   [  [ Clear All Data ]           ]   y=200
 *   [                               ]
 *   [  About                        ]
 *   [  VelaSense v1.0.0             ]
 *   [  V377 - moyumanbo             ]   y=320
 *   [                               ]
 *   [  [ Factory Reset ]           ]   y=390
 */

#include "screen_settings.h"
#include "ui_theme.h"

/* ── Private state ────────────────────────────────────────────────── */

static lv_obj_t *root;
static lv_obj_t *toggles[SETTING_COUNT];

static bool settings[SETTING_COUNT] = {
    true,   /* BLE enabled */
    true,   /* Event alerts */
    false,  /* Breath reminders */
};

static settings_action_cb_t action_cb   = NULL;
static void                *action_data = NULL;

/* Confirmation dialog state */
static lv_obj_t *confirm_box = NULL;
static int       confirm_action = -1;

/* ── Confirmation dialog ──────────────────────────────────────────── */

static void confirm_close(void)
{
    if (confirm_box) {
        lv_obj_del(confirm_box);
        confirm_box = NULL;
        confirm_action = -1;
    }
}

static void confirm_yes_cb(lv_event_t *e)
{
    (void)e;
    int act = confirm_action;
    confirm_close();
    if (action_cb) {
        action_cb(act, action_data);
    }
}

static void confirm_no_cb(lv_event_t *e)
{
    (void)e;
    confirm_close();
}

static void show_confirm(const char *title, const char *msg, int action)
{
    confirm_close();
    confirm_action = action;

    confirm_box = lv_obj_create(root);
    lv_obj_set_size(confirm_box, UI_DISP_HOR - 48, 180);
    lv_obj_center(confirm_box);
    lv_obj_set_style_bg_color(confirm_box, UI_COLOR_SURFACE2, 0);
    lv_obj_set_style_bg_opa(confirm_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(confirm_box, UI_RADIUS, 0);
    lv_obj_set_style_border_color(confirm_box, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_width(confirm_box, 1, 0);
    lv_obj_clear_flag(confirm_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_t = lv_label_create(confirm_box);
    lv_label_set_text(lbl_t, title);
    lv_obj_set_style_text_color(lbl_t, UI_COLOR_AMBER, 0);
    lv_obj_set_style_text_font(lbl_t, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl_t, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *lbl_m = lv_label_create(confirm_box);
    lv_label_set_text(lbl_m, msg);
    lv_obj_set_style_text_color(lbl_m, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_m, &lv_font_montserrat_14, 0);
    lv_obj_set_width(lbl_m, UI_DISP_HOR - 80);
    lv_label_set_long_mode(lbl_m, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(lbl_m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_m, LV_ALIGN_TOP_MID, 0, 42);

    /* Yes button */
    lv_obj_t *btn_y = lv_btn_create(confirm_box);
    lv_obj_set_size(btn_y, 100, 36);
    lv_obj_align(btn_y, LV_ALIGN_BOTTOM_LEFT, 20, -14);
    lv_obj_set_style_bg_color(btn_y, UI_COLOR_ERROR, 0);
    lv_obj_set_style_radius(btn_y, UI_RADIUS_SMALL, 0);
    lv_obj_add_event_cb(btn_y, confirm_yes_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_y = lv_label_create(btn_y);
    lv_label_set_text(lbl_y, "Confirm");
    lv_obj_set_style_text_color(lbl_y, UI_COLOR_TEXT, 0);
    lv_obj_center(lbl_y);

    /* No button */
    lv_obj_t *btn_n = lv_btn_create(confirm_box);
    lv_obj_set_size(btn_n, 100, 36);
    lv_obj_align(btn_n, LV_ALIGN_BOTTOM_RIGHT, -20, -14);
    lv_obj_set_style_bg_color(btn_n, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_radius(btn_n, UI_RADIUS_SMALL, 0);
    lv_obj_add_event_cb(btn_n, confirm_no_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_n = lv_label_create(btn_n);
    lv_label_set_text(lbl_n, "Cancel");
    lv_obj_set_style_text_color(lbl_n, UI_COLOR_TEXT, 0);
    lv_obj_center(lbl_n);
}

/* ── Toggle handler ───────────────────────────────────────────────── */

static void toggle_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= SETTING_COUNT) return;
    settings[idx] = lv_obj_has_state(sw, LV_STATE_CHECKED);
}

/* ── Action button handlers ───────────────────────────────────────── */

static void clear_data_cb(lv_event_t *e)
{
    (void)e;
    show_confirm("Clear All Data",
                 "This will delete all health records and event history. "
                 "This action cannot be undone.",
                 0);
}

static void factory_reset_cb(lv_event_t *e)
{
    (void)e;
    show_confirm("Factory Reset",
                 "This will erase ALL data and restore factory defaults. "
                 "The device will reboot.",
                 1);
}

/* ── Build a settings row ─────────────────────────────────────────── */

static lv_obj_t *create_toggle_row(lv_obj_t *parent, const char *text,
                                    setting_id_t id, lv_align_t align,
                                    lv_coord_t y_off)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, UI_DISP_HOR - 32, 44);
    lv_obj_align(row, align, 0, y_off);
    lv_obj_set_style_bg_color(row, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, UI_PAD, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 48, 26);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(sw, UI_COLOR_SURFACE2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, UI_COLOR_TEAL, LV_PART_INDICATOR);
    if (settings[id]) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, toggle_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)id);

    toggles[id] = sw;
    return row;
}

static lv_obj_t *create_button_row(lv_obj_t *parent, const char *text,
                                    lv_color_t color, lv_event_cb_t cb,
                                    lv_align_t align, lv_coord_t y_off)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, UI_DISP_HOR - 64, 42);
    lv_obj_align(btn, align, 0, y_off);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_30, 0);
    lv_obj_set_style_radius(btn, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl);

    return btn;
}

/* ── Public lifecycle ─────────────────────────────────────────────── */

lv_obj_t *screen_settings_create(void)
{
    root = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(root, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Header ───────────────────────────────────────────────────── */
    lv_obj_t *hdr = lv_label_create(root);
    lv_label_set_text(hdr, LV_SYMBOL_SETTINGS "  Settings");
    lv_obj_set_style_text_color(hdr, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_20, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, UI_PAD, 14);

    /* ── Toggle section ───────────────────────────────────────────── */
    lv_obj_t *sec1 = lv_label_create(root);
    lv_label_set_text(sec1, "Connectivity");
    lv_obj_set_style_text_color(sec1, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(sec1, &lv_font_montserrat_12, 0);
    lv_obj_align(sec1, LV_ALIGN_TOP_LEFT, UI_PAD, 52);

    create_toggle_row(root, "Bluetooth (BLE)", SETTING_BLE_ENABLED,
                      LV_ALIGN_TOP_MID, 70);
    create_toggle_row(root, "Event Alerts", SETTING_EVENT_ALERTS,
                      LV_ALIGN_TOP_MID, 120);
    create_toggle_row(root, "Breathing Reminders", SETTING_BREATH_REMIND,
                      LV_ALIGN_TOP_MID, 170);

    /* ── Privacy section ──────────────────────────────────────────── */
    lv_obj_t *sec2 = lv_label_create(root);
    lv_label_set_text(sec2, "Privacy");
    lv_obj_set_style_text_color(sec2, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(sec2, &lv_font_montserrat_12, 0);
    lv_obj_align(sec2, LV_ALIGN_TOP_LEFT, UI_PAD, 228);

    create_button_row(root, LV_SYMBOL_TRASH "  Clear All Data",
                      UI_COLOR_AMBER, clear_data_cb,
                      LV_ALIGN_TOP_MID, 248);

    /* ── About section ────────────────────────────────────────────── */
    lv_obj_t *sec3 = lv_label_create(root);
    lv_label_set_text(sec3, "About");
    lv_obj_set_style_text_color(sec3, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(sec3, &lv_font_montserrat_12, 0);
    lv_obj_align(sec3, LV_ALIGN_TOP_LEFT, UI_PAD, 308);

    lv_obj_t *about_box = lv_obj_create(root);
    lv_obj_set_size(about_box, UI_DISP_HOR - 32, 70);
    lv_obj_align(about_box, LV_ALIGN_TOP_MID, 0, 326);
    lv_obj_set_style_bg_color(about_box, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(about_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(about_box, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_border_width(about_box, 0, 0);
    lv_obj_clear_flag(about_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_v = lv_label_create(about_box);
    lv_label_set_text(lbl_v, "VelaSense  v1.0.0");
    lv_obj_set_style_text_color(lbl_v, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_v, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_v, LV_ALIGN_TOP_LEFT, 12, 8);

    lv_obj_t *lbl_d = lv_label_create(about_box);
    lv_label_set_text(lbl_d, "V377 - moyumanbo");
    lv_obj_set_style_text_color(lbl_d, UI_COLOR_TEAL, 0);
    lv_obj_set_style_text_font(lbl_d, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_d, LV_ALIGN_TOP_LEFT, 12, 32);

    lv_obj_t *lbl_team = lv_label_create(about_box);
    lv_label_set_text(lbl_team, "Team 377");
    lv_obj_set_style_text_color(lbl_team, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_team, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_team, LV_ALIGN_TOP_RIGHT, -12, 12);

    /* ── Factory reset (danger zone) ──────────────────────────────── */
    create_button_row(root, LV_SYMBOL_WARNING "  Factory Reset",
                      UI_COLOR_ERROR, factory_reset_cb,
                      LV_ALIGN_TOP_MID, 408);

    return root;
}

void screen_settings_on_show(void)
{
    /* Sync toggle states with current settings */
    for (int i = 0; i < SETTING_COUNT; i++) {
        if (toggles[i]) {
            if (settings[i]) {
                lv_obj_add_state(toggles[i], LV_STATE_CHECKED);
            } else {
                lv_obj_clear_state(toggles[i], LV_STATE_CHECKED);
            }
        }
    }
}

void screen_settings_on_hide(void)
{
    confirm_close();
}

/* ── Data ─────────────────────────────────────────────────────────── */

bool screen_settings_get(setting_id_t id)
{
    if (id < 0 || id >= SETTING_COUNT) return false;
    return settings[id];
}

void screen_settings_set(setting_id_t id, bool value)
{
    if (id < 0 || id >= SETTING_COUNT) return;
    settings[id] = value;
}

void screen_settings_set_action_cb(settings_action_cb_t cb, void *data)
{
    action_cb   = cb;
    action_data = data;
}
