/****************************************************************************
 * firmware/drivers/max86141/max86141.h
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
 * @file max86141.h
 * @brief MAX86141 Three-Wavelength PPG Sensor Driver for VelaSense
 *
 * The MAX86141 is a complete optical pulse oximetry and heart-rate
 * sensor analog front-end (AFE). It operates from a 1.8V power supply
 * and supports up to three LED wavelengths for multi-wavelength PPG.
 *
 * Key features:
 *  - SPI interface (4-wire, up to 10 MHz)
 *  - 3 LED channels (Green / Red / IR)
 *  - 20-bit ADC with programmable sample rate (up to 1024 Hz)
 *  - 32-sample FIFO with programmable watermark
 *  - Programmable LED current (0 - 204 mA)
 *  - Ambient light cancellation
 *  - Low power: ~50 uA typical in normal mode
 *
 * Reference: MAX86141 Datasheet Rev 3, Analog Devices / Maxim Integrated
 */

#ifndef __DRIVERS_MAX86141_H
#define __DRIVERS_MAX86141_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* ---- Device identification ---- */

#define MAX86141_PART_ID                0x35
#define MAX86141_PART_ID_ALT            0x36   /* MAX86141B variant */
#define MAX86141_REV_ID_MASK            0x0f

/* ========================================================================
 * REGISTER MAP
 *
 * All register addresses are 7-bit. Bit 7 of the SPI address byte is the
 * R/W flag (0 = write, 1 = read), handled by the SPI helpers below.
 * ======================================================================== */

/* ---- Status registers (0x00 - 0x01) ---- */

#define MAX86141_REG_PART_ID            0x00   /* R   - Part ID              */
#define MAX86141_REG_REV_ID             0x01   /* R   - Revision ID          */

/* ---- Interrupt / FIFO status (0x02 - 0x03) ---- */

#define MAX86141_REG_INT_STATUS1        0x02   /* R/C - Interrupt status 1   */
#define MAX86141_REG_INT_STATUS2        0x03   /* R/C - Interrupt status 2   */
#define MAX86141_REG_INT_ENABLE1        0x04   /* R/W - Interrupt enable 1   */
#define MAX86141_REG_INT_ENABLE2        0x05   /* R/W - Interrupt enable 2   */

/* Interrupt Status 1 bits */

#define MAX86141_INT_FIFO_FULL          (1 << 7) /* FIFO full                */
#define MAX86141_INT_FIFO_DATA_RDY      (1 << 6) /* FIFO data ready (wmk)   */
#define MAX86141_INT_ALC_OVF            (1 << 5) /* ALC overflow             */
#define MAX86141_INT_PROXY              (1 << 4) /* Proximity detected        */
#define MAX86141_INT_LED_COMPLIANT      (1 << 3) /* LED current compliance   */
#define MAX86141_INT_DIE_TEMP_RDY       (1 << 2) /* Die temperature ready    */
#define MAX86141_INT_POWER_RDY          (1 << 1) /* Power ready (POR done)   */
#define MAX86141_INT_FRAME_RDY          (1 << 0) /* Frame / sample ready     */

/* Interrupt Status 2 bits */

#define MAX86141_INT_SHA_DONE           (1 << 0) /* SHA computation done     */

/* Interrupt Enable 1 bits (mirrors Status 1) */

#define MAX86141_INT_EN_FIFO_FULL       (1 << 7)
#define MAX86141_INT_EN_FIFO_DATA_RDY   (1 << 6)
#define MAX86141_INT_EN_ALC_OVF         (1 << 5)
#define MAX86141_INT_EN_PROXY           (1 << 4)
#define MAX86141_INT_EN_LED_COMPLIANT   (1 << 3)
#define MAX86141_INT_EN_DIE_TEMP_RDY    (1 << 2)
#define MAX86141_INT_EN_POWER_RDY       (1 << 1)
#define MAX86141_INT_EN_FRAME_RDY       (1 << 0)

/* Interrupt Enable 2 bits */

#define MAX86141_INT_EN_SHA_DONE        (1 << 0)

/* ---- FIFO registers (0x06 - 0x0A) ---- */

#define MAX86141_REG_FIFO_WRITE_PTR     0x06   /* R/W - FIFO write pointer  */
#define MAX86141_REG_FIFO_READ_PTR      0x07   /* R/W - FIFO read pointer   */
#define MAX86141_REG_FIFO_OVERFLOW      0x08   /* R   - FIFO overflow count */
#define MAX86141_REG_FIFO_DATA_COUNT    0x09   /* R   - FIFO sample count   */
#define MAX86141_REG_FIFO_DATA          0x0A   /* R   - FIFO data port      */
#define MAX86141_REG_FIFO_CONFIG1       0x0B   /* R/W - FIFO config (wmk)   */
#define MAX86141_REG_FIFO_CONFIG2       0x0C   /* R/W - FIFO config (flush) */

/* FIFO_CONFIG1: watermark = [7:0], range 0-31 (number of samples) */

#define MAX86141_FIFO_WMK_MASK          0x3f   /* Bits [5:0] for watermark  */

/* FIFO_CONFIG2 bits */

#define MAX86141_FIFO_FLUSH             (1 << 0) /* Flush FIFO (self-clr)    */
#define MAX86141_FIFO_ROLL_OVER_EN      (1 << 4) /* Enable FIFO roll-over    */
#define MAX86141_FIFO_A_FULL_MASK       0x0f   /* A_FULL level              */

/* ---- System control (0x0D - 0x0E) ---- */

#define MAX86141_REG_SYS_CONTROL        0x0D   /* R/W - System control      */
#define MAX86141_REG_PPG_CONFIG1        0x0E   /* R/W - PPG configuration 1 */
#define MAX86141_REG_PPG_CONFIG2        0x0F   /* R/W - PPG configuration 2 */
#define MAX86141_REG_PPG_CONFIG3        0x10   /* R/W - PPG configuration 3 */

/* SYS_CONTROL bits */

#define MAX86141_SYS_SHUTDOWN           (1 << 0) /* Soft shutdown            */
#define MAX86141_SYS_RESET             (1 << 1) /* Soft reset (self-clear)   */
#define MAX86141_SYS_LOW_PWR_MODE      (1 << 2) /* Low power mode           */
#define MAX86141_SYS_FIFO_EN           (1 << 3) /* FIFO enable              */

/* PPG_CONFIG1 bits: ADC range, sample rate (see enums below) */

#define MAX86141_PPG1_ADC_RANGE_MASK    0x60   /* ADC range [6:5]           */
#define MAX86141_PPG1_ADC_RANGE_SHIFT   5
#define MAX86141_PPG1_SAMPLE_RATE_MASK  0x1f   /* Sample rate [4:0]         */

/* PPG_CONFIG2 bits: sample average, LED pulse width */

#define MAX86141_PPG2_SMP_AVG_MASK      0x70   /* Sample averaging [6:4]    */
#define MAX86141_PPG2_SMP_AVG_SHIFT     4
#define MAX86141_PPG2_LED_PW_MASK       0x03   /* LED pulse width [1:0]     */

/* PPG_CONFIG3 bits: LED settling, digital filter */

#define MAX86141_PPG3_LED_SETLNG_MASK   0x30   /* LED settling time [5:4]   */
#define MAX86141_PPG3_LED_SETLNG_SHIFT  4

/* ---- LED Sequence / Timing (0x11 - 0x14) ---- */

#define MAX86141_REG_LED_SEQ1           0x11   /* R/W - LED sequence 1      */
#define MAX86141_REG_LED_SEQ2           0x12   /* R/W - LED sequence 2      */
#define MAX86141_REG_LED_SEQ3           0x13   /* R/W - LED sequence 3      */
#define MAX86141_REG_LED_SEQ4           0x14   /* R/W - LED sequence 4      */

/* LED sequence slot assignments (each slot is 4 bits) */

#define MAX86141_LED_SLOT_NONE          0x00
#define MAX86141_LED_SLOT_GREEN1        0x01
#define MAX86141_LED_SLOT_GREEN2        0x02
#define MAX86141_LED_SLOT_RED           0x03
#define MAX86141_LED_SLOT_IR            0x04
#define MAX86141_LED_SLOT_GREEN_PILOT   0x05
#define MAX86141_LED_SLOT_RED_PILOT     0x06
#define MAX86141_LED_SLOT_IR_PILOT      0x07
#define MAX86141_LED_SLOT_AMBIENT       0x08

/* Sequence register packing: each register holds 2 slots */

#define MAX86141_LED_SEQ_LO_SHIFT       0
#define MAX86141_LED_SEQ_HI_SHIFT       4

/* ---- LED current control (0x15 - 0x1A) ---- */

#define MAX86141_REG_LED1_PA            0x15   /* R/W - LED1 (Green) current */
#define MAX86141_REG_LED2_PA            0x16   /* R/W - LED2 (Red) current   */
#define MAX86141_REG_LED3_PA            0x17   /* R/W - LED3 (IR) current    */
#define MAX86141_REG_LED_PILOT_PA       0x18   /* R/W - Pilot LED current    */
#define MAX86141_REG_LED_RANGE1         0x19   /* R/W - LED1/2 range select  */
#define MAX86141_REG_LED_RANGE2         0x1A   /* R/W - LED3 range select    */

/* LED current: 0-255 maps to 0-204 mA (with 100-ohm Rset)
 *   current_mA = register_value * 0.8
 *   register_value = current_mA / 0.8
 */

#define MAX86141_LED_CURR_REG_MAX       255
#define MAX86141_LED_CURR_MA_PER_LSB    0.8f
#define MAX86141_LED_CURR_MAX_MA        204.0f

/* LED range register bits */

#define MAX86141_LED_RANGE1_MASK        0x03   /* LED1 range [1:0]          */
#define MAX86141_LED_RANGE2_MASK        0x0c   /* LED2 range [3:2]          */
#define MAX86141_LED_RANGE3_MASK        0x30   /* LED3 range [5:4]          */

/* ---- Multi-LED mode / Phase control (0x1B - 0x1F) ---- */

#define MAX86141_REG_PHASE1             0x1B   /* R/W - Phase 1 config      */
#define MAX86141_REG_PHASE2             0x1C   /* R/W - Phase 2 config      */
#define MAX86141_REG_PHASE3             0x1D   /* R/W - Phase 3 config      */
#define MAX86141_REG_PHASE4             0x1E   /* R/W - Phase 4 config      */

/* ---- Ambient / ADC configuration (0x20 - 0x2F) ---- */

#define MAX86141_REG_ALC_CONFIG         0x20   /* R/W - ALC configuration   */
#define MAX86141_REG_ALC_CONFIG2        0x21   /* R/W - ALC configuration 2 */
#define MAX86141_REG_ALC_INT_CONFIG     0x22   /* R/W - ALC integrator cfg  */
#define MAX86141_REG_ALC_INT_THRESH     0x23   /* R/W - ALC threshold      */
#define MAX86141_REG_ALC_INT_THRESH2    0x24   /* R/W - ALC threshold 2    */

/* ALC_CONFIG bits */

#define MAX86141_ALC_EN                 (1 << 0) /* ALC enable               */
#define MAX86141_ALC_USE_LED            (1 << 1) /* Use LED for ALC          */

/* ---- Die temperature (0x30 - 0x33) ---- */

#define MAX86141_REG_DIE_TEMP_INT       0x30   /* R   - Temp integer part   */
#define MAX86141_REG_DIE_TEMP_FRAC      0x31   /* R   - Temp fractional     */
#define MAX86141_REG_DIE_TEMP_CONFIG    0x32   /* R/W - Temp config         */
#define MAX86141_REG_DIE_TEMP_EN        0x33   /* R/W - Temp enable         */

#define MAX86141_TEMP_EN                (1 << 0) /* Enable temperature meas  */

/* ---- Digital Filter / Signal Quality (0x34 - 0x3A) ---- */

#define MAX86141_REG_DIG_FILTER_CFG     0x34   /* R/W - Digital filter cfg  */
#define MAX86141_REG_DIG_FILTER_CFG2    0x35   /* R/W - Digital filter cfg2 */
#define MAX86141_REG_SMP_RATE_DELAY     0x36   /* R/W - Sample rate delay   */

/* ---- DAC / Offset cancellation (0x38 - 0x3F) ---- */

#define MAX86141_REG_DAC_OFFSET_LED1    0x38   /* R/W - DAC offset LED1     */
#define MAX86141_REG_DAC_OFFSET_LED2    0x39   /* R/W - DAC offset LED2     */
#define MAX86141_REG_DAC_OFFSET_LED3    0x3A   /* R/W - DAC offset LED3     */

/* ---- Proximity (0x40 - 0x44) ---- */

#define MAX86141_REG_PROX_INT_THRESH    0x40   /* R/W - Proximity threshold */
#define MAX86141_REG_PROX_INT_CONFIG    0x41   /* R/W - Proximity config    */

/* ---- SHA / Security (0x50 - 0x5F) ---- */

#define MAX86141_REG_SHA_CONFIG         0x50   /* R/W - SHA configuration   */

/* ---- FIFO depth constant ---- */

#define MAX86141_FIFO_DEPTH             32     /* Maximum FIFO entries      */

/* ---- FIFO sample sizes ---- */

#define MAX86141_FIFO_BYTES_PER_SAMPLE  3      /* 3 bytes per PPG sample    */

/****************************************************************************
 * Public Data Types
 ****************************************************************************/

/**
 * @brief ADC range setting for PPG.
 *
 * Higher range allows larger signals but reduces effective resolution.
 */

enum max86141_adc_range_e
{
  MAX86141_ADC_RANGE_4096  = 0,  /* +/- 4096 nA  (full dynamic range) */
  MAX86141_ADC_RANGE_8192  = 1,  /* +/- 8192 nA                       */
  MAX86141_ADC_RANGE_16384 = 2,  /* +/- 16384 nA                      */
  MAX86141_ADC_RANGE_32768 = 3,  /* +/- 32768 nA (max dynamic range)  */
};

/**
 * @brief Sample rate in Hz.
 *
 * The actual rate is set by the PPG_CONFIG1 register [4:0].
 */

enum max86141_sample_rate_e
{
  MAX86141_SR_1000HZ = 0x00,     /* 1000 samples/sec (pulse ox)      */
  MAX86141_SR_800HZ  = 0x01,     /*  800 samples/sec                 */
  MAX86141_SR_600HZ  = 0x02,     /*  600 samples/sec                 */
  MAX86141_SR_500HZ  = 0x03,     /*  500 samples/sec                 */
  MAX86141_SR_400HZ  = 0x04,     /*  400 samples/sec                 */
  MAX86141_SR_300HZ  = 0x05,     /*  300 samples/sec                 */
  MAX86141_SR_200HZ  = 0x06,     /*  200 samples/sec                 */
  MAX86141_SR_100HZ  = 0x07,     /*  100 samples/sec (HR default)    */
  MAX86141_SR_50HZ   = 0x08,     /*   50 samples/sec                 */
  MAX86141_SR_25HZ   = 0x09,     /*   25 samples/sec                 */
  MAX86141_SR_10HZ   = 0x0a,     /*   10 samples/sec (low power)     */
  MAX86141_SR_1HZ    = 0x0b,     /*    1 sample/sec                  */
};

/**
 * @brief LED pulse width / ADC resolution.
 *
 * Longer pulse width = more ADC resolution but slower conversion.
 */

enum max86141_pulse_width_e
{
  MAX86141_PW_50US   = 0,   /* 50 us   - 15-bit effective resolution  */
  MAX86141_PW_100US  = 1,   /* 100 us  - 16-bit effective resolution  */
  MAX86141_PW_200US  = 2,   /* 200 us  - 17-bit effective resolution  */
  MAX86141_PW_400US  = 3,   /* 400 us  - 18-bit effective resolution  */
};

/**
 * @brief Sample averaging factor.
 *
 * Multiple samples averaged before FIFO push to reduce noise.
 */

enum max86141_sample_avg_e
{
  MAX86141_AVG_NONE  = 0,  /* No averaging                     */
  MAX86141_AVG_2     = 1,  /* Average 2 samples                */
  MAX86141_AVG_4     = 2,  /* Average 4 samples                */
  MAX86141_AVG_8     = 3,  /* Average 8 samples                */
  MAX86141_AVG_16    = 4,  /* Average 16 samples               */
  MAX86141_AVG_32    = 5,  /* Average 32 samples               */
};

/**
 * @brief LED channel index.
 */

enum max86141_led_channel_e
{
  MAX86141_LED_GREEN = 0,  /* Green LED (primary for wrist PPG) */
  MAX86141_LED_RED   = 1,  /* Red LED  (SpO2)                   */
  MAX86141_LED_IR    = 2,  /* IR LED   (SpO2)                   */
  MAX86141_LED_COUNT = 3,
};

/**
 * @brief FIFO sample tag identifies the LED/channel for each entry.
 *
 * Extracted from bits [7:5] of FIFO byte 0.
 */

enum max86141_fifo_tag_e
{
  MAX86141_TAG_GREEN     = 0x01,  /* Green LED sample              */
  MAX86141_TAG_GREEN2    = 0x02,  /* Green LED 2 (dual green)      */
  MAX86141_TAG_RED       = 0x03,  /* Red LED sample                */
  MAX86141_TAG_IR        = 0x04,  /* IR LED sample                 */
  MAX86141_TAG_AMBIENT   = 0x05,  /* Ambient / dark measurement    */
  MAX86141_TAG_PILOT_G   = 0x06,  /* Green pilot / proximity       */
  MAX86141_TAG_PILOT_R   = 0x07,  /* Red pilot / proximity         */
};

/**
 * @brief Parsed FIFO sample.
 */

struct max86141_fifo_sample_s
{
  uint8_t  tag;        /* LED/channel tag (enum max86141_fifo_tag_e) */
  uint32_t raw_data;   /* 20-bit ADC value (0 .. 1048575)           */
};

/**
 * @brief Driver configuration passed at initialization.
 */

struct max86141_config_s
{
  /* SPI bus parameters */

  uint32_t spi_frequency;       /* SPI clock frequency (Hz), max 10 MHz */
  uint8_t  spi_devid;           /* SPI device ID / chip-select index    */

  /* GPIO interrupt (FIFO watermark) */

  uint8_t  irq_pin;             /* GPIO pin number for interrupt        */
  bool     irq_active_low;      /* true = active-low IRQ line           */

  /* PPG configuration */

  enum max86141_sample_rate_e   sample_rate;     /* ADC sample rate      */
  enum max86141_pulse_width_e   pulse_width;     /* LED pulse width      */
  enum max86141_sample_avg_e    sample_avg;      /* Sample averaging     */
  enum max86141_adc_range_e     adc_range;       /* ADC range            */

  /* FIFO configuration */

  uint8_t  fifo_watermark;      /* FIFO interrupt threshold (0 - 31)    */
  bool     fifo_rollover;       /* Allow FIFO rollover on overflow      */

  /* LED current (initial values in mA, 0 = off) */

  float    led_green_mA;        /* Green LED current, typically 20 mA   */
  float    led_red_mA;          /* Red LED current                      */
  float    led_ir_mA;           /* IR LED current                       */

  /* LED sequence: which LEDs are active and in what order.
   * Up to 4 phases, each phase is a LED slot assignment. */

  uint8_t  led_seq[4];          /* Phase 1-4 LED assignments            */
};

/**
 * @brief Runtime PPG sample published to uORB.
 *
 * This mirrors the uORB sensor_ppgd topic structure.
 */

struct sensor_ppgd_s
{
  uint64_t timestamp;           /* Timestamp in microseconds (CLOCK_MONOTONIC) */
  uint32_t timestamp_sample;    /* Sample timestamp                            */

  /* PPG data per channel */

  float    ppg[3];              /* PPG values: [0]=Green [1]=Red [2]=IR       */
  uint32_t raw[3];              /* Raw 20-bit ADC values                       */
  uint8_t  tag[3];              /* FIFO tags per channel                       */

  /* Quality indicators */

  uint8_t  samples_lost;        /* Number of samples lost (FIFO overflow)      */
  uint8_t  sqi;                 /* Signal quality index (0 - 100)              */

  /* Ambient light cancellation */

  float    ambient;             /* Ambient light level estimate                */

  /* Sensor status */

  bool     overflow;            /* FIFO overflow flag                          */
  bool     alc_overflow;        /* ALC overflow flag                           */
};

/**
 * @brief Driver runtime state.
 */

struct max86141_dev_s
{
  struct spi_dev_s *spi;        /* SPI device handle                          */
  struct max86141_config_s cfg; /* Active configuration                       */

  /* FIFO state */

  uint8_t  fifo_wptr;           /* Last known FIFO write pointer              */
  uint8_t  fifo_rptr;           /* Last known FIFO read pointer               */
  uint8_t  fifo_overflow_cnt;   /* Overflow counter from last read            */

  /* LED current (active, in mA) */

  float    led_current[MAX86141_LED_COUNT];

  /* Interrupt state */

  bool     irq_pending;         /* True when interrupt is being processed     */
  int      irq_fd;              /* File descriptor for IRQ wait               */

  /* SQI tracking */

  uint8_t  sqi;                 /* Last computed signal quality index          */

  /* Buffer for burst FIFO read */

  uint8_t  fifo_buf[MAX86141_FIFO_DEPTH * MAX86141_FIFO_BYTES_PER_SAMPLE];
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the MAX86141 sensor.
 *
 * Performs a soft reset, verifies part ID, and configures all registers
 * per the provided configuration.  Must be called before any other
 * driver function.
 *
 * @param[in] dev   Pointer to driver state structure.
 * @param[in] cfg   Pointer to configuration.
 * @return 0 on success, negative errno on failure.
 */

int max86141_init(struct max86141_dev_s *dev,
                  const struct max86141_config_s *cfg);

/**
 * @brief Shut down the MAX86141 and put it in low-power mode.
 *
 * @param[in] dev   Pointer to driver state structure.
 * @return 0 on success, negative errno on failure.
 */

int max86141_shutdown(struct max86141_dev_s *dev);

/**
 * @brief Read the device part ID and revision.
 *
 * @param[in]  dev      Pointer to driver state structure.
 * @param[out] part_id  Receives the part ID byte.
 * @param[out] rev_id   Receives the revision ID byte (may be NULL).
 * @return 0 on success, negative errno on failure.
 */

int max86141_read_id(struct max86141_dev_s *dev,
                     uint8_t *part_id, uint8_t *rev_id);

/**
 * @brief Perform a soft reset of the MAX86141.
 *
 * Waits for reset to complete (max 50 ms) then re-applies the stored
 * configuration.
 *
 * @param[in] dev   Pointer to driver state structure.
 * @return 0 on success, negative errno on failure.
 */

int max86141_reset(struct max86141_dev_s *dev);

/**
 * @brief Read all available samples from the FIFO.
 *
 * Performs a burst read of the FIFO and parses 3-byte entries into
 * structured sample data.  Updates the internal FIFO pointers and
 * overflow counter.
 *
 * @param[in]  dev       Pointer to driver state structure.
 * @param[out] samples   Array to receive parsed samples (max MAX86141_FIFO_DEPTH).
 * @param[in]  max_count Maximum number of samples to read.
 * @param[out] count     Actual number of samples read.
 * @return 0 on success, negative errno on failure.
 */

int max86141_read_fifo(struct max86141_dev_s *dev,
                       struct max86141_fifo_sample_s *samples,
                       uint8_t max_count,
                       uint8_t *count);

/**
 * @brief Flush the FIFO (reset pointers and clear data).
 *
 * @param[in] dev   Pointer to driver state structure.
 * @return 0 on success, negative errno on failure.
 */

int max86141_flush_fifo(struct max86141_dev_s *dev);

/**
 * @brief Set the LED current for a specific channel.
 *
 * Current is set in mA.  The function converts to the register value
 * and programs the corresponding LED current register.
 *
 * @param[in] dev     Pointer to driver state structure.
 * @param[in] channel LED channel index.
 * @param[in] current_mA Desired current in mA (0 - 204).
 * @return 0 on success, negative errno on failure.
 */

int max86141_set_led_current(struct max86141_dev_s *dev,
                             enum max86141_led_channel_e channel,
                             float current_mA);

/**
 * @brief Get the current LED current for a channel.
 *
 * @param[in]  dev         Pointer to driver state structure.
 * @param[in]  channel     LED channel index.
 * @param[out] current_mA  Receives the current in mA.
 * @return 0 on success, negative errno on failure.
 */

int max86141_get_led_current(struct max86141_dev_s *dev,
                             enum max86141_led_channel_e channel,
                             float *current_mA);

/**
 * @brief Adjust LED current based on signal quality index (SQI).
 *
 * Implements a simple feedback loop: if SQI is low (poor signal),
 * increase current; if SQI is high (signal too strong), decrease.
 * Clamps to the configured minimum and maximum current limits.
 *
 * @param[in] dev         Pointer to driver state structure.
 * @param[in] sqi         Signal quality index (0 - 100).
 * @param[in] min_mA      Minimum allowed current.
 * @param[in] max_mA      Maximum allowed current.
 * @return 0 on success, negative errno on failure.
 */

int max86141_adjust_led_from_sqi(struct max86141_dev_s *dev,
                                 uint8_t sqi,
                                 float min_mA,
                                 float max_mA);

/**
 * @brief Set the sample rate.
 *
 * @param[in] dev   Pointer to driver state structure.
 * @param[in] rate  Desired sample rate (enum max86141_sample_rate_e).
 * @return 0 on success, negative errno on failure.
 */

int max86141_set_sample_rate(struct max86141_dev_s *dev,
                             enum max86141_sample_rate_e rate);

/**
 * @brief Set the FIFO watermark level.
 *
 * @param[in] dev       Pointer to driver state structure.
 * @param[in] watermark Number of samples for FIFO watermark (0-31).
 * @return 0 on success, negative errno on failure.
 */

int max86141_set_fifo_watermark(struct max86141_dev_s *dev,
                                uint8_t watermark);

/**
 * @brief Enable or disable the sensor.
 *
 * When disabled, the sensor enters low-power shutdown mode.
 *
 * @param[in] dev     Pointer to driver state structure.
 * @param[in] enable  true = enable, false = shutdown.
 * @return 0 on success, negative errno on failure.
 */

int max86141_enable(struct max86141_dev_s *dev, bool enable);

/**
 * @brief Read the die temperature.
 *
 * @param[in]  dev      Pointer to driver state structure.
 * @param[out] temp_c   Temperature in degrees Celsius.
 * @return 0 on success, negative errno on failure.
 */

int max86141_read_temperature(struct max86141_dev_s *dev, float *temp_c);

/**
 * @brief Read and clear interrupt status registers.
 *
 * @param[in]  dev       Pointer to driver state structure.
 * @param[out] status1   Value of INT_STATUS1 register.
 * @param[out] status2   Value of INT_STATUS2 register.
 * @return 0 on success, negative errno on failure.
 */

int max86141_read_int_status(struct max86141_dev_s *dev,
                             uint8_t *status1,
                             uint8_t *status2);

/**
 * @brief Compute signal quality index from recent samples.
 *
 * Uses coefficient of variation (CV) of the signal amplitude.
 * A well-acquired PPG signal has a regular pulsatile pattern;
 * SQI increases with signal regularity.
 *
 * @param[in] samples    Array of recent FIFO samples.
 * @param[in] count      Number of samples in array.
 * @param[in] target_tag Only consider samples matching this tag.
 * @return SQI value 0 (poor) - 100 (excellent).
 */

uint8_t max86141_compute_sqi(const struct max86141_fifo_sample_s *samples,
                             uint8_t count,
                             uint8_t target_tag);

/**
 * @brief Low-level SPI write of a single register.
 *
 * @param[in] dev   Pointer to driver state structure.
 * @param[in] reg   Register address.
 * @param[in] val   Value to write.
 * @return 0 on success, negative errno on failure.
 */

int max86141_write_reg(struct max86141_dev_s *dev,
                       uint8_t reg, uint8_t val);

/**
 * @brief Low-level SPI read of a single register.
 *
 * @param[in]  dev   Pointer to driver state structure.
 * @param[in]  reg   Register address.
 * @param[out] val   Pointer to receive the register value.
 * @return 0 on success, negative errno on failure.
 */

int max86141_read_reg(struct max86141_dev_s *dev,
                      uint8_t reg, uint8_t *val);

/**
 * @brief Low-level SPI burst read of multiple registers.
 *
 * Used for efficient FIFO data retrieval.
 *
 * @param[in]  dev   Pointer to driver state structure.
 * @param[in]  reg   Start register address.
 * @param[out] buf   Buffer to receive data.
 * @param[in]  len   Number of bytes to read.
 * @return 0 on success, negative errno on failure.
 */

int max86141_read_burst(struct max86141_dev_s *dev,
                        uint8_t reg,
                        uint8_t *buf,
                        uint8_t len);

/**
 * @brief Parse a raw 3-byte FIFO entry into a structured sample.
 *
 * @param[in]  raw    Pointer to 3 raw bytes from FIFO.
 * @param[out] sample Parsed sample structure.
 */

void max86141_parse_fifo_entry(const uint8_t *raw,
                               struct max86141_fifo_sample_s *sample);

#ifdef __cplusplus
}
#endif

#endif /* __DRIVERS_MAX86141_H */
