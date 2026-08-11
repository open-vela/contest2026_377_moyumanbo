/****************************************************************************
 * firmware/drivers/max86141/max86141.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

/**
 * @file max86141.c
 * @brief MAX86141 Three-Wavelength PPG Sensor Driver Implementation
 *
 * This driver implements full SPI communication with the MAX86141 PPG
 * analog front-end.  It supports:
 *  - SPI register read/write with NuttX SPI API
 *  - FIFO burst reads with 3-byte entry parsing
 *  - LED current programming and SQI-based feedback
 *  - Sample rate and FIFO watermark configuration
 *  - Die temperature reading
 *  - Soft reset and shutdown management
 *
 * The SPI interface uses 7-bit register addresses with bit 7 as the
 * read/write flag (1 = read, 0 = write).  The MAX86141 supports burst
 * (multi-byte) reads and writes for efficient FIFO access.
 *
 * NuttX SPI API usage:
 *   SPI_LOCK(bus, true)           - Acquire the bus
 *   SPI_SETFREQUENCY(bus, freq)   - Set clock rate
 *   SPI_SETMODE(bus, SPIDEV_MODE3) - CPOL=1, CPHA=1
 *   SPI_SELECT(bus, devid, true)  - Assert chip-select
 *   SPI_EXCHANGE(bus, tx, rx, n)  - Transfer n bytes
 *   SPI_SELECT(bus, devid, false) - Deassert chip-select
 *   SPI_LOCK(bus, false)          - Release the bus
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/spi/spi.h>
#include <nuttx/clock.h>
#include <syslog.h>

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "max86141.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* SPI address byte construction: bit 7 = R/W flag */

#define MAX86141_SPI_WRITE(reg)         ((reg) & 0x7f)
#define MAX86141_SPI_READ(reg)          ((reg) | 0x80)

/* Delay after soft reset (ms) */

#define MAX86141_RESET_DELAY_MS         50

/* Max retry count for busy-wait operations */

#define MAX86141_MAX_RETRIES            1000

/* Default ADC range and settling time for wrist PPG */

#define MAX86141_DEFAULT_LED_SETTLING   1       /* 12 us settling time */

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int max86141_spi_lock(struct max86141_dev_s *dev, bool lock);
static int max86141_apply_config(struct max86141_dev_s *dev);
static int max86141_program_led_sequence(struct max86141_dev_s *dev);
static int max86141_wait_reset(struct max86141_dev_s *dev);

/****************************************************************************
 * Private Functions: SPI Transport
 ****************************************************************************/

/**
 * @brief Acquire or release the SPI bus lock.
 *
 * @param[in] dev  Pointer to driver state structure.
 * @param[in] lock true = acquire, false = release.
 * @return 0 on success, negative errno on failure.
 */

static int max86141_spi_lock(struct max86141_dev_s *dev, bool lock)
{
  if (dev->spi == NULL)
    {
      return -ENODEV;
    }

  SPI_LOCK(dev->spi, lock);
  return 0;
}

/**
 * @brief Write a single register over SPI.
 *
 * Protocol:
 *   CS low
 *   Send: [reg_addr | 0x00]  [value]
 *   CS high
 *
 * @param[in] dev  Pointer to driver state structure.
 * @param[in] reg  Register address (7-bit).
 * @param[in] val  Value to write.
 * @return 0 on success, negative errno on failure.
 */

int max86141_write_reg(struct max86141_dev_s *dev,
                       uint8_t reg, uint8_t val)
{
  uint8_t txbuf[2];

  if (dev->spi == NULL)
    {
      return -ENODEV;
    }

  txbuf[0] = MAX86141_SPI_WRITE(reg);
  txbuf[1] = val;

  SPI_SELECT(dev->spi, dev->cfg.spi_devid, true);
  SPI_EXCHANGE(dev->spi, txbuf, NULL, 2);
  SPI_SELECT(dev->spi, dev->cfg.spi_devid, false);

  return 0;
}

/**
 * @brief Read a single register over SPI.
 *
 * Protocol:
 *   CS low
 *   Send: [reg_addr | 0x80]  [dummy]
 *   Receive: [dummy]  [data]
 *   CS high
 *
 * The MAX86141 returns a dummy byte on the first clock cycle after the
 * address, then the register data on the next cycle.
 *
 * @param[in]  dev  Pointer to driver state structure.
 * @param[in]  reg  Register address (7-bit).
 * @param[out] val  Pointer to receive the register value.
 * @return 0 on success, negative errno on failure.
 */

int max86141_read_reg(struct max86141_dev_s *dev,
                      uint8_t reg, uint8_t *val)
{
  uint8_t txbuf[3];
  uint8_t rxbuf[3];

  if (dev->spi == NULL || val == NULL)
    {
      return -EINVAL;
    }

  txbuf[0] = MAX86141_SPI_READ(reg);
  txbuf[1] = 0x00;   /* Dummy byte (for read, device clocks out NOP) */
  txbuf[2] = 0x00;   /* Data byte clocked in on this edge */

  memset(rxbuf, 0, sizeof(rxbuf));
  SPI_SELECT(dev->spi, dev->cfg.spi_devid, true);
  SPI_EXCHANGE(dev->spi, txbuf, rxbuf, 3);
  SPI_SELECT(dev->spi, dev->cfg.spi_devid, false);

  /* Byte 0 = address echo (or dummy), Byte 1 = dummy, Byte 2 = data */

  *val = rxbuf[2];

  return 0;
}

/**
 * @brief Burst-read multiple bytes from consecutive registers.
 *
 * Used for efficient FIFO data retrieval.  The MAX86141 auto-increments
 * the register address during burst reads.
 *
 * Protocol:
 *   CS low
 *   Send: [reg_addr | 0x80]
 *   Clock out: [dummy_byte]  [len bytes of data]
 *   CS high
 *
 * @param[in]  dev  Pointer to driver state structure.
 * @param[in]  reg  Start register address.
 * @param[out] buf  Buffer to receive data.
 * @param[in]  len  Number of data bytes to read (excluding dummy).
 * @return 0 on success, negative errno on failure.
 */

int max86141_read_burst(struct max86141_dev_s *dev,
                        uint8_t reg,
                        uint8_t *buf,
                        uint8_t len)
{
  uint8_t txbuf[1 + 1 + MAX86141_FIFO_DEPTH * MAX86141_FIFO_BYTES_PER_SAMPLE];
  uint8_t rxbuf[1 + 1 + MAX86141_FIFO_DEPTH * MAX86141_FIFO_BYTES_PER_SAMPLE];
  uint8_t total;

  if (dev->spi == NULL || buf == NULL || len == 0)
    {
      return -EINVAL;
    }

  /* Total SPI transfer: 1 address + 1 dummy + len data bytes */

  total = 2 + len;
  if (total > sizeof(txbuf))
    {
      return -ENOSPC;
    }

  memset(txbuf, 0, total);
  memset(rxbuf, 0, total);
  txbuf[0] = MAX86141_SPI_READ(reg);
  /* txbuf[1..] = 0x00 (dummy + data clock-out) */

  SPI_SELECT(dev->spi, dev->cfg.spi_devid, true);
  SPI_EXCHANGE(dev->spi, txbuf, rxbuf, total);
  SPI_SELECT(dev->spi, dev->cfg.spi_devid, false);

  /* Copy data bytes (skip address echo and dummy byte) */

  memcpy(buf, &rxbuf[2], len);

  return 0;
}

/****************************************************************************
 * Private Functions: Device Configuration
 ****************************************************************************/

/**
 * @brief Wait for soft reset to complete.
 *
 * Polls the PART_ID register until the expected value is returned.
 *
 * @param[in] dev  Pointer to driver state structure.
 * @return 0 on success, -ETIMEDOUT if reset did not complete.
 */

static int max86141_wait_reset(struct max86141_dev_s *dev)
{
  uint8_t part_id;
  int retries = 0;

  /* Give the device time to reset */

  usleep(MAX86141_RESET_DELAY_MS * 1000);

  while (retries < MAX86141_MAX_RETRIES)
    {
      if (max86141_read_reg(dev, MAX86141_REG_PART_ID, &part_id) < 0)
        {
          retries++;
          usleep(1000);
          continue;
        }

      if (part_id == MAX86141_PART_ID || part_id == MAX86141_PART_ID_ALT)
        {
          return 0;
        }

      retries++;
      usleep(1000);
    }

  syslog(LOG_ERR, "max86141: reset timeout (retries=%d)\n", retries);
  return -ETIMEDOUT;
}

/**
 * @brief Program the LED sequence registers.
 *
 * Each sequence register controls two phases.  The low nibble is phase
 * N, the high nibble is phase N+1.
 *
 * @param[in] dev  Pointer to driver state structure.
 * @return 0 on success, negative errno on failure.
 */

static int max86141_program_led_sequence(struct max86141_dev_s *dev)
{
  int ret;
  uint8_t seq1;
  uint8_t seq2;

  /* Sequence register 1: Phase 1 (low) + Phase 2 (high) */

  seq1 = (dev->cfg.led_seq[1] << MAX86141_LED_SEQ_HI_SHIFT) |
         (dev->cfg.led_seq[0] << MAX86141_LED_SEQ_LO_SHIFT);

  /* Sequence register 2: Phase 3 (low) + Phase 4 (high) */

  seq2 = (dev->cfg.led_seq[3] << MAX86141_LED_SEQ_HI_SHIFT) |
         (dev->cfg.led_seq[2] << MAX86141_LED_SEQ_LO_SHIFT);

  ret = max86141_write_reg(dev, MAX86141_REG_LED_SEQ1, seq1);
  if (ret < 0)
    {
      return ret;
    }

  ret = max86141_write_reg(dev, MAX86141_REG_LED_SEQ2, seq2);
  if (ret < 0)
    {
      return ret;
    }

  return 0;
}

/**
 * @brief Apply the full driver configuration to the device.
 *
 * Programs all configuration registers in the correct order:
 *   1. FIFO configuration (watermark, rollover)
 *   2. PPG configuration (sample rate, pulse width, averaging, range)
 *   3. LED sequence control
 *   4. LED current (Green, Red, IR)
 *   5. Ambient light cancellation
 *   6. Interrupt enable
 *
 * @param[in] dev  Pointer to driver state structure.
 * @return 0 on success, negative errno on failure.
 */

static int max86141_apply_config(struct max86141_dev_s *dev)
{
  int ret;
  uint8_t val;

  /* ---- FIFO Configuration ---- */

  /* FIFO_CONFIG1: watermark level [5:0] */

  val = dev->cfg.fifo_watermark & MAX86141_FIFO_WMK_MASK;
  ret = max86141_write_reg(dev, MAX86141_REG_FIFO_CONFIG1, val);
  if (ret < 0)
    {
      return ret;
    }

  /* FIFO_CONFIG2: rollover enable, A_FULL level */

  val = 0;
  if (dev->cfg.fifo_rollover)
    {
      val |= MAX86141_FIFO_ROLL_OVER_EN;
    }

  /* Set A_FULL to FIFO_DEPTH - 1 to avoid spurious full interrupts
   * when watermark is the primary trigger */

  val |= (MAX86141_FIFO_DEPTH - 1) & MAX86141_FIFO_A_FULL_MASK;
  ret = max86141_write_reg(dev, MAX86141_REG_FIFO_CONFIG2, val);
  if (ret < 0)
    {
      return ret;
    }

  /* ---- PPG Configuration ---- */

  /* PPG_CONFIG1: ADC range + sample rate */

  val = ((dev->cfg.adc_range << MAX86141_PPG1_ADC_RANGE_SHIFT) &
         MAX86141_PPG1_ADC_RANGE_MASK) |
        (dev->cfg.sample_rate & MAX86141_PPG1_SAMPLE_RATE_MASK);
  ret = max86141_write_reg(dev, MAX86141_REG_PPG_CONFIG1, val);
  if (ret < 0)
    {
      return ret;
    }

  /* PPG_CONFIG2: sample averaging + LED pulse width */

  val = ((dev->cfg.sample_avg << MAX86141_PPG2_SMP_AVG_SHIFT) &
         MAX86141_PPG2_SMP_AVG_MASK) |
        (dev->cfg.pulse_width & MAX86141_PPG2_LED_PW_MASK);
  ret = max86141_write_reg(dev, MAX86141_REG_PPG_CONFIG2, val);
  if (ret < 0)
    {
      return ret;
    }

  /* PPG_CONFIG3: LED settling time, digital filter enable */

  val = (MAX86141_DEFAULT_LED_SETTLING << MAX86141_PPG3_LED_SETLNG_SHIFT) &
        MAX86141_PPG3_LED_SETLNG_MASK;
  ret = max86141_write_reg(dev, MAX86141_REG_PPG_CONFIG3, val);
  if (ret < 0)
    {
      return ret;
    }

  /* ---- LED Sequence ---- */

  ret = max86141_program_led_sequence(dev);
  if (ret < 0)
    {
      return ret;
    }

  /* ---- LED Current ---- */

  ret = max86141_set_led_current(dev, MAX86141_LED_GREEN,
                                 dev->cfg.led_green_mA);
  if (ret < 0)
    {
      return ret;
    }

  ret = max86141_set_led_current(dev, MAX86141_LED_RED,
                                 dev->cfg.led_red_mA);
  if (ret < 0)
    {
      return ret;
    }

  ret = max86141_set_led_current(dev, MAX86141_LED_IR,
                                 dev->cfg.led_ir_mA);
  if (ret < 0)
    {
      return ret;
    }

  /* ---- LED Range ---- */

  /* LED range registers: each LED has a 2-bit range selector.
   * Range 0 = 1x (default, 0-51 mA), Range 1 = 2x (0-102 mA),
   * Range 2 = 4x (0-204 mA).  For typical PPG use, range 0 is fine.
   *
   * LED_RANGE1: bits[1:0] = LED1 range, bits[3:2] = LED2 range
   * LED_RANGE2: bits[5:4] = LED3 range */

  val = 0x00; /* All LEDs at range 0 (0-51 mA per register step) */
  ret = max86141_write_reg(dev, MAX86141_REG_LED_RANGE1, val);
  if (ret < 0)
    {
      return ret;
    }

  val = 0x00;
  ret = max86141_write_reg(dev, MAX86141_REG_LED_RANGE2, val);
  if (ret < 0)
    {
      return ret;
    }

  /* ---- Ambient Light Cancellation ---- */

  /* Enable ALC for ambient light rejection */

  val = MAX86141_ALC_EN;
  ret = max86141_write_reg(dev, MAX86141_REG_ALC_CONFIG, val);
  if (ret < 0)
    {
      return ret;
    }

  /* ---- Interrupt Enable ---- */

  /* Enable FIFO watermark (data ready) and ALC overflow interrupts */

  val = MAX86141_INT_EN_FIFO_DATA_RDY | MAX86141_INT_EN_ALC_OVF;
  ret = max86141_write_reg(dev, MAX86141_REG_INT_ENABLE1, val);
  if (ret < 0)
    {
      return ret;
    }

  val = 0x00;
  ret = max86141_write_reg(dev, MAX86141_REG_INT_ENABLE2, val);
  if (ret < 0)
    {
      return ret;
    }

  /* ---- Digital Filter Configuration ---- */

  /* Enable the low-pass and high-pass filters for PPG signal conditioning.
   * Bits [3:0] select filter cutoff; 0x02 is a reasonable default for
   * heart-rate PPG at 100 Hz (LP ~ 16 Hz). */

  val = 0x02;
  ret = max86141_write_reg(dev, MAX86141_REG_DIG_FILTER_CFG, val);
  if (ret < 0)
    {
      return ret;
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * @brief Initialize the MAX86141 sensor.
 */

int max86141_init(struct max86141_dev_s *dev,
                  const struct max86141_config_s *cfg)
{
  int ret;
  uint8_t part_id;
  uint8_t rev_id;

  if (dev == NULL || cfg == NULL)
    {
      return -EINVAL;
    }

  /* Store configuration */

  memcpy(&dev->cfg, cfg, sizeof(dev->cfg));

  /* Validate configuration */

  if (dev->cfg.fifo_watermark > MAX86141_FIFO_DEPTH)
    {
      dev->cfg.fifo_watermark = MAX86141_FIFO_DEPTH;
    }

  /* Initialize runtime state */

  dev->fifo_wptr = 0;
  dev->fifo_rptr = 0;
  dev->fifo_overflow_cnt = 0;
  dev->irq_pending = false;
  dev->irq_fd = -1;
  dev->sqi = 50;   /* Default SQI estimate */

  /* Initialize LED current tracking */

  dev->led_current[MAX86141_LED_GREEN] = cfg->led_green_mA;
  dev->led_current[MAX86141_LED_RED] = cfg->led_red_mA;
  dev->led_current[MAX86141_LED_IR] = cfg->led_ir_mA;

  /* Acquire SPI bus lock */

  ret = max86141_spi_lock(dev, true);
  if (ret < 0)
    {
      return ret;
    }

  /* Configure SPI bus parameters */

  SPI_SETFREQUENCY(dev->spi, dev->cfg.spi_frequency);
  SPI_SETMODE(dev->spi, SPIDEV_MODE3);  /* CPOL=1, CPHA=1 for MAX86141 */
  SPI_SETBITS(dev->spi, 8);             /* 8-bit transfer width        */
  SPI_HWFEATURES(dev->spi, 0);          /* No HW features needed       */

  /* Perform soft reset */

  ret = max86141_write_reg(dev, MAX86141_REG_SYS_CONTROL,
                           MAX86141_SYS_RESET);
  if (ret < 0)
    {
      syslog(LOG_ERR, "max86141: failed to trigger reset (%d)\n", ret);
      goto err_unlock;
    }

  /* Wait for reset to complete and verify part ID */

  ret = max86141_wait_reset(dev);
  if (ret < 0)
    {
      syslog(LOG_ERR, "max86141: reset did not complete\n");
      goto err_unlock;
    }

  /* Read and verify part ID */

  ret = max86141_read_id(dev, &part_id, &rev_id);
  if (ret < 0)
    {
      syslog(LOG_ERR, "max86141: failed to read part ID\n");
      goto err_unlock;
    }

  if (part_id != MAX86141_PART_ID && part_id != MAX86141_PART_ID_ALT)
    {
      syslog(LOG_ERR, "max86141: unexpected part ID 0x%02x "
             "(expected 0x%02x)\n", part_id, MAX86141_PART_ID);
      ret = -ENODEV;
      goto err_unlock;
    }

  syslog(LOG_INFO, "max86141: found part ID 0x%02x, rev 0x%02x\n",
         part_id, rev_id & MAX86141_REV_ID_MASK);

  /* Apply full configuration */

  ret = max86141_apply_config(dev);
  if (ret < 0)
    {
      syslog(LOG_ERR, "max86141: configuration failed (%d)\n", ret);
      goto err_unlock;
    }

  /* Flush FIFO to start with clean state */

  ret = max86141_flush_fifo(dev);
  if (ret < 0)
    {
      syslog(LOG_ERR, "max86141: FIFO flush failed (%d)\n", ret);
      goto err_unlock;
    }

  /* Enable the sensor (exit shutdown) */

  ret = max86141_write_reg(dev, MAX86141_REG_SYS_CONTROL,
                           MAX86141_SYS_FIFO_EN);
  if (ret < 0)
    {
      syslog(LOG_ERR, "max86141: enable failed (%d)\n", ret);
      goto err_unlock;
    }

  max86141_spi_lock(dev, false);
  syslog(LOG_INFO, "max86141: initialization complete "
         "(SR=%d Hz, WMK=%d, Green=%.1f mA)\n",
         1000 >> dev->cfg.sample_rate,
         dev->cfg.fifo_watermark,
         dev->cfg.led_green_mA);
  return 0;

err_unlock:
  max86141_spi_lock(dev, false);
  return ret;
}

/**
 * @brief Shut down the MAX86141.
 */

int max86141_shutdown(struct max86141_dev_s *dev)
{
  int ret;

  if (dev == NULL)
    {
      return -EINVAL;
    }

  ret = max86141_spi_lock(dev, true);
  if (ret < 0)
    {
      return ret;
    }

  ret = max86141_write_reg(dev, MAX86141_REG_SYS_CONTROL,
                           MAX86141_SYS_SHUTDOWN);
  if (ret < 0)
    {
      syslog(LOG_ERR, "max86141: shutdown write failed (%d)\n", ret);
    }

  max86141_spi_lock(dev, false);
  return ret;
}

/**
 * @brief Read part ID and revision.
 */

int max86141_read_id(struct max86141_dev_s *dev,
                     uint8_t *part_id, uint8_t *rev_id)
{
  int ret;

  if (dev == NULL || part_id == NULL)
    {
      return -EINVAL;
    }

  ret = max86141_read_reg(dev, MAX86141_REG_PART_ID, part_id);
  if (ret < 0)
    {
      return ret;
    }

  if (rev_id != NULL)
    {
      ret = max86141_read_reg(dev, MAX86141_REG_REV_ID, rev_id);
    }

  return ret;
}

/**
 * @brief Soft reset and re-apply configuration.
 */

int max86141_reset(struct max86141_dev_s *dev)
{
  int ret;

  if (dev == NULL)
    {
      return -EINVAL;
    }

  ret = max86141_spi_lock(dev, true);
  if (ret < 0)
    {
      return ret;
    }

  /* Trigger soft reset */

  ret = max86141_write_reg(dev, MAX86141_REG_SYS_CONTROL,
                           MAX86141_SYS_RESET);
  if (ret < 0)
    {
      goto err_unlock;
    }

  /* Wait for reset and verify device is back */

  ret = max86141_wait_reset(dev);
  if (ret < 0)
    {
      goto err_unlock;
    }

  /* Re-apply stored configuration */

  ret = max86141_apply_config(dev);
  if (ret < 0)
    {
      goto err_unlock;
    }

  /* Re-enable the sensor */

  ret = max86141_write_reg(dev, MAX86141_REG_SYS_CONTROL,
                           MAX86141_SYS_FIFO_EN);

err_unlock:
  max86141_spi_lock(dev, false);
  return ret;
}

/**
 * @brief Read all available samples from the FIFO.
 *
 * The FIFO pointer difference gives the number of available samples.
 * Each sample is 3 bytes (tag + 20-bit data).  The burst read is
 * limited to the lesser of available samples and max_count.
 */

int max86141_read_fifo(struct max86141_dev_s *dev,
                       struct max86141_fifo_sample_s *samples,
                       uint8_t max_count,
                       uint8_t *count)
{
  int ret;
  uint8_t wptr;
  uint8_t rptr;
  uint8_t avail;
  uint8_t to_read;
  uint8_t raw_count;
  uint8_t overflow_cnt;
  uint8_t i;

  if (dev == NULL || samples == NULL || count == NULL)
    {
      return -EINVAL;
    }

  *count = 0;

  ret = max86141_spi_lock(dev, true);
  if (ret < 0)
    {
      return ret;
    }

  /* Read FIFO pointers and overflow counter */

  ret = max86141_read_reg(dev, MAX86141_REG_FIFO_WRITE_PTR, &wptr);
  if (ret < 0)
    {
      goto err_unlock;
    }

  ret = max86141_read_reg(dev, MAX86141_REG_FIFO_READ_PTR, &rptr);
  if (ret < 0)
    {
      goto err_unlock;
    }

  ret = max86141_read_reg(dev, MAX86141_REG_FIFO_OVERFLOW, &overflow_cnt);
  if (ret < 0)
    {
      goto err_unlock;
    }

  /* Calculate available samples.
   * If pointers are equal, FIFO is either empty or full (check overflow). */

  if (wptr == rptr)
    {
      if (overflow_cnt > 0)
        {
          /* FIFO is full (32 samples) and overflow has occurred */

          avail = MAX86141_FIFO_DEPTH;
        }
      else
        {
          /* FIFO is empty */

          avail = 0;
        }
    }
  else
    {
      /* Normal case: wptr > rptr (the FIFO is not circular in the
       * MAX86141; pointers wrap at 31 and increment independently) */

      avail = (wptr - rptr) & 0x1f;  /* 5-bit wrap at 32 */
      if (avail == 0)
        {
          avail = MAX86141_FIFO_DEPTH;
        }
    }

  /* Limit to caller buffer size */

  to_read = (avail < max_count) ? avail : max_count;
  raw_count = to_read * MAX86141_FIFO_BYTES_PER_SAMPLE;

  if (to_read == 0)
    {
      dev->fifo_overflow_cnt = overflow_cnt;
      max86141_spi_lock(dev, false);
      return 0;
    }

  /* Burst-read raw FIFO data */

  ret = max86141_read_burst(dev, MAX86141_REG_FIFO_DATA,
                            dev->fifo_buf, raw_count);
  if (ret < 0)
    {
      goto err_unlock;
    }

  /* Parse 3-byte entries into structured samples */

  for (i = 0; i < to_read; i++)
    {
      max86141_parse_fifo_entry(&dev->fifo_buf[i * 3], &samples[i]);
    }

  *count = to_read;

  /* Update internal state */

  dev->fifo_wptr = wptr;
  dev->fifo_rptr = rptr;
  dev->fifo_overflow_cnt = overflow_cnt;

  max86141_spi_lock(dev, false);
  return 0;

err_unlock:
  max86141_spi_lock(dev, false);
  return ret;
}

/**
 * @brief Flush the FIFO.
 */

int max86141_flush_fifo(struct max86141_dev_s *dev)
{
  int ret;
  uint8_t val;

  if (dev == NULL)
    {
      return -EINVAL;
    }

  ret = max86141_spi_lock(dev, true);
  if (ret < 0)
    {
      return ret;
    }

  /* Read current config, set flush bit, write back, wait for self-clear */

  ret = max86141_read_reg(dev, MAX86141_REG_FIFO_CONFIG2, &val);
  if (ret < 0)
    {
      goto err_unlock;
    }

  val |= MAX86141_FIFO_FLUSH;
  ret = max86141_write_reg(dev, MAX86141_REG_FIFO_CONFIG2, val);
  if (ret < 0)
    {
      goto err_unlock;
    }

  /* Wait for flush to complete (self-clearing bit) */

  int retries = 0;
  while (retries < MAX86141_MAX_RETRIES)
    {
      ret = max86141_read_reg(dev, MAX86141_REG_FIFO_CONFIG2, &val);
      if (ret < 0)
        {
          goto err_unlock;
        }

      if ((val & MAX86141_FIFO_FLUSH) == 0)
        {
          break;
        }

      retries++;
      usleep(10);
    }

  if (retries >= MAX86141_MAX_RETRIES)
    {
      ret = -ETIMEDOUT;
      goto err_unlock;
    }

  /* Reset internal pointer tracking */

  dev->fifo_wptr = 0;
  dev->fifo_rptr = 0;
  dev->fifo_overflow_cnt = 0;

  max86141_spi_lock(dev, false);
  return 0;

err_unlock:
  max86141_spi_lock(dev, false);
  return ret;
}

/**
 * @brief Set LED current for a specific channel.
 *
 * The MAX86141 LED current register value maps to:
 *   current_mA = register_value * 0.8
 * So for 20 mA: register = 20 / 0.8 = 25
 */

int max86141_set_led_current(struct max86141_dev_s *dev,
                             enum max86141_led_channel_e channel,
                             float current_mA)
{
  int ret;
  uint8_t reg;
  uint8_t reg_val;

  if (dev == NULL || channel >= MAX86141_LED_COUNT)
    {
      return -EINVAL;
    }

  /* Clamp to valid range */

  if (current_mA < 0.0f)
    {
      current_mA = 0.0f;
    }

  if (current_mA > MAX86141_LED_CURR_MAX_MA)
    {
      current_mA = MAX86141_LED_CURR_MAX_MA;
    }

  /* Convert mA to register value: val = current / 0.8 */

  reg_val = (uint8_t)(current_mA / MAX86141_LED_CURR_MA_PER_LSB + 0.5f);
  if (reg_val > MAX86141_LED_CURR_REG_MAX)
    {
      reg_val = MAX86141_LED_CURR_REG_MAX;
    }

  /* Select register based on channel */

  switch (channel)
    {
      case MAX86141_LED_GREEN:
        reg = MAX86141_REG_LED1_PA;
        break;
      case MAX86141_LED_RED:
        reg = MAX86141_REG_LED2_PA;
        break;
      case MAX86141_LED_IR:
        reg = MAX86141_REG_LED3_PA;
        break;
      default:
        return -EINVAL;
    }

  ret = max86141_write_reg(dev, reg, reg_val);
  if (ret < 0)
    {
      return ret;
    }

  /* Update cached current value */

  dev->led_current[channel] = current_mA;

  return 0;
}

/**
 * @brief Get current LED current for a channel.
 */

int max86141_get_led_current(struct max86141_dev_s *dev,
                             enum max86141_led_channel_e channel,
                             float *current_mA)
{
  if (dev == NULL || current_mA == NULL || channel >= MAX86141_LED_COUNT)
    {
      return -EINVAL;
    }

  *current_mA = dev->led_current[channel];
  return 0;
}

/**
 * @brief Adjust LED current based on SQI.
 *
 * Feedback logic:
 *   SQI < 30: signal too weak -> increase current by 20%
 *   SQI < 50: signal moderate  -> increase current by 10%
 *   SQI > 80: signal very strong -> decrease current by 10%
 *   SQI > 90: signal saturated -> decrease current by 20%
 *   else: maintain current
 */

int max86141_adjust_led_from_sqi(struct max86141_dev_s *dev,
                                 uint8_t sqi,
                                 float min_mA,
                                 float max_mA)
{
  int ret;
  float current_mA;
  float step;
  int i;

  if (dev == NULL || min_mA < 0.0f || max_mA <= min_mA)
    {
      return -EINVAL;
    }

  /* Only adjust the primary (green) LED for wrist PPG */

  current_mA = dev->led_current[MAX86141_LED_GREEN];

  if (sqi < 30)
    {
      step = current_mA * 0.20f;
      if (step < 0.8f)
        {
          step = 0.8f;  /* Minimum step = 1 LSB */
        }

      current_mA += step;
    }
  else if (sqi < 50)
    {
      step = current_mA * 0.10f;
      if (step < 0.8f)
        {
          step = 0.8f;
        }

      current_mA += step;
    }
  else if (sqi > 90)
    {
      step = current_mA * 0.20f;
      current_mA -= step;
    }
  else if (sqi > 80)
    {
      step = current_mA * 0.10f;
      current_mA -= step;
    }

  /* Clamp to range */

  if (current_mA < min_mA)
    {
      current_mA = min_mA;
    }

  if (current_mA > max_mA)
    {
      current_mA = max_mA;
    }

  /* Only write if the value changed significantly (> 1 LSB) */

  if (fabsf(current_mA - dev->led_current[MAX86141_LED_GREEN]) > 0.4f)
    {
      ret = max86141_set_led_current(dev, MAX86141_LED_GREEN, current_mA);
      if (ret < 0)
        {
          return ret;
        }

      /* Also adjust Red and IR proportionally for SpO2 channels */

      for (i = MAX86141_LED_RED; i < MAX86141_LED_COUNT; i++)
        {
          float ratio = (dev->led_current[i] > 0.0f) ?
                        (dev->led_current[i] /
                         (dev->led_current[MAX86141_LED_GREEN] + 0.01f)) :
                        1.0f;

          float new_curr = current_mA * ratio;
          ret = max86141_set_led_current(dev,
                                         (enum max86141_led_channel_e)i,
                                         new_curr);
          if (ret < 0)
            {
              return ret;
            }
        }
    }

  dev->sqi = sqi;
  return 0;
}

/**
 * @brief Set the sample rate.
 */

int max86141_set_sample_rate(struct max86141_dev_s *dev,
                             enum max86141_sample_rate_e rate)
{
  int ret;
  uint8_t val;

  if (dev == NULL)
    {
      return -EINVAL;
    }

  ret = max86141_spi_lock(dev, true);
  if (ret < 0)
    {
      return ret;
    }

  ret = max86141_read_reg(dev, MAX86141_REG_PPG_CONFIG1, &val);
  if (ret < 0)
    {
      goto err_unlock;
    }

  val = (val & ~MAX86141_PPG1_SAMPLE_RATE_MASK) |
        (rate & MAX86141_PPG1_SAMPLE_RATE_MASK);

  ret = max86141_write_reg(dev, MAX86141_REG_PPG_CONFIG1, val);
  if (ret < 0)
    {
      goto err_unlock;
    }

  dev->cfg.sample_rate = rate;

err_unlock:
  max86141_spi_lock(dev, false);
  return ret;
}

/**
 * @brief Set the FIFO watermark.
 */

int max86141_set_fifo_watermark(struct max86141_dev_s *dev,
                                uint8_t watermark)
{
  int ret;

  if (dev == NULL || watermark > MAX86141_FIFO_DEPTH)
    {
      return -EINVAL;
    }

  ret = max86141_spi_lock(dev, true);
  if (ret < 0)
    {
      return ret;
    }

  ret = max86141_write_reg(dev, MAX86141_REG_FIFO_CONFIG1,
                           watermark & MAX86141_FIFO_WMK_MASK);
  if (ret < 0)
    {
      goto err_unlock;
    }

  dev->cfg.fifo_watermark = watermark;

err_unlock:
  max86141_spi_lock(dev, false);
  return ret;
}

/**
 * @brief Enable or disable the sensor.
 */

int max86141_enable(struct max86141_dev_s *dev, bool enable)
{
  int ret;
  uint8_t val;

  if (dev == NULL)
    {
      return -EINVAL;
    }

  ret = max86141_spi_lock(dev, true);
  if (ret < 0)
    {
      return ret;
    }

  if (enable)
    {
      /* Exit shutdown, enable FIFO */

      val = MAX86141_SYS_FIFO_EN;
    }
  else
    {
      /* Enter shutdown */

      val = MAX86141_SYS_SHUTDOWN;
    }

  ret = max86141_write_reg(dev, MAX86141_REG_SYS_CONTROL, val);

  max86141_spi_lock(dev, false);
  return ret;
}

/**
 * @brief Read die temperature.
 *
 * The temperature is a signed 8-bit integer plus a fractional part
 * (0.0625 C per LSB in the fractional register).
 */

int max86141_read_temperature(struct max86141_dev_s *dev, float *temp_c)
{
  int ret;
  uint8_t int_part;
  uint8_t frac_part;

  if (dev == NULL || temp_c == NULL)
    {
      return -EINVAL;
    }

  ret = max86141_spi_lock(dev, true);
  if (ret < 0)
    {
      return ret;
    }

  /* Enable temperature measurement */

  ret = max86141_write_reg(dev, MAX86141_REG_DIE_TEMP_EN,
                           MAX86141_TEMP_EN);
  if (ret < 0)
    {
      goto err_unlock;
    }

  /* Wait for measurement to complete (~30 ms typical) */

  usleep(35000);

  /* Read integer and fractional parts */

  ret = max86141_read_reg(dev, MAX86141_REG_DIE_TEMP_INT, &int_part);
  if (ret < 0)
    {
      goto err_unlock;
    }

  ret = max86141_read_reg(dev, MAX86141_REG_DIE_TEMP_FRAC, &frac_part);
  if (ret < 0)
    {
      goto err_unlock;
    }

  /* Convert: integer is signed 8-bit, fractional is in 0.0625 C steps */

  *temp_c = (float)((int8_t)int_part) +
            (float)(frac_part >> 4) * 0.0625f;

  /* Disable temperature measurement */

  max86141_write_reg(dev, MAX86141_REG_DIE_TEMP_EN, 0);

err_unlock:
  max86141_spi_lock(dev, false);
  return ret;
}

/**
 * @brief Read and clear interrupt status registers.
 */

int max86141_read_int_status(struct max86141_dev_s *dev,
                             uint8_t *status1,
                             uint8_t *status2)
{
  int ret;

  if (dev == NULL || status1 == NULL)
    {
      return -EINVAL;
    }

  ret = max86141_read_reg(dev, MAX86141_REG_INT_STATUS1, status1);
  if (ret < 0)
    {
      return ret;
    }

  if (status2 != NULL)
    {
      ret = max86141_read_reg(dev, MAX86141_REG_INT_STATUS2, status2);
    }

  return ret;
}

/**
 * @brief Parse a raw 3-byte FIFO entry.
 *
 * FIFO data format:
 *   Byte 0: [tag(3)] [data_bits_19_16(4)] [reserved(1)]
 *   Byte 1: [data_bits_15_8]
 *   Byte 2: [data_bits_7_0]
 *
 * Tag values (bits [7:5]):
 *   0x01 = Green 1
 *   0x02 = Green 2
 *   0x03 = Red
 *   0x04 = IR
 *   0x05 = Ambient
 *   0x06 = Green pilot
 *   0x07 = Red pilot
 */

void max86141_parse_fifo_entry(const uint8_t *raw,
                               struct max86141_fifo_sample_s *sample)
{
  uint32_t data;

  /* Extract tag from bits [7:5] of byte 0 */

  sample->tag = (raw[0] >> 5) & 0x07;

  /* Extract 20-bit data:
   *   bits [3:0] of byte 0 = data bits [19:16]
   *   byte 1               = data bits [15:8]
   *   byte 2               = data bits [7:0]  */

  data  = ((uint32_t)(raw[0] & 0x0f)) << 16;  /* bits 19:16 */
  data |= ((uint32_t)raw[1])            << 8;   /* bits 15:8  */
  data |=  (uint32_t)raw[2];                   /* bits 7:0   */

  sample->raw_data = data;
}

/**
 * @brief Compute signal quality index.
 *
 * Uses the coefficient of variation (CV = stddev/mean) of the raw
 * PPG signal amplitude for the target channel.  A well-acquired
 * PPG signal shows a consistent pulsatile waveform; noise or motion
 * artifact increases the CV and reduces SQI.
 *
 * SQI mapping:
 *   CV < 0.05  -> SQI = 90-100 (excellent)
 *   CV < 0.10  -> SQI = 70-89  (good)
 *   CV < 0.20  -> SQI = 50-69  (moderate)
 *   CV < 0.40  -> SQI = 30-49  (poor)
 *   CV >= 0.40 -> SQI = 0-29   (very poor)
 */

uint8_t max86141_compute_sqi(const struct max86141_fifo_sample_s *samples,
                             uint8_t count,
                             uint8_t target_tag)
{
  float sum = 0.0f;
  float sum_sq = 0.0f;
  float mean;
  float variance;
  float cv;
  uint8_t n = 0;
  uint8_t i;

  if (samples == NULL || count == 0)
    {
      return 0;
    }

  /* Compute mean and variance of matching samples */

  for (i = 0; i < count; i++)
    {
      if (samples[i].tag == target_tag)
        {
          float val = (float)samples[i].raw_data;
          sum    += val;
          sum_sq += val * val;
          n++;
        }
    }

  if (n < 2)
    {
      return 50;  /* Insufficient data; return neutral SQI */
    }

  mean = sum / (float)n;

  /* Avoid division by zero for constant (zero) signals */

  if (mean < 1.0f)
    {
      return 0;
    }

  variance = (sum_sq / (float)n) - (mean * mean);
  if (variance < 0.0f)
    {
      variance = 0.0f;  /* Numerical precision guard */
    }

  cv = sqrtf(variance) / mean;

  /* Map CV to SQI (0-100) */

  if (cv < 0.05f)
    {
      return 90 + (uint8_t)((0.05f - cv) / 0.05f * 10.0f);
    }
  else if (cv < 0.10f)
    {
      return 70 + (uint8_t)((0.10f - cv) / 0.05f * 20.0f);
    }
  else if (cv < 0.20f)
    {
      return 50 + (uint8_t)((0.20f - cv) / 0.10f * 20.0f);
    }
  else if (cv < 0.40f)
    {
      return 30 + (uint8_t)((0.40f - cv) / 0.20f * 20.0f);
    }
  else
    {
      uint8_t sqi_raw = (uint8_t)((0.60f - cv) / 0.20f * 30.0f);
      return (cv < 0.60f) ? sqi_raw : 0;
    }
}
