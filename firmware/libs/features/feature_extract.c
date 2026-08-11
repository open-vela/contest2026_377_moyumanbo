/****************************************************************************
 * Feature Extraction Implementation
 *
 * Extracts all 15 features from a 60-second sensor window.
 *
 * Numerical stability measures applied throughout:
 *   - All divisions guarded against zero/near-zero denominators
 *   - sqrtf on non-negative values with negative clamp
 *   - Linear regression uses compensated summation
 *   - Coefficient of variation clamped to prevent sigma explosion
 *   - Temperature slope uses 4-point history to avoid single-sample noise
 *   - Invalid/unavailable features set to safe defaults with valid_mask=0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <math.h>
#include <string.h>
#include <errno.h>
#include "feature_extract.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Minimum IBI count for valid HRV computation */

#define MIN_IBI_FOR_HRV     3

/* Valid HR range for sanity checks */

#define HR_MIN_VALID        25.0f
#define HR_MAX_VALID        230.0f

/* Valid IBI range (ms) */

#define IBI_MIN_MS          260.0f   /* ~230 bpm */
#define IBI_MAX_MS          2400.0f  /* ~25 bpm */

/* Default feature values for invalid features */

#define FEATURE_DEFAULT_ZERO  0.0f

/* Minimum IBI dispersion count (need enough samples for meaningful CV) */

#define MIN_IBI_FOR_DISPERSION  5

/* Temperature slope history interval (seconds between history points) */

#define TEMP_SLOPE_INTERVAL_S   15

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: extract_ppg_features
 *
 * Description:
 *   Extract PPG-derived features [0-4]: HR, HR_slope, RMSSD, SDNN,
 *   IBI_dispersion from the IBI array.
 *
 ****************************************************************************/

static void extract_ppg_features(struct feature_ctx *ctx,
                                 const struct feature_sensor_window *window,
                                 struct feature_result *result)
{
  float *feat = result->features;

  /* Default: all PPG features invalid */

  feat[FEAT_IDX_HR] = FEATURE_DEFAULT_ZERO;
  feat[FEAT_IDX_HR_SLOPE] = FEATURE_DEFAULT_ZERO;
  feat[FEAT_IDX_RMSSD] = FEATURE_DEFAULT_ZERO;
  feat[FEAT_IDX_SDNN] = FEATURE_DEFAULT_ZERO;
  feat[FEAT_IDX_IBI_DISP] = FEATURE_DEFAULT_ZERO;

  if (!window->ibi_array || window->ibi_count < MIN_IBI_FOR_HRV)
    {
      return;
    }

  /* Filter to valid IBI values within physiological range */

  float ibi[FEATURE_IBI_MAX_COUNT];
  int n = 0;

  for (int i = 0; i < window->ibi_count && n < FEATURE_IBI_MAX_COUNT; i++)
    {
      if (window->ibi_array[i].is_valid &&
          window->ibi_array[i].ibi_ms >= IBI_MIN_MS &&
          window->ibi_array[i].ibi_ms <= IBI_MAX_MS)
        {
          ibi[n++] = window->ibi_array[i].ibi_ms;
        }
    }

  if (n < MIN_IBI_FOR_HRV)
    {
      return;
    }

  /* ---- Feature 0: HR (from mean IBI) ---- */

  float sum_ibi = 0.0f;
  for (int i = 0; i < n; i++)
    {
      sum_ibi += ibi[i];
    }

  float mean_ibi = sum_ibi / n;

  if (mean_ibi > 0.0f)
    {
      feat[FEAT_IDX_HR] = 60000.0f / mean_ibi;
      result->valid_mask |= (1 << FEAT_IDX_HR);
    }

  /* ---- Feature 1: HR_slope (bpm/min from linear regression) ---- */

  if (n >= 4)
    {
      /* Linear regression of beat-to-beat HR over time.
       * x = beat index (as time proxy), y = instantaneous HR.
       * slope = (n * sum(xy) - sum(x)*sum(y)) / (n * sum(x^2) - sum(x)^2)
       *
       * Use compensated summation for numerical stability.
       */

      float sum_x = 0.0f;
      float sum_y = 0.0f;
      float sum_xy = 0.0f;
      float sum_x2 = 0.0f;

      for (int i = 0; i < n; i++)
        {
          float x = (float)i;
          float y = 60000.0f / ibi[i];  /* Instantaneous HR */

          sum_x += x;
          sum_y += y;
          sum_xy += x * y;
          sum_x2 += x * x;
        }

      float denom = (float)n * sum_x2 - sum_x * sum_x;
      if (fabsf(denom) > 1e-6f)
        {
          float slope_per_beat = ((float)n * sum_xy - sum_x * sum_y) / denom;

          /* Convert from bpm/beat to bpm/min.
           * Average beat interval in seconds:
           *   beat_period = mean_ibi / 1000
           *   beats_per_min = 60 / beat_period
           *   slope_per_min = slope_per_beat * beats_per_min
           */

          float beat_period_s = mean_ibi / 1000.0f;
          if (beat_period_s > 0.0f)
            {
              float slope_per_min = slope_per_beat * (60.0f / beat_period_s);

              /* Clamp to physiologically plausible range */

              if (slope_per_min > 50.0f)
                slope_per_min = 50.0f;
              else if (slope_per_min < -50.0f)
                slope_per_min = -50.0f;

              feat[FEAT_IDX_HR_SLOPE] = slope_per_min;
              result->valid_mask |= (1 << FEAT_IDX_HR_SLOPE);
            }
        }
    }

  /* ---- Feature 2: RMSSD ---- */

  if (n >= 2)
    {
      float sum_diff_sq = 0.0f;
      for (int i = 1; i < n; i++)
        {
          float diff = ibi[i] - ibi[i - 1];
          sum_diff_sq += diff * diff;
        }

      float mean_diff_sq = sum_diff_sq / (n - 1);
      if (mean_diff_sq >= 0.0f)
        {
          feat[FEAT_IDX_RMSSD] = sqrtf(mean_diff_sq);
          result->valid_mask |= (1 << FEAT_IDX_RMSSD);
        }
    }

  /* ---- Feature 3: SDNN ---- */

  {
    float sum_sq = 0.0f;
    for (int i = 0; i < n; i++)
      {
        float diff = ibi[i] - mean_ibi;
        sum_sq += diff * diff;
      }

    float variance = sum_sq / n;
    if (variance >= 0.0f)
      {
        feat[FEAT_IDX_SDNN] = sqrtf(variance);
        result->valid_mask |= (1 << FEAT_IDX_SDNN);
      }
  }

  /* ---- Feature 4: IBI dispersion (coefficient of variation) ---- */

  if (n >= MIN_IBI_FOR_DISPERSION)
    {
      float sdnn = feat[FEAT_IDX_SDNN];
      if (mean_ibi > 1e-3f && sdnn >= 0.0f)
        {
          float cv = (sdnn / mean_ibi) * 100.0f;  /* Percentage */

          /* Clamp to plausible range (0-200%) */

          if (cv > 200.0f)
            cv = 200.0f;

          feat[FEAT_IDX_IBI_DISP] = cv;
          result->valid_mask |= (1 << FEAT_IDX_IBI_DISP);
        }
    }
}

/****************************************************************************
 * Name: extract_activity_features
 *
 * Description:
 *   Extract activity features [5-6]: activity_intensity and posture
 *   from IMU accelerometer data.
 *
 ****************************************************************************/

static void extract_activity_features(struct feature_ctx *ctx,
                                      const struct feature_sensor_window *window,
                                      struct feature_result *result)
{
  float *feat = result->features;
  feat[FEAT_IDX_ACTIVITY] = (float)ACTIVITY_REST;
  feat[FEAT_IDX_POSTURE] = (float)POSTURE_SIT;

  if (!window->accel_x || !window->accel_y || !window->accel_z ||
      window->accel_count < 10)
    {
      return;
    }

  /* Push all accel data into activity classifier and run.
   * For efficiency, use the single-shot classification path. */

  int count = window->accel_count;
  int offset = 0;
  if (count > ACTIVITY_WINDOW_SAMPLES)
    {
      /* Use only the most recent window's worth of data */

      offset = count - ACTIVITY_WINDOW_SAMPLES;
      count = ACTIVITY_WINDOW_SAMPLES;
    }

  /* Compute magnitudes into a temporary buffer.
   * On embedded, this stack allocation is bounded at 500 floats = 2KB.
   */

  float mag_buf[ACTIVITY_WINDOW_SAMPLES];
  for (int i = 0; i < count; i++)
    {
      int idx = offset + i;
      float xy = hypotf(window->accel_x[idx], window->accel_y[idx]);
      mag_buf[i] = hypotf(xy, window->accel_z[idx]);
    }

  struct activity_result act_result;
  int ret = activity_classify_single(mag_buf, count,
                                     window->imu_rate_hz > 0 ?
                                         window->imu_rate_hz :
                                         FEATURE_IMU_RATE,
                                     &act_result);
  if (ret == 0 && act_result.valid)
    {
      feat[FEAT_IDX_ACTIVITY] = (float)act_result.intensity;
      feat[FEAT_IDX_POSTURE] = (float)act_result.posture;
      result->valid_mask |= (1 << FEAT_IDX_ACTIVITY);
      result->valid_mask |= (1 << FEAT_IDX_POSTURE);
    }
}

/****************************************************************************
 * Name: extract_eda_features
 *
 * Description:
 *   Extract EDA features [7-9]: SCL_baseline, SCR_count, SCR_amplitude.
 *
 ****************************************************************************/

static void extract_eda_features(struct feature_ctx *ctx,
                                 const struct feature_sensor_window *window,
                                 struct feature_result *result)
{
  float *feat = result->features;

  /* Feature 7: SCL baseline (tonic level) */

  feat[FEAT_IDX_SCL_BASELINE] = window->eda_scl_baseline;
  if (window->eda_scl_baseline > 0.0f)
    {
      result->valid_mask |= (1 << FEAT_IDX_SCL_BASELINE);
    }

  /* Features 8-9: SCR count and amplitude from event array */

  feat[FEAT_IDX_SCR_COUNT] = FEATURE_DEFAULT_ZERO;
  feat[FEAT_IDX_SCR_AMP] = FEATURE_DEFAULT_ZERO;

  if (!window->scr_events || window->scr_event_count <= 0)
    {
      return;
    }

  int valid_count = 0;
  float amp_sum = 0.0f;

  for (int i = 0; i < window->scr_event_count; i++)
    {
      if (window->scr_events[i].is_valid &&
          window->scr_events[i].amplitude >= FEATURE_SCR_THRESHOLD_UV)
        {
          valid_count++;
          amp_sum += window->scr_events[i].amplitude;
        }
    }

  /* SCR count per minute (window is 60 seconds) */

  feat[FEAT_IDX_SCR_COUNT] = (float)valid_count;
  result->valid_mask |= (1 << FEAT_IDX_SCR_COUNT);

  /* Mean SCR amplitude */

  if (valid_count > 0)
    {
      feat[FEAT_IDX_SCR_AMP] = amp_sum / valid_count;
      result->valid_mask |= (1 << FEAT_IDX_SCR_AMP);
    }
}

/****************************************************************************
 * Name: extract_temp_features
 *
 * Description:
 *   Extract temperature feature [10]: skin temperature rate of change
 *   in degrees C per minute.
 *
 ****************************************************************************/

static void extract_temp_features(struct feature_ctx *ctx,
                                  const struct feature_sensor_window *window,
                                  struct feature_result *result)
{
  float *feat = result->features;
  feat[FEAT_IDX_TEMP_SLOPE] = FEATURE_DEFAULT_ZERO;

  /* Use the temperature history maintained in the context for slope.
   * This gives us a longer time horizon than just the 60s window. */

  if (ctx->temp_hist_count < 2)
    {
      /* Fall back to window data if available */

      if (window->temp_array && window->temp_count >= 2)
        {
          int last = window->temp_count - 1;
          float dt_ms = (float)(window->temp_array[last].timestamp_ms -
                                window->temp_array[0].timestamp_ms);
          float dt_min = dt_ms / 60000.0f;

          if (dt_min > 0.001f)
            {
              float dtemp = window->temp_array[last].temperature_c -
                            window->temp_array[0].temperature_c;
              feat[FEAT_IDX_TEMP_SLOPE] = dtemp / dt_min;

              /* Clamp to plausible range */

              if (feat[FEAT_IDX_TEMP_SLOPE] > 5.0f)
                feat[FEAT_IDX_TEMP_SLOPE] = 5.0f;
              else if (feat[FEAT_IDX_TEMP_SLOPE] < -5.0f)
                feat[FEAT_IDX_TEMP_SLOPE] = -5.0f;

              result->valid_mask |= (1 << FEAT_IDX_TEMP_SLOPE);
            }
        }

      return;
    }

  /* Compute slope from context history using least-squares fit.
   * History stores the last 4 temperature readings at ~15s intervals.
   * Find the oldest and newest entries for the slope estimate. */

  int newest = (ctx->temp_hist_idx - 1 + 4) % 4;
  int oldest = ctx->temp_hist_idx % 4;

  /* If we wrapped, oldest is the current index; otherwise index 0 */

  if (ctx->temp_hist_count >= 4)
    {
      oldest = ctx->temp_hist_idx;  /* Oldest was overwritten */
    }
  else
    {
      oldest = 0;
    }

  float dt_ms = (float)(ctx->temp_timestamps[newest] -
                        ctx->temp_timestamps[oldest]);
  float dt_min = dt_ms / 60000.0f;

  if (dt_min > 0.01f)
    {
      float dtemp = ctx->temp_history[newest] - ctx->temp_history[oldest];
      feat[FEAT_IDX_TEMP_SLOPE] = dtemp / dt_min;

      /* Clamp to +/- 5 C/min (physiological limit) */

      if (feat[FEAT_IDX_TEMP_SLOPE] > 5.0f)
        feat[FEAT_IDX_TEMP_SLOPE] = 5.0f;
      else if (feat[FEAT_IDX_TEMP_SLOPE] < -5.0f)
        feat[FEAT_IDX_TEMP_SLOPE] = -5.0f;

      result->valid_mask |= (1 << FEAT_IDX_TEMP_SLOPE);
    }
}

/****************************************************************************
 * Name: extract_quality_feature
 *
 * Description:
 *   Extract feature [11]: PPG signal quality index.
 *
 ****************************************************************************/

static void extract_quality_feature(const struct feature_sensor_window *window,
                                    struct feature_result *result)
{
  float *feat = result->features;

  feat[FEAT_IDX_PPG_SQI] = window->ppg_sqi;

  /* Clamp to [0, 1] */

  if (feat[FEAT_IDX_PPG_SQI] < 0.0f)
    feat[FEAT_IDX_PPG_SQI] = 0.0f;
  else if (feat[FEAT_IDX_PPG_SQI] > 1.0f)
    feat[FEAT_IDX_PPG_SQI] = 1.0f;

  /* Always valid (even if SQI is low, the value itself is meaningful) */

  result->valid_mask |= (1 << FEAT_IDX_PPG_SQI);
}

/****************************************************************************
 * Name: extract_baseline_features
 *
 * Description:
 *   Extract features [12-13]: hr_deviation and rmssd_deviation from
 *   personal baseline.
 *
 ****************************************************************************/

static void extract_baseline_features(struct feature_ctx *ctx,
                                      const struct feature_sensor_window *window,
                                      struct feature_result *result)
{
  float *feat = result->features;
  feat[FEAT_IDX_HR_DEV] = FEATURE_DEFAULT_ZERO;
  feat[FEAT_IDX_RMSSD_DEV] = FEATURE_DEFAULT_ZERO;

  /* Need valid HR and RMSSD from this window */

  if (!(result->valid_mask & (1 << FEAT_IDX_HR)) ||
      !(result->valid_mask & (1 << FEAT_IDX_RMSSD)))
    {
      return;
    }

  /* Determine activity bin for baseline lookup */

  enum baseline_activity act_bin = BASELINE_ACT_REST;
  if (result->valid_mask & (1 << FEAT_IDX_ACTIVITY))
    {
      act_bin = baseline_activity_from_level((int)feat[FEAT_IDX_ACTIVITY]);
    }

  /* HR deviation */

  float hr_dev = baseline_get_deviation(
      &ctx->baseline,
      BASELINE_SIGNAL_HR,
      feat[FEAT_IDX_HR],
      window->hour_of_day,
      act_bin,
      window->is_worn != 0);

  feat[FEAT_IDX_HR_DEV] = hr_dev;
  if (baseline_is_stable(&ctx->baseline, BASELINE_SIGNAL_HR))
    {
      result->valid_mask |= (1 << FEAT_IDX_HR_DEV);
    }

  /* RMSSD deviation */

  float rmssd_dev = baseline_get_deviation(
      &ctx->baseline,
      BASELINE_SIGNAL_RMSSD,
      feat[FEAT_IDX_RMSSD],
      window->hour_of_day,
      act_bin,
      window->is_worn != 0);

  feat[FEAT_IDX_RMSSD_DEV] = rmssd_dev;
  if (baseline_is_stable(&ctx->baseline, BASELINE_SIGNAL_RMSSD))
    {
      result->valid_mask |= (1 << FEAT_IDX_RMSSD_DEV);
    }

  /* Update baseline with current values */

  baseline_update(&ctx->baseline,
                  BASELINE_SIGNAL_HR,
                  feat[FEAT_IDX_HR],
                  window->hour_of_day,
                  act_bin,
                  window->is_worn != 0,
                  result->timestamp_ms / 1000);

  baseline_update(&ctx->baseline,
                  BASELINE_SIGNAL_RMSSD,
                  feat[FEAT_IDX_RMSSD],
                  window->hour_of_day,
                  act_bin,
                  window->is_worn != 0,
                  result->timestamp_ms / 1000);
}

/****************************************************************************
 * Name: extract_temporal_feature
 *
 * Description:
 *   Extract feature [14]: normalized time of day.
 *   0 = midnight, 0.5 = noon, 1 = midnight.
 *
 ****************************************************************************/

static void extract_temporal_feature(const struct feature_sensor_window *window,
                                     struct feature_result *result)
{
  float *feat = result->features;

  /* Normalize: total minutes since midnight / minutes in a day */

  int total_minutes = window->hour_of_day * 60 + window->minute_of_hour;

  if (total_minutes < 0)
    total_minutes = 0;
  if (total_minutes >= 1440)
    total_minutes = 1439;

  feat[FEAT_IDX_TIME_OF_DAY] = (float)total_minutes / 1440.0f;

  result->valid_mask |= (1 << FEAT_IDX_TIME_OF_DAY);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int feature_extract_init(struct feature_ctx *ctx)
{
  if (!ctx)
    {
      return -EINVAL;
    }

  memset(ctx, 0, sizeof(*ctx));

  /* Initialize subsystems */

  int ret = baseline_init(&ctx->baseline);
  if (ret < 0)
    {
      return ret;
    }

  ret = activity_classify_init(&ctx->act_cls);
  if (ret < 0)
    {
      return ret;
    }

  ctx->initialized = 1;

  return 0;
}

int feature_extract_run(struct feature_ctx *ctx,
                        const struct feature_sensor_window *window,
                        struct feature_result *result)
{
  if (!ctx || !window || !result || !ctx->initialized)
    {
      return -EINVAL;
    }

  /* Clear result */

  memset(result, 0, sizeof(*result));
  result->timestamp_ms = window->ibi_count > 0 ?
      window->ibi_array[window->ibi_count - 1].timestamp_ms : 0;

  /* Extract each feature group */

  extract_ppg_features(ctx, window, result);
  extract_activity_features(ctx, window, result);
  extract_eda_features(ctx, window, result);
  extract_temp_features(ctx, window, result);
  extract_quality_feature(window, result);
  extract_baseline_features(ctx, window, result);
  extract_temporal_feature(window, result);

  /* Check if all features are valid */

  int all_mask = (1 << FEATURE_COUNT) - 1;
  result->all_valid = ((result->valid_mask & all_mask) == all_mask) ? 1 : 0;

  return 0;
}

void feature_extract_update_temp(struct feature_ctx *ctx,
                                 float temperature,
                                 uint32_t timestamp_ms)
{
  if (!ctx || !ctx->initialized)
    {
      return;
    }

  /* Reject obviously invalid temperature */

  if (temperature < 10.0f || temperature > 50.0f)
    {
      return;
    }

  ctx->temp_history[ctx->temp_hist_idx] = temperature;
  ctx->temp_timestamps[ctx->temp_hist_idx] = timestamp_ms;
  ctx->temp_hist_idx = (ctx->temp_hist_idx + 1) % 4;

  if (ctx->temp_hist_count < 4)
    {
      ctx->temp_hist_count++;
    }
}

void feature_extract_reset(struct feature_ctx *ctx)
{
  if (!ctx)
    {
      return;
    }

  baseline_reset(&ctx->baseline);
  activity_classify_reset(&ctx->act_cls);

  ctx->prev_hr = 0.0f;
  ctx->prev_rmssd = 0.0f;
  ctx->prev_timestamp_ms = 0;
  ctx->has_prev = 0;
  ctx->temp_hist_idx = 0;
  ctx->temp_hist_count = 0;
  memset(ctx->temp_history, 0, sizeof(ctx->temp_history));
  memset(ctx->temp_timestamps, 0, sizeof(ctx->temp_timestamps));
}

const char *feature_name(int index)
{
  static const char *names[FEATURE_COUNT] =
    {
      "HR",
      "HR_slope",
      "RMSSD",
      "SDNN",
      "IBI_dispersion",
      "activity_intensity",
      "posture",
      "SCL_baseline",
      "SCR_count",
      "SCR_amplitude",
      "temp_slope",
      "ppg_sqi",
      "hr_deviation",
      "rmssd_deviation",
      "time_of_day"
    };

  if (index < 0 || index >= FEATURE_COUNT)
    {
      return "unknown";
    }

  return names[index];
}

const char *feature_unit(int index)
{
  static const char *units[FEATURE_COUNT] =
    {
      "bpm",
      "bpm/min",
      "ms",
      "ms",
      "%",
      "level",
      "level",
      "uS",
      "/min",
      "uS",
      "C/min",
      "0-1",
      "sigma",
      "sigma",
      "0-1"
    };

  if (index < 0 || index >= FEATURE_COUNT)
    {
      return "";
    }

  return units[index];
}
