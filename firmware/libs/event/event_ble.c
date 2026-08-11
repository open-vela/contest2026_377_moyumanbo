/**
 * @file event_ble.c
 * @brief BLE GATT event sync helper implementation
 *
 * Manages per-connection sync state and formats event data into
 * compact binary packets for BLE transmission.
 */

#include "event_ble.h"
#include "event_store.h"

#include <string.h>
#include <syslog.h>

/* ------------------------------------------------------------------ */
/* Per-connection sync state                                          */
/* ------------------------------------------------------------------ */

struct ble_conn_state {
    uint16_t conn_id;
    uint32_t last_synced_seq;   /* Next read starts at seq+1        */
    bool     in_use;
};

static struct ble_conn_state g_conns[EVT_BLE_CONN_MAX];

/* ------------------------------------------------------------------ */
/* Internal helpers                                                   */
/* ------------------------------------------------------------------ */

static struct ble_conn_state *find_conn(uint16_t conn_id)
{
    for (int i = 0; i < EVT_BLE_CONN_MAX; i++) {
        if (g_conns[i].in_use && g_conns[i].conn_id == conn_id) {
            return &g_conns[i];
        }
    }
    return NULL;
}

/**
 * Serialize an event_plain into the on-the-wire 48-byte format.
 *
 * The wire format matches struct event_plain layout exactly (little-endian
 * on ARM, which is the target).  We use explicit byte packing to be
 * safe across compilers.
 */
static void serialize_event(const evt_ble_event_t *event, uint8_t out[48])
{
    memset(out, 0, 48);

    /* timestamp: little-endian uint32 */
    out[0] = (uint8_t)(event->timestamp);
    out[1] = (uint8_t)(event->timestamp >> 8);
    out[2] = (uint8_t)(event->timestamp >> 16);
    out[3] = (uint8_t)(event->timestamp >> 24);

    /* label */
    out[4] = event->label;

    /* confidence */
    out[5] = event->confidence;

    /* reason_flags: little-endian uint16 */
    out[6] = (uint8_t)(event->reason_flags);
    out[7] = (uint8_t)(event->reason_flags >> 8);

    /* hr: little-endian uint16 */
    out[8]  = (uint8_t)(event->hr);
    out[9]  = (uint8_t)(event->hr >> 8);

    /* rmssd: little-endian uint16 */
    out[10] = (uint8_t)(event->rmssd);
    out[11] = (uint8_t)(event->rmssd >> 8);

    /* activity: little-endian uint16 */
    out[12] = (uint8_t)(event->activity);
    out[13] = (uint8_t)(event->activity >> 8);

    /* reserved[34] already zeroed by memset */
}

/**
 * Write the BLE response header into buf (12 bytes, little-endian).
 */
static void write_header(uint8_t *buf, const struct evt_ble_response_header *hdr)
{
    buf[0]  = hdr->status;
    buf[1]  = hdr->count;
    buf[2]  = (uint8_t)(hdr->total_remaining);
    buf[3]  = (uint8_t)(hdr->total_remaining >> 8);
    buf[4]  = (uint8_t)(hdr->start_seq);
    buf[5]  = (uint8_t)(hdr->start_seq >> 8);
    buf[6]  = (uint8_t)(hdr->start_seq >> 16);
    buf[7]  = (uint8_t)(hdr->start_seq >> 24);
    buf[8]  = (uint8_t)(hdr->end_seq);
    buf[9]  = (uint8_t)(hdr->end_seq >> 8);
    buf[10] = (uint8_t)(hdr->end_seq >> 16);
    buf[11] = (uint8_t)(hdr->end_seq >> 24);
}

/* ------------------------------------------------------------------ */
/* Connection management                                              */
/* ------------------------------------------------------------------ */

evt_ble_err_t evt_ble_conn_register(uint16_t conn_id)
{
    /* Check for duplicate */
    if (find_conn(conn_id) != NULL) {
        return EVT_BLE_OK;  /* Already registered, idempotent */
    }

    /* Find a free slot */
    for (int i = 0; i < EVT_BLE_CONN_MAX; i++) {
        if (!g_conns[i].in_use) {
            g_conns[i].in_use         = true;
            g_conns[i].conn_id        = conn_id;
            g_conns[i].last_synced_seq = evt_store_oldest_seq();
            if (g_conns[i].last_synced_seq > 0) {
                g_conns[i].last_synced_seq--;  /* Will start at oldest */
            }
            syslog(LOG_INFO, "event_ble: conn %u registered, sync from seq %u\n",
                   conn_id, g_conns[i].last_synced_seq);
            return EVT_BLE_OK;
        }
    }

    syslog(LOG_ERR, "event_ble: no room for conn %u\n", conn_id);
    return EVT_BLE_ERR_FULL;
}

void evt_ble_conn_unregister(uint16_t conn_id)
{
    struct ble_conn_state *cs = find_conn(conn_id);
    if (cs) {
        syslog(LOG_INFO, "event_ble: conn %u unregistered\n", conn_id);
        cs->in_use = false;
        memset(cs, 0, sizeof(*cs));
    }
}

evt_ble_err_t evt_ble_conn_reset(uint16_t conn_id)
{
    struct ble_conn_state *cs = find_conn(conn_id);
    if (!cs) return EVT_BLE_ERR_CONN;

    cs->last_synced_seq = evt_store_oldest_seq();
    if (cs->last_synced_seq > 0) {
        cs->last_synced_seq--;
    }
    syslog(LOG_INFO, "event_ble: conn %u reset to seq %u\n",
           conn_id, cs->last_synced_seq);
    return EVT_BLE_OK;
}

/* ------------------------------------------------------------------ */
/* Data retrieval                                                     */
/* ------------------------------------------------------------------ */

evt_ble_err_t evt_ble_get_next(uint16_t conn_id,
                               uint8_t *buf, size_t buf_len,
                               size_t *out_len)
{
    if (!buf || !out_len) return EVT_BLE_ERR_PARAM;

    struct ble_conn_state *cs = find_conn(conn_id);
    if (!cs) return EVT_BLE_ERR_CONN;

    /* Minimum buffer: 12-byte header + 1 event (48 bytes) = 60 bytes */
    size_t min_buf = sizeof(struct evt_ble_response_header) +
                     sizeof(evt_ble_event_t);
    if (buf_len < min_buf) return EVT_BLE_ERR_OVERFLOW;

    /* How many events can this buffer hold? */
    size_t max_events = (buf_len - sizeof(struct evt_ble_response_header)) /
                        sizeof(evt_ble_event_t);
    if (max_events > EVT_BLE_MAX_BATCH) max_events = EVT_BLE_MAX_BATCH;

    /* How many events are available? */
    uint32_t latest = evt_store_latest_seq();
    if (cs->last_synced_seq >= latest) {
        /* Nothing new */
        struct evt_ble_response_header hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.status = 0;
        hdr.count  = 0;
        hdr.total_remaining = 0;
        hdr.start_seq = 0;
        hdr.end_seq   = 0;
        write_header(buf, &hdr);
        *out_len = sizeof(struct evt_ble_response_header);
        return EVT_BLE_OK;
    }

    uint32_t available = latest - cs->last_synced_seq;
    uint32_t to_read   = (available < max_events) ? available :
                                                  (uint32_t)max_events;

    /* Read events */
    uint8_t *payload = buf + sizeof(struct evt_ble_response_header);
    uint8_t  count   = 0;
    uint32_t first_seq = 0;
    uint32_t last_seq  = 0;

    for (uint32_t i = 0; i < to_read; i++) {
        uint32_t seq = cs->last_synced_seq + 1 + i;
        struct event_plain event;
        evt_store_err_t rc = evt_store_read(seq, &event);
        if (rc == EVT_STORE_ERR_NOTFOUND) {
            /* Gap — skip this sequence */
            continue;
        }
        if (rc != EVT_STORE_OK) {
            syslog(LOG_ERR, "event_ble: read seq %u failed: %d\n", seq, rc);
            break;
        }

        serialize_event(&event, payload + count * sizeof(evt_ble_event_t));

        if (count == 0) first_seq = seq;
        last_seq = seq;
        count++;
    }

    /* Advance sync pointer */
    if (count > 0) {
        cs->last_synced_seq = last_seq;
    }

    /* Fill header */
    struct evt_ble_response_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.status          = 0;
    hdr.count           = count;
    hdr.total_remaining = (uint16_t)((available > count) ?
                                      (available - count) : 0);
    hdr.start_seq       = first_seq;
    hdr.end_seq         = last_seq;
    write_header(buf, &hdr);

    *out_len = sizeof(struct evt_ble_response_header) +
               (size_t)count * sizeof(evt_ble_event_t);

    return EVT_BLE_OK;
}

evt_ble_err_t evt_ble_read_page(uint16_t conn_id,
                                uint32_t offset, uint8_t count,
                                uint8_t *buf, size_t buf_len,
                                size_t *out_len)
{
    (void)conn_id;  /* Not used for pagination; kept for API symmetry */
    if (!buf || !out_len) return EVT_BLE_ERR_PARAM;
    if (count == 0)       return EVT_BLE_ERR_RANGE;

    size_t min_buf = sizeof(struct evt_ble_response_header) +
                     sizeof(evt_ble_event_t);
    if (buf_len < min_buf) return EVT_BLE_ERR_OVERFLOW;

    size_t max_events = (buf_len - sizeof(struct evt_ble_response_header)) /
                        sizeof(evt_ble_event_t);
    if (count > max_events) count = (uint8_t)max_events;
    if (count > EVT_BLE_MAX_BATCH) count = EVT_BLE_MAX_BATCH;

    /* Start reading from oldest_seq + offset */
    uint32_t start_seq = evt_store_oldest_seq() + offset;
    uint32_t latest    = evt_store_latest_seq();

    uint8_t *payload = buf + sizeof(struct evt_ble_response_header);
    uint8_t  readn   = 0;
    uint32_t first_seq = 0;
    uint32_t last_seq  = 0;

    for (uint32_t seq = start_seq;
         seq <= latest && readn < count;
         seq++) {
        struct event_plain event;
        evt_store_err_t rc = evt_store_read(seq, &event);
        if (rc == EVT_STORE_ERR_NOTFOUND) continue;
        if (rc != EVT_STORE_OK) break;

        serialize_event(&event, payload + readn * sizeof(evt_ble_event_t));

        if (readn == 0) first_seq = seq;
        last_seq = seq;
        readn++;
    }

    struct evt_ble_response_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.status          = 0;
    hdr.count           = readn;
    hdr.total_remaining = 0;  /* Page read does not track remaining */
    hdr.start_seq       = first_seq;
    hdr.end_seq         = last_seq;
    write_header(buf, &hdr);

    *out_len = sizeof(struct evt_ble_response_header) +
               (size_t)readn * sizeof(evt_ble_event_t);

    return EVT_BLE_OK;
}

uint32_t evt_ble_pending_count(uint16_t conn_id)
{
    struct ble_conn_state *cs = find_conn(conn_id);
    if (!cs) return 0;

    uint32_t latest = evt_store_latest_seq();
    if (cs->last_synced_seq >= latest) return 0;
    return latest - cs->last_synced_seq;
}

evt_ble_err_t evt_ble_format_single(const evt_ble_event_t *event,
                                    uint8_t *buf, size_t buf_len,
                                    size_t *out_len)
{
    if (!event || !buf || !out_len) return EVT_BLE_ERR_PARAM;
    if (buf_len < sizeof(evt_ble_event_t)) return EVT_BLE_ERR_OVERFLOW;

    serialize_event(event, buf);
    *out_len = sizeof(evt_ble_event_t);
    return EVT_BLE_OK;
}
