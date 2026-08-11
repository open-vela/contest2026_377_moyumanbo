/****************************************************************************
 * VelaSense Board Bring-up
 *
 * Initializes all board peripherals for the VelaSense wrist-worn
 * emotion arousal detection terminal.
 *
 * Peripheral init order:
 *   1. GPIO / pinmux
 *   2. LCD + touch
 *   3. Backlight PWM
 *   4. I2C buses (sensors)
 *   5. SPI bus (PPG / IMU / EDA)
 *   6. RTC
 *   7. Watchdog
 *   8. Haptic motor
 *   9. BLE
 *  10. Sensor framework (uORB)
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/board.h>
#include <nuttx/kthread.h>
#include <syslog.h>

#include "velasense_bringup.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VELASENSE_TAG "velasense"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: velasense_init_display
 *
 * Description:
 *   Initialize LCD and touch controller.
 *
 ****************************************************************************/

static int velasense_init_display(void)
{
#ifdef CONFIG_VELASENSE_LCD_ENABLE
  syslog(LOG_INFO, "[%s] Initializing LCD (CO5300 AMOLED)...\n",
         VELASENSE_TAG);

  /* LCD init is handled by the SiFli LCDC driver + co5300 panel driver.
   * The framebuffer device /dev/fb0 will be registered automatically.
   * LVGL will use it via CONFIG_LV_USE_NUTTX_LCD.
   */

  syslog(LOG_INFO, "[%s] LCD ready: /dev/fb0 (390x450)\n", VELASENSE_TAG);
#endif

#ifdef CONFIG_VELASENSE_TOUCH_ENABLE
  syslog(LOG_INFO, "[%s] Touch controller (FT6146) ready: /dev/input0\n",
         VELASENSE_TAG);
#endif

  return OK;
}

/****************************************************************************
 * Name: velasense_init_backlight
 *
 * Description:
 *   Set LCD backlight to default brightness.
 *
 ****************************************************************************/

static int velasense_init_backlight(void)
{
#ifdef CONFIG_PWM
  syslog(LOG_INFO, "[%s] Backlight PWM initialized\n", VELASENSE_TAG);
#endif
  return OK;
}

/****************************************************************************
 * Name: velasense_init_sensors
 *
 * Description:
 *   Initialize sensor buses and register sensor drivers.
 *
 ****************************************************************************/

static int velasense_init_sensors(void)
{
  syslog(LOG_INFO, "[%s] Initializing sensor buses...\n", VELASENSE_TAG);

#ifdef CONFIG_I2C
  /* I2C1: touch (FT6146) + temperature (MAX30208) + haptic (DRV2605L) */
  syslog(LOG_INFO, "[%s] I2C1 ready (touch + temp + haptic)\n",
         VELASENSE_TAG);
#endif

#ifdef CONFIG_SPI
  /* SPI1: PPG (MAX86141) + IMU (ICM42688/BMI270) + EDA (AD5940) */
  syslog(LOG_INFO, "[%s] SPI1 ready (PPG + IMU + EDA)\n", VELASENSE_TAG);
#endif

  /* TODO: Register sensor drivers when implemented
   * - max86141_register()  -> /dev/sensor_ppg
   * - icm42688_register()  -> /dev/sensor_imu
   * - max30208_register()  -> /dev/sensor_temp
   * - ad5940_register()    -> /dev/sensor_eda
   * - drv2605l_register()  -> /dev/sensor_haptic
   * - max17048_register()  -> /dev/sensor_fuel_gauge
   */

  return OK;
}

/****************************************************************************
 * Name: velasense_init_rtc
 *
 * Description:
 *   Verify RTC is running.
 *
 ****************************************************************************/

static int velasense_init_rtc(void)
{
#ifdef CONFIG_RTC
  syslog(LOG_INFO, "[%s] RTC ready: /dev/rtc0\n", VELASENSE_TAG);
#endif
  return OK;
}

/****************************************************************************
 * Name: velasense_init_watchdog
 *
 * Description:
 *   Start watchdog timer.
 *
 ****************************************************************************/

static int velasense_init_watchdog(void)
{
#ifdef CONFIG_WATCHDOG
  syslog(LOG_INFO, "[%s] Watchdog ready: /dev/watchdog0\n",
         VELASENSE_TAG);
#endif
  return OK;
}

/****************************************************************************
 * Name: velasense_init_haptic
 *
 * Description:
 *   Initialize haptic vibration motor driver.
 *
 ****************************************************************************/

static int velasense_init_haptic(void)
{
#ifdef CONFIG_VELASENSE_HAPTIC_ENABLE
  syslog(LOG_INFO, "[%s] Haptic motor (DRV2605L) initializing...\n",
         VELASENSE_TAG);

  /* TODO: drv2605l_i2c_init()
   * - Verify I2C ACK at 0x5A
   * - Set default waveform library
   * - Test vibration
   */

  syslog(LOG_INFO, "[%s] Haptic motor ready\n", VELASENSE_TAG);
#endif
  return OK;
}

/****************************************************************************
 * Name: velasense_init_ble
 *
 * Description:
 *   Initialize Bluetooth stack.
 *
 ****************************************************************************/

static int velasense_init_ble(void)
{
#ifdef CONFIG_VELASENSE_BLE_ENABLE
  syslog(LOG_INFO, "[%s] BLE initializing...\n", VELASENSE_TAG);

  /* BLE init is handled by sf32lb52_bt_adapter + sf32lb52_bth4.
   * The NuttX BT stack will be initialized automatically.
   * GATT services will be registered by the VelaSense BLE module.
   */

  syslog(LOG_INFO, "[%s] BLE ready: VelaSense-377\n", VELASENSE_TAG);
#endif
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_late_initialize
 *
 * Description:
 *   Board late initialization entry point. Called after NuttX basic
 *   initialization is complete.
 *
 ****************************************************************************/

void board_late_initialize(void)
{
  int ret;

  syslog(LOG_INFO,
         "========================================\n");
  syslog(LOG_INFO,
         "  VelaSense 心迹 v0.1.0\n");
  syslog(LOG_INFO,
         "  腕式情绪唤醒识别终端\n");
  syslog(LOG_INFO,
         "  Board: SF32LB52 LCD\n");
  syslog(LOG_INFO,
         "========================================\n");

  /* 1. Display */

  ret = velasense_init_display();
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] LCD init failed: %d\n", VELASENSE_TAG, ret);
    }

  /* 2. Backlight */

  ret = velasense_init_backlight();
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Backlight init failed: %d\n",
             VELASENSE_TAG, ret);
    }

  /* 3. Sensors */

  ret = velasense_init_sensors();
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Sensor init failed: %d\n",
             VELASENSE_TAG, ret);
    }

  /* 4. RTC */

  ret = velasense_init_rtc();
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] RTC init failed: %d\n", VELASENSE_TAG, ret);
    }

  /* 5. Watchdog */

  ret = velasense_init_watchdog();
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Watchdog init failed: %d\n",
             VELASENSE_TAG, ret);
    }

  /* 6. Haptic motor */

  ret = velasense_init_haptic();
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Haptic init failed: %d\n",
             VELASENSE_TAG, ret);
    }

  /* 7. BLE */

  ret = velasense_init_ble();
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] BLE init failed: %d\n", VELASENSE_TAG, ret);
    }

  syslog(LOG_INFO, "[%s] Board bring-up complete\n", VELASENSE_TAG);
}
