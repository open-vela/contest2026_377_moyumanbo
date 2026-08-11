/****************************************************************************
 * VelaSense BLE Event Synchronization Protocol
 *
 * Implements delta synchronization between the wearable device and the
 * companion phone app.  The protocol is designed for efficiency and
 * robustness against disconnections.
 *
 * Sync Flow:
 *
 *   Phone                    Device
 *     │                         │
 *     │── SYNC_REQ(last_seq) ──>│
 *     │                         │  look up events with seq > last_seq
 *     │<── EVENT_BATCH(events)──│
 *     │                         │
 *     │── SYNC_ACK(received) ──>│  confirm how many received
 *     │                         │
 *     │<── SYNC_DONE ──────────│
 *     │                         │
 *
 * Queue:
 *   - Up to CONFIG_BLE_SYNC_QUEUE_SIZE events buffered during disconnect
 *   - Ring buffer with oldest-dropped policy on overflow
 *   - Each queued event carries its sequence number for ordering
 *
 * Resume:
 *   - On reconnect, phone sends its last confirmed seq
 *   - Device responds with all events seq > last_confirmed
 *   - No duplicate delivery: phone ignores events it already has
 *
 ****************************************************************************/

#ifndef __FIRMWARE_BLE_BLE_SYNC_H
#define __FIRMWARE_BLE_BLE_SYNC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>

#include "gatt_service.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Maximum events in the sync queue (ring buffer depth).
 * Must be a power of two.  100 events at ~16 bytes each = 1.6 KB.
 */

#ifndef CONFIG_BLE_SYNC_QUEUE_SIZE
#  define CONFIG_BLE_SYNC_QUEUE_SIZE    128  /* next power of 2 above 100 */
#endif

/* Maximum events per sync batch (one ATT notification burst).
 * Limited by MTU and connection interval.
 */

#ifndef CONFIG_BLE_SYNC_BATCH_MAX
#  define CONFIG_BLE_SYNC_BATCH_MAX     10
#endif

/* Sync timeout — if no ACK from phone within this window, retry. */

#ifndef CONFIG_BLE_SYNC_TIMEOUT_MS
#  define CONFIG_BLE_SYNC_TIMEOUT_MS    5000
#endif

/* Maximum retry attempts before giving up on a batch. */

#ifndef CONFIG_BLE_SYNC_MAX_RETRIES
#  define CONFIG_BLE_SYNC_MAX_RETRIES   3
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Sync protocol state */

enum ble_sync_state
{
  BLE_SYNC_IDLE = 0,        /* No sync in progress */
  BLE_SYNC_WAIT_REQ,        /* Waiting for SYNC_REQ from phone */
  BLE_SYNC_SENDING,         /* Sending event batches */
  BLE_SYNC_WAIT_ACK,        /* Waiting for ACK from phone */
  BLE_SYNC_DONE,            /* Sync complete */
  BLE_SYNC_ERROR            /* Sync failed after max retries */
};

/* Sync event — one queued event for synchronization.
 * This is a flattened version of ble_event_notify with a sequence number.
 */

struct __attribute__((packed)) ble_sync_event
{
  uint32_t seq;              /* Monotonic sequence number */
  struct ble_event_notify notify;  /* 12-byte event payload */
};

/* Sync statistics */

struct ble_sync_stats
{
  uint32_t total_synced;     /* Total events ever synced */
  uint32_t total_queued;     /* Total events ever queued */
  uint32_t total_dropped;    /* Events dropped (queue overflow) */
  uint32_t total_retries;    /* Total retry count across all syncs */
  uint32_t sync_count;       /* Number of completed sync sessions */
  uint32_t error_count;      /* Number of failed sync sessions */
};

/* Sync context — opaque, managed internally */

struct ble_sync_ctx;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: ble_sync_init
 *
 * Description:
 *   Initialize the sync protocol module.  Allocates the event queue
 *   and resets all counters.
 *
 * Returned Value:
 *   0 on success, negative errno on failure.
 *
 ****************************************************************************/

int ble_sync_init(void);

/****************************************************************************
 * Name: ble_sync_deinit
 *
 * Description:
 *   Release all sync resources.
 *
 ****************************************************************************/

void ble_sync_deinit(void);

/****************************************************************************
 * Name: ble_sync_queue_event
 *
 * Description:
 *   Enqueue a newly detected event for synchronization.
 *   Called by the event state machine when an event reaches the
 *   ALERTING or CONFIRMED state.  The event is assigned a sequence
 *   number and pushed into the ring buffer.
 *
 *   If the queue is full, the oldest event is dropped.
 *
 * Input Parameters:
 *   notify - Pointer to the 12-byte event notification payload.
 *
 * Returned Value:
 *   Assigned sequence number (always positive).
 *   Returns 0 only if notify is NULL.
 *
 ****************************************************************************/

uint32_t ble_sync_queue_event(const struct ble_event_notify *notify);

/****************************************************************************
 * Name: ble_sync_get_last_seq
 *
 * Description:
 *   Return the sequence number of the most recently queued event.
 *   Used by the phone to request only new events (delta sync).
 *
 * Returned Value:
 *   Last sequence number (0 if no events have been queued).
 *
 ****************************************************************************/

uint32_t ble_sync_get_last_seq(void);

/****************************************************************************
 * Name: ble_sync_start
 *
 * Description:
 *   Begin a sync session.  Called when a BLE connection is established
 *   and the phone sends a SYNC_REQ with its last confirmed sequence.
 *
 *   The module will iterate through the queue and send all events
 *   with seq > last_confirmed_seq.
 *
 * Input Parameters:
 *   last_confirmed_seq - The last sequence number the phone has.
 *                        Events with seq > this will be sent.
 *
 * Returned Value:
 *   Number of events to sync (0 if phone is already up to date).
 *
 ****************************************************************************/

int ble_sync_start(uint32_t last_confirmed_seq);

/****************************************************************************
 * Name: ble_sync_process
 *
 * Description:
 *   Drive the sync state machine forward.  Must be called periodically
 *   (from the BLE task main loop) while a sync is in progress.
 *
 *   This function:
 *   - Sends the next batch of events if in SENDING state
 *   - Handles timeouts and retries if in WAIT_ACK state
 *   - Transitions to DONE or ERROR when complete
 *
 * Returned Value:
 *   Current sync state.
 *
 ****************************************************************************/

enum ble_sync_state ble_sync_process(void);

/****************************************************************************
 * Name: ble_sync_ack
 *
 * Description:
 *   Phone acknowledges receipt of a batch.  Advances the sync cursor
 *   past the acknowledged events.
 *
 * Input Parameters:
 *   received_count - Number of events the phone confirms receiving.
 *
 * Returned Value:
 *   0 on success, -EINVAL if no sync in progress.
 *
 ****************************************************************************/

int ble_sync_ack(uint16_t received_count);

/****************************************************************************
 * Name: ble_sync_get_state
 *
 * Description:
 *   Query the current sync state.
 *
 * Returned Value:
 *   Current sync state.
 *
 ****************************************************************************/

enum ble_sync_state ble_sync_get_state(void);

/****************************************************************************
 * Name: ble_sync_is_complete
 *
 * Description:
 *   Check whether the current sync session is complete (all queued
 *   events sent and acknowledged).
 *
 * Returned Value:
 *   true if sync is complete or idle, false if in progress.
 *
 ****************************************************************************/

bool ble_sync_is_complete(void);

/****************************************************************************
 * Name: ble_sync_get_stats
 *
 * Description:
 *   Retrieve sync statistics.
 *
 * Input Parameters:
 *   stats - Output: sync statistics.
 *
 ****************************************************************************/

void ble_sync_get_stats(struct ble_sync_stats *stats);

/****************************************************************************
 * Name: ble_sync_get_pending_count
 *
 * Description:
 *   Return the number of events waiting to be synced (in the queue
 *   but not yet confirmed by the phone).
 *
 * Returned Value:
 *   Number of pending events.
 *
 ****************************************************************************/

uint32_t ble_sync_get_pending_count(void);

/****************************************************************************
 * Name: ble_sync_on_disconnect
 *
 * Description:
 *   Notify the sync module that the BLE connection was lost.
 *   Resets the sync state machine but preserves the event queue so
 *   that events can be synced on the next connection.
 *
 ****************************************************************************/

void ble_sync_on_disconnect(void);

#endif /* __FIRMWARE_BLE_BLE_SYNC_H */
