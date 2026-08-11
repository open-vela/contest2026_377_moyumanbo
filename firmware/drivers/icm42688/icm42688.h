/****************************************************************************
 * ICM-42688-P Six-Axis IMU Driver
 *
 * SPI interface, 3-axis accelerometer + 3-axis gyroscope.
 * Designed for VelaSense wearable motion sensing and motion artifact
 * removal from PPG signals.
 *
 * Datasheet: ICM-42688-P v1.7 (TDK InvenSense)
 *
 * Key specs:
 *   - Supply: 1.71V - 3.6V (VDD), 1.71V - 1.95V (VDDIO)
 *   - SPI: up to 24 MHz
 *   - I2C: up to 1 MHz (400 kHz fast mode)
 *   - Accel: +/-2/4/8/16g, 16-bit, ODR 12.5Hz - 32kHz
 *   - Gyro: +/-15.625..2000 dps, 16-bit, ODR 12.5Hz - 32kHz
 *   - FIFO: 2KB, packet-based, watermark interrupt
 *   - Data-ready interrupt on INT1/INT2
 *   - Low power: 3.5mA accel+gyro at 100Hz ODR
 *
 * VelaSense configuration:
 *   - Accel: +/-8g, 100Hz ODR, low-noise mode
 *   - Gyro:  +/-2000 dps, 100Hz ODR
 *   - FIFO:  stream mode, watermark at 16 samples
 *   - INT1:  data-ready interrupt
 *
 * Usage:
 *   1. icm42688_init()       -- reset, verify WHO_AM_I, configure
 *   2. icm42688_read_accel() -- read accelerometer XYZ
 *   3. icm42688_read_gyro()  -- read gyroscope XYZ
 *   4. icm42688_read_fifo()  -- batch-read from hardware FIFO
 *   5. icm42688_close()      -- power down and release
 *
 ****************************************************************************/

#ifndef __FIRMWARE_DRIVERS_ICM42688_ICM42688_H
#define __FIRMWARE_DRIVERS_ICM42688_ICM42688_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* -------------------------------------------------------------------------
 * Bank 0 Registers (BANK_SEL = 0x00, default after reset)
 * ----------------------------------------------------------------------- */

/* Device identification */

#define ICM42688_REG_WHO_AM_I            0x75  /* Device ID register */
#define ICM42688_WHO_AM_I_VALUE          0x47  /* Expected value for ICM-42688-P */

#define ICM42688_REG_BANK_SEL            0x76  /* Register bank selection [1:0] */

/* Device configuration */

#define ICM42688_REG_DEVICE_CONFIG       0x11  /* Device config (SPI mode, reset) */
#define ICM42688_REG_SIGNAL_PATH_RESET   0x02  /* Signal path reset */
#define ICM42688_REG_DRIVE_CONFIG        0x13  /* SPI/I2C drive strength */
#define ICM42688_REG_INT_CONFIG          0x14  /* Interrupt configuration */

/* FIFO configuration */

#define ICM42688_REG_FIFO_CONFIG         0x16  /* FIFO configuration */
#define ICM42688_REG_FIFO_COUNTH         0x2E  /* FIFO byte count [15:8] */
#define ICM42688_REG_FIFO_COUNTL         0x2F  /* FIFO byte count [7:0] */
#define ICM42688_REG_FIFO_DATA           0x30  /* FIFO data read port */

/* Interrupt sources */

#define ICM42688_REG_INT_SOURCE0         0x65  /* Interrupt source 0 */
#define ICM42688_REG_INT_SOURCE1         0x66  /* Interrupt source 1 */
#define ICM42688_REG_INT_SOURCE3         0x68  /* Interrupt source 3 */
#define ICM42688_REG_INT_SOURCE4         0x69  /* Interrupt source 4 */
#define ICM42688_REG_INT_SOURCE5         0x6A  /* Interrupt source 5 */
#define ICM42688_REG_FIFO_CONFIG1        0x5F  /* FIFO config 1 (enable) */
#define ICM42688_REG_FIFO_CONFIG2        0x60  /* FIFO watermark [7:0] */
#define ICM42688_REG_FIFO_CONFIG3        0x61  /* FIFO watermark [10:8] */

/* Sensor data output registers (16-bit signed, big-endian) */

#define ICM42688_REG_TEMP_DATA1          0x1D  /* Temperature data [15:8] */
#define ICM42688_REG_TEMP_DATA0          0x1E  /* Temperature data [7:0] */

#define ICM42688_REG_ACCEL_DATA_X1       0x1F  /* Accel X [15:8] */
#define ICM42688_REG_ACCEL_DATA_X0       0x20  /* Accel X [7:0] */
#define ICM42688_REG_ACCEL_DATA_Y1       0x21  /* Accel Y [15:8] */
#define ICM42688_REG_ACCEL_DATA_Y0       0x22  /* Accel Y [7:0] */
#define ICM42688_REG_ACCEL_DATA_Z1       0x23  /* Accel Z [15:8] */
#define ICM42688_REG_ACCEL_DATA_Z0       0x24  /* Accel Z [7:0] */

#define ICM42688_REG_GYRO_DATA_X1        0x25  /* Gyro X [15:8] */
#define ICM42688_REG_GYRO_DATA_X0        0x26  /* Gyro X [7:0] */
#define ICM42688_REG_GYRO_DATA_Y1        0x27  /* Gyro Y [15:8] */
#define ICM42688_REG_GYRO_DATA_Y0        0x28  /* Gyro Y [7:0] */
#define ICM42688_REG_GYRO_DATA_Z1        0x29  /* Gyro Z [15:8] */
#define ICM42688_REG_GYRO_DATA_Z0        0x2A  /* Gyro Z [7:0] */

/* PWR_MGMT0: power management (accel/gyro mode) */

#define ICM42688_REG_PWR_MGMT0           0x4E  /* Power management 0 */

/* GYRO_CONFIG0: gyroscope configuration */

#define ICM42688_REG_GYRO_CONFIG0        0x4F  /* Gyro ODR + FSR */
#define ICM42688_REG_GYRO_CONFIG1        0x51  /* Gyro filter config */
#define ICM42688_REG_GYRO_CONFIG2        0x52  /* Gyro filter config 2 */
#define ICM42688_REG_GYRO_ACCEL_CONFIG0  0x52  /* Gyro/accel LPF bandwidth */

/* ACCEL_CONFIG0: accelerometer configuration */

#define ICM42688_REG_ACCEL_CONFIG0       0x50  /* Accel ODR + FSR */
#define ICM42688_REG_ACCEL_CONFIG1       0x53  /* Accel filter config 1 */
#define ICM42688_REG_ACCEL_CONFIG2       0x54  /* Accel filter config 2 */
#define ICM42688_REG_ACCEL_CONFIG3       0x55  /* Accel filter config 3 */
#define ICM42688_REG_ACCEL_WOM_X_THR    0x4B  /* Wake-on-motion X threshold */
#define ICM42688_REG_ACCEL_WOM_Y_THR    0x4C  /* Wake-on-motion Y threshold */
#define ICM42688_REG_ACCEL_WOM_Z_THR    0x4D  /* Wake-on-motion Z threshold */
#define ICM42688_REG_INT_CONFIG0        0x63  /* Interrupt config 0 */
#define ICM42688_REG_INT_CONFIG1        0x64  /* Interrupt config 1 */
#define ICM42688_REG_INT_STATUS         0x2D  /* Interrupt status */
#define ICM42688_REG_INT_STATUS2        0x37  /* Interrupt status 2 */
#define ICM42688_REG_INT_STATUS3        0x38  /* Interrupt status 3 */
#define ICM42688_REG_SMD_CONFIG         0x57  /* Significant motion detect */

/* -------------------------------------------------------------------------
 * Bank 1 Registers (BANK_SEL = 0x01)
 * ----------------------------------------------------------------------- */

#define ICM42688_REG_SENSOR_CONFIG0_B1   0x03  /* Sensor config 0 */
#define ICM42688_REG_GYRO_CONFIG_STATIC2_B1  0x0B  /* Gyro config static 2 */
#define ICM42688_REG_GYRO_CONFIG_STATIC3_B1  0x0C  /* Gyro config static 3 */
#define ICM42688_REG_GYRO_CONFIG_STATIC4_B1  0x0D  /* Gyro config static 4 */
#define ICM42688_REG_GYRO_CONFIG_STATIC5_B1  0x0E  /* Gyro config static 5 */
#define ICM42688_REG_ACCEL_CONFIG_STATIC2_B1 0x03  /* Accel config static 2 */
#define ICM42688_REG_ACCEL_CONFIG_STATIC3_B1 0x04  /* Accel config static 3 */
#define ICM42688_REG_ACCEL_CONFIG_STATIC4_B1 0x05  /* Accel config static 4 */

/* -------------------------------------------------------------------------
 * Bank 2 Registers (BANK_SEL = 0x02)
 * ----------------------------------------------------------------------- */

#define ICM42688_REG_ACCEL_CONFIG_STATIC5_B2 0x03  /* Accel config static 5 */

/* -------------------------------------------------------------------------
 * Bank 4 Registers (BANK_SEL = 0x04)
 * ----------------------------------------------------------------------- */

#define ICM42688_REG_INT_SOURCE6_B4      0x4D  /* Interrupt source 6 */
#define ICM42688_REG_INT_SOURCE7_B4      0x4E  /* Interrupt source 7 */
#define ICM42688_REG_INT_SOURCE8_B4      0x4F  /* Interrupt source 8 */
#define ICM42688_REG_INT_SOURCE9_B4      0x50  /* Interrupt source 9 */

/* -------------------------------------------------------------------------
 * DEVICE_CONFIG (0x11) bit definitions
 * ----------------------------------------------------------------------- */

#define ICM42688_DEVICE_CONFIG_SPI_3W    (1 << 1)  /* 1 = 3-wire SPI mode */
#define ICM42688_DEVICE_CONFIG_SPI_4W    (0 << 1)  /* 0 = 4-wire SPI mode */
#define ICM42688_DEVICE_CONFIG_RESET     (1 << 0)  /* 1 = software reset */

/* -------------------------------------------------------------------------
 * PWR_MGMT0 (0x4E) bit definitions
 *
 * Gyro mode [3:2]:
 *   00 = off (default)
 *   01 = standby
 *   11 = low-noise mode
 *
 * Accel mode [1:0]:
 *   00 = off (default)
 *   01 = standby (not supported, reserved)
 *   10 = low-power mode
 *   11 = low-noise mode
 *
 * Note: When gyro is in standby mode and accel transitions from OFF to
 * any active mode, there is a 200us wake-up delay.
 * ----------------------------------------------------------------------- */

#define ICM42688_PWR_MGMT0_GYRO_OFF       (0x00 << 2) /* Gyro off */
#define ICM42688_PWR_MGMT0_GYRO_STANDBY   (0x01 << 2) /* Gyro standby */
#define ICM42688_PWR_MGMT0_GYRO_LN        (0x03 << 2) /* Gyro low-noise */
#define ICM42688_PWR_MGMT0_ACCEL_OFF      0x00        /* Accel off */
#define ICM42688_PWR_MGMT0_ACCEL_LP       0x02        /* Accel low-power */
#define ICM42688_PWR_MGMT0_ACCEL_LN       0x03        /* Accel low-noise */

#define ICM42688_PWR_MGMT0_GYRO_MASK      0x0C
#define ICM42688_PWR_MGMT0_ACCEL_MASK     0x03

/* -------------------------------------------------------------------------
 * GYRO_CONFIG0 (0x4F) bit definitions
 *
 * Gyro FSR [7:5]:
 *   000 = +/-2000 dps (default)
 *   001 = +/-1000 dps
 *   010 = +/-500 dps
 *   011 = +/-250 dps
 *   100 = +/-125 dps
 *   101 = +/-62.5 dps
 *   110 = +/-31.25 dps
 *   111 = +/-15.625 dps
 *
 * Gyro ODR [3:0]:
 *   0110 = 12.5 Hz
 *   0111 = 25 Hz
 *   1000 = 50 Hz
 *   1001 = 100 Hz (default)
 *   1010 = 200 Hz
 *   1011 = 1 kHz
 *   1100 = 2 kHz
 *   1101 = 4 kHz
 *   1110 = 8 kHz
 *   1111 = 16 kHz
 *   0000 = 32 kHz
 * ----------------------------------------------------------------------- */

#define ICM42688_GYRO_FSR_2000DPS        (0x00 << 5)
#define ICM42688_GYRO_FSR_1000DPS        (0x01 << 5)
#define ICM42688_GYRO_FSR_500DPS         (0x02 << 5)
#define ICM42688_GYRO_FSR_250DPS         (0x03 << 5)
#define ICM42688_GYRO_FSR_125DPS         (0x04 << 5)
#define ICM42688_GYRO_FSR_62DPS          (0x05 << 5)
#define ICM42688_GYRO_FSR_31DPS          (0x06 << 5)
#define ICM42688_GYRO_FSR_15DPS          (0x07 << 5)
#define ICM42688_GYRO_FSR_MASK           0xE0

#define ICM42688_GYRO_ODR_32KHZ          0x00
#define ICM42688_GYRO_ODR_16KHZ          0x01
#define ICM42688_GYRO_ODR_8KHZ           0x02
#define ICM42688_GYRO_ODR_4KHZ           0x03
#define ICM42688_GYRO_ODR_2KHZ           0x04
#define ICM42688_GYRO_ODR_1KHZ           0x05
#define ICM42688_GYRO_ODR_200HZ          0x06
#define ICM42688_GYRO_ODR_100HZ          0x07
#define ICM42688_GYRO_ODR_50HZ           0x08
#define ICM42688_GYRO_ODR_25HZ           0x09
#define ICM42688_GYRO_ODR_12HZ5          0x0A
#define ICM42688_GYRO_ODR_500HZ          0x0B
#define ICM42688_GYRO_ODR_MASK           0x0F

/* -------------------------------------------------------------------------
 * ACCEL_CONFIG0 (0x50) bit definitions
 *
 * Accel FSR [7:5]:
 *   000 = +/-16g (default)
 *   001 = +/-8g
 *   010 = +/-4g
 *   011 = +/-2g
 *
 * Accel ODR [3:0]:
 *   0110 = 12.5 Hz
 *   0111 = 25 Hz
 *   1000 = 50 Hz
 *   1001 = 100 Hz (default)
 *   1010 = 200 Hz
 *   1011 = 1 kHz
 *   1100 = 2 kHz
 *   1101 = 4 kHz
 *   1110 = 8 kHz
 *   1111 = 16 kHz
 *   0000 = 32 kHz
 * ----------------------------------------------------------------------- */

#define ICM42688_ACCEL_FSR_16G           (0x00 << 5)
#define ICM42688_ACCEL_FSR_8G            (0x01 << 5)
#define ICM42688_ACCEL_FSR_4G            (0x02 << 5)
#define ICM42688_ACCEL_FSR_2G            (0x03 << 5)
#define ICM42688_ACCEL_FSR_MASK          0xE0

#define ICM42688_ACCEL_ODR_32KHZ         0x00
#define ICM42688_ACCEL_ODR_16KHZ         0x01
#define ICM42688_ACCEL_ODR_8KHZ          0x02
#define ICM42688_ACCEL_ODR_4KHZ          0x03
#define ICM42688_ACCEL_ODR_2KHZ          0x04
#define ICM42688_ACCEL_ODR_1KHZ          0x05
#define ICM42688_ACCEL_ODR_200HZ         0x06
#define ICM42688_ACCEL_ODR_100HZ         0x07
#define ICM42688_ACCEL_ODR_50HZ          0x08
#define ICM42688_ACCEL_ODR_25HZ          0x09
#define ICM42688_ACCEL_ODR_12HZ5         0x0A
#define ICM42688_ACCEL_ODR_500HZ         0x0B
#define ICM42688_ACCEL_ODR_MASK          0x0F

/* -------------------------------------------------------------------------
 * GYRO_CONFIG1 (0x51) bit definitions
 *
 * GYRO_UI_FILT_ORD [6:5]:
 *   00 = 1st order
 *   01 = 2nd order
 *   10 = 3rd order
 *
 * GYRO_DEC2_M2_ORD [4:3]:
 *   00 = 1st order
 *   01 = 2nd order
 *   10 = 3rd order
 *
 * TEMP_FILT_BW [2:0]:
 *   000 = BW@4000Hz ODR@8kHz
 *   001 = BW@170Hz ODR@1kHz
 *   ...
 * ----------------------------------------------------------------------- */

#define ICM42688_GYRO_CONFIG1_UI_FILT_1ST  (0x00 << 5)
#define ICM42688_GYRO_CONFIG1_UI_FILT_2ND  (0x01 << 5)
#define ICM42688_GYRO_CONFIG1_UI_FILT_3RD  (0x02 << 5)
#define ICM42688_GYRO_CONFIG1_DEC2_M2_1ST  (0x00 << 3)
#define ICM42688_GYRO_CONFIG1_DEC2_M2_2ND  (0x01 << 3)
#define ICM42688_GYRO_CONFIG1_DEC2_M2_3RD  (0x02 << 3)

/* Temperature filter bandwidth values */

#define ICM42688_TEMP_FILT_BW_4000HZ      0x00
#define ICM42688_TEMP_FILT_BW_170HZ       0x01
#define ICM42688_TEMP_FILT_BW_82HZ        0x02
#define ICM42688_TEMP_FILT_BW_40HZ        0x03
#define ICM42688_TEMP_FILT_BW_20HZ        0x04
#define ICM42688_TEMP_FILT_BW_10HZ        0x05
#define ICM42688_TEMP_FILT_BW_5HZ         0x06

/* -------------------------------------------------------------------------
 * GYRO_ACCEL_CONFIG0 (0x52) bit definitions
 *
 * ACCEL_UI_FILT_BW [7:4]: see table below
 * GYRO_UI_FILT_BW [3:0]: see table below
 *
 * LPF BW index values (same for accel and gyro):
 *   0000 = ODR/2
 *   0001 = max(ODR/2, ODR/4 * sqrt(2))
 *   0010 = ODR/4 * sqrt(2)
 *   0011 = max(ODR/4, ODR/5)
 *   0100 = ODR/4
 *   0101 = ODR/5
 *   0110 = ODR/8
 *   0111 = ODR/10
 *   1000 = ODR/16
 *   1001 = ODR/20
 *   1010 = ODR/40
 *   1011 = ODR/2 * sqrt(2)
 *   1100 = ODR/2
 *   ...  (see datasheet Table 5)
 * ----------------------------------------------------------------------- */

#define ICM42688_ACCEL_UI_FILT_BW_ODR2     (0x00 << 4)
#define ICM42688_ACCEL_UI_FILT_BW_ODR2SQ   (0x01 << 4)
#define ICM42688_ACCEL_UI_FILT_BW_ODR4SQ   (0x02 << 4)
#define ICM42688_ACCEL_UI_FILT_BW_ODR4     (0x03 << 4)
#define ICM42688_ACCEL_UI_FILT_BW_ODR4P    (0x04 << 4)
#define ICM42688_ACCEL_UI_FILT_BW_ODR5     (0x05 << 4)
#define ICM42688_ACCEL_UI_FILT_BW_ODR8     (0x06 << 4)
#define ICM42688_ACCEL_UI_FILT_BW_ODR10    (0x07 << 4)
#define ICM42688_ACCEL_UI_FILT_BW_ODR16    (0x08 << 4)
#define ICM42688_ACCEL_UI_FILT_BW_ODR20    (0x09 << 4)
#define ICM42688_ACCEL_UI_FILT_BW_ODR40    (0x0A << 4)

#define ICM42688_GYRO_UI_FILT_BW_ODR2      0x00
#define ICM42688_GYRO_UI_FILT_BW_ODR2SQ    0x01
#define ICM42688_GYRO_UI_FILT_BW_ODR4SQ    0x02
#define ICM42688_GYRO_UI_FILT_BW_ODR4      0x03
#define ICM42688_GYRO_UI_FILT_BW_ODR4P     0x04
#define ICM42688_GYRO_UI_FILT_BW_ODR5      0x05
#define ICM42688_GYRO_UI_FILT_BW_ODR8      0x06
#define ICM42688_GYRO_UI_FILT_BW_ODR10     0x07
#define ICM42688_GYRO_UI_FILT_BW_ODR16     0x08
#define ICM42688_GYRO_UI_FILT_BW_ODR20     0x09
#define ICM42688_GYRO_UI_FILT_BW_ODR40     0x0A

/* -------------------------------------------------------------------------
 * ACCEL_CONFIG1 (0x53) bit definitions
 *
 * ACCEL_UI_FILT_ORD [4:3]:
 *   00 = 1st order
 *   01 = 2nd order
 *   10 = 3rd order
 *
 * ACCEL_DEC2_M2_ORD [2:1]:
 *   00 = 1st order
 *   01 = 2nd order
 *   10 = 3rd order
 * ----------------------------------------------------------------------- */

#define ICM42688_ACCEL_CONFIG1_UI_FILT_1ST  (0x00 << 3)
#define ICM42688_ACCEL_CONFIG1_UI_FILT_2ND  (0x01 << 3)
#define ICM42688_ACCEL_CONFIG1_UI_FILT_3RD  (0x02 << 3)

/* -------------------------------------------------------------------------
 * ACCEL_CONFIG2 (0x54) bit definitions
 *
 * ACCEL_UI_FILT_ORD [5:4]:
 *   00 = 1st order
 *   01 = 2nd order
 *   10 = 3rd order
 *
 * DEC2_M2_ORD [3:2]:
 *   00 = 1st order
 *   01 = 2nd order
 *   10 = 3rd order
 * ----------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * FIFO_CONFIG (0x16) bit definitions
 *
 * FIFO_MODE [7:6]:
 *   00 = bypass (FIFO disabled)
 *   01 = stream mode (old data overwritten when full)
 *   10 = stop-on-full (no overwrites)
 *   11 = reserved
 *
 * FIFO_DEPTH [4:0]: not used in ICM-42688-P (fixed 2KB)
 * ----------------------------------------------------------------------- */

#define ICM42688_FIFO_MODE_BYPASS       (0x00 << 6)
#define ICM42688_FIFO_MODE_STREAM       (0x01 << 6)
#define ICM42688_FIFO_MODE_STOP_ON_FULL (0x02 << 6)
#define ICM42688_FIFO_MODE_MASK         0xC0

/* -------------------------------------------------------------------------
 * FIFO_CONFIG1 (0x5F) bit definitions
 *
 * FIFO_GYRO_EN   [2] : 1 = include gyro data in FIFO packets
 * FIFO_ACCEL_EN  [1] : 1 = include accel data in FIFO packets
 * FIFO_TMST_FSYNC_EN [0] : 1 = include timestamp in FIFO packets
 * ----------------------------------------------------------------------- */

#define ICM42688_FIFO_CONFIG1_GYRO_EN     (1 << 2)
#define ICM42688_FIFO_CONFIG1_ACCEL_EN    (1 << 1)
#define ICM42688_FIFO_CONFIG1_TMST_EN     (1 << 0)

/* -------------------------------------------------------------------------
 * INT_CONFIG (0x14) bit definitions
 *
 * INT1_MODE       [2] : 0 = pulse, 1 = latched
 * INT1_DRIVE_CIRCUIT [1] : 0 = open drain, 1 = push-pull
 * INT1_POLARITY   [0] : 0 = active low, 1 = active high
 * ----------------------------------------------------------------------- */

#define ICM42688_INT_CONFIG_INT1_LATCHED  (1 << 2)
#define ICM42688_INT_CONFIG_INT1_PULSE    (0 << 2)
#define ICM42688_INT_CONFIG_INT1_PP       (1 << 1)  /* push-pull */
#define ICM42688_INT_CONFIG_INT1_OD       (0 << 1)  /* open drain */
#define ICM42688_INT_CONFIG_INT1_ACT_HI   (1 << 0)  /* active high */
#define ICM42688_INT_CONFIG_INT1_ACT_LO   (0 << 0)  /* active low */

/* -------------------------------------------------------------------------
 * INT_CONFIG0 (0x63) bit definitions
 *
 * UI_FILT_INT_CLEAR [6:5]:
 *   00 = cleared on status read (default)
 *   01 = latched clear on status read
 *   10 = latched clear on FIFO read
 *   11 = reserved
 *
 * FIFO_THS_INT_CLEAR [4:3]:
 *   00 = cleared on status read (default)
 *   ...
 * ----------------------------------------------------------------------- */

#define ICM42688_INT_CONFIG0_UI_CLR_ON_STATUS  (0x00 << 5)
#define ICM42688_INT_CONFIG0_UI_CLR_LATCH      (0x01 << 5)

/* -------------------------------------------------------------------------
 * INT_SOURCE0 (0x65) — routes interrupt sources to INT1
 *
 * bit 6: UI_FSYNC_INT1_EN
 * bit 5: PLL_RDY_INT1_EN
 * bit 4: RESET_DONE_INT1_EN
 * bit 3: DATA_RDY_UI_DRDY_INT1_EN (data-ready for accel+gyro)
 * bit 2: FIFO_THS_INT1_EN (FIFO watermark)
 * bit 1: FIFO_FULL_INT1_EN
 * bit 0: AGC_RDY_INT1_EN
 * ----------------------------------------------------------------------- */

#define ICM42688_INT_SOURCE0_UI_DRDY      (1 << 3)  /* data-ready to INT1 */
#define ICM42688_INT_SOURCE0_FIFO_THS     (1 << 2)  /* FIFO watermark */
#define ICM42688_INT_SOURCE0_FIFO_FULL    (1 << 1)  /* FIFO full */

/* -------------------------------------------------------------------------
 * INT_SOURCE1 (0x66) — routes interrupt sources to INT2
 * Same bit definitions as INT_SOURCE0.
 * ----------------------------------------------------------------------- */

#define ICM42688_INT_SOURCE1_UI_DRDY      (1 << 3)  /* data-ready to INT2 */
#define ICM42688_INT_SOURCE1_FIFO_THS     (1 << 2)  /* FIFO watermark */
#define ICM42688_INT_SOURCE1_FIFO_FULL    (1 << 1)  /* FIFO full */

/* -------------------------------------------------------------------------
 * INT_STATUS (0x2D) — interrupt status register
 *
 * bit 6: UI_FSYNC_INT
 * bit 5: PLL_RDY_INT
 * bit 4: RESET_DONE_INT
 * bit 3: DATA_RDY_INT (new accel+gyro data ready)
 * bit 2: FIFO_THS_INT (FIFO watermark reached)
 * bit 1: FIFO_FULL_INT
 * bit 0: AGC_RDY_INT
 * ----------------------------------------------------------------------- */

#define ICM42688_INT_STATUS_DATA_RDY      (1 << 3)
#define ICM42688_INT_STATUS_FIFO_THS      (1 << 2)
#define ICM42688_INT_STATUS_FIFO_FULL     (1 << 1)
#define ICM42688_INT_STATUS_RESET_DONE    (1 << 4)

/* -------------------------------------------------------------------------
 * SIGNAL_PATH_RESET (0x02) bit definitions
 *
 * bit 4: DMP_INIT_EN (enable DMP)
 * bit 3: DMP_MEM_RESET_EN (reset DMP memory)
 * bit 2: FIFO_FLUSH (1 = flush FIFO, auto-clears)
 * bit 1: TMST_STROBE (timestamp strobe, auto-clears)
 * bit 0: ???
 * ----------------------------------------------------------------------- */

#define ICM42688_SIGNAL_PATH_FIFO_FLUSH   (1 << 2)

/* -------------------------------------------------------------------------
 * DRIVE_CONFIG (0x13) bit definitions
 *
 * SPI slew rate [5:3] and I2C slew rate [2:0]
 * 0x00 = minimal, 0x07 = maximum (default for I2C)
 * ----------------------------------------------------------------------- */

#define ICM42688_DRIVE_CONFIG_SPI_SLEW    (0x05 << 3)  /* fast SPI */

/* -------------------------------------------------------------------------
 * Temperature conversion constants
 *
 * Temp (C) = (raw / 132.48) + 25
 * 1 LSB = 1/132.48 C = ~0.007548 C
 * ----------------------------------------------------------------------- */

#define ICM42688_TEMP_OFFSET             25.0f
#define ICM42688_TEMP_SCALE              132.48f

/* -------------------------------------------------------------------------
 * Accel sensitivity (LSB/g) per FSR
 *
 * +/-16g:  2048 LSB/g
 * +/-8g:   4096 LSB/g
 * +/-4g:   8192 LSB/g
 * +/-2g:  16384 LSB/g
 * ----------------------------------------------------------------------- */

#define ICM42688_ACCEL_SENS_16G          2048.0f
#define ICM42688_ACCEL_SENS_8G           4096.0f
#define ICM42688_ACCEL_SENS_4G           8192.0f
#define ICM42688_ACCEL_SENS_2G          16384.0f

/* -------------------------------------------------------------------------
 * Gyro sensitivity (LSB/dps) per FSR
 *
 * +/-2000 dps:  16.4 LSB/dps
 * +/-1000 dps:  32.8 LSB/dps
 * +/-500 dps:   65.5 LSB/dps
 * +/-250 dps:  131.0 LSB/dps
 * +/-125 dps:  262.0 LSB/dps
 * +/-62.5 dps: 524.3 LSB/dps
 * +/-31.25 dps: 1048.6 LSB/dps
 * +/-15.625 dps: 2097.2 LSB/dps
 * ----------------------------------------------------------------------- */

#define ICM42688_GYRO_SENS_2000DPS        16.4f
#define ICM42688_GYRO_SENS_1000DPS        32.8f
#define ICM42688_GYRO_SENS_500DPS         65.5f
#define ICM42688_GYRO_SENS_250DPS        131.0f
#define ICM42688_GYRO_SENS_125DPS        262.0f
#define ICM42688_GYRO_SENS_62DPS         524.3f
#define ICM42688_GYRO_SENS_31DPS        1048.6f
#define ICM42688_GYRO_SENS_15DPS        2097.2f

/* -------------------------------------------------------------------------
 * Physical unit conversion factors
 *
 * Accel: raw / sensitivity_LSBg * 9.80665  -> m/s^2
 * Gyro:  raw / sensitivity_LSBdps * (PI/180) -> rad/s
 * ----------------------------------------------------------------------- */

#define ICM42688_G_TO_MS2                  9.80665f
#define ICM42688_DPS_TO_RADS               0.017453292519943f  /* PI/180 */

/* -------------------------------------------------------------------------
 * FIFO packet size
 *
 * 16-byte packet (with accel + gyro + timestamp):
 *   Byte 0-1:  FIFO header + accel data marker
 *   Byte 2-3:  Accel X (16-bit signed, big-endian)
 *   Byte 4-5:  Accel Y
 *   Byte 6-7:  Accel Z
 *   Byte 8-9:  Gyro X
 *   Byte 10-11: Gyro Y
 *   Byte 12-13: Gyro Z
 *   Byte 14-15: Temperature or timestamp (16-bit)
 *
 * Header byte (byte 0):
 *   Bit 7: FIFO_MSG (1 = valid data packet, 0 = empty/error)
 *   Bit 6: HEADER_MSG
 *   Bit 5: ACCEL
 *   Bit 4: GYRO
 *   Bit 3: TMST_FSYNC
 *   Bit 2: ??
 *   Bit 1: ??
 *   Bit 0: ??
 * ----------------------------------------------------------------------- */

#define ICM42688_FIFO_HEADER_ACCEL_GYRO_TS  0x70  /* accel + gyro + timestamp */
#define ICM42688_FIFO_HEADER_ACCEL_GYRO     0x60  /* accel + gyro, no timestamp */
#define ICM42688_FIFO_PACKET_SIZE_6         6     /* accel only or gyro only */
#define ICM42688_FIFO_PACKET_SIZE_14        14    /* accel + gyro */
#define ICM42688_FIFO_PACKET_SIZE_16        16    /* accel + gyro + timestamp */

/* FIFO max depth (2KB / 16 bytes per packet = 128 packets) */

#define ICM42688_FIFO_MAX_PACKETS         128

/* -------------------------------------------------------------------------
 * Timing constants (microseconds)
 * ----------------------------------------------------------------------- */

#define ICM42688_RESET_WAIT_US           1000  /* 1ms after software reset */
#define ICM42688_BOOT_WAIT_US            1000  /* 1ms boot time */
#define ICM42688_WAKEUP_WAIT_US           200  /* 200us accel wake-up delay */

/* -------------------------------------------------------------------------
 * SPI configuration
 * ----------------------------------------------------------------------- */

#define ICM42688_SPI_MAX_FREQ        24000000  /* 24 MHz max SPI clock */
#define ICM42688_SPI_MODE            0         /* CPOL=0, CPHA=0 (Mode 0) */
#define ICM42688_SPI_BITS            8         /* 8 bits per transfer */

/* SPI register access: bit 7 of address byte is R/W flag */

#define ICM42688_SPI_READ            0x80  /* Set bit 7 for read */
#define ICM42688_SPI_WRITE           0x00  /* Clear bit 7 for write */

/* -------------------------------------------------------------------------
 * Utility macros for default VelaSense configuration
 * ----------------------------------------------------------------------- */

#define ICM42688_DEFAULT_ACCEL_FSR    ICM42688_ACCEL_FSR_8G
#define ICM42688_DEFAULT_GYRO_FSR     ICM42688_GYRO_FSR_2000DPS
#define ICM42688_DEFAULT_ACCEL_ODR    ICM42688_ACCEL_ODR_100HZ
#define ICM42688_DEFAULT_GYRO_ODR     ICM42688_GYRO_ODR_100HZ
#define ICM42688_DEFAULT_FIFO_WM      16  /* watermark in packets */
#define ICM42688_DEFAULT_ACCEL_LSB_G  ICM42688_ACCEL_SENS_8G
#define ICM42688_DEFAULT_GYRO_LSB_DPS ICM42688_GYRO_SENS_2000DPS

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Accel full-scale range */

enum icm42688_accel_fsr
{
  ICM42688_ACCEL_RANGE_16G = 0,   /* +/-16g  */
  ICM42688_ACCEL_RANGE_8G,        /* +/-8g   */
  ICM42688_ACCEL_RANGE_4G,        /* +/-4g   */
  ICM42688_ACCEL_RANGE_2G         /* +/-2g   */
};

/* Gyro full-scale range */

enum icm42688_gyro_fsr
{
  ICM42688_GYRO_RANGE_2000DPS = 0, /* +/-2000 dps */
  ICM42688_GYRO_RANGE_1000DPS,     /* +/-1000 dps */
  ICM42688_GYRO_RANGE_500DPS,      /* +/-500 dps  */
  ICM42688_GYRO_RANGE_250DPS,      /* +/-250 dps  */
  ICM42688_GYRO_RANGE_125DPS,      /* +/-125 dps  */
  ICM42688_GYRO_RANGE_62DPS,       /* +/-62.5 dps */
  ICM42688_GYRO_RANGE_31DPS,       /* +/-31.25 dps */
  ICM42688_GYRO_RANGE_15DPS        /* +/-15.625 dps */
};

/* Power mode */

enum icm42688_power_mode
{
  ICM42688_POWER_OFF = 0,          /* Both accel and gyro off */
  ICM42688_POWER_ACCEL_LP,         /* Accel low-power, gyro off */
  ICM42688_POWER_ACCEL_LN,         /* Accel low-noise, gyro off */
  ICM42688_POWER_6AXIS_LP,         /* Both low-power */
  ICM42688_POWER_6AXIS_LN          /* Both low-noise (VelaSense default) */
};

/* FIFO mode */

enum icm42688_fifo_mode
{
  ICM42688_FIFO_BYPASS = 0,        /* FIFO disabled */
  ICM42688_FIFO_STREAM,            /* Stream mode (overwrite old) */
  ICM42688_FIFO_STOP_ON_FULL       /* Stop recording when full */
};

/* Driver configuration */

struct icm42688_config
{
  int      spi_bus;               /* SPI bus number (e.g., 1 for /dev/spi1) */
  int      cs_index;              /* Chip-select index on the SPI bus */
  int      irq_pin;               /* GPIO pin for data-ready interrupt
                                   * (-1 = polling mode) */
  enum icm42688_accel_fsr accel_fsr; /* Accel full-scale range */
  enum icm42688_gyro_fsr  gyro_fsr;  /* Gyro full-scale range */
  uint8_t  accel_odr;             /* Accel ODR register value */
  uint8_t  gyro_odr;              /* Gyro ODR register value */
  uint16_t fifo_watermark;        /* FIFO watermark in packets */
  enum icm42688_fifo_mode fifo_mode; /* FIFO mode */
  bool     enable_fifo;           /* true = use FIFO, false = direct read */
};

/* 3-axis vector (raw 16-bit signed) */

struct icm42688_raw_vec3
{
  int16_t x;
  int16_t y;
  int16_t z;
};

/* 3-axis vector (physical units) */

struct icm42688_vec3
{
  float x;
  float y;
  float z;
};

/* Combined IMU reading */

struct icm42688_reading
{
  struct icm42688_raw_vec3 accel_raw;   /* Raw 16-bit accel counts */
  struct icm42688_raw_vec3 gyro_raw;    /* Raw 16-bit gyro counts */
  struct icm42688_vec3     accel_ms2;   /* Accel in m/s^2 */
  struct icm42688_vec3     gyro_rads;   /* Gyro in rad/s */
  float                    temperature; /* Die temperature in C */
  int64_t                  timestamp;   /* Timestamp in microseconds */
  bool                     valid;       /* true if reading is valid */
};

/* Driver context */

struct icm42688_dev
{
  struct icm42688_config config;
  int      fd;                    /* SPI file descriptor */
  uint8_t  who_am_i;             /* WHO_AM_I register value */
  uint8_t  current_bank;         /* Currently selected register bank */
  float    accel_sensitivity;    /* Accel LSB/g for current FSR */
  float    gyro_sensitivity;     /* Gyro LSB/dps for current FSR */
  int      initialized;          /* 1 if init succeeded */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: icm42688_init
 *
 * Description:
 *   Initialize the ICM-42688-P IMU.
 *   Opens SPI, verifies WHO_AM_I, performs software reset, configures
 *   accel/gyro FSR and ODR, sets up FIFO, and enables data-ready
 *   interrupt on INT1.
 *
 * Input Parameters:
 *   config - Driver configuration
 *   dev    - Driver context to initialize
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int icm42688_init(const struct icm42688_config *config,
                  struct icm42688_dev *dev);

/****************************************************************************
 * Name: icm42688_read_accel
 *
 * Description:
 *   Read accelerometer XYZ directly from data registers.
 *   Converts raw 16-bit values to m/s^2.
 *
 * Input Parameters:
 *   dev     - Driver context
 *   reading - Output reading (accel fields populated)
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int icm42688_read_accel(struct icm42688_dev *dev,
                        struct icm42688_reading *reading);

/****************************************************************************
 * Name: icm42688_read_gyro
 *
 * Description:
 *   Read gyroscope XYZ directly from data registers.
 *   Converts raw 16-bit values to rad/s.
 *
 * Input Parameters:
 *   dev     - Driver context
 *   reading - Output reading (gyro fields populated)
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int icm42688_read_gyro(struct icm42688_dev *dev,
                       struct icm42688_reading *reading);

/****************************************************************************
 * Name: icm42688_read_all
 *
 * Description:
 *   Read both accelerometer and gyroscope, plus temperature.
 *   This is the most efficient single-shot read since all data
 *   registers are contiguous in Bank 0.
 *
 * Input Parameters:
 *   dev     - Driver context
 *   reading - Output reading (all fields populated)
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int icm42688_read_all(struct icm42688_dev *dev,
                      struct icm42688_reading *reading);

/****************************************************************************
 * Name: icm42688_read_fifo
 *
 * Description:
 *   Read all available packets from the hardware FIFO.
 *   Each packet contains accel + gyro (and optionally timestamp).
 *   Parses and converts to physical units.
 *
 * Input Parameters:
 *   dev    - Driver context
 *   buf    - Output array of readings
 *   max    - Maximum number of readings to parse
 *   count  - Output: actual number of readings parsed
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int icm42688_read_fifo(struct icm42688_dev *dev,
                       struct icm42688_reading *buf, int max,
                       int *count);

/****************************************************************************
 * Name: icm42688_data_ready
 *
 * Description:
 *   Check if new data is available by reading INT_STATUS register.
 *   Clears the DATA_RDY flag.
 *
 * Returned Value:
 *   1 if data ready, 0 if not ready, negative errno on error
 *
 ****************************************************************************/

int icm42688_data_ready(struct icm42688_dev *dev);

/****************************************************************************
 * Name: icm42688_set_power_mode
 *
 * Description:
 *   Set the operating power mode (off, low-power, low-noise).
 *
 * Input Parameters:
 *   dev  - Driver context
 *   mode - Desired power mode
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int icm42688_set_power_mode(struct icm42688_dev *dev,
                            enum icm42688_power_mode mode);

/****************************************************************************
 * Name: icm42688_set_accel_fsr
 *
 * Description:
 *   Change accelerometer full-scale range at runtime.
 *
 * Input Parameters:
 *   dev - Driver context
 *   fsr - Desired FSR
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int icm42688_set_accel_fsr(struct icm42688_dev *dev,
                           enum icm42688_accel_fsr fsr);

/****************************************************************************
 * Name: icm42688_set_gyro_fsr
 *
 * Description:
 *   Change gyroscope full-scale range at runtime.
 *
 * Input Parameters:
 *   dev - Driver context
 *   fsr - Desired FSR
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int icm42688_set_gyro_fsr(struct icm42688_dev *dev,
                          enum icm42688_gyro_fsr fsr);

/****************************************************************************
 * Name: icm42688_reset
 *
 * Description:
 *   Perform a software reset of the IMU.
 *   Waits for the reset sequence to complete.
 *
 * Input Parameters:
 *   dev - Driver context
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int icm42688_reset(struct icm42688_dev *dev);

/****************************************************************************
 * Name: icm42688_flush_fifo
 *
 * Description:
 *   Flush the hardware FIFO (discard all buffered data).
 *
 * Input Parameters:
 *   dev - Driver context
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int icm42688_flush_fifo(struct icm42688_dev *dev);

/****************************************************************************
 * Name: icm42688_get_fifo_count
 *
 * Description:
 *   Read the current FIFO byte count.
 *
 * Input Parameters:
 *   dev   - Driver context
 *   count - Output: number of bytes in FIFO
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int icm42688_get_fifo_count(struct icm42688_dev *dev, uint16_t *count);

/****************************************************************************
 * Name: icm42688_close
 *
 * Description:
 *   Power down the sensor and close the SPI connection.
 *
 * Input Parameters:
 *   dev - Driver context
 *
 ****************************************************************************/

void icm42688_close(struct icm42688_dev *dev);

/****************************************************************************
 * Name: icm42688_raw_accel_to_ms2
 *
 * Description:
 *   Convert raw accel counts to m/s^2 for the configured FSR.
 *
 ****************************************************************************/

float icm42688_raw_accel_to_ms2(struct icm42688_dev *dev, int16_t raw);

/****************************************************************************
 * Name: icm42688_raw_gyro_to_rads
 *
 * Description:
 *   Convert raw gyro counts to rad/s for the configured FSR.
 *
 ****************************************************************************/

float icm42688_raw_gyro_to_rads(struct icm42688_dev *dev, int16_t raw);

/****************************************************************************
 * Name: icm42688_raw_to_temp_c
 *
 * Description:
 *   Convert raw temperature value to degrees Celsius.
 *
 ****************************************************************************/

float icm42688_raw_to_temp_c(int16_t raw);

#endif /* __FIRMWARE_DRIVERS_ICM42688_ICM42688_H */
