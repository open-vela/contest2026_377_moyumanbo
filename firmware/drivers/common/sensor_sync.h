/****************************************************************************
 * Unified Sensor Timestamp Synchronization System
 *
 * Provides a common microsecond-resolution time base for all sensors.
 * Uses a hardware timer to ensure drift-free synchronization across
 * PPG, IMU, EDA, and temperature sensor data streams.
 *
 * All sensor drivers must stamp every sample through sensor_sync_get_us()
 * so downstream fusion, correlation, and inference operate on a single
 * timeline regardless of individual sensor clock domains.
 *
 * Features:
 *   - Hardware timer-backed microsecond counter (monotonic)
 *   - Millisecond convenience wrapper
 *   - Sensor-read delay correction (call after bus transaction completes)
 *   - Reference epoch at init for compact 32-bit delta encoding
 *
 ****************************************************************************/

#ifndef __FIRMWARE_DRIVERS_COMMON_SENSOR_SYNC_H
#define __FIRMWARE_DRIVERS_COMMON_SENSOR_SYNC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Conversion helpers */

#define SENSOR_SYNC_US_PER_MS   1000U
#define SENSOR_SYNC_US_PER_SEC  1000000U

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Timestamp correction descriptor for a single bus transaction.
 * Filled in by each sensor driver at init time and reused on every read.
 */

struct sensor_timestamp_corr
{
  int32_t  bus_delay_us;    /* Typical SPI/I2C transaction time in us */
  int32_t  irq_to_read_us;  /* ISR-to-read latency in us (0 if polling) */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: sensor_sync_init
 *
 * Description:
 *   Initialize the hardware timer and establish the reference epoch.
 *   Must be called once before any sensor driver starts sampling.
 *   Safe to call from board bring-up.
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int sensor_sync_init(void);

/****************************************************************************
 * Name: sensor_sync_get_us
 *
 * Description:
 *   Return the current time in microseconds since the reference epoch.
 *   Monotonic, non-wrapping (uint64_t).
 *
 * Returned Value:
 *   Microsecond timestamp.
 *
 ****************************************************************************/

uint64_t sensor_sync_get_us(void);

/****************************************************************************
 * Name: sensor_sync_get_ms
 *
 * Description:
 *   Return the current time in milliseconds since the reference epoch.
 *   Convenience wrapper over sensor_sync_get_us().
 *
 * Returned Value:
 *   Millisecond timestamp.
 *
 ****************************************************************************/

uint64_t sensor_sync_get_ms(void);

/****************************************************************************
 * Name: sensor_sync_correct
 *
 * Description:
 *   Correct a raw timestamp for known bus / ISR delays.
 *   Subtracts the combined delay so the timestamp reflects the actual
 *   instant the physical measurement was captured by the sensor IC.
 *
 * Input Parameters:
 *   raw_us    - Raw timestamp captured at read time (sensor_sync_get_us())
 *   corr      - Delay descriptor for this sensor channel
 *
 * Returned Value:
 *   Corrected microsecond timestamp.
 *
 ****************************************************************************/

uint64_t sensor_sync_correct(uint64_t raw_us,
                             const struct sensor_timestamp_corr *corr);

/****************************************************************************
 * Name: sensor_sync_elapsed_us
 *
 * Description:
 *   Compute the elapsed time between two microsecond timestamps.
 *   Handles the uint64_t wrap-around case (which won't occur in practice
 *   but keeps the logic sound).
 *
 * Input Parameters:
 *   start_us - Earlier timestamp
 *   end_us   - Later timestamp
 *
 * Returned Value:
 *   Elapsed microseconds (end - start).
 *
 ****************************************************************************/

uint64_t sensor_sync_elapsed_us(uint64_t start_us, uint64_t end_us);

#endif /* __FIRMWARE_DRIVERS_COMMON_SENSOR_SYNC_H */
