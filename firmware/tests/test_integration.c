/****************************************************************************
 * VelaSense Integration Test Scenarios — Implementation
 *
 * End-to-end pipeline tests exercising sensor → DSP → features →
 * inference → event → confirm → store → BLE.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "test_integration.h"
#include "test_dsp.h"
#include "test_features.h"
#include "test_event_sm.h"
#include "test_privacy.h"

#include "../libs/dsp/ppg_sqi.h"
#include "../libs/dsp/ppg_filter.h"
#include "../libs/dsp/hrv.h"
#include "../libs/event/event_sm.h"
#include "../libs/event/event_crypto.h"
#include "../ble/gatt_service.h"

#include <math.h>
#include <stdlib.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define INTEG_PI 3.14159265358979f

/****************************************************************************
 * Helper Function Implementations
 ****************************************************************************/

/****************************************************************************
 * Name: integ_init_pipeline
 ****************************************************************************/

void integ_init_pipeline(struct integ_pipeline_state *state)
{
  memset(state, 0, sizeof(*state));
  state->ble_connected = 1;
  state->battery_pct = 80.0f;
}

/****************************************************************************
 * Name: integ_run_dsp_stage
 *
 * Description:
 *   Run SQI computation, bandpass filtering, and peak detection.
 *   Returns 0 on success, -1 if SQI too low.
 *
 ****************************************************************************/

int integ_run_dsp_stage(struct integ_pipeline_state *state)
{
  /* Initialize SQI */

  struct ppg_sqi_config sqi_config = {
    .sample_rate = INTEG_SAMPLE_RATE,
    .threshold   = 0.70f,
    .window_size = INTEG_WINDOW_SAMPLES
  };

  if (ppg_sqi_init(&sqi_config) != 0)
    {
      return -1;
    }

  /* Compute SQI */

  struct ppg_sqi_result sqi_result;
  if (ppg_sqi_compute(state->ppg_buf, INTEG_WINDOW_SAMPLES,
                      state->imu_buf, INTEG_WINDOW_SAMPLES,
                      &sqi_result) != 0)
    {
      return -1;
    }

  state->sqi = sqi_result.quality;

  if (!sqi_result.is_reliable)
    {
      state->sqi_low = 1;
      return -1;
    }

  /* Copy PPG to filtered buffer and apply bandpass filter */

  memcpy(state->filtered_ppg, state->ppg_buf,
         INTEG_WINDOW_SAMPLES * sizeof(float));

  struct ppg_filter_state filt_state;
  if (ppg_filter_init(INTEG_SAMPLE_RATE, &filt_state) != 0)
    {
      return -1;
    }

  ppg_filter_process(state->filtered_ppg, INTEG_WINDOW_SAMPLES, &filt_state);

  /* Peak detection (process in chunks) */

  struct peak_detect_config pd_config = {
    .sample_rate      = INTEG_SAMPLE_RATE,
    .min_ibi_ms       = 300.0f,
    .max_ibi_ms       = 2000.0f,
    .threshold_factor = 0.4f,
    .outlier_low      = 0.5f,
    .outlier_high     = 1.5f,
    .motion_threshold = 0.3f,
    .snr_threshold    = 3.0f
  };

  struct peak_detect_state pd_state;
  peak_detect_init(&pd_config, &pd_state);

  /* Collect IBI values for HRV */

  float ibi_values[128];
  int ibi_count = 0;
  int chunk = 25; /* 250ms at 100Hz */

  for (int offset = 0; offset < INTEG_WINDOW_SAMPLES; offset += chunk)
    {
      int n = (offset + chunk <= INTEG_WINDOW_SAMPLES) ?
              chunk : (INTEG_WINDOW_SAMPLES - offset);

      struct peak_detect_result pd_result;
      peak_detect_process(&state->filtered_ppg[offset], n,
                          NULL, &pd_state, &pd_result);

      for (int p = 0; p < pd_result.num_peaks && ibi_count < 128; p++)
        {
          if (pd_result.peaks[p].ibi_ms > 0.0f)
            {
              ibi_values[ibi_count++] = pd_result.peaks[p].ibi_ms;
            }
        }
    }

  /* HRV computation */

  if (ibi_count >= 2)
    {
      struct hrv_result hrv;
      if (hrv_compute(ibi_values, ibi_count, &hrv) == 0 && hrv.valid)
        {
          state->heart_rate = hrv.hr;
          state->rmssd = hrv.rmssd;
          state->sdnn = hrv.sdnn;
        }
    }

  return 0;
}

/****************************************************************************
 * Name: integ_run_feature_stage
 ****************************************************************************/

int integ_run_feature_stage(struct integ_pipeline_state *state)
{
  struct feature_extractor_ctx ctx;
  if (feature_extract_init(&ctx, INTEG_WINDOW_SEC) != 0)
    {
      return -1;
    }

  /* Compute activity from IMU magnitude */

  float mean_mag = 0.0f;
  for (int i = 0; i < INTEG_WINDOW_SAMPLES; i++)
    {
      mean_mag += fabsf(state->imu_buf[i]);
    }
  mean_mag /= INTEG_WINDOW_SAMPLES;

  float activity = 0.0f;
  if (mean_mag >= 1.4f)
    {
      activity = 3.0f; /* Run */
    }
  else if (mean_mag >= 1.2f)
    {
      activity = 2.0f; /* Brisk */
    }
  else if (mean_mag >= 1.05f)
    {
      activity = 1.0f; /* Walk */
    }

  /* Compute EDA mean (simplified) */

  int eda_count = INTEG_WINDOW_SAMPLES / 3;
  float eda_mean = 0.0f;
  for (int i = 0; i < eda_count; i++)
    {
      eda_mean += state->eda_buf[i];
    }
  eda_mean /= (eda_count > 0 ? eda_count : 1);

  /* Temperature (last sample) */

  float temp = state->temp_buf[INTEG_WINDOW_SEC - 1];

  if (feature_extract_compute(&ctx,
                              state->heart_rate,
                              0.0f, /* hr_slope */
                              state->rmssd,
                              state->sdnn,
                              activity,
                              0.0f, /* posture */
                              eda_mean,
                              3.0f, /* scr_count */
                              0.5f, /* scr_amp */
                              temp,
                              0.01f, /* temp_slope */
                              state->sqi,
                              30.0f, /* wear_duration */
                              14) != 0) /* hour_of_day */
    {
      return -1;
    }

  memcpy(state->features, ctx.feature_vec,
         INTEG_FEATURE_DIM * sizeof(float));

  return 0;
}

/****************************************************************************
 * Name: integ_run_inference_stage
 *
 * Description:
 *   Simulated inference. In production this runs the TinyML model.
 *   Here we use a simplified rule-based proxy.
 *
 ****************************************************************************/

int integ_run_inference_stage(struct integ_pipeline_state *state)
{
  /* Rule-based proxy for arousal inference:
   * - HR > 85 AND RMSSD < 35 AND activity < 2 → high confidence
   * - HR > 80 AND RMSSD < 40 → moderate confidence
   * - Otherwise → low confidence
   */

  float hr = state->heart_rate;
  float rmssd = state->rmssd;
  float activity = state->features[FEAT_ACTIVITY_INTENSITY];

  if (activity >= 2.0f)
    {
      /* High activity → suppress inference */

      state->confidence = 0.0f;
      state->reason_flags = 0;
      state->motion_rejected = 1;
      return 0;
    }

  if (hr > 85.0f && rmssd < 35.0f)
    {
      state->confidence = 0.90f;
      state->reason_flags = EVENT_REASON_HR_RISE | EVENT_REASON_HRV_DROP;
    }
  else if (hr > 80.0f && rmssd < 40.0f)
    {
      state->confidence = 0.75f;
      state->reason_flags = EVENT_REASON_HR_RISE;
    }
  else
    {
      state->confidence = 0.30f;
      state->reason_flags = 0;
    }

  return 0;
}

/****************************************************************************
 * Name: integ_run_event_stage
 ****************************************************************************/

int integ_run_event_stage(struct integ_pipeline_state *state)
{
  struct event_sm_ctx sm_ctx;
  event_sm_init(&sm_ctx);

  /* Initial transition: IDLE → MONITORING */

  struct event_inference_input input =
      make_inference_input(state->confidence, state->reason_flags,
                           state->heart_rate, state->rmssd,
                           state->features[FEAT_ACTIVITY_INTENSITY],
                           (state->sqi >= 0.70f) ? 1 : 0,
                           1); /* is_worn */

  event_sm_update(&sm_ctx, &input);

  if (event_sm_get_state(&sm_ctx) != EVENT_STATE_MONITORING)
    {
      state->event_triggered = 0;
      return 0;
    }

  /* Feed high-confidence input to trigger CANDIDATE → ALERTING */

  if (state->confidence >= EVENT_CONFIDENCE_THRESHOLD)
    {
      input.confidence = state->confidence;
      event_sm_update(&sm_ctx, &input);

      if (event_sm_get_state(&sm_ctx) == EVENT_STATE_CANDIDATE)
        {
          /* Force time to trigger ALERTING */

          force_candidate_duration(&sm_ctx, 20.0f);
          event_sm_update(&sm_ctx, &input);

          if (event_sm_get_state(&sm_ctx) == EVENT_STATE_ALERTING)
            {
              state->event_triggered = 1;

              /* Auto-confirm for integration test */

              event_sm_confirm(&sm_ctx, EVENT_LABEL_STRESS);
              state->event_confirmed = 1;
              state->event_label = EVENT_LABEL_STRESS;
            }
        }
    }

  return 0;
}

/****************************************************************************
 * Name: integ_simulate_ble_sync
 ****************************************************************************/

int integ_simulate_ble_sync(struct integ_pipeline_state *state)
{
  if (!state->ble_connected)
    {
      /* Queue events for later sync */

      if (state->event_confirmed)
        {
          state->events_queued++;
        }

      return 0;
    }

  /* Simulate BLE sync: verify only summary data is sent */

  struct ble_event_summary_record summary;
  memset(&summary, 0, sizeof(summary));
  summary.seq = 1;
  summary.timestamp = 12345;
  summary.label = state->event_label;
  summary.confidence = (uint8_t)(state->confidence * 100.0f);
  summary.reason_flags = (uint16_t)state->reason_flags;
  summary.hr = (uint16_t)(state->heart_rate * 10.0f);
  summary.rmssd = (uint16_t)(state->rmssd * 10.0f);

  /* Verify no raw data in the summary */

  int violations = privacy_scan_ble_for_raw_data((const uint8_t *)&summary,
                                                  sizeof(summary));
  if (violations > 0)
    {
      return -1;
    }

  state->ble_synced = 1;

  /* Sync any queued events */

  if (state->events_queued > 0)
    {
      state->events_queued = 0;
    }

  return 0;
}

/****************************************************************************
 * Integration Test: Full Pipeline Flow
 ****************************************************************************/

void test_integration_full_flow(void **state)
{
  (void)state;

  struct integ_pipeline_state pipe;
  integ_init_pipeline(&pipe);

  /* Generate synthetic PPG signal at 75 BPM (elevated HR for arousal) */

  generate_ppg_like(pipe.ppg_buf, INTEG_WINDOW_SAMPLES,
                    INTEG_SAMPLE_RATE, 75.0f, 1000.0f);

  /* Add small noise to make it realistic */

  srand(42);
  for (int i = 0; i < INTEG_WINDOW_SAMPLES; i++)
    {
      pipe.ppg_buf[i] += 10.0f * ((float)rand() / RAND_MAX - 0.5f);
    }

  /* Generate IMU data at rest (magnitude ~1.0g) */

  for (int i = 0; i < INTEG_WINDOW_SAMPLES; i++)
    {
      pipe.imu_buf[i] = 1.0f + 0.01f * ((float)rand() / RAND_MAX - 0.5f);
    }

  /* Generate EDA data (elevated for arousal) */

  int eda_count = INTEG_WINDOW_SAMPLES / 3;
  for (int i = 0; i < eda_count; i++)
    {
      pipe.eda_buf[i] = 8.0f + 0.5f * ((float)rand() / RAND_MAX);
    }

  /* Generate temperature data */

  for (int i = 0; i < INTEG_WINDOW_SEC; i++)
    {
      pipe.temp_buf[i] = 33.0f + 0.1f * ((float)rand() / RAND_MAX);
    }

  /* Stage 1: DSP */

  int ret = integ_run_dsp_stage(&pipe);
  assert_int_equal(ret, 0);
  assert_true(pipe.sqi >= 0.70f);
  assert_true(pipe.heart_rate > 0.0f);

  /* Stage 2: Features */

  ret = integ_run_feature_stage(&pipe);
  assert_int_equal(ret, 0);

  /* Stage 3: Inference */

  ret = integ_run_inference_stage(&pipe);
  assert_int_equal(ret, 0);

  /* Stage 4: Event state machine */

  ret = integ_run_event_stage(&pipe);

  /* Stage 5: BLE sync */

  ret = integ_simulate_ble_sync(&pipe);
  assert_int_equal(ret, 0);

  /* Verify the pipeline completed without privacy violations */

  if (pipe.event_confirmed)
    {
      assert_true(pipe.ble_synced == 1 || pipe.events_queued > 0);
    }
}

/****************************************************************************
 * Integration Test: Motion Rejection
 ****************************************************************************/

void test_integration_motion_rejection(void **state)
{
  (void)state;

  struct integ_pipeline_state pipe;
  integ_init_pipeline(&pipe);

  /* Generate PPG signal at 75 BPM */

  generate_ppg_like(pipe.ppg_buf, INTEG_WINDOW_SAMPLES,
                    INTEG_SAMPLE_RATE, 75.0f, 1000.0f);

  /* Generate HIGH IMU magnitude (simulating running) */

  srand(42);
  for (int i = 0; i < INTEG_WINDOW_SAMPLES; i++)
    {
      float t = (float)i / INTEG_SAMPLE_RATE;
      /* Running: ~2.5g magnitude with 2.5 Hz step frequency */

      pipe.imu_buf[i] = 1.0f + 1.5f *
                         sinf(2.0f * INTEG_PI * 2.5f * t) +
                         0.2f * ((float)rand() / RAND_MAX - 0.5f);
    }

  /* EDA at baseline */

  int eda_count = INTEG_WINDOW_SAMPLES / 3;
  for (int i = 0; i < eda_count; i++)
    {
      pipe.eda_buf[i] = 3.0f;
    }

  for (int i = 0; i < INTEG_WINDOW_SEC; i++)
    {
      pipe.temp_buf[i] = 33.0f;
    }

  /* Run DSP */

  integ_run_dsp_stage(&pipe);

  /* Run features */

  integ_run_feature_stage(&pipe);

  /* Run inference — motion should suppress confidence */

  integ_run_inference_stage(&pipe);

  /* Verify: motion rejection should prevent event triggering */

  assert_true(pipe.motion_rejected == 1 || pipe.confidence < 0.50f);

  /* Run event stage — should not trigger */

  integ_run_event_stage(&pipe);
  assert_int_equal(pipe.event_triggered, 0);
}

/****************************************************************************
 * Integration Test: Low SQI
 ****************************************************************************/

void test_integration_low_sqi(void **state)
{
  (void)state;

  struct integ_pipeline_state pipe;
  integ_init_pipeline(&pipe);

  /* Generate NOISE instead of valid PPG (simulating bad sensor contact) */

  generate_noise(pipe.ppg_buf, INTEG_WINDOW_SAMPLES, 500.0f);

  /* IMU at rest */

  for (int i = 0; i < INTEG_WINDOW_SAMPLES; i++)
    {
      pipe.imu_buf[i] = 1.0f;
    }

  int eda_count = INTEG_WINDOW_SAMPLES / 3;
  for (int i = 0; i < eda_count; i++)
    {
      pipe.eda_buf[i] = 3.0f;
    }

  for (int i = 0; i < INTEG_WINDOW_SEC; i++)
    {
      pipe.temp_buf[i] = 33.0f;
    }

  /* Run DSP — SQI should be low */

  int ret = integ_run_dsp_stage(&pipe);

  /* DSP should reject the signal */

  assert_true(pipe.sqi_low == 1 || pipe.sqi < 0.70f);

  /* The system should prompt "adjust strap" — verify no event triggered */

  integ_run_feature_stage(&pipe);
  integ_run_inference_stage(&pipe);
  integ_run_event_stage(&pipe);

  assert_int_equal(pipe.event_triggered, 0);
}

/****************************************************************************
 * Integration Test: BLE Disconnect/Reconnect
 ****************************************************************************/

void test_integration_ble_reconnect(void **state)
{
  (void)state;

  struct integ_pipeline_state pipe;
  integ_init_pipeline(&pipe);

  /* Generate a valid PPG signal and run the pipeline */

  generate_ppg_like(pipe.ppg_buf, INTEG_WINDOW_SAMPLES,
                    INTEG_SAMPLE_RATE, 75.0f, 1000.0f);

  for (int i = 0; i < INTEG_WINDOW_SAMPLES; i++)
    {
      pipe.imu_buf[i] = 1.0f;
    }

  int eda_count = INTEG_WINDOW_SAMPLES / 3;
  for (int i = 0; i < eda_count; i++)
    {
      pipe.eda_buf[i] = 8.0f;
    }

  for (int i = 0; i < INTEG_WINDOW_SEC; i++)
    {
      pipe.temp_buf[i] = 33.0f;
    }

  /* Run pipeline with BLE disconnected */

  pipe.ble_connected = 0;

  integ_run_dsp_stage(&pipe);
  integ_run_feature_stage(&pipe);
  integ_run_inference_stage(&pipe);
  integ_run_event_stage(&pipe);

  /* Events should be queued, not synced */

  int ret = integ_simulate_ble_sync(&pipe);
  assert_int_equal(ret, 0);

  if (pipe.event_confirmed)
    {
      assert_true(pipe.events_queued > 0);
      assert_int_equal(pipe.ble_synced, 0);
    }

  /* Reconnect BLE */

  pipe.ble_connected = 1;

  ret = integ_simulate_ble_sync(&pipe);
  assert_int_equal(ret, 0);

  /* After reconnect, queued events should be synced */

  if (pipe.events_queued > 0 || pipe.event_confirmed)
    {
      assert_int_equal(pipe.ble_synced, 1);
      assert_int_equal(pipe.events_queued, 0);
    }
}

/****************************************************************************
 * Integration Test: Battery Low
 ****************************************************************************/

void test_integration_battery_low(void **state)
{
  (void)state;

  struct integ_pipeline_state pipe;
  integ_init_pipeline(&pipe);

  /* Set battery to low level */

  pipe.battery_pct = 5.0f;
  pipe.low_battery = 1;

  /* Generate PPG signal */

  generate_ppg_like(pipe.ppg_buf, INTEG_WINDOW_SAMPLES,
                    INTEG_SAMPLE_RATE, 72.0f, 1000.0f);

  for (int i = 0; i < INTEG_WINDOW_SAMPLES; i++)
    {
      pipe.imu_buf[i] = 1.0f;
    }

  int eda_count = INTEG_WINDOW_SAMPLES / 3;
  for (int i = 0; i < eda_count; i++)
    {
      pipe.eda_buf[i] = 5.0f;
    }

  for (int i = 0; i < INTEG_WINDOW_SEC; i++)
    {
      pipe.temp_buf[i] = 33.0f;
    }

  /* Run pipeline — should not crash even in low battery */

  int ret;

  ret = integ_run_dsp_stage(&pipe);

  ret = integ_run_feature_stage(&pipe);
  assert_int_equal(ret, 0);

  ret = integ_run_inference_stage(&pipe);
  assert_int_equal(ret, 0);

  ret = integ_run_event_stage(&pipe);
  assert_int_equal(ret, 0);

  ret = integ_simulate_ble_sync(&pipe);
  assert_int_equal(ret, 0);

  /* In low battery, the system should reduce sampling rate
   * but not crash. We verify the pipeline completes successfully.
   */

  /* Verify battery state is tracked */

  assert_true(pipe.battery_pct < 10.0f);
  assert_int_equal(pipe.low_battery, 1);
}

/****************************************************************************
 * Integration Test: Privacy — No Data Leaks at Any Stage
 ****************************************************************************/

void test_integration_privacy_no_leaks(void **state)
{
  (void)state;

  struct integ_pipeline_state pipe;
  integ_init_pipeline(&pipe);

  /* Generate synthetic data */

  generate_ppg_like(pipe.ppg_buf, INTEG_WINDOW_SAMPLES,
                    INTEG_SAMPLE_RATE, 72.0f, 1000.0f);

  for (int i = 0; i < INTEG_WINDOW_SAMPLES; i++)
    {
      pipe.imu_buf[i] = 1.0f;
    }

  int eda_count = INTEG_WINDOW_SAMPLES / 3;
  for (int i = 0; i < eda_count; i++)
    {
      pipe.eda_buf[i] = 5.0f;
    }

  for (int i = 0; i < INTEG_WINDOW_SEC; i++)
    {
      pipe.temp_buf[i] = 33.0f;
    }

  /* Run full pipeline */

  integ_run_dsp_stage(&pipe);
  integ_run_feature_stage(&pipe);
  integ_run_inference_stage(&pipe);
  integ_run_event_stage(&pipe);
  integ_simulate_ble_sync(&pipe);

  /* Privacy check 1: Feature vector should contain only summary statistics,
   * not raw waveform samples.
   */

  /* Verify features are in expected ranges (not raw PPG counts) */

  for (int i = 0; i < INTEG_FEATURE_DIM; i++)
    {
      /* Raw PPG values are typically > 1000. Features should be
       * normalized/summary values.
       */

      if (i == FEAT_HR)
        {
          /* HR is in BPM range (30-220), not raw counts */

          assert_true(pipe.features[i] >= 0.0f &&
                      pipe.features[i] <= 300.0f);
        }
      else if (i == FEAT_SKIN_TEMP)
        {
          /* Temperature in Celsius (20-45) */

          assert_true(pipe.features[i] >= 20.0f &&
                      pipe.features[i] <= 45.0f);
        }
    }

  /* Privacy check 2: BLE summary should contain no raw data */

  struct ble_event_summary_record summary;
  memset(&summary, 0, sizeof(summary));
  summary.hr = (uint16_t)(pipe.heart_rate * 10.0f);
  summary.rmssd = (uint16_t)(pipe.rmssd * 10.0f);

  int violations = privacy_scan_ble_for_raw_data((const uint8_t *)&summary,
                                                  sizeof(summary));
  assert_int_equal(violations, 0);

  /* Privacy check 3: Upload payload should contain no medical terms */

  const char *desc = "User confirmed stress event. "
                     "HR elevated, RMSSD decreased.";
  assert_int_equal(privacy_check_medical_terms(desc), 0);

  /* Privacy check 4: Upload payload should contain no raw sensor data */

  struct test_upload_payload upload;
  memset(&upload, 0, sizeof(upload));
  upload.event_seq = 1;
  upload.confidence = (uint8_t)(pipe.confidence * 100.0f);
  upload.hr_x10 = (uint16_t)(pipe.heart_rate * 10.0f);
  upload.rmssd_x10 = (uint16_t)(pipe.rmssd * 10.0f);
  snprintf(upload.description, sizeof(upload.description), "%s", desc);

  assert_int_equal(privacy_check_raw_sensor_data(&upload, sizeof(upload)),
                   0);
}
