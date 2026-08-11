/****************************************************************************
 * MAX30208 Precision Digital Temperature Sensor Driver
 *
 * I2C driver for the MAX30208 skin temperature sensor.
 * Supports one-shot and continuous conversion modes with FIFO readout.
 *
 * Datasheet: MAX30208 Rev 2 (Analog Devices / Maxim Integrated)
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

#include "max30208.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX30208_TAG "max30208"

/* I2C read/write helpers — adapt to your platform's I2C API.
 * On NuttX, use the i2c_master_s interface or /dev/i2cN with ioctl.
 * These stubs show the intended logic; replace with actual platform calls.
 */

#define MAX30208_I2C_WRITE(dev, reg, data, len) \
  max30208_i2c_write_reg(dev, reg, data, len)

#define MAX30208_I2C_READ(dev, reg, data, len) \
  max30208_i2c_read_reg(dev, reg, data, len)

/* Conversion time for one-shot mode (datasheet: max 70ms) */

#define MAX30208_CONV_TIME_MS   80

/* FIFO depth (MAX30208 has 32-sample FIFO) */

#define MAX30208_FIFO_DEPTH     32

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: max30208_i2c_write_reg
 *
 * Description:
 *   Write data to an I2C register.
 *
 ****************************************************************************/

static int max30208_i2c_write_reg(struct max30208_dev *dev,
                                  uint8_t reg,
                                  const uint8_t *data,
                                  uint8_t len)
{
  /* TODO: Implement using NuttX I2C API
   *
   * On NuttX with /dev/i2cN:
   *   struct i2c_msg_s msg[2];
   *   msg[0].addr = dev->config.addr;
   *   msg[0].flags = 0;  // write
   *   msg[0].buffer = &reg;
   *   msg[0].length = 1;
   *   msg[1].addr = dev->config.addr;
   *   msg[1].flags = 0;  // write
   *   msg[1].buffer = (uint8_t *)data;
   *   msg[1].length = len;
   *   struct i2c_transfer_s xfer = { .msgv = msg, .msgc = 2 };
   *   return ioctl(dev->fd, I2CIOC_TRANSFER, &xfer);
   */

  (void)dev;
  (void)reg;
  (void)data;
  (void)len;

  syslog(LOG_DEBUG, "[%s] WRITE reg=0x%02X len=%d\n",
         MAX30208_TAG, reg, len);

  return OK;
}

/****************************************************************************
 * Name: max30208_i2c_read_reg
 *
 * Description:
 *   Read data from an I2C register.
 *
 ****************************************************************************/

static int max30208_i2c_read_reg(struct max30208_dev *dev,
                                 uint8_t reg,
                                 uint8_t *data,
                                 uint8_t len)
{
  /* TODO: Implement using NuttX I2C API
   *
   * On NuttX with /dev/i2cN:
   *   struct i2c_msg_s msg[2];
   *   msg[0].addr = dev->config.addr;
   *   msg[0].flags = 0;  // write (register address)
   *   msg[0].buffer = &reg;
   *   msg[0].length = 1;
   *   msg[1].addr = dev->config.addr;
   *   msg[1].flags = I2C_M_READ;
   *   msg[1].buffer = data;
   *   msg[1].length = len;
   *   struct i2c_transfer_s xfer = { .msgv = msg, .msgc = 2 };
   *   return ioctl(dev->fd, I2CIOC_TRANSFER, &xfer);
   */

  (void)dev;
  (void)reg;
  (void)data;
  (void)len;

  syslog(LOG_DEBUG, "[%s] READ reg=0x%02X len=%d\n",
         MAX30208_TAG, reg, len);

  return OK;
}

/****************************************************************************
 * Name: max30208_wait_conversion
 *
 * Description:
 *   Wait for a one-shot conversion to complete.
 *
 ****************************************************************************/

static int max30208_wait_conversion(struct max30208_dev *dev)
{
  uint8_t status;
  int retries = 20;

  while (retries-- > 0)
    {
      int ret = MAX30208_I2C_READ(dev, MAX30208_REG_STATUS,
                                  &status, 1);
      if (ret < 0)
        {
          return ret;
        }

      if (status & MAX30208_STATUS_TEMP_RDY)
        {
          return OK;
        }

      usleep(MAX30208_CONV_TIME_MS * 1000 / 20);
    }

  syslog(LOG_ERR, "[%s] Conversion timeout\n", MAX30208_TAG);
  return -ETIMEDOUT;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

float max30208_raw_to_celsius(uint16_t raw)
{
  /* The MAX30208 returns a 16-bit unsigned value where:
   *   Temperature (°C) = raw * 0.00390625
   *
   * The sensor uses an unsigned representation:
   *   0x0000 = 0°C
   *   0x8000 = 128°C
   *   Negative temperatures wrap around (two's complement-like)
   *
   * For skin temperature (20°C - 45°C range):
   *   20°C ≈ 0x1400 (5120)
   *   37°C ≈ 0x2500 (9472)
   *   45°C ≈ 0x2D00 (11520)
   */

  float temp = (float)raw * MAX30208_RESOLUTION;

  /* Handle negative temperatures (if MSB indicates negative) */

  if (raw >= 0x8000)
    {
      temp -= 256.0f;  /* Wrap around for negative values */
    }

  return temp;
}

int max30208_init(const struct max30208_config *config,
                  struct max30208_dev *dev)
{
  uint8_t part_id;
  int ret;

  if (!config || !dev)
    {
      return -EINVAL;
    }

  memset(dev, 0, sizeof(*dev));
  memcpy(&dev->config, config, sizeof(*config));

  /* Set default address if not specified */

  if (dev->config.addr == 0)
    {
      dev->config.addr = MAX30208_I2C_ADDR;
    }

  /* Open I2C bus
   *
   * TODO: Replace with actual NuttX I2C open:
   *   char path[16];
   *   snprintf(path, sizeof(path), "/dev/i2c%d", config->i2c_bus);
   *   dev->fd = open(path, O_RDWR);
   *   if (dev->fd < 0) return -errno;
   */

  syslog(LOG_INFO, "[%s] Initializing on I2C%d @ 0x%02X\n",
         MAX30208_TAG, config->i2c_bus, dev->config.addr);

  /* Verify part ID */

  ret = max30208_get_part_id(dev, &part_id);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Failed to read part ID: %d\n",
             MAX30208_TAG, ret);
      return ret;
    }

  if (part_id != MAX30208_PART_ID_VALUE)
    {
      syslog(LOG_ERR, "[%s] Unexpected part ID: 0x%02X (expected 0x%02X)\n",
             MAX30208_TAG, part_id, MAX30208_PART_ID_VALUE);
      return -ENODEV;
    }

  dev->part_id = part_id;
  syslog(LOG_INFO, "[%s] Part ID verified: 0x%02X\n",
         MAX30208_TAG, part_id);

  /* Reset the sensor */

  ret = max30208_reset(dev);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Reset failed: %d\n", MAX30208_TAG, ret);
      return ret;
    }

  /* Configure FIFO: enable roll-over, set almost-full to 15 */

  uint8_t fifo_cfg = MAX30208_FIFO_CFG_ROLL | MAX30208_FIFO_CFG_A_FULL;
  ret = MAX30208_I2C_WRITE(dev, MAX30208_REG_FIFO_CFG,
                           &fifo_cfg, 1);
  if (ret < 0)
    {
      return ret;
    }

  /* Set to one-shot mode by default (lowest power) */

  ret = max30208_set_mode(dev, MAX30208_MODE_ONE_SHOT);
  if (ret < 0)
    {
      return ret;
    }

  dev->initialized = 1;

  syslog(LOG_INFO, "[%s] Initialized successfully\n", MAX30208_TAG);

  return OK;
}

int max30208_read_temp(struct max30208_dev *dev,
                       struct max30208_reading *reading)
{
  uint8_t buf[2];
  uint16_t raw;
  int ret;

  if (!dev || !dev->initialized || !reading)
    {
      return -EINVAL;
    }

  memset(reading, 0, sizeof(*reading));

  /* In one-shot mode, trigger a conversion first */

  uint8_t setup = MAX30208_TEMP_SETUP_CONV;
  ret = MAX30208_I2C_WRITE(dev, MAX30208_REG_TEMP_SETUP, &setup, 1);
  if (ret < 0)
    {
      return ret;
    }

  /* Wait for conversion to complete */

  ret = max30208_wait_conversion(dev);
  if (ret < 0)
    {
      return ret;
    }

  /* Read 2 bytes from FIFO data register (MSB first) */

  ret = MAX30208_I2C_READ(dev, MAX30208_REG_FIFO_DATA, buf, 2);
  if (ret < 0)
    {
      return ret;
    }

  raw = ((uint16_t)buf[0] << 8) | buf[1];

  reading->raw_adc = raw;
  reading->temperature = max30208_raw_to_celsius(raw);
  reading->valid = 1;

  dev->last_temp = reading->temperature;

  syslog(LOG_DEBUG, "[%s] Temp: %.2f°C (raw=0x%04X)\n",
         MAX30208_TAG, reading->temperature, raw);

  return OK;
}

int max30208_set_mode(struct max30208_dev *dev, enum max30208_mode mode)
{
  uint8_t setup = 0;
  uint8_t sys_ctrl;
  int ret;

  if (!dev || !dev->initialized)
    {
      return -EINVAL;
    }

  switch (mode)
    {
      case MAX30208_MODE_SHUTDOWN:
        {
          /* Set shutdown bit in system control */

          ret = MAX30208_I2C_READ(dev, MAX30208_REG_SYS_CTRL,
                                  &sys_ctrl, 1);
          if (ret < 0)
            {
              return ret;
            }

          sys_ctrl |= MAX30208_SYS_CTRL_SHUTDOWN;
          ret = MAX30208_I2C_WRITE(dev, MAX30208_REG_SYS_CTRL,
                                   &sys_ctrl, 1);
        }
        break;

      case MAX30208_MODE_ONE_SHOT:
        {
          /* Clear shutdown, set one-shot trigger */

          ret = MAX30208_I2C_READ(dev, MAX30208_REG_SYS_CTRL,
                                  &sys_ctrl, 1);
          if (ret < 0)
            {
              return ret;
            }

          sys_ctrl &= ~MAX30208_SYS_CTRL_SHUTDOWN;
          ret = MAX30208_I2C_WRITE(dev, MAX30208_REG_SYS_CTRL,
                                   &sys_ctrl, 1);
          if (ret < 0)
            {
              return ret;
            }

          /* One-shot: write CONV bit to TEMP_SETUP */

          setup = MAX30208_TEMP_SETUP_CONV;
          ret = MAX30208_I2C_WRITE(dev, MAX30208_REG_TEMP_SETUP,
                                   &setup, 1);
        }
        break;

      case MAX30208_MODE_CONTINUOUS:
        {
          /* Clear shutdown, enable continuous conversion */

          ret = MAX30208_I2C_READ(dev, MAX30208_REG_SYS_CTRL,
                                  &sys_ctrl, 1);
          if (ret < 0)
            {
              return ret;
            }

          sys_ctrl &= ~MAX30208_SYS_CTRL_SHUTDOWN;
          ret = MAX30208_I2C_WRITE(dev, MAX30208_REG_SYS_CTRL,
                                   &sys_ctrl, 1);
          if (ret < 0)
            {
              return ret;
            }

          /* Continuous: clear CONV bit, rate = 0 */

          setup = 0;
          ret = MAX30208_I2C_WRITE(dev, MAX30208_REG_TEMP_SETUP,
                                   &setup, 1);
        }
        break;

      default:
        return -EINVAL;
    }

  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Failed to set mode %d: %d\n",
             MAX30208_TAG, mode, ret);
      return ret;
    }

  syslog(LOG_INFO, "[%s] Mode set to %d\n", MAX30208_TAG, mode);

  return OK;
}

int max30208_read_fifo(struct max30208_dev *dev,
                       struct max30208_reading *buf, int max,
                       int *count)
{
  uint8_t wr_ptr;
  uint8_t rd_ptr;
  uint8_t raw_buf[2];
  int ret;
  int n = 0;

  if (!dev || !dev->initialized || !buf || !count)
    {
      return -EINVAL;
    }

  *count = 0;

  /* Read FIFO write and read pointers */

  ret = MAX30208_I2C_READ(dev, MAX30208_REG_FIFO_WR_PTR, &wr_ptr, 1);
  if (ret < 0)
    {
      return ret;
    }

  ret = MAX30208_I2C_READ(dev, MAX30208_REG_FIFO_RD_PTR, &rd_ptr, 1);
  if (ret < 0)
    {
      return ret;
    }

  /* Calculate number of samples available */

  int available = (wr_ptr - rd_ptr) & 0x1F;  /* 5-bit pointers */
  if (available == 0)
    {
      return OK;
    }

  if (available > max)
    {
      available = max;
    }

  if (available > MAX30208_FIFO_DEPTH)
    {
      available = MAX30208_FIFO_DEPTH;
    }

  /* Read samples from FIFO */

  for (int i = 0; i < available; i++)
    {
      ret = MAX30208_I2C_READ(dev, MAX30208_REG_FIFO_DATA,
                              raw_buf, 2);
      if (ret < 0)
        {
          break;
        }

      uint16_t raw = ((uint16_t)raw_buf[0] << 8) | raw_buf[1];

      buf[n].raw_adc = raw;
      buf[n].temperature = max30208_raw_to_celsius(raw);
      buf[n].valid = 1;
      n++;
    }

  *count = n;

  syslog(LOG_DEBUG, "[%s] FIFO read: %d samples (available=%d)\n",
         MAX30208_TAG, n, available);

  return OK;
}

int max30208_reset(struct max30208_dev *dev)
{
  uint8_t ctrl = MAX30208_SYS_CTRL_RESET;
  int ret;

  ret = MAX30208_I2C_WRITE(dev, MAX30208_REG_SYS_CTRL, &ctrl, 1);
  if (ret < 0)
    {
      return ret;
    }

  /* Wait for reset to complete (datasheet: ~10ms) */

  usleep(20000);

  return OK;
}

int max30208_get_part_id(struct max30208_dev *dev, uint8_t *part_id)
{
  if (!dev || !part_id)
    {
      return -EINVAL;
    }

  return MAX30208_I2C_READ(dev, MAX30208_REG_PART_ID, part_id, 1);
}

int max30208_enable_interrupts(struct max30208_dev *dev,
                               uint8_t mask, int enable)
{
  uint8_t int_en;
  int ret;

  if (!dev || !dev->initialized)
    {
      return -EINVAL;
    }

  ret = MAX30208_I2C_READ(dev, MAX30208_REG_INT_ENABLE, &int_en, 1);
  if (ret < 0)
    {
      return ret;
    }

  if (enable)
    {
      int_en |= mask;
    }
  else
    {
      int_en &= ~mask;
    }

  return MAX30208_I2C_WRITE(dev, MAX30208_REG_INT_ENABLE, &int_en, 1);
}

int max30208_set_alarm(struct max30208_dev *dev,
                       float high_c, float low_c)
{
  uint8_t buf[2];
  int ret;

  if (!dev || !dev->initialized)
    {
      return -EINVAL;
    }

  /* Convert temperature to raw 16-bit value */

  uint16_t high_raw = (uint16_t)(high_c / MAX30208_RESOLUTION);
  uint16_t low_raw = (uint16_t)(low_c / MAX30208_RESOLUTION);

  /* Write high threshold (MSB, LSB) */

  buf[0] = (high_raw >> 8) & 0xFF;
  buf[1] = high_raw & 0xFF;
  ret = MAX30208_I2C_WRITE(dev, MAX30208_REG_ALARM_HIGH_MSB, buf, 2);
  if (ret < 0)
    {
      return ret;
    }

  /* Write low threshold (MSB, LSB) */

  buf[0] = (low_raw >> 8) & 0xFF;
  buf[1] = low_raw & 0xFF;
  ret = MAX30208_I2C_WRITE(dev, MAX30208_REG_ALARM_LOW_MSB, buf, 2);
  if (ret < 0)
    {
      return ret;
    }

  syslog(LOG_INFO, "[%s] Alarm set: high=%.1f°C low=%.1f°C\n",
         MAX30208_TAG, high_c, low_c);

  return OK;
}

void max30208_close(struct max30208_dev *dev)
{
  if (!dev)
    {
      return;
    }

  /* Put sensor in shutdown mode */

  if (dev->initialized)
    {
      max30208_set_mode(dev, MAX30208_MODE_SHUTDOWN);
    }

  /* Close I2C file descriptor */

  if (dev->fd >= 0)
    {
      close(dev->fd);
      dev->fd = -1;
    }

  dev->initialized = 0;

  syslog(LOG_INFO, "[%s] Closed\n", MAX30208_TAG);
}
