/****************************************************************************
 * VelaSense Sensor Manager
 *
 * Central orchestrator for the wearable sensor suite.
 *
 * Manages the complete lifecycle of six sensor channels:
 *
 *   Sensor          IC          Bus    Rate      Method
 *   ─────────────────────────────────────────────────────
 *   PPG             MAX86141    SPI    100 Hz    Interrupt
 *   IMU (accel+gyro) ICM42688   SPI    100 Hz    Interrupt
 *   EDA             AD5940      SPI     32 Hz    Interrupt
 *   Temperature     MAX30208    I2C      1 Hz    Polling
 *   Haptic          DRV2605L    I2C    on-demand I2C command
 *   Fuel gauge      MAX17048    I2C     1/min    Polling
 *
 * Responsibilities:
 *   - Unified init / start / stop / close lifecycle
 *   - Configurable per-sensor sampling rates via Kconfig
 *   - Lock-free ring buffers for each high-rate channel
 *   - Unified data access: get_latest() returns the freshest sample
 *   - Health monitoring: SQI gate, sensor timeout detection, watchdog
 *   - Power management: normal / low-power / shutdown states
 *
 ****************************************************************************/

#ifndef __FIRMWARE_DRIVERS_COMMON_SENSOR_MANAGER_H
#define __FIRMWARE_DRIVERS_COMMON_SENSOR_MANAGER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>

#include "ring_buffer.h"
#include "sensor_sync.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Sensor identifiers */

#define SENSOR_ID_PPG       0
#define SENSOR_ID_ACCEL     1
#define SENSOR_ID_GYRO      2
#define SENSOR_ID_EDA       3
#define SENSOR_ID_TEMP      4
#define SENSOR_ID_HAPTIC    5
#define SENSOR_ID_FUEL_GAUGE 6
#define SENSOR_ID_COUNT     7

/* Data type identifiers (within sensor_data.data_type) */

#define DATA_TYPE_RAW_PPG   0
#define DATA_TYPE_ACCEL     1
#define DATA_TYPE_GYRO      2
#define DATA_TYPE_EDA       3
#define DATA_TYPE_TEMP      4
#define DATA_TYPE_HAPTIC    5
#define DATA_TYPE_FUEL_GAUGE 6

/* Default sampling rates (Hz) — override via Kconfig */

#ifndef CONFIG_SENSOR_PPG_RATE
#  define CONFIG_SENSOR_PPG_RATE        100
#endif

#ifndef CONFIG_SENSOR_IMU_RATE
#  define CONFIG_SENSOR_IMU_RATE        100
#endif

#ifndef CONFIG_SENSOR_EDA_RATE
#  define CONFIG_SENSOR_EDA_RATE        32
#endif

#ifndef CONFIG_SENSOR_TEMP_RATE
#  define CONFIG_SENSOR_TEMP_RATE       1
#endif

#ifndef CONFIG_SENSOR_FUEL_GAUGE_RATE
#  define CONFIG_SENSOR_FUEL_GAUGE_RATE  1  /* 1 per 60s, rate=1 means 1/min */
#endif

/* Ring buffer depths — enough to cover DSP windowing without overflow.
 * Must be powers of two.
 */

#ifndef CONFIG_SENSOR_PPG_BUF_DEPTH
#  define CONFIG_SENSOR_PPG_BUF_DEPTH       256
#endif

#ifndef CONFIG_SENSOR_ACCEL_BUF_DEPTH
#  define CONFIG_SENSOR_ACCEL_BUF_DEPTH     256
#endif

#ifndef CONFIG_SENSOR_GYRO_BUF_DEPTH
#  define CONFIG_SENSOR_GYRO_BUF_DEPTH      256
#endif

#ifndef CONFIG_SENSOR_EDA_BUF_DEPTH
#  define CONFIG_SENSOR_EDA_BUF_DEPTH       128
#endif

#ifndef CONFIG_SENSOR_TEMP_BUF_DEPTH
#  define CONFIG_SENSOR_TEMP_BUF_DEPTH      16
#endif

#ifndef CONFIG_SENSOR_FUEL_BUF_DEPTH
#  define CONFIG_SENSOR_FUEL_BUF_DEPTH      8
#endif

/* Health monitoring thresholds */

#define SENSOR_TIMEOUT_MS       2000    /* No data for 2s = unhealthy */
#define SENSOR_SQI_MIN_DEFAULT  0.70f   /* Minimum SQI to consider valid */
#define SENSOR_HEALTH_CHECK_MS  5000    /* Health check interval */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Sensor power states */

enum sensor_power_state
{
  SENSOR_POWER_NORMAL = 0,   /* Full-rate sampling */
  SENSOR_POWER_LOW_POWER,    /* Reduced rate, suspend non-critical */
  SENSOR_POWER_SHUTDOWN      /* All sensors off */
};

/* Sensor health status per channel */

struct sensor_health
{
  bool     is_healthy;         /* True if sensor is responding */
  uint32_t last_sample_ms;     /* Timestamp of last valid sample */
  uint32_t total_samples;      /* Total samples received since start */
  uint32_t error_count;        /* Bus errors since start */
  float    sqi;                /* Signal quality index (PPG only) */
  uint32_t ring_overflow;      /* Ring buffer overflow count */
};

/* Standard sensor data element — fits in 48 bytes (cache-line friendly) */

struct sensor_data
{
  uint64_t timestamp_us;       /* Microsecond timestamp from sensor_sync */
  uint8_t  sensor_id;          /* SENSOR_ID_* */
  uint8_t  data_type;          /* DATA_TYPE_* */
  uint16_t data_len;           /* Actual payload length in bytes */
  union
  {
    struct
    {
      float x, y, z;
    } accel;

    struct
    {
      float x, y, z;
    } gyro;

    struct
    {
      float value;
    } ppg;

    struct
    {
      float value;
    } temp;

    struct
    {
      float value;
    } eda;

    uint8_t raw[32];
  } data;
};

/* Manager configuration — populated from Kconfig and board init */

struct sensor_manager_config
{
  /* SPI bus numbers */

  int spi_ppg;            /* SPI bus for MAX86141 */
  int spi_imu;            /* SPI bus for ICM42688 */
  int spi_eda;            /* SPI bus for AD5940 */

  /* I2C bus numbers */

  int i2c_temp;           /* I2C bus for MAX30208 */
  int i2c_haptic;         /* I2C bus for DRV2605L */
  int i2c_fuel;           /* I2C bus for MAX17048 */

  /* GPIO IRQ pins (-1 = polling) */

  int irq_ppg;            /* MAX86141 data-ready interrupt */
  int irq_imu;            /* ICM42688 data-ready interrupt */
  int irq_eda;            /* AD5940 data-ready interrupt */

  /* Sampling rates (Hz) */

  int rate_ppg;
  int rate_imu;
  int rate_eda;
  int rate_temp;
};

/* Manager context — opaque singleton managed internally */

struct sensor_manager_ctx;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: sensor_manager_init
 *
 * Description:
 *   Initialize the sensor manager and all sensor drivers.
 *   Allocates ring buffers, initializes sensor_sync, opens bus
 *   connections, verifies chip IDs, and configures each sensor.
 *
 * Input Parameters:
 *   config - Board-specific configuration (bus numbers, IRQ pins, rates)
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int sensor_manager_init(const struct sensor_manager_config *config);

/****************************************************************************
 * Name: sensor_manager_start
 *
 * Description:
 *   Start sampling all sensors. Launches the per-sensor sampling
 *   tasks (NuttX kernel threads) that read data and push into
 *   ring buffers.
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int sensor_manager_start(void);

/****************************************************************************
 * Name: sensor_manager_stop
 *
 * Description:
 *   Stop all sensor sampling tasks. Sensors are left in a low-power
 *   state but not fully closed — sensor_manager_start() can be called
 *   again to resume.
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int sensor_manager_stop(void);

/****************************************************************************
 * Name: sensor_manager_close
 *
 * Description:
 *   Shut down all sensors, free resources, and release bus handles.
 *   The manager must be re-initialized before use.
 *
 ****************************************************************************/

void sensor_manager_close(void);

/****************************************************************************
 * Name: sensor_manager_get_latest
 *
 * Description:
 *   Retrieve the most recent sample from the specified sensor's ring
 *   buffer.  If multiple samples are available, only the newest is
 *   returned (older ones are discarded).
 *
 * Input Parameters:
 *   sensor_id - SENSOR_ID_PPG, SENSOR_ID_ACCEL, etc.
 *   data      - Output: latest sensor_data element
 *
 * Returned Value:
 *   0 on success, -EAGAIN if no data available.
 *
 ****************************************************************************/

int sensor_manager_get_latest(uint8_t sensor_id,
                              struct sensor_data *data);

/****************************************************************************
 * Name: sensor_manager_read_bulk
 *
 * Description:
 *   Pop up to max_count samples from the specified sensor's ring buffer.
 *   Intended for DSP processing that operates on windows of data.
 *
 * Input Parameters:
 *   sensor_id  - Sensor identifier
 *   buf        - Output array of sensor_data elements
 *   max_count  - Maximum number of elements to read
 *
 * Returned Value:
 *   Number of elements actually read (0 if buffer empty).
 *
 ****************************************************************************/

int sensor_manager_read_bulk(uint8_t sensor_id,
                             struct sensor_data *buf,
                             uint32_t max_count);

/****************************************************************************
 * Name: sensor_manager_health_check
 *
 * Description:
 *   Check the health of all sensors. Returns a summary of which
 *   sensors are responding and their SQI / error counts.
 *
 * Input Parameters:
 *   health - Array of SENSOR_ID_COUNT entries to fill
 *
 * Returned Value:
 *   Number of unhealthy sensors (0 = all healthy).
 *
 ****************************************************************************/

int sensor_manager_health_check(struct sensor_health *health);

/****************************************************************************
 * Name: sensor_manager_set_power_state
 *
 * Description:
 *   Transition the sensor suite to a new power state.
 *
 *   SENSOR_POWER_NORMAL:
 *     All sensors at configured rates.
 *
 *   SENSOR_POWER_LOW_POWER:
 *     PPG and IMU at half rate, EDA at quarter rate,
 *     temperature and fuel gauge unchanged.
 *
 *   SENSOR_POWER_SHUTDOWN:
 *     All sensors powered down, sampling stopped.
 *
 * Input Parameters:
 *   state - Desired power state
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int sensor_manager_set_power_state(enum sensor_power_state state);

/****************************************************************************
 * Name: sensor_manager_get_power_state
 *
 * Description:
 *   Query the current power state.
 *
 * Returned Value:
 *   Current power state.
 *
 ****************************************************************************/

enum sensor_power_state sensor_manager_get_power_state(void);

/****************************************************************************
 * Name: sensor_manager_trigger_haptic
 *
 * Description:
 *   Trigger a haptic vibration pattern on the DRV2605L.
 *   This is a non-sampling, on-demand actuator command.
 *
 * Input Parameters:
 *   effect_id - DRV2605L effect ID (1-123, see datasheet)
 *   duration_ms - Minimum vibration duration (0 = use effect default)
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int sensor_manager_trigger_haptic(uint8_t effect_id,
                                  uint16_t duration_ms);

/****************************************************************************
 * Name: sensor_manager_is_running
 *
 * Description:
 *   Check if the sensor manager is in the running (sampling) state.
 *
 * Returned Value:
 *   true if sampling is active, false otherwise.
 *
 ****************************************************************************/

bool sensor_manager_is_running(void);

#endif /* __FIRMWARE_DRIVERS_COMMON_SENSOR_MANAGER_H */
