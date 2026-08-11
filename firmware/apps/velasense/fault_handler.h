/****************************************************************************
 * VelaSense Fault Handler
 *
 * Graceful degradation and fault logging for the VelaSense system.
 *
 * Fault codes cover sensor failures, bus errors, storage errors,
 * BLE failures, inference timeouts, and stack overflows.  Each
 * fault is logged with a timestamp, code, task ID, and context
 * word.  The last 10 faults survive a system reset (stored in
 * a dedicated Flash sector).
 *
 * Degradation strategy:
 *   - Sensor failure: continue with remaining sensors
 *   - SPI/I2C bus error: retry 3 times, then disable sensor
 *   - Flash write error: switch to RAM-only logging
 *   - BLE error: restart BLE stack
 *   - Inference timeout: fall back to rule-based detection
 *   - Stack overflow: log and reset
 *
 * NSH shell command: `velasense faults` dumps the fault log.
 ****************************************************************************/

#ifndef __FIRMWARE_APPS_VELASENSE_FAULT_HANDLER_H
#define __FIRMWARE_APPS_VELASENSE_FAULT_HANDLER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Task IDs shared across modules.
 * Used by the watchdog for health monitoring and by the fault handler
 * for logging which task reported the fault.
 */

#define TASK_ID_MAIN            0
#define TASK_ID_SENSOR          1
#define TASK_ID_DSP             2
#define TASK_ID_INFERENCE       3
#define TASK_ID_UI              4
#define TASK_ID_BLE             5
#define TASK_ID_POWER           6
#define TASK_ID_COUNT           7

/* Fault log capacity (survives reset in Flash) */

#define FAULT_LOG_CAPACITY      10

/* Bus retry configuration */

#define FAULT_BUS_RETRIES       3
#define FAULT_BUS_RETRY_MS      10

/* Flash sector for persistent fault log.
 * Board-specific; adjust for your target.
 */

#define FAULT_FLASH_OFFSET      0x1f0000   /* 2MB - 64KB */
#define FAULT_FLASH_SIZE        4096       /* One sector */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Fault codes */

enum fault_code
{
  FAULT_CODE_NONE = 0,

  /* Sensor faults (1-9) */

  FAULT_CODE_PPG_INIT_FAIL = 1,       /* PPG sensor init failed */
  FAULT_CODE_PPG_READ_FAIL,           /* PPG read error */
  FAULT_CODE_IMU_INIT_FAIL,           /* IMU init failed */
  FAULT_CODE_IMU_READ_FAIL,           /* IMU read error */
  FAULT_CODE_EDA_INIT_FAIL,           /* EDA init failed */
  FAULT_CODE_EDA_READ_FAIL,           /* EDA read error */
  FAULT_CODE_TEMP_INIT_FAIL,          /* Temperature sensor init failed */
  FAULT_CODE_TEMP_READ_FAIL,          /* Temperature read error */

  /* Bus faults (10-19) */

  FAULT_CODE_SPI_BUS_ERROR = 10,      /* SPI bus error */
  FAULT_CODE_I2C_BUS_ERROR,           /* I2C bus error */
  FAULT_CODE_I2C_TIMEOUT,             /* I2C transaction timeout */

  /* Storage faults (20-29) */

  FAULT_CODE_FLASH_WRITE_FAIL = 20,   /* Flash write error */
  FAULT_CODE_FLASH_ERASE_FAIL,        /* Flash erase error */
  FAULT_CODE_FLASH_FULL,              /* Flash storage full */

  /* Communication faults (30-39) */

  FAULT_CODE_BLE_INIT_FAIL = 30,      /* BLE stack init failed */
  FAULT_CODE_BLE_DISCONNECT,          /* Unexpected BLE disconnect */
  FAULT_CODE_BLE_TX_FAIL,             /* BLE transmit failed */
  FAULT_CODE_BLE_STACK_CRASH,         /* BLE stack crash */

  /* Processing faults (40-49) */

  FAULT_CODE_INFERENCE_TIMEOUT = 40,  /* Model inference timeout */
  FAULT_CODE_INFERENCE_OOM,           /* Out of memory during inference */
  FAULT_CODE_DSP_ERROR,               /* DSP processing error */

  /* System faults (50-59) */

  FAULT_CODE_STACK_OVERFLOW = 50,     /* Stack overflow detected */
  FAULT_CODE_TASK_HUNG,               /* Task not responding */
  FAULT_CODE_WATCHDOG_RESET,          /* Watchdog triggered reset */
  FAULT_CODE_UNEXPECTED_IRQ,          /* Unexpected interrupt */

  /* Power faults (60-69) */

  FAULT_CODE_LOW_BATTERY = 60,        /* Battery below 20% */
  FAULT_CODE_CRITICAL_BATTERY,        /* Battery below 5% */
  FAULT_CODE_SHUTDOWN_IMMINENT,       /* Battery below 2%, shutting down */
  FAULT_CODE_CHARGER_FAULT,           /* Charger error */

  FAULT_CODE_MAX
};

/* Fault log entry */

struct fault_entry
{
  uint32_t    timestamp;       /* Seconds since boot (CLOCK_MONOTONIC) */
  uint16_t    code;            /* fault_code enum */
  uint8_t     task_id;         /* TASK_ID_* from watchdog.h */
  uint8_t     reserved;        /* Alignment padding */
  uint32_t    context;         /* Fault-specific context (e.g., errno) */
  uint32_t    sequence;        /* Monotonic sequence number */
};

/* Persistent fault log (Flash image) */

struct fault_log
{
  uint32_t               magic;                            /* 0x56454c41 "VELA" */
  uint32_t               write_count;                      /* Total writes to Flash */
  uint32_t               head;                             /* Next write index */
  uint32_t               count;                            /* Valid entries */
  struct fault_entry     entries[FAULT_LOG_CAPACITY];      /* Circular buffer */
  uint32_t               checksum;                         /* CRC32 of entries */
};

/* Fault handler statistics */

struct fault_stats
{
  uint32_t    total_faults;             /* Total faults since boot */
  uint32_t    faults_by_code[FAULT_CODE_MAX]; /* Per-code count */
  uint32_t    bus_retries;              /* Successful bus retries */
  uint32_t    bus_failures;             /* Unrecoverable bus errors */
  uint32_t    ble_restarts;             /* BLE stack restarts */
  uint32_t    inference_fallbacks;      /* Times fell back to rules */
  bool        ram_only_mode;            /* Flash errors → RAM only */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: fault_handler_init
 *
 * Description:
 *   Initialize the fault handler.  Loads the persistent fault log
 *   from Flash (if valid) and prepares the in-RAM fault buffer.
 *   Must be called once before any other fault_* function.
 *
 * Returned Value:
 *   OK on success, negative errno on failure.
 *
 ****************************************************************************/

int fault_handler_init(void);

/****************************************************************************
 * Name: fault_handler_deinit
 *
 * Description:
 *   Flush any pending fault entries to Flash and release resources.
 *
 ****************************************************************************/

void fault_handler_deinit(void);

/****************************************************************************
 * Name: fault_report
 *
 * Description:
 *   Report a fault.  Logs the fault with timestamp, code, task ID,
 *   and context.  Also stores to persistent Flash log if available.
 *
 * Input Parameters:
 *   code    - Fault code (enum fault_code).
 *   task_id - TASK_ID_* of the reporting task.
 *   context - Fault-specific context word (errno, register value, etc.).
 *
 ****************************************************************************/

void fault_report(enum fault_code code, int task_id, uint32_t context);

/****************************************************************************
 * Name: fault_get_log
 *
 * Description:
 *   Copy the persistent fault log into a caller-provided buffer.
 *
 * Output Parameters:
 *   log    - Filled with fault log data.
 *
 * Returned Value:
 *   Number of valid entries in the log.
 *
 ****************************************************************************/

int fault_get_log(struct fault_log *log);

/****************************************************************************
 * Name: fault_get_stats
 *
 * Description:
 *   Return fault handler statistics.
 *
 * Output Parameters:
 *   stats - Filled with current statistics.
 *
 ****************************************************************************/

void fault_get_stats(struct fault_stats *stats);

/****************************************************************************
 * Name: fault_clear_log
 *
 * Description:
 *   Clear the persistent fault log in Flash.
 *
 ****************************************************************************/

void fault_clear_log(void);

/****************************************************************************
 * Name: fault_handle_bus_error
 *
 * Description:
 *   Handle a sensor bus error with retry logic.  Retries the
 *   operation up to FAULT_BUS_RETRIES times with a short delay.
 *   If all retries fail, reports the fault and returns an error.
 *
 * Input Parameters:
 *   fault_code - The specific bus fault code.
 *   task_id    - The reporting task.
 *   context    - Error context (e.g., errno).
 *
 * Returned Value:
 *   OK if retry succeeded, -EIO if all retries failed.
 *
 ****************************************************************************/

int fault_handle_bus_error(enum fault_code code, int task_id,
                           uint32_t context);

/****************************************************************************
 * Name: fault_handle_ble_error
 *
 * Description:
 *   Handle a BLE error by requesting a BLE stack restart.
 *
 * Input Parameters:
 *   code     - The specific BLE fault code.
 *   context  - Error context.
 *
 ****************************************************************************/

void fault_handle_ble_error(enum fault_code code, uint32_t context);

/****************************************************************************
 * Name: fault_handle_inference_timeout
 *
 * Description:
 *   Handle an inference timeout by falling back to rule-based
 *   detection mode.
 *
 ****************************************************************************/

void fault_handle_inference_timeout(void);

/****************************************************************************
 * Name: fault_is_inference_fallback
 *
 * Description:
 *   Query whether the system is in inference fallback mode
 * * (rule-based detection due to model failures).
 *
 * Returned Value:
 *   true if in fallback mode.
 *
 ****************************************************************************/

bool fault_is_inference_fallback(void);

/****************************************************************************
 * Name: fault_nsh_register
 *
 * Description:
 *   Register the `velasense faults` NSH shell command.
 *   Call once during system initialization.
 *
 ****************************************************************************/

void fault_nsh_register(void);

/****************************************************************************
 * Name: fault_code_to_string
 *
 * Description:
 *   Convert a fault code to a human-readable string.
 *
 ****************************************************************************/

const char *fault_code_to_string(enum fault_code code);

#endif /* __FIRMWARE_APPS_VELASENSE_FAULT_HANDLER_H */
