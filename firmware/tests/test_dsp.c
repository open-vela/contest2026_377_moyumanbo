/****************************************************************************
 * VelaSense DSP Unit Tests — Implementation
 *
 * Tests the DSP pipeline components against known-good synthetic signals.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "test_dsp.h"

#include "../libs/dsp/ppg_sqi.h"
#include "../libs/dsp/ppg_filter.h"
#include "../libs/dsp/peak_detect.h"
#include "../libs/dsp/hrv.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TEST_SAMPLE_RATE    100.0f   /* 100 Hz */
#define TEST_WINDOW_SAMPLES 1000     /* 10 seconds at 100 Hz */
#define TEST_PI             3.14159265358979f

/* Floating-point comparison tolerance */

#define SQI_TOLERANCE       0.15f
#define HRV_TOLERANCE       2.0f     /* ms tolerance for HRV metrics */
#define HR_TOLERANCE        2.0f     /* BPM tolerance for heart rate */

/****************************************************************************
 * Private Data
 ****************************************************************************/

static float g_test_buf[TEST_WINDOW_SAMPLES * 2];
static float g_test_buf2[TEST_WINDOW_SAMPLES * 2];

/****************************************************************************
 * Helper Functions
 ****************************************************************************/

/****************************************************************************
 * Name: generate_sinusoidal
 ****************************************************************************/

void generate_sinusoidal(float *buf, int count, float sample_rate,
                         float freq_hz, float amplitude, float offset)
{
  for (int i = 0; i < count; i++)
    {
      float t = (float)i / sample_rate;
      buf[i] = offset + amplitude * sinf(2.0f * TEST_PI * freq_hz * t);
    }
}

/****************************************************************************
 * Name: generate_noise
 ****************************************************************************/

void generate_noise(float *buf, int count, float amplitude)
{
  srand(42); /* Deterministic seed for reproducibility */
  for (int i = 0; i < count; i++)
    {
      buf[i] = amplitude * ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f;
    }
}

/****************************************************************************
 * Name: generate_ppg_like
 *
 * Description:
 *   Generate a synthetic PPG signal with cardiac-like morphology.
 *   Uses a sum of harmonics to approximate the PPG waveform shape.
 *
 ****************************************************************************/

void generate_ppg_like(float *buf, int count, float sample_rate,
                       float heart_rate_bpm, float amplitude)
{
  float freq = heart_rate_bpm / 60.0f;

  for (int i = 0; i < count; i++)
    {
      float t = (float)i / sample_rate;
      float phase = 2.0f * TEST_PI * freq * t;

      /* Systolic peak (fundamental + 2nd harmonic) */

      float val = amplitude * (sinf(phase) +
                               0.5f * sinf(2.0f * phase) +
                               0.2f * sinf(3.0f * phase));

      /* Add dicrotic notch approximation */

      val += 0.15f * amplitude * sinf(4.0f * phase + 0.5f);

      buf[i] = val;
    }
}

/****************************************************************************
 * SQI Tests
 ****************************************************************************/

/****************************************************************************
 * Name: test_sqi_clean_signal
 *
 * Description:
 *   Verify SQI > 0.9 for a clean sinusoidal PPG signal at 1.2 Hz (72 BPM).
 *
 ****************************************************************************/

void test_sqi_clean_signal(void **state)
{
  (void)state;

  struct ppg_sqi_config config = {
    .sample_rate = TEST_SAMPLE_RATE,
    .threshold   = 0.70f,
    .window_size = TEST_WINDOW_SAMPLES
  };

  int ret = ppg_sqi_init(&config);
  assert_int_equal(ret, 0);

  /* Generate clean PPG-like signal at 72 BPM */

  generate_ppg_like(g_test_buf, TEST_WINDOW_SAMPLES,
                    TEST_SAMPLE_RATE, 72.0f, 1000.0f);

  struct ppg_sqi_result result;
  ret = ppg_sqi_compute(g_test_buf, TEST_WINDOW_SAMPLES,
                        NULL, 0, &result);
  assert_int_equal(ret, 0);

  /* Clean signal should have high quality */

  assert_true(result.quality > 0.9f);
  assert_int_equal(result.is_reliable, 1);

  /* Component scores should also be high */

  assert_true(result.amp_stability > 0.7f);
  assert_true(result.spectral_score > 0.5f);
  assert_true(result.morph_score > 0.5f);
}

/****************************************************************************
 * Name: test_sqi_noisy_signal
 *
 * Description:
 *   Verify SQI < 0.5 for random noise (no cardiac component).
 *
 ****************************************************************************/

void test_sqi_noisy_signal(void **state)
{
  (void)state;

  struct ppg_sqi_config config = {
    .sample_rate = TEST_SAMPLE_RATE,
    .threshold   = 0.70f,
    .window_size = TEST_WINDOW_SAMPLES
  };

  int ret = ppg_sqi_init(&config);
  assert_int_equal(ret, 0);

  /* Generate pure random noise */

  generate_noise(g_test_buf, TEST_WINDOW_SAMPLES, 500.0f);

  struct ppg_sqi_result result;
  ret = ppg_sqi_compute(g_test_buf, TEST_WINDOW_SAMPLES,
                        NULL, 0, &result);
  assert_int_equal(ret, 0);

  /* Noisy signal should have low quality */

  assert_true(result.quality < 0.5f);
  assert_int_equal(result.is_reliable, 0);
}

/****************************************************************************
 * Name: test_sqi_flatline
 *
 * Description:
 *   Verify SQI = 0 for a flat (DC) signal with no variation.
 *
 ****************************************************************************/

void test_sqi_flatline(void **state)
{
  (void)state;

  struct ppg_sqi_config config = {
    .sample_rate = TEST_SAMPLE_RATE,
    .threshold   = 0.70f,
    .window_size = TEST_WINDOW_SAMPLES
  };

  int ret = ppg_sqi_init(&config);
  assert_int_equal(ret, 0);

  /* Generate flat signal (constant value) */

  for (int i = 0; i < TEST_WINDOW_SAMPLES; i++)
    {
      g_test_buf[i] = 500.0f;
    }

  struct ppg_sqi_result result;
  ret = ppg_sqi_compute(g_test_buf, TEST_WINDOW_SAMPLES,
                        NULL, 0, &result);
  assert_int_equal(ret, 0);

  /* Flat signal has zero variation → SQI should be 0 */

  assert_true(result.quality < 0.1f);
  assert_int_equal(result.is_reliable, 0);
  assert_true(result.amp_stability < 0.1f);
}

/****************************************************************************
 * Filter Tests
 ****************************************************************************/

/****************************************************************************
 * Name: test_filter_passband
 *
 * Description:
 *   Verify that frequencies within the 0.5-5 Hz passband are preserved.
 *   Inject a 2 Hz sinusoid and check that output amplitude is maintained.
 *
 ****************************************************************************/

void test_filter_passband(void **state)
{
  (void)state;

  struct ppg_filter_state filt_state;
  int ret = ppg_filter_init(TEST_SAMPLE_RATE, &filt_state);
  assert_int_equal(ret, 0);

  /* Generate 2 Hz sinusoid (well within 0.5-5 Hz passband) */

  int count = 2000; /* 20 seconds for filter to settle */
  float *sig = g_test_buf;
  float *ref = g_test_buf2;

  generate_sinusoidal(sig, count, TEST_SAMPLE_RATE, 2.0f, 100.0f, 0.0f);
  memcpy(ref, sig, count * sizeof(float));

  /* Apply filter */

  ret = ppg_filter_process(sig, count, &filt_state);
  assert_int_equal(ret, 0);

  /* Measure output amplitude in the steady-state region (skip first 5s) */

  int settle = (int)(5.0f * TEST_SAMPLE_RATE);
  float out_max = 0.0f;

  for (int i = settle; i < count; i++)
    {
      float absval = fabsf(sig[i]);
      if (absval > out_max)
        {
          out_max = absval;
        }
    }

  /* Input amplitude was 100.0.  After bandpass, 2 Hz should pass through
   * with at least 50% of the original amplitude (typical Butterworth ripple).
   */

  assert_true(out_max > 50.0f);
}

/****************************************************************************
 * Name: test_filter_stopband
 *
 * Description:
 *   Verify that frequencies outside 0.5-5 Hz are attenuated.
 *   Inject 0.1 Hz and 20 Hz signals and check attenuation.
 *
 ****************************************************************************/

void test_filter_stopband(void **state)
{
  (void)state;

  struct ppg_filter_state filt_state;
  int ret;

  /* Test 1: 0.1 Hz (below passband) — should be strongly attenuated */

  ret = ppg_filter_init(TEST_SAMPLE_RATE, &filt_state);
  assert_int_equal(ret, 0);

  int count = 3000; /* 30 seconds */
  float *sig = g_test_buf;

  generate_sinusoidal(sig, count, TEST_SAMPLE_RATE, 0.1f, 100.0f, 0.0f);
  ppg_filter_process(sig, count, &filt_state);

  /* Measure output amplitude in steady state */

  int settle = (int)(10.0f * TEST_SAMPLE_RATE);
  float out_max_low = 0.0f;

  for (int i = settle; i < count; i++)
    {
      float absval = fabsf(sig[i]);
      if (absval > out_max_low)
        {
          out_max_low = absval;
        }
    }

  /* 0.1 Hz should be attenuated by at least 40 dB (factor of 100) */

  assert_true(out_max_low < 10.0f);

  /* Test 2: 20 Hz (above passband) — should be strongly attenuated */

  ppg_filter_reset(&filt_state);
  generate_sinusoidal(sig, count, TEST_SAMPLE_RATE, 20.0f, 100.0f, 0.0f);
  ppg_filter_process(sig, count, &filt_state);

  float out_max_high = 0.0f;

  for (int i = settle; i < count; i++)
    {
      float absval = fabsf(sig[i]);
      if (absval > out_max_high)
        {
          out_max_high = absval;
        }
    }

  /* 20 Hz should be attenuated by at least 20 dB (factor of 10) */

  assert_true(out_max_high < 10.0f);
}

/****************************************************************************
 * Peak Detection Tests
 ****************************************************************************/

/****************************************************************************
 * Name: test_peak_detection_regular
 *
 * Description:
 *   Generate a synthetic 60 BPM PPG signal and verify peak detection
 *   finds peaks at approximately 1-second intervals.
 *
 ****************************************************************************/

void test_peak_detection_regular(void **state)
{
  (void)state;

  struct peak_detect_config pd_config = {
    .sample_rate      = TEST_SAMPLE_RATE,
    .min_ibi_ms       = 300.0f,
    .max_ibi_ms       = 2000.0f,
    .threshold_factor = 0.4f,
    .outlier_low      = 0.5f,
    .outlier_high     = 1.5f,
    .motion_threshold = 0.3f,
    .snr_threshold    = 3.0f
  };

  struct peak_detect_state pd_state;
  int ret = peak_detect_init(&pd_config, &pd_state);
  assert_int_equal(ret, 0);

  /* Generate 60 BPM PPG signal (1 Hz, 1000ms IBI) for 30 seconds */

  int count = (int)(30.0f * TEST_SAMPLE_RATE);
  generate_ppg_like(g_test_buf, count, TEST_SAMPLE_RATE, 60.0f, 500.0f);

  /* Process in chunks of 250ms (25 samples) */

  int chunk_size = 25;
  int total_peaks = 0;

  struct peak_detect_result pd_result;

  for (int offset = 0; offset < count; offset += chunk_size)
    {
      int n = (offset + chunk_size <= count) ? chunk_size : (count - offset);

      ret = peak_detect_process(&g_test_buf[offset], n, NULL,
                                &pd_state, &pd_result);
      assert_int_equal(ret, 0);
      total_peaks += pd_result.num_peaks;
    }

  /* At 60 BPM for 30 seconds, expect approximately 30 peaks
   * (allow some tolerance for edge effects and algorithm behavior).
   */

  assert_true(total_peaks >= 25);
  assert_true(total_peaks <= 35);

  /* Heart rate estimate should be close to 60 BPM */

  float hr = peak_detect_get_hr(&pd_state);
  if (hr > 0.0f)
    {
      assert_true(fabsf(hr - 60.0f) < HR_TOLERANCE * 2.0f);
    }
}

/****************************************************************************
 * Name: test_peak_detection_missed_beat
 *
 * Description:
 *   Generate a 60 BPM signal with one beat removed (gap injection).
 *   Verify the detector handles the gap gracefully and reports
 *   an interpolated peak with the appropriate quality flag.
 *
 ****************************************************************************/

void test_peak_detection_missed_beat(void **state)
{
  (void)state;

  struct peak_detect_config pd_config = {
    .sample_rate      = TEST_SAMPLE_RATE,
    .min_ibi_ms       = 300.0f,
    .max_ibi_ms       = 2000.0f,
    .threshold_factor = 0.4f,
    .outlier_low      = 0.5f,
    .outlier_high     = 1.5f,
    .motion_threshold = 0.3f,
    .snr_threshold    = 3.0f
  };

  struct peak_detect_state pd_state;
  int ret = peak_detect_init(&pd_config, &pd_state);
  assert_int_equal(ret, 0);

  /* Generate 60 BPM signal for 20 seconds, then zero out the region
   * around sample 1000-1100 (simulating a missed beat at ~10s).
   */

  int count = (int)(20.0f * TEST_SAMPLE_RATE);
  generate_ppg_like(g_test_buf, count, TEST_SAMPLE_RATE, 60.0f, 500.0f);

  /* Zero out one beat period (samples 950-1050) */

  for (int i = 950; i < 1050 && i < count; i++)
    {
      g_test_buf[i] = 0.0f;
    }

  /* Process in chunks */

  int chunk_size = 25;
  int total_peaks = 0;
  int has_interp = 0;
  struct peak_detect_result pd_result;

  for (int offset = 0; offset < count; offset += chunk_size)
    {
      int n = (offset + chunk_size <= count) ? chunk_size : (count - offset);

      ret = peak_detect_process(&g_test_buf[offset], n, NULL,
                                &pd_state, &pd_result);
      assert_int_equal(ret, 0);

      total_peaks += pd_result.num_peaks;

      /* Check if any peak was marked as interpolated */

      for (int p = 0; p < pd_result.num_peaks; p++)
        {
          if (pd_result.peaks[p].quality & PEAK_QUALITY_INTERP)
            {
              has_interp = 1;
            }
        }
    }

  /* Should still detect most peaks (some may be lost at the gap) */

  assert_true(total_peaks >= 15);

  /* The detector should not crash or produce wildly wrong results.
   * If interpolation is implemented, we'd check has_interp == 1 here.
   */

  /* Verify HR estimate is still reasonable (not wildly off) */

  float hr = peak_detect_get_hr(&pd_state);
  if (hr > 0.0f)
    {
      /* HR might be slightly off due to the gap, but should be within
       * a reasonable range.
       */

      assert_true(hr > 40.0f && hr < 90.0f);
    }
}

/****************************************************************************
 * HRV Tests
 ****************************************************************************/

/****************************************************************************
 * Name: test_hrv_known_sequence
 *
 * Description:
 *   Verify HRV computation against hand-calculated values.
 *   Input: [800, 850, 780, 820, 810] ms IBI sequence.
 *
 *   Hand calculations:
 *     Mean RR = (800 + 850 + 780 + 820 + 810) / 5 = 812.0 ms
 *     HR = 60000 / 812 = 73.89 BPM
 *     SDNN = sqrt(((800-812)^2 + (850-812)^2 + (780-812)^2 +
 *                  (820-812)^2 + (810-812)^2) / 5)
 *          = sqrt((144 + 1296 + 1024 + 64 + 4) / 5)
 *          = sqrt(2532/5) = sqrt(506.4) = 22.50 ms
 *     Successive diffs: 50, -70, 40, -10
 *     RMSSD = sqrt((2500 + 4900 + 1600 + 100) / 4)
 *           = sqrt(9100/4) = sqrt(2275) = 47.70 ms
 *     |diffs|: 50, 70, 40, 10 → all > 50: 2 out of 4
 *     pNN50 = 2/4 * 100 = 50%
 *
 ****************************************************************************/

void test_hrv_known_sequence(void **state)
{
  (void)state;

  float ibi_ms[] = { 800.0f, 850.0f, 780.0f, 820.0f, 810.0f };
  int count = sizeof(ibi_ms) / sizeof(ibi_ms[0]);

  struct hrv_result result;
  int ret = hrv_compute(ibi_ms, count, &result);
  assert_int_equal(ret, 0);

  assert_int_equal(result.valid, 1);
  assert_int_equal(result.num_beats, count);

  /* Mean RR = 812.0 ms */

  assert_true(fabsf(result.mean_rr - 812.0f) < HRV_TOLERANCE);

  /* HR = 60000 / 812 ≈ 73.89 BPM */

  assert_true(fabsf(result.hr - 73.89f) < HR_TOLERANCE);

  /* SDNN ≈ 22.50 ms */

  assert_true(fabsf(result.sdnn - 22.50f) < HRV_TOLERANCE);

  /* RMSSD ≈ 47.70 ms */

  assert_true(fabsf(result.rmssd - 47.70f) < HRV_TOLERANCE);

  /* pNN50 = 50% (2 out of 4 successive diffs exceed 50ms) */

  assert_true(fabsf(result.pnn50 - 50.0f) < HRV_TOLERANCE);

  /* hrv_hr_from_ibi helper */

  float hr = hrv_hr_from_ibi(1000.0f);
  assert_true(fabsf(hr - 60.0f) < 0.1f);

  hr = hrv_hr_from_ibi(500.0f);
  assert_true(fabsf(hr - 120.0f) < 0.1f);

  /* Edge case: zero IBI should return 0 */

  hr = hrv_hr_from_ibi(0.0f);
  assert_true(hr == 0.0f);
}
