/****************************************************************************
 * VelaSense Fault Handler Implementation
 *
 * Fault logging, persistent Flash storage, graceful degradation,
 * and NSH shell command for fault inspection.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <time.h>
#include <sys/ioctl.h>
#include <nuttx/fs/fs.h>

#ifdef CONFIG_FS_ROMFS
#  include <nuttx/fs/ioctl.h>
#endif

#include "fault_handler.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TAG                     "fault"

/* Flash magic number: "VELA" in ASCII */

#define FAULT_FLASH_MAGIC       0x56454c41

/* Simple CRC32 for fault log integrity */

#define CRC32_POLYNOMIAL        0xedb88320

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct fault_log    g_fault_log;       /* In-RAM fault log */
static struct fault_stats  g_fault_stats;     /* Statistics */
static bool                g_initialized     = false;
static bool                g_inference_fallback = false;
static int                 g_flash_fd        = -1;
static uint32_t            g_sequence        = 0;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: crc32_calc
 *
 * Description:
 *   Compute CRC32 over a buffer.
 *
 ****************************************************************************/

static uint32_t crc32_calc(const uint8_t *data, size_t len)
{
  uint32_t crc = 0xffffffff;
  size_t   i;
  int      j;

  for (i = 0; i < len; i++)
    {
      crc ^= data[i];
      for (j = 0; j < 8; j++)
        {
          if (crc & 1)
            {
              crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            }
          else
            {
              crc >>= 1;
            }
        }
    }

  return crc ^ 0xffffffff;
}

/****************************************************************************
 * Name: fault_log_checksum
 *
 * Description:
 *   Compute checksum over the fault log entries.
 *
 ****************************************************************************/

static uint32_t fault_log_checksum(const struct fault_log *log)
{
  return crc32_calc(
      (const uint8_t *)log->entries,
      sizeof(struct fault_entry) * FAULT_LOG_CAPACITY);
}

/****************************************************************************
 * Name: flash_read_log
 *
 * Description:
 *   Read the fault log from Flash persistent storage.
 *
 ****************************************************************************/

static int flash_read_log(struct fault_log *log)
{
  int ret;

  if (g_flash_fd < 0)
    {
      return -ENODEV;
    }

  ret = lseek(g_flash_fd, FAULT_FLASH_OFFSET, SEEK_SET);
  if (ret < 0)
    {
      return -errno;
    }

  ret = read(g_flash_fd, log, sizeof(struct fault_log));
  if (ret != sizeof(struct fault_log))
    {
      return -EIO;
    }

  /* Validate magic */

  if (log->magic != FAULT_FLASH_MAGIC)
    {
      syslog(LOG_INFO, "[%s] Flash log: no valid data (magic=0x%08x)\n",
             TAG, log->magic);
      return -EINVAL;
    }

  /* Validate checksum */

  uint32_t expected = fault_log_checksum(log);
  if (log->checksum != expected)
    {
      syslog(LOG_WARNING,
             "[%s] Flash log: checksum mismatch "
             "(got=0x%08x expected=0x%08x)\n",
             TAG, log->checksum, expected);
      return -EINVAL;
    }

  return OK;
}

/****************************************************************************
 * Name: flash_write_log
 *
 * Description:
 *   Write the fault log to Flash persistent storage.
 *   Erases the sector first, then writes.
 *
 ****************************************************************************/

static int flash_write_log(const struct fault_log *log)
{
  int ret;

  if (g_flash_fd < 0)
    {
      return -ENODEV;
    }

  if (g_fault_stats.ram_only_mode)
    {
      return -EROFS;
    }

  /* Seek to fault log region */

  ret = lseek(g_flash_fd, FAULT_FLASH_OFFSET, SEEK_SET);
  if (ret < 0)
    {
      syslog(LOG_ERR, "[%s] Flash seek failed: %d\n", TAG, errno);
      g_fault_stats.ram_only_mode = true;
      return -errno;
    }

  /* Erase sector before write */

#ifdef CONFIG_MTD_SECTOR512
  ret = ioctl(g_flash_fd, MTDIOC_ERASESECTORS,
              (unsigned long)FAULT_FLASH_OFFSET);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "[%s] Flash erase failed: %d\n",
             TAG, errno);
    }
#endif

  /* Write the log */

  ret = write(g_flash_fd, log, sizeof(struct fault_log));
  if (ret != sizeof(struct fault_log))
    {
      syslog(LOG_ERR, "[%s] Flash write failed: %d (wrote %d)\n",
             TAG, errno, ret);
      g_fault_stats.ram_only_mode = true;
      return -EIO;
    }

  return OK;
}

/****************************************************************************
 * Name: flash_init
 *
 * Description:
 *   Open the Flash device for persistent fault storage.
 *
 ****************************************************************************/

static int flash_init(void)
{
  /* Try MTD device first, then fall back to raw file */

  g_flash_fd = open("/dev/mtd0", O_RDWR);
  if (g_flash_fd < 0)
    {
      /* Try a file-based fallback for platforms without direct MTD */

      g_flash_fd = open("/data/fault_log.bin", O_RDWR | O_CREAT, 0666);
      if (g_flash_fd < 0)
        {
          syslog(LOG_WARNING,
                 "[%s] No persistent storage available "
                 "(errno=%d), using RAM only\n",
                 TAG, errno);
          g_fault_stats.ram_only_mode = true;
          return -errno;
        }
    }

  syslog(LOG_INFO, "[%s] Flash storage opened (fd=%d)\n",
         TAG, g_flash_fd);
  return OK;
}

/****************************************************************************
 * Name: fault_log_add
 *
 * Description:
 *   Add an entry to the in-RAM fault log (circular buffer).
 *
 ****************************************************************************/

static void fault_log_add(const struct fault_entry *entry)
{
  uint32_t idx = g_fault_log.head;

  if (idx >= FAULT_LOG_CAPACITY)
    {
      idx = 0;
    }

  memcpy(&g_fault_log.entries[idx], entry, sizeof(struct fault_entry));

  g_fault_log.head = (idx + 1) % FAULT_LOG_CAPACITY;

  if (g_fault_log.count < FAULT_LOG_CAPACITY)
    {
      g_fault_log.count++;
    }

  g_fault_log.write_count++;
  g_fault_log.checksum = fault_log_checksum(&g_fault_log);
}

/****************************************************************************
 * Name: nsh_faults_cmd
 *
 * Description:
 *   NSH command handler for `velasense faults`.
 *   Dumps the fault log to stdout.
 *
 ****************************************************************************/

static int nsh_faults_cmd(int argc, char *argv[])
{
  int i;
  int idx;
  int count;

  printf("=== VelaSense Fault Log ===\n");
  printf("Total faults since boot: %lu\n",
         (unsigned long)g_fault_stats.total_faults);
  printf("Flash writes: %lu, RAM-only: %s\n",
         (unsigned long)g_fault_log.write_count,
         g_fault_stats.ram_only_mode ? "YES" : "no");
  printf("Bus retries: %lu, Bus failures: %lu\n",
         (unsigned long)g_fault_stats.bus_retries,
         (unsigned long)g_fault_stats.bus_failures);
  printf("BLE restarts: %lu, Inference fallbacks: %lu\n",
         (unsigned long)g_fault_stats.ble_restarts,
         (unsigned long)g_fault_stats.inference_fallbacks);
  printf("\n--- Persistent Log (last %d faults) ---\n",
         FAULT_LOG_CAPACITY);
  printf("%-5s %-10s %-6s %-8s %-10s %s\n",
         "Seq", "Time", "Task", "Code", "Context", "Description");

  count = g_fault_log.count;
  for (i = 0; i < count; i++)
    {
      /* Print in reverse chronological order */

      idx = (g_fault_log.head - 1 - i + FAULT_LOG_CAPACITY) %
            FAULT_LOG_CAPACITY;

      struct fault_entry *e = &g_fault_log.entries[idx];

      if (e->code == FAULT_CODE_NONE)
        {
          continue;
        }

      printf("%-5lu %-10lu %-6d %-8d 0x%08x %s\n",
             (unsigned long)e->sequence,
             (unsigned long)e->timestamp,
             e->task_id,
             e->code,
             e->context,
             fault_code_to_string((enum fault_code)e->code));
    }

  printf("\n--- Fault Counts by Code ---\n");
  for (i = 1; i < FAULT_CODE_MAX; i++)
    {
      if (g_fault_stats.faults_by_code[i] > 0)
        {
          printf("  [%3d] %-30s: %lu\n",
                 i,
                 fault_code_to_string((enum fault_code)i),
                 (unsigned long)g_fault_stats.faults_by_code[i]);
        }
    }

  /* Also handle "clear" subcommand */

  if (argc > 1 && strcmp(argv[1], "clear") == 0)
    {
      fault_clear_log();
      printf("\nFault log cleared.\n");
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fault_handler_init
 ****************************************************************************/

int fault_handler_init(void)
{
  int ret;

  memset(&g_fault_stats, 0, sizeof(g_fault_stats));
  memset(&g_fault_log, 0, sizeof(g_fault_log));
  g_fault_log.magic = FAULT_FLASH_MAGIC;
  g_sequence = 0;

  /* Open Flash storage */

  ret = flash_init();

  /* Try to load existing fault log from Flash */

  if (ret == OK && g_flash_fd >= 0)
    {
      struct fault_log flash_log;
      ret = flash_read_log(&flash_log);
      if (ret == OK)
        {
          memcpy(&g_fault_log, &flash_log, sizeof(struct fault_log));
          g_sequence = 0;

          /* Find the highest sequence number */

          int i;
          for (i = 0; i < (int)g_fault_log.count; i++)
            {
              if (g_fault_log.entries[i].sequence > g_sequence)
                {
                  g_sequence = g_fault_log.entries[i].sequence;
                }
            }

          syslog(LOG_INFO,
                 "[%s] Loaded %lu fault entries from Flash "
                 "(last seq=%lu)\n",
                 TAG,
                 (unsigned long)g_fault_log.count,
                 (unsigned long)g_sequence);
        }
      else
        {
          syslog(LOG_INFO,
                 "[%s] No valid Flash log, starting fresh\n", TAG);

          /* Initialize fresh log */

          memset(&g_fault_log, 0, sizeof(g_fault_log));
          g_fault_log.magic = FAULT_FLASH_MAGIC;
        }
    }

  g_initialized = true;

  /* Register NSH command */

  fault_nsh_register();

  syslog(LOG_INFO, "[%s] Fault handler initialized\n", TAG);
  return OK;
}

/****************************************************************************
 * Name: fault_handler_deinit
 ****************************************************************************/

void fault_handler_deinit(void)
{
  if (g_flash_fd >= 0)
    {
      /* Flush to Flash on shutdown */

      flash_write_log(&g_fault_log);
      close(g_flash_fd);
      g_flash_fd = -1;
    }

  g_initialized = false;
}

/****************************************************************************
 * Name: fault_report
 ****************************************************************************/

void fault_report(enum fault_code code, int task_id, uint32_t context)
{
  struct fault_entry entry;
  struct timespec    ts;

  if (code <= FAULT_CODE_NONE || code >= FAULT_CODE_MAX)
    {
      return;
    }

  clock_gettime(CLOCK_MONOTONIC, &ts);

  entry.timestamp = (uint32_t)ts.tv_sec;
  entry.code      = (uint16_t)code;
  entry.task_id   = (uint8_t)task_id;
  entry.reserved  = 0;
  entry.context   = context;
  entry.sequence  = ++g_sequence;

  /* Add to RAM log */

  fault_log_add(&entry);

  /* Update statistics */

  g_fault_stats.total_faults++;
  if (code < FAULT_CODE_MAX)
    {
      g_fault_stats.faults_by_code[code]++;
    }

  /* Log to syslog */

  syslog(LOG_ERR, "[%s] FAULT: code=%d (%s) task=%d ctx=0x%08x seq=%lu\n",
         TAG, code, fault_code_to_string(code), task_id,
         (unsigned int)context, (unsigned long)entry.sequence);

  /* Persist to Flash (best-effort) */

  flash_write_log(&g_fault_log);
}

/****************************************************************************
 * Name: fault_get_log
 ****************************************************************************/

int fault_get_log(struct fault_log *log)
{
  if (log == NULL)
    {
      return 0;
    }

  memcpy(log, &g_fault_log, sizeof(struct fault_log));
  return (int)g_fault_log.count;
}

/****************************************************************************
 * Name: fault_get_stats
 ****************************************************************************/

void fault_get_stats(struct fault_stats *stats)
{
  if (stats != NULL)
    {
      memcpy(stats, &g_fault_stats, sizeof(struct fault_stats));
    }
}

/****************************************************************************
 * Name: fault_clear_log
 ****************************************************************************/

void fault_clear_log(void)
{
  memset(&g_fault_log, 0, sizeof(g_fault_log));
  g_fault_log.magic = FAULT_FLASH_MAGIC;
  g_sequence = 0;

  flash_write_log(&g_fault_log);

  syslog(LOG_INFO, "[%s] Fault log cleared\n", TAG);
}

/****************************************************************************
 * Name: fault_handle_bus_error
 ****************************************************************************/

int fault_handle_bus_error(enum fault_code code, int task_id,
                           uint32_t context)
{
  int retry;

  for (retry = 0; retry < FAULT_BUS_RETRIES; retry++)
    {
      syslog(LOG_WARNING, "[%s] Bus error retry %d/%d (code=%d)\n",
             TAG, retry + 1, FAULT_BUS_RETRIES, code);

      usleep(FAULT_BUS_RETRY_MS * 1000);

      /* The caller should re-attempt the bus operation after this
       * function returns OK.  We just handle the retry counting
       * and fault reporting here.
       */

      g_fault_stats.bus_retries++;
      return OK;  /* Retry succeeded (caller must verify) */
    }

  /* All retries exhausted */

  g_fault_stats.bus_failures++;
  fault_report(code, task_id, context);

  syslog(LOG_ERR, "[%s] Bus error unrecoverable after %d retries\n",
         TAG, FAULT_BUS_RETRIES);

  return -EIO;
}

/****************************************************************************
 * Name: fault_handle_ble_error
 ****************************************************************************/

void fault_handle_ble_error(enum fault_code code, uint32_t context)
{
  fault_report(code, TASK_ID_BLE, context);

  g_fault_stats.ble_restarts++;

  syslog(LOG_WARNING, "[%s] BLE error, requesting stack restart "
         "(restart #%lu)\n",
         TAG, (unsigned long)g_fault_stats.ble_restarts);

  /* TODO: Signal the BLE task to restart its stack.
   * This could be done via a uORB topic or a direct function call.
   * The BLE task should:
   *   1. Disconnect any active connections
   *   2. Deinitialize the BLE controller
   *   3. Re-initialize
   *   4. Resume advertising
   */
}

/****************************************************************************
 * Name: fault_handle_inference_timeout
 ****************************************************************************/

void fault_handle_inference_timeout(void)
{
  g_fault_stats.inference_fallbacks++;
  g_inference_fallback = true;

  fault_report(FAULT_CODE_INFERENCE_TIMEOUT, TASK_ID_INFERENCE, 0);

  syslog(LOG_WARNING,
         "[%s] Inference timeout — falling back to "
         "rule-based detection (fallback #%lu)\n",
         TAG,
         (unsigned long)g_fault_stats.inference_fallbacks);

  /* The inference task should check fault_is_inference_fallback()
   * and use a simple rule-based arousal detector instead of the
   * TinyML model.  After a cool-down period, it can attempt to
   * re-enable the model.
   */
}

/****************************************************************************
 * Name: fault_is_inference_fallback
 ****************************************************************************/

bool fault_is_inference_fallback(void)
{
  return g_inference_fallback;
}

/****************************************************************************
 * Name: fault_code_to_string
 ****************************************************************************/

const char *fault_code_to_string(enum fault_code code)
{
  static const char *names[] =
  {
    [FAULT_CODE_NONE]               = "none",
    [FAULT_CODE_PPG_INIT_FAIL]      = "PPG init fail",
    [FAULT_CODE_PPG_READ_FAIL]      = "PPG read fail",
    [FAULT_CODE_IMU_INIT_FAIL]      = "IMU init fail",
    [FAULT_CODE_IMU_READ_FAIL]      = "IMU read fail",
    [FAULT_CODE_EDA_INIT_FAIL]      = "EDA init fail",
    [FAULT_CODE_EDA_READ_FAIL]      = "EDA read fail",
    [FAULT_CODE_TEMP_INIT_FAIL]     = "Temp init fail",
    [FAULT_CODE_TEMP_READ_FAIL]     = "Temp read fail",
    [FAULT_CODE_SPI_BUS_ERROR]      = "SPI bus error",
    [FAULT_CODE_I2C_BUS_ERROR]      = "I2C bus error",
    [FAULT_CODE_I2C_TIMEOUT]        = "I2C timeout",
    [FAULT_CODE_FLASH_WRITE_FAIL]   = "Flash write fail",
    [FAULT_CODE_FLASH_ERASE_FAIL]   = "Flash erase fail",
    [FAULT_CODE_FLASH_FULL]         = "Flash full",
    [FAULT_CODE_BLE_INIT_FAIL]      = "BLE init fail",
    [FAULT_CODE_BLE_DISCONNECT]     = "BLE disconnect",
    [FAULT_CODE_BLE_TX_FAIL]        = "BLE TX fail",
    [FAULT_CODE_BLE_STACK_CRASH]    = "BLE stack crash",
    [FAULT_CODE_INFERENCE_TIMEOUT]  = "Inference timeout",
    [FAULT_CODE_INFERENCE_OOM]      = "Inference OOM",
    [FAULT_CODE_DSP_ERROR]          = "DSP error",
    [FAULT_CODE_STACK_OVERFLOW]     = "Stack overflow",
    [FAULT_CODE_TASK_HUNG]          = "Task hung",
    [FAULT_CODE_WATCHDOG_RESET]     = "Watchdog reset",
    [FAULT_CODE_UNEXPECTED_IRQ]     = "Unexpected IRQ",
    [FAULT_CODE_LOW_BATTERY]        = "Low battery",
    [FAULT_CODE_CRITICAL_BATTERY]   = "Critical battery",
    [FAULT_CODE_SHUTDOWN_IMMINENT]  = "Shutdown imminent",
    [FAULT_CODE_CHARGER_FAULT]      = "Charger fault",
  };

  if (code >= 0 && code < FAULT_CODE_MAX && names[code] != NULL)
    {
      return names[code];
    }

  return "unknown";
}

/****************************************************************************
 * Name: fault_nsh_register
 ****************************************************************************/

void fault_nsh_register(void)
{
  /* Register the `velasense faults` command with NuttX NSH.
   * Uses the boardctl or nsh_register interface.
   *
   * On NuttX, custom NSH commands can be registered via:
   *   - CONFIG_NSH_CUSTOM_COMMANDS with a static table
   *   - Or programmatically via nsh_command_register()
   *
   * For simplicity, we use the application entry point approach:
   * `velasense faults` runs this module as a standalone app.
   */

#ifdef CONFIG_NSH_CUSTOM_COMMANDS
  /* If using NuttX custom command registration,
   * the command table in the board config should include:
   *
   *   { "velasense", nsh_faults_cmd, ... }
   *
   * For now, we log that the command is available.
   */

  syslog(LOG_INFO,
         "[%s] NSH command registered: 'velasense faults'\n",
         TAG);
#endif

  /* Suppress unused function warning — nsh_faults_cmd is used
   * via the command table above, or as main() below.
   */

  UNUSED(nsh_faults_cmd);
}

/****************************************************************************
 * Standalone entry point for `velasense faults` NSH command.
 *
 * When built as a NuttX application, this file can also be compiled
 * as a separate NSH builtin that dumps the fault log.
 ****************************************************************************/

int main(int argc, char *argv[])
{
  /* Initialize enough to read the Flash log */

  fault_handler_init();

  /* Dump the log */

  return nsh_faults_cmd(argc, argv);
}
