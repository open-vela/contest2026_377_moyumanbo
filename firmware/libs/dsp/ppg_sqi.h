/****************************************************************************
 * PPG Signal Quality Index (SQI)
 *
 * Computes a 0.0-1.0 quality score for PPG signals based on:
 *   - Amplitude stability
 *   - Spectral energy in cardiac band (0.5-5 Hz)
 *   - Morphological consistency
 *   - Motion contamination estimate
 *
 * SQI < 0.70 → signal unreliable, do not output emotion conclusions.
 ****************************************************************************/

#ifndef __FIRMWARE_LIBS_DSP_PPG_SQI_H
#define __FIRMWARE_LIBS_DSP_PPG_SQI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct ppg_sqi_config
{
  float sample_rate;     /* PPG sample rate in Hz */
  float threshold;       /* Quality threshold (0.0-1.0), default 0.70 */
  int   window_size;     /* Analysis window size in samples */
};

struct ppg_sqi_result
{
  float quality;         /* Overall SQI score 0.0-1.0 */
  float amp_stability;   /* Amplitude stability component */
  float spectral_score;  /* Spectral energy score */
  float morph_score;     /* Morphological consistency score */
  float motion_score;    /* Motion contamination estimate (1.0 = clean) */
  int   is_reliable;     /* 1 if quality >= threshold */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: ppg_sqi_init
 *
 * Description:
 *   Initialize the SQI calculator with the given configuration.
 *
 * Input Parameters:
 *   config - SQI configuration
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int ppg_sqi_init(const struct ppg_sqi_config *config);

/****************************************************************************
 * Name: ppg_sqi_compute
 *
 * Description:
 *   Compute SQI for a window of raw PPG samples.
 *
 * Input Parameters:
 *   samples  - Array of raw PPG samples
 *   count    - Number of samples
 *   imu_data - Optional IMU data for motion estimation (NULL if unavailable)
 *   imu_count - Number of IMU samples
 *   result   - Output SQI result
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int ppg_sqi_compute(const float *samples, int count,
                    const float *imu_data, int imu_count,
                    struct ppg_sqi_result *result);

/****************************************************************************
 * Name: ppg_sqi_is_reliable
 *
 * Description:
 *   Quick check: is the last computed SQI above threshold?
 *
 * Returned Value:
 *   1 if reliable, 0 if not
 *
 ****************************************************************************/

int ppg_sqi_is_reliable(void);

#endif /* __FIRMWARE_LIBS_DSP_PPG_SQI_H */
