/****************************************************************************
 * VelaSense DSP Unit Tests
 *
 * Tests for the DSP pipeline components:
 *   - PPG Signal Quality Index (SQI)
 *   - PPG bandpass filter (0.5-5 Hz Butterworth)
 *   - Peak detection (adaptive threshold)
 *   - Heart Rate Variability (HRV) analysis
 *
 * Framework: CMocka
 ****************************************************************************/

#ifndef __FIRMWARE_TESTS_TEST_DSP_H
#define __FIRMWARE_TESTS_TEST_DSP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

/****************************************************************************
 * Test Function Prototypes
 ****************************************************************************/

/* SQI tests */

void test_sqi_clean_signal(void **state);
void test_sqi_noisy_signal(void **state);
void test_sqi_flatline(void **state);

/* Filter tests */

void test_filter_passband(void **state);
void test_filter_stopband(void **state);

/* Peak detection tests */

void test_peak_detection_regular(void **state);
void test_peak_detection_missed_beat(void **state);

/* HRV tests */

void test_hrv_known_sequence(void **state);

/****************************************************************************
 * Helper Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: generate_sinusoidal
 *
 * Description:
 *   Generate a sinusoidal PPG-like signal at a given frequency.
 *
 ****************************************************************************/

void generate_sinusoidal(float *buf, int count, float sample_rate,
                         float freq_hz, float amplitude, float offset);

/****************************************************************************
 * Name: generate_noise
 *
 * Description:
 *   Generate pseudo-random noise with given amplitude.
 *
 ****************************************************************************/

void generate_noise(float *buf, int count, float amplitude);

/****************************************************************************
 * Name: generate_ppg_like
 *
 * Description:
 *   Generate a synthetic PPG signal with realistic cardiac morphology.
 *
 ****************************************************************************/

void generate_ppg_like(float *buf, int count, float sample_rate,
                       float heart_rate_bpm, float amplitude);

#endif /* __FIRMWARE_TESTS_TEST_DSP_H */
