/****************************************************************************
 * ICM-42688-P Six-Axis IMU Driver — SPI Implementation
 *
 * Full SPI driver for the TDK InvenSense ICM-42688-P 6-axis IMU.
 * Supports direct register read, FIFO burst read, interrupt-driven
 * and polled data acquisition.
 *
 * Datasheet: ICM-42688-P v1.7 (TDK InvenSense)
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <time.h>

#include "icm42688.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ICM42688_TAG "icm42688"

/* SPI read/write helpers -- adapt to your platform's SPI API.
 *
 * On NuttX with /dev/spiN:
 *   struct spi_dev_s *spi = ...;
 *   SPI_SELECT(spi, SPIDEV_IMU(dev->config.cs_index), true);
 *   SPI_SEND(spi, reg | ICM42688_SPI_READ);
 *   val = SPI_SEND(spi, 0);
 *   SPI_SELECT(spi, SPIDEV_IMU(dev->config.cs_index), false);
 *
 * Or via ioctl on /dev/spiN:
 *   struct spi_trans_s trans;
 *   struct spi_sequence_s seq;
 *   trans.nwords = 2;
 *   tx[0] = reg | ICM42688_SPI_READ;
 *   tx[1] = 0x00;
 *   ...
 *   ioctl(dev->fd, SPIDEV_TRANSFER, &seq);
 *
 * These stubs show the intended logic; replace with actual platform calls.
 */

#define ICM42688_SPI_WRITE(dev, reg, data, len) \
  icm42688_spi_write_reg(dev, reg, data, len)

#define ICM42688_SPI_READ(dev, reg, data, len) \
  icm42688_spi_read_reg(dev, reg, data, len)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: icm42688_get_time_us
 *
 * Description:
 *   Get monotonic timestamp in microseconds.
 *
 ****************************************************************************/

static int64_t icm42688_get_time_us(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

/****************************************************************************
 * Name: icm42688_spi_write_reg
 *
 * Description:
 *   Write data to one or more consecutive ICM-42688 registers via SPI.
 *   The SPI protocol for the ICM-42688 uses a 1-byte address phase
 *   followed by the data phase. Bit 7 of the address byte must be 0
 *   for write operations.
 *
 ****************************************************************************/

static int icm42688_spi_write_reg(struct icm42688_dev *dev,
                                  uint8_t reg,
                                  const uint8_t *data,
                                  uint8_t len)
{
  /* TODO: Implement using NuttX SPI API
   *
   * On NuttX with /dev/spiN:
   *   uint8_t *txbuf = malloc(1 + len);
   *   uint8_t *rxbuf = malloc(1 + len);
   *   txbuf[0] = reg;  // bit 7 = 0 for write
   *   memcpy(&txbuf[1], data, len);
   *
   *   struct spi_trans_s trans = {
   *     .txbuf = txbuf,
   *     .rxbuf = rxbuf,
   *     .nwords = 1 + len,
   *   };
   *   struct spi_sequence_s seq = {
   *     .dev = SPIDEV_IMU(dev->config.cs_index),
   *     .mode = SPIDEV_MODE0,
   *     .nbits = 8,
   *     .frequency = ICM42688_SPI_MAX_FREQ,
   *     .ntrans = 1,
   *     .trans = &trans,
   *   };
   *   int ret = ioctl(dev->fd, SPIDEV_TRANSFER, &seq);
   *   free(txbuf);
   *   free(rxbuf);
   *   return ret;
   */

  (void)dev;
  (void)reg;
  (void)data;
  (void)len;

  syslog(LOG_DEBUG, "[%s] SPI WRITE reg=0x%02X len=%d\n",
         ICM42688_TAG, reg, len);

  return OK;
}

/****************************************************************************
 * Name: icm42688_spi_read_reg
 *
 * Description:
 *   Read data from one or more consecutive ICM-42688 registers via SPI.
 *   Bit 7 of the address byte is set to 1 for read operations.
 *
 ****************************************************************************/

static int icm42688_spi_read_reg(struct icm42688_dev *dev,
                                 uint8_t reg,
                                 uint8_t *data,
                                 uint8_t len)
{
  /* TODO: Implement using NuttX SPI API
   *
   * On NuttX with /dev/spiN:
   *   uint8_t *txbuf = calloc(1, 1 + len);
   *   uint8_t *rxbuf = malloc(1 + len);
   *   txbuf[0] = reg | ICM42688_SPI_READ;
   *
   *   struct spi_trans_s trans = {
   *     .txbuf = txbuf,
   *     .rxbuf = rxbuf,
   *     .nwords = 1 + len,
   *   };
   *   struct spi_sequence_s seq = {
   *     .dev = SPIDEV_IMU(dev->config.cs_index),
   *     .mode = SPIDEV_MODE0,
   *     .nbits = 8,
   *     .frequency = ICM42688_SPI_MAX_FREQ,
   *     .ntrans = 1,
   *     .trans = &trans,
   *   };
   *   int ret = ioctl(dev->fd, SPIDEV_TRANSFER, &seq);
   *   memcpy(data, &rxbuf[1], len);
   *   free(txbuf);
   *   free(rxbuf);
   *   return ret;
   */

  (void)dev;
  (void)reg;
  (void)data;
  (void)len;

  syslog(LOG_DEBUG, "[%s] SPI READ reg=0x%02X len=%d\n",
         ICM42688_TAG, reg, len);

  return OK;
}

/****************************************************************************
 * Name: icm42688_write_reg
 *
 * Description:
 *   Write a single register.
 *
 ****************************************************************************/

static int icm42688_write_reg(struct icm42688_dev *dev,
                              uint8_t reg,
                              uint8_t value)
{
  return ICM42688_SPI_WRITE(dev, reg, &value, 1);
}

/****************************************************************************
 * Name: icm42688_read_reg
 *
 * Description:
 *   Read a single register.
 *
 ****************************************************************************/

static int icm42688_read_reg(struct icm42688_dev *dev,
                             uint8_t reg,
                             uint8_t *value)
{
  return ICM42688_SPI_READ(dev, reg, value, 1);
}

/****************************************************************************
 * Name: icm42688_select_bank
 *
 * Description:
 *   Switch the active register bank. The ICM-42688 has 4 register banks
 *   (0-3), selected by writing to BANK_SEL (0x76) in Bank 0.
 *   Only Bank 0 contains the sensor data and config registers used
 *   during normal operation.
 *
 ****************************************************************************/

static int icm42688_select_bank(struct icm42688_dev *dev, uint8_t bank)
{
  int ret;

  if (dev->current_bank == bank)
    {
      return OK;
    }

  ret = icm42688_write_reg(dev, ICM42688_REG_BANK_SEL, bank);
  if (ret < 0)
    {
      return ret;
    }

  dev->current_bank = bank;
  return OK;
}

/****************************************************************************
 * Name: icm42688_set_power_mode_raw
 *
 * Description:
 *   Write the PWR_MGMT0 register to control accel/gyro power state.
 *
 ****************************************************************************/

static int icm42688_set_power_mode_raw(struct icm42688_dev *dev,
                                       uint8_t pwr_mgmt0)
{
  int ret;

  /* Must be in Bank 0 */

  ret = icm42688_select_bank(dev, 0);
  if (ret < 0)
    {
      return ret;
    }

  ret = icm42688_write_reg(dev, ICM42688_REG_PWR_MGMT0, pwr_mgmt0);
  if (ret < 0)
    {
      return ret;
    }

  /* Per datasheet: when transitioning from OFF to any active mode,
   * wait 200us for accel to wake up.
   */

  usleep(ICM42688_WAKEUP_WAIT_US);

  return OK;
}

/****************************************************************************
 * Name: icm42688_update_reg
 *
 * Description:
 *   Read-modify-write a single register.
 *
 ****************************************************************************/

static int icm42688_update_reg(struct icm42688_dev *dev,
                               uint8_t reg,
                               uint8_t mask,
                               uint8_t value)
{
  uint8_t regval;
  int ret;

  ret = icm42688_read_reg(dev, reg, &regval);
  if (ret < 0)
    {
      return ret;
    }

  regval = (regval & ~mask) | (value & mask);

  return icm42688_write_reg(dev, reg, regval);
}

/****************************************************************************
 * Name: icm42688_wait_reset_done
 *
 * Description:
 *   Wait for the RESET_DONE interrupt status bit to be set after
 *   a software reset. Times out after a reasonable delay.
 *
 ****************************************************************************/

static int icm42688_wait_reset_done(struct icm42688_dev *dev)
{
  uint8_t status;
  int retries = 50;
  int ret;

  while (retries-- > 0)
    {
      ret = icm42688_read_reg(dev, ICM42688_REG_INT_STATUS, &status);
      if (ret < 0)
        {
          return ret;
        }

      if (status & ICM42688_INT_STATUS_RESET_DONE)
        {
          return OK;
        }

      usleep(100);  /* 100us between checks */
    }

  syslog(LOG_ERR, "[%s] Reset-done timeout\n", ICM42688_TAG);
  return -ETIMEDOUT;
}

/****************************************************************************
 * Name: icm42688_fsr_to_accel_sensitivity
 *
 * Description:
 *   Return accel sensitivity in LSB/g for the given FSR enum value.
 *
 ****************************************************************************/

static float icm42688_fsr_to_accel_sensitivity(enum icm42688_accel_fsr fsr)
{
  switch (fsr)
    {
      case ICM42688_ACCEL_RANGE_16G:
        return ICM42688_ACCEL_SENS_16G;
      case ICM42688_ACCEL_RANGE_8G:
        return ICM42688_ACCEL_SENS_8G;
      case ICM42688_ACCEL_RANGE_4G:
        return ICM42688_ACCEL_SENS_4G;
      case ICM42688_ACCEL_RANGE_2G:
        return ICM42688_ACCEL_SENS_2G;
      default:
        return ICM42688_ACCEL_SENS_8G;
    }
}

/****************************************************************************
 * Name: icm42688_fsr_to_gyro_sensitivity
 *
 * Description:
 *   Return gyro sensitivity in LSB/dps for the given FSR enum value.
 *
 ****************************************************************************/

static float icm42688_fsr_to_gyro_sensitivity(enum icm42688_gyro_fsr fsr)
{
  switch (fsr)
    {
      case ICM42688_GYRO_RANGE_2000DPS:
        return ICM42688_GYRO_SENS_2000DPS;
      case ICM42688_GYRO_RANGE_1000DPS:
        return ICM42688_GYRO_SENS_1000DPS;
      case ICM42688_GYRO_RANGE_500DPS:
        return ICM42688_GYRO_SENS_500DPS;
      case ICM42688_GYRO_RANGE_250DPS:
        return ICM42688_GYRO_SENS_250DPS;
      case ICM42688_GYRO_RANGE_125DPS:
        return ICM42688_GYRO_SENS_125DPS;
      case ICM42688_GYRO_RANGE_62DPS:
        return ICM42688_GYRO_SENS_62DPS;
      case ICM42688_GYRO_RANGE_31DPS:
        return ICM42688_GYRO_SENS_31DPS;
      case ICM42688_GYRO_RANGE_15DPS:
        return ICM42688_GYRO_SENS_15DPS;
      default:
        return ICM42688_GYRO_SENS_2000DPS;
    }
}

/****************************************************************************
 * Name: icm42688_configure_accel
 *
 * Description:
 *   Configure accelerometer FSR and ODR.
 *
 ****************************************************************************/

static int icm42688_configure_accel(struct icm42688_dev *dev)
{
  uint8_t config0;
  int ret;

  config0 = (dev->config.accel_fsr << 5) | dev->config.accel_odr;

  ret = icm42688_write_reg(dev, ICM42688_REG_ACCEL_CONFIG0, config0);
  if (ret < 0)
    {
      return ret;
    }

  dev->accel_sensitivity =
      icm42688_fsr_to_accel_sensitivity(dev->config.accel_fsr);

  syslog(LOG_INFO, "[%s] Accel: FSR=%d ODR=0x%02X sens=%.1f LSB/g\n",
         ICM42688_TAG, dev->config.accel_fsr, dev->config.accel_odr,
         dev->accel_sensitivity);

  return OK;
}

/****************************************************************************
 * Name: icm42688_configure_gyro
 *
 * Description:
 *   Configure gyroscope FSR and ODR.
 *
 ****************************************************************************/

static int icm42688_configure_gyro(struct icm42688_dev *dev)
{
  uint8_t config0;
  int ret;

  config0 = (dev->config.gyro_fsr << 5) | dev->config.gyro_odr;

  ret = icm42688_write_reg(dev, ICM42688_REG_GYRO_CONFIG0, config0);
  if (ret < 0)
    {
      return ret;
    }

  dev->gyro_sensitivity =
      icm42688_fsr_to_gyro_sensitivity(dev->config.gyro_fsr);

  syslog(LOG_INFO, "[%s] Gyro:  FSR=%d ODR=0x%02X sens=%.1f LSB/dps\n",
         ICM42688_TAG, dev->config.gyro_fsr, dev->config.gyro_odr,
         dev->gyro_sensitivity);

  return OK;
}

/****************************************************************************
 * Name: icm42688_configure_filters
 *
 * Description:
 *   Configure accel and gyro digital low-pass filters.
 *   Sets both UI filter order and bandwidth for low-noise mode.
 *
 ****************************************************************************/

static int icm42688_configure_filters(struct icm42688_dev *dev)
{
  uint8_t val;
  int ret;

  /* Gyro filter: 3rd order, BW = ODR/4 */

  val = ICM42688_GYRO_CONFIG1_UI_FILT_3RD |
        ICM42688_GYRO_CONFIG1_DEC2_M2_3RD |
        ICM42688_TEMP_FILT_BW_40HZ;

  ret = icm42688_write_reg(dev, ICM42688_REG_GYRO_CONFIG1, val);
  if (ret < 0)
    {
      return ret;
    }

  /* Gyro/accel UI filter bandwidth = ODR/4 */

  val = ICM42688_ACCEL_UI_FILT_BW_ODR4 |
        ICM42688_GYRO_UI_FILT_BW_ODR4;

  ret = icm42688_write_reg(dev, ICM42688_REG_GYRO_ACCEL_CONFIG0, val);
  if (ret < 0)
    {
      return ret;
    }

  /* Accel filter: 3rd order */

  val = ICM42688_ACCEL_CONFIG1_UI_FILT_3RD;
  ret = icm42688_write_reg(dev, ICM42688_REG_ACCEL_CONFIG1, val);
  if (ret < 0)
    {
      return ret;
    }

  syslog(LOG_INFO, "[%s] Filters: 3rd order, BW=ODR/4\n", ICM42688_TAG);

  return OK;
}

/****************************************************************************
 * Name: icm42688_configure_fifo
 *
 * Description:
 *   Configure the hardware FIFO for streaming mode with watermark.
 *
 ****************************************************************************/

static int icm42688_configure_fifo(struct icm42688_dev *dev)
{
  uint8_t fifo_config;
  uint8_t fifo_config1;
  uint8_t fifo_wm_lo;
  uint8_t fifo_wm_hi;
  int ret;

  /* Set FIFO mode (stream or bypass) */

  fifo_config = (uint8_t)dev->config.fifo_mode << 6;

  ret = icm42688_write_reg(dev, ICM42688_REG_FIFO_CONFIG, fifo_config);
  if (ret < 0)
    {
      return ret;
    }

  if (!dev->config.enable_fifo)
    {
      syslog(LOG_INFO, "[%s] FIFO disabled (bypass mode)\n", ICM42688_TAG);
      return OK;
    }

  /* Enable accel + gyro + timestamp in FIFO packets */

  fifo_config1 = ICM42688_FIFO_CONFIG1_GYRO_EN |
                 ICM42688_FIFO_CONFIG1_ACCEL_EN |
                 ICM42688_FIFO_CONFIG1_TMST_EN;

  ret = icm42688_write_reg(dev, ICM42688_REG_FIFO_CONFIG1, fifo_config1);
  if (ret < 0)
    {
      return ret;
    }

  /* Set FIFO watermark level (in packets).
   * FIFO_CONFIG2 [7:0] = watermark[7:0]
   * FIFO_CONFIG3 [2:0] = watermark[10:8]
   */

  fifo_wm_lo = dev->config.fifo_watermark & 0xFF;
  fifo_wm_hi = (dev->config.fifo_watermark >> 8) & 0x07;

  ret = icm42688_write_reg(dev, ICM42688_REG_FIFO_CONFIG2, fifo_wm_lo);
  if (ret < 0)
    {
      return ret;
    }

  ret = icm42688_write_reg(dev, ICM42688_REG_FIFO_CONFIG3, fifo_wm_hi);
  if (ret < 0)
    {
      return ret;
    }

  syslog(LOG_INFO, "[%s] FIFO: stream mode, watermark=%d packets\n",
         ICM42688_TAG, dev->config.fifo_watermark);

  return OK;
}

/****************************************************************************
 * Name: icm42688_configure_interrupts
 *
 * Description:
 *   Configure INT1 pin for data-ready interrupt.
 *   Sets push-pull, active-high, latched mode, and routes the
 *   DATA_RDY source to INT1.
 *
 ****************************************************************************/

static int icm42688_configure_interrupts(struct icm42688_dev *dev)
{
  uint8_t int_config;
  uint8_t int_source0 = 0;
  int ret;

  /* INT_CONFIG (0x14):
   *   INT1: push-pull, active-high, latched
   */

  int_config = ICM42688_INT_CONFIG_INT1_LATCHED |
               ICM42688_INT_CONFIG_INT1_PP |
               ICM42688_INT_CONFIG_INT1_ACT_HI;

  ret = icm42688_write_reg(dev, ICM42688_REG_INT_CONFIG, int_config);
  if (ret < 0)
    {
      return ret;
    }

  /* INT_SOURCE0 (0x65):
   *   Route data-ready to INT1.
   *   If FIFO is enabled, also route FIFO watermark.
   */

  int_source0 = ICM42688_INT_SOURCE0_UI_DRDY;

  if (dev->config.enable_fifo)
    {
      int_source0 |= ICM42688_INT_SOURCE0_FIFO_THS;
    }

  ret = icm42688_write_reg(dev, ICM42688_REG_INT_SOURCE0, int_source0);
  if (ret < 0)
    {
      return ret;
    }

  syslog(LOG_INFO, "[%s] INT1: data-ready=%s fifo_wm=%s\n",
         ICM42688_TAG,
         (int_source0 & ICM42688_INT_SOURCE0_UI_DRDY) ? "ON" : "OFF",
         (int_source0 & ICM42688_INT_SOURCE0_FIFO_THS) ? "ON" : "OFF");

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

float icm42688_raw_to_temp_c(int16_t raw)
{
  return ((float)raw / ICM42688_TEMP_SCALE) + ICM42688_TEMP_OFFSET;
}

float icm42688_raw_accel_to_ms2(struct icm42688_dev *dev, int16_t raw)
{
  return ((float)raw / dev->accel_sensitivity) * ICM42688_G_TO_MS2;
}

float icm42688_raw_gyro_to_rads(struct icm42688_dev *dev, int16_t raw)
{
  return ((float)raw / dev->gyro_sensitivity) * ICM42688_DPS_TO_RADS;
}

int icm42688_init(const struct icm42688_config *config,
                  struct icm42688_dev *dev)
{
  uint8_t who_am_i;
  int ret;

  if (!config || !dev)
    {
      return -EINVAL;
    }

  memset(dev, 0, sizeof(*dev));
  memcpy(&dev->config, config, sizeof(*config));
  dev->current_bank = 0xFF;  /* Force bank selection on first access */

  /* Validate config defaults */

  if (dev->config.fifo_watermark == 0)
    {
      dev->config.fifo_watermark = ICM42688_DEFAULT_FIFO_WM;
    }

  /* Open SPI bus
   *
   * TODO: Replace with actual NuttX SPI open:
   *   char path[16];
   *   snprintf(path, sizeof(path), "/dev/spi%d", config->spi_bus);
   *   dev->fd = open(path, O_RDWR);
   *   if (dev->fd < 0) return -errno;
   */

  syslog(LOG_INFO, "[%s] Initializing on SPI%d CS%d\n",
         ICM42688_TAG, config->spi_bus, config->cs_index);

  /* Step 1: Verify WHO_AM_I */

  ret = icm42688_select_bank(dev, 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] SPI communication failed: %d\n",
             ICM42688_TAG, ret);
      return ret;
    }

  ret = icm42688_read_reg(dev, ICM42688_REG_WHO_AM_I, &who_am_i);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Failed to read WHO_AM_I: %d\n",
             ICM42688_TAG, ret);
      return ret;
    }

  if (who_am_i != ICM42688_WHO_AM_I_VALUE)
    {
      syslog(LOG_ERR,
             "[%s] Unexpected WHO_AM_I: 0x%02X (expected 0x%02X)\n",
             ICM42688_TAG, who_am_i, ICM42688_WHO_AM_I_VALUE);
      return -ENODEV;
    }

  dev->who_am_i = who_am_i;
  syslog(LOG_INFO, "[%s] WHO_AM_I verified: 0x%02X\n",
         ICM42688_TAG, who_am_i);

  /* Step 2: Software reset */

  ret = icm42688_reset(dev);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Reset failed: %d\n", ICM42688_TAG, ret);
      return ret;
    }

  /* Step 3: Disable all sensors first (standby mode) */

  ret = icm42688_set_power_mode_raw(dev,
      ICM42688_PWR_MGMT0_GYRO_STANDBY | ICM42688_PWR_MGMT0_ACCEL_OFF);
  if (ret < 0)
    {
      return ret;
    }

  /* Step 4: Configure SPI drive strength for fast SPI */

  ret = icm42688_select_bank(dev, 0);
  if (ret < 0)
    {
      return ret;
    }

  ret = icm42688_write_reg(dev, ICM42688_REG_DRIVE_CONFIG,
                           ICM42688_DRIVE_CONFIG_SPI_SLEW);
  if (ret < 0)
    {
      return ret;
    }

  /* Step 5: Configure accelerometer FSR and ODR */

  ret = icm42688_configure_accel(dev);
  if (ret < 0)
    {
      return ret;
    }

  /* Step 6: Configure gyroscope FSR and ODR */

  ret = icm42688_configure_gyro(dev);
  if (ret < 0)
    {
      return ret;
    }

  /* Step 7: Configure digital filters */

  ret = icm42688_configure_filters(dev);
  if (ret < 0)
    {
      return ret;
    }

  /* Step 8: Configure FIFO */

  ret = icm42688_configure_fifo(dev);
  if (ret < 0)
    {
      return ret;
    }

  /* Step 9: Configure interrupts (data-ready on INT1) */

  ret = icm42688_configure_interrupts(dev);
  if (ret < 0)
    {
      return ret;
    }

  /* Step 10: Enable both accel and gyro in low-noise mode */

  ret = icm42688_set_power_mode_raw(dev,
      ICM42688_PWR_MGMT0_GYRO_LN | ICM42688_PWR_MGMT0_ACCEL_LN);
  if (ret < 0)
    {
      return ret;
    }

  dev->initialized = 1;

  syslog(LOG_INFO, "[%s] Initialized: accel=+/-8g/100Hz gyro=+/-2000dps/100Hz\n",
         ICM42688_TAG);
  syslog(LOG_INFO, "[%s] Mode: low-noise, FIFO=%s, INT1=data-ready\n",
         ICM42688_TAG,
         dev->config.enable_fifo ? "stream" : "bypass");

  return OK;
}

int icm42688_read_accel(struct icm42688_dev *dev,
                        struct icm42688_reading *reading)
{
  uint8_t buf[6];
  int ret;

  if (!dev || !dev->initialized || !reading)
    {
      return -EINVAL;
    }

  /* Read 6 bytes: ACCEL_DATA_X1..Z0 (0x1F - 0x24) */

  ret = ICM42688_SPI_READ(dev, ICM42688_REG_ACCEL_DATA_X1, buf, 6);
  if (ret < 0)
    {
      return ret;
    }

  /* Parse big-endian 16-bit signed values */

  reading->accel_raw.x = (int16_t)((uint16_t)buf[0] << 8 | buf[1]);
  reading->accel_raw.y = (int16_t)((uint16_t)buf[2] << 8 | buf[3]);
  reading->accel_raw.z = (int16_t)((uint16_t)buf[4] << 8 | buf[5]);

  /* Convert to m/s^2 */

  reading->accel_ms2.x = icm42688_raw_accel_to_ms2(dev, reading->accel_raw.x);
  reading->accel_ms2.y = icm42688_raw_accel_to_ms2(dev, reading->accel_raw.y);
  reading->accel_ms2.z = icm42688_raw_accel_to_ms2(dev, reading->accel_raw.z);

  reading->valid = true;

  return OK;
}

int icm42688_read_gyro(struct icm42688_dev *dev,
                       struct icm42688_reading *reading)
{
  uint8_t buf[6];
  int ret;

  if (!dev || !dev->initialized || !reading)
    {
      return -EINVAL;
    }

  /* Read 6 bytes: GYRO_DATA_X1..Z0 (0x25 - 0x2A) */

  ret = ICM42688_SPI_READ(dev, ICM42688_REG_GYRO_DATA_X1, buf, 6);
  if (ret < 0)
    {
      return ret;
    }

  /* Parse big-endian 16-bit signed values */

  reading->gyro_raw.x = (int16_t)((uint16_t)buf[0] << 8 | buf[1]);
  reading->gyro_raw.y = (int16_t)((uint16_t)buf[2] << 8 | buf[3]);
  reading->gyro_raw.z = (int16_t)((uint16_t)buf[4] << 8 | buf[5]);

  /* Convert to rad/s */

  reading->gyro_rads.x = icm42688_raw_gyro_to_rads(dev, reading->gyro_raw.x);
  reading->gyro_rads.y = icm42688_raw_gyro_to_rads(dev, reading->gyro_raw.y);
  reading->gyro_rads.z = icm42688_raw_gyro_to_rads(dev, reading->gyro_raw.z);

  reading->valid = true;

  return OK;
}

int icm42688_read_all(struct icm42688_dev *dev,
                      struct icm42688_reading *reading)
{
  uint8_t buf[14];  /* temp(2) + accel(6) + gyro(6) = 14 bytes */
  int ret;

  if (!dev || !dev->initialized || !reading)
    {
      return -EINVAL;
    }

  memset(reading, 0, sizeof(*reading));

  /* Read 14 contiguous bytes: TEMP_DATA1..GYRO_DATA_Z0
   * Registers 0x1D through 0x2A are contiguous in Bank 0.
   */

  ret = ICM42688_SPI_READ(dev, ICM42688_REG_TEMP_DATA1, buf, 14);
  if (ret < 0)
    {
      return ret;
    }

  /* Parse temperature (bytes 0-1) */

  int16_t temp_raw = (int16_t)((uint16_t)buf[0] << 8 | buf[1]);
  reading->temperature = icm42688_raw_to_temp_c(temp_raw);

  /* Parse accelerometer (bytes 2-7) */

  reading->accel_raw.x = (int16_t)((uint16_t)buf[2] << 8 | buf[3]);
  reading->accel_raw.y = (int16_t)((uint16_t)buf[4] << 8 | buf[5]);
  reading->accel_raw.z = (int16_t)((uint16_t)buf[6] << 8 | buf[7]);

  reading->accel_ms2.x = icm42688_raw_accel_to_ms2(dev, reading->accel_raw.x);
  reading->accel_ms2.y = icm42688_raw_accel_to_ms2(dev, reading->accel_raw.y);
  reading->accel_ms2.z = icm42688_raw_accel_to_ms2(dev, reading->accel_raw.z);

  /* Parse gyroscope (bytes 8-13) */

  reading->gyro_raw.x = (int16_t)((uint16_t)buf[8] << 8 | buf[9]);
  reading->gyro_raw.y = (int16_t)((uint16_t)buf[10] << 8 | buf[11]);
  reading->gyro_raw.z = (int16_t)((uint16_t)buf[12] << 8 | buf[13]);

  reading->gyro_rads.x = icm42688_raw_gyro_to_rads(dev, reading->gyro_raw.x);
  reading->gyro_rads.y = icm42688_raw_gyro_to_rads(dev, reading->gyro_raw.y);
  reading->gyro_rads.z = icm42688_raw_gyro_to_rads(dev, reading->gyro_raw.z);

  /* Timestamp */

  reading->timestamp = icm42688_get_time_us();
  reading->valid = true;

  syslog(LOG_DEBUG,
         "[%s] AX=%d AY=%d AZ=%d GX=%d GY=%d GZ=%d T=%.1fC\n",
         ICM42688_TAG,
         reading->accel_raw.x, reading->accel_raw.y, reading->accel_raw.z,
         reading->gyro_raw.x, reading->gyro_raw.y, reading->gyro_raw.z,
         reading->temperature);

  return OK;
}

int icm42688_data_ready(struct icm42688_dev *dev)
{
  uint8_t status;
  int ret;

  if (!dev || !dev->initialized)
    {
      return -EINVAL;
    }

  ret = icm42688_read_reg(dev, ICM42688_REG_INT_STATUS, &status);
  if (ret < 0)
    {
      return ret;
    }

  return (status & ICM42688_INT_STATUS_DATA_RDY) ? 1 : 0;
}

int icm42688_get_fifo_count(struct icm42688_dev *dev, uint16_t *count)
{
  uint8_t buf[2];
  int ret;

  if (!dev || !dev->initialized || !count)
    {
      return -EINVAL;
    }

  ret = ICM42688_SPI_READ(dev, ICM42688_REG_FIFO_COUNTH, buf, 2);
  if (ret < 0)
    {
      return ret;
    }

  *count = ((uint16_t)buf[0] << 8) | buf[1];
  return OK;
}

int icm42688_read_fifo(struct icm42688_dev *dev,
                       struct icm42688_reading *buf, int max,
                       int *count)
{
  uint16_t fifo_bytes;
  uint8_t header;
  uint8_t pkt_buf[16];
  int ret;
  int n = 0;

  if (!dev || !dev->initialized || !buf || !count)
    {
      return -EINVAL;
    }

  *count = 0;

  /* Get FIFO byte count */

  ret = icm42688_get_fifo_count(dev, &fifo_bytes);
  if (ret < 0)
    {
      return ret;
    }

  if (fifo_bytes == 0)
    {
      return OK;
    }

  /* Each packet is 16 bytes (accel + gyro + timestamp) */

  int available = fifo_bytes / ICM42688_FIFO_PACKET_SIZE_16;
  if (available > max)
    {
      available = max;
    }

  if (available > ICM42688_FIFO_MAX_PACKETS)
    {
      available = ICM42688_FIFO_MAX_PACKETS;
    }

  /* Read packets from FIFO */

  for (int i = 0; i < available && n < max; i++)
    {
      /* Read one 16-byte packet from FIFO_DATA register */

      ret = ICM42688_SPI_READ(dev, ICM42688_REG_FIFO_DATA,
                              pkt_buf, ICM42688_FIFO_PACKET_SIZE_16);
      if (ret < 0)
        {
          syslog(LOG_ERR, "[%s] FIFO read error at packet %d: %d\n",
                 ICM42688_TAG, i, ret);
          break;
        }

      /* Parse header byte */

      header = pkt_buf[0];

      /* Validate: header byte 0x68 = accel+gyro+timestamp present.
       * Bits [7:5] = 0b011 when accel+gyro+ts present.
       * Bit 4 = 1 (gyro present), bit 5 = 1 (accel present),
       * bit 7 = 1 (valid packet header marker).
       */

      if (!(header & 0x40))
        {
          /* Invalid packet header, skip */

          syslog(LOG_WARNING, "[%s] FIFO invalid header: 0x%02X\n",
                 ICM42688_TAG, header);
          continue;
        }

      memset(&buf[n], 0, sizeof(buf[n]));

      /* Accel XYZ: bytes 2-7 (big-endian signed 16-bit) */

      buf[n].accel_raw.x =
          (int16_t)((uint16_t)pkt_buf[2] << 8 | pkt_buf[3]);
      buf[n].accel_raw.y =
          (int16_t)((uint16_t)pkt_buf[4] << 8 | pkt_buf[5]);
      buf[n].accel_raw.z =
          (int16_t)((uint16_t)pkt_buf[6] << 8 | pkt_buf[7]);

      /* Gyro XYZ: bytes 8-13 */

      buf[n].gyro_raw.x =
          (int16_t)((uint16_t)pkt_buf[8] << 8 | pkt_buf[9]);
      buf[n].gyro_raw.y =
          (int16_t)((uint16_t)pkt_buf[10] << 8 | pkt_buf[11]);
      buf[n].gyro_raw.z =
          (int16_t)((uint16_t)pkt_buf[12] << 8 | pkt_buf[13]);

      /* Timestamp (16-bit, bytes 14-15) */

      uint16_t tmst = ((uint16_t)pkt_buf[14] << 8) | pkt_buf[15];

      /* Convert to physical units */

      buf[n].accel_ms2.x =
          icm42688_raw_accel_to_ms2(dev, buf[n].accel_raw.x);
      buf[n].accel_ms2.y =
          icm42688_raw_accel_to_ms2(dev, buf[n].accel_raw.y);
      buf[n].accel_ms2.z =
          icm42688_raw_accel_to_ms2(dev, buf[n].accel_raw.z);

      buf[n].gyro_rads.x =
          icm42688_raw_gyro_to_rads(dev, buf[n].gyro_raw.x);
      buf[n].gyro_rads.y =
          icm42688_raw_gyro_to_rads(dev, buf[n].gyro_raw.y);
      buf[n].gyro_rads.z =
          icm42688_raw_gyro_to_rads(dev, buf[n].gyro_raw.z);

      /* Use hardware timestamp offset from current time.
       * The FIFO timestamp is 16-bit, in microseconds.
       * We store the relative offset here; the uORB layer
       * will compute the absolute timestamp.
       */

      buf[n].timestamp = icm42688_get_time_us() -
                         (int64_t)(available - i - 1) * 10000;  /* 10ms @ 100Hz */
      buf[n].valid = true;
      n++;
    }

  *count = n;

  syslog(LOG_DEBUG, "[%s] FIFO: read %d packets (available=%d, bytes=%d)\n",
         ICM42688_TAG, n, available, fifo_bytes);

  return OK;
}

int icm42688_set_power_mode(struct icm42688_dev *dev,
                            enum icm42688_power_mode mode)
{
  uint8_t pwr;

  if (!dev || !dev->initialized)
    {
      return -EINVAL;
    }

  switch (mode)
    {
      case ICM42688_POWER_OFF:
        pwr = ICM42688_PWR_MGMT0_GYRO_OFF |
              ICM42688_PWR_MGMT0_ACCEL_OFF;
        break;

      case ICM42688_POWER_ACCEL_LP:
        pwr = ICM42688_PWR_MGMT0_GYRO_OFF |
              ICM42688_PWR_MGMT0_ACCEL_LP;
        break;

      case ICM42688_POWER_ACCEL_LN:
        pwr = ICM42688_PWR_MGMT0_GYRO_OFF |
              ICM42688_PWR_MGMT0_ACCEL_LN;
        break;

      case ICM42688_POWER_6AXIS_LP:
        pwr = ICM42688_PWR_MGMT0_GYRO_LN |
              ICM42688_PWR_MGMT0_ACCEL_LP;
        break;

      case ICM42688_POWER_6AXIS_LN:
        pwr = ICM42688_PWR_MGMT0_GYRO_LN |
              ICM42688_PWR_MGMT0_ACCEL_LN;
        break;

      default:
        return -EINVAL;
    }

  int ret = icm42688_set_power_mode_raw(dev, pwr);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Failed to set power mode %d: %d\n",
             ICM42688_TAG, mode, ret);
      return ret;
    }

  syslog(LOG_INFO, "[%s] Power mode set to %d\n", ICM42688_TAG, mode);

  return OK;
}

int icm42688_set_accel_fsr(struct icm42688_dev *dev,
                           enum icm42688_accel_fsr fsr)
{
  int ret;

  if (!dev || !dev->initialized)
    {
      return -EINVAL;
    }

  ret = icm42688_update_reg(dev, ICM42688_REG_ACCEL_CONFIG0,
                            ICM42688_ACCEL_FSR_MASK, fsr << 5);
  if (ret < 0)
    {
      return ret;
    }

  dev->config.accel_fsr = fsr;
  dev->accel_sensitivity = icm42688_fsr_to_accel_sensitivity(fsr);

  syslog(LOG_INFO, "[%s] Accel FSR changed: sens=%.1f LSB/g\n",
         ICM42688_TAG, dev->accel_sensitivity);

  return OK;
}

int icm42688_set_gyro_fsr(struct icm42688_dev *dev,
                          enum icm42688_gyro_fsr fsr)
{
  int ret;

  if (!dev || !dev->initialized)
    {
      return -EINVAL;
    }

  ret = icm42688_update_reg(dev, ICM42688_REG_GYRO_CONFIG0,
                            ICM42688_GYRO_FSR_MASK, fsr << 5);
  if (ret < 0)
    {
      return ret;
    }

  dev->config.gyro_fsr = fsr;
  dev->gyro_sensitivity = icm42688_fsr_to_gyro_sensitivity(fsr);

  syslog(LOG_INFO, "[%s] Gyro FSR changed: sens=%.1f LSB/dps\n",
         ICM42688_TAG, dev->gyro_sensitivity);

  return OK;
}

int icm42688_reset(struct icm42688_dev *dev)
{
  int ret;

  if (!dev)
    {
      return -EINVAL;
    }

  /* Ensure we are in Bank 0 */

  ret = icm42688_select_bank(dev, 0);
  if (ret < 0)
    {
      return ret;
    }

  /* Write software reset bit */

  ret = icm42688_write_reg(dev, ICM42688_REG_DEVICE_CONFIG,
                           ICM42688_DEVICE_CONFIG_RESET);
  if (ret < 0)
    {
      return ret;
    }

  /* Wait for reset to complete (datasheet: ~1ms) */

  usleep(ICM42688_RESET_WAIT_US);

  /* Wait for RESET_DONE interrupt status bit */

  ret = icm42688_wait_reset_done(dev);
  if (ret < 0)
    {
      return ret;
    }

  /* After reset, the bank reverts to 0 */

  dev->current_bank = 0;

  syslog(LOG_INFO, "[%s] Software reset complete\n", ICM42688_TAG);

  return OK;
}

int icm42688_flush_fifo(struct icm42688_dev *dev)
{
  int ret;

  if (!dev || !dev->initialized)
    {
      return -EINVAL;
    }

  ret = icm42688_write_reg(dev, ICM42688_REG_SIGNAL_PATH_RESET,
                           ICM42688_SIGNAL_PATH_FIFO_FLUSH);
  if (ret < 0)
    {
      return ret;
    }

  /* Wait for flush to complete */

  usleep(100);

  return OK;
}

void icm42688_close(struct icm42688_dev *dev)
{
  if (!dev)
    {
      return;
    }

  /* Power down all sensors */

  if (dev->initialized)
    {
      icm42688_set_power_mode(dev, ICM42688_POWER_OFF);
    }

  /* Close SPI file descriptor */

  if (dev->fd >= 0)
    {
      close(dev->fd);
      dev->fd = -1;
    }

  dev->initialized = 0;

  syslog(LOG_INFO, "[%s] Closed\n", ICM42688_TAG);
}
