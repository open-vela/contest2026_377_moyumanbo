/* Render and exercise the same LVGL widgets that run on the board. */
#include "demo_ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH=390, HEIGHT=450 };
static uint16_t pixels[WIDTH*HEIGHT], draw[WIDTH*40];
static int touch_x, touch_y;
static bool pressed;

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
  int w = lv_area_get_width(area);
  for (int y = area->y1; y <= area->y2; y++)
    memcpy(pixels+y*WIDTH+area->x1, (uint16_t *)data+(y-area->y1)*w, w*2);
  lv_display_flush_ready(display);
}

static void input(lv_indev_t *dev, lv_indev_data_t *data)
{
  (void)dev;
  data->point.x = touch_x;
  data->point.y = touch_y;
  data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void advance(unsigned ms)
{
  for (unsigned i=0; i<ms; i+=20) { lv_tick_inc(20); lv_timer_handler(); }
}

static void tap(int x, int y)
{
  touch_x=x; touch_y=y;
  pressed=true; advance(80);
  pressed=false; advance(100);
}

static void capture(const char *directory, const char *name)
{
  char path[512];
  snprintf(path,sizeof(path),"%s/%s.ppm",directory,name);
  lv_refr_now(NULL);
  FILE *f=fopen(path,"wb");
  assert(f);
  fprintf(f,"P6\n%d %d\n255\n",WIDTH,HEIGHT);
  for(int i=0;i<WIDTH*HEIGHT;i++)
    {
      unsigned char rgb[3] = {
        (unsigned char)(((pixels[i]>>11)&31)*255/31),
        (unsigned char)(((pixels[i]>>5)&63)*255/63),
        (unsigned char)((pixels[i]&31)*255/31)};
      fwrite(rgb,1,3,f);
    }
  fclose(f);
  printf("Rendered %s\n",path);
}

int main(int argc,char **argv)
{
  assert(argc==2);
  lv_init();
  lv_display_t *display=lv_display_create(WIDTH,HEIGHT);
  lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,draw,NULL,sizeof(draw),LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display,flush);
  lv_indev_t *pointer=lv_indev_create();
  lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(pointer,input);
  demo_ui_create(false);
  advance(100);
  capture(argv[1],"01-home");
  tap(280,320); capture(argv[1],"05-mood");
  tap(110,195); capture(argv[1],"06-saved");
  tap(190,325);
  tap(150,419); assert(demo_ui_current()==DEMO_TREND);
  capture(argv[1],"02-trend");
  tap(240,419); assert(demo_ui_current()==DEMO_BREATH);
  capture(argv[1],"03-breath-ready");
  tap(190,370); advance(1500);
  capture(argv[1],"03-breath");
  advance(3200); capture(argv[1],"07-hold");
  advance(4000); capture(argv[1],"08-exhale");
  tap(190,370);
  capture(argv[1],"09-breath-paused");
  tap(330,419); assert(demo_ui_current()==DEMO_SETTINGS);
  capture(argv[1],"04-settings");
  tap(325,216); advance(8100);
  assert(demo_ui_current()==DEMO_HOME);
  /* Repeated navigation exercises object/timer lifetime without recreating pages. */
  for(int i=0;i<100;i++)
    { tap(150,419); tap(240,419); tap(330,419); tap(65,419); }
  demo_ui_destroy();
  lv_deinit();
  puts("PASS: navigation, mood dialog, breathing phases, automatic tour, 400 page changes.");
  return 0;
}
