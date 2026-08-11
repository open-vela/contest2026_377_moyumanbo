/****************************************************************************
 * VelaSense Integration Test Scenarios
 *
 * End-to-end tests that exercise the full data pipeline:
 *   sensor → DSP → features → inference → event → confirm → store → BLE
 *
 * Framework: CMocka
 ****************************************************************************/

#ifndef __FIRMWARE_TESTS_TEST_INTEGRATION_H
#define __FIRMWARE_TESTS_TEST_INTEGRATION_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define INTEG_SAMPLE_RATE       100.0f
#define INTEG_WINDOW_SEC        60
#define INTEG_WINDOW_SAMPLES    ((int)(INTEG_SAMPLE_RATE * INTEG_WINDOW_SEC))
#define INTEG_FEATURE_DIM       15

/****************************************************************************
 * Test Function Prototypes
 ****************************************************************************/

/* Scenario 1: Full pipeline flow */

void test_integration_full_flow(void **state);

/* Scenario 2: Motion rejection */

void test_integration_motion_rejection(void **state);

/* Scenario 3: Low SQI handling */

void test_integration_low_sqi(void **state);

/* Scenario 4: BLE disconnect/reconnect */

void test_integration_ble_reconnect(void **state);

/* Scenario 5: Battery low behavior */

void test_integration_battery_low(void **state);

/* Scenario 6: Privacy verification across all stages */

void test_integration_privacy_no_leaks(void **state);

/****************************************************************************
 * Helper Types
 ****************************************************************************/

/* Simulated pipeline state */

struct integ_pipeline_state
{
  /* Sensor data buffers */

  float ppg_buf[INTEG_WINDOW_SAMPLES];
  float imu_buf[INTEG_WINDOW_SAMPLES];
  float eda_buf[INTEG_WINDOW_SAMPLES / 3]; /* 32 Hz / 100 Hz ratio */
  float temp_buf[INTEG_WINDOW_SEC];

  /* DSP outputs */

  float sqi;
  float filtered_ppg[INTEG_WINDOW_SAMPLES];
  float heart_rate;
  float rmssd;
  float sdnn;

  /* Feature vector */

  float features[INTEG_FEATURE_DIM];

  /* Inference output */

  float confidence;
  uint32_t reason_flags;

  /* Event state */

  int event_triggered;
  int event_confirmed;
  uint8_t event_label;

  /* BLE state */

  int ble_connected;
  int ble_synced;
  int events_queued;

  /* System state */

  float battery_pct;
  int low_battery;
  int motion_rejected;
  int sqi_low;
};

/****************************************************************************
 * Helper Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: integ_init_pipeline
 *
 * Description:
 *   Initialize the integration test pipeline state.
 *
 ****************************************************************************/

void integ_init_pipeline(struct integ_pipeline_state *state);

/****************************************************************************
 * Name: integ_run_dsp_stage
 *
 * Description:
 *   Run the DSP stage (SQI + filter + peak detect + HRV).
 *
 ****************************************************************************/

int integ_run_dsp_stage(struct integ_pipeline_state *state);

/****************************************************************************
 * Name: integ_run_feature_stage
 *
 * Description:
 *   Run the feature extraction stage.
 *
 ****************************************************************************/

int integ_run_feature_stage(struct integ_pipeline_state *state);

/****************************************************************************
 * Name: integ_run_inference_stage
 *
 * Description:
 *   Run the inference stage (simulated model output).
 *
 ****************************************************************************/

int integ_run_inference_stage(struct integ_pipeline_state *state);

/****************************************************************************
 * Name: integ_run_event_stage
 *
 * Description:
 *   Run the event state machine stage.
 *
 ****************************************************************************/

int integ_run_event_stage(struct integ_pipeline_state *state);

/****************************************************************************
 * Name: integ_simulate_ble_sync
 *
 * Description:
 *   Simulate BLE event synchronization.
 *
 ****************************************************************************/

int integ_simulate_ble_sync(struct integ_pipeline_state *state);

#endif /* __FIRMWARE_TESTS_TEST_INTEGRATION_H */
