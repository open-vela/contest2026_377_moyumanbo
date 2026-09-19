#ifndef VELASENSE_DEMO_UI_H
#define VELASENSE_DEMO_UI_H

#include <stdbool.h>
#include <lvgl/lvgl.h>

LV_FONT_DECLARE(lv_font_velasense_16);

enum demo_page { DEMO_HOME, DEMO_TREND, DEMO_BREATH, DEMO_SETTINGS, DEMO_PAGE_COUNT };
void demo_ui_create(bool auto_rotate);
void demo_ui_show(enum demo_page page);
void demo_ui_destroy(void);
enum demo_page demo_ui_current(void);

#endif
