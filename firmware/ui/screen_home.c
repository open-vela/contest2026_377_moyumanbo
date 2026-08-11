/**
 * @file screen_home.c
 * @brief Home screen implementation
 *
 * Layout (390 x 450):
 *   [  Battery %           SQI dot  ]   y=12
 *   [        ♥ heart icon           ]   y=60   (120x120)
 *   [         72 bpm                ]   y=190  (huge font)
 *   [      ▂▃▅▇▅▃▂ mini chart      ]   y=260  (60x40)
 *   [  🚶 Walking  |  14:32        ]   y=330
 */

#include "screen_home.h"
#include "ui_theme.h"

/* ── Private state ────────────────────────────────────────────────── */

static lv_obj_t *root;
static lv_obj_t *heart_icon;
static lv_obj_t *lbl_hr;
static lv_obj_t *lbl_activity;
static lv_obj_t *act_icons[HOME_ACT_COUNT];
static lv_obj_t *sqi_dot;
static lv_obj_t *lbl_battery;
static lv_obj_t *chart_hr;
static lv_chart_series_t *ser_hr;

static int  cur_hr        = 72;
static int  cur_battery   = 85;
static int  cur_sqi       = 90;
static home_activity_t cur_act = HOME_ACT_SIT;

/* Mini-chart buffer (last ~5 min at ~1 pt / 15 s = 20 points) */
#define HR_CHART_PTS  20
static int hr_history[HR_CHART_PTS];

/* Activity label strings */
static const char *act_labels[] = { "Sitting", "Standing", "Walking", "Running" };

/* ── Build helpers ────────────────────────────────────────────────── */

static lv_obj_t *create_status_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, UI_DISP_HOR, 40);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, UI_PAD, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Battery label (left) */
    lbl_battery = lv_label_create(bar);
    lv_label_set_text(lbl_battery, LV_SYMBOL_BATTERY_FULL " 85%");
    lv_obj_set_style_text_color(lbl_battery, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_battery, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_battery, LV_ALIGN_LEFT_MID, 0, 0);

    /* SQI dot (right) */
    sqi_dot = lv_obj_create(bar);
    lv_obj_set_size(sqi_dot, 12, 12);
    lv_obj_set_style_bg_color(sqi_dot, UI_COLOR_SQI_GOOD, 0);
    lv_obj_set_style_bg_opa(sqi_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sqi_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(sqi_dot, 0, 0);
    lv_obj_align(sqi_dot, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *lbl_sqi = lv_label_create(bar);
    lv_label_set_text(lbl_sqi, "SQI");
    lv_obj_set_style_text_color(lbl_sqi, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_sqi, &lv_font_montserrat_14, 0);
    lv_obj_align_to(lbl_sqi, sqi_dot, LV_ALIGN_OUT_LEFT_MID, -6, 0);

    return bar;
}

static void create_hr_section(lv_obj_t *parent)
{
    /* Heart icon */
    heart_icon = ui_theme_create_heart_icon(parent, 100);
    lv_obj_align(heart_icon, LV_ALIGN_TOP_MID, 0, 70);

    /* HR label */
    lbl_hr = lv_label_create(parent);
    lv_label_set_text_fmt(lbl_hr, "%d", cur_hr);
    lv_obj_set_style_text_color(lbl_hr, UI_COLOR_CORAL, 0);
    lv_obj_set_style_text_font(lbl_hr, &lv_font_montserrat_48, 0);
    lv_obj_align(lbl_hr, LV_ALIGN_TOP_MID, 0, 185);

    lv_obj_t *lbl_bpm = lv_label_create(parent);
    lv_label_set_text(lbl_bpm, "bpm");
    lv_obj_set_style_text_color(lbl_bpm, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_bpm, &lv_font_montserrat_14, 0);
    lv_obj_align_to(lbl_bpm, lbl_hr, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
}

static void create_mini_chart(lv_obj_t *parent)
{
    chart_hr = lv_chart_create(parent);
    lv_obj_set_size(chart_hr, 200, 60);
    lv_obj_align(chart_hr, LV_ALIGN_TOP_MID, 0, 250);
    lv_chart_set_type(chart_hr, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_hr, HR_CHART_PTS);
    lv_chart_set_range(chart_hr, LV_CHART_AXIS_PRIMARY_Y, 40, 180);
    lv_chart_set_div_line_count(chart_hr, 0, 0);
    lv_chart_set_update_mode(chart_hr, LV_CHART_UPDATE_MODE_SHIFT);

    /* Transparent background */
    lv_obj_set_style_bg_opa(chart_hr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart_hr, 0, 0);
    lv_obj_set_style_pad_all(chart_hr, 2, 0);

    ser_hr = lv_chart_add_series(chart_hr, UI_COLOR_TEAL, LV_CHART_AXIS_PRIMARY_Y);

    /* Fill with initial data */
    for (int i = 0; i < HR_CHART_PTS; i++) {
        lv_chart_set_next_value(chart_hr, ser_hr, (lv_coord_t)hr_history[i]);
    }
    lv_chart_refresh(chart_hr);
}

static void create_activity_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, UI_DISP_HOR - 32, 44);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 340);
    lv_obj_set_style_bg_color(row, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, UI_PAD, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* Activity icon placeholder (we just use text symbols) */
    static const char *icons[] = {
        LV_SYMBOL_HOME,
        LV_SYMBOL_EYE_OPEN,
        LV_SYMBOL_LOOP,
        LV_SYMBOL_REFRESH,
    };
    for (int i = 0; i < HOME_ACT_COUNT; i++) {
        act_icons[i] = lv_label_create(row);
        lv_label_set_text(act_icons[i], icons[i]);
        lv_obj_set_style_text_color(act_icons[i],
                                     i == cur_act ? UI_COLOR_TEAL : UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(act_icons[i], &lv_font_montserrat_20, 0);
        lv_obj_align(act_icons[i], LV_ALIGN_LEFT_MID, i * 36, 0);
    }

    lbl_activity = lv_label_create(row);
    lv_label_set_text(lbl_activity, act_labels[cur_act]);
    lv_obj_set_style_text_color(lbl_activity, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_activity, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_activity, LV_ALIGN_RIGHT_MID, 0, 0);
}

/* ── Public lifecycle ─────────────────────────────────────────────── */

lv_obj_t *screen_home_create(void)
{
    root = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(root, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    create_status_bar(root);
    create_hr_section(root);
    create_mini_chart(root);
    create_activity_row(root);

    /* Hint label at bottom */
    lv_obj_t *hint = lv_label_create(root);
    lv_label_set_text(hint, "< Settings          Trend >");
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -12);

    /* Pre-fill HR history with resting values */
    for (int i = 0; i < HR_CHART_PTS; i++) {
        hr_history[i] = 70;
    }

    return root;
}

void screen_home_on_show(void)
{
    ui_theme_pulse_heart(heart_icon);
}

void screen_home_on_hide(void)
{
    /* Stop heart animation */
    lv_anim_del(heart_icon, (lv_anim_exec_xcb_t)lv_obj_set_style_transform_zoom);
}

void screen_home_tick(void)
{
    /* Update HR label */
    lv_label_set_text_fmt(lbl_hr, "%d", cur_hr);

    /* Update chart */
    lv_chart_set_next_value(chart_hr, ser_hr, (lv_coord_t)cur_hr);
    lv_chart_refresh(chart_hr);

    /* Update battery */
    const char *bat_sym = LV_SYMBOL_BATTERY_FULL;
    if (cur_battery < 20)      bat_sym = LV_SYMBOL_BATTERY_EMPTY;
    else if (cur_battery < 50) bat_sym = LV_SYMBOL_BATTERY_2;
    else if (cur_battery < 80) bat_sym = LV_SYMBOL_BATTERY_3;
    lv_label_set_text_fmt(lbl_battery, "%s %d%%", bat_sym, cur_battery);

    /* Update SQI dot colour */
    lv_color_t sqi_col;
    if (cur_sqi >= 70)      sqi_col = UI_COLOR_SQI_GOOD;
    else if (cur_sqi >= 40) sqi_col = UI_COLOR_SQI_FAIR;
    else                    sqi_col = UI_COLOR_SQI_POOR;
    lv_obj_set_style_bg_color(sqi_dot, sqi_col, 0);

    /* Activity highlight */
    for (int i = 0; i < HOME_ACT_COUNT; i++) {
        lv_obj_set_style_text_color(act_icons[i],
                                     i == cur_act ? UI_COLOR_TEAL : UI_COLOR_TEXT_DIM, 0);
    }
    lv_label_set_text(lbl_activity, act_labels[cur_act]);
}

/* ── Data setters ─────────────────────────────────────────────────── */

void screen_home_set_hr(int bpm)
{
    if (bpm < 20)  bpm = 20;
    if (bpm > 250) bpm = 250;
    cur_hr = bpm;
}

void screen_home_set_activity(home_activity_t act)
{
    cur_act = act;
}

void screen_home_set_sqi(int pct)
{
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    cur_sqi = pct;
}

void screen_home_set_battery(int pct)
{
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    cur_battery = pct;
}

void screen_home_set_hr_history(const int *bpm_array, int count)
{
    if (!bpm_array || count <= 0) return;
    int n = count < HR_CHART_PTS ? count : HR_CHART_PTS;
    /* Copy the last n values */
    for (int i = 0; i < n; i++) {
        hr_history[HR_CHART_PTS - n + i] = bpm_array[count - n + i];
    }
}
