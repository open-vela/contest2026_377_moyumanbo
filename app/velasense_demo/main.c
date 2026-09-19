/* Display-only entry point. NuttX CMake aliases main to velasense_main. */
#include <nuttx/config.h>
#include <nuttx/lcd/lcd_dev.h>
#include <sys/ioctl.h>
#include <sys/boardctl.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "demo_ui.h"

#ifdef CONFIG_SYSTEM_NSH
int nsh_main(int argc, char **argv);
#endif

/* Serial diagnostics read only scalars; all LVGL work stays on the UI task. */
/* NuttX owns SysTick, so the HAL's default uwTick never advances.
 * Drive HAL transfer deadlines from the same monotonic clock as LVGL. */
uint32_t HAL_GetTick(void)
{
  struct timespec ts = {0};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static volatile bool ui_running;
static volatile unsigned int rendered_frames;
static volatile unsigned int ui_heartbeat;
static volatile int requested_page = -1;
static volatile int visible_page;
static volatile int display_width, display_height;
static volatile bool touch_ready;

static void frame_ready(lv_event_t *e)
{
  (void)e;
  rendered_frames++;
}

static int wait_for_lcd(void)
{
  /* SiFli registers /dev/lcd0 from an asynchronous board init task. */
  for (unsigned int attempt = 0; attempt < 150; attempt++)
    {
      int fd = open("/dev/lcd0", O_RDWR);
      if (fd >= 0)
        {
          if (ioctl(fd, LCDDEVIO_SETPOWER, CONFIG_LCD_MAXPOWER) < 0)
            {
              printf("VelaSense: LCD power-on failed: %d\n", errno);
              close(fd);
              return -1;
            }
          close(fd);
          return 0;
        }
      usleep(100000);
    }
  printf("VelaSense: /dev/lcd0 unavailable after 15s; check LCD bring-up\n");
  return -1;
}

int main(int argc, char **argv)
{
  /* Initial tasks may inherit /dev/null; bind logs before spawning NSH. */
  int console = open("/dev/console", O_RDWR);
  if (console >= 0)
    {
      dup2(console, STDIN_FILENO);
      dup2(console, STDOUT_FILENO);
      dup2(console, STDERR_FILENO);
      if (console > STDERR_FILENO) close(console);
    }
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;
  if (argc > 1)
    {
      if (strcmp(argv[1], "status") == 0)
        {
          printf("VelaSense: running=%d LCD=%dx%d touch=%d page=%d "
                 "frames=%u heartbeat=%u\n", ui_running,
                 display_width, display_height, touch_ready, visible_page,
                 rendered_frames, ui_heartbeat);
          return 0;
        }
      if (argc == 3 && strcmp(argv[1], "page") == 0 &&
          argv[2][0] >= '0' && argv[2][0] <= '3' && argv[2][1] == '\0')
        {
          if (!ui_running) return 1;
          requested_page = argv[2][0] - '0';
          return 0;
        }
      printf("Usage: velasense [status | page 0..3]\n");
      return 1;
    }

  if (lv_is_initialized())
    {
      printf("VelaSense: UI already running\n");
      return 1;
    }

#if !defined(CONFIG_BOARD_LATE_INITIALIZE) && defined(CONFIG_BOARDCTL)
  if (boardctl(BOARDIOC_INIT, 0) < 0)
    {
      perror("VelaSense: board init");
      return 1;
    }
#endif

#ifdef CONFIG_SYSTEM_NSH
  /* The firmware boots directly into this app; keep a recovery console. */
  static bool console_started;
  if (!console_started)
    {
      console_started = task_create("nsh", 100, 16384, nsh_main, NULL) >= 0;
    }
#endif

  printf("VelaSense: starting display demo\n");
  if (wait_for_lcd() < 0)
    return 1;

  /* Touch is optional. Keep displaying if the controller is absent. */
  for (unsigned int i = 0; i < 20 && access("/dev/input0", F_OK) < 0; i++)
    usleep(100000);

  lv_init();
  lv_nuttx_dsc_init(&info);
  info.fb_path = "/dev/lcd0";
  info.input_path = access("/dev/input0", F_OK) == 0 ? "/dev/input0" : NULL;
  lv_nuttx_init(&info, &result);
  if (result.disp == NULL)
    {
      printf("VelaSense: LVGL LCD initialization failed\n");
      lv_nuttx_deinit(&result);
      lv_deinit();
      return 1;
    }

  printf("VelaSense: LCD %ld x %ld, touch %s\n",
         (long)lv_display_get_horizontal_resolution(result.disp),
         (long)lv_display_get_vertical_resolution(result.disp),
         result.indev ? "ready" : "unavailable (automatic page tour)");
  display_width = lv_display_get_horizontal_resolution(result.disp);
  display_height = lv_display_get_vertical_resolution(result.disp);
  touch_ready = result.indev != NULL;
  lv_display_add_event_cb(result.disp, frame_ready, LV_EVENT_REFR_READY, NULL);
  demo_ui_create(true);
  ui_running = true;
  printf("VelaSense: UI created, beginning display refresh\n");

  while (true)
    {
      if (requested_page >= 0)
        {
          int page = requested_page;
          requested_page = -1;
          demo_ui_show((enum demo_page)page);
        }
      uint32_t idle = lv_timer_handler();
      visible_page = demo_ui_current();
      ui_heartbeat++;
      /* Bound sleeps for responsive touch, clock, and animation updates. */
      if (idle < 1) idle = 1;
      if (idle > 20) idle = 20;
      usleep(idle * 1000);
    }
}
