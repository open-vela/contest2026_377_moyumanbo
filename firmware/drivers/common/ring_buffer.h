/****************************************************************************
 * Lock-Free Ring Buffer for Sensor Data
 *
 * Single-producer, single-consumer (SPSC) lock-free ring buffer designed
 * for passing sensor samples from interrupt context (producer) to task
 * context (consumer) without mutexes or disabling interrupts.
 *
 * Design constraints:
 *   - Capacity must be a power of two (enforced at init)
 *   - Head index written only by producer, tail index only by consumer
 *   - Memory barriers ensure visibility across cores / ISR-to-task boundary
 *   - Overflow is detected and counted (oldest sample is dropped)
 *
 * Bulk push/pop are provided for FIFO-based sensors (MAX86141, ICM42688)
 * that deliver multiple samples per interrupt.
 *
 ****************************************************************************/

#ifndef __FIRMWARE_DRIVERS_COMMON_RING_BUFFER_H
#define __FIRMWARE_DRIVERS_COMMON_RING_BUFFER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stddef.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Maximum supported ring buffer capacity (must be power of two).
 * This is a compile-time upper bound; actual capacity is set at init.
 */

#define RING_BUF_MAX_CAPACITY  1024

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Ring buffer context.
 * The caller allocates this struct and passes a pointer to the init
 * function.  The data storage is also caller-provided.
 */

struct ring_buf
{
  uint8_t  *buf;            /* Pointer to caller-allocated data storage */
  uint32_t  capacity;       /* Number of elements (must be power of two) */
  uint32_t  elem_size;      /* Size of one element in bytes */
  uint32_t  mask;           /* capacity - 1, for fast modular arithmetic */
  volatile uint32_t head;   /* Write index — only modified by producer */
  volatile uint32_t tail;   /* Read index  — only modified by consumer */
  uint32_t  overflow_count; /* Number of elements lost due to overflow */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: ring_buf_init
 *
 * Description:
 *   Initialize a ring buffer with the given storage and capacity.
 *   Capacity must be a power of two and at least 2.
 *
 * Input Parameters:
 *   rb         - Ring buffer context to initialize
 *   storage    - Pointer to caller-allocated memory for elements.
 *                Must be at least (capacity * elem_size) bytes.
 *   capacity   - Number of elements (must be a power of two)
 *   elem_size  - Size of one element in bytes
 *
 * Returned Value:
 *   0 on success, -EINVAL on bad parameters.
 *
 ****************************************************************************/

int ring_buf_init(struct ring_buf *rb, void *storage,
                  uint32_t capacity, uint32_t elem_size);

/****************************************************************************
 * Name: ring_buf_push
 *
 * Description:
 *   Push one element into the ring buffer.
 *   If the buffer is full, the oldest element is overwritten and
 *   the overflow counter is incremented.
 *
 * Input Parameters:
 *   rb    - Ring buffer context
 *   data  - Pointer to the element to push (copied into the buffer)
 *
 * Returned Value:
 *   0 on success, -ENOSPC if buffer was full (element still pushed,
 *   oldest overwritten).
 *
 ****************************************************************************/

int ring_buf_push(struct ring_buf *rb, const void *data);

/****************************************************************************
 * Name: ring_buf_pop
 *
 * Description:
 *   Pop one element from the ring buffer.
 *
 * Input Parameters:
 *   rb    - Ring buffer context
 *   data  - Pointer to storage for the popped element
 *
 * Returned Value:
 *   0 on success, -EAGAIN if buffer is empty.
 *
 ****************************************************************************/

int ring_buf_pop(struct ring_buf *rb, void *data);

/****************************************************************************
 * Name: ring_buf_push_bulk
 *
 * Description:
 *   Push multiple elements into the ring buffer.
 *   If the buffer fills, oldest elements are overwritten.
 *   Useful for FIFO-based sensors that deliver N samples per interrupt.
 *
 * Input Parameters:
 *   rb    - Ring buffer context
 *   data  - Pointer to array of elements to push
 *   count - Number of elements to push
 *
 * Returned Value:
 *   Number of elements actually pushed (always == count).
 *
 ****************************************************************************/

int ring_buf_push_bulk(struct ring_buf *rb, const void *data,
                       uint32_t count);

/****************************************************************************
 * Name: ring_buf_pop_bulk
 *
 * Description:
 *   Pop up to max_count elements from the ring buffer.
 *   Useful for batch-processing accumulated sensor samples.
 *
 * Input Parameters:
 *   rb         - Ring buffer context
 *   data       - Pointer to output array
 *   max_count  - Maximum number of elements to pop
 *
 * Returned Value:
 *   Number of elements actually popped (0 if buffer was empty).
 *
 ****************************************************************************/

int ring_buf_pop_bulk(struct ring_buf *rb, void *data,
                      uint32_t max_count);

/****************************************************************************
 * Name: ring_buf_count
 *
 * Description:
 *   Return the number of elements currently in the ring buffer.
 *   This is a snapshot and may change immediately if the producer
 *   is concurrently pushing.
 *
 * Input Parameters:
 *   rb - Ring buffer context
 *
 * Returned Value:
 *   Number of elements available for reading.
 *
 ****************************************************************************/

uint32_t ring_buf_count(const struct ring_buf *rb);

/****************************************************************************
 * Name: ring_buf_overflow
 *
 * Description:
 *   Return the cumulative overflow count since the last init.
 *   Each overflow represents one lost sample.
 *
 * Input Parameters:
 *   rb - Ring buffer context
 *
 * Returned Value:
 *   Total number of overflowed elements.
 *
 ****************************************************************************/

uint32_t ring_buf_overflow(const struct ring_buf *rb);

/****************************************************************************
 * Name: ring_buf_reset
 *
 * Description:
 *   Reset the ring buffer to empty state. Clears the overflow counter.
 *   Must not be called while the producer or consumer is active.
 *
 ****************************************************************************/

void ring_buf_reset(struct ring_buf *rb);

/****************************************************************************
 * Name: ring_buf_is_empty
 *
 * Description:
 *   Check whether the ring buffer is empty.
 *
 * Returned Value:
 *   1 if empty, 0 otherwise.
 *
 ****************************************************************************/

int ring_buf_is_empty(const struct ring_buf *rb);

/****************************************************************************
 * Name: ring_buf_is_full
 *
 * Description:
 *   Check whether the ring buffer is full.
 *
 * Returned Value:
 *   1 if full, 0 otherwise.
 *
 ****************************************************************************/

int ring_buf_is_full(const struct ring_buf *rb);

#endif /* __FIRMWARE_DRIVERS_COMMON_RING_BUFFER_H */
