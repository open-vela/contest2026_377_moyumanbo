/****************************************************************************
 * Activity Classifier Implementation
 *
 * Rule-based activity classification from accelerometer data.
 * Uses magnitude thresholds and spectral energy in the 0.5-3 Hz
 * walking frequency band, with 5-second window and majority voting.
 *
 * Numerical stability considerations:
 *   - All divisions guarded against zero denominators
 *   - Spectral energy computed via DFT to avoid FFT buffer requirements
 *   - Magnitude uses hypotf for overflow protection
 *   - Clamped outputs prevent downstream NaN propagation
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <math.h>
#include <string.h>
#include <errno.h>
#include "activity_classify.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Sub-window size: 1 second of data at 100 Hz */

#define SUBWINDOW_SAMPLES   (ACTIVITY_IMU_RATE_HZ)
#define NUM_SUBWINDOWS      (ACTIVITY_WINDOW_SEC)

/* DFT bin parameters for 0.5-3 Hz detection at 100 Hz sample rate.
 * Frequency resolution = fs / N = 100 / 100 = 1 Hz per bin.
 * We need finer resolution, so use zero-padded 256-point DFT.
 * Bin k corresponds to frequency = k * fs / N = k * 100 / 256.
 * Walk band: 0.5-3 Hz -> bins 2..8 (approximately).
 */

#define DFT_N               256
#define DFT_FREQ_RES        ((float)ACTIVITY_IMU_RATE_HZ / DFT_N)
#define WALK_BIN_LOW         1   /* ~0.39 Hz */
#define WALK_BIN_HIGH        8   /* ~3.12 Hz */
#define RUN_BIN_LOW          8   /* ~3.12 Hz */
#define RUN_BIN_HIGH         16  /* ~6.25 Hz */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: compute_magnitude
 *
 * Description:
 *   Compute acceleration magnitude using hypotf for numerical stability.
 *   hypotf avoids overflow/underflow that raw sqrt(x*x + y*y + z*z)
 *   could produce with large values.
 *
 ****************************************************************************/

static float compute_magnitude(float ax, float ay, float az)
{
  /* Use two-step hypot to avoid overflow: hypot(hypot(ax, ay), az) */

  float xy = hypotf(ax, ay);
  return hypotf(xy, az);
}

/****************************************************************************
 * Name: compute_dft_energy_in_band
 *
 * Description:
 *   Compute normalized spectral energy in a frequency band using
 *   a direct DFT. This avoids allocating a full FFT buffer and is
 *   feasible for small window sizes (100 samples).
 *
 *   Only computes the bins of interest (walk band and run band)
 *   to minimize computation.
 *
 * Input Parameters:
 *   data    - Input time-domain signal
 *   n       - Number of samples
 *   bin_lo  - Lowest DFT bin index (inclusive)
 *   bin_hi  - Highest DFT bin index (inclusive)
 *
 * Returned Value:
 *   Normalized energy in the specified band [0, 1]
 *
 ****************************************************************************/

static float compute_dft_energy_in_band(const float *data, int n,
                                        int bin_lo, int bin_hi)
{
  if (n < 2 || bin_lo < 0 || bin_hi < bin_lo)
    {
      return 0.0f;
    }

  /* Compute total signal energy first (for normalization) */

  float total_energy = 0.0f;
  for (int i = 0; i < n; i++)
    {
      total_energy += data[i] * data[i];
    }

  if (total_energy < 1e-12f)
    {
      return 0.0f;
    }

  /* Compute band energy via direct DFT for bins of interest.
   * DFT X[k] = sum_{n=0}^{N-1} x[n] * exp(-j*2*pi*k*n/N)
   * Energy at bin k = |X[k]|^2 = Re^2 + Im^2
   *
   * We use the actual sample count n (not DFT_N) for the time-domain
   * data, but compute at bin indices that correspond to DFT_N.
   * This is equivalent to zero-padding to DFT_N.
   */

  float band_energy = 0.0f;

  for (int k = bin_lo; k <= bin_hi; k++)
    {
      float re = 0.0f;
      float im = 0.0f;
      float angle_step = 2.0f * (float)M_PI * k / DFT_N;

      for (int m = 0; m < n; m++)
        {
          float angle = angle_step * m;
          re += data[m] * cosf(angle);
          im -= data[m] * sinf(angle);
        }

      band_energy += re * re + im * im;
    }

  /* Normalize: divide by (N^2 * total_energy) to get fraction of
   * total power in the band. Factor of 2 for one-sided spectrum.
   */

  float norm = 2.0f / ((float)DFT_N * (float)DFT_N * total_energy);
  return band_energy * norm;
}

/****************************************************************************
 * Name: estimate_dominant_frequency
 *
 * Description:
 *   Estimate the dominant frequency in the signal by scanning DFT bins.
 *
 ****************************************************************************/

static float estimate_dominant_frequency(const float *data, int n,
                                         int sample_rate)
{
  if (n < 2 || sample_rate <= 0)
    {
      return 0.0f;
    }

  float max_energy = 0.0f;
  int best_bin = 0;

  /* Scan bins 1..N/2-1 (skip DC at bin 0) */

  int max_bin = DFT_N / 2;
  if (max_bin > 30)
    {
      max_bin = 30;  /* Only scan up to ~12 Hz */
    }

  for (int k = 1; k < max_bin; k++)
    {
      float re = 0.0f;
      float im = 0.0f;
      float angle_step = 2.0f * (float)M_PI * k / DFT_N;

      for (int m = 0; m < n; m++)
        {
          float angle = angle_step * m;
          re += data[m] * cosf(angle);
          im -= data[m] * sinf(angle);
        }

      float energy = re * re + im * im;
      if (energy > max_energy)
        {
          max_energy = energy;
          best_bin = k;
        }
    }

  return (float)best_bin * (float)sample_rate / DFT_N;
}

/****************************************************************************
 * Name: classify_subwindow
 *
 * Description:
 *   Classify a single 1-second sub-window of acceleration magnitude.
 *
 ****************************************************************************/

static enum activity_level classify_subwindow(float mean_mag,
                                              float peak_mag,
                                              float walk_energy,
                                              float run_energy)
{
  /* Decision tree:
   *
   * 1. Rest: low magnitude AND low spectral energy
   * 2. Run:  high magnitude OR high spectral energy in run band
   * 3. Walk: moderate magnitude AND spectral energy in walk band
   * 4. Rest: default (insufficient evidence for walk/run)
   */

  /* Rest check */

  if (mean_mag < ACTIVITY_REST_MAG_MAX && walk_energy < ACTIVITY_WALK_SPEC_MIN)
    {
      return ACTIVITY_REST;
    }

  /* Run check: magnitude exceeds threshold or strong high-freq energy */

  if (mean_mag > ACTIVITY_RUN_MAG_MIN ||
      (run_energy > ACTIVITY_RUN_SPEC_MIN && mean_mag > ACTIVITY_WALK_MAG_MIN))
    {
      /* Distinguish brisk walk from run based on magnitude */

      if (mean_mag > 1.6f || run_energy > 0.20f)
        {
          return ACTIVITY_RUN;
        }

      return ACTIVITY_BRISK;
    }

  /* Walk check: moderate magnitude with walk-band spectral content */

  if (mean_mag >= ACTIVITY_WALK_MAG_MIN &&
      mean_mag <= ACTIVITY_WALK_MAG_MAX &&
      walk_energy >= ACTIVITY_WALK_SPEC_MIN)
    {
      return ACTIVITY_WALK;
    }

  /* Borderline: magnitude elevated but no clear spectral signature */

  if (mean_mag > ACTIVITY_REST_MAG_MAX)
    {
      /* Could be slow walk or fidgeting */

      if (walk_energy > ACTIVITY_WALK_SPEC_MIN * 0.5f)
        {
          return ACTIVITY_WALK;
        }
    }

  return ACTIVITY_REST;
}

/****************************************************************************
 * Name: classify_posture
 *
 * Description:
 *   Estimate posture from activity level and vertical acceleration
 *   component. This is a simplified heuristic for wrist-worn devices.
 *
 ****************************************************************************/

static enum posture_type classify_posture(enum activity_level activity,
                                          float vertical_ratio)
{
  switch (activity)
    {
      case ACTIVITY_REST:
        {
          /* Low vertical ratio suggests seated; high suggests standing */

          if (vertical_ratio < 0.4f)
            {
              return POSTURE_SIT;
            }

          return POSTURE_STAND;
        }

      case ACTIVITY_WALK:
      case ACTIVITY_BRISK:
        return POSTURE_WALK;

      case ACTIVITY_RUN:
        return POSTURE_RUN;

      default:
        return POSTURE_SIT;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int activity_classify_init(struct activity_classifier *ctx)
{
  if (!ctx)
    {
      return -1;
    }

  memset(ctx, 0, sizeof(*ctx));
  ctx->initialized = 1;

  return 0;
}

int activity_classify_push(struct activity_classifier *ctx,
                           float ax, float ay, float az)
{
  if (!ctx || !ctx->initialized)
    {
      return -1;
    }

  float mag = compute_magnitude(ax, ay, az);
  ctx->accel_buf[ctx->buf_idx] = mag;
  ctx->buf_idx = (ctx->buf_idx + 1) % ACTIVITY_WINDOW_SAMPLES;

  if (ctx->buf_count < ACTIVITY_WINDOW_SAMPLES)
    {
      ctx->buf_count++;
    }

  return 0;
}

int activity_classify_run(struct activity_classifier *ctx,
                          struct activity_result *result)
{
  if (!ctx || !result || !ctx->initialized)
    {
      return -1;
    }

  if (ctx->buf_count < ACTIVITY_WINDOW_SAMPLES)
    {
      return -EAGAIN;
    }

  return activity_classify_single(ctx->accel_buf,
                                  ACTIVITY_WINDOW_SAMPLES,
                                  ACTIVITY_IMU_RATE_HZ,
                                  result);
}

int activity_classify_single(const float *magnitudes, int count,
                             int imu_rate,
                             struct activity_result *result)
{
  if (!magnitudes || !result || count < 10 || imu_rate <= 0)
    {
      return -1;
    }

  memset(result, 0, sizeof(*result));

  int subwin_size = imu_rate;  /* 1-second sub-windows */
  if (subwin_size > count)
    {
      subwin_size = count;
    }

  int num_subwindows = count / subwin_size;
  if (num_subwindows < 1)
    {
      num_subwindows = 1;
      subwin_size = count;
    }

  /* Tally votes from each sub-window */

  int votes[5] = {0, 0, 0, 0, 0};  /* One per activity_level */
  float total_magnitude = 0.0f;
  float total_walk_energy = 0.0f;
  int total_samples = 0;

  for (int sw = 0; sw < num_subwindows; sw++)
    {
      const float *seg = magnitudes + sw * subwin_size;
      int seg_len = subwin_size;
      if (sw * subwin_size + seg_len > count)
        {
          seg_len = count - sw * subwin_size;
        }

      /* Compute mean and peak magnitude */

      float sum = 0.0f;
      float peak = 0.0f;
      for (int i = 0; i < seg_len; i++)
        {
          sum += seg[i];
          if (seg[i] > peak)
            {
              peak = seg[i];
            }
        }

      float mean_mag = sum / seg_len;

      /* Compute spectral energy in walk band */

      float walk_e = compute_dft_energy_in_band(seg, seg_len,
                                                 WALK_BIN_LOW, WALK_BIN_HIGH);
      float run_e = compute_dft_energy_in_band(seg, seg_len,
                                                RUN_BIN_LOW, RUN_BIN_HIGH);

      /* Classify this sub-window */

      enum activity_level level = classify_subwindow(mean_mag, peak,
                                                      walk_e, run_e);
      votes[level]++;

      total_magnitude += mean_mag * seg_len;
      total_walk_energy += walk_e * seg_len;
      total_samples += seg_len;
    }

  /* Majority voting */

  int best_count = 0;
  int best_level = ACTIVITY_REST;
  for (int i = 0; i < 5; i++)
    {
      if (votes[i] > best_count)
        {
          best_count = votes[i];
          best_level = i;
        }
    }

  float vote_ratio = (float)best_count / num_subwindows;

  /* If no clear majority, fall back to highest magnitude interpretation */

  if (vote_ratio < ACTIVITY_VOTE_THRESHOLD && best_level == ACTIVITY_REST)
    {
      float overall_mean = total_magnitude / total_samples;
      if (overall_mean > ACTIVITY_WALK_MAG_MIN)
        {
          best_level = ACTIVITY_WALK;
          vote_ratio = 0.5f;  /* Lower confidence */
        }
    }

  /* Compute vertical ratio for posture estimation.
   * For wrist-worn device, approximate vertical as the axis with
   * the most variation (gravity component).
   * Since we only have magnitude, use magnitude statistics as proxy.
   */

  float vertical_ratio = 0.5f;  /* Default: unknown orientation */
  float overall_mean = total_magnitude / total_samples;
  if (overall_mean > 1.3f)
    {
      vertical_ratio = 0.7f;  /* High impact suggests upright */
    }
  else if (overall_mean < 0.95f)
    {
      vertical_ratio = 0.3f;  /* Near-zero suggests reclined */
    }

  /* Estimate dominant frequency from full window */

  float dom_freq = estimate_dominant_frequency(magnitudes, count, imu_rate);

  /* Fill result */

  result->intensity = (enum activity_level)best_level;
  result->posture = classify_posture(result->intensity, vertical_ratio);
  result->confidence = vote_ratio;
  result->mean_magnitude = overall_mean;
  result->dominant_freq_hz = dom_freq;
  result->num_subwindows = num_subwindows;
  result->valid = 1;

  return 0;
}

void activity_classify_reset(struct activity_classifier *ctx)
{
  if (ctx)
    {
      ctx->buf_idx = 0;
      ctx->buf_count = 0;
    }
}

const char *activity_level_to_string(enum activity_level level)
{
  switch (level)
    {
      case ACTIVITY_REST:   return "rest";
      case ACTIVITY_WALK:   return "walk";
      case ACTIVITY_BRISK:  return "brisk";
      case ACTIVITY_RUN:    return "run";
      case ACTIVITY_CYCLE:  return "cycle";
      default:              return "unknown";
    }
}

const char *posture_type_to_string(enum posture_type posture)
{
  switch (posture)
    {
      case POSTURE_SIT:    return "sit";
      case POSTURE_STAND:  return "stand";
      case POSTURE_WALK:   return "walk";
      case POSTURE_RUN:    return "run";
      default:             return "unknown";
    }
}
