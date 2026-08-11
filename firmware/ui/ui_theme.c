/**
 * @file ui_theme.c
 * @brief VelaSense theme implementation – dark AMOLED style with heart motif
 */

#include "ui_theme.h"

/* ── Private helpers ──────────────────────────────────────────────── */

static void apply_default_style(lv_style_t *s)
{
    lv_style_set_bg_color(s, UI_COLOR_BG);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
    lv_style_set_text_color(s, UI_COLOR_TEXT);
    lv_style_set_border_width(s, 0);
    lv_style_set_radius(s, 0);
    lv_style_set_pad_all(s, 0);
}

/* ── Heart icon drawing via canvas callback ───────────────────────── */

static void heart_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    lv_coord_t cx = (coords.x1 + coords.x2) / 2;
    lv_coord_t cy = (coords.y1 + coords.y2) / 2;
    lv_coord_t r  = lv_obj_get_width(obj) / 4;

    /* Draw two overlapping circles + triangle to form a heart */
    lv_draw_rect_dsc_t disc_dsc;
    lv_draw_rect_dsc_init(&disc_dsc);
    disc_dsc.bg_color = UI_COLOR_CORAL;
    disc_dsc.bg_opa   = LV_OPA_COVER;
    disc_dsc.radius   = LV_RADIUS_CIRCLE;
    disc_dsc.border_width = 0;

    /* Left lobe */
    lv_area_t a1 = { cx - r - r / 3, cy - r / 2, cx - r / 3 + r, cy + r };
    lv_draw_rect(draw_ctx, &disc_dsc, &a1);

    /* Right lobe */
    lv_area_t a2 = { cx - r + r / 3, cy - r / 2, cx + r + r / 3, cy + r };
    lv_draw_rect(draw_ctx, &disc_dsc, &a2);

    /* Bottom triangle */
    lv_draw_triangle_dsc_t tri_dsc;
    lv_draw_triangle_dsc_init(&tri_dsc);
    tri_dsc.bg_color = UI_COLOR_CORAL;
    tri_dsc.bg_opa   = LV_OPA_COVER;

    lv_point_t pts[3] = {
        { cx - r - r / 3, cy },
        { cx + r + r / 3, cy },
        { cx,              cy + r + r / 2 },
    };
    tri_dsc.p[0] = pts[0];
    tri_dsc.p[1] = pts[1];
    tri_dsc.p[2] = pts[2];
    lv_draw_triangle(draw_ctx, &tri_dsc);
}

/* ── Public API ───────────────────────────────────────────────────── */

void ui_theme_init(void)
{
    /* Global default: dark navy background, light text */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Style the default scrollbar */
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
}

lv_obj_t *ui_theme_create_heart_icon(lv_obj_t *parent, lv_coord_t size)
{
    lv_obj_t *heart = lv_obj_create(parent);
    lv_obj_set_size(heart, size, size);
    lv_obj_set_style_bg_opa(heart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(heart, 0, 0);
    lv_obj_set_style_pad_all(heart, 0, 0);
    lv_obj_clear_flag(heart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(heart, heart_event_cb, LV_EVENT_DRAW_MAIN, NULL);
    return heart;
}

void ui_theme_pulse_heart(lv_obj_t *heart)
{
    if (!heart) return;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, heart);
    lv_anim_set_values(&a, LV_IMG_ZOOM_NONE, LV_IMG_ZOOM_NONE + 30);
    lv_anim_set_time(&a, UI_HEART_BEAT_MS / 2);
    lv_anim_set_playback_time(&a, UI_HEART_BEAT_MS / 2);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_transform_zoom);
    lv_anim_start(&a);
}
