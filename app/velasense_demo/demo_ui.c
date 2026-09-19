/* VelaSense visual demo. All physiological values are sample data. */
#include "demo_ui.h"
#include <stdint.h>
#include <stdio.h>
#include <lvgl/src/misc/lv_text_private.h>

#define BG      0x0B1718
#define CARD    0x14282A
#define LINE    0x294042
#define TEXT    0xF0F4EC
#define MUTED   0x8FAAA6
#define MINT    0xBFE8CB
#define INK     0x18372B
#define ACCENT  0x5BC8A8
#define CORAL   0xF1B1A0

static lv_obj_t *root, *pages[DEMO_PAGE_COUNT], *nav[DEMO_PAGE_COUNT];
static lv_obj_t *clock_label, *breath_disk, *breath_phase, *breath_count;
static lv_obj_t *breath_button_label, *modal, *tour_switch;
static lv_timer_t *timer;
static enum demo_page current;
static bool touring, breathing;
static uint32_t tour_at, breath_at;
static int32_t sw, sh;
static unsigned int missing_glyphs;
static const char *const names[] = {"Overview", "Your rhythm", "A little pause", "Your space"};
static const char *const tabs[] = {"首页", "趋势", "呼吸", "设置"};
static const char *const icons[] = {LV_SYMBOL_HOME, LV_SYMBOL_LIST, LV_SYMBOL_LOOP, LV_SYMBOL_SETTINGS};

static int32_t X(int32_t v) { return v * sw / 390; }
static int32_t Y(int32_t v) { return v * sh / 450; }
static lv_color_t C(uint32_t v) { return lv_color_hex(v); }

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, X(x), Y(y));
  lv_obj_set_size(obj, X(w), Y(h));
  lv_obj_set_style_bg_color(obj, C(color), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(obj, X(radius), 0);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y,
                       const lv_font_t *font, uint32_t color)
{
  uint32_t offset = 0;
  while (text[offset])
    {
      uint32_t codepoint = lv_text_encoded_next(text, &offset);
      lv_font_glyph_dsc_t glyph;
      if (codepoint >= 32 &&
          (!lv_font_get_glyph_dsc(font, &glyph, codepoint, 0) || glyph.is_placeholder))
        {
          printf("VelaSense font missing U+%04lx in %s\n",
                 (unsigned long)codepoint, text);
          missing_glyphs++;
        }
    }
  lv_obj_t *obj = lv_label_create(parent);
  lv_label_set_text(obj, text);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_style_text_color(obj, C(color), 0);
  lv_obj_set_pos(obj, X(x), Y(y));
  return obj;
}

static void centered(lv_obj_t *parent, const char *text, int y,
                     const lv_font_t *font, uint32_t color)
{
  lv_obj_t *obj = label(parent, text, 0, 0, font, color);
  lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, Y(y));
}

static void touch_activity(void)
{
  tour_at = lv_tick_get();
}

static void close_event(lv_event_t *e)
{
  (void)e;
  if (modal) lv_obj_delete(modal);
  modal = NULL;
  touch_activity();
}

static void choose_event(lv_event_t *e)
{
  lv_obj_t *choice = lv_event_get_target(e);
  lv_obj_t *text = lv_obj_get_child(choice, 0);
  const char *selected = lv_label_get_text(text);
  char message[96];
  snprintf(message, sizeof(message), "%s · 已记录", selected);
  lv_obj_clean(modal);
  centered(modal, LV_SYMBOL_OK, 66, &lv_font_montserrat_48, ACCENT);
  centered(modal, message, 138, &lv_font_velasense_16, TEXT);
  centered(modal, "A moment for yourself.", 174, &lv_font_montserrat_18, MUTED);
  lv_obj_t *done = box(modal, 40, 242, 240, 46, MINT, 23);
  centered(done, "完成", 15, &lv_font_velasense_16, INK);
  lv_obj_add_flag(done, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(done, close_event, LV_EVENT_CLICKED, NULL);
}

static void open_event(lv_event_t *e)
{
  (void)e;
  touch_activity();
  if (modal) return;
  modal = box(root, 35, 60, 320, 318, CARD, 24);
  lv_obj_set_style_border_width(modal, 1, 0);
  lv_obj_set_style_border_color(modal, C(ACCENT), 0);
  centered(modal, "How are you feeling?", 28, &lv_font_montserrat_24, TEXT);
  centered(modal, "记录此刻的心情", 66, &lv_font_velasense_16, MUTED);
  static const char *const choices[] = {"平静", "开心", "紧张", "疲惫"};
  for (int i = 0; i < 4; i++)
    {
      lv_obj_t *b = box(modal, 24 + (i % 2) * 142, 106 + (i / 2) * 66, 130, 54,
                        i == 0 ? MINT : LINE, 16);
      centered(b, choices[i], 19, &lv_font_velasense_16, i == 0 ? INK : TEXT);
      lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(b, choose_event, LV_EVENT_CLICKED, NULL);
    }
  lv_obj_t *cancel = box(modal, 80, 252, 160, 44, CARD, 10);
  centered(cancel, "稍后再说", 13, &lv_font_velasense_16, MUTED);
  lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(cancel, close_event, LV_EVENT_CLICKED, NULL);
}

static void home_page(lv_obj_t *page)
{
  lv_obj_t *hero = box(page, 18, 94, 354, 180, MINT, 24);
  label(hero, "TODAY'S BALANCE", 20, 18, &lv_font_montserrat_14, INK);
  label(hero, "In balance.", 20, 44, &lv_font_montserrat_24, INK);
  label(hero, "给自己一个平静的时刻", 20, 80, &lv_font_velasense_16, INK);
  label(hero, "72", 20, 113, &lv_font_montserrat_48, INK);
  label(hero, "bpm", 87, 142, &lv_font_montserrat_14, INK);

  lv_obj_t *arc = lv_arc_create(hero);
  lv_obj_set_size(arc, X(120), Y(120));
  lv_obj_set_pos(arc, X(216), Y(30));
  lv_arc_set_rotation(arc, 135);
  lv_arc_set_bg_angles(arc, 0, 270);
  lv_arc_set_value(arc, 86);
  lv_obj_set_style_arc_color(arc, C(0xA3CDB0), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, C(INK), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(arc, X(7), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, X(7), LV_PART_INDICATOR);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  centered(arc, "86", 34, &lv_font_montserrat_24, INK);
  centered(arc, "CALM", 66, &lv_font_montserrat_14, INK);

  lv_obj_t *a = box(page, 18, 286, 171, 66, CARD, 18);
  label(a, "HRV", 16, 11, &lv_font_montserrat_14, MUTED);
  label(a, "48 ms", 16, 32, &lv_font_montserrat_24, TEXT);
  label(a, LV_SYMBOL_UP, 130, 34, &lv_font_montserrat_18, ACCENT);
  lv_obj_t *b = box(page, 201, 286, 171, 66, CARD, 18);
  label(b, "MOMENTS", 16, 11, &lv_font_montserrat_14, MUTED);
  label(b, "03", 16, 32, &lv_font_montserrat_24, TEXT);
  label(b, "记录心情  " LV_SYMBOL_RIGHT, 63, 37, &lv_font_velasense_16, MINT);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(b, open_event, LV_EVENT_CLICKED, NULL);
  centered(page, "慢一点，听见自己的节奏", 365, &lv_font_velasense_16, MUTED);
}

static void trend_page(lv_obj_t *page)
{
  lv_obj_t *card = box(page, 18, 94, 354, 177, CARD, 24);
  label(card, "HEART RATE", 18, 17, &lv_font_montserrat_14, MUTED);
  label(card, "68 - 92", 18, 41, &lv_font_montserrat_24, TEXT);
  label(card, "bpm  /  TODAY", 142, 48, &lv_font_montserrat_14, MUTED);
  lv_obj_t *chart = lv_chart_create(card);
  lv_obj_set_pos(chart, X(16), Y(83));
  lv_obj_set_size(chart, X(322), Y(62));
  lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chart, 0, 0);
  lv_obj_set_style_pad_all(chart, 0, 0);
  lv_obj_set_style_line_color(chart, C(LINE), LV_PART_MAIN);
  lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
  lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 60, 100);
  lv_chart_set_div_line_count(chart, 3, 0);
  lv_chart_set_point_count(chart, 24);
  lv_chart_series_t *series = lv_chart_add_series(chart, C(ACCENT), LV_CHART_AXIS_PRIMARY_Y);
  static const int32_t values[] = {70,72,68,71,75,74,82,91,86,77,74,72,78,81,75,72,70,73,80,77,74,72,73,72};
  for (int i = 0; i < 24; i++) lv_chart_set_next_value(chart, series, values[i]);
  label(card, "08:00", 17, 153, &lv_font_montserrat_14, MUTED);
  label(card, "12:00", 151, 153, &lv_font_montserrat_14, MUTED);
  label(card, "18:00", 290, 153, &lv_font_montserrat_14, MUTED);

  label(page, "你的今日片刻", 22, 286, &lv_font_velasense_16, TEXT);
  lv_obj_t *row = box(page, 18, 313, 354, 59, CARD, 18);
  box(row, 16, 22, 7, 7, ACCENT, 4);
  label(row, "14:32", 35, 12, &lv_font_montserrat_18, TEXT);
  label(row, "午后的小放松", 117, 14, &lv_font_velasense_16, MINT);
  label(row, "A calm afternoon", 117, 35, &lv_font_montserrat_14, MUTED);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(row, open_event, LV_EVENT_CLICKED, NULL);
}

static void breathe_toggle(lv_event_t *e)
{
  (void)e;
  breathing = !breathing;
  breath_at = lv_tick_get();
  lv_label_set_text(breath_button_label, breathing ? "暂停" : "开始呼吸");
  if (breathing)
    {
      lv_obj_align(breath_phase, LV_ALIGN_CENTER, 0, -Y(16));
      lv_label_set_text(breath_phase, "Breathe in");
      lv_label_set_text(breath_count, "4 s");
      lv_obj_remove_flag(breath_count, LV_OBJ_FLAG_HIDDEN);
    }
  else
    {
      lv_label_set_text(breath_phase, "Ready?");
      lv_obj_align(breath_phase, LV_ALIGN_CENTER, 0, -Y(16));
      lv_obj_add_flag(breath_count, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_size(breath_disk, X(180), Y(180));
      lv_obj_set_pos(breath_disk, X(18), Y(18));
    }
  touch_activity();
}

static void breath_page(lv_obj_t *page)
{
  lv_obj_t *outer = box(page, 87, 99, 216, 216, CARD, 108);
  lv_obj_set_style_border_width(outer, 1, 0);
  lv_obj_set_style_border_color(outer, C(LINE), 0);
  breath_disk = box(outer, 18, 18, 180, 180, MINT, 90);
  breath_phase = label(outer, "Ready?", 0, 0, &lv_font_montserrat_24, INK);
  lv_obj_align(breath_phase, LV_ALIGN_CENTER, 0, -Y(16));
  breath_count = label(outer, "", 0, 0, &lv_font_montserrat_18, INK);
  lv_obj_align(breath_count, LV_ALIGN_CENTER, 0, Y(18));
  lv_obj_add_flag(breath_count, LV_OBJ_FLAG_HIDDEN);
  centered(page, "吸气4秒 · 停留4秒 · 呼气6秒", 326, &lv_font_velasense_16, MUTED);
  lv_obj_t *button = box(page, 88, 352, 214, 36, MINT, 18);
  breath_button_label = label(button, "开始呼吸", 0, 0, &lv_font_velasense_16, INK);
  lv_obj_center(breath_button_label);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(button, breathe_toggle, LV_EVENT_CLICKED, NULL);
}

static void tour_changed(lv_event_t *e)
{
  touring = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  touch_activity();
}

static void switch_style(lv_obj_t *obj)
{
  lv_obj_set_style_bg_color(obj, C(LINE), LV_PART_MAIN);
  lv_obj_set_style_bg_color(obj, C(ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_size(obj, X(48), Y(26));
}

static void settings_page(lv_obj_t *page)
{
  lv_obj_t *id = box(page, 18, 94, 354, 80, MINT, 24);
  label(id, "心迹", 20, 17, &lv_font_velasense_16, INK);
  label(id, "VelaSense", 20, 41, &lv_font_montserrat_24, INK);
  label(id, "DISPLAY\nEDITION", 245, 24, &lv_font_montserrat_14, INK);
  lv_obj_t *row = box(page, 18, 186, 354, 61, CARD, 18);
  label(row, "自动展示", 18, 12, &lv_font_velasense_16, TEXT);
  label(row, "Tour the screens", 18, 35, &lv_font_montserrat_14, MUTED);
  tour_switch = lv_switch_create(row);
  switch_style(tour_switch);
  lv_obj_set_pos(tour_switch, X(287), Y(17));
  if (touring) lv_obj_add_state(tour_switch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(tour_switch, tour_changed, LV_EVENT_VALUE_CHANGED, NULL);

  row = box(page, 18, 257, 354, 61, CARD, 18);
  label(row, "温柔提醒", 18, 12, &lv_font_velasense_16, TEXT);
  label(row, "A little time for you", 18, 35, &lv_font_montserrat_14, MUTED);
  lv_obj_t *toggle = lv_switch_create(row);
  switch_style(toggle);
  lv_obj_set_pos(toggle, X(287), Y(17));
  lv_obj_add_state(toggle, LV_STATE_CHECKED);
  centered(page, "演示数据 · 仅供界面展示", 336, &lv_font_velasense_16, MUTED);
  centered(page, "openvela  /  VelaSense 0.1", 362, &lv_font_montserrat_14, MUTED);
}

void demo_ui_show(enum demo_page page)
{
  if (page < 0 || page >= DEMO_PAGE_COUNT || !root) return;
  if (modal) { lv_obj_delete(modal); modal = NULL; }
  current = page;
  for (int i = 0; i < DEMO_PAGE_COUNT; i++)
    {
      if (i == (int)page) lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_bg_color(nav[i], C(i == (int)page ? MINT : CARD), 0);
      lv_obj_set_style_text_color(nav[i], C(i == (int)page ? INK : MUTED), 0);
    }
  touch_activity();
}

static void navigate(lv_event_t *e)
{
  demo_ui_show((enum demo_page)(intptr_t)lv_event_get_user_data(e));
}

static void tick(lv_timer_t *t)
{
  (void)t;
  uint32_t now = lv_tick_get();
  uint32_t minutes = now / 60000;
  lv_label_set_text_fmt(clock_label, "%02lu:%02lu",
                       (unsigned long)(((9*60 + 41 + minutes) / 60) % 24),
                       (unsigned long)((41 + minutes) % 60));
  if (breathing)
    {
      uint32_t ms = (now - breath_at) % 14000;
      const char *phase = ms < 4000 ? "Breathe in" : ms < 8000 ? "Hold" : "Breathe out";
      uint32_t left = ms < 4000 ? 4000-ms : ms < 8000 ? 8000-ms : 14000-ms;
      int size = ms < 4000 ? 156 + ms*34/4000 : ms < 8000 ? 190 : 190-(ms-8000)*34/6000;
      lv_obj_set_size(breath_disk, X(size), Y(size));
      lv_obj_center(breath_disk);
      lv_label_set_text(breath_phase, phase);
      lv_label_set_text_fmt(breath_count, "%lu s", (unsigned long)((left+999)/1000));
    }
  if (touring && !modal && !breathing && now-tour_at >= 8000)
    demo_ui_show((current+1) % DEMO_PAGE_COUNT);
}

void demo_ui_create(bool auto_rotate)
{
  missing_glyphs = 0;
  sw = lv_display_get_horizontal_resolution(NULL);
  sh = lv_display_get_vertical_resolution(NULL);
  touring = auto_rotate;
  breathing = false;
  root = lv_obj_create(NULL);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, sw, sh);
  lv_obj_set_style_bg_color(root, C(BG), 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  clock_label = label(root, "09:41", 26, 15, &lv_font_montserrat_14, TEXT);
  lv_obj_t *battery_label = label(root, LV_SYMBOL_BLUETOOTH "  85%  " LV_SYMBOL_BATTERY_3, 269, 15, &lv_font_montserrat_14, MUTED);
  for (int i = 0; i < DEMO_PAGE_COUNT; i++)
    {
      pages[i] = box(root, 0, 39, 390, 355, BG, 0);
      /* Pages use screen coordinates; offset their content origin to zero. */
      lv_obj_set_pos(pages[i], 0, 0);
      lv_obj_set_height(pages[i], Y(394));
      label(pages[i], names[i], 22, 47, &lv_font_montserrat_24, TEXT);
      lv_obj_t *badge = box(pages[i], 310, 50, 60, 23, CARD, 11);
      centered(badge, "DEMO", 5, &lv_font_montserrat_14, ACCENT);
    }
  home_page(pages[DEMO_HOME]);
  trend_page(pages[DEMO_TREND]);
  breath_page(pages[DEMO_BREATH]);
  settings_page(pages[DEMO_SETTINGS]);
  /* Status bar above the full-size pages. */
  lv_obj_move_foreground(clock_label);
  lv_obj_move_foreground(battery_label);
  lv_obj_t *bar = box(root, 18, 399, 354, 40, CARD, 20);
  for (int i = 0; i < DEMO_PAGE_COUNT; i++)
    {
      nav[i] = box(bar, 3+i*87, 3, 87, 34, CARD, 17);
      lv_obj_add_flag(nav[i], LV_OBJ_FLAG_CLICKABLE);
      lv_obj_t *icon = label(nav[i], icons[i], 13, 10, &lv_font_montserrat_14, TEXT);
      lv_obj_remove_local_style_prop(icon, LV_STYLE_TEXT_COLOR, 0);
      lv_obj_t *text = label(nav[i], tabs[i], 34, 9, &lv_font_velasense_16, TEXT);
      lv_obj_remove_local_style_prop(text, LV_STYLE_TEXT_COLOR, 0);
      lv_obj_add_event_cb(nav[i], navigate, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
  lv_screen_load(root);
  demo_ui_show(DEMO_HOME);
  timer = lv_timer_create(tick, 100, NULL);
  printf("VelaSense: font check, %u missing glyphs\n", missing_glyphs);
}

enum demo_page demo_ui_current(void) { return current; }

void demo_ui_destroy(void)
{
  if (timer) lv_timer_delete(timer);
  timer = NULL;
  if (root)
    {
      if (lv_screen_active() == root) lv_screen_load(lv_obj_create(NULL));
      lv_obj_delete(root);
    }
  root = modal = NULL;
  breathing = false;
}
