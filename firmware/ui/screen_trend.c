/**
 * @file screen_trend.c
 * @brief Trend screen implementation
 *
 * Layout (390 x 450):
 *   [  "24h Heart Rate"    back  ]   y=12
 *   [   ┌──────────────────┐    ]   y=55   (340 x 160)
 *   [   │ HR line chart    │    ]
 *   [   │ + event dots     │    ]
 *   [   └──────────────────┘    ]
 *   [  Summary row:  events / avg / stress ]  y=230
 *   [  Event list (scrollable)  ]   y=280
 */

#include "screen_trend.h"
#include "ui_theme.h"

#include <string.h>

/* ── Limits ───────────────────────────────────────────────────────── */
#define TREND_24H_PTS    1440    /* one per minute */
#define TREND_MAX_EVENTS 64

/* ── Private state ────────────────────────────────────────────────── */

static lv_obj_t *root;
static lv_obj_t *chart;
static lv_chart_series_t *ser_hr;

/* Event markers */
static trend_event_marker_t events[TREND_MAX_EVENTS];
static int                  event_count = 0;

/* Summary */
static int sum_events  = 0;
static int sum_avg_hr  = 72;
static int sum_stress  = 0;

/* Summary labels */
static lv_obj_t *lbl_ev_count;
static lv_obj_t *lbl_avg_hr;
static lv_obj_t *lbl_stress;

/* Event list container */
static lv_obj_t *event_list;

/* HR data pointer (owned by caller) */
static const int16_t *hr_data     = NULL;
static int            hr_data_cnt = 0;

/* ── Colours for each event label ─────────────────────────────────── */
static const lv_color_t label_colors[] = {
    UI_COLOR_CORAL,        /* EXCITEMENT */
    UI_COLOR_AMBER,        /* NERVOUS    */
    UI_COLOR_AMBER,        /* SURPRISE   */
    UI_COLOR_ERROR,        /* STRESS     */
    UI_COLOR_TEXT_DIM,     /* FALSE_POS  */
};

static const char *label_names[] = {
    "Excitement", "Nervous", "Surprise", "Stress", "False alarm"
};

/* ── Build helpers ────────────────────────────────────────────────── */

static void refresh_event_list(void)
{
    /* Clear existing children */
    lv_obj_clean(event_list);

    if (event_count == 0) {
        lv_obj_t *lbl = lv_label_create(event_list);
        lv_label_set_text(lbl, "No events today");
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        return;
    }

    for (int i = 0; i < event_count; i++) {
        int h = events[i].minute_of_day / 60;
        int m = events[i].minute_of_day % 60;

        lv_obj_t *row = lv_obj_create(event_list);
        lv_obj_set_size(row, LV_PCT(100), 36);
        lv_obj_set_style_bg_color(row, UI_COLOR_SURFACE, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, UI_RADIUS_SMALL, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_hor(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        /* Colour dot */
        lv_obj_t *dot = lv_obj_create(row);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_bg_color(dot, label_colors[events[i].label], 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0);

        /* Time + label text */
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text_fmt(lbl, "%02d:%02d  %s", h, m,
                               events[i].label < EVENT_LABEL_COUNT
                                   ? label_names[events[i].label]
                                   : "?");
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_align_to(lbl, dot, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    }
}

lv_obj_t *screen_trend_create(void)
{
    root = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(root, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Header ───────────────────────────────────────────────────── */
    lv_obj_t *hdr = lv_label_create(root);
    lv_label_set_text(hdr, LV_SYMBOL_LEFT "  24h Heart Rate");
    lv_obj_set_style_text_color(hdr, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_20, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, UI_PAD, 14);

    /* ── 24h chart ────────────────────────────────────────────────── */
    chart = lv_chart_create(root);
    lv_obj_set_size(chart, UI_DISP_HOR - 32, 150);
    lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 55);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 96);  /* one point per 15 min for performance */
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 40, 180);
    lv_chart_set_div_line_count(chart, 3, 4);
    lv_obj_set_style_bg_color(chart, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chart, UI_COLOR_DIVIDER, 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_pad_all(chart, 4, 0);

    lv_obj_set_style_line_color(chart, UI_COLOR_DIVIDER, LV_PART_MAIN);

    ser_hr = lv_chart_add_series(chart, UI_COLOR_TEAL, LV_CHART_AXIS_PRIMARY_Y);

    /* ── Summary row ──────────────────────────────────────────────── */
    lv_obj_t *sum_row = lv_obj_create(root);
    lv_obj_set_size(sum_row, UI_DISP_HOR - 32, 50);
    lv_obj_align(sum_row, LV_ALIGN_TOP_MID, 0, 218);
    lv_obj_set_style_bg_color(sum_row, UI_COLOR_SURFACE, 0);
    lv_obj_set_style_bg_opa(sum_row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sum_row, UI_RADIUS_SMALL, 0);
    lv_obj_set_style_border_width(sum_row, 0, 0);
    lv_obj_clear_flag(sum_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(sum_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sum_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Events count */
    lv_obj_t *col1 = lv_obj_create(sum_row);
    lv_obj_set_size(col1, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col1, 0, 0);
    lv_obj_set_style_pad_all(col1, 2, 0);
    lv_obj_clear_flag(col1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(col1, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl1a = lv_label_create(col1);
    lv_label_set_text(lbl1a, "Events");
    lv_obj_set_style_text_color(lbl1a, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl1a, &lv_font_montserrat_12, 0);

    lbl_ev_count = lv_label_create(col1);
    lv_label_set_text(lbl_ev_count, "0");
    lv_obj_set_style_text_color(lbl_ev_count, UI_COLOR_CORAL, 0);
    lv_obj_set_style_text_font(lbl_ev_count, &lv_font_montserrat_20, 0);

    /* Avg HR */
    lv_obj_t *col2 = lv_obj_create(sum_row);
    lv_obj_set_size(col2, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col2, 0, 0);
    lv_obj_set_style_pad_all(col2, 2, 0);
    lv_obj_clear_flag(col2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(col2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl2a = lv_label_create(col2);
    lv_label_set_text(lbl2a, "Avg HR");
    lv_obj_set_style_text_color(lbl2a, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl2a, &lv_font_montserrat_12, 0);

    lbl_avg_hr = lv_label_create(col2);
    lv_label_set_text_fmt(lbl_avg_hr, "%d", sum_avg_hr);
    lv_obj_set_style_text_color(lbl_avg_hr, UI_COLOR_TEAL, 0);
    lv_obj_set_style_text_font(lbl_avg_hr, &lv_font_montserrat_20, 0);

    /* Stress level */
    lv_obj_t *col3 = lv_obj_create(sum_row);
    lv_obj_set_size(col3, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col3, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col3, 0, 0);
    lv_obj_set_style_pad_all(col3, 2, 0);
    lv_obj_clear_flag(col3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(col3, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col3, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl3a = lv_label_create(col3);
    lv_label_set_text(lbl3a, "Stress");
    lv_obj_set_style_text_color(lbl3a, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl3a, &lv_font_montserrat_12, 0);

    lbl_stress = lv_label_create(col3);
    lv_label_set_text_fmt(lbl_stress, "%d%%", sum_stress);
    lv_obj_set_style_text_color(lbl_stress, UI_COLOR_AMBER, 0);
    lv_obj_set_style_text_font(lbl_stress, &lv_font_montserrat_20, 0);

    /* ── Event list ───────────────────────────────────────────────── */
    event_list = lv_obj_create(root);
    lv_obj_set_size(event_list, UI_DISP_HOR - 32, 155);
    lv_obj_align(event_list, LV_ALIGN_TOP_MID, 0, 280);
    lv_obj_set_style_bg_opa(event_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(event_list, 0, 0);
    lv_obj_set_style_pad_all(event_list, 0, 0);
    lv_obj_set_flex_flow(event_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(event_list, 4, 0);

    refresh_event_list();

    return root;
}

void screen_trend_on_show(void)
{
    /* Refresh chart from latest data */
    if (hr_data && hr_data_cnt > 0) {
        /* Down-sample to 96 points (every 15th minute) */
        int step = hr_data_cnt / 96;
        if (step < 1) step = 1;
        lv_chart_set_point_count(chart, 96);
        for (int i = 0; i < 96; i++) {
            int idx = i * step;
            if (idx >= hr_data_cnt) idx = hr_data_cnt - 1;
            lv_chart_set_next_value(chart, ser_hr, (lv_coord_t)hr_data[idx]);
        }
        lv_chart_refresh(chart);
    }

    /* Update summary labels */
    lv_label_set_text_fmt(lbl_ev_count, "%d", sum_events);
    lv_label_set_text_fmt(lbl_avg_hr,   "%d", sum_avg_hr);
    lv_label_set_text_fmt(lbl_stress,   "%d%%", sum_stress);

    refresh_event_list();
}

void screen_trend_on_hide(void)
{
    /* Nothing to stop */
}

/* ── Data setters ─────────────────────────────────────────────────── */

void screen_trend_set_hr_24h(const int16_t *hr_per_min, int count)
{
    hr_data     = hr_per_min;
    hr_data_cnt = count;
}

void screen_trend_add_event(const trend_event_marker_t *evt)
{
    if (!evt || event_count >= TREND_MAX_EVENTS) return;
    events[event_count++] = *evt;
}

void screen_trend_clear_events(void)
{
    event_count = 0;
}

void screen_trend_set_summary(int event_count_, int avg_hr, int stress_pct)
{
    sum_events = event_count_;
    sum_avg_hr = avg_hr;
    sum_stress = stress_pct;
}
