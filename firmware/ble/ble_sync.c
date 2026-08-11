/****************************************************************************
 * VelaSense BLE Event Synchronization Protocol — Implementation
 *
 * Manages a ring buffer of events awaiting synchronization and drives
 * the delta-sync protocol with the companion phone.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <string.h>
#include <errno.h>
#include <time.h>
#include <syslog.h>
#include <nuttx/config.h>

#include "ble_sync.h"
#include "gatt_service.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Queue capacity must be a power of two for fast modular indexing */

#define QUEUE_MASK  (CONFIG_BLE_SYNC_QUEUE_SIZE - 1)

#if (CONFIG_BLE_SYNC_QUEUE_SIZE & QUEUE_MASK) != 0
#  error "CONFIG_BLE_SYNC_QUEUE_SIZE must be a power of two"
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Internal sync context */

struct ble_sync_ctx
{
  /* Ring buffer of pending events */

  struct ble_sync_event queue[CONFIG_BLE_SYNC_QUEUE_SIZE];
  volatile uint32_t head;          /* Next write index (producer) */
  volatile uint32_t tail;          /* Next read index (consumer) */
  uint32_t overflow_count;         /* Events lost to overflow */

  /* Sequence counter — monotonically increasing */

  uint32_t next_seq;

  /* Sync state machine */

  enum ble_sync_state state;
  uint32_t sync_cursor;            /* Current seq being sent */
  uint32_t sync_end_seq;           /* Last seq to send in this sync */
  uint16_t batch_sent;             /* Events sent in current batch */
  uint16_t batch_acked;            /* Events ACKed in current batch */
  int      retry_count;            /* Retries for current batch */
  uint32_t last_activity_ms;       /* For timeout detection */

  /* Statistics */

  struct ble_sync_stats stats;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct ble_sync_ctx g_sync;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: queue_count
 *
 * Description:
 *   Return the number of events currently in the ring buffer.
 *
 ****************************************************************************/

static inline uint32_t queue_count(void)
{
  return (g_sync.head - g_sync.tail) & QUEUE_MASK;
}

/****************************************************************************
 * Name: queue_is_full
 *
 * Description:
 *   Check if the queue is full.
 *
 ****************************************************************************/

static inline bool queue_is_full(void)
{
  return queue_count() == (CONFIG_BLE_SYNC_QUEUE_SIZE - 1);
}

/****************************************************************************
 * Name: queue_is_empty
 *
 * Description:
 *   Check if the queue is empty.
 *
 ****************************************************************************/

static inline bool queue_is_empty(void)
{
  return g_sync.head == g_sync.tail;
}

/****************************************************************************
 * Name: queue_push
 *
 * Description:
 *   Push an event into the queue.  If full, drop the oldest.
 *
 ****************************************************************************/

static void queue_push(const struct ble_sync_event *evt)
{
  if (queue_is_full())
    {
      /* Drop oldest: advance tail */

      g_sync.tail = (g_sync.tail + 1) & QUEUE_MASK;
      g_sync.overflow_count++;
      g_sync.stats.total_dropped++;
      syslog(LOG_WARNING, "BLE sync: queue full, dropped seq=%u\n",
             g_sync.queue[g_sync.tail].seq);
    }

  g_sync.queue[g_sync.head] = *evt;
  g_sync.head = (g_sync.head + 1) & QUEUE_MASK;
  g_sync.stats.total_queued++;
}

/****************************************************************************
 * Name: queue_peek_at
 *
 * Description:
 *   Peek at the event at a logical index (0 = oldest).
 *   Returns NULL if index is out of range.
 *
 ****************************************************************************/

static const struct ble_sync_event *queue_peek_at(uint32_t index)
{
  if (index >= queue_count())
    {
      return NULL;
    }

  uint32_t pos = (g_sync.tail + index) & QUEUE_MASK;
  return &g_sync.queue[pos];
}

/****************************************************************************
 * Name: queue_advance_tail
 *
 * Description:
 *   Advance the tail past 'count' events (they have been confirmed).
 *
 ****************************************************************************/

static void queue_advance_tail(uint32_t count)
{
  uint32_t avail = queue_count();
  if (count > avail)
    {
      count = avail;
    }

  g_sync.tail = (g_sync.tail + count) & QUEUE_MASK;
}

/****************************************************************************
 * Name: find_first_after_seq
 *
 * Description:
 *   Find the logical index of the first event with seq > given seq.
 *   Returns (uint32_t)-1 if no such event exists.
 *
 ****************************************************************************/

static uint32_t find_first_after_seq(uint32_t seq)
{
  uint32_t count = queue_count();
  for (uint32_t i = 0; i < count; i++)
    {
      const struct ble_sync_event *evt = queue_peek_at(i);
      if (evt != NULL && evt->seq > seq)
        {
          return i;
        }
    }

  return (uint32_t)-1;
}

/****************************************************************************
 * Name: get_tick_ms
 *
 * Description:
 *   Get current time in milliseconds (NuttX clock).
 *
 ****************************************************************************/

static uint32_t get_tick_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/****************************************************************************
 * Name: send_batch
 *
 * Description:
 *   Send the next batch of events as GATT notifications.
 *   Returns the number of events successfully sent.
 *
 ****************************************************************************/

static int send_batch(void)
{
  int sent = 0;
  uint16_t batch_max = CONFIG_BLE_SYNC_BATCH_MAX;
  uint32_t count = queue_count();

  while (sent < batch_max && g_sync.sync_cursor <= g_sync.sync_end_seq)
    {
      /* Find the event with this sequence number in the queue */

      uint32_t idx = 0;
      const struct ble_sync_event *evt = NULL;
      bool found = false;

      for (uint32_t i = 0; i < count; i++)
        {
          evt = queue_peek_at(i);
          if (evt != NULL && evt->seq == g_sync.sync_cursor)
            {
              idx = i;
              found = true;
              break;
            }
        }

      if (!found)
        {
          /* Sequence gap — skip */

          g_sync.sync_cursor++;
          continue;
        }

      /* Send as GATT notification via ble_gatt_notify_event.
       * For the sync path, we directly notify the payload.
       */

      int ret = ble_gatt_notify_event();
      if (ret < 0)
        {
          syslog(LOG_WARNING, "BLE sync: notify failed (%d)\n", ret);
          break;
        }

      sent++;
      g_sync.sync_cursor++;
      g_sync.stats.total_synced++;
    }

  return sent;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ble_sync_init
 ****************************************************************************/

int ble_sync_init(void)
{
  memset(&g_sync, 0, sizeof(g_sync));
  g_sync.state     = BLE_SYNC_IDLE;
  g_sync.next_seq  = 1;  /* Sequences start at 1; 0 means "none" */

  syslog(LOG_INFO, "BLE sync: initialized (queue=%d, batch=%d)\n",
         CONFIG_BLE_SYNC_QUEUE_SIZE, CONFIG_BLE_SYNC_BATCH_MAX);

  return 0;
}

/****************************************************************************
 * Name: ble_sync_deinit
 ****************************************************************************/

void ble_sync_deinit(void)
{
  memset(&g_sync, 0, sizeof(g_sync));
  syslog(LOG_INFO, "BLE sync: deinitialized\n");
}

/****************************************************************************
 * Name: ble_sync_queue_event
 ****************************************************************************/

uint32_t ble_sync_queue_event(const struct ble_event_notify *notify)
{
  if (notify == NULL)
    {
      return 0;
    }

  struct ble_sync_event evt;
  evt.seq    = g_sync.next_seq++;
  evt.notify = *notify;

  queue_push(&evt);

  syslog(LOG_DEBUG, "BLE sync: queued event seq=%u label=%u conf=%u\n",
         evt.seq, notify->label, notify->confidence);

  return evt.seq;
}

/****************************************************************************
 * Name: ble_sync_get_last_seq
 ****************************************************************************/

uint32_t ble_sync_get_last_seq(void)
{
  if (g_sync.next_seq == 0)
    {
      return 0;
    }

  return g_sync.next_seq - 1;
}

/****************************************************************************
 * Name: ble_sync_start
 ****************************************************************************/

int ble_sync_start(uint32_t last_confirmed_seq)
{
  uint32_t first_idx = find_first_after_seq(last_confirmed_seq);
  int count;

  if (first_idx == (uint32_t)-1)
    {
      /* Phone is already up to date */

      g_sync.state = BLE_SYNC_DONE;
      g_sync.stats.sync_count++;
      syslog(LOG_INFO, "BLE sync: phone up to date (last=%u)\n",
             last_confirmed_seq);
      return 0;
    }

  count = (int)(queue_count() - first_idx);
  if (count <= 0)
    {
      g_sync.state = BLE_SYNC_DONE;
      g_sync.stats.sync_count++;
      return 0;
    }

  /* Find the sequence of the last event in the queue */

  const struct ble_sync_event *last_evt =
    queue_peek_at(queue_count() - 1);

  g_sync.state          = BLE_SYNC_SENDING;
  g_sync.sync_cursor    = last_confirmed_seq + 1;
  g_sync.sync_end_seq   = last_evt->seq;
  g_sync.batch_sent     = 0;
  g_sync.batch_acked    = 0;
  g_sync.retry_count    = 0;
  g_sync.last_activity_ms = get_tick_ms();

  syslog(LOG_INFO, "BLE sync: starting sync (%d events, seq %u-%u)\n",
         count, g_sync.sync_cursor, g_sync.sync_end_seq);

  return count;
}

/****************************************************************************
 * Name: ble_sync_process
 ****************************************************************************/

enum ble_sync_state ble_sync_process(void)
{
  uint32_t now;

  switch (g_sync.state)
    {
      case BLE_SYNC_IDLE:
      case BLE_SYNC_DONE:
      case BLE_SYNC_ERROR:
        break;

      case BLE_SYNC_SENDING:
        {
          g_sync.batch_sent = (uint16_t)send_batch();

          if (g_sync.batch_sent > 0)
            {
              g_sync.state = BLE_SYNC_WAIT_ACK;
              g_sync.batch_acked = 0;
              g_sync.last_activity_ms = get_tick_ms();
            }
          else if (g_sync.sync_cursor > g_sync.sync_end_seq)
            {
              /* All events sent */

              g_sync.state = BLE_SYNC_DONE;
              g_sync.stats.sync_count++;
              syslog(LOG_INFO, "BLE sync: complete\n");
            }
          else
            {
              /* Could not send — connection issue */

              g_sync.state = BLE_SYNC_ERROR;
              g_sync.stats.error_count++;
              syslog(LOG_ERR, "BLE sync: send failed\n");
            }
        }
        break;

      case BLE_SYNC_WAIT_ACK:
        {
          now = get_tick_ms();
          if ((now - g_sync.last_activity_ms) >
              CONFIG_BLE_SYNC_TIMEOUT_MS)
            {
              g_sync.retry_count++;
              g_sync.stats.total_retries++;

              if (g_sync.retry_count >= CONFIG_BLE_SYNC_MAX_RETRIES)
                {
                  g_sync.state = BLE_SYNC_ERROR;
                  g_sync.stats.error_count++;
                  syslog(LOG_ERR, "BLE sync: max retries exceeded\n");
                }
              else
                {
                  /* Retry: go back to SENDING to resend the batch */

                  syslog(LOG_WARNING,
                         "BLE sync: timeout, retry %d/%d\n",
                         g_sync.retry_count,
                         CONFIG_BLE_SYNC_MAX_RETRIES);

                  /* Reset cursor to resend from where we left off */

                  g_sync.sync_cursor -= g_sync.batch_sent;
                  g_sync.state = BLE_SYNC_SENDING;
                  g_sync.last_activity_ms = now;
                }
            }
        }
        break;

      default:
        g_sync.state = BLE_SYNC_IDLE;
        break;
    }

  return g_sync.state;
}

/****************************************************************************
 * Name: ble_sync_ack
 ****************************************************************************/

int ble_sync_ack(uint16_t received_count)
{
  if (g_sync.state != BLE_SYNC_WAIT_ACK)
    {
      return -EINVAL;
    }

  g_sync.batch_acked = received_count;
  g_sync.retry_count = 0;

  /* The events have been confirmed by the phone.
   * If all events in the queue have been synced, we can clear them.
   * Otherwise, advance the tail past confirmed events.
   */

  if (g_sync.sync_cursor > g_sync.sync_end_seq &&
      received_count >= g_sync.batch_sent)
    {
      /* All events in this sync session confirmed */

      uint32_t total_to_confirm = queue_count();
      queue_advance_tail(total_to_confirm);

      g_sync.state = BLE_SYNC_DONE;
      g_sync.stats.sync_count++;
      syslog(LOG_INFO, "BLE sync: ACK received, sync complete\n");
    }
  else
    {
      /* More events to send */

      g_sync.state = BLE_SYNC_SENDING;
      g_sync.last_activity_ms = get_tick_ms();
      syslog(LOG_DEBUG, "BLE sync: ACK %u events, continuing\n",
             received_count);
    }

  return 0;
}

/****************************************************************************
 * Name: ble_sync_get_state
 ****************************************************************************/

enum ble_sync_state ble_sync_get_state(void)
{
  return g_sync.state;
}

/****************************************************************************
 * Name: ble_sync_is_complete
 ****************************************************************************/

bool ble_sync_is_complete(void)
{
  return g_sync.state == BLE_SYNC_DONE ||
         g_sync.state == BLE_SYNC_IDLE;
}

/****************************************************************************
 * Name: ble_sync_get_stats
 ****************************************************************************/

void ble_sync_get_stats(struct ble_sync_stats *stats)
{
  if (stats != NULL)
    {
      *stats = g_sync.stats;
    }
}

/****************************************************************************
 * Name: ble_sync_get_pending_count
 ****************************************************************************/

uint32_t ble_sync_get_pending_count(void)
{
  return queue_count();
}

/****************************************************************************
 * Name: ble_sync_on_disconnect
 ****************************************************************************/

void ble_sync_on_disconnect(void)
{
  /* Reset sync state but keep the event queue intact.
   * Events will be synced on the next connection.
   */

  g_sync.state        = BLE_SYNC_IDLE;
  g_sync.batch_sent   = 0;
  g_sync.batch_acked  = 0;
  g_sync.retry_count  = 0;

  syslog(LOG_INFO, "BLE sync: disconnected (%u events still pending)\n",
         queue_count());
}
