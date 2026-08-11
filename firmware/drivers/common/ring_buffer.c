/****************************************************************************
 * Lock-Free Ring Buffer for Sensor Data
 *
 * SPSC lock-free implementation.
 *
 * Memory ordering guarantees:
 *   - head is written by the producer and read by the consumer.
 *     The producer does a write-release on head after writing data.
 *     The consumer does an acquire-read on head before reading data.
 *   - tail is written by the consumer and read by the producer.
 *     The consumer does a write-release on tail after consuming data.
 *     The producer does an acquire-read on tail before writing data.
 *
 * On ARM Cortex-M (single core) these barriers are effectively free but
 * they keep the code correct on SMP platforms (e.g. dual-core openvela).
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdatomic.h>

#include "ring_buffer.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ring_buf_is_power_of_two
 *
 * Description:
 *   Check if a value is a power of two (and not zero).
 *
 ****************************************************************************/

static inline int ring_buf_is_power_of_two(uint32_t val)
{
  return val != 0 && (val & (val - 1)) == 0;
}

/****************************************************************************
 * Name: ring_buf_write_elem
 *
 * Description:
 *   Copy one element into the buffer at the given index.
 *
 ****************************************************************************/

static inline void ring_buf_write_elem(const struct ring_buf *rb,
                                       uint32_t index,
                                       const void *data)
{
  memcpy(rb->buf + (index & rb->mask) * rb->elem_size,
         data, rb->elem_size);
}

/****************************************************************************
 * Name: ring_buf_read_elem
 *
 * Description:
 *   Copy one element from the buffer at the given index.
 *
 ****************************************************************************/

static inline void ring_buf_read_elem(const struct ring_buf *rb,
                                      uint32_t index,
                                      void *data)
{
  memcpy(data,
         rb->buf + (index & rb->mask) * rb->elem_size,
         rb->elem_size);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int ring_buf_init(struct ring_buf *rb, void *storage,
                  uint32_t capacity, uint32_t elem_size)
{
  if (!rb || !storage || !ring_buf_is_power_of_two(capacity) ||
      elem_size == 0 || capacity < 2)
    {
      return -EINVAL;
    }

  rb->buf            = (uint8_t *)storage;
  rb->capacity       = capacity;
  rb->elem_size      = elem_size;
  rb->mask           = capacity - 1;
  rb->head           = 0;
  rb->tail           = 0;
  rb->overflow_count = 0;

  return OK;
}

int ring_buf_push(struct ring_buf *rb, const void *data)
{
  if (!rb || !data)
    {
      return -EINVAL;
    }

  uint32_t head = rb->head;
  uint32_t tail = atomic_load_explicit((_Atomic uint32_t *)&rb->tail,
                                       memory_order_acquire);

  /* Check if buffer is full */

  int ret = OK;

  if (head - tail >= rb->capacity)
    {
      /* Buffer full — drop the oldest element by advancing tail.
       * This keeps the most recent data, which is the right trade-off
       * for real-time sensor streams.
       */

      rb->overflow_count++;
      tail = head - rb->capacity + 1;

      atomic_store_explicit((_Atomic uint32_t *)&rb->tail, tail,
                            memory_order_release);
      ret = -ENOSPC;
    }

  /* Write the element at the current head position */

  ring_buf_write_elem(rb, head, data);

  /* Publish the new head — this release-store makes the data visible
   * to the consumer.
   */

  atomic_store_explicit((_Atomic uint32_t *)&rb->head, head + 1,
                        memory_order_release);

  return ret;
}

int ring_buf_pop(struct ring_buf *rb, void *data)
{
  if (!rb || !data)
    {
      return -EINVAL;
    }

  uint32_t tail = rb->tail;
  uint32_t head = atomic_load_explicit((_Atomic uint32_t *)&rb->head,
                                       memory_order_acquire);

  if (tail == head)
    {
      /* Buffer is empty */

      return -EAGAIN;
    }

  /* Read the element at the current tail position */

  ring_buf_read_elem(rb, tail, data);

  /* Publish the new tail — this release-store makes the slot available
   * to the producer.
   */

  atomic_store_explicit((_Atomic uint32_t *)&rb->tail, tail + 1,
                        memory_order_release);

  return OK;
}

int ring_buf_push_bulk(struct ring_buf *rb, const void *data,
                       uint32_t count)
{
  if (!rb || !data || count == 0)
    {
      return 0;
    }

  const uint8_t *src = (const uint8_t *)data;

  for (uint32_t i = 0; i < count; i++)
    {
      ring_buf_push(rb, src + i * rb->elem_size);
    }

  return (int)count;
}

int ring_buf_pop_bulk(struct ring_buf *rb, void *data,
                      uint32_t max_count)
{
  if (!rb || !data || max_count == 0)
    {
      return 0;
    }

  uint8_t *dst = (uint8_t *)data;
  uint32_t popped = 0;

  for (uint32_t i = 0; i < max_count; i++)
    {
      if (ring_buf_pop(rb, dst + i * rb->elem_size) < 0)
        {
          break;
        }

      popped++;
    }

  return (int)popped;
}

uint32_t ring_buf_count(const struct ring_buf *rb)
{
  if (!rb)
    {
      return 0;
    }

  uint32_t head = atomic_load_explicit(
      (_Atomic uint32_t *)&rb->head, memory_order_acquire);
  uint32_t tail = atomic_load_explicit(
      (_Atomic uint32_t *)&rb->tail, memory_order_acquire);

  uint32_t count = head - tail;

  /* Clamp to capacity in case of concurrent modification edge case */

  if (count > rb->capacity)
    {
      return rb->capacity;
    }

  return count;
}

uint32_t ring_buf_overflow(const struct ring_buf *rb)
{
  if (!rb)
    {
      return 0;
    }

  return rb->overflow_count;
}

void ring_buf_reset(struct ring_buf *rb)
{
  if (!rb)
    {
      return;
    }

  rb->head           = 0;
  rb->tail           = 0;
  rb->overflow_count = 0;
}

int ring_buf_is_empty(const struct ring_buf *rb)
{
  return ring_buf_count(rb) == 0;
}

int ring_buf_is_full(const struct ring_buf *rb)
{
  return ring_buf_count(rb) >= rb->capacity;
}
