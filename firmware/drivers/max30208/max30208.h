/****************************************************************************
 * MAX30208 Precision Digital Temperature Sensor Driver
 *
 * I2C interface, ±0.1°C accuracy, 16-bit resolution.
 * Designed for wearable skin temperature measurement.
 *
 * Datasheet: MAX30208 Rev 2 (Analog Devices / Maxim Integrated)
 *
 * Key specs:
 *   - Supply: 1.7V - 3.6V
 *   - I2C address: 0x50 (7-bit, fixed)
 *   - Resolution: 16-bit (0.00390625°C/LSB)
 *   - Range: -20°C to +60°C (operating: -40°C to +85°C)
 *   - Accuracy: ±0.1°C (30°C to 50°C)
 *   - Conversion time: ~70ms (one-shot) or continuous mode
 *
 * Usage:
 *   1. max30208_init() — verify sensor, configure
 *   2. max30208_read_temp() — read temperature
 *   3. max30208_set_mode() — continuous or one-shot
 *
 ****************************************************************************/

#ifndef __FIRMWARE_DRIVERS_MAX30208_MAX30208_H
#define __FIRMWARE_DRIVERS_MAX30208_MAX30208_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* I2C address (7-bit) */

#define MAX30208_I2C_ADDR           0x50

/* Register addresses */

#define MAX30208_REG_STATUS         0x00  /* Status register */
#define MAX30208_REG_INT_ENABLE     0x01  /* Interrupt enable */
#define MAX30208_REG_INT_STATUS     0x02  /* Interrupt status */
#define MAX30208_REG_FIFO_WR_PTR    0x04  /* FIFO write pointer */
#define MAX30208_REG_FIFO_RD_PTR    0x05  /* FIFO read pointer */
#define MAX30208_REG_FIFO_OVF_CNT   0x06  /* FIFO overflow counter */
#define MAX30208_REG_FIFO_DATA      0x07  /* FIFO data register */
#define MAX30208_REG_FIFO_CFG       0x08  /* FIFO configuration */
#define MAX30208_REG_SYS_CTRL       0x0C  /* System control */
#define MAX30208_REG_ALARM_HIGH_MSB 0x10  /* Alarm high threshold MSB */
#define MAX30208_REG_ALARM_HIGH_LSB 0x11  /* Alarm high threshold LSB */
#define MAX30208_REG_ALARM_LOW_MSB  0x12  /* Alarm low threshold MSB */
#define MAX30208_REG_ALARM_LOW_LSB  0x13  /* Alarm low threshold LSB */
#define MAX30208_REG_TEMP_SETUP     0x14  /* Temperature sensor setup */
#define MAX30208_REG_PART_ID        0xFF  /* Part ID (0x30 for MAX30208) */

/* Status register bits (0x00) */

#define MAX30208_STATUS_TEMP_RDY    (1 << 0)  /* Temperature data ready */
#define MAX30208_STATUS_FIFO_RDY    (1 << 1)  /* FIFO data ready */
#define MAX30208_STATUS_TEMP_OVF    (1 << 2)  /* Temperature FIFO overflow */
#define MAX30208_STATUS_ALARM_HI    (1 << 3)  /* High alarm triggered */
#define MAX30208_STATUS_ALARM_LO    (1 << 4)  /* Low alarm triggered */

/* Interrupt enable bits (0x01) */

#define MAX30208_INT_EN_TEMP_RDY    (1 << 0)
#define MAX30208_INT_EN_FIFO_RDY    (1 << 1)
#define MAX30208_INT_EN_TEMP_OVF    (1 << 2)
#define MAX30208_INT_EN_ALARM_HI    (1 << 3)
#define MAX30208_INT_EN_ALARM_LO    (1 << 4)

/* System control bits (0x0C) */

#define MAX30208_SYS_CTRL_RESET     (1 << 1)  /* Software reset */
#define MAX30208_SYS_CTRL_SHUTDOWN  (1 << 0)  /* Shutdown mode */

/* Temperature setup register bits (0x14) */

#define MAX30208_TEMP_SETUP_CONV    (1 << 0)  /* Start conversion (one-shot) */
#define MAX30208_TEMP_SETUP_RATE_0  (1 << 1)  /* Conversion rate bit 0 */
#define MAX30208_TEMP_SETUP_RATE_1  (1 << 2)  /* Conversion rate bit 1 */
#define MAX30208_TEMP_SETUP_RATE_2  (1 << 3)  /* Conversion rate bit 2 */

/* Conversion rates */

#define MAX30208_RATE_CONTINUOUS    0x00  /* Continuous mode */
#define MAX30208_RATE_ONE_SHOT      0x01  /* One-shot mode */

/* FIFO configuration bits (0x08) */

#define MAX30208_FIFO_CFG_A_FULL    0x0F  /* FIFO almost full threshold (15) */
#define MAX30208_FIFO_CFG_ROLL      (1 << 4)  /* Roll-over on full */

/* Part ID expected value */

#define MAX30208_PART_ID_VALUE      0x30

/* Temperature conversion constants */

#define MAX30208_RESOLUTION         0.00390625f  /* 1 LSB = 2^-8 °C */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Operating modes */

enum max30208_mode
{
  MAX30208_MODE_SHUTDOWN = 0,   /* Low power shutdown */
  MAX30208_MODE_ONE_SHOT,       /* Single conversion then shutdown */
  MAX30208_MODE_CONTINUOUS      /* Continuous conversion */
};

/* Conversion rate (for continuous mode) */

enum max30208_rate
{
  MAX30208_RATE_0 = 0,  /* 0: continuous, rate determined by setup */
};

/* Driver configuration */

struct max30208_config
{
  int      i2c_bus;     /* I2C bus number (e.g., 1 for /dev/i2c1) */
  uint8_t  addr;        /* I2C address (default 0x50) */
  int      irq_pin;     /* GPIO pin for data-ready interrupt (-1 = polling) */
};

/* Driver context */

struct max30208_dev
{
  struct max30208_config config;
  int      fd;          /* I2C file descriptor */
  float    last_temp;   /* Last read temperature in °C */
  uint8_t  part_id;     /* Part ID read from sensor */
  int      initialized; /* 1 if init succeeded */
};

/* Temperature reading */

struct max30208_reading
{
  float    temperature;   /* Temperature in °C */
  uint16_t raw_adc;       /* Raw 16-bit ADC value */
  int      valid;         /* 1 if reading is valid */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: max30208_init
 *
 * Description:
 *   Initialize the MAX30208 temperature sensor.
 *   Verifies part ID, configures operating mode.
 *
 * Input Parameters:
 *   config - Driver configuration
 *   dev    - Driver context to initialize
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int max30208_init(const struct max30208_config *config,
                  struct max30208_dev *dev);

/****************************************************************************
 * Name: max30208_read_temp
 *
 * Description:
 *   Read the current temperature from the sensor.
 *   In one-shot mode, triggers a conversion and waits for completion.
 *   In continuous mode, reads the latest value from the FIFO.
 *
 * Input Parameters:
 *   dev     - Driver context
 *   reading - Output temperature reading
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int max30208_read_temp(struct max30208_dev *dev,
                       struct max30208_reading *reading);

/****************************************************************************
 * Name: max30208_set_mode
 *
 * Description:
 *   Set the operating mode (shutdown, one-shot, or continuous).
 *
 * Input Parameters:
 *   dev  - Driver context
 *   mode - Desired operating mode
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int max30208_set_mode(struct max30208_dev *dev, enum max30208_mode mode);

/****************************************************************************
 * Name: max30208_read_fifo
 *
 * Description:
 *   Read all available samples from the FIFO.
 *   Useful in continuous mode to batch-read accumulated samples.
 *
 * Input Parameters:
 *   dev    - Driver context
 *   buf    - Output buffer for temperature readings
 *   max    - Maximum number of readings to read
 *   count  - Output: actual number of readings read
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int max30208_read_fifo(struct max30208_dev *dev,
                       struct max30208_reading *buf, int max,
                       int *count);

/****************************************************************************
 * Name: max30208_reset
 *
 * Description:
 *   Perform a software reset of the sensor.
 *
 ****************************************************************************/

int max30208_reset(struct max30208_dev *dev);

/****************************************************************************
 * Name: max30208_get_part_id
 *
 * Description:
 *   Read the part ID register.
 *
 ****************************************************************************/

int max30208_get_part_id(struct max30208_dev *dev, uint8_t *part_id);

/****************************************************************************
 * Name: max30208_enable_interrupts
 *
 * Description:
 *   Enable/disable specific interrupt sources.
 *
 * Input Parameters:
 *   dev    - Driver context
 *   mask   - Interrupt mask (MAX30208_INT_EN_*)
 *   enable - 1 to enable, 0 to disable
 *
 ****************************************************************************/

int max30208_enable_interrupts(struct max30208_dev *dev,
                               uint8_t mask, int enable);

/****************************************************************************
 * Name: max30208_set_alarm
 *
 * Description:
 *   Set high and low temperature alarm thresholds.
 *
 * Input Parameters:
 *   dev    - Driver context
 *   high_c - High threshold in °C
 *   low_c  - Low threshold in °C
 *
 ****************************************************************************/

int max30208_set_alarm(struct max30208_dev *dev,
                       float high_c, float low_c);

/****************************************************************************
 * Name: max30208_close
 *
 * Description:
 *   Close the I2C connection and put sensor in shutdown mode.
 *
 ****************************************************************************/

void max30208_close(struct max30208_dev *dev);

/****************************************************************************
 * Name: max30208_raw_to_celsius
 *
 * Description:
 *   Convert raw 16-bit ADC value to temperature in °C.
 *
 ****************************************************************************/

float max30208_raw_to_celsius(uint16_t raw);

#endif /* __FIRMWARE_DRIVERS_MAX30208_MAX30208_H */
