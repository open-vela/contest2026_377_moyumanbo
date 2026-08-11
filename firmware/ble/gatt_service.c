/****************************************************************************
 * VelaSense BLE GATT Service — Implementation
 *
 * Registers the custom VelaSense GATT service and all six characteristics
 * with the NuttX Bluetooth stack.  Handles incoming read/write requests
 * from the phone client and dispatches them to the application callbacks.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <string.h>
#include <errno.h>
#include <syslog.h>

#include <nuttx/config.h>
#include <nuttx/bluetooth/bluetooth.h>
#include <nuttx/bluetooth/gatt.h>

#include "gatt_service.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* CCC (Client Characteristic Configuration) descriptor indices */

#define CCC_EVENT_NOTIFY        0
#define CCC_DEVICE_STATUS       1
#define CCC_COUNT               2

/* Connection state flags */

#define CONN_FLAG_CONNECTED     (1 << 0)
#define CONN_FLAG_SUBSCRIBE_EN  (1 << 1)  /* Event Notify subscribed */
#define CONN_FLAG_SUBSCRIBE_ST  (1 << 2)  /* Device Status subscribed */

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Per-connection state */

struct gatt_conn_state
{
  uint16_t conn_handle;         /* BLE connection handle */
  uint16_t mtu;                 /* Negotiated ATT MTU */
  uint32_t flags;               /* CONN_FLAG_* bitmask */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Application callback table */

static const struct ble_gatt_callbacks *g_cbs;

/* Connection state (single-connection device) */

static struct gatt_conn_state g_conn;

/* CCC values — tracks client notification subscriptions */

static uint16_t g_ccc[CCC_COUNT];

/* Service registered flag */

static bool g_service_registered;

/****************************************************************************
 * Private Forward Declarations
 ****************************************************************************/

static ssize_t gatt_event_summary_read(struct bt_conn *conn,
                                       const struct bt_gatt_attr *attr,
                                       void *buf, uint16_t len,
                                       uint16_t offset);

static ssize_t gatt_config_read(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                void *buf, uint16_t len,
                                uint16_t offset);

static ssize_t gatt_config_write(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 const void *buf, uint16_t len,
                                 uint16_t offset, uint8_t flags);

static ssize_t gatt_user_label_write(struct bt_conn *conn,
                                     const struct bt_gatt_attr *attr,
                                     const void *buf, uint16_t len,
                                     uint16_t offset, uint8_t flags);

static ssize_t gatt_ota_write(struct bt_conn *conn,
                              const struct bt_gatt_attr *attr,
                              const void *buf, uint16_t len,
                              uint16_t offset, uint8_t flags);

static ssize_t gatt_device_status_read(struct bt_conn *conn,
                                       const struct bt_gatt_attr *attr,
                                       void *buf, uint16_t len,
                                       uint16_t offset);

static void gatt_ccc_changed(const struct bt_gatt_attr *attr,
                             uint16_t value);

static void gatt_connected(struct bt_conn *conn, uint8_t err);
static void gatt_disconnected(struct bt_conn *conn, uint8_t reason);

/****************************************************************************
 * GATT Attribute Table
 *
 * The attribute table is laid out as:
 *   0: Service Declaration
 *   1: Event Notify — Declaration
 *   2: Event Notify — Value
 *   3: Event Notify — CCC
 *   4: Event Summary — Declaration
 *   5: Event Summary — Value (read, variable length)
 *   6: User Label — Declaration
 *   7: User Label — Value (write)
 *   8: Config — Declaration
 *   9: Config — Value (read/write)
 *  10: OTA Data — Declaration
 *  11: OTA Data — Value (write, variable length)
 *  12: Device Status — Declaration
 *  13: Device Status — Value (read/notify)
 *  14: Device Status — CCC
 *
 ****************************************************************************/

/* 128-bit service UUID */

static struct bt_uuid_128 g_velasense_svc_uuid = BT_UUID_INIT_128(
  VELASENSE_UUID_BASE);

/* 128-bit characteristic UUIDs */

static struct bt_uuid_128 g_uuid_event_notify = BT_UUID_INIT_128(
  VELASENSE_UUID_BASE);
static struct bt_uuid_128 g_uuid_event_summary = BT_UUID_INIT_128(
  VELASENSE_UUID_BASE);
static struct bt_uuid_128 g_uuid_user_label = BT_UUID_INIT_128(
  VELASENSE_UUID_BASE);
static struct bt_uuid_128 g_uuid_config = BT_UUID_INIT_128(
  VELASENSE_UUID_BASE);
static struct bt_uuid_128 g_uuid_ota_data = BT_UUID_INIT_128(
  VELASENSE_UUID_BASE);
static struct bt_uuid_128 g_uuid_device_status = BT_UUID_INIT_128(
  VELASENSE_UUID_BASE);

/* NOTE: In a real NuttX BT implementation, each 128-bit UUID above
 * would have its offset bytes set differently.  For clarity and
 * portability, we construct them in ble_gatt_service_init().
 */

static struct bt_gatt_attr g_velasense_attrs[] = {
  /* 0: Service Declaration */

  BT_GATT_PRIMARY_SERVICE(&g_velasense_svc_uuid),

  /* 1-3: Event Notify Characteristic */

  BT_GATT_CHARACTERISTIC(&g_uuid_event_notify.uuid,
                         BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                         BT_GATT_PERM_READ),
  BT_GATT_DESCRIPTOR(&g_uuid_event_notify.uuid,
                     BT_GATT_PERM_READ,
                     NULL, NULL, NULL),
  BT_GATT_CCC(g_ccc, gatt_ccc_changed),

  /* 4-5: Event Summary Characteristic (read, paginated) */

  BT_GATT_CHARACTERISTIC(&g_uuid_event_summary.uuid,
                         BT_GATT_CHRC_READ,
                         BT_GATT_PERM_READ),
  BT_GATT_DESCRIPTOR(&g_uuid_event_summary.uuid,
                     BT_GATT_PERM_READ,
                     gatt_event_summary_read, NULL, NULL),

  /* 6-7: User Label Characteristic (write) */

  BT_GATT_CHARACTERISTIC(&g_uuid_user_label.uuid,
                         BT_GATT_CHRC_WRITE,
                         BT_GATT_PERM_WRITE),
  BT_GATT_DESCRIPTOR(&g_uuid_user_label.uuid,
                     BT_GATT_PERM_WRITE,
                     NULL, gatt_user_label_write, NULL),

  /* 8-9: Config Characteristic (read/write) */

  BT_GATT_CHARACTERISTIC(&g_uuid_config.uuid,
                         BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                         BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
  BT_GATT_DESCRIPTOR(&g_uuid_config.uuid,
                     BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                     gatt_config_read, gatt_config_write, NULL),

  /* 10-11: OTA Data Characteristic (write) */

  BT_GATT_CHARACTERISTIC(&g_uuid_ota_data.uuid,
                         BT_GATT_CHRC_WRITE,
                         BT_GATT_PERM_WRITE),
  BT_GATT_DESCRIPTOR(&g_uuid_ota_data.uuid,
                     BT_GATT_PERM_WRITE,
                     NULL, gatt_ota_write, NULL),

  /* 12-14: Device Status Characteristic (read/notify) */

  BT_GATT_CHARACTERISTIC(&g_uuid_device_status.uuid,
                         BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                         BT_GATT_PERM_READ),
  BT_GATT_DESCRIPTOR(&g_uuid_device_status.uuid,
                     BT_GATT_PERM_READ,
                     gatt_device_status_read, NULL, NULL),
  BT_GATT_CCC(&g_ccc[CCC_DEVICE_STATUS], gatt_ccc_changed),
};

/* GATT service structure */

static struct bt_gatt_service g_velasense_svc;

/* Connection callbacks */

static struct bt_conn_cb g_conn_callbacks = {
  .connected    = gatt_connected,
  .disconnected = gatt_disconnected,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: set_uuid_offset
 *
 * Description:
 *   Set the 16-bit offset in a 128-bit UUID (bytes 12-13, little-endian).
 *
 ****************************************************************************/

static void set_uuid_offset(struct bt_uuid_128 *uuid, uint16_t offset)
{
  uuid->val[12] = (uint8_t)(offset & 0xff);
  uuid->val[13] = (uint8_t)((offset >> 8) & 0xff);
}

/****************************************************************************
 * Name: gatt_connected
 *
 * Description:
 *   BLE connection established callback.
 *
 ****************************************************************************/

static void gatt_connected(struct bt_conn *conn, uint8_t err)
{
  if (err != 0)
    {
      syslog(LOG_ERR, "BLE: connection failed (err=%u)\n", err);
      return;
    }

  g_conn.conn_handle = bt_conn_get_handle(conn);
  g_conn.mtu         = 23;  /* Default ATT MTU */
  g_conn.flags       = CONN_FLAG_CONNECTED;

  syslog(LOG_INFO, "BLE: connected (handle=%u)\n", g_conn.conn_handle);
}

/****************************************************************************
 * Name: gatt_disconnected
 *
 * Description:
 *   BLE disconnection callback.
 *
 ****************************************************************************/

static void gatt_disconnected(struct bt_conn *conn, uint8_t reason)
{
  (void)conn;

  syslog(LOG_INFO, "BLE: disconnected (reason=%u)\n", reason);

  g_conn.conn_handle = 0;
  g_conn.flags       = 0;
  memset(g_ccc, 0, sizeof(g_ccc));
}

/****************************************************************************
 * Name: gatt_ccc_changed
 *
 * Description:
 *   CCC descriptor written — client toggled notifications.
 *
 ****************************************************************************/

static void gatt_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
  /* Identify which CCC was modified by comparing the attribute pointer.
   * Index 3  = Event Notify CCC
   * Index 14 = Device Status CCC
   */

  if (attr == &g_velasense_attrs[3])
    {
      /* Event Notify CCC */

      if (value & BT_GATT_CCC_NOTIFY)
        {
          g_conn.flags |= CONN_FLAG_SUBSCRIBE_EN;
          syslog(LOG_INFO, "BLE: Event Notify subscribed\n");
        }
      else
        {
          g_conn.flags &= ~CONN_FLAG_SUBSCRIBE_EN;
          syslog(LOG_INFO, "BLE: Event Notify unsubscribed\n");
        }
    }
  else if (attr == &g_velasense_attrs[14])
    {
      if (value & BT_GATT_CCC_NOTIFY)
        {
          g_conn.flags |= CONN_FLAG_SUBSCRIBE_ST;
          syslog(LOG_INFO, "BLE: Device Status subscribed\n");
        }
      else
        {
          g_conn.flags &= ~CONN_FLAG_SUBSCRIBE_ST;
          syslog(LOG_INFO, "BLE: Device Status unsubscribed\n");
        }
    }
}

/****************************************************************************
 * Name: gatt_event_summary_read
 *
 * Description:
 *   Handle read request for Event Summary characteristic.
 *   Returns a page of confirmed event records.
 *
 ****************************************************************************/

static ssize_t gatt_event_summary_read(struct bt_conn *conn,
                                       const struct bt_gatt_attr *attr,
                                       void *buf, uint16_t len,
                                       uint16_t offset)
{
  struct ble_event_summary_record records[VELASENSE_SUMMARY_PAGE_MAX];
  int count;
  int page;
  uint16_t total;

  (void)conn;
  (void)attr;

  if (g_cbs == NULL || g_cbs->event_summary_read == NULL)
    {
      return bt_gatt_attr_read(conn, attr, buf, len, offset, NULL, 0);
    }

  /* Compute page index from byte offset */

  page = (int)(offset / VELASENSE_SUMMARY_RECORD_LEN);

  count = g_cbs->event_summary_read(records, page,
                                    VELASENSE_SUMMARY_PAGE_MAX);
  if (count <= 0)
    {
      return bt_gatt_attr_read(conn, attr, buf, len, offset, NULL, 0);
    }

  total = (uint16_t)(count * VELASENSE_SUMMARY_RECORD_LEN);

  return bt_gatt_attr_read(conn, attr, buf, len, offset, records, total);
}

/****************************************************************************
 * Name: gatt_config_read
 *
 * Description:
 *   Handle read request for Config characteristic.
 *
 ****************************************************************************/

static ssize_t gatt_config_read(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                void *buf, uint16_t len,
                                uint16_t offset)
{
  struct ble_config_data cfg;
  int ret;

  (void)conn;
  (void)attr;

  if (g_cbs == NULL || g_cbs->config_read == NULL)
    {
      return bt_gatt_attr_read(conn, attr, buf, len, offset, NULL, 0);
    }

  memset(&cfg, 0, sizeof(cfg));
  ret = g_cbs->config_read(&cfg);
  if (ret < 0)
    {
      return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

  return bt_gatt_attr_read(conn, attr, buf, len, offset,
                           &cfg, VELASENSE_CONFIG_LEN);
}

/****************************************************************************
 * Name: gatt_config_write
 *
 * Description:
 *   Handle write request for Config characteristic.
 *
 ****************************************************************************/

static ssize_t gatt_config_write(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 const void *buf, uint16_t len,
                                 uint16_t offset, uint8_t flags)
{
  struct ble_config_data cfg;
  int ret;

  (void)conn;
  (void)attr;
  (void)flags;

  if (len < VELASENSE_CONFIG_LEN)
    {
      return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

  if (g_cbs == NULL || g_cbs->config_write == NULL)
    {
      return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
    }

  memcpy(&cfg, buf, VELASENSE_CONFIG_LEN);
  ret = g_cbs->config_write(&cfg);
  if (ret < 0)
    {
      return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

  return len;
}

/****************************************************************************
 * Name: gatt_user_label_write
 *
 * Description:
 *   Handle write request for User Label characteristic.
 *
 ****************************************************************************/

static ssize_t gatt_user_label_write(struct bt_conn *conn,
                                     const struct bt_gatt_attr *attr,
                                     const void *buf, uint16_t len,
                                     uint16_t offset, uint8_t flags)
{
  struct ble_user_label label;
  int ret;

  (void)conn;
  (void)attr;
  (void)offset;
  (void)flags;

  if (len < VELASENSE_USER_LABEL_LEN)
    {
      return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

  if (g_cbs == NULL || g_cbs->user_label_write == NULL)
    {
      return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
    }

  memcpy(&label, buf, VELASENSE_USER_LABEL_LEN);
  ret = g_cbs->user_label_write(&label);
  if (ret < 0)
    {
      return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

  return len;
}

/****************************************************************************
 * Name: gatt_ota_write
 *
 * Description:
 *   Handle write request for OTA Data characteristic.
 *   The first 6 bytes are the header (offset + data_len), followed by
 *   the firmware data chunk.  A CRC32 is appended at the end of the
 *   final chunk (offset + data_len == total image size).
 *
 ****************************************************************************/

static ssize_t gatt_ota_write(struct bt_conn *conn,
                              const struct bt_gatt_attr *attr,
                              const void *buf, uint16_t len,
                              uint16_t offset, uint8_t flags)
{
  const struct ble_ota_header *hdr;
  int ret;

  (void)conn;
  (void)attr;
  (void)offset;
  (void)flags;

  if (len < VELASENSE_OTA_HEADER_LEN)
    {
      return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

  if (g_cbs == NULL || g_cbs->ota_data_write == NULL)
    {
      return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
    }

  hdr = (const struct ble_ota_header *)buf;

  /* Validate that declared data_len matches actual payload */

  if (hdr->data_len > (len - VELASENSE_OTA_HEADER_LEN))
    {
      syslog(LOG_ERR, "BLE: OTA length mismatch (decl=%u, avail=%u)\n",
             hdr->data_len, len - VELASENSE_OTA_HEADER_LEN);
      return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

  if (hdr->data_len > VELASENSE_OTA_CHUNK_MAX)
    {
      syslog(LOG_ERR, "BLE: OTA chunk too large (%u)\n", hdr->data_len);
      return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

  ret = g_cbs->ota_data_write(hdr->offset,
                               (const uint8_t *)hdr + VELASENSE_OTA_HEADER_LEN,
                               hdr->data_len);
  if (ret < 0)
    {
      syslog(LOG_ERR, "BLE: OTA write failed (offset=%u, ret=%d)\n",
             hdr->offset, ret);
      return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

  return len;
}

/****************************************************************************
 * Name: gatt_device_status_read
 *
 * Description:
 *   Handle read request for Device Status characteristic.
 *
 ****************************************************************************/

static ssize_t gatt_device_status_read(struct bt_conn *conn,
                                       const struct bt_gatt_attr *attr,
                                       void *buf, uint16_t len,
                                       uint16_t offset)
{
  struct ble_device_status status;
  int ret;

  (void)conn;
  (void)attr;

  if (g_cbs == NULL || g_cbs->device_status_read == NULL)
    {
      return bt_gatt_attr_read(conn, attr, buf, len, offset, NULL, 0);
    }

  memset(&status, 0, sizeof(status));
  ret = g_cbs->device_status_read(&status);
  if (ret < 0)
    {
      return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

  return bt_gatt_attr_read(conn, attr, buf, len, offset,
                           &status, VELASENSE_STATUS_LEN);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ble_gatt_service_init
 ****************************************************************************/

int ble_gatt_service_init(const struct ble_gatt_callbacks *cbs)
{
  if (cbs == NULL)
    {
      return -EINVAL;
    }

  g_cbs = cbs;
  memset(&g_conn, 0, sizeof(g_conn));
  memset(g_ccc, 0, sizeof(g_ccc));

  /* Set unique UUID offsets for each characteristic.
   * The base UUID has 0x0000 in bytes 12-13; we patch each one.
   */

  set_uuid_offset(&g_velasense_svc_uuid,  VELASENSE_UUID_SERVICE);
  set_uuid_offset(&g_uuid_event_notify,   VELASENSE_UUID_EVENT_NOTIFY);
  set_uuid_offset(&g_uuid_event_summary,  VELASENSE_UUID_EVENT_SUMMARY);
  set_uuid_offset(&g_uuid_user_label,     VELASENSE_UUID_USER_LABEL);
  set_uuid_offset(&g_uuid_config,         VELASENSE_UUID_CONFIG);
  set_uuid_offset(&g_uuid_ota_data,       VELASENSE_UUID_OTA_DATA);
  set_uuid_offset(&g_uuid_device_status,  VELASENSE_UUID_DEVICE_STATUS);

  /* Register the GATT service */

  g_velasense_svc.attrs      = g_velasense_attrs;
  g_velasense_svc.attr_count = sizeof(g_velasense_attrs) /
                               sizeof(g_velasense_attrs[0]);

  int ret = bt_gatt_service_register(&g_velasense_svc);
  if (ret < 0)
    {
      syslog(LOG_ERR, "BLE: GATT service register failed (%d)\n", ret);
      return ret;
    }

  /* Register connection callbacks */

  ret = bt_conn_cb_register(&g_conn_callbacks);
  if (ret < 0)
    {
      syslog(LOG_ERR, "BLE: conn callback register failed (%d)\n", ret);
      bt_gatt_service_unregister(&g_velasense_svc);
      return ret;
    }

  g_service_registered = true;
  syslog(LOG_INFO, "BLE: GATT service registered (15 attributes)\n");

  return 0;
}

/****************************************************************************
 * Name: ble_gatt_service_deinit
 ****************************************************************************/

void ble_gatt_service_deinit(void)
{
  if (g_service_registered)
    {
      bt_gatt_service_unregister(&g_velasense_svc);
      g_service_registered = false;
    }

  g_cbs = NULL;
  memset(&g_conn, 0, sizeof(g_conn));
  memset(g_ccc, 0, sizeof(g_ccc));

  syslog(LOG_INFO, "BLE: GATT service deinitialized\n");
}

/****************************************************************************
 * Name: ble_gatt_notify_event
 ****************************************************************************/

int ble_gatt_notify_event(void)
{
  struct ble_event_notify evt;
  int ret;

  if (!(g_conn.flags & CONN_FLAG_CONNECTED))
    {
      return -ENOTCONN;
    }

  if (!(g_conn.flags & CONN_FLAG_SUBSCRIBE_EN))
    {
      return -ENOTCONN;  /* Client not subscribed */
    }

  if (g_cbs == NULL || g_cbs->event_notify_fill == NULL)
    {
      return -EINVAL;
    }

  if (!g_cbs->event_notify_fill(&evt))
    {
      return -EAGAIN;  /* No event to send */
    }

  /* Send notification via the GATT attribute at index 2 (Event Notify value) */

  ret = bt_gatt_notify(NULL, &g_velasense_attrs[2], &evt,
                       VELASENSE_EVENT_NOTIFY_LEN);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "BLE: event notify failed (%d)\n", ret);
    }

  return ret;
}

/****************************************************************************
 * Name: ble_gatt_notify_status
 ****************************************************************************/

int ble_gatt_notify_status(void)
{
  struct ble_device_status status;
  int ret;

  if (!(g_conn.flags & CONN_FLAG_CONNECTED))
    {
      return -ENOTCONN;
    }

  if (!(g_conn.flags & CONN_FLAG_SUBSCRIBE_ST))
    {
      return -ENOTCONN;
    }

  if (g_cbs == NULL || g_cbs->device_status_read == NULL)
    {
      return -EINVAL;
    }

  memset(&status, 0, sizeof(status));
  ret = g_cbs->device_status_read(&status);
  if (ret < 0)
    {
      return ret;
    }

  /* Send notification via Device Status value attribute (index 13) */

  ret = bt_gatt_notify(NULL, &g_velasense_attrs[13], &status,
                       VELASENSE_STATUS_LEN);
  if (ret < 0)
    {
      syslog(LOG_WARNING, "BLE: status notify failed (%d)\n", ret);
    }

  return ret;
}

/****************************************************************************
 * Name: ble_gatt_is_connected
 ****************************************************************************/

bool ble_gatt_is_connected(void)
{
  return (g_conn.flags & CONN_FLAG_CONNECTED) != 0;
}

/****************************************************************************
 * Name: ble_gatt_get_mtu
 ****************************************************************************/

uint16_t ble_gatt_get_mtu(void)
{
  if (g_conn.mtu == 0)
    {
      return 23;  /* ATT default */
    }

  return g_conn.mtu;
}
