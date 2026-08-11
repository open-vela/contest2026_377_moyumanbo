/**
 * @file event_ble.h
 * @brief BLE GATT event sync helper for VelaSense
 *
 * Formats decrypted events for transmission over a BLE GATT
 * characteristic.  Supports:
 *   - Batching up to 20 events per read response
 *   - Pagination via offset + count
 *   - Delta sync via per-connection sequence tracking
 *   - Compact binary encoding for BLE MTU efficiency
 *
 * The phone side (Mimo app) is responsible for:
 *   - Reassembling paginated responses
 *   - Uploading to the cloud (the device never uploads directly)
 *   - Sending back user labels / confirmations
 */

#ifndef VELASENSE_EVENT_BLE_H
#define VELASENSE_EVENT_BLE_H

#include "event_store.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

#define EVT_BLE_MAX_BATCH       20    /* Max events per BLE response */
#define EVT_BLE_MAX_PAYLOAD     960   /* 20 * 48 bytes per event     */
#define EVT_BLE_CONN_MAX        4     /* Max simultaneous connections */

/* BLE GATT characteristic UUIDs (vendor-specific) */
#define EVT_BLE_CHAR_SYNC_UUID  0x2A5A  /* Device-synced event char  */

/* ------------------------------------------------------------------ */
/* Sync status codes                                                  */
/* ------------------------------------------------------------------ */

typedef enum {
    EVT_BLE_OK             =  0,
    EVT_BLE_ERR_PARAM      = -1,
    EVT_BLE_ERR_CONN       = -2,   /* Unknown connection ID          */
    EVT_BLE_ERR_FULL       = -3,   /* No room for more connections   */
    EVT_BLE_ERR_RANGE      = -4,   /* Invalid offset/count           */
    EVT_BLE_ERR_STORE      = -5,   /* Underlying store error         */
    EVT_BLE_ERR_OVERFLOW   = -6,   /* Payload exceeds BLE_MTU        */
} evt_ble_err_t;

/* ------------------------------------------------------------------ */
/* Packed event for BLE transmission (48 bytes)                       */
/* ------------------------------------------------------------------ */

/*
 * This is the on-the-wire format sent to the phone.  It matches
 * struct event_plain exactly so the phone can deserialize directly.
 * If the BLE MTU is smaller than 48 bytes, the GATT layer will
 * handle segmentation (L2CAP); we just provide the full payload.
 */
typedef struct event_plain evt_ble_event_t;

/* ------------------------------------------------------------------ */
/* BLE response descriptor                                            */
/* ------------------------------------------------------------------ */

/**
 * Header prepended to every BLE event response.
 */
struct evt_ble_response_header {
    uint8_t  status;          /* 0 = ok, else error                  */
    uint8_t  count;           /* Number of events in this batch      */
    uint16_t total_remaining; /* Events still available after this   */
    uint32_t start_seq;       /* Sequence number of first event      */
    uint32_t end_seq;         /* Sequence number of last event       */
};  /* 12 bytes */

/* ------------------------------------------------------------------ */
/* Connection management                                              */
/* ------------------------------------------------------------------ */

/**
 * Register a new BLE connection for event sync.
 *
 * Call this when a BLE client connects and subscribes to the
 * event-sync characteristic.  Tracks the last-synced sequence
 * number so subsequent calls return only new events.
 *
 * @param conn_id   Opaque BLE connection identifier.
 * @return EVT_BLE_OK on success.
 */
evt_ble_err_t evt_ble_conn_register(uint16_t conn_id);

/**
 * Unregister a BLE connection.
 *
 * Call this when the BLE client disconnects.
 *
 * @param conn_id   Connection identifier.
 */
void evt_ble_conn_unregister(uint16_t conn_id);

/**
 * Reset the sync pointer for a connection to start from the beginning.
 *
 * Useful when the phone requests a full re-sync.
 *
 * @param conn_id   Connection identifier.
 * @return EVT_BLE_OK on success.
 */
evt_ble_err_t evt_ble_conn_reset(uint16_t conn_id);

/* ------------------------------------------------------------------ */
/* Data retrieval                                                     */
/* ------------------------------------------------------------------ */

/**
 * Get the next batch of unsynced events for a connection.
 *
 * Reads up to EVT_BLE_MAX_BATCH events starting from the connection's
 * last-synced sequence number.  The response is written into buf as:
 *
 *   [header(12)] [event_0(48)] [event_1(48)] ... [event_N(48)]
 *
 * The caller should transmit buf (header_len + count*48 bytes) over
 * the BLE GATT characteristic.
 *
 * On success the connection's sync pointer is advanced past the
 * returned events.
 *
 * @param conn_id   Connection identifier.
 * @param buf       Output buffer (must be >= EVT_BLE_MAX_PAYLOAD + 12).
 * @param buf_len   Size of buf.
 * @param out_len   [out] Bytes written to buf.
 * @return EVT_BLE_OK on success.
 */
evt_ble_err_t evt_ble_get_next(uint16_t conn_id,
                               uint8_t *buf, size_t buf_len,
                               size_t *out_len);

/**
 * Paginated read: fetch events at a specific offset and count.
 *
 * Unlike get_next, this does NOT advance the sync pointer.
 * Used when the phone wants to browse history.
 *
 * @param conn_id   Connection identifier (for context).
 * @param offset    Starting event offset (0 = oldest).
 * @param count     Number of events requested (clamped to MAX_BATCH).
 * @param buf       Output buffer.
 * @param buf_len   Size of buf.
 * @param out_len   [out] Bytes written.
 * @return EVT_BLE_OK on success.
 */
evt_ble_err_t evt_ble_read_page(uint16_t conn_id,
                                uint32_t offset, uint8_t count,
                                uint8_t *buf, size_t buf_len,
                                size_t *out_len);

/**
 * Return the number of pending (unsynced) events for a connection.
 */
uint32_t evt_ble_pending_count(uint16_t conn_id);

/**
 * Format a single event into the BLE wire format.
 *
 * Useful for push notifications when a new event is stored.
 * The caller provides the raw event_plain; this function does
 * NOT read from the store.
 *
 * @param event     Decrypted event to format.
 * @param buf       Output buffer (must be >= 48 bytes).
 * @param buf_len   Size of buf.
 * @param out_len   [out] Bytes written.
 * @return EVT_BLE_OK on success.
 */
evt_ble_err_t evt_ble_format_single(const evt_ble_event_t *event,
                                    uint8_t *buf, size_t buf_len,
                                    size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* VELASENSE_EVENT_BLE_H */
