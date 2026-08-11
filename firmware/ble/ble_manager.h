/****************************************************************************
 * VelaSense BLE Manager
 *
 * Top-level BLE lifecycle manager.  Handles:
 *
 *   - Stack initialization (NuttX Bluetooth host + controller)
 *   - Advertising (connectable, device name "VelaSense-377")
 *   - Connection management (single peripheral connection)
 *   - Pairing and bonding (passkey or Just Works)
 *   - Power management (TX power reduction when RSSI is strong)
 *   - Reconnection with exponential backoff
 *   - Integration with the GATT service and sync protocol
 *
 * The manager owns the BLE task thread that:
 *   1. Processes GATT requests
 *   2. Drives the sync state machine
 *   3. Periodically sends Device Status notifications
 *   4. Handles reconnection logic
 *
 ****************************************************************************/

#ifndef __FIRMWARE_BLE_BLE_MANAGER_H
#define __FIRMWARE_BLE_BLE_MANAGER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Connection parameters — tuned for low-latency event delivery */

#define BLE_CONN_INTERVAL_MIN       6       /* 6 * 1.25ms = 7.5ms */
#define BLE_CONN_INTERVAL_MAX       12      /* 12 * 1.25ms = 15ms */
#define BLE_CONN_SLAVE_LATENCY      0       /* Always responsive */
#define BLE_CONN_SUP_TIMEOUT        400     /* 400 * 10ms = 4s */

/* Advertising parameters */

#define BLE_ADV_INTERVAL_MIN        160     /* 160 * 0.625ms = 100ms */
#define BLE_ADV_INTERVAL_MAX        320     /* 320 * 0.625ms = 200ms */

/* Reconnection backoff */

#define BLE_RECONNECT_MIN_MS        1000    /* 1 second */
#define BLE_RECONNECT_MAX_MS        30000   /* 30 seconds */

/* Power management RSSI threshold */

#define BLE_RSSI_POWER_DOWN_THRESH  (-40)   /* dBm — reduce TX above this */

/* Device status notification interval */

#ifndef CONFIG_BLE_STATUS_INTERVAL_MS
#  define CONFIG_BLE_STATUS_INTERVAL_MS    10000  /* 10 seconds */
#endif

/* Task stack size and priority */

#ifndef CONFIG_BLE_TASK_STACKSIZE
#  define CONFIG_BLE_TASK_STACKSIZE    2048
#endif

#ifndef CONFIG_BLE_TASK_PRIORITY
#  define CONFIG_BLE_TASK_PRIORITY     100
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* BLE manager state */

enum ble_manager_state
{
  BLE_MGR_UNINIT = 0,       /* Not initialized */
  BLE_MGR_IDLE,             /* Initialized, not advertising */
  BLE_MGR_ADVERTISING,      /* Advertising, waiting for connection */
  BLE_MGR_CONNECTED,        /* Client connected */
  BLE_MGR_PAIRED,           /* Paired and encrypted */
  BLE_MGR_SYNCING,          /* Syncing events with phone */
  BLE_MGR_RECONNECTING,     /* Lost connection, retrying */
  BLE_MGR_ERROR             /* Fatal error */
};

/* BLE manager statistics */

struct ble_manager_stats
{
  uint32_t connection_count;      /* Total connections accepted */
  uint32_t disconnection_count;   /* Total disconnections */
  uint32_t pairing_count;         /* Successful pairings */
  uint32_t sync_sessions;         /* Completed sync sessions */
  uint32_t reconnect_attempts;    /* Reconnection attempts */
  int8_t   last_rssi;             /* Last measured RSSI */
  uint8_t  tx_power_dbm;          /* Current TX power */
};

/* BLE manager configuration — from Kconfig */

struct ble_manager_config
{
  uint8_t  tx_power_default;      /* Default TX power in dBm */
  uint8_t  tx_power_low;          /* Reduced TX power in dBm */
  int8_t   rssi_power_threshold;  /* RSSI threshold for power reduction */
  bool     pairing_enabled;       /* Enable pairing/bonding */
  bool     just_works;            /* Use Just Works pairing (no passkey) */
  uint32_t status_interval_ms;    /* Status notification interval */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: ble_manager_init
 *
 * Description:
 *   Initialize the BLE manager.  Sets up the BLE controller and host
 *   stack, registers the GATT service, and initializes the sync module.
 *
 *   Does not start advertising — call ble_manager_start() for that.
 *
 * Input Parameters:
 *   config - BLE configuration (from Kconfig).
 *            If NULL, default Kconfig values are used.
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int ble_manager_init(const struct ble_manager_config *config);

/****************************************************************************
 * Name: ble_manager_start
 *
 * Description:
 *   Start the BLE manager.  Begins advertising and launches the
 *   BLE task thread.
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int ble_manager_start(void);

/****************************************************************************
 * Name: ble_manager_stop
 *
 * Description:
 *   Stop advertising and disconnect any active connection.
 *   The BLE task thread is signaled to exit.
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int ble_manager_stop(void);

/****************************************************************************
 * Name: ble_manager_deinit
 *
 * Description:
 *   Fully shut down the BLE manager.  Stops the task, unregisters
 *   the GATT service, and releases all resources.
 *
 ****************************************************************************/

void ble_manager_deinit(void);

/****************************************************************************
 * Name: ble_manager_get_state
 *
 * Description:
 *   Query the current BLE manager state.
 *
 * Returned Value:
 *   Current state.
 *
 ****************************************************************************/

enum ble_manager_state ble_manager_get_state(void);

/****************************************************************************
 * Name: ble_manager_is_ready
 *
 * Description:
 *   Check if the BLE manager is ready to send event notifications.
 *   This requires a connected and (optionally) paired client.
 *
 * Returned Value:
 *   true if ready, false otherwise.
 *
 ****************************************************************************/

bool ble_manager_is_ready(void);

/****************************************************************************
 * Name: ble_manager_get_stats
 *
 * Description:
 *   Retrieve BLE manager statistics.
 *
 * Input Parameters:
 *   stats - Output: manager statistics.
 *
 ****************************************************************************/

void ble_manager_get_stats(struct ble_manager_stats *stats);

/****************************************************************************
 * Name: ble_manager_send_event
 *
 * Description:
 *   Convenience function: queue an event for sync and attempt to
 *   send it immediately if connected.
 *
 * Input Parameters:
 *   notify - Event notification payload (12 bytes).
 *
 * Returned Value:
 *   Assigned sequence number (always positive).
 *
 ****************************************************************************/

uint32_t ble_manager_send_event(const struct ble_event_notify *notify);

/****************************************************************************
 * Name: ble_manager_request_sync
 *
 * Description:
 *   Request a delta sync with the phone.  The phone should respond
 *   with its last confirmed sequence number.
 *
 * Returned Value:
 *   0 on success, -ENOTCONN if not connected.
 *
 ****************************************************************************/

int ble_manager_request_sync(void);

/****************************************************************************
 * Name: ble_manager_set_power_state
 *
 * Description:
 *   Control the BLE radio power state.
 *
 *   true  = radio active (advertising / connected)
 *   false = radio off (deep sleep)
 *
 * Input Parameters:
 *   active - true to enable the radio, false to disable.
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int ble_manager_set_power_state(bool active);

/****************************************************************************
 * Name: ble_manager_set_tx_power
 *
 * Description:
 *   Manually set the BLE transmit power.
 *
 * Input Parameters:
 *   power_dbm - TX power in dBm (platform-dependent range).
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int ble_manager_set_tx_power(int8_t power_dbm);

/****************************************************************************
 * Name: ble_manager_get_rssi
 *
 * Description:
 *   Read the current RSSI of the active connection.
 *
 * Input Parameters:
 *   rssi - Output: RSSI in dBm.
 *
 * Returned Value:
 *   0 on success, -ENOTCONN if not connected.
 *
 ****************************************************************************/

int ble_manager_get_rssi(int8_t *rssi);

#endif /* __FIRMWARE_BLE_BLE_MANAGER_H */
