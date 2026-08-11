/****************************************************************************
 * VelaSense BLE Manager — Implementation
 *
 * Manages the full BLE lifecycle: stack init, advertising, connection,
 * pairing, power management, reconnection, and integration with the
 * GATT service and sync protocol.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <syslog.h>

#include <nuttx/config.h>
#include <nuttx/bluetooth/bluetooth.h>
#include <nuttx/bluetooth/hci.h>

#include "ble_manager.h"
#include "gatt_service.h"
#include "ble_sync.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Default configuration values (from Kconfig or compile-time) */

#ifndef CONFIG_BLE_TX_POWER_DEFAULT
#  define CONFIG_BLE_TX_POWER_DEFAULT       0    /* dBm */
#endif

#ifndef CONFIG_BLE_TX_POWER_LOW
#  define CONFIG_BLE_TX_POWER_LOW           (-12) /* dBm */
#endif

#ifndef CONFIG_BLE_RSSI_POWER_THRESH
#  define CONFIG_BLE_RSSI_POWER_THRESH      (-40)
#endif

#ifndef CONFIG_BLE_PAIRING_ENABLED
#  define CONFIG_BLE_PAIRING_ENABLED        1
#endif

#ifndef CONFIG_BLE_JUST_WORKS
#  define CONFIG_BLE_JUST_WORKS             1
#endif

#ifndef ARRAY_SIZE
#  define ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Internal manager state */

struct ble_manager_ctx
{
  enum ble_manager_state state;
  struct ble_manager_config config;
  struct ble_manager_stats stats;

  /* Task control */

  pthread_t task_handle;
  bool task_running;
  bool shutdown_requested;

  /* Reconnection */

  uint32_t reconnect_delay_ms;
  uint32_t last_disconnect_ms;

  /* Connection reference */

  struct bt_conn *active_conn;

  /* Timing */

  uint32_t last_status_ms;
  uint32_t last_rssi_check_ms;

  /* Power state */

  bool radio_active;
};

/****************************************************************************
 * Private Functions — Utilities
 ****************************************************************************/

/****************************************************************************
 * Name: get_tick_ms_32
 *
 * Description:
 *   Get current system time as a 32-bit millisecond value.
 *
 ****************************************************************************/

static uint32_t get_tick_ms_32(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/****************************************************************************
 * Private Forward Declarations
 ****************************************************************************/

static void *ble_task_entry(void *arg);
static void handle_connected(struct bt_conn *conn, uint8_t err);
static void handle_disconnected(struct bt_conn *conn, uint8_t reason);
static void handle_security_changed(struct bt_conn *conn,
                                    bt_security_t level, enum bt_security_err err);
static int  start_advertising(void);
static int  stop_advertising(void);
static void update_tx_power(void);
static int  apply_connection_params(struct bt_conn *conn);

/* GATT service callbacks */

static bool cb_event_notify_fill(struct ble_event_notify *out);
static int  cb_event_summary_read(struct ble_event_summary_record *out,
                                  int offset, int max_count);
static int  cb_user_label_write(const struct ble_user_label *label);
static int  cb_config_read(struct ble_config_data *cfg);
static int  cb_config_write(const struct ble_config_data *cfg);
static int  cb_ota_data_write(uint32_t offset,
                              const uint8_t *data, uint16_t data_len);
static int  cb_device_status_read(struct ble_device_status *status);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct ble_manager_ctx g_mgr;

/* GATT callback table */

static const struct ble_gatt_callbacks g_gatt_cbs = {
  .event_notify_fill  = cb_event_notify_fill,
  .event_summary_read = cb_event_summary_read,
  .user_label_write   = cb_user_label_write,
  .config_read        = cb_config_read,
  .config_write       = cb_config_write,
  .ota_data_write     = cb_ota_data_write,
  .device_status_read = cb_device_status_read,
};

/* Connection callbacks (registered with NuttX BT stack) */

static struct bt_conn_cb g_conn_callbacks = {
  .connected           = handle_connected,
  .disconnected        = handle_disconnected,
  .security_changed    = handle_security_changed,
};

/****************************************************************************
 * Private Functions — Connection Management
 ****************************************************************************/

/****************************************************************************
 * Name: handle_connected
 ****************************************************************************/

static void handle_connected(struct bt_conn *conn, uint8_t err)
{
  if (err != 0)
    {
      syslog(LOG_ERR, "BLE mgr: connection failed (err=%u)\n", err);
      return;
    }

  syslog(LOG_INFO, "BLE mgr: connected\n");

  g_mgr.active_conn = conn;
  g_mgr.state       = BLE_MGR_CONNECTED;
  g_mgr.stats.connection_count++;
  g_mgr.reconnect_delay_ms = BLE_RECONNECT_MIN_MS;

  /* Apply preferred connection parameters */

  apply_connection_params(conn);

  /* Request security (pairing) if enabled */

  if (g_mgr.config.pairing_enabled)
    {
      bt_conn_set_security(conn, BT_SECURITY_L2);
    }

  /* Stop advertising while connected */

  stop_advertising();
}

/****************************************************************************
 * Name: handle_disconnected
 ****************************************************************************/

static void handle_disconnected(struct bt_conn *conn, uint8_t reason)
{
  (void)conn;

  syslog(LOG_INFO, "BLE mgr: disconnected (reason=%u)\n", reason);

  g_mgr.active_conn = NULL;
  g_mgr.stats.disconnection_count++;

  /* Notify sync module */

  ble_sync_on_disconnect();

  /* Schedule reconnection */

  g_mgr.state             = BLE_MGR_RECONNECTING;
  g_mgr.last_disconnect_ms = get_tick_ms_32();

  /* Reduce TX power back to default */

  g_mgr.stats.tx_power_dbm = g_mgr.config.tx_power_default;
}

/****************************************************************************
 * Name: handle_security_changed
 ****************************************************************************/

static void handle_security_changed(struct bt_conn *conn,
                                    bt_security_t level,
                                    enum bt_security_err err)
{
  if (err != BT_SECURITY_ERR_SUCCESS)
    {
      syslog(LOG_WARNING, "BLE mgr: security failed (level=%u, err=%u)\n",
             level, err);
      return;
    }

  syslog(LOG_INFO, "BLE mgr: security level %u\n", level);

  if (level >= BT_SECURITY_L2)
    {
      g_mgr.state = BLE_MGR_PAIRED;
      g_mgr.stats.pairing_count++;
    }
}

/****************************************************************************
 * Name: apply_connection_params
 ****************************************************************************/

static int apply_connection_params(struct bt_conn *conn)
{
  struct bt_le_conn_param param = {
    .interval_min = BLE_CONN_INTERVAL_MIN,
    .interval_max = BLE_CONN_INTERVAL_MAX,
    .latency      = BLE_CONN_SLAVE_LATENCY,
    .timeout      = BLE_CONN_SUP_TIMEOUT,
  };

  int ret = bt_conn_le_param_update(conn, &param);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "BLE mgr: param update failed (%d)\n", ret);
    }

  return ret;
}

/****************************************************************************
 * Private Functions — Advertising
 ****************************************************************************/

/****************************************************************************
 * Name: start_advertising
 ****************************************************************************/

static int start_advertising(void)
{
  struct bt_le_adv_param param = {
    .options    = BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_NAME,
    .interval_min = BLE_ADV_INTERVAL_MIN,
    .interval_max = BLE_ADV_INTERVAL_MAX,
  };

  /* Advertise the VelaSense service UUID in the scan response */

  static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS,
                  BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL,
                  VELASENSE_UUID_BASE),
  };

  static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE,
            VELASENSE_DEVICE_NAME,
            VELASENSE_DEVICE_NAME_LEN),
  };

  int ret = bt_le_adv_start(&param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
  if (ret < 0)
    {
      syslog(LOG_ERR, "BLE mgr: advertising start failed (%d)\n", ret);
      return ret;
    }

  g_mgr.state = BLE_MGR_ADVERTISING;
  syslog(LOG_INFO, "BLE mgr: advertising as \"%s\"\n", VELASENSE_DEVICE_NAME);

  return 0;
}

/****************************************************************************
 * Name: stop_advertising
 ****************************************************************************/

static int stop_advertising(void)
{
  int ret = bt_le_adv_stop();
  if (ret < 0 && ret != -EALREADY)
    {
      syslog(LOG_WARNING, "BLE mgr: advertising stop failed (%d)\n", ret);
    }

  return ret;
}

/****************************************************************************
 * Private Functions — Power Management
 ****************************************************************************/

/****************************************************************************
 * Name: update_tx_power
 *
 * Description:
 *   Check RSSI and adjust TX power if RSSI is very strong (device is
 *   close to the phone).  Reduces power to save energy.
 *
 ****************************************************************************/

static void update_tx_power(void)
{
  int8_t rssi;
  int ret;

  if (g_mgr.active_conn == NULL)
    {
      return;
    }

  ret = bt_conn_get_rssi(g_mgr.active_conn, &rssi);
  if (ret < 0)
    {
      return;
    }

  g_mgr.stats.last_rssi = rssi;

  if (rssi > g_mgr.config.rssi_power_threshold)
    {
      if (g_mgr.stats.tx_power_dbm != g_mgr.config.tx_power_low)
        {
          ble_manager_set_tx_power(g_mgr.config.tx_power_low);
        }
    }
  else
    {
      if (g_mgr.stats.tx_power_dbm != g_mgr.config.tx_power_default)
        {
          ble_manager_set_tx_power(g_mgr.config.tx_power_default);
        }
    }
}

/****************************************************************************
 * Private Functions — BLE Task
 ****************************************************************************/

/****************************************************************************
 * Name: ble_task_entry
 *
 * Description:
 *   Main BLE task loop.  Runs at CONFIG_BLE_TASK_PRIORITY and:
 *   - Processes GATT events (handled by the BT stack callbacks)
 *   - Drives the sync state machine
 *   - Periodically sends Device Status notifications
 *   - Handles reconnection with exponential backoff
 *   - Monitors RSSI and adjusts TX power
 *
 ****************************************************************************/

static void *ble_task_entry(void *arg)
{
  uint32_t now;

  (void)arg;

  syslog(LOG_INFO, "BLE mgr: task started\n");

  while (!g_mgr.shutdown_requested)
    {
      now = get_tick_ms_32();

      switch (g_mgr.state)
        {
          case BLE_MGR_UNINIT:
          case BLE_MGR_IDLE:
            usleep(100000);  /* 100ms */
            break;

          case BLE_MGR_ADVERTISING:
            /* Nothing to do — wait for connection callback */
            usleep(100000);
            break;

          case BLE_MGR_CONNECTED:
          case BLE_MGR_PAIRED:
          case BLE_MGR_SYNCING:
            {
              /* Drive the sync state machine */

              enum ble_sync_state sync_st = ble_sync_process();
              if (sync_st == BLE_SYNC_DONE)
                {
                  g_mgr.stats.sync_sessions++;
                  if (g_mgr.state == BLE_MGR_SYNCING)
                    {
                      g_mgr.state = BLE_MGR_PAIRED;
                    }
                }
              else if (sync_st == BLE_SYNC_SENDING ||
                       sync_st == BLE_SYNC_WAIT_ACK)
                {
                  g_mgr.state = BLE_MGR_SYNCING;
                }

              /* Periodic device status notification */

              if (g_mgr.config.status_interval_ms > 0 &&
                  (now - g_mgr.last_status_ms) >=
                  g_mgr.config.status_interval_ms)
                {
                  g_mgr.last_status_ms = now;
                  ble_gatt_notify_status();
                }

              /* Periodic RSSI check (every 5 seconds) */

              if ((now - g_mgr.last_rssi_check_ms) >= 5000)
                {
                  g_mgr.last_rssi_check_ms = now;
                  update_tx_power();
                }

              usleep(10000);  /* 10ms — responsive to sync events */
            }
            break;

          case BLE_MGR_RECONNECTING:
            {
              /* Exponential backoff reconnection */

              uint32_t elapsed = now - g_mgr.last_disconnect_ms;
              if (elapsed >= g_mgr.reconnect_delay_ms)
                {
                  g_mgr.stats.reconnect_attempts++;
                  syslog(LOG_INFO,
                         "BLE mgr: reconnect attempt %u (delay=%ums)\n",
                         g_mgr.stats.reconnect_attempts,
                         g_mgr.reconnect_delay_ms);

                  int ret = start_advertising();
                  if (ret == 0)
                    {
                      g_mgr.state = BLE_MGR_ADVERTISING;
                    }

                  /* Exponential backoff: double the delay, cap at max */

                  g_mgr.reconnect_delay_ms *= 2;
                  if (g_mgr.reconnect_delay_ms > BLE_RECONNECT_MAX_MS)
                    {
                      g_mgr.reconnect_delay_ms = BLE_RECONNECT_MAX_MS;
                    }
                }

              usleep(100000);  /* 100ms */
            }
            break;

          case BLE_MGR_ERROR:
            syslog(LOG_ERR, "BLE mgr: in error state\n");
            usleep(1000000);  /* 1s */
            break;

          default:
            g_mgr.state = BLE_MGR_IDLE;
            break;
        }
    }

  syslog(LOG_INFO, "BLE mgr: task exiting\n");
  g_mgr.task_running = false;
  return NULL;
}

/****************************************************************************
 * Private Functions — GATT Callbacks
 ****************************************************************************/

/****************************************************************************
 * Name: cb_event_notify_fill
 ****************************************************************************/

static bool cb_event_notify_fill(struct ble_event_notify *out)
{
  /* This is called by ble_gatt_notify_event() to get the next event.
   * For sync-driven notifications, the event is sent directly.
   * For real-time push, fill from the event state machine.
   */

  if (out == NULL)
    {
      return false;
    }

  /* Return false — real-time push is handled by ble_manager_send_event()
   * which directly calls ble_gatt_notify_event().  The callback is
   * available for the sync module to override if needed.
   */

  return false;
}

/****************************************************************************
 * Name: cb_event_summary_read
 ****************************************************************************/

static int cb_event_summary_read(struct ble_event_summary_record *out,
                                 int offset, int max_count)
{
  /* This would read confirmed events from persistent storage.
   * For now, return 0 — the event storage module provides this data.
   */

  (void)out;
  (void)offset;
  (void)max_count;

  /* TODO: integrate with event storage module */
  return 0;
}

/****************************************************************************
 * Name: cb_user_label_write
 ****************************************************************************/

static int cb_user_label_write(const struct ble_user_label *label)
{
  if (label == NULL)
    {
      return -EINVAL;
    }

  syslog(LOG_INFO, "BLE: user label seq=%u label=%u\n",
         label->event_seq, label->label);

  /* Forward to the event state machine to confirm/reject the event.
   * TODO: integrate with event_sm_confirm() / event_sm_reject()
   */

  return 0;
}

/****************************************************************************
 * Name: cb_config_read
 ****************************************************************************/

static int cb_config_read(struct ble_config_data *cfg)
{
  if (cfg == NULL)
    {
      return -EINVAL;
    }

  /* Fill with current configuration values.
   * TODO: read from persistent storage or live state.
   */

  cfg->confidence_threshold = 82;     /* 0.82 mapped to 0-100 */
  cfg->event_cooldown       = 300;    /* 5 minutes */
  cfg->sqi_threshold        = 70;     /* 0.70 mapped to 0-100 */
  cfg->sampling_enabled     = 1;
  cfg->reserved             = 0;

  return 0;
}

/****************************************************************************
 * Name: cb_config_write
 ****************************************************************************/

static int cb_config_write(const struct ble_config_data *cfg)
{
  if (cfg == NULL)
    {
      return -EINVAL;
    }

  syslog(LOG_INFO,
         "BLE: config update conf=%u cooldown=%u sqi=%u sampling=%u\n",
         cfg->confidence_threshold, cfg->event_cooldown,
         cfg->sqi_threshold, cfg->sampling_enabled);

  /* TODO: apply configuration to event state machine and sensor manager */

  return 0;
}

/****************************************************************************
 * Name: cb_ota_data_write
 ****************************************************************************/

static int cb_ota_data_write(uint32_t offset,
                             const uint8_t *data, uint16_t data_len)
{
  if (data == NULL || data_len == 0)
    {
      return -EINVAL;
    }

  syslog(LOG_DEBUG, "BLE: OTA chunk offset=%u len=%u\n",
         offset, data_len);

  /* TODO: integrate with OTA flash writer.
   * The OTA module handles:
   *   - Flash erase/write at the given offset
   *   - CRC32 verification on the final chunk
   *   - Reboot on successful completion
   */

  return 0;
}

/****************************************************************************
 * Name: cb_device_status_read
 ****************************************************************************/

static int cb_device_status_read(struct ble_device_status *status)
{
  if (status == NULL)
    {
      return -EINVAL;
    }

  /* Fill with current device status.
   * TODO: integrate with battery gauge, sensor manager, etc.
   */

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  status->battery_pct  = 85;    /* TODO: read from MAX17048 */
  status->uptime_sec   = (uint32_t)now.tv_sec;
  status->event_count  = (uint16_t)ble_sync_get_last_seq();
  status->sqi          = 80;    /* TODO: read from SQI module */
  status->state        = (uint8_t)g_mgr.state;

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ble_manager_init
 ****************************************************************************/

int ble_manager_init(const struct ble_manager_config *config)
{
  int ret;

  memset(&g_mgr, 0, sizeof(g_mgr));
  g_mgr.state = BLE_MGR_UNINIT;

  /* Apply configuration — use defaults if not provided */

  if (config != NULL)
    {
      g_mgr.config = *config;
    }
  else
    {
      g_mgr.config.tx_power_default    = CONFIG_BLE_TX_POWER_DEFAULT;
      g_mgr.config.tx_power_low        = CONFIG_BLE_TX_POWER_LOW;
      g_mgr.config.rssi_power_threshold = CONFIG_BLE_RSSI_POWER_THRESH;
      g_mgr.config.pairing_enabled     = CONFIG_BLE_PAIRING_ENABLED;
      g_mgr.config.just_works          = CONFIG_BLE_JUST_WORKS;
      g_mgr.config.status_interval_ms  = CONFIG_BLE_STATUS_INTERVAL_MS;
    }

  g_mgr.stats.tx_power_dbm = g_mgr.config.tx_power_default;

  /* Initialize the NuttX Bluetooth subsystem */

  ret = bt_initialize();
  if (ret < 0 && ret != -EALREADY)
    {
      syslog(LOG_ERR, "BLE mgr: bt_initialize failed (%d)\n", ret);
      return ret;
    }

  /* Register connection callbacks */

  ret = bt_conn_cb_register(&g_conn_callbacks);
  if (ret < 0)
    {
      syslog(LOG_ERR, "BLE mgr: conn cb register failed (%d)\n", ret);
      return ret;
    }

  /* Initialize the GATT service */

  ret = ble_gatt_service_init(&g_gatt_cbs);
  if (ret < 0)
    {
      syslog(LOG_ERR, "BLE mgr: GATT init failed (%d)\n", ret);
      return ret;
    }

  /* Initialize the sync protocol */

  ret = ble_sync_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "BLE mgr: sync init failed (%d)\n", ret);
      ble_gatt_service_deinit();
      return ret;
    }

  /* Set initial TX power */

  ble_manager_set_tx_power(g_mgr.config.tx_power_default);

  g_mgr.state        = BLE_MGR_IDLE;
  g_mgr.radio_active = true;

  syslog(LOG_INFO, "BLE mgr: initialized (tx=%ddBm, pair=%d)\n",
         g_mgr.config.tx_power_default, g_mgr.config.pairing_enabled);

  return 0;
}

/****************************************************************************
 * Name: ble_manager_start
 ****************************************************************************/

int ble_manager_start(void)
{
  pthread_attr_t attr;
  int ret;

  if (g_mgr.state == BLE_MGR_UNINIT)
    {
      return -EPERM;
    }

  if (g_mgr.task_running)
    {
      return -EALREADY;
    }

  /* Start advertising */

  ret = start_advertising();
  if (ret < 0)
    {
      return ret;
    }

  /* Launch the BLE task thread */

  g_mgr.shutdown_requested = false;
  g_mgr.task_running       = true;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, CONFIG_BLE_TASK_STACKSIZE);

  ret = pthread_create(&g_mgr.task_handle, &attr, ble_task_entry, NULL);
  pthread_attr_destroy(&attr);

  if (ret != 0)
    {
      syslog(LOG_ERR, "BLE mgr: task create failed (%d)\n", ret);
      g_mgr.task_running = false;
      stop_advertising();
      return -ret;
    }

  syslog(LOG_INFO, "BLE mgr: started\n");
  return 0;
}

/****************************************************************************
 * Name: ble_manager_stop
 ****************************************************************************/

int ble_manager_stop(void)
{
  if (!g_mgr.task_running)
    {
      return -EALREADY;
    }

  /* Signal the task to exit */

  g_mgr.shutdown_requested = true;

  /* Disconnect if connected */

  if (g_mgr.active_conn != NULL)
    {
      bt_conn_disconnect(g_mgr.active_conn,
                         BT_HCI_ERR_REMOTE_USER_TERM);
    }

  /* Stop advertising */

  stop_advertising();

  /* Wait for task to exit */

  pthread_join(g_mgr.task_handle, NULL);

  g_mgr.state = BLE_MGR_IDLE;
  syslog(LOG_INFO, "BLE mgr: stopped\n");

  return 0;
}

/****************************************************************************
 * Name: ble_manager_deinit
 ****************************************************************************/

void ble_manager_deinit(void)
{
  if (g_mgr.task_running)
    {
      ble_manager_stop();
    }

  ble_sync_deinit();
  ble_gatt_service_deinit();

  g_mgr.state = BLE_MGR_UNINIT;
  syslog(LOG_INFO, "BLE mgr: deinitialized\n");
}

/****************************************************************************
 * Name: ble_manager_get_state
 ****************************************************************************/

enum ble_manager_state ble_manager_get_state(void)
{
  return g_mgr.state;
}

/****************************************************************************
 * Name: ble_manager_is_ready
 ****************************************************************************/

bool ble_manager_is_ready(void)
{
  return (g_mgr.state == BLE_MGR_CONNECTED ||
          g_mgr.state == BLE_MGR_PAIRED    ||
          g_mgr.state == BLE_MGR_SYNCING) &&
         g_mgr.active_conn != NULL;
}

/****************************************************************************
 * Name: ble_manager_get_stats
 ****************************************************************************/

void ble_manager_get_stats(struct ble_manager_stats *stats)
{
  if (stats != NULL)
    {
      *stats = g_mgr.stats;
    }
}

/****************************************************************************
 * Name: ble_manager_send_event
 ****************************************************************************/

uint32_t ble_manager_send_event(const struct ble_event_notify *notify)
{
  uint32_t seq;

  if (notify == NULL)
    {
      return 0;
    }

  /* Always queue — ensures events survive disconnections */

  seq = ble_sync_queue_event(notify);

  /* If connected, try to send immediately */

  if (ble_gatt_is_connected())
    {
      ble_gatt_notify_event();
    }

  return seq;
}

/****************************************************************************
 * Name: ble_manager_request_sync
 ****************************************************************************/

int ble_manager_request_sync(void)
{
  if (!ble_gatt_is_connected())
    {
      return -ENOTCONN;
    }

  /* The sync is initiated by the phone sending its last_seq via the
   * Config characteristic or a custom sync request.  Here we just
   * signal that we're ready for sync.
   */

  g_mgr.state = BLE_MGR_SYNCING;
  syslog(LOG_INFO, "BLE mgr: sync requested (%u pending)\n",
         ble_sync_get_pending_count());

  return 0;
}

/****************************************************************************
 * Name: ble_manager_set_power_state
 ****************************************************************************/

int ble_manager_set_power_state(bool active)
{
  if (active == g_mgr.radio_active)
    {
      return 0;
    }

  if (active)
    {
      int ret = bt_enable(NULL);
      if (ret < 0 && ret != -EALREADY)
        {
          syslog(LOG_ERR, "BLE mgr: bt_enable failed (%d)\n", ret);
          return ret;
        }

      g_mgr.radio_active = true;
      syslog(LOG_INFO, "BLE mgr: radio ON\n");
    }
  else
    {
      /* Stop everything */

      if (g_mgr.active_conn != NULL)
        {
          bt_conn_disconnect(g_mgr.active_conn,
                             BT_HCI_ERR_REMOTE_POWER_OFF);
        }

      stop_advertising();

      /* Note: bt_disable() is not always available in NuttX.
       * The radio is effectively disabled by stopping advertising
       * and disconnecting.
       */

      g_mgr.radio_active = false;
      g_mgr.state = BLE_MGR_IDLE;
      syslog(LOG_INFO, "BLE mgr: radio OFF\n");
    }

  return 0;
}

/****************************************************************************
 * Name: ble_manager_set_tx_power
 ****************************************************************************/

int ble_manager_set_tx_power(int8_t power_dbm)
{
  /* NuttX BT API for setting TX power — platform specific */

  int ret = bt_set_tx_power(power_dbm);
  if (ret < 0)
    {
      /* Not all platforms support this — log and continue */

      syslog(LOG_DEBUG, "BLE mgr: set_tx_power(%d) not supported\n",
             power_dbm);
    }

  g_mgr.stats.tx_power_dbm = power_dbm;
  return 0;
}

/****************************************************************************
 * Name: ble_manager_get_rssi
 ****************************************************************************/

int ble_manager_get_rssi(int8_t *rssi)
{
  if (g_mgr.active_conn == NULL)
    {
      return -ENOTCONN;
    }

  if (rssi == NULL)
    {
      return -EINVAL;
    }

  int ret = bt_conn_get_rssi(g_mgr.active_conn, rssi);
  if (ret == 0)
    {
      g_mgr.stats.last_rssi = *rssi;
    }

  return ret;
}
