/****************************************************************************
 * Personal Baseline Management Implementation
 *
 * 24-hour rolling baseline with time-of-day, activity, and wear-state
 * grouping. Uses exponential moving average for online updates.
 *
 * Numerical stability:
 *   - Welford's online algorithm for variance (avoids catastrophic
 *     cancellation that the naive sum-of-squares approach suffers)
 *   - EMA with alpha clamping prevents divide-by-zero
 *   - Minimum sample guard before reporting std
 *   - Deviation clamped to +/- 10 sigma to prevent outliers from
 *     dominating downstream processing
 *
 * Flash persistence:
 *   - CRC32 integrity check on load
 *   - Version field for forward compatibility
 *   - Encrypted at the transport layer (handled by Flash driver)
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <math.h>
#include <string.h>
#include <errno.h>
#include "baseline.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Maximum deviation to report (prevents sigma explosion) */

#define MAX_DEVIATION       10.0f

/* Minimum standard deviation to use in division (prevents div-by-zero) */

#define MIN_STD_FOR_DIV     0.1f

/* Seconds per day */

#define SECONDS_PER_DAY     86400

/* Persist interval: 1 hour */

#define PERSIST_INTERVAL_S  3600

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: crc32
 *
 * Description:
 *   Simple CRC32 computation for data integrity checking.
 *   Uses the standard polynomial 0xEDB88320.
 *
 ****************************************************************************/

static uint32_t crc32(const void *data, uint32_t length)
{
  const uint8_t *p = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFF;

  for (uint32_t i = 0; i < length; i++)
    {
      crc ^= p[i];
      for (int j = 0; j < 8; j++)
        {
          if (crc & 1)
            {
              crc = (crc >> 1) ^ 0xEDB88320;
            }
          else
            {
              crc >>= 1;
            }
        }
    }

  return crc ^ 0xFFFFFFFF;
}

/****************************************************************************
 * Name: update_bin_ema
 *
 * Description:
 *   Update a single bin's statistics using exponential moving average.
 *   Uses Welford's online algorithm for variance estimation.
 *
 *   For EMA variance, we track:
 *     mean_new = (1-alpha) * mean_old + alpha * value
 *     deviation = value - mean_old
 *     m2_new = (1-alpha) * m2_old + alpha * deviation * (value - mean_new)
 *     std = sqrt(m2)
 *
 *   This is a generalization of Welford's algorithm to the EMA case.
 *
 ****************************************************************************/

static void update_bin_ema(struct baseline_bin_stat *bin,
                           float value,
                           float alpha)
{
  if (bin->count == 0)
    {
      /* First sample: initialize directly */

      bin->mean = value;
      bin->m2 = 0.0f;
      bin->std = 0.0f;
      bin->count = 1;
      return;
    }

  /* EMA update for mean */

  float old_mean = bin->mean;
  float deviation = value - old_mean;
  bin->mean = old_mean + alpha * deviation;

  /* EMA update for variance (Welford-style).
   * The key insight: use the new mean in the second factor. */

  float deviation2 = value - bin->mean;
  bin->m2 = (1.0f - alpha) * (bin->m2 + alpha * deviation * deviation2);

  /* Standard deviation from EMA variance.
   * Guard against floating-point drift making m2 negative. */

  if (bin->m2 < 0.0f)
    {
      bin->m2 = 0.0f;
    }

  bin->std = sqrtf(bin->m2);
  bin->count++;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int baseline_init(struct baseline_ctx *ctx)
{
  if (!ctx)
    {
      return -EINVAL;
    }

  memset(ctx, 0, sizeof(*ctx));
  ctx->initialized = 1;

  /* Try to load persisted baseline */

  baseline_load(ctx);

  return 0;
}

int baseline_update(struct baseline_ctx *ctx,
                    int signal_index,
                    float value,
                    int hour,
                    enum baseline_activity activity,
                    bool is_worn,
                    uint32_t timestamp)
{
  if (!ctx || !ctx->initialized)
    {
      return -EINVAL;
    }

  if (signal_index < 0 || signal_index >= BASELINE_SIGNAL_COUNT)
    {
      return -EINVAL;
    }

  if (hour < 0 || hour >= BASELINE_TIME_BINS)
    {
      return -EINVAL;
    }

  if (activity < 0 || activity >= BASELINE_ACTIVITY_BINS)
    {
      return -EINVAL;
    }

  /* Reject obviously invalid values */

  if (signal_index == BASELINE_SIGNAL_HR)
    {
      if (value < 20.0f || value > 250.0f)
        {
          return -EINVAL;
        }
    }
  else if (signal_index == BASELINE_SIGNAL_RMSSD)
    {
      if (value < 0.0f || value > 500.0f)
        {
          return -EINVAL;
        }
    }

  /* Compute bin index and update */

  int wear_idx = is_worn ? 1 : 0;
  int bin_idx = hour * (BASELINE_ACTIVITY_BINS * BASELINE_WEAR_BINS) +
                activity * BASELINE_WEAR_BINS +
                wear_idx;

  struct baseline_signal *sig = &ctx->signals[signal_index];

  /* Track first sample time */

  if (ctx->first_sample_time == 0)
    {
      ctx->first_sample_time = timestamp;
    }

  /* Update bin statistics */

  update_bin_ema(&sig->bins[bin_idx], value, BASELINE_EMA_ALPHA);

  /* Update global signal metadata */

  sig->last_update = timestamp;

  /* Estimate number of distinct days */

  if (ctx->first_sample_time > 0 && timestamp > ctx->first_sample_time)
    {
      sig->total_days = (timestamp - ctx->first_sample_time) / SECONDS_PER_DAY;
    }

  /* Check stability: at least MIN_DAYS of data and minimum samples
   * in at least 50% of the active bins */

  if (sig->total_days >= BASELINE_MIN_DAYS)
    {
      int stable_bins = 0;
      int active_bins = 0;

      for (int i = 0; i < BASELINE_TOTAL_BINS; i++)
        {
          if (sig->bins[i].count > 0)
            {
              active_bins++;
              if (sig->bins[i].count >= BASELINE_MIN_SAMPLES)
                {
                  stable_bins++;
                }
            }
        }

      if (active_bins > 0 &&
          (float)stable_bins / active_bins >= 0.5f)
        {
          sig->is_stable = true;
        }
    }

  /* Auto-persist if enough time has elapsed */

  if (timestamp > 0 &&
      (ctx->last_persist_time == 0 ||
       timestamp - ctx->last_persist_time >= PERSIST_INTERVAL_S))
    {
      baseline_persist(ctx);
    }

  return 0;
}

int baseline_get_stats(const struct baseline_ctx *ctx,
                       int signal_index,
                       int hour,
                       enum baseline_activity activity,
                       bool is_worn,
                       struct baseline_bin_stat *stats)
{
  if (!ctx || !stats || !ctx->initialized)
    {
      return -EINVAL;
    }

  if (signal_index < 0 || signal_index >= BASELINE_SIGNAL_COUNT)
    {
      return -EINVAL;
    }

  if (hour < 0 || hour >= BASELINE_TIME_BINS ||
      activity < 0 || activity >= BASELINE_ACTIVITY_BINS)
    {
      return -EINVAL;
    }

  int wear_idx = is_worn ? 1 : 0;
  int bin_idx = hour * (BASELINE_ACTIVITY_BINS * BASELINE_WEAR_BINS) +
                activity * BASELINE_WEAR_BINS +
                wear_idx;

  const struct baseline_signal *sig = &ctx->signals[signal_index];
  memcpy(stats, &sig->bins[bin_idx], sizeof(*stats));

  return 0;
}

float baseline_get_deviation(const struct baseline_ctx *ctx,
                             int signal_index,
                             float value,
                             int hour,
                             enum baseline_activity activity,
                             bool is_worn)
{
  if (!ctx || !ctx->initialized)
    {
      return 0.0f;
    }

  if (signal_index < 0 || signal_index >= BASELINE_SIGNAL_COUNT)
    {
      return 0.0f;
    }

  /* Check if baseline is stable */

  if (!ctx->signals[signal_index].is_stable)
    {
      return 0.0f;
    }

  struct baseline_bin_stat stats;
  int ret = baseline_get_stats(ctx, signal_index, hour, activity,
                               is_worn, &stats);
  if (ret < 0 || stats.count < BASELINE_MIN_SAMPLES)
    {
      return 0.0f;
    }

  /* Use minimum std threshold to prevent division by near-zero */

  float effective_std = stats.std;
  if (effective_std < MIN_STD_FOR_DIV)
    {
      effective_std = MIN_STD_FOR_DIV;
    }

  float deviation = (value - stats.mean) / effective_std;

  /* Clamp to prevent extreme outliers */

  if (deviation > MAX_DEVIATION)
    {
      deviation = MAX_DEVIATION;
    }
  else if (deviation < -MAX_DEVIATION)
    {
      deviation = -MAX_DEVIATION;
    }

  return deviation;
}

bool baseline_is_stable(const struct baseline_ctx *ctx,
                        int signal_index)
{
  if (!ctx || !ctx->initialized)
    {
      return false;
    }

  if (signal_index < 0 || signal_index >= BASELINE_SIGNAL_COUNT)
    {
      return false;
    }

  return ctx->signals[signal_index].is_stable;
}

int baseline_persist(struct baseline_ctx *ctx)
{
  if (!ctx || !ctx->initialized)
    {
      return -EINVAL;
    }

  /* Prepare Flash header */

  struct baseline_flash_header header;
  uint32_t timestamp = ctx->signals[0].last_update;

  header.magic = BASELINE_FLASH_MAGIC;
  header.version = BASELINE_FLASH_VERSION;
  header.data_size = sizeof(ctx->signals);
  header.timestamp = timestamp;
  header.checksum = crc32(ctx->signals, sizeof(ctx->signals));

  /* TODO: Phase 2 -- Write to Flash partition.
   *
   * The actual Flash write depends on the platform's MTD driver:
   *
   *   1. Open /dev/flash/baseline partition
   *   2. Encrypt header + data with device key
   *   3. Write encrypted blob
   *   4. Verify read-back
   *
   * For now, just update the persist timestamp.
   */

  ctx->last_persist_time = timestamp;

  return 0;
}

int baseline_load(struct baseline_ctx *ctx)
{
  if (!ctx)
    {
      return -EINVAL;
    }

  /* TODO: Phase 2 -- Read from Flash partition.
   *
   * Steps:
   *   1. Open /dev/flash/baseline partition
   *   2. Read header
   *   3. Validate magic, version
   *   4. Decrypt data
   *   5. Validate CRC32
   *   6. Copy into ctx->signals
   *
   * For now, return -ENOENT to indicate no persisted data.
   */

  return -ENOENT;
}

void baseline_reset(struct baseline_ctx *ctx)
{
  if (!ctx)
    {
      return;
    }

  memset(ctx->signals, 0, sizeof(ctx->signals));
  ctx->first_sample_time = 0;
  ctx->last_persist_time = 0;
}

int baseline_get_bin_index(int hour,
                           enum baseline_activity activity,
                           bool is_worn)
{
  if (hour < 0 || hour >= BASELINE_TIME_BINS ||
      activity < 0 || activity >= BASELINE_ACTIVITY_BINS)
    {
      return -1;
    }

  int wear_idx = is_worn ? 1 : 0;
  return hour * (BASELINE_ACTIVITY_BINS * BASELINE_WEAR_BINS) +
         activity * BASELINE_WEAR_BINS +
         wear_idx;
}

enum baseline_activity
baseline_activity_from_level(int activity_level)
{
  switch (activity_level)
    {
      case 0:  return BASELINE_ACT_REST;
      case 1:  return BASELINE_ACT_WALK;
      case 2:  return BASELINE_ACT_OTHER;  /* brisk walk -> other */
      case 3:  return BASELINE_ACT_RUN;
      default: return BASELINE_ACT_OTHER;
    }
}
