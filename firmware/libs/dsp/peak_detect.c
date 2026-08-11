/****************************************************************************
 * Adaptive Peak Detection for PPG Signals — Implementation
 *
 * Detects systolic peaks via first-derivative zero-crossing with
 * adaptive thresholding, IBI outlier rejection, and motion-aware
 * quality flags.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <math.h>
#include <string.h>
#include "peak_detect.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Exponential moving average coefficient for peak/trough tracking.
 * Smaller = smoother but slower to adapt. 0.125 = ~8-sample time constant.
 */

#define EMA_ALPHA               0.125f

/* Minimum number of IBIs before we can do outlier rejection */

#define MIN_IBI_FOR_OUTLIER     3

/* Minimum number of IBI samples for reliable median computation */

#define MIN_IBI_FOR_MEDIAN      3

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: compute_median_ibi
 *
 * Description:
 *   Compute the median of the IBI history buffer. Uses a simple
 *   insertion-sort on a temporary copy (fast for small N).
 *
 ****************************************************************************/

static float compute_median_ibi(const struct peak_detect_state *state)
{
  if (state->ibi_count < MIN_IBI_FOR_MEDIAN)
    {
      return 0.0f;
    }

  /* Copy history into temporary array for sorting */

  float tmp[PEAK_DETECT_IBI_HISTORY];
  int n = state->ibi_count;

  if (n > PEAK_DETECT_IBI_HISTORY)
    {
      n = PEAK_DETECT_IBI_HISTORY;
    }

  for (int i = 0; i < n; i++)
    {
      tmp[i] = state->ibi_history[i];
    }

  /* Insertion sort (fast for small n) */

  for (int i = 1; i < n; i++)
    {
      float key = tmp[i];
      int j = i - 1;

      while (j >= 0 && tmp[j] > key)
        {
          tmp[j + 1] = tmp[j];
          j--;
        }

      tmp[j + 1] = key;
    }

  /* Return median */

  if (n & 1)
    {
      return tmp[n / 2];
    }
  else
    {
      return (tmp[n / 2 - 1] + tmp[n / 2]) * 0.5f;
    }
}

/****************************************************************************
 * Name: compute_mean_ibi
 *
 * Description:
 *   Compute the mean of the IBI history buffer.
 *
 ****************************************************************************/

static float compute_mean_ibi(const struct peak_detect_state *state)
{
  if (state->ibi_count < 1)
    {
      return 0.0f;
    }

  float sum = 0.0f;
  int n = state->ibi_count;

  if (n > PEAK_DETECT_IBI_HISTORY)
    {
      n = PEAK_DETECT_IBI_HISTORY;
    }

  for (int i = 0; i < n; i++)
    {
      sum += state->ibi_history[i];
    }

  return sum / n;
}

/****************************************************************************
 * Name: store_ibi
 *
 * Description:
 *   Store a new IBI value in the circular history buffer.
 *
 ****************************************************************************/

static void store_ibi(struct peak_detect_state *state, float ibi_ms)
{
  state->ibi_history[state->ibi_write_idx] = ibi_ms;
  state->ibi_write_idx =
    (state->ibi_write_idx + 1) & (PEAK_DETECT_IBI_HISTORY - 1);

  if (state->ibi_count < PEAK_DETECT_IBI_HISTORY)
    {
      state->ibi_count++;
    }
}

/****************************************************************************
 * Name: is_ibi_outlier
 *
 * Description:
 *   Check if an IBI value is an outlier relative to the median.
 *   Returns 1 if IBI > median * high_ratio or IBI < median * low_ratio.
 *
 ****************************************************************************/

static int is_ibi_outlier(const struct peak_detect_state *state, float ibi_ms)
{
  if (state->ibi_count < MIN_IBI_FOR_OUTLIER)
    {
      return 0; /* Not enough history to judge */
    }

  float median = compute_median_ibi(state);

  if (median < 1.0f)
    {
      return 0;
    }

  if (ibi_ms > median * state->config.outlier_high ||
      ibi_ms < median * state->config.outlier_low)
    {
      return 1;
    }

  return 0;
}

/****************************************************************************
 * Name: estimate_local_snr
 *
 * Description:
 *   Estimate local signal-to-noise ratio around a peak.
 *   Computed as peak amplitude divided by the local noise floor
 *   (RMS of samples in a window around the peak, excluding the peak
 *   itself).
 *
 ****************************************************************************/

static float estimate_local_snr(const float *ppg_data, int count,
                                int peak_idx, float peak_amp)
{
  /* Use a window of +/- 15 samples (~300ms at 100Hz) */

  int half_win = 15;
  int start = peak_idx - half_win;
  int end = peak_idx + half_win;

  if (start < 0)
    {
      start = 0;
    }

  if (end >= count)
    {
      end = count - 1;
    }

  /* Compute RMS excluding a 5-sample region around the peak */

  float sum_sq = 0.0f;
  int n = 0;

  for (int i = start; i <= end; i++)
    {
      int dist = i - peak_idx;

      if (dist < 0)
        {
          dist = -dist;
        }

      if (dist <= 2)
        {
          continue; /* Skip the peak itself */
        }

      sum_sq += ppg_data[i] * ppg_data[i];
      n++;
    }

  if (n < 1)
    {
      return 10.0f; /* Default high SNR if window is too small */
    }

  float noise_rms = sqrtf(sum_sq / n);

  if (noise_rms < 1e-6f)
    {
      return 20.0f; /* Very high SNR (near-zero noise) */
    }

  return fabsf(peak_amp) / noise_rms;
}

/****************************************************************************
 * Name: emit_peak
 *
 * Description:
 *   Populate a peak result structure and add it to the result set.
 *
 ****************************************************************************/

static void emit_peak(struct peak_detect_result *result,
                      const struct peak_detect_state *state,
                      int32_t sample_index, float amplitude,
                      float ibi_ms, float local_snr,
                      uint8_t quality)
{
  if (result->num_peaks >= PEAK_DETECT_MAX_PEAKS)
    {
      return;
    }

  struct peak_detect_peak *p = &result->peaks[result->num_peaks];

  p->sample_index = (uint32_t)sample_index;
  p->timestamp_us = (uint64_t)sample_index * 1000000ULL /
                    (uint64_t)state->config.sample_rate;
  p->amplitude = amplitude;
  p->ibi_ms = ibi_ms;
  p->local_snr = local_snr;
  p->quality = quality;

  result->num_peaks++;
}

/****************************************************************************
 * Name: accept_peak
 *
 * Description:
 *   Called when a peak candidate is confirmed. Computes IBI, performs
 *   outlier check, and emits the peak with appropriate quality flags.
 *
 ****************************************************************************/

static void accept_peak(struct peak_detect_state *state,
                        struct peak_detect_result *result,
                        const float *ppg_data, int count,
                        const float *motion_mag)
{
  int32_t idx = state->candidate_index;
  float amp = state->candidate_amp;

  /* Compute IBI in milliseconds */

  float ibi_ms = 0.0f;
  uint8_t quality = PEAK_QUALITY_VALID;

  if (state->have_last_peak)
    {
      int32_t delta_samples = idx - state->last_peak_index;
      ibi_ms = (float)delta_samples * 1000.0f / state->config.sample_rate;

      /* Reject if IBI is outside physical limits */

      if (ibi_ms < state->config.min_ibi_ms ||
          ibi_ms > state->config.max_ibi_ms)
        {
          /* Do not emit, but still update internal state so we don't
           * get permanently stuck.
           */

          state->last_peak_index = idx;
          state->candidate_valid = 0;
          return;
        }

      /* Check for outlier IBI */

      if (is_ibi_outlier(state, ibi_ms))
        {
          quality |= PEAK_QUALITY_OUTLIER;
        }
    }

  /* Estimate local SNR */

  float snr = estimate_local_snr(ppg_data, count, idx, amp);

  if (snr < state->config.snr_threshold)
    {
      quality |= PEAK_QUALITY_LOW_SNR;
    }

  /* Check motion contamination */

  if (motion_mag && idx >= 0 && idx < count)
    {
      if (motion_mag[idx] > state->config.motion_threshold)
        {
          quality |= PEAK_QUALITY_MOTION;
        }
    }
  else if (state->motion_magnitude > state->config.motion_threshold)
    {
      quality |= PEAK_QUALITY_MOTION;
    }

  /* Emit the peak */

  emit_peak(result, state, idx, amp, ibi_ms, snr, quality);

  /* Update state */

  if (ibi_ms > 0.0f)
    {
      store_ibi(state, ibi_ms);
    }

  state->last_peak_index = idx;
  state->have_last_peak = 1;
  state->candidate_valid = 0;
}

/****************************************************************************
 * Name: check_missing_beat
 *
 * Description:
 *   After processing a buffer, check if a beat is likely missing
 *   (last peak was too long ago). Interpolates a synthetic peak.
 *
 ****************************************************************************/

static void check_missing_beat(struct peak_detect_state *state,
                               struct peak_detect_result *result,
                               int32_t buffer_end_index)
{
  if (!state->have_last_peak || state->ibi_count < MIN_IBI_FOR_MEDIAN)
    {
      return;
    }

  float median_ibi = compute_median_ibi(state);

  if (median_ibi < 1.0f)
    {
      return;
    }

  /* How many samples since last peak? */

  int32_t elapsed = buffer_end_index - state->last_peak_index;
  float elapsed_ms = (float)elapsed * 1000.0f / state->config.sample_rate;

  /* If elapsed time exceeds 1.5x the median IBI, a beat was likely missed */

  if (elapsed_ms > median_ibi * state->config.outlier_high)
    {
      /* Interpolate a peak at the expected position */

      int32_t interp_idx = state->last_peak_index +
        (int32_t)(median_ibi * state->config.sample_rate / 1000.0f);

      if (interp_idx < buffer_end_index)
        {
          uint8_t quality = PEAK_QUALITY_INTERP | PEAK_QUALITY_VALID;
          emit_peak(result, state, interp_idx, 0.0f, median_ibi, 0.0f,
                    quality);

          /* Update last peak index to the interpolated position */

          state->last_peak_index = interp_idx;
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int peak_detect_init(const struct peak_detect_config *config,
                     struct peak_detect_state *state)
{
  if (!config || !state)
    {
      return -1;
    }

  if (config->sample_rate <= 0.0f)
    {
      return -1;
    }

  memset(state, 0, sizeof(*state));

  /* Copy configuration with defaults for unset fields */

  state->config = *config;

  if (state->config.min_ibi_ms <= 0.0f)
    {
      state->config.min_ibi_ms = 300.0f;
    }

  if (state->config.max_ibi_ms <= 0.0f)
    {
      state->config.max_ibi_ms = 2000.0f;
    }

  if (state->config.threshold_factor <= 0.0f)
    {
      state->config.threshold_factor = 0.4f;
    }

  if (state->config.outlier_low <= 0.0f)
    {
      state->config.outlier_low = 0.5f;
    }

  if (state->config.outlier_high <= 0.0f)
    {
      state->config.outlier_high = 1.5f;
    }

  if (state->config.motion_threshold <= 0.0f)
    {
      state->config.motion_threshold = 0.3f;
    }

  if (state->config.snr_threshold <= 0.0f)
    {
      state->config.snr_threshold = 3.0f;
    }

  /* Initialize running means to conservative values.
   * These will adapt quickly once real data arrives.
   */

  state->running_peak_mean = 0.5f;
  state->running_trough_mean = 0.0f;
  state->adaptive_threshold = state->config.threshold_factor * 0.5f;
  state->last_peak_index = -1;
  state->initialized = 1;

  return 0;
}

int peak_detect_process(const float *ppg_data, int count,
                        const float *motion_mag,
                        struct peak_detect_state *state,
                        struct peak_detect_result *result)
{
  if (!state || !state->initialized || !ppg_data || !result || count < 1)
    {
      return -1;
    }

  memset(result, 0, sizeof(*result));

  /* Track buffer boundaries for missing-beat interpolation */

  int32_t buffer_start = (int32_t)state->sample_count;

  for (int i = 0; i < count; i++)
    {
      float x = ppg_data[i];
      int32_t abs_idx = (int32_t)(state->sample_count + i);

      /* ----------------------------------------------------------------
       * Step 1: Compute first derivative (finite difference)
       * ---------------------------------------------------------------- */

      float derivative = 0.0f;

      if (state->have_prev_sample)
        {
          derivative = x - state->prev_sample;
        }

      /* ----------------------------------------------------------------
       * Step 2: Detect zero-crossing of derivative (descending)
       *   A local maximum occurs when derivative goes from positive to
       *   negative (or zero). This is the classic systolic peak shape.
       * ---------------------------------------------------------------- */

      int is_zero_crossing = 0;

      if (state->have_prev_sample)
        {
          if (state->prev_derivative > 0.0f && derivative <= 0.0f)
            {
              is_zero_crossing = 1;
            }
        }

      if (is_zero_crossing && state->have_prev_sample)
        {
          /* This sample is a local maximum candidate.
           * Use the previous sample (the actual peak) as the candidate
           * since the zero crossing happens at the transition.
           */

          float candidate_amp = state->prev_sample;

          /* ----------------------------------------------------------------
           * Step 3: Apply adaptive threshold
           *   threshold = factor * (running_peak_mean - running_trough_mean)
           * ---------------------------------------------------------------- */

          if (candidate_amp > state->adaptive_threshold)
            {
              /* ----------------------------------------------------------------
               * Step 4: Enforce minimum distance between peaks
               * ---------------------------------------------------------------- */

              int too_close = 0;

              if (state->have_last_peak)
                {
                  int32_t dist = abs_idx - 1 - state->last_peak_index;
                  float dist_ms = (float)dist * 1000.0f /
                                  state->config.sample_rate;

                  if (dist_ms < state->config.min_ibi_ms)
                    {
                      too_close = 1;
                    }
                }

              if (!too_close)
                {
                  /* Accept or replace current candidate */

                  if (!state->candidate_valid ||
                      candidate_amp > state->candidate_amp)
                    {
                      state->candidate_amp = candidate_amp;
                      state->candidate_index = abs_idx - 1;
                      state->candidate_valid = 1;
                    }
                }
            }
          else
            {
              /* Below threshold — update trough mean */

              state->running_trough_mean =
                (1.0f - EMA_ALPHA) * state->running_trough_mean +
                EMA_ALPHA * candidate_amp;
            }
        }

      /* ----------------------------------------------------------------
       * Step 5: When we see the next zero-crossing or a significant
       *   amplitude drop, confirm the current candidate as a peak.
       *   We confirm a peak when:
       *   a) A new higher candidate replaces the old one (old is rejected),
       *   b) The signal drops below the trough mean (peak is past), or
       *   c) A new zero-crossing occurs and the old candidate was valid.
       *
       *   We use condition (b) here for reliable confirmation: once the
       *   signal falls back to the trough level, the peak is over.
       * ---------------------------------------------------------------- */

      if (state->candidate_valid &&
          x < state->running_trough_mean &&
          derivative < 0.0f)
        {
          /* Confirm the peak */

          accept_peak(state, result, ppg_data, count, motion_mag);
        }

      /* Update adaptive threshold */

      if (state->candidate_valid)
        {
          /* Update peak mean when we have a candidate */

          state->running_peak_mean =
            (1.0f - EMA_ALPHA) * state->running_peak_mean +
            EMA_ALPHA * state->candidate_amp;
        }

      state->adaptive_threshold = state->config.threshold_factor *
        (state->running_peak_mean - state->running_trough_mean);

      /* Clamp threshold to avoid going negative or too small */

      if (state->adaptive_threshold < 0.01f)
        {
          state->adaptive_threshold = 0.01f;
        }

      /* Update per-sample motion magnitude if array provided */

      if (motion_mag)
        {
          state->motion_magnitude = motion_mag[i];
        }

      /* Store for next iteration */

      state->prev_sample = x;
      state->prev_derivative = derivative;
      state->have_prev_sample = 1;
    }

  /* ----------------------------------------------------------------
   * Check for missing beats at end of buffer
   * ---------------------------------------------------------------- */

  check_missing_beat(state, result,
                     (int32_t)(state->sample_count + count));

  /* Update total sample counter */

  state->sample_count += count;

  /* ----------------------------------------------------------------
   * Compute heart rate and median IBI for the result
   * ---------------------------------------------------------------- */

  result->median_ibi_ms = compute_median_ibi(state);

  float mean_ibi = compute_mean_ibi(state);

  if (mean_ibi > 0.0f)
    {
      result->heart_rate_bpm = 60000.0f / mean_ibi;
    }
  else
    {
      result->heart_rate_bpm = 0.0f;
    }

  return 0;
}

void peak_detect_reset(struct peak_detect_state *state)
{
  if (!state)
    {
      return;
    }

  /* Preserve configuration */

  struct peak_detect_config saved_config = state->config;
  int was_init = state->initialized;

  memset(state, 0, sizeof(*state));

  state->config = saved_config;
  state->initialized = was_init;

  /* Re-initialize running means */

  state->running_peak_mean = 0.5f;
  state->running_trough_mean = 0.0f;
  state->adaptive_threshold = state->config.threshold_factor * 0.5f;
  state->last_peak_index = -1;
}

float peak_detect_get_hr(const struct peak_detect_state *state)
{
  if (!state || state->ibi_count < 1)
    {
      return 0.0f;
    }

  float mean_ibi = compute_mean_ibi(state);

  if (mean_ibi <= 0.0f)
    {
      return 0.0f;
    }

  return 60000.0f / mean_ibi;
}

float peak_detect_get_median_ibi(const struct peak_detect_state *state)
{
  if (!state)
    {
      return 0.0f;
    }

  return compute_median_ibi(state);
}
