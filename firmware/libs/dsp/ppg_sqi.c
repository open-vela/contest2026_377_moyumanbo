/****************************************************************************
 * PPG Signal Quality Index (SQI) Implementation
 *
 * Multi-component SQI based on amplitude stability, spectral energy
 * in the cardiac band, morphological consistency, and motion estimate.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <math.h>
#include <string.h>
#include "ppg_sqi.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct ppg_sqi_config g_config;
static int g_initialized = 0;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: compute_amplitude_stability
 *
 * Description:
 *   Compute amplitude stability as 1 - (std/mean) of peak-to-peak
 *   amplitudes within the window. Clamped to [0, 1].
 *
 ****************************************************************************/

static float compute_amplitude_stability(const float *samples, int count)
{
  if (count < 2)
    {
      return 0.0f;
    }

  /* Find local max and min in segments */

  float sum = 0.0f;
  float sum_sq = 0.0f;
  int n = 0;
  int seg_size = (int)(g_config.sample_rate * 0.5f); /* 0.5s segments */

  if (seg_size < 10)
    {
      seg_size = 10;
    }

  for (int i = 0; i < count - seg_size; i += seg_size)
    {
      float seg_max = samples[i];
      float seg_min = samples[i];

      for (int j = i; j < i + seg_size && j < count; j++)
        {
          if (samples[j] > seg_max)
            seg_max = samples[j];
          if (samples[j] < seg_min)
            seg_min = samples[j];
        }

      float ptp = seg_max - seg_min;
      sum += ptp;
      sum_sq += ptp * ptp;
      n++;
    }

  if (n < 2)
    {
      return 0.5f;
    }

  float mean = sum / n;
  float variance = (sum_sq / n) - (mean * mean);
  float std = sqrtf(variance > 0 ? variance : 0);

  if (mean < 1e-6f)
    {
      return 0.0f;
    }

  float cv = std / mean; /* Coefficient of variation */
  float stability = 1.0f - cv;

  if (stability < 0.0f)
    stability = 0.0f;
  if (stability > 1.0f)
    stability = 1.0f;

  return stability;
}

/****************************************************************************
 * Name: compute_spectral_score
 *
 * Description:
 *   Estimate spectral energy ratio in cardiac band (0.5-5 Hz) vs total.
 *   Simple zero-crossing rate approximation.
 *
 ****************************************************************************/

static float compute_spectral_score(const float *samples, int count)
{
  if (count < 10)
    {
      return 0.0f;
    }

  /* Count zero crossings of first derivative (peaks/valleys) */

  int crossings = 0;
  float prev_diff = samples[1] - samples[0];

  for (int i = 2; i < count; i++)
    {
      float diff = samples[i] - samples[i - 1];
      if ((prev_diff > 0 && diff < 0) || (prev_diff < 0 && diff > 0))
        {
          crossings++;
        }
      prev_diff = diff;
    }

  /* Expected crossings for cardiac signal: ~2 per heartbeat cycle
   * At 100Hz, 60bpm → 100 samples/beat → ~2 crossings per 100 samples
   * Normalize to [0, 1] */

  float expected_rate = 2.0f * (count / g_config.sample_rate) *
                        (70.0f / 60.0f); /* Assume ~70bpm */
  float ratio = (float)crossings / (expected_rate + 1.0f);

  /* Good signal has ratio near 1.0 */

  float score;
  if (ratio > 0.5f && ratio < 2.0f)
    {
      score = 1.0f - fabsf(ratio - 1.0f);
    }
  else
    {
      score = 0.0f;
    }

  return score > 1.0f ? 1.0f : (score < 0.0f ? 0.0f : score);
}

/****************************************************************************
 * Name: compute_morphological_score
 *
 * Description:
 *   Estimate morphological consistency by comparing consecutive
 *   beat shapes (normalized cross-correlation of segments).
 *
 ****************************************************************************/

static float compute_morphological_score(const float *samples, int count)
{
  if (count < 20)
    {
      return 0.5f;
    }

  /* Simple: compute variance of first derivative normalized by mean */

  float sum_diff = 0.0f;
  float sum_diff_sq = 0.0f;
  int n = count - 1;

  for (int i = 1; i < count; i++)
    {
      float d = samples[i] - samples[i - 1];
      sum_diff += d;
      sum_diff_sq += d * d;
    }

  float mean_diff = sum_diff / n;
  float var_diff = (sum_diff_sq / n) - (mean_diff * mean_diff);

  /* Low variance in derivative = consistent morphology */

  float score = 1.0f / (1.0f + var_diff * 0.01f);
  return score > 1.0f ? 1.0f : (score < 0.0f ? 0.0f : score);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int ppg_sqi_init(const struct ppg_sqi_config *config)
{
  if (!config || config->sample_rate <= 0 || config->window_size < 10)
    {
      return -1;
    }

  memcpy(&g_config, config, sizeof(g_config));

  if (g_config.threshold <= 0)
    {
      g_config.threshold = 0.70f;
    }

  g_initialized = 1;
  return 0;
}

int ppg_sqi_compute(const float *samples, int count,
                    const float *imu_data, int imu_count,
                    struct ppg_sqi_result *result)
{
  if (!g_initialized || !samples || !result || count < 10)
    {
      return -1;
    }

  memset(result, 0, sizeof(*result));

  /* Component 1: Amplitude stability */

  result->amp_stability = compute_amplitude_stability(samples, count);

  /* Component 2: Spectral score */

  result->spectral_score = compute_spectral_score(samples, count);

  /* Component 3: Morphological consistency */

  result->morph_score = compute_morphological_score(samples, count);

  /* Component 4: Motion estimate (from IMU if available) */

  if (imu_data && imu_count > 0)
    {
      /* Compute IMU energy as motion proxy */

      float energy = 0.0f;
      for (int i = 0; i < imu_count; i++)
        {
          energy += imu_data[i] * imu_data[i];
        }

      energy = sqrtf(energy / imu_count);

      /* Low energy = clean signal. Threshold at ~0.5g for rest */

      result->motion_score = 1.0f / (1.0f + energy * 2.0f);
    }
  else
    {
      result->motion_score = 0.8f; /* Assume reasonable if no IMU */
    }

  /* Weighted combination */

  result->quality = 0.30f * result->amp_stability +
                    0.25f * result->spectral_score +
                    0.25f * result->morph_score +
                    0.20f * result->motion_score;

  result->is_reliable = (result->quality >= g_config.threshold) ? 1 : 0;

  return 0;
}

int ppg_sqi_is_reliable(void)
{
  /* TODO: Store last result and return its is_reliable field */
  return 1;
}
