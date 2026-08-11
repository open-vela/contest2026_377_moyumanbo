/****************************************************************************
 * VelaSense BLE GATT Service
 *
 * Defines the custom GATT service and characteristics for the VelaSense
 * wearable emotion-arousal detection system.  This is the BLE data plane:
 * events, configuration, OTA, and device status flow through these
 * characteristics.
 *
 * Service UUID: 128-bit custom base + offset for each characteristic
 *
 * Characteristics:
 *
 *   Name            UUID+Offset  Properties          Size    Description
 *   ─────────────────────────────────────────────────────────────────────
 *   Event Notify    0x0001       Notify              12 B    Real-time push
 *   Event Summary   0x0002       Read (paginated)    var     Confirmed list
 *   User Label      0x0003       Write               5 B     Label confirm
 *   Config          0x0004       Read / Write        6 B     Device config
 *   OTA Data        0x0005       Write               var     FW update data
 *   Device Status   0x0006       Read / Notify       9 B     Status report
 *
 * All multi-byte fields are little-endian on the wire.
 *
 ****************************************************************************/

#ifndef __FIRMWARE_BLE_GATT_SERVICE_H
#define __FIRMWARE_BLE_GATT_SERVICE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* 128-bit base UUID for VelaSense service.
 * Full UUID: 12345678-1234-5678-9abc-VELASENSE00xx
 * Stored in BT stack byte order (little-endian).
 */

#define VELASENSE_UUID_BASE \
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x56, 0x45, 0x4c, 0x41, 0x53, 0x45 }

/* Offsets from the 128-bit base for each characteristic.
 * These fill the first two bytes of the base UUID (little-endian).
 */

#define VELASENSE_UUID_SERVICE          0x0000
#define VELASENSE_UUID_EVENT_NOTIFY     0x0001
#define VELASENSE_UUID_EVENT_SUMMARY    0x0002
#define VELASENSE_UUID_USER_LABEL       0x0003
#define VELASENSE_UUID_CONFIG           0x0004
#define VELASENSE_UUID_OTA_DATA         0x0005
#define VELASENSE_UUID_DEVICE_STATUS    0x0006

/* Event Notify payload — 12 bytes (see format above) */

#define VELASENSE_EVENT_NOTIFY_LEN      12

/* User Label payload — 5 bytes: event_seq(4) + label(1) */

#define VELASENSE_USER_LABEL_LEN        5

/* Config payload — 6 bytes */

#define VELASENSE_CONFIG_LEN            6

/* Device Status payload — 9 bytes */

#define VELASENSE_STATUS_LEN            9

/* OTA Data header — offset(4) + data_len(2), before the actual data */

#define VELASENSE_OTA_HEADER_LEN        6

/* Maximum OTA data chunk size (ATT MTU minus overhead).
 * Conservative default; negotiated MTU may allow larger.
 */

#define VELASENSE_OTA_CHUNK_MAX         244

/* Event Summary: maximum events per read (pagination) */

#define VELASENSE_SUMMARY_PAGE_MAX      20

/* Event Summary record size — 16 bytes per event */

#define VELASENSE_SUMMARY_RECORD_LEN    16

/* Device name for advertising */

#define VELASENSE_DEVICE_NAME           "VelaSense-377"
#define VELASENSE_DEVICE_NAME_LEN       (sizeof(VELASENSE_DEVICE_NAME) - 1)

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Event Notify payload — pushed on every arousal detection.
 * Wire format (12 bytes, little-endian):
 */

struct __attribute__((packed)) ble_event_notify
{
  uint32_t timestamp;       /* Seconds since boot (compact) */
  uint8_t  label;           /* event_label enum value */
  uint8_t  confidence;      /* 0-100 (uint8, mapped from float) */
  uint16_t reason_flags;    /* Bitmask of event_reason flags */
  uint16_t hr;              /* Heart rate * 10 (0.1 BPM resolution) */
  uint16_t rmssd;           /* RMSSD * 10 (0.1 ms resolution) */
};

/* User Label write payload — phone sends confirmation for a pending event.
 * Wire format (5 bytes):
 */

struct __attribute__((packed)) ble_user_label
{
  uint32_t event_seq;       /* Sequence number of the event to label */
  uint8_t  label;           /* event_label enum value */
};

/* Config characteristic payload — bidirectional.
 * Wire format (6 bytes):
 */

struct __attribute__((packed)) ble_config_data
{
  uint8_t  confidence_threshold;  /* 0-100 (mapped from float) */
  uint16_t event_cooldown;        /* Cooldown in seconds */
  uint8_t  sqi_threshold;         /* 0-100 (mapped from float) */
  uint8_t  sampling_enabled;      /* 1 = sensors active, 0 = paused */
  uint8_t  reserved;              /* Alignment / future use */
};

/* OTA Data write payload.
 * Wire format: header(6) + data(N):
 */

struct __attribute__((packed)) ble_ota_header
{
  uint32_t offset;          /* Byte offset into the firmware image */
  uint16_t data_len;        /* Length of the data[] that follows */
  /* uint8_t data[]; */     /* Variable-length data follows */
};

/* Device Status payload — readable and notify-capable.
 * Wire format (9 bytes):
 */

struct __attribute__((packed)) ble_device_status
{
  uint8_t  battery_pct;     /* Battery percentage 0-100 */
  uint32_t uptime_sec;      /* Uptime in seconds since boot */
  uint16_t event_count;     /* Total confirmed events */
  uint8_t  sqi;             /* Signal quality 0-100 */
  uint8_t  state;           /* event_state enum value */
};

/* Event Summary record — one entry in the paginated read.
 * Wire format (16 bytes):
 */

struct __attribute__((packed)) ble_event_summary_record
{
  uint32_t seq;             /* Event sequence number */
  uint32_t timestamp;       /* Seconds since boot */
  uint8_t  label;           /* event_label enum value */
  uint8_t  confidence;      /* 0-100 */
  uint16_t reason_flags;    /* Bitmask of event_reason flags */
  uint16_t hr;              /* HR * 10 */
  uint16_t rmssd;           /* RMSSD * 10 */
};

/* GATT service callback interface — implemented by the application
 * layer to handle characteristic read/write requests.
 */

struct ble_gatt_callbacks
{
  /* Event Notify: called when the stack is ready to send a notification.
   * Must fill in the payload or return false to skip.
   */

  bool (*event_notify_fill)(struct ble_event_notify *out);

  /* Event Summary: called to fill a page of summary records.
   * offset is the starting record index.
   * Must return the number of records written (0 = no more).
   */

  int  (*event_summary_read)(struct ble_event_summary_record *out,
                             int offset, int max_count);

  /* User Label: called when the phone writes a label confirmation.
   * Returns 0 on success, negative errno on failure.
   */

  int  (*user_label_write)(const struct ble_user_label *label);

  /* Config Read: called to fill the current configuration. */

  int  (*config_read)(struct ble_config_data *cfg);

  /* Config Write: called when the phone updates configuration. */

  int  (*config_write)(const struct ble_config_data *cfg);

  /* OTA Data: called when a firmware chunk arrives.
   * offset + data_len will not exceed the firmware image size.
   * Returns 0 to acknowledge, negative errno to NAK.
   */

  int  (*ota_data_write)(uint32_t offset,
                         const uint8_t *data,
                         uint16_t data_len);

  /* Device Status: called to fill the current status. */

  int  (*device_status_read)(struct ble_device_status *status);
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: ble_gatt_service_init
 *
 * Description:
 *   Initialize the VelaSense GATT service.  Registers all
 *   characteristics with the NuttX BLE stack.
 *
 *   Must be called before ble_manager_start().
 *
 * Input Parameters:
 *   cbs - Callback table (application layer handles read/write logic).
 *         Must remain valid for the lifetime of the service.
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int ble_gatt_service_init(const struct ble_gatt_callbacks *cbs);

/****************************************************************************
 * Name: ble_gatt_service_deinit
 *
 * Description:
 *   Unregister the GATT service and release resources.
 *
 ****************************************************************************/

void ble_gatt_service_deinit(void);

/****************************************************************************
 * Name: ble_gatt_notify_event
 *
 * Description:
 *   Send an Event Notify notification to the connected client.
 *   This is a convenience wrapper that calls the event_notify_fill
 *   callback and sends the notification if a client is subscribed.
 *
 * Returned Value:
 *   0 on success, -ENOTCONN if no client subscribed, negative errno
 *   on other failure.
 *
 ****************************************************************************/

int ble_gatt_notify_event(void);

/****************************************************************************
 * Name: ble_gatt_notify_status
 *
 * Description:
 *   Send a Device Status notification to the connected client.
 *   Only sent if the client has enabled notifications.
 *
 * Returned Value:
 *   0 on success, -ENOTCONN if no client subscribed.
 *
 ****************************************************************************/

int ble_gatt_notify_status(void);

/****************************************************************************
 * Name: ble_gatt_is_connected
 *
 * Description:
 *   Check whether a BLE client is currently connected and the GATT
 *   service is operational.
 *
 * Returned Value:
 *   true if connected, false otherwise.
 *
 ****************************************************************************/

bool ble_gatt_is_connected(void);

/****************************************************************************
 * Name: ble_gatt_get_mtu
 *
 * Description:
 *   Return the negotiated ATT MTU for the current connection.
 *
 * Returned Value:
 *   Current MTU (minimum 23 if not yet negotiated).
 *
 ****************************************************************************/

uint16_t ble_gatt_get_mtu(void);

#endif /* __FIRMWARE_BLE_GATT_SERVICE_H */
