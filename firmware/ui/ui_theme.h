/**
 * @file ui_theme.h
 * @brief VelaSense UI Theme - CO5300 AMOLED (390x450) dark mode with heart motif
 *
 * Color palette:
 *   Background:  #0A0E27 (deep navy)
 *   Coral:       #FF6B6B (excitement/alert)
 *   Teal:        #4ECDC4 (calm/normal)
 *   Amber:       #FFE66D (warning)
 *   Surface:     #151937 (card/panel bg)
 *   Text:        #E8E8F0 (primary text)
 *   Text dim:    #6B7094 (secondary text)
 */

#ifndef UI_THEME_H
#define UI_THEME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* ── Display geometry ─────────────────────────────────────────────── */
#define UI_DISP_HOR          390
#define UI_DISP_VER          450

/* ── Core palette ─────────────────────────────────────────────────── */
#define UI_COLOR_BG          lv_color_hex(0x0A0E27)
#define UI_COLOR_SURFACE     lv_color_hex(0x151937)
#define UI_COLOR_SURFACE2    lv_color_hex(0x1E2350)
#define UI_COLOR_CORAL       lv_color_hex(0xFF6B6B)
#define UI_COLOR_TEAL        lv_color_hex(0x4ECDC4)
#define UI_COLOR_AMBER       lv_color_hex(0xFFE66D)
#define UI_COLOR_TEXT        lv_color_hex(0xE8E8F0)
#define UI_COLOR_TEXT_DIM    lv_color_hex(0x6B7094)
#define UI_COLOR_DIVIDER     lv_color_hex(0x2A2F55)
#define UI_COLOR_ERROR       lv_color_hex(0xFF4444)
#define UI_COLOR_SUCCESS     lv_color_hex(0x44CC88)

/* ── SQI colours ──────────────────────────────────────────────────── */
#define UI_COLOR_SQI_GOOD    UI_COLOR_SUCCESS
#define UI_COLOR_SQI_FAIR    UI_COLOR_AMBER
#define UI_COLOR_SQI_POOR    UI_COLOR_ERROR

/* ── Font sizes (px) ──────────────────────────────────────────────── */
#define UI_FONT_HUGE         64      /* HR value on home               */
#define UI_FONT_LARGE        32      /* phase text, section headings   */
#define UI_FONT_MEDIUM       20      /* labels, values                 */
#define UI_FONT_SMALL        14      /* captions, hints                */

/* ── Spacing / radius ─────────────────────────────────────────────── */
#define UI_PAD               16
#define UI_PAD_SMALL         8
#define UI_RADIUS            16
#define UI_RADIUS_SMALL      8

/* ── Animation timings (ms) ──────────────────────────────────────── */
#define UI_ANIM_FAST         200
#define UI_ANIM_NORMAL       350
#define UI_ANIM_SLOW         600
#define UI_HEART_BEAT_MS     800

/* ── Event timer ──────────────────────────────────────────────────── */
#define UI_EVENT_TIMEOUT_S   60

/* ── Breathing pattern (seconds) ──────────────────────────────────── */
#define UI_BREATH_INHALE     4
#define UI_BREATH_HOLD       4
#define UI_BREATH_EXHALE     6
#define UI_BREATH_SESSION_S  (5 * 60)

/* ── Public API ───────────────────────────────────────────────────── */

/**
 * @brief Apply the VelaSense theme to the running LVGL instance.
 *        Call once during UI init, before any screen is created.
 */
void ui_theme_init(void);

/**
 * @brief Create the large pulsing heart icon as an LVGL object.
 *        Caller owns the returned object; place it wherever needed.
 */
lv_obj_t *ui_theme_create_heart_icon(lv_obj_t *parent, lv_coord_t size);

/**
 * @brief Trigger a heartbeat animation on a previously-created heart icon.
 */
void ui_theme_pulse_heart(lv_obj_t *heart);

#ifdef __cplusplus
}
#endif

#endif /* UI_THEME_H */
