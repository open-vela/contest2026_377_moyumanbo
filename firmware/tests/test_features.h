/****************************************************************************
 * VelaSense Feature Extraction Unit Tests
 *
 * Tests for the feature extraction pipeline:
 *   - 15-dimensional feature vector construction
 *   - Personal baseline (exponential moving average)
 *   - Time-of-day bin selection
 *   - Activity classifier (rest / walk / run)
 *
 * Framework: CMocka
 ****************************************************************************/

#ifndef __FIRMWARE_TESTS_TEST_FEATURES_H
#define __FIRMWARE_TESTS_TEST_FEATURES_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <math.h>
#include <string.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Feature vector dimensions */

#define FEATURE_DIM             15

/* Feature indices */

#define FEAT_HR                 0   /* Heart rate (BPM) */
#define FEAT_HR_SLOPE           1   /* HR trend over window */
#define FEAT_RMSSD              2   /* RMSSD (ms) */
#define FEAT_SDNN               3   /* SDNN (ms) */
#define FEAT_IBI_DISPERSION     4   /* IBI coefficient of variation */
#define FEAT_ACTIVITY_INTENSITY 5   /* Activity level 0-3 */
#define FEAT_POSTURE            6   /* Posture type 0-3 */
#define FEAT_SCL                7   /* Skin conductance level (uS) */
#define FEAT_SCR_COUNT          8   /* SCR count in window */
#define FEAT_SCR_AMP            9   /* Mean SCR amplitude */
#define FEAT_SKIN_TEMP          10  /* Skin temperature (C) */
#define FEAT_SKIN_TEMP_SLOPE    11  /* Temperature trend */
#define FEAT_SQI                12  /* Signal quality index */
#define FEAT_WEAR_DURATION      13  /* Continuous wear time (min) */
#define FEAT_TIME_OF_DAY        14  /* Time-of-day bin (0-5) */

/* Time-of-day bins */

#define TOD_BIN_NIGHT     0   /* 00:00 - 04:00 */
#define TOD_BIN_EARLY     1   /* 04:00 - 08:00 */
#define TOD_BIN_MORNING   2   /* 08:00 - 12:00 */
#define TOD_BIN_AFTERNOON 3   /* 12:00 - 16:00 */
#define TOD_BIN_EVENING   4   /* 16:00 - 20:00 */
#define TOD_BIN_LATE      5   /* 20:00 - 24:00 */

/* Baseline parameters */

#define BASELINE_EMA_ALPHA      0.05f  /* EMA smoothing factor */
#define BASELINE_HISTORY_LEN    288    /* 24h at 5-min intervals */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Baseline context */

struct feature_baseline
{
  float   mean[FEATURE_DIM];           /* Running EMA mean */
  float   variance[FEATURE_DIM];       /* Running EMA variance */
  int     sample_count;                /* Total samples incorporated */
  int     initialized;                 /* 1 if baseline is ready */
  int     time_of_day;                 /* Current TOD bin */
};

/* Feature extraction context */

struct feature_extractor_ctx
{
  float                feature_vec[FEATURE_DIM];
  struct feature_baseline baseline;
  int                  window_sec;     /* Analysis window in seconds */
  int                  sample_count;   /* Samples in current window */
  int                  initialized;
};

/****************************************************************************
 * Test Function Prototypes
 ****************************************************************************/

void test_feature_extraction_15dims(void **state);
void test_baseline_update(void **state);
void test_baseline_time_of_day(void **state);
void test_activity_classifier_rest(void **state);
void test_activity_classifier_walk(void **state);

/****************************************************************************
 * Helper Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: feature_extract_init
 *
 * Description:
 *   Initialize the feature extractor context.
 *
 ****************************************************************************/

int feature_extract_init(struct feature_extractor_ctx *ctx, int window_sec);

/****************************************************************************
 * Name: feature_extract_compute
 *
 * Description:
 *   Compute the 15-dimensional feature vector from DSP outputs.
 *
 ****************************************************************************/

int feature_extract_compute(struct feature_extractor_ctx *ctx,
                            float hr, float hr_slope,
                            float rmssd, float sdnn,
                            float activity_intensity, float posture,
                            float scl, float scr_count, float scr_amp,
                            float skin_temp, float skin_temp_slope,
                            float sqi, float wear_duration_min,
                            int hour_of_day);

/****************************************************************************
 * Name: baseline_update
 *
 * Description:
 *   Update the personal baseline with new feature vector using EMA.
 *
 ****************************************************************************/

int baseline_update(struct feature_baseline *baseline,
                    const float *features, int dim);

/****************************************************************************
 * Name: baseline_get_deviation
 *
 * Description:
 *   Compute z-score deviation of current features from baseline.
 *
 ****************************************************************************/

int baseline_get_deviation(const struct feature_baseline *baseline,
                           const float *features, float *deviation, int dim);

/****************************************************************************
 * Name: get_time_of_day_bin
 *
 * Description:
 *   Map hour (0-23) to time-of-day bin.
 *
 ****************************************************************************/

int get_time_of_day_bin(int hour);

#endif /* __FIRMWARE_TESTS_TEST_FEATURES_H */
