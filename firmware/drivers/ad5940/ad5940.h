/****************************************************************************
 * AD5940 Electrodermal Activity (EDA) Analog Frontend Driver
 *
 * SPI interface, 16-bit ADC, programmable excitation generator.
 * Configured for galvanic skin response (GSR/EDA) measurement.
 *
 * Datasheet: AD5940 Rev A (Analog Devices)
 *
 * Key specs:
 *   - Supply: 1.62V - 1.98V (core), 1.71V - 5.5V (IO)
 *   - SPI: up to 16 MHz, CPOL=0 CPHA=0 (Mode 0)
 *   - ADC: 12-bit or 16-bit, up to 800 kSPS
 *   - Excitation: 0.2 Hz - 200 kHz sine, DC bias, PGA
 *   - DSP: hardware DFT, 2048-point FFT
 *   - Power: ~600 uA active, <5 uA hibernate
 *   - Part ID: 0x550X (upper nibble varies by variant)
 *
 * For VelaSense EDA (galvanic skin response):
 *   - DC excitation: 100 mV bias across skin electrodes
 *   - ADC: 16-bit, 32 Hz sampling
 *   - Low-pass filter: 15 Hz cutoff (motion artifact removal)
 *   - Output: SCL (skin conductance level) in uS
 *            SCR (skin conductance response) events
 *
 * Usage:
 *   1. ad5940_init()         -- reset AFE, verify part ID, configure
 *   2. ad5940_start_eda()    -- begin continuous GSR measurement
 *   3. ad5940_read_eda()     -- read one SCL/SCR sample
 *   4. ad5940_hibernate()    -- enter low-power hibernate
 *   5. ad5940_wakeup()       -- resume from hibernate
 *   6. ad5940_close()        -- shutdown and release resources
 *
 ****************************************************************************/

#ifndef __FIRMWARE_DRIVERS_AD5940_AD5940_H
#define __FIRMWARE_DRIVERS_AD5940_AD5940_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions -- SPI Protocol
 ****************************************************************************/

/* AD5940 SPI frame: 16-bit address + 16-bit data (32-bit total transaction)
 * Bit 15 of the first 16-bit word = R/W# (0=write, 1=read)
 * Bits 14:0 = 15-bit register address
 */

#define AD5940_SPI_READ_FLAG        (1u << 15)
#define AD5940_SPI_ADDR_MASK        0x7FFFu

/* Maximum SPI clock frequency */

#define AD5940_SPI_MAX_HZ           16000000

/****************************************************************************
 * Pre-processor Definitions -- Part Identification
 ****************************************************************************/

/* AFECON register (0x0010) lower 12 bits contain silicon revision.
 * Part ID is read from the REVID field of AFECON after reset.
 * Typical value: upper nibble 0x55, lower nibble variant/revision.
 * The exact Part ID register location is at AFECON bits [11:0].
 *
 * TODO: Verify exact Part ID location from final datasheet.
 *       Some revisions use a dedicated PART_ID register at 0x0474.
 */

#define AD5940_PART_ID_REG          0x0474
#define AD5940_PART_ID_MASK         0xFFF0u
#define AD5940_PART_ID_EXPECTED     0x5550u  /* TODO: confirm per variant */

/****************************************************************************
 * Pre-processor Definitions -- Register Map
 *
 * Register addresses are 15-bit. The map below is organized by functional
 * block as described in the datasheet.
 ****************************************************************************/

/* ---- 0x0000-0x00FF: System Control (power, clock, reset) ---- */

#define AD5940_REG_PWRMOD           0x0000  /* Power mode control */
#define AD5940_REG_AFECON           0x0010  /* AFE configuration / ID */
#define AD5940_REG_CLKNCON          0x0014  /* Clock configuration */
#define AD5940_REG_CLKSEL           0x0018  /* Clock source select */
#define AD5940_REG_RSTCONKEY        0x0024  /* Reset control key (unlock) */
#define AD5940_REG_LPMCONKEY        0x0028  /* Low-power mode key (unlock) */
#define AD5940_REG_CALDATLOCK       0x0030  /* Calibration data lock */
#define AD5940_REG_OSCCON           0x0038  /* Oscillator control */
#define AD5940_REG_OSCTRIM          0x003C  /* Oscillator trim */
#define AD5940_REG_HSDACCON         0x0040  /* HS DAC configuration */
#define AD5940_REG_HSDACDAT         0x0044  /* HS DAC data */
#define AD5940_REG_HSDACCFG         0x0048  /* HS DAC gain/offset config */
#define AD5940_REG_LPDACCON         0x0050  /* LP DAC configuration */
#define AD5940_REG_LPDACDAT         0x0054  /* LP DAC data */
#define AD5940_REG_LPDACCFG         0x0058  /* LP DAC gain/offset config */
#define AD5940_REG_LPMODKEY         0x0060  /* Low-power mode unlock key */

/* ---- 0x0100-0x01FF: AFE Configuration (excitation, ADC, switches) ---- */

#define AD5940_REG_AFECON2          0x0100  /* AFE control 2 */
#define AD5940_REG_ADCFILTERCON     0x0104  /* ADC filter configuration */
#define AD5940_REG_ADCDAT           0x0108  /* ADC data register */
#define AD5940_REG_ADCCON           0x010C  /* ADC control */
#define AD5940_REG_ADCDATSTAT       0x0110  /* ADC status */
#define AD5940_REG_ADCCON2          0x0114  /* ADC control 2 */
#define AD5940_REG_ADCBUFCON        0x0118  /* ADC buffer configuration */
#define AD5940_REG_SWCON            0x0120  /* Switch matrix control */
#define AD5940_REG_SWSTATUS         0x0124  /* Switch matrix status */
#define AD5940_REG_TIA_CON          0x0130  /* TIA (transimpedance amp) cfg */
#define AD5940_REG_TIACON2          0x0134  /* TIA configuration 2 */
#define AD5940_REG_LPTIASERVCON     0x0138  /* LP TIA servo loop config */
#define AD5940_REG_LPTIA_CON        0x013C  /* LP TIA configuration */
#define AD5940_REG_LPMODECON        0x0140  /* LP mode control */
#define AD5940_REG_LPCON            0x0144  /* LP control */
#define AD5940_REG_EXCITBUFCON      0x0148  /* Excitation buffer config */
#define AD5940_REG_EXCITCON         0x014C  /* Excitation control */
#define AD5940_REG_AFECON3          0x0150  /* AFE control 3 */
#define AD5940_REG_DFTCON           0x0154  /* DFT control */
#define AD5940_REG_DFTREAL          0x0158  /* DFT real result */
#define AD5940_REG_DFTIMAG          0x015C  /* DFT imaginary result */
#define AD5940_REG_DFTSCONFIG       0x0160  /* DFT source select config */
#define AD5940_REG_HPTIACON         0x0164  /* HP TIA configuration */
#define AD5940_REG_TEMPSENS         0x0170  /* Temperature sensor config */
#define AD5940_REG_LPDACDAT2        0x0180  /* LP DAC data 2 */
#define AD5940_REG_DSWFULLCON       0x0190  /* D switch full config */
#define AD5940_REG_DSWCON           0x0194  /* D switch config */
#define AD5940_REG_SWTRIG           0x0198  /* Switch trigger */
#define AD5940_REG_PMBW             0x01A0  /* Power/bandwidth mode */
#define AD5940_REG_AFEWDTCON        0x01B0  /* AFE watchdog config */

/* ---- 0x0200-0x02FF: Sequencer / DFT Control ---- */

#define AD5940_REG_SEQCON           0x0200  /* Sequencer control */
#define AD5940_REG_SEQCOUNT         0x0204  /* Sequencer counter */
#define AD5940_REG_SEQTRG           0x0208  /* Sequencer trigger */
#define AD5940_REG_SEQTIMEOUT       0x020C  /* Sequencer timeout */
#define AD5940_REG_SEQCRC           0x0210  /* Sequencer CRC */
#define AD5940_REG_SEQ0ADDR         0x0220  /* Sequence 0 start address */
#define AD5940_REG_SEQ1ADDR         0x0224  /* Sequence 1 start address */
#define AD5940_REG_SEQ2ADDR         0x0228  /* Sequence 2 start address */
#define AD5940_REG_SEQ3ADDR         0x022C  /* Sequence 3 start address */
#define AD5940_REG_SEQ4ADDR         0x0230  /* Sequence 4 start address */
#define AD5940_REG_SEQ5ADDR         0x0234  /* Sequence 5 start address */
#define AD5940_REG_SEQ6ADDR         0x0238  /* Sequence 6 start address */
#define AD5940_REG_SEQ7ADDR         0x023C  /* Sequence 7 start address */
#define AD5940_REG_MEM_ADDR         0x0240  /* Sequencer memory address */
#define AD5940_REG_MEM_DATA         0x0244  /* Sequencer memory data */
#define AD5940_REG_MEMCON           0x0248  /* Sequencer memory control */

/* ---- 0x0300-0x03FF: FIFO Control ---- */

#define AD5940_REG_FIFOCOUNT        0x0300  /* FIFO fill count */
#define AD5940_REG_FIFOCFG          0x0304  /* FIFO configuration */
#define AD5940_REG_FIFOCON          0x0308  /* FIFO control (read/clear) */
#define AD5940_REG_FIFODATA         0x030C  /* FIFO data read port */
#define AD5940_REG_FIFOSTA          0x0310  /* FIFO status */
#define AD5940_REG_FIFO_WMARK       0x0314  /* FIFO watermark level */
#define AD5940_REG_FIFO_DFT_DATA    0x0320  /* FIFO DFT result data */

/* ---- 0x0400-0x04FF: Interrupt Control ---- */

#define AD5940_REG_INTCSEL          0x0400  /* Interrupt C select */
#define AD5940_REG_INTCSRC          0x0404  /* Interrupt C source */
#define AD5940_REG_INTCCLR          0x0408  /* Interrupt C clear */
#define AD5940_REG_INTCFLAG         0x040C  /* Interrupt C flag */
#define AD5940_REG_INTSEL           0x0410  /* Interrupt select */
#define AD5940_REG_INTSRC           0x0414  /* Interrupt source */
#define AD5940_REG_INTCLR           0x0418  /* Interrupt clear */
#define AD5940_REG_INTFLAG          0x041C  /* Interrupt flag */
#define AD5940_REG_INTENA           0x0420  /* Interrupt enable A */
#define AD5940_REG_INTENB           0x0424  /* Interrupt enable B */
#define AD5940_REG_INTCSEL2         0x0428  /* Interrupt C select 2 */
#define AD5940_REG_INTCSET          0x042C  /* Interrupt C set */

/* ---- 0x0500-0x05FF: GPIO / Control Registers ---- */

#define AD5940_REG_GPIOPINSEL       0x0500  /* GPIO pin select */
#define AD5940_REG_GPIOMUX          0x0504  /* GPIO mux */
#define AD5940_REG_GPIODIR          0x0508  /* GPIO direction */
#define AD5940_REG_GPIODOUT         0x050C  /* GPIO data out */
#define AD5940_REG_GPIODIN          0x0510  /* GPIO data in */
#define AD5940_REG_GPIOSET          0x0514  /* GPIO set */
#define AD5940_REG_GPIOCLR          0x0518  /* GPIO clear */
#define AD5940_REG_GPIOTOG          0x051C  /* GPIO toggle */
#define AD5940_REG_GPIOPEN          0x0520  /* GPIO pull enable */
#define AD5940_REG_GPIOPLD          0x0524  /* GPIO polarity */

/****************************************************************************
 * Pre-processor Definitions -- Register Bit Fields
 ****************************************************************************/

/* PWRMOD (0x0000) -- Power mode control */

#define AD5940_PWRMOD_AWAKE         (0u << 0)  /* Full power mode */
#define AD5940_PWRMOD_HIBERNATE     (1u << 0)  /* Hibernate mode */
#define AD5940_PWRMOD_SRAMRET       (1u << 1)  /* Retain SRAM in hibernate */
#define AD5940_PWRMOD_BANDGAPEN     (1u << 4)  /* Bandgap enable */

/* AFECON (0x0010) -- AFE main control */

#define AD5940_AFECON_ADCEN         (1u << 0)  /* ADC enable */
#define AD5940_AFECON_ADCREFEN      (1u << 1)  /* ADC reference enable */
#define AD5940_AFECON_SINC2EN       (1u << 2)  /* SINC2 filter enable */
#define AD5940_AFECON_SINC3EN       (1u << 3)  /* SINC3 filter enable */
#define AD5940_AFECON_DFTEN         (1u << 4)  /* DFT engine enable */
#define AD5940_AFECON_EXCBUFEN      (1u << 5)  /* Excitation buffer enable */
#define AD5940_AFECON_INAMPEN       (1u << 6)  /* Instrumentation amp enable */
#define AD5940_AFECON_TEMPCONVEN    (1u << 7)  /* Temp sensor conversion */
#define AD5940_AFECON_WDTEN         (1u << 8)  /* AFE watchdog enable */
#define AD5940_AFECON_DACEN         (1u << 9)  /* DAC enable */
#define AD5940_AFECON_HPREFPWRCON   (1u << 10) /* HP ref power */
#define AD5940_AFECON_ADCCONVEN     (1u << 11) /* ADC conversion enable */
#define AD5940_AFECON_REVID_MASK    0xF000u    /* Revision ID [15:12] */
#define AD5940_AFECON_REVID_SHIFT   12

/* CLKSEL (0x0018) -- Clock source select */

#define AD5940_CLKSEL_HFOSC         (0u << 0)  /* High-frequency oscillator */
#define AD5940_CLKSEL_LFOSC         (1u << 0)  /* Low-frequency oscillator */
#define AD5940_CLKSEL_EXTCLK        (2u << 0)  /* External clock input */
#define AD5940_CLKSEL_PLL           (3u << 0)  /* PLL output */

/* CLKNCON (0x0014) -- Clock configuration */

#define AD5940_CLKNCON_HFOSCEN      (1u << 0)  /* HF oscillator enable */
#define AD5940_CLKNCON_LFOSCEN      (1u << 1)  /* LF oscillator enable */
#define AD5940_CLKNCON_PLLEN        (1u << 2)  /* PLL enable */

/* OSCCON (0x0038) -- Oscillator control */

#define AD5940_OSCCON_HFOSCEN       (1u << 0)  /* HF oscillator enable */
#define AD5940_OSCCON_LFOSCEN       (1u << 1)  /* LF oscillator enable */
#define AD5940_OSCCON_HFRCEN        (1u << 2)  /* HF RC enable */
#define AD5940_OSCCON_LFXTALEN      (1u << 3)  /* LF crystal enable */
#define AD5940_OSCCON_HFXTALEN      (1u << 4)  /* HF crystal enable */

/* RSTCONKEY (0x0024) -- Reset control key */

#define AD5940_RSTKEY_UNLOCK         0x12EAu   /* Unlock reset register */
#define AD5940_RSTKEY_SOFT_RESET     0x0001u   /* Trigger soft reset */

/* AFECON2 (0x0100) -- AFE control 2 */

#define AD5940_AFECON2_LPTIAEN       (1u << 0)  /* LP TIA enable */
#define AD5940_AFECON2_HPTIAEN       (1u << 1)  /* HP TIA enable */
#define AD5940_AFECON2_EXCBUFEN      (1u << 2)  /* Excitation buffer enable */
#define AD5940_AFECON2_LPDACEN       (1u << 3)  /* LP DAC enable */
#define AD5940_AFECON2_VZEROEN       (1u << 4)  /* VZERO enable */
#define AD5940_AFECON2_LPTIA_RLOAD_0 (0u << 8)  /* Rload = 0 ohm */
#define AD5940_AFECON2_LPTIA_RLOAD_1 (1u << 8)  /* Rload = 100 ohm */
#define AD5940_AFECON2_LPTIA_RLOAD_2 (2u << 8)  /* Rload = 1 kohm */
#define AD5940_AFECON2_LPTIA_RLOAD_3 (3u << 8)  /* Rload = 10 kohm */

/* ADCCON (0x010C) -- ADC control */

#define AD5940_ADCCON_RATE_800K      (0u << 0)  /* 800 kSPS */
#define AD5940_ADCCON_RATE_400K      (1u << 0)  /* 400 kSPS */
#define AD5940_ADCCON_RATE_200K      (2u << 0)  /* 200 kSPS */
#define AD5940_ADCCON_RATE_100K      (3u << 0)  /* 100 kSPS */
#define AD5940_ADCCON_RATE_50K       (4u << 0)  /* 50 kSPS */
#define AD5940_ADCCON_RATE_25K       (5u << 0)  /* 25 kSPS */
#define AD5940_ADCCON_RATE_12K       (6u << 0)  /* 12.5 kSPS */
#define AD5940_ADCCON_RATE_MASK      0x0007u
#define AD5940_ADCCON_GAIN_1         (0u << 4)  /* Gain = 1 */
#define AD5940_ADCCON_GAIN_1_5       (1u << 4)  /* Gain = 1.5 */
#define AD5940_ADCCON_GAIN_2         (2u << 4)  /* Gain = 2 */
#define AD5940_ADCCON_GAIN_4         (3u << 4)  /* Gain = 4 */
#define AD5940_ADCCON_GAIN_MASK      0x0030u
#define AD5940_ADCCON_GAIN_SHIFT     4
#define AD5940_ADCCON_16BIT          (1u << 6)  /* 16-bit mode */
#define AD5940_ADCCON_VREF_1V8       (0u << 8)  /* Internal 1.82V ref */
#define AD5940_ADCCON_VREF_2V5       (1u << 8)  /* Internal 2.5V ref */
#define AD5940_ADCCON_VREF_EXT       (2u << 8)  /* External reference */
#define AD5940_ADCCON_VREF_AVDD      (3u << 8)  /* AVDD reference */
#define AD5940_ADCCON_VREF_MASK      0x0300u
#define AD5940_ADCCON_VREF_SHIFT     8

/* ADCFILTERCON (0x0104) -- ADC filter configuration */

#define AD5940_ADCFILTER_SINC3_OSR2   (0u << 0)  /* SINC3, OSR=2 */
#define AD5940_ADCFILTER_SINC3_OSR4   (1u << 0)  /* SINC3, OSR=4 */
#define AD5940_ADCFILTER_SINC3_OSR8   (2u << 0)  /* SINC3, OSR=8 */
#define AD5940_ADCFILTER_SINC3_OSR16  (3u << 0)  /* SINC3, OSR=16 */
#define AD5940_ADCFILTER_SINC3_OSR32  (4u << 0)  /* SINC3, OSR=32 */
#define AD5940_ADCFILTER_SINC3_OSR64  (5u << 0)  /* SINC3, OSR=64 */
#define AD5940_ADCFILTER_SINC3_OSR128 (6u << 0)  /* SINC3, OSR=128 */
#define AD5940_ADCFILTER_SINC3_OSR256 (7u << 0)  /* SINC3, OSR=256 */
#define AD5940_ADCFILTER_SINC3_MASK   0x0007u
#define AD5940_ADCFILTER_SINC2_OSR1   (0u << 4)  /* SINC2, OSR=1 (bypass) */
#define AD5940_ADCFILTER_SINC2_OSR2   (1u << 4)  /* SINC2, OSR=2 */
#define AD5940_ADCFILTER_SINC2_OSR4   (2u << 4)  /* SINC2, OSR=4 */
#define AD5940_ADCFILTER_SINC2_OSR8   (3u << 4)  /* SINC2, OSR=8 */
#define AD5940_ADCFILTER_SINC2_MASK   0x0030u
#define AD5940_ADCFILTER_SINC2_SHIFT  4
#define AD5940_ADCFILTER_AVRGEN       (1u << 8)  /* Averager enable */
#define AD5940_ADCFILTER_AVRG_2       (0u << 9)  /* Averager divide by 2 */
#define AD5940_ADCFILTER_AVRG_4       (1u << 9)  /* Averager divide by 4 */
#define AD5940_ADCFILTER_AVRG_8       (2u << 9)  /* Averager divide by 8 */
#define AD5940_ADCFILTER_AVRG_16      (3u << 9)  /* Averager divide by 16 */
#define AD5940_ADCFILTER_AVRG_MASK    0x0600u
#define AD5940_ADCFILTER_AVRG_SHIFT   9

/* ADCDATSTAT (0x0110) -- ADC status */

#define AD5940_ADCDATSTAT_VALID       (1u << 16) /* Data valid flag */
#define AD5940_ADCDATSTAT_OVERRANGE   (1u << 17) /* Overrange flag */
#define AD5940_ADCDATSTAT_SIGN        (1u << 15) /* Sign bit (16-bit) */

/* SWCON (0x0120) -- Switch matrix control
 *
 * The AD5940 has a programmable switch matrix that connects
 * the excitation and measurement paths to the electrode pins.
 * Each switch is controlled by a 2-bit field.
 *
 * Switch matrix layout (bits [15:0]):
 *   [1:0]   = D (drive) switch
 *   [3:2]   = SE0 (sense electrode 0)
 *   [5:4]   = SE1 (sense electrode 1)
 *   [7:6]   = RE (reference electrode)
 *   [9:8]   = CE (counter electrode)
 *   [11:10] = WE (working electrode)
 *   [15:12] = Reserved
 *
 * Switch states: 0=OPEN, 1=CLOSE, 2=DAC, 3=TIA
 *
 * TODO: Verify exact switch matrix bit assignments from datasheet.
 *       The above is based on preliminary documentation.
 */

#define AD5940_SWCON_OPEN             0x0u
#define AD5940_SWCON_CLOSE            0x1u
#define AD5940_SWCON_DAC              0x2u
#define AD5940_SWCON_TIA             0x3u

/* TIA_CON (0x0130) -- Transimpedance amplifier configuration */

#define AD5940_TIA_CON_GAIN_200       (0u << 0)  /* 200 ohm */
#define AD5940_TIA_CON_GAIN_1K        (1u << 0)  /* 1 kohm */
#define AD5940_TIA_CON_GAIN_2K        (2u << 0)  /* 2 kohm */
#define AD5940_TIA_CON_GAIN_3K        (3u << 0)  /* 3 kohm */
#define AD5940_TIA_CON_GAIN_4K        (4u << 0)  /* 4 kohm */
#define AD5940_TIA_CON_GAIN_6K        (5u << 0)  /* 6 kohm */
#define AD5940_TIA_CON_GAIN_8K        (6u << 0)  /* 8 kohm */
#define AD5940_TIA_CON_GAIN_10K       (7u << 0)  /* 10 kohm */
#define AD5940_TIA_CON_GAIN_12K       (8u << 0)  /* 12 kohm */
#define AD5940_TIA_CON_GAIN_16K       (9u << 0)  /* 16 kohm */
#define AD5940_TIA_CON_GAIN_20K       (10u << 0) /* 20 kohm */
#define AD5940_TIA_CON_GAIN_24K       (11u << 0) /* 24 kohm */
#define AD5940_TIA_CON_GAIN_30K       (12u << 0) /* 30 kohm */
#define AD5940_TIA_CON_GAIN_32K       (13u << 0) /* 32 kohm */
#define AD5940_TIA_CON_GAIN_40K       (14u << 0) /* 40 kohm */
#define AD5940_TIA_CON_GAIN_48K       (15u << 0) /* 48 kohm */
#define AD5940_TIA_CON_GAIN_64K       (16u << 0) /* 64 kohm */
#define AD5940_TIA_CON_GAIN_85K       (17u << 0) /* 85 kohm */
#define AD5940_TIA_CON_GAIN_96K       (18u << 0) /* 96 kohm */
#define AD5940_TIA_CON_GAIN_100K      (19u << 0) /* 100 kohm */
#define AD5940_TIA_CON_GAIN_120K      (20u << 0) /* 120 kohm */
#define AD5940_TIA_CON_GAIN_128K      (21u << 0) /* 128 kohm */
#define AD5940_TIA_CON_GAIN_160K      (22u << 0) /* 160 kohm */
#define AD5940_TIA_CON_GAIN_196K      (23u << 0) /* 196 kohm */
#define AD5940_TIA_CON_GAIN_256K      (24u << 0) /* 256 kohm */
#define AD5940_TIA_CON_GAIN_512K      (25u << 0) /* 512 kohm */
#define AD5940_TIA_CON_GAIN_MASK      0x001Fu
#define AD5940_TIA_CON_PDO_EN         (1u << 5)  /* Power-down override */
#define AD5940_TIA_CON_VBIAS          (1u << 8)  /* VBIAS switch connect */

/* LPTIASERVCON (0x0138) -- LP TIA servo loop */

#define AD5940_LPTIASERVCON_EN        (1u << 0)  /* Servo loop enable */
#define AD5940_LPTIASERVCON_PGAEN     (1u << 1)  /* PGA in loop enable */

/* LPCON (0x0144) -- LP control */

#define AD5940_LPCON_LPTIAEN          (1u << 0)  /* LP TIA enable */
#define AD5940_LPCON_VZEROEN          (1u << 1)  /* VZERO enable */
#define AD5940_LPCON_DACEN            (1u << 2)  /* LP DAC enable */

/* EXCITBUFCON (0x0148) -- Excitation buffer configuration */

#define AD5940_EXCITBUFCON_EN         (1u << 0)  /* Excitation buffer enable */
#define AD5940_EXCITBUFCON_VDD2       (0u << 1)  /* Supply = VDD/2 */
#define AD5940_EXCITBUFCON_DAC        (1u << 1)  /* Supply from DAC */
#define AD5940_EXCITBUFCON_BUFPWR     (1u << 4)  /* Buffer power boost */

/* DFTCON (0x0154) -- DFT engine control */

#define AD5940_DFTCON_DFTEN           (1u << 0)  /* DFT enable */
#define AD5940_DFTCON_HANNING         (0u << 2)  /* Hanning window */
#define AD5940_DFTCON_BLACKMAN        (1u << 2)  /* Blackman window */
#define AD5940_DFTCON_NO_WINDOW       (2u << 2)  /* No window */
#define AD5940_DFTCON_WINDOW_MASK     0x000Cu
#define AD5940_DFTCON_NUMSAMPLES_4    (0u << 4)  /* 4 DFT points */
#define AD5940_DFTCON_NUMSAMPLES_8    (1u << 4)  /* 8 DFT points */
#define AD5940_DFTCON_NUMSAMPLES_16   (2u << 4)  /* 16 DFT points */
#define AD5940_DFTCON_NUMSAMPLES_32   (3u << 4)  /* 32 DFT points */
#define AD5940_DFTCON_NUMSAMPLES_64   (4u << 4)  /* 64 DFT points */
#define AD5940_DFTCON_NUMSAMPLES_128  (5u << 4)  /* 128 DFT points */
#define AD5940_DFTCON_NUMSAMPLES_256  (6u << 4)  /* 256 DFT points */
#define AD5940_DFTCON_NUMSAMPLES_512  (7u << 4)  /* 512 DFT points */
#define AD5940_DFTCON_NUMSAMPLES_1024 (8u << 4)  /* 1024 DFT points */
#define AD5940_DFTCON_NUMSAMPLES_2048 (9u << 4)  /* 2048 DFT points */
#define AD5940_DFTCON_NUMSAMPLES_MASK 0x00F0u
#define AD5940_DFTCON_NUMSAMPLES_SHIFT 4

/* FIFOCFG (0x0304) -- FIFO configuration */

#define AD5940_FIFOCFG_ADC_EN         (1u << 0)  /* ADC data to FIFO */
#define AD5940_FIFOCFG_DFT_EN         (1u << 1)  /* DFT data to FIFO */
#define AD5940_FIFOCFG_SINC3_EN       (1u << 2)  /* SINC3 data to FIFO */
#define AD5940_FIFOCFG_SINC2_EN       (1u << 3)  /* SINC2 data to FIFO */
#define AD5940_FIFOCFG_TYPE_ADC       (0u << 4)  /* Type = raw ADC */
#define AD5940_FIFOCFG_TYPE_MEAS      (1u << 4)  /* Type = measurement */
#define AD5940_FIFOCFG_TYPE_DFT       (2u << 4)  /* Type = DFT result */
#define AD5940_FIFOCFG_TYPE_MASK      0x0030u

/* FIFOSTA (0x0310) -- FIFO status */

#define AD5940_FIFOSTA_EMPTY          (1u << 0)  /* FIFO empty */
#define AD5940_FIFOSTA_FULL           (1u << 1)  /* FIFO full */
#define AD5940_FIFOSTA_OVERFLOW       (1u << 2)  /* FIFO overflow */
#define AD5940_FIFOSTA_WATERMARK      (1u << 3)  /* Watermark reached */

/* FIFOCON (0x0308) -- FIFO control */

#define AD5940_FIFOCON_CLEAR          (1u << 0)  /* Clear FIFO */
#define AD5940_FIFOCON_FLUSH          (1u << 1)  /* Flush FIFO */

/* INTFLAG (0x041C) / INTCLR (0x0418) -- Interrupt bits */

#define AD5940_INT_ADC_RDY            (1u << 0)  /* ADC data ready */
#define AD5940_INT_FIFO_RDY           (1u << 1)  /* FIFO watermark */
#define AD5940_INT_FIFO_OVF           (1u << 2)  /* FIFO overflow */
#define AD5940_INT_DFT_RDY            (1u << 3)  /* DFT result ready */
#define AD5940_INT_MEAS_DONE          (1u << 4)  /* Measurement done */
#define AD5940_INT_SEQ_DONE           (1u << 5)  /* Sequence done */
#define AD5940_INT_SLP_WAKEUP         (1u << 6)  /* Sleep wakeup */
#define AD5940_INT_AFE_CMD_DONE       (1u << 7)  /* AFE command done */
#define AD5940_INT_ADC_TIMEOUT        (1u << 8)  /* ADC timeout */
#define AD5940_INT_CAL_RDY            (1u << 9)  /* Calibration ready */
#define AD5940_INT_SEQ_TIMEOUT        (1u << 10) /* Sequencer timeout */
#define AD5940_INT_GPI0               (1u << 12) /* GPIO interrupt 0 */
#define AD5940_INT_GPI1               (1u << 13) /* GPIO interrupt 1 */
#define AD5940_INT_WDT                (1u << 14) /* Watchdog timeout */

/* GPIO pins (directly connected to electrode switches and control) */

#define AD5940_GPIO_0                 (1u << 0)
#define AD5940_GPIO_1                 (1u << 1)
#define AD5940_GPIO_2                 (1u << 2)
#define AD5940_GPIO_3                 (1u << 3)
#define AD5940_GPIO_4                 (1u << 4)

/* Sequencer commands (written to sequencer memory) */

#define AD5940_SEQ_CMD_WAIT           0x0000   /* NOP / wait */
#define AD5940_SEQ_CMD_WRITE_REG      0x0001   /* Write register */
#define AD5940_SEQ_CMD_WAIT_REG       0x0002   /* Wait for register value */
#define AD5940_SEQ_CMD_STOP           0x0003   /* Stop sequencer */
#define AD5940_SEQ_CMD_BRANCH         0x0004   /* Branch */
#define AD5940_SEQ_CMD_EOF            0x00FF   /* End of sequence */

/****************************************************************************
 * Pre-processor Definitions -- EDA/GSR Measurement Constants
 ****************************************************************************/

/* Excitation parameters for DC GSR measurement */

#define AD5940_EDA_EXCIT_DC_MV        100      /* DC excitation: 100 mV */
#define AD5940_EDA_VBIAS_MV           1100     /* DC bias: 1.1 V (mid-rail) */

/* ADC configuration */

#define AD5940_EDA_SAMPLE_RATE_HZ     32       /* Target sample rate */
#define AD5940_EDA_ADC_BITS           16       /* 16-bit ADC mode */

/* Reference voltage for conductance calculation */

#define AD5940_EDA_VREF_V             1.82f    /* Internal reference (V) */

/* TIA gain for GSR measurement
 *
 * For GSR: expected current range 0.1 uA - 50 uA
 * with 100 mV excitation. TIA gain of 20 kohm gives
 * good dynamic range. Output voltage = I * R_TIA.
 * At max current (50 uA * 20 kohm = 1.0 V) stays within
 * ADC input range with 1.82V reference.
 */

#define AD5940_EDA_TIA_GAIN           AD5940_TIA_CON_GAIN_20K
#define AD5940_EDA_TIA_GAIN_OHMS      20000.0f /* 20 kohm */

/* Low-pass filter configuration for motion artifact removal */

#define AD5940_EDA_LP_FILTER_HZ       15.0f    /* Cutoff frequency (Hz) */

/* SCR (skin conductance response) detection thresholds */

#define AD5940_SCR_THRESHOLD_US       0.05f    /* 0.05 uS minimum amplitude */
#define AD5940_SCR_MIN_DURATION_MS    500      /* Min response duration */
#define AD5940_SCR_MAX_DURATION_MS    4000     /* Max response duration */
#define AD5940_SCR_LATENCY_MIN_MS     1000     /* Minimum latency from stim */
#define AD5940_SCR_LATENCY_MAX_MS     5000     /* Maximum latency from stim */
#define AD5940_SCR_BASELINE_ALPHA     0.01f    /* EMA alpha for baseline */

/* Low-pass filter coefficients (single-pole IIR, 15 Hz at 32 Hz sample rate)
 *
 *   y[n] = (1-alpha)*y[n-1] + alpha*x[n]
 *   alpha = 2*pi*fc / (2*pi*fc + fs)
 *   fc = 15 Hz, fs = 32 Hz
 *   alpha = 2*pi*15 / (2*pi*15 + 32) ~ 0.746
 *
 * We use a slightly lower alpha for more aggressive filtering:
 */

#define AD5940_EDA_FILTER_ALPHA       0.70f    /* TODO: tune for actual hardware */

/* Number of samples for initial baseline estimation */

#define AD5940_EDA_BASELINE_SAMPLES   64       /* 2 seconds at 32 Hz */

/* FIFO depth */

#define AD5940_FIFO_DEPTH             2048     /* 2048-entry FIFO */

/****************************************************************************
 * Pre-processor Definitions -- Sequencer Memory Layout
 ****************************************************************************/

/* Sequencer SRAM addresses (fixed layout for EDA measurement) */

#define AD5940_SEQ_ADDR_INIT          0x0000   /* Init sequence start */
#define AD5940_SEQ_ADDR_MEAS          0x0100   /* Measurement sequence start */
#define AD5940_SEQ_ADDR_END           0x0200   /* End marker */

/* Sequencer scratch register addresses (in SRAM, not HW registers) */

#define AD5940_SEQ_SCRATCH_BASE       0x0000
#define AD5940_SEQ_SCRATCH_COUNT      16

/****************************************************************************
 * Pre-processor Definitions -- uORB Topic
 ****************************************************************************/

/* Publish rate for sensor_impd topic */

#define AD5940_UORB_RATE_HZ           32       /* 32 Hz */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Operating modes */

enum ad5940_mode
{
  AD5940_MODE_IDLE = 0,           /* Idle, AFE powered down */
  AD5940_MODE_EDA_DC,             /* EDA: DC galvanic skin response */
  AD5940_MODE_EDA_AC,             /* EDA: AC impedance (future) */
  AD5940_MODE_HIBERNATE           /* Hibernate (<5 uA) */
};

/* SCR detection state */

enum ad5940_scr_state
{
  AD5940_SCR_STATE_IDLE = 0,      /* No SCR in progress */
  AD5940_SCR_STATE_RISING,        /* Conductance increasing */
  AD5940_SCR_STATE_PEAK,          /* Peak detected */
  AD5940_SCR_STATE_FALLING        /* Conductance decreasing */
};

/* Driver configuration (passed to ad5940_init) */

struct ad5940_config
{
  int      spi_bus;               /* SPI bus number (e.g., 1 for /dev/spi1) */
  int      cs_pin;                /* GPIO for chip select (-1 = HW CS) */
  int      irq_pin;               /* GPIO for data-ready interrupt (-1 = polling) */
  int      reset_pin;             /* GPIO for hardware reset (-1 = SW reset) */
  uint32_t spi_freq;              /* SPI clock frequency (Hz) */
};

/* EDA measurement result */

struct ad5940_eda_sample
{
  float    scl;                   /* Skin conductance level (uS) */
  float    scl_filtered;          /* Filtered SCL (uS) */
  float    scr_amplitude;         /* SCR amplitude (uS, 0 if no event) */
  uint32_t scr_onset_time;        /* SCR onset timestamp (ms) */
  uint8_t  scr_event;             /* 1 if SCR event detected */
  uint16_t raw_adc;               /* Raw 16-bit ADC value */
  float    voltage_v;             /* Measured voltage (V) */
  float    current_ua;            /* Measured current (uA) */
  int      valid;                 /* 1 if reading is valid */
};

/* SCR detector state */

struct ad5940_scr_detector
{
  enum ad5940_scr_state state;    /* Current SCR state */
  float    baseline;              /* Baseline conductance (uS) */
  float    peak_value;            /* Peak conductance during event */
  float    peak_amplitude;        /* Peak amplitude above baseline */
  uint32_t onset_time;            /* Event onset timestamp (ms) */
  uint32_t peak_time;             /* Peak timestamp (ms) */
  uint32_t sample_count;          /* Samples since event start */
  int      baseline_ready;        /* 1 if baseline is established */
  uint32_t baseline_samples;      /* Samples used for baseline */
};

/* Driver context */

struct ad5940_dev
{
  struct ad5940_config config;    /* Configuration copy */
  int      fd;                    /* SPI file descriptor */
  uint16_t part_id;               /* Part ID read from device */
  enum ad5940_mode mode;          /* Current operating mode */
  int      initialized;           /* 1 if init succeeded */

  /* EDA measurement state */

  float    scl_current;           /* Current SCL value (uS) */
  float    scl_filtered;          /* Filtered SCL (uS) */
  struct ad5940_scr_detector scr; /* SCR event detector */

  /* Calibration */

  int16_t  adc_offset;            /* ADC offset calibration */
  float    adc_gain;              /* ADC gain calibration factor */

  /* Timing */

  uint32_t last_sample_ms;        /* Timestamp of last sample */
  uint32_t sample_count;          /* Total samples taken */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: ad5940_init
 *
 * Description:
 *   Initialize the AD5940 AFE.
 *   Opens SPI bus, verifies part ID, performs soft reset, configures
 *   the AFE for idle mode.
 *
 * Input Parameters:
 *   config - Driver configuration
 *   dev    - Driver context to initialize
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int ad5940_init(const struct ad5940_config *config,
                struct ad5940_dev *dev);

/****************************************************************************
 * Name: ad5940_start_eda
 *
 * Description:
 *   Configure the AD5940 for continuous EDA (galvanic skin response)
 *   measurement. Programs the excitation generator, ADC, TIA, switch
 *   matrix, and FIFO for 32 Hz DC measurement.
 *
 * Input Parameters:
 *   dev - Driver context
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int ad5940_start_eda(struct ad5940_dev *dev);

/****************************************************************************
 * Name: ad5940_read_eda
 *
 * Description:
 *   Read one EDA sample. Reads the ADC value from the FIFO, converts
 *   to skin conductance, applies the low-pass filter, and runs the
 *   SCR event detector.
 *
 * Input Parameters:
 *   dev    - Driver context
 *   sample - Output EDA sample
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int ad5940_read_eda(struct ad5940_dev *dev,
                    struct ad5940_eda_sample *sample);

/****************************************************************************
 * Name: ad5940_read_fifo
 *
 * Description:
 *   Read raw samples from the AD5940 FIFO.
 *
 * Input Parameters:
 *   dev   - Driver context
 *   buf   - Output buffer for raw ADC values
 *   max   - Maximum samples to read
 *   count - Output: actual samples read
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int ad5940_read_fifo(struct ad5940_dev *dev,
                     uint16_t *buf, int max, int *count);

/****************************************************************************
 * Name: ad5940_stop_eda
 *
 * Description:
 *   Stop EDA measurement and return to idle mode.
 *
 ****************************************************************************/

int ad5940_stop_eda(struct ad5940_dev *dev);

/****************************************************************************
 * Name: ad5940_hibernate
 *
 * Description:
 *   Enter hibernate mode (<5 uA). Saves current configuration to SRAM
 *   and powers down the AFE.
 *
 ****************************************************************************/

int ad5940_hibernate(struct ad5940_dev *dev);

/****************************************************************************
 * Name: ad5940_wakeup
 *
 * Description:
 *   Wake from hibernate and restore configuration.
 *
 ****************************************************************************/

int ad5940_wakeup(struct ad5940_dev *dev);

/****************************************************************************
 * Name: ad5940_close
 *
 * Description:
 *   Shutdown the AFE and release SPI resources.
 *
 ****************************************************************************/

void ad5940_close(struct ad5940_dev *dev);

/****************************************************************************
 * Name: ad5940_reset
 *
 * Description:
 *   Perform a soft reset of the AD5940.
 *
 ****************************************************************************/

int ad5940_reset(struct ad5940_dev *dev);

/****************************************************************************
 * Name: ad5940_read_reg
 *
 * Description:
 *   Read a single 16-bit register from the AD5940.
 *
 ****************************************************************************/

int ad5940_read_reg(struct ad5940_dev *dev,
                    uint16_t addr, uint16_t *value);

/****************************************************************************
 * Name: ad5940_write_reg
 *
 * Description:
 *   Write a single 16-bit register to the AD5940.
 *
 ****************************************************************************/

int ad5940_write_reg(struct ad5940_dev *dev,
                     uint16_t addr, uint16_t value);

/****************************************************************************
 * Name: ad5940_raw_to_conductance
 *
 * Description:
 *   Convert raw ADC value to skin conductance in microsiemens (uS).
 *   Uses the configured TIA gain and excitation voltage.
 *
 *   Conductance (S) = I / V = (V_adc / R_tia) / V_excite
 *   Conductance (uS) = V_adc / (R_tia * V_excite) * 1e6
 *
 ****************************************************************************/

float ad5940_raw_to_conductance(struct ad5940_dev *dev, uint16_t raw);

/****************************************************************************
 * Name: ad5940_get_scr_state
 *
 * Description:
 *   Get the current SCR detector state.
 *
 ****************************************************************************/

const struct ad5940_scr_detector *
ad5940_get_scr_state(const struct ad5940_dev *dev);

/****************************************************************************
 * Name: ad5940_reset_scr_detector
 *
 * Description:
 *   Reset the SCR event detector (clear baseline, state).
 *
 ****************************************************************************/

void ad5940_reset_scr_detector(struct ad5940_dev *dev);

#endif /* __FIRMWARE_DRIVERS_AD5940_AD5940_H */
