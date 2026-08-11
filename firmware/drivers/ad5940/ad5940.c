/****************************************************************************
 * AD5940 Electrodermal Activity (EDA) Analog Frontend Driver
 *
 * SPI driver for the AD5940 bioimpedance AFE, configured for galvanic
 * skin response (GSR/EDA) measurement in the VelaSense wearable.
 *
 * Datasheet: AD5940 Rev A (Analog Devices)
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
#include <math.h>
#include <sys/ioctl.h>

#include "ad5940.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define AD5940_TAG "ad5940"

/* SPI read/write helpers -- adapt to platform SPI API.
 * On NuttX, use the spi_dev_s interface or /dev/spiN with ioctl.
 * The AD5940 uses 16-bit address + 16-bit data frames.
 */

#define AD5940_SPI_WRITE(dev, addr, data) \
  ad5940_spi_write_reg(dev, addr, data)

#define AD5940_SPI_READ(dev, addr, data) \
  ad5940_spi_read_reg(dev, addr, data)

/* Soft reset delay (datasheet: typ 50 us, max 200 us) */

#define AD5940_RESET_DELAY_US     500

/* Hibernate wakeup delay */

#define AD5940_WAKEUP_DELAY_US    1000

/* FIFO read timeout */

#define AD5940_FIFO_TIMEOUT_MS    50
#define AD5940_FIFO_POLL_US       100

/* Sequencer SRAM size (words) */

#define AD5940_SEQ_SRAM_WORDS     1024

/* Sequencer command encoding helpers */

#define AD5940_SEQCMD_WRITE(addr, data)  ((((uint32_t)(addr) & 0x7FFF) << 16) | \
                                          ((uint32_t)(data) & 0xFFFF))
#define AD5940_SEQCMD_WAIT(cycles)       ((uint32_t)(cycles) & 0xFFFF)
#define AD5940_SEQCMD_STOP()             0x00000003u

/* ADC conversion: raw 16-bit signed to voltage
 *
 * With 16-bit signed mode and 1.82V reference:
 *   V = (raw / 32768) * Vref
 *   V = (raw / 32768) * 1.82
 *
 * TODO: Verify full-scale voltage and sign convention from datasheet.
 *       The AD5940 ADC may use unsigned or bipolar encoding depending
 *       on the measurement mode.
 */

#define AD5940_ADC_VREF           1.82f
#define AD5940_ADC_FS_COUNTS      32768.0f
#define AD5940_RAW_TO_VOLTAGE(raw) (((float)(int16_t)(raw) / AD5940_ADC_FS_COUNTS) * AD5940_ADC_VREF)

/* Timing: get monotonic time in milliseconds */

static uint32_t ad5940_get_time_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/****************************************************************************
 * Private Functions -- SPI Interface
 ****************************************************************************/

/****************************************************************************
 * Name: ad5940_spi_write_reg
 *
 * Description:
 *   Write a 16-bit value to an AD5940 register via SPI.
 *   Frame: [16-bit: 0 + addr(14:0)] [16-bit: data]
 *
 ****************************************************************************/

static int ad5940_spi_write_reg(struct ad5940_dev *dev,
                                uint16_t addr,
                                uint16_t data)
{
  /* TODO: Implement using NuttX SPI API
   *
   * On NuttX with /dev/spiN:
   *   struct spi_dev_s *spi = dev->spi_dev;
   *   SPI_SETFREQUENCY(spi, dev->config.spi_freq);
   *   SPI_SETMODE(spi, SPIDEV_MODE0);
   *   SPI_SETBITS(spi, 16);
   *   SPI_SELECT(spi, SPIDEV_USER(dev->config.cs_pin), true);
   *   SPI_SEND(spi, addr & ~AD5940_SPI_READ_FLAG);
   *   SPI_SEND(spi, data);
   *   SPI_SELECT(spi, SPIDEV_USER(dev->config.cs_pin), false);
   *
   * Alternative with /dev/spiN character device:
   *   struct spi_trans_s xfer;
   *   uint16_t tx[2];
   *   tx[0] = addr & ~AD5940_SPI_READ_FLAG;
   *   tx[1] = data;
   *   xfer.txbuffer = tx;
   *   xfer.rxbuffer = NULL;
   *   xfer.nwords = 2;
   *   ioctl(dev->fd, SPIIOC_EXCHANGE, &xfer);
   */

  (void)dev;
  (void)addr;
  (void)data;

  syslog(LOG_DEBUG, "[%s] WR 0x%04X = 0x%04X\n",
         AD5940_TAG, addr, data);

  return OK;
}

/****************************************************************************
 * Name: ad5940_spi_read_reg
 *
 * Description:
 *   Read a 16-bit value from an AD5940 register via SPI.
 *   Frame: [16-bit: 1 + addr(14:0)] [16-bit: dummy clock for read]
 *
 ****************************************************************************/

static int ad5940_spi_read_reg(struct ad5940_dev *dev,
                               uint16_t addr,
                               uint16_t *data)
{
  /* TODO: Implement using NuttX SPI API
   *
   * On NuttX with /dev/spiN:
   *   struct spi_dev_s *spi = dev->spi_dev;
   *   SPI_SETFREQUENCY(spi, dev->config.spi_freq);
   *   SPI_SETMODE(spi, SPIDEV_MODE0);
   *   SPI_SETBITS(spi, 16);
   *   SPI_SELECT(spi, SPIDEV_USER(dev->config.cs_pin), true);
   *   SPI_SEND(spi, addr | AD5940_SPI_READ_FLAG);
   *   *data = SPI_SEND(spi, 0xFFFF);
   *   SPI_SELECT(spi, SPIDEV_USER(dev->config.cs_pin), false);
   *
   * Alternative with /dev/spiN character device:
   *   struct spi_trans_s xfer;
   *   uint16_t tx[2], rx[2];
   *   tx[0] = addr | AD5940_SPI_READ_FLAG;
   *   tx[1] = 0xFFFF;
   *   xfer.txbuffer = tx;
   *   xfer.rxbuffer = rx;
   *   xfer.nwords = 2;
   *   ioctl(dev->fd, SPIIOC_EXCHANGE, &xfer);
   *   *data = rx[1];
   */

  (void)dev;
  (void)addr;

  /* Return a stub value for compilation */

  if (data)
    {
      *data = 0;
    }

  syslog(LOG_DEBUG, "[%s] RD 0x%04X\n", AD5940_TAG, addr);

  return OK;
}

/****************************************************************************
 * Name: ad5940_spi_read_fifo
 *
 * Description:
 *   Read multiple 32-bit entries from the FIFO data register.
 *   Each FIFO entry is 32 bits wide. The lower 16 bits hold ADC data
 *   and the upper 16 bits hold status/metadata.
 *
 ****************************************************************************/

static int ad5940_spi_read_fifo(struct ad5940_dev *dev,
                                uint32_t *buf, int max, int *count)
{
  uint16_t fifo_count;
  int to_read;
  int i;
  int ret;

  *count = 0;

  /* Read FIFO fill count */

  ret = AD5940_SPI_READ(dev, AD5940_REG_FIFOCOUNT, &fifo_count);
  if (ret < 0)
    {
      return ret;
    }

  if (fifo_count == 0)
    {
      return OK;
    }

  to_read = (fifo_count < max) ? fifo_count : max;

  /* Read FIFO entries
   *
   * TODO: The FIFO read protocol may require reading from FIFODATA
   *       register in a burst. Verify if a single SPI read returns
   *       16 or 32 bits per access. Some AD5940 revisions use a
   *       16-bit FIFO data register (two reads per sample), others
   *       use a 32-bit auto-incrementing read.
   *
   *       For now, assume 16-bit reads: each sample requires two
   *       16-bit reads from FIFODATA (status word + data word).
   */

  for (i = 0; i < to_read; i++)
    {
      uint16_t status;
      uint16_t data;

      ret = AD5940_SPI_READ(dev, AD5940_REG_FIFODATA, &status);
      if (ret < 0)
        {
          break;
        }

      ret = AD5940_SPI_READ(dev, AD5940_REG_FIFODATA, &data);
      if (ret < 0)
        {
          break;
        }

      buf[i] = ((uint32_t)status << 16) | data;
    }

  *count = i;
  return OK;
}

/****************************************************************************
 * Private Functions -- Sequencer Programming
 ****************************************************************************/

/****************************************************************************
 * Name: ad5940_write_seq_mem
 *
 * Description:
 *   Write a word to the sequencer SRAM at the given address.
 *   Uses the MEM_ADDR and MEM_DATA registers.
 *
 ****************************************************************************/

static int ad5940_write_seq_mem(struct ad5940_dev *dev,
                                uint16_t addr,
                                uint32_t data)
{
  int ret;

  /* Set memory address */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_MEM_ADDR, addr);
  if (ret < 0)
    {
      return ret;
    }

  /* Write data (upper 16 bits) */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_MEM_DATA,
                          (uint16_t)(data >> 16));
  if (ret < 0)
    {
      return ret;
    }

  /* Write data (lower 16 bits) */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_MEM_DATA,
                          (uint16_t)(data & 0xFFFF));
  return ret;
}

/****************************************************************************
 * Name: ad5940_program_eda_sequence
 *
 * Description:
 *   Program the sequencer SRAM with commands for DC EDA measurement.
 *   The sequence:
 *     1. Configure ADC and filter for 32 Hz sampling
 *     2. Enable excitation buffer (DC 100 mV)
 *     3. Connect switch matrix to electrodes
 *     4. Trigger ADC conversion
 *     5. Wait for ADC ready
 *     6. Store result to FIFO
 *     7. Loop back to step 3
 *
 ****************************************************************************/

static int ad5940_program_eda_sequence(struct ad5940_dev *dev)
{
  uint16_t addr = AD5940_SEQ_ADDR_INIT;
  int ret;

  syslog(LOG_INFO, "[%s] Programming EDA sequencer\n", AD5940_TAG);

  /* ---- Initialization sequence ---- */

  /* Enable bandgap reference */

  ret = ad5940_write_seq_mem(dev, addr++,
      AD5940_SEQCMD_WRITE(AD5940_REG_AFECON,
                           AD5940_AFECON_ADCREFEN |
                           AD5940_AFECON_EXCBUFEN));
  if (ret < 0) return ret;

  /* Configure ADC: 16-bit, 25 kSPS (close to 32 Hz with oversampling),
   * internal 1.82V reference, gain = 1
   *
   * TODO: The 25 kSPS rate combined with the SINC3 filter at OSR=256
   *       and SINC2 at OSR=8 gives:
   *         Effective rate = 25000 / (256 * 8) = 12.2 Hz
   *       This is below 32 Hz. Adjust OSR values to achieve exactly
   *       32 Hz output rate. For now, use conservative settings and
   *       compensate with the FIFO watermark interrupt rate.
   */

  ret = ad5940_write_seq_mem(dev, addr++,
      AD5940_SEQCMD_WRITE(AD5940_REG_ADCCON,
                           AD5940_ADCCON_16BIT |
                           AD5940_ADCCON_RATE_25K |
                           AD5940_ADCCON_GAIN_1 |
                           AD5940_ADCCON_VREF_1V8));
  if (ret < 0) return ret;

  /* Configure ADC filter: SINC3 OSR=32, SINC2 OSR=4, averager=4
   *
   * Effective output rate calculation:
   *   25000 / (SINC3_OSR * SINC2_OSR * AVG) = 25000 / (32*4*4)
   *   = 25000 / 512 = 48.8 Hz
   *   With FIFO watermark at 1.5 samples, we get ~32 Hz readback.
   *
   * TODO: Fine-tune OSR for exact 32 Hz. This is approximate.
   */

  ret = ad5940_write_seq_mem(dev, addr++,
      AD5940_SEQCMD_WRITE(AD5940_REG_ADCFILTERCON,
                           AD5940_ADCFILTER_SINC3_OSR32 |
                           AD5940_ADCFILTER_SINC2_OSR4 |
                           AD5940_ADCFILTER_AVRGEN |
                           AD5940_ADCFILTER_AVRG_4));
  if (ret < 0) return ret;

  /* Configure TIA: 20 kohm gain for GSR measurement range */

  ret = ad5940_write_seq_mem(dev, addr++,
      AD5940_SEQCMD_WRITE(AD5940_REG_TIA_CON,
                           AD5940_EDA_TIA_GAIN));
  if (ret < 0) return ret;

  /* Configure LP DAC for DC bias (VBIAS = 1.1V)
   *
   * LP DAC is 12-bit, output = VBIAS = (data / 4096) * Vref
   * For 1.1V with 1.82V reference:
   *   data = 1.1 / 1.82 * 4096 = 2479 (0x09AF)
   *
   * TODO: Verify LP DAC transfer function and reference voltage.
   */

  ret = ad5940_write_seq_mem(dev, addr++,
      AD5940_SEQCMD_WRITE(AD5940_REG_LPDACCON, 0x0001));
  if (ret < 0) return ret;

  ret = ad5940_write_seq_mem(dev, addr++,
      AD5940_SEQCMD_WRITE(AD5940_REG_LPDACDAT, 0x09AF));
  if (ret < 0) return ret;

  /* Configure LP TIA servo loop for DC bias stabilization */

  ret = ad5940_write_seq_mem(dev, addr++,
      AD5940_SEQCMD_WRITE(AD5940_REG_LPTIASERVCON,
                           AD5940_LPTIASERVCON_EN));
  if (ret < 0) return ret;

  /* Configure excitation buffer for DC mode
   *
   * For DC GSR: excitation = 100 mV DC across electrodes.
   * The LP DAC provides the DC bias (VBIAS), and the excitation
   * buffer generates the small differential.
   *
   * TODO: Verify excitation buffer configuration for DC bias mode.
   *       The AD5940 excitation buffer may need specific settings
   *       for DC vs. AC excitation.
   */

  ret = ad5940_write_seq_mem(dev, addr++,
      AD5940_SEQCMD_WRITE(AD5940_REG_EXCITBUFCON,
                           AD5940_EXCITBUFCON_EN |
                           AD5940_EXCITBUFCON_BUFPWR));
  if (ret < 0) return ret;

  /* Configure switch matrix for 2-wire GSR measurement
   *
   * Connect: excitation buffer -> drive electrode (D)
   *          TIA -> sense electrode (S)
   *
   * TODO: Verify switch matrix encoding. The exact bit assignments
   *       depend on the specific electrode pin configuration on the
   *       target PCB. This is a best-effort default.
   */

  ret = ad5940_write_seq_mem(dev, addr++,
      AD5940_SEQCMD_WRITE(AD5940_REG_SWCON, 0x0009));
  if (ret < 0) return ret;

  /* Configure FIFO to accept ADC data */

  ret = ad5940_write_seq_mem(dev, addr++,
      AD5940_SEQCMD_WRITE(AD5940_REG_FIFOCFG,
                           AD5940_FIFOCFG_ADC_EN));
  if (ret < 0) return ret;

  /* Set FIFO watermark to 1 (interrupt after 1 sample) */

  ret = ad5940_write_seq_mem(dev, addr++,
      AD5940_SEQCMD_WRITE(AD5940_REG_FIFO_WMARK, 1));
  if (ret < 0) return ret;

  /* ---- Measurement sequence (loop) ---- */

  addr = AD5940_SEQ_ADDR_MEAS;

  /* Enable AFE: ADC + excitation buffer + DAC
   *
   * Full AFECON value for active EDA measurement:
   *   ADC enable | ADC reference | SINC2 | SINC3 | excitation buf | DAC
   */

  ret = ad5940_write_seq_mem(dev, addr++,
      AD5940_SEQCMD_WRITE(AD5940_REG_AFECON,
                           AD5940_AFECON_ADCEN |
                           AD5940_AFECON_ADCREFEN |
                           AD5940_AFECON_SINC2EN |
                           AD5940_AFECON_SINC3EN |
                           AD5940_AFECON_EXCBUFEN |
                           AD5940_AFECON_DACEN |
                           AD5940_AFECON_ADCCONVEN));
  if (ret < 0) return ret;

  /* Wait for ADC conversion complete (loop until interrupt)
   *
   * TODO: Determine proper wait mechanism. The sequencer can poll
   *       a status register or wait for a fixed number of clock cycles.
   *       For now, use a fixed wait of ~31250 cycles at 16 MHz = ~2 ms,
   *       which is much faster than the 31.25 ms sample period.
   */

  /* Wait ~2 ms (at 16 MHz: 32000 cycles) */

  ret = ad5940_write_seq_mem(dev, addr++,
      AD5940_SEQCMD_WAIT(32000));
  if (ret < 0) return ret;

  /* Stop (the sequencer will be re-triggered for each sample by
   * the FIFO watermark or timer interrupt)
   *
   * TODO: Implement auto-repeating sequencer. The AD5940 sequencer
   *       can loop using a branch command. For now, use single-shot
   *       mode and retrigger from the uORB polling loop.
   */

  ret = ad5940_write_seq_mem(dev, addr++,
      AD5940_SEQCMD_STOP());
  if (ret < 0) return ret;

  /* Program sequence start addresses */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_SEQ0ADDR,
                          AD5940_SEQ_ADDR_INIT);
  if (ret < 0) return ret;

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_SEQ1ADDR,
                          AD5940_SEQ_ADDR_MEAS);
  if (ret < 0) return ret;

  syslog(LOG_INFO, "[%s] Sequencer programmed (%d init + %d meas words)\n",
         AD5940_TAG, AD5940_SEQ_ADDR_MEAS - AD5940_SEQ_ADDR_INIT,
         addr - AD5940_SEQ_ADDR_MEAS);

  return OK;
}

/****************************************************************************
 * Private Functions -- SCR Event Detector
 ****************************************************************************/

/****************************************************************************
 * Name: ad5940_scr_update
 *
 * Description:
 *   Update the SCR (skin conductance response) event detector with a
 *   new conductance sample. Detects rapid increases above baseline
 *   that match the signature of an SCR event.
 *
 *   Detection algorithm:
 *     - Baseline: exponential moving average (EMA) of conductance
 *     - SCR onset: conductance rises above baseline + threshold
 *     - SCR peak: conductance stops rising and begins falling
 *     - SCR amplitude: peak - baseline
 *     - Valid SCR: amplitude > 0.05 uS, duration 0.5-4 s
 *
 ****************************************************************************/

static void ad5940_scr_update(struct ad5940_scr_detector *scr,
                              float conductance_us,
                              uint32_t timestamp_ms)
{
  /* Initialize baseline with EMA during first N samples */

  if (!scr->baseline_ready)
    {
      if (scr->baseline_samples == 0)
        {
          scr->baseline = conductance_us;
        }
      else
        {
          scr->baseline += AD5940_SCR_BASELINE_ALPHA *
                           (conductance_us - scr->baseline);
        }

      scr->baseline_samples++;

      if (scr->baseline_samples >= AD5940_EDA_BASELINE_SAMPLES)
        {
          scr->baseline_ready = 1;
          syslog(LOG_INFO, "[ad5940_scr] Baseline established: %.3f uS\n",
                 scr->baseline);
        }

      return;
    }

  /* Update baseline (slow EMA, adapts to tonic changes) */

  scr->baseline += AD5940_SCR_BASELINE_ALPHA *
                   (conductance_us - scr->baseline);

  /* State machine for SCR detection */

  float above_baseline = conductance_us - scr->baseline;

  switch (scr->state)
    {
      case AD5940_SCR_STATE_IDLE:
        {
          /* Watch for rapid conductance increase above threshold */

          if (above_baseline > AD5940_SCR_THRESHOLD_US)
            {
              scr->state = AD5940_SCR_STATE_RISING;
              scr->onset_time = timestamp_ms;
              scr->peak_value = conductance_us;
              scr->peak_amplitude = above_baseline;
              scr->sample_count = 1;

              syslog(LOG_DEBUG, "[ad5940_scr] SCR onset at %u ms "
                     "(%.3f uS above baseline)\n",
                     timestamp_ms, above_baseline);
            }
        }
        break;

      case AD5940_SCR_STATE_RISING:
        {
          scr->sample_count++;

          if (above_baseline > scr->peak_amplitude)
            {
              /* Still rising -- update peak */

              scr->peak_value = conductance_us;
              scr->peak_amplitude = above_baseline;
              scr->peak_time = timestamp_ms;
            }
          else
            {
              /* Conductance stopped rising -- transition to PEAK */

              scr->state = AD5940_SCR_STATE_PEAK;
            }

          /* Timeout check: if rising for too long, reset */

          uint32_t duration = timestamp_ms - scr->onset_time;
          if (duration > AD5940_SCR_MAX_DURATION_MS)
            {
              syslog(LOG_DEBUG, "[ad5940_scr] SCR timeout (rising)\n");
              scr->state = AD5940_SCR_STATE_IDLE;
            }
        }
        break;

      case AD5940_SCR_STATE_PEAK:
        {
          scr->sample_count++;

          if (above_baseline < scr->peak_amplitude * 0.5f)
            {
              /* Falling -- transition to FALLING */

              scr->state = AD5940_SCR_STATE_FALLING;
            }

          uint32_t duration = timestamp_ms - scr->onset_time;
          if (duration > AD5940_SCR_MAX_DURATION_MS)
            {
              scr->state = AD5940_SCR_STATE_IDLE;
            }
        }
        break;

      case AD5940_SCR_STATE_FALLING:
        {
          scr->sample_count++;

          /* Wait for return to near-baseline */

          if (above_baseline < AD5940_SCR_THRESHOLD_US * 0.5f)
            {
              /* SCR event complete -- validate */

              uint32_t duration = timestamp_ms - scr->onset_time;

              if (scr->peak_amplitude >= AD5940_SCR_THRESHOLD_US &&
                  duration >= AD5940_SCR_MIN_DURATION_MS &&
                  duration <= AD5940_SCR_MAX_DURATION_MS)
                {
                  syslog(LOG_INFO, "[ad5940_scr] SCR event: "
                         "amp=%.3f uS, duration=%u ms, peak=%.3f uS\n",
                         scr->peak_amplitude, duration, scr->peak_value);
                  /* Note: The event is marked valid; the caller reads
                   * peak_amplitude to check if a new event occurred */
                }

              scr->state = AD5940_SCR_STATE_IDLE;
            }

          uint32_t duration = timestamp_ms - scr->onset_time;
          if (duration > AD5940_SCR_MAX_DURATION_MS * 2)
            {
              scr->state = AD5940_SCR_STATE_IDLE;
            }
        }
        break;
    }
}

/****************************************************************************
 * Name: ad5940_scr_check_event
 *
 * Description:
 *   Check if an SCR event was just completed. Returns the amplitude
 *   if an event was detected, or 0 otherwise. Resets the event flag.
 *
 ****************************************************************************/

static float ad5940_scr_check_event(struct ad5940_scr_detector *scr)
{
  if (scr->state == AD5940_SCR_STATE_IDLE &&
      scr->peak_amplitude > AD5940_SCR_THRESHOLD_US)
    {
      float amp = scr->peak_amplitude;
      scr->peak_amplitude = 0.0f;
      return amp;
    }

  return 0.0f;
}

/****************************************************************************
 * Private Functions -- AFE Configuration Helpers
 ****************************************************************************/

/****************************************************************************
 * Name: ad5940_config_power
 *
 * Description:
 *   Configure the AD5940 power mode. Enable required oscillators
 *   and power domains for EDA measurement.
 *
 ****************************************************************************/

static int ad5940_config_power(struct ad5940_dev *dev)
{
  int ret;

  /* Enable HF oscillator (16 MHz) and LF oscillator (32 kHz) */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_OSCCON,
                          AD5940_OSCCON_HFOSCEN |
                          AD5940_OSCCON_LFOSCEN);
  if (ret < 0) return ret;

  /* Select HF oscillator as system clock */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_CLKSEL,
                          AD5940_CLKSEL_HFOSC);
  if (ret < 0) return ret;

  /* Enable clock to all blocks */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_CLKNCON,
                          AD5940_CLKNCON_HFOSCEN |
                          AD5940_CLKNCON_LFOSCEN);
  if (ret < 0) return ret;

  /* Set power/bandwidth mode
   *
   * TODO: Determine optimal PMBW setting for EDA measurement.
   *       Lower bandwidth saves power but may affect measurement
   *       accuracy at higher frequencies. For DC GSR, low bandwidth
   *       is acceptable.
   *
   *       Bits [1:0]: 00=low power, 01=normal, 10=high perf
   */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_PMBW, 0x0001);
  if (ret < 0) return ret;

  syslog(LOG_DEBUG, "[%s] Power configuration complete\n", AD5940_TAG);

  return OK;
}

/****************************************************************************
 * Name: ad5940_config_adc
 *
 * Description:
 *   Configure the ADC for 16-bit DC measurement at the EDA sample rate.
 *
 ****************************************************************************/

static int ad5940_config_adc(struct ad5940_dev *dev)
{
  int ret;

  /* ADC control: 16-bit, 25 kSPS, gain=1, 1.82V internal ref */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_ADCCON,
                          AD5940_ADCCON_16BIT |
                          AD5940_ADCCON_RATE_25K |
                          AD5940_ADCCON_GAIN_1 |
                          AD5940_ADCCON_VREF_1V8);
  if (ret < 0) return ret;

  /* ADC filter: SINC3 OSR=32, SINC2 OSR=4, averager=4
   *
   * TODO: Calculate exact effective sample rate:
   *   25000 / (32 * 4 * 4) = 48.8 Hz
   *   Need to adjust for 32 Hz target. Options:
   *   a) Use 12.5 kSPS with SINC3=32, SINC2=4, AVG=2 -> 48.8 Hz
   *   b) Use 25 kSPS with SINC3=64, SINC2=4, AVG=4 -> 24.4 Hz
   *   c) Use 25 kSPS with SINC3=32, SINC2=8, AVG=2 -> 48.8 Hz
   *
   *   The closest to 32 Hz is likely (b) at 24.4 Hz, or we can use
   *   a timer to trigger measurements at exactly 32 Hz.
   */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_ADCFILTERCON,
                          AD5940_ADCFILTER_SINC3_OSR32 |
                          AD5940_ADCFILTER_SINC2_OSR4 |
                          AD5940_ADCFILTER_AVRGEN |
                          AD5940_ADCFILTER_AVRG_4);
  if (ret < 0) return ret;

  /* ADC buffer configuration
   *
   * TODO: Set input buffer bias current and power mode.
   *       For DC measurement, use low-power buffer settings.
   */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_ADCBUFCON, 0x0000);
  if (ret < 0) return ret;

  syslog(LOG_DEBUG, "[%s] ADC configured (16-bit, 1.82V ref)\n",
         AD5940_TAG);

  return OK;
}

/****************************************************************************
 * Name: ad5940_config_tia
 *
 * Description:
 *   Configure the transimpedance amplifier for GSR current measurement.
 *
 ****************************************************************************/

static int ad5940_config_tia(struct ad5940_dev *dev)
{
  int ret;

  /* TIA configuration: 20 kohm gain
   *
   * For GSR measurement:
   *   Current range: ~0.1 uA to ~50 uA (typical skin conductance 1-50 uS)
   *   At 100 mV excitation: I = G * V = 50 uS * 0.1V = 5 uA max
   *   TIA output: 5 uA * 20 kohm = 100 mV (well within ADC range)
   *
   *   At 1 uS: I = 0.1 uA, Vout = 0.1 uA * 20 kohm = 2 mV
   *   ADC resolution at 16-bit, 1.82V ref: 1.82V / 65536 = 27.8 uV
   *   2 mV / 27.8 uV = 72 counts -- adequate resolution
   *
   * TODO: Consider using higher TIA gain (e.g., 40 kohm) for better
   *       low-conductance resolution, at the expense of dynamic range.
   */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_TIA_CON,
                          AD5940_EDA_TIA_GAIN);
  if (ret < 0) return ret;

  /* TIA control 2: power-on, normal operation
   *
   * TODO: Configure TIA power mode and bandwidth.
   */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_TIACON2, 0x0000);
  if (ret < 0) return ret;

  syslog(LOG_DEBUG, "[%s] TIA configured (20 kohm gain)\n", AD5940_TAG);

  return OK;
}

/****************************************************************************
 * Name: ad5940_config_excitation
 *
 * Description:
 *   Configure the excitation generator for DC GSR measurement.
 *   Sets up the LP DAC for DC bias and the excitation buffer.
 *
 ****************************************************************************/

static int ad5940_config_excitation(struct ad5940_dev *dev)
{
  int ret;

  /* Configure LP DAC for DC bias (VBIAS = 1.1V)
   *
   * The LP DAC sets the common-mode voltage at the electrode.
   * For GSR: VBIAS should be mid-rail (~1.1V with 2.2V supply)
   * to allow maximum signal swing.
   *
   * LP DAC: 12-bit, Vout = (data / 4096) * Vref
   * For 1.1V with 1.82V ref: data = 1.1 / 1.82 * 4096 = 2479
   *
   * TODO: Verify LP DAC transfer function. Some AD5940 variants use
   *       6-bit + 12-bit encoding or different reference.
   */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_LPDACCON, 0x0001);
  if (ret < 0) return ret;

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_LPDACDAT, 0x09AF);
  if (ret < 0) return ret;

  /* LP DAC data 2: excitation voltage (100 mV offset from VBIAS)
   *
   * The excitation voltage is the differential applied across the skin.
   * 100 mV is typical for GSR -- high enough for good SNR, low enough
   * to avoid electrode polarization.
   *
   * 100 mV in DAC counts (12-bit, 1.82V ref): 0.1 / 1.82 * 4096 = 225
   *
   * TODO: Verify if LPDACDAT2 controls the excitation amplitude or
   *       if it requires HS DAC configuration. Some modes use the
   *       high-speed DAC for excitation generation.
   */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_LPDACDAT2, 0x00E1);
  if (ret < 0) return ret;

  /* Enable LP TIA with servo loop for bias stabilization */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_LPTIASERVCON,
                          AD5940_LPTIASERVCON_EN);
  if (ret < 0) return ret;

  /* LP mode control: enable LP TIA, VZERO, and DAC */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_LPMODECON,
                          AD5940_LPCON_LPTIAEN |
                          AD5940_LPCON_VZEROEN |
                          AD5940_LPCON_DACEN);
  if (ret < 0) return ret;

  /* Excitation buffer: enable, high-power mode for accuracy */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_EXCITBUFCON,
                          AD5940_EXCITBUFCON_EN |
                          AD5940_EXCITBUFCON_BUFPWR);
  if (ret < 0) return ret;

  /* Excitation control
   *
   * TODO: Configure excitation waveform shape (DC for GSR).
   *       Verify register encoding for DC vs. sine mode.
   */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_EXCITCON, 0x0000);
  if (ret < 0) return ret;

  syslog(LOG_DEBUG, "[%s] Excitation configured (100 mV DC, VBIAS=1.1V)\n",
         AD5940_TAG);

  return OK;
}

/****************************************************************************
 * Name: ad5940_config_switches
 *
 * Description:
 *   Configure the switch matrix for 2-wire GSR measurement.
 *   Connects the excitation buffer to the drive electrode and the
 *   TIA to the sense electrode.
 *
 ****************************************************************************/

static int ad5940_config_switches(struct ad5940_dev *dev)
{
  int ret;

  /* Switch matrix configuration
   *
   * For 2-wire GSR:
   *   Drive path: excitation buffer -> CE (counter electrode)
   *   Sense path: WE (working electrode) -> TIA input
   *
   * TODO: The switch matrix encoding is highly PCB-specific.
   *       The values below assume a standard AD5940 evaluation board
   *       pinout. Adjust for the actual VelaSense board layout.
   *
   * Switch register bit fields (16 bits):
   *   [1:0]   D-switch (drive): 10 = DAC output
   *   [3:2]   SE0-switch:       01 = closed (connect to TIA)
   *   [5:4]   SE1-switch:       00 = open
   *   [7:6]   RE-switch:        00 = open
   *   [9:8]   CE-switch:        10 = DAC output
   *   [11:10] WE-switch:        01 = closed (connect to TIA)
   *   [15:12] Reserved
   */

  /* TODO: This value is a placeholder. Update based on actual PCB. */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_SWCON, 0x0509);
  if (ret < 0) return ret;

  /* D-switch full configuration
   *
   * TODO: Configure the D-switch for DC excitation path.
   *       Verify that DSWFULLCON selects the correct electrode pair.
   */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_DSWFULLCON, 0x0000);
  if (ret < 0) return ret;

  syslog(LOG_DEBUG, "[%s] Switch matrix configured for 2-wire GSR\n",
         AD5940_TAG);

  return OK;
}

/****************************************************************************
 * Name: ad5940_config_interrupts
 *
 * Description:
 *   Configure interrupts for FIFO watermark (data ready).
 *
 ****************************************************************************/

static int ad5940_config_interrupts(struct ad5940_dev *dev)
{
  int ret;

  /* Clear all pending interrupts */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_INTCLR, 0xFFFF);
  if (ret < 0) return ret;

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_INTCCLR, 0xFFFF);
  if (ret < 0) return ret;

  /* Enable FIFO watermark interrupt */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_INTENA,
                          AD5940_INT_FIFO_RDY |
                          AD5940_INT_ADC_RDY);
  if (ret < 0) return ret;

  /* Configure interrupt pin mapping
   *
   * TODO: Configure which interrupt sources map to which GPIO
   *       interrupt output pin. Depends on board wiring.
   */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_INTSEL, 0x0000);
  if (ret < 0) return ret;

  syslog(LOG_DEBUG, "[%s] Interrupts configured (FIFO watermark)\n",
         AD5940_TAG);

  return OK;
}

/****************************************************************************
 * Name: ad5940_config_fifo
 *
 * Description:
 *   Configure the FIFO for ADC data storage.
 *
 ****************************************************************************/

static int ad5940_config_fifo(struct ad5940_dev *dev)
{
  int ret;

  /* Clear FIFO */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_FIFOCON,
                          AD5940_FIFOCON_CLEAR);
  if (ret < 0) return ret;

  /* Configure FIFO: ADC data, 32-bit entries
   *
   * TODO: Verify FIFO entry format. The AD5940 FIFO can store:
   *       - Raw ADC data (16 or 32 bit)
   *       - DFT results (32 bit real + 32 bit imaginary)
   *       - Measurement data (with timestamp)
   *
   *       For DC GSR, raw ADC data is sufficient.
   */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_FIFOCFG,
                          AD5940_FIFOCFG_ADC_EN |
                          AD5940_FIFOCFG_TYPE_ADC);
  if (ret < 0) return ret;

  /* Set watermark to 1 sample (interrupt after each conversion) */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_FIFO_WMARK, 1);
  if (ret < 0) return ret;

  syslog(LOG_DEBUG, "[%s] FIFO configured (watermark=1)\n", AD5940_TAG);

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int ad5940_init(const struct ad5940_config *config,
                struct ad5940_dev *dev)
{
  uint16_t part_id;
  int ret;

  if (!config || !dev)
    {
      return -EINVAL;
    }

  memset(dev, 0, sizeof(*dev));
  memcpy(&dev->config, config, sizeof(*config));
  dev->fd = -1;
  dev->mode = AD5940_MODE_IDLE;

  /* Set default SPI frequency if not specified */

  if (dev->config.spi_freq == 0)
    {
      dev->config.spi_freq = AD5940_SPI_MAX_HZ;
    }

  /* Open SPI bus
   *
   * TODO: Replace with actual NuttX SPI open:
   *   char path[16];
   *   snprintf(path, sizeof(path), "/dev/spi%d", config->spi_bus);
   *   dev->fd = open(path, O_RDWR);
   *   if (dev->fd < 0) return -errno;
   *
   *   struct spi_dev_s *spi = ...;
   *   SPI_SETFREQUENCY(spi, config->spi_freq);
   *   SPI_SETMODE(spi, SPIDEV_MODE0);
   *   SPI_SETBITS(spi, 16);
   */

  syslog(LOG_INFO, "[%s] Initializing on SPI%d @ %u Hz\n",
         AD5940_TAG, config->spi_bus, dev->config.spi_freq);

  /* Hardware reset if reset pin is available */

  if (config->reset_pin >= 0)
    {
      /* TODO: Drive reset pin low for 10 ms, then high.
       *   gpio_set(config->reset_pin, 0);
       *   usleep(10000);
       *   gpio_set(config->reset_pin, 1);
       *   usleep(AD5940_RESET_DELAY_US);
       */
    }

  /* Soft reset via register */

  ret = ad5940_reset(dev);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Reset failed: %d\n", AD5940_TAG, ret);
      return ret;
    }

  /* Verify part ID
   *
   * TODO: The Part ID register address and expected value may vary
   *       by AD5940 variant. The read may return the silicon revision
   *       in AFECON[15:12] instead of a dedicated Part ID register.
   */

  ret = ad5940_read_reg(dev, AD5940_PART_ID_REG, &part_id);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "[%s] Cannot read Part ID register\n",
             AD5940_TAG);
      /* Non-fatal: some variants may not have this register */
    }
  else
    {
      dev->part_id = part_id;
      syslog(LOG_INFO, "[%s] Part ID: 0x%04X\n", AD5940_TAG, part_id);

      /* TODO: Uncomment once Part ID value is confirmed:
       *   if ((part_id & AD5940_PART_ID_MASK) != AD5940_PART_ID_EXPECTED)
       *     {
       *       syslog(LOG_ERR, "[%s] Unexpected Part ID: 0x%04X\n",
       *              AD5940_TAG, part_id);
       *       return -ENODEV;
       *     }
       */
    }

  /* Configure power and clocks */

  ret = ad5940_config_power(dev);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Power config failed: %d\n", AD5940_TAG, ret);
      return ret;
    }

  /* Initialize SCR detector */

  ad5940_reset_scr_detector(dev);

  /* Set default calibration */

  dev->adc_offset = 0;
  dev->adc_gain = 1.0f;

  dev->initialized = 1;

  syslog(LOG_INFO, "[%s] Initialized successfully\n", AD5940_TAG);

  return OK;
}

int ad5940_start_eda(struct ad5940_dev *dev)
{
  int ret;

  if (!dev || !dev->initialized)
    {
      return -EINVAL;
    }

  syslog(LOG_INFO, "[%s] Starting EDA (GSR) measurement\n", AD5940_TAG);

  /* Clear FIFO before starting */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_FIFOCON,
                          AD5940_FIFOCON_CLEAR);
  if (ret < 0) return ret;

  /* Configure ADC for EDA measurement */

  ret = ad5940_config_adc(dev);
  if (ret < 0) return ret;

  /* Configure TIA for GSR current range */

  ret = ad5940_config_tia(dev);
  if (ret < 0) return ret;

  /* Configure excitation for DC GSR */

  ret = ad5940_config_excitation(dev);
  if (ret < 0) return ret;

  /* Configure switch matrix */

  ret = ad5940_config_switches(dev);
  if (ret < 0) return ret;

  /* Configure FIFO */

  ret = ad5940_config_fifo(dev);
  if (ret < 0) return ret;

  /* Configure interrupts */

  ret = ad5940_config_interrupts(dev);
  if (ret < 0) return ret;

  /* Program sequencer for EDA measurement */

  ret = ad5940_program_eda_sequence(dev);
  if (ret < 0) return ret;

  /* Enable AFE: ADC + excitation + reference
   *
   * This starts the continuous measurement cycle. The sequencer
   * will run the init sequence, then the measurement sequence
   * which loops to produce 32 Hz samples.
   */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_AFECON,
                          AD5940_AFECON_ADCEN |
                          AD5940_AFECON_ADCREFEN |
                          AD5940_AFECON_SINC2EN |
                          AD5940_AFECON_SINC3EN |
                          AD5940_AFECON_EXCBUFEN |
                          AD5940_AFECON_DACEN |
                          AD5940_AFECON_ADCCONVEN);
  if (ret < 0) return ret;

  /* Trigger sequencer start */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_SEQCON, 0x0001);
  if (ret < 0) return ret;

  dev->mode = AD5940_MODE_EDA_DC;
  dev->last_sample_ms = ad5940_get_time_ms();
  dev->sample_count = 0;

  syslog(LOG_INFO, "[%s] EDA measurement started\n", AD5940_TAG);

  return OK;
}

int ad5940_read_eda(struct ad5940_dev *dev,
                    struct ad5940_eda_sample *sample)
{
  uint32_t fifo_buf[4];
  int count = 0;
  int ret;
  uint32_t now_ms;

  if (!dev || !dev->initialized || !sample)
    {
      return -EINVAL;
    }

  if (dev->mode != AD5940_MODE_EDA_DC)
    {
      return -EPERM;
    }

  memset(sample, 0, sizeof(*sample));

  /* Read available samples from FIFO */

  ret = ad5940_spi_read_fifo(dev, fifo_buf, 1, &count);
  if (ret < 0)
    {
      return ret;
    }

  if (count == 0)
    {
      /* No data available yet -- not an error, caller should retry */

      return -EAGAIN;
    }

  /* Extract raw ADC value from FIFO entry
   *
   * FIFO entry format (32 bits):
   *   [31:16] = status/metadata (depends on FIFOCFG)
   *   [15:0]  = raw ADC data (16-bit signed)
   *
   * TODO: Verify FIFO entry format from datasheet. Some modes may
   *       pack the data differently.
   */

  uint16_t raw_adc = (uint16_t)(fifo_buf[0] & 0xFFFF);

  /* Apply calibration offset */

  int32_t calibrated = (int32_t)(int16_t)raw_adc - dev->adc_offset;
  if (calibrated > 32767) calibrated = 32767;
  if (calibrated < -32768) calibrated = -32768;

  /* Convert to voltage */

  float voltage = ((float)calibrated / AD5940_ADC_FS_COUNTS) *
                  AD5940_ADC_VREF * dev->adc_gain;

  /* Convert voltage to current through TIA
   *
   * I = V_out / R_TIA
   * I (uA) = V_out / R_TIA * 1e6
   */

  float current_ua = voltage / AD5940_EDA_TIA_GAIN_OHMS * 1e6f;

  /* Convert current to conductance
   *
   * G (S) = I / V_excite
   * G (uS) = I (uA) / V_excite (mV) = current_ua / excite_mv * 1000
   *
   * Simplified: G (uS) = I (uA) / V_excite (V)
   *                       = current_ua / (AD5940_EDA_EXCIT_DC_MV / 1000)
   */

  float v_excite = (float)AD5940_EDA_EXCIT_DC_MV / 1000.0f;
  float conductance_us = current_ua / v_excite;

  /* Clamp to valid range (0 - 100 uS is typical for skin) */

  if (conductance_us < 0.0f)
    {
      conductance_us = 0.0f;
    }

  if (conductance_us > 100.0f)
    {
      conductance_us = 100.0f;
    }

  /* Apply low-pass filter (single-pole IIR)
   *
   * y[n] = alpha * x[n] + (1-alpha) * y[n-1]
   */

  if (dev->sample_count == 0)
    {
      dev->scl_filtered = conductance_us;
    }
  else
    {
      dev->scl_filtered = AD5940_EDA_FILTER_ALPHA * conductance_us +
                          (1.0f - AD5940_EDA_FILTER_ALPHA) *
                          dev->scl_filtered;
    }

  dev->scl_current = conductance_us;

  /* Update SCR detector */

  now_ms = ad5940_get_time_ms();

  ad5940_scr_update(&dev->scr, dev->scl_filtered, now_ms);

  /* Fill output structure */

  sample->raw_adc = raw_adc;
  sample->voltage_v = voltage;
  sample->current_ua = current_ua;
  sample->scl = conductance_us;
  sample->scl_filtered = dev->scl_filtered;

  /* Check for completed SCR event */

  float scr_amp = ad5940_scr_check_event(&dev->scr);
  if (scr_amp > 0.0f)
    {
      sample->scr_event = 1;
      sample->scr_amplitude = scr_amp;
      sample->scr_onset_time = dev->scr.onset_time;
    }
  else
    {
      /* Report ongoing SCR amplitude if in progress */

      if (dev->scr.state != AD5940_SCR_STATE_IDLE)
        {
          sample->scr_amplitude = dev->scr.peak_amplitude;
        }
    }

  sample->valid = 1;

  dev->sample_count++;
  dev->last_sample_ms = now_ms;

  return OK;
}

int ad5940_read_fifo(struct ad5940_dev *dev,
                     uint16_t *buf, int max, int *count)
{
  uint32_t fifo_buf[16];
  int raw_count = 0;
  int ret;
  int i;

  if (!dev || !dev->initialized || !buf || !count)
    {
      return -EINVAL;
    }

  *count = 0;

  /* Read in chunks of up to 16 entries */

  int remaining = max;
  int offset = 0;

  while (remaining > 0)
    {
      int chunk = (remaining < 16) ? remaining : 16;

      ret = ad5940_spi_read_fifo(dev, fifo_buf, chunk, &raw_count);
      if (ret < 0)
        {
          return ret;
        }

      if (raw_count == 0)
        {
          break;
        }

      for (i = 0; i < raw_count; i++)
        {
          buf[offset + i] = (uint16_t)(fifo_buf[i] & 0xFFFF);
        }

      offset += raw_count;
      remaining -= raw_count;

      if (raw_count < chunk)
        {
          break;  /* FIFO drained */
        }
    }

  *count = offset;
  return OK;
}

int ad5940_stop_eda(struct ad5940_dev *dev)
{
  int ret;

  if (!dev || !dev->initialized)
    {
      return -EINVAL;
    }

  syslog(LOG_INFO, "[%s] Stopping EDA measurement\n", AD5940_TAG);

  /* Disable AFE */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_AFECON, 0x0000);
  if (ret < 0) return ret;

  /* Stop sequencer */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_SEQCON, 0x0000);
  if (ret < 0) return ret;

  /* Clear FIFO */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_FIFOCON,
                          AD5940_FIFOCON_CLEAR);
  if (ret < 0) return ret;

  /* Disable excitation buffer */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_EXCITBUFCON, 0x0000);
  if (ret < 0) return ret;

  /* Disable LP TIA */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_LPMODECON, 0x0000);
  if (ret < 0) return ret;

  dev->mode = AD5940_MODE_IDLE;

  syslog(LOG_INFO, "[%s] EDA measurement stopped (%u samples)\n",
         AD5940_TAG, dev->sample_count);

  return OK;
}

int ad5940_hibernate(struct ad5940_dev *dev)
{
  int ret;

  if (!dev || !dev->initialized)
    {
      return -EINVAL;
    }

  /* If currently measuring, stop first */

  if (dev->mode == AD5940_MODE_EDA_DC)
    {
      ret = ad5940_stop_eda(dev);
      if (ret < 0)
        {
          return ret;
        }
    }

  syslog(LOG_INFO, "[%s] Entering hibernate mode\n", AD5940_TAG);

  /* Disable all oscillators except LF (needed for wakeup) */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_OSCCON,
                          AD5940_OSCCON_LFOSCEN);
  if (ret < 0) return ret;

  /* Save SRAM contents if needed
   *
   * TODO: The AD5940 can retain SRAM in hibernate mode if
   *       PWRMOD_SRAMRET is set. Configure this based on
   *       whether we need to preserve sequencer state.
   */

  /* Enter hibernate
   *
   * The LPMODKEY register must be unlocked before writing PWRMOD.
   * Unlock key = 0xA115 (from datasheet).
   *
   * TODO: Verify unlock key value from final datasheet.
   */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_LPMODKEY, 0xA115);
  if (ret < 0) return ret;

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_PWRMOD,
                          AD5940_PWRMOD_HIBERNATE |
                          AD5940_PWRMOD_SRAMRET);
  if (ret < 0) return ret;

  dev->mode = AD5940_MODE_HIBERNATE;

  syslog(LOG_INFO, "[%s] Hibernate mode entered (<5 uA)\n", AD5940_TAG);

  return OK;
}

int ad5940_wakeup(struct ad5940_dev *dev)
{
  int ret;

  if (!dev || !dev->initialized)
    {
      return -EINVAL;
    }

  if (dev->mode != AD5940_MODE_HIBERNATE)
    {
      syslog(LOG_WARNING, "[%s] Not in hibernate, wakeup ignored\n",
             AD5940_TAG);
      return OK;
    }

  syslog(LOG_INFO, "[%s] Waking from hibernate\n", AD5940_TAG);

  /* Exit hibernate by writing to PWRMOD
   *
   * The wakeup is triggered by any SPI transaction. The AD5940
   * will re-enable its internal oscillator and be ready for
   * register access after a brief startup delay.
   */

  /* Unlock and clear hibernate bit */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_LPMODKEY, 0xA115);
  if (ret < 0) return ret;

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_PWRMOD,
                          AD5940_PWRMOD_AWAKE);
  if (ret < 0) return ret;

  /* Wait for oscillator stabilization */

  usleep(AD5940_WAKEUP_DELAY_US);

  /* Re-enable oscillators */

  ret = ad5940_config_power(dev);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Power restore failed after wakeup: %d\n",
             AD5940_TAG, ret);
      return ret;
    }

  dev->mode = AD5940_MODE_IDLE;

  syslog(LOG_INFO, "[%s] Wakeup complete\n", AD5940_TAG);

  return OK;
}

void ad5940_close(struct ad5940_dev *dev)
{
  if (!dev)
    {
      return;
    }

  /* Stop any active measurement */

  if (dev->mode == AD5940_MODE_EDA_DC)
    {
      ad5940_stop_eda(dev);
    }

  /* Wake from hibernate if needed (to cleanly shut down) */

  if (dev->mode == AD5940_MODE_HIBERNATE)
    {
      ad5940_wakeup(dev);
    }

  /* Disable all AFE blocks */

  if (dev->initialized)
    {
      AD5940_SPI_WRITE(dev, AD5940_REG_AFECON, 0x0000);
      AD5940_SPI_WRITE(dev, AD5940_REG_AFECON2, 0x0000);
      AD5940_SPI_WRITE(dev, AD5940_REG_EXCITBUFCON, 0x0000);
      AD5940_SPI_WRITE(dev, AD5940_REG_LPMODECON, 0x0000);

      /* Disable oscillators */

      AD5940_SPI_WRITE(dev, AD5940_REG_OSCCON, 0x0000);
    }

  /* Close SPI file descriptor */

  if (dev->fd >= 0)
    {
      close(dev->fd);
      dev->fd = -1;
    }

  dev->initialized = 0;
  dev->mode = AD5940_MODE_IDLE;

  syslog(LOG_INFO, "[%s] Closed\n", AD5940_TAG);
}

int ad5940_reset(struct ad5940_dev *dev)
{
  int ret;

  if (!dev)
    {
      return -EINVAL;
    }

  /* Unlock reset control register */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_RSTCONKEY,
                          AD5940_RSTKEY_UNLOCK);
  if (ret < 0) return ret;

  /* Trigger soft reset */

  ret = AD5940_SPI_WRITE(dev, AD5940_REG_RSTCONKEY,
                          AD5940_RSTKEY_SOFT_RESET);
  if (ret < 0) return ret;

  /* Wait for reset to complete */

  usleep(AD5940_RESET_DELAY_US);

  /* Verify AFECON is in reset state
   *
   * TODO: After reset, AFECON should read 0x0000 or a known
   *       default value. Verify from datasheet.
   */

  uint16_t afecon;
  ret = ad5940_read_reg(dev, AD5940_REG_AFECON, &afecon);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "[%s] Cannot verify reset (AFECON read failed)\n",
             AD5940_TAG);
    }
  else
    {
      syslog(LOG_DEBUG, "[%s] AFECON after reset: 0x%04X\n",
             AD5940_TAG, afecon);
    }

  syslog(LOG_INFO, "[%s] Soft reset complete\n", AD5940_TAG);

  return OK;
}

int ad5940_read_reg(struct ad5940_dev *dev,
                    uint16_t addr, uint16_t *value)
{
  if (!dev || !value)
    {
      return -EINVAL;
    }

  return AD5940_SPI_READ(dev, addr, value);
}

int ad5940_write_reg(struct ad5940_dev *dev,
                     uint16_t addr, uint16_t value)
{
  if (!dev)
    {
      return -EINVAL;
    }

  return AD5940_SPI_WRITE(dev, addr, value);
}

float ad5940_raw_to_conductance(struct ad5940_dev *dev, uint16_t raw)
{
  if (!dev)
    {
      return 0.0f;
    }

  /* Convert raw ADC to voltage */

  float voltage = ((float)(int16_t)raw / AD5940_ADC_FS_COUNTS) *
                  AD5940_ADC_VREF * dev->adc_gain -
                  ((float)dev->adc_offset / AD5940_ADC_FS_COUNTS) *
                  AD5940_ADC_VREF;

  /* Convert voltage to current through TIA */

  float current_ua = voltage / AD5940_EDA_TIA_GAIN_OHMS * 1e6f;

  /* Convert current to conductance */

  float v_excite = (float)AD5940_EDA_EXCIT_DC_MV / 1000.0f;
  float conductance_us = current_ua / v_excite;

  if (conductance_us < 0.0f)
    {
      conductance_us = 0.0f;
    }

  return conductance_us;
}

const struct ad5940_scr_detector *
ad5940_get_scr_state(const struct ad5940_dev *dev)
{
  if (!dev)
    {
      return NULL;
    }

  return &dev->scr;
}

void ad5940_reset_scr_detector(struct ad5940_dev *dev)
{
  if (!dev)
    {
      return;
    }

  memset(&dev->scr, 0, sizeof(dev->scr));
  dev->scr.state = AD5940_SCR_STATE_IDLE;
  dev->scr.baseline_ready = 0;
}
