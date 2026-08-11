/****************************************************************************
 * Activity Classifier
 *
 * Classifies user activity from IMU accelerometer data using a simple
 * rule-based approach:
 *
 *   - Compute acceleration magnitude: sqrt(ax^2 + ay^2 + az^2)
 *   - Compute spectral energy in 0.5-3 Hz band (walking frequency)
 *   - Rest:  magnitude < 1.1g and spectral energy < threshold
 *   - Walk:  magnitude 1.1-1.4g and spectral energy in walk band
 *   - Run:   magnitude > 1.4g or spectral energy > run threshold
 *   - Cycle: magnitude 0.9-1.2g with low vertical component
 *
 * Uses a 5-second window with majority voting for temporal stability.
 * Operates on acceleration data in g units (1g = 9.80665 m/s^2).
 *
 ****************************************************************************/

#ifndef __FIRMWARE_LIBS_FEATURES_ACTIVITY_CLASSIFY_H
#define __FIRMWARE_LIBS_FEATURES_ACTIVITY_CLASSIFY_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Activity classification window: 5 seconds at 100 Hz = 500 samples */

#define ACTIVITY_WINDOW_SEC     5
#define ACTIVITY_IMU_RATE_HZ    100
#define ACTIVITY_WINDOW_SAMPLES (ACTIVITY_WINDOW_SEC * ACTIVITY_IMU_RATE_HZ)

/* Thresholds in g units */

#define ACTIVITY_REST_MAG_MAX       1.1f    /* Rest: magnitude < 1.1g */
#define ACTIVITY_WALK_MAG_MIN       1.05f   /* Walk: magnitude lower bound */
#define ACTIVITY_WALK_MAG_MAX       1.4f    /* Walk: magnitude upper bound */
#define ACTIVITY_RUN_MAG_MIN        1.35f   /* Run:  magnitude lower bound */

/* Spectral energy thresholds (normalized) */

#define ACTIVITY_WALK_SPEC_MIN      0.02f   /* Walk band minimum energy */
#define ACTIVITY_WALK_SPEC_MAX      0.15f   /* Walk band maximum energy */
#define ACTIVITY_RUN_SPEC_MIN       0.10f   /* Run  band minimum energy */

/* Cycle detection: low vertical component relative to total */

#define ACTIVITY_CYCLE_VERTICAL_RATIO  0.5f /* Vertical / total ratio */
#define ACTIVITY_CYCLE_MAG_MIN         0.9f
#define ACTIVITY_CYCLE_MAG_MAX         1.2f

/* Majority voting: at least this fraction of sub-windows must agree */

#define ACTIVITY_VOTE_THRESHOLD     0.6f

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Activity levels (matches feature vector encoding) */

enum activity_level
{
  ACTIVITY_REST  = 0,   /* Stationary / sitting / standing */
  ACTIVITY_WALK  = 1,   /* Walking */
  ACTIVITY_BRISK = 2,   /* Brisk walk / light jog */
  ACTIVITY_RUN   = 3,   /* Running */
  ACTIVITY_CYCLE = 4    /* Cycling (future) */
};

/* Posture classification (from IMU orientation) */

enum posture_type
{
  POSTURE_SIT  = 0,     /* Seated / reclined */
  POSTURE_STAND = 1,    /* Standing upright */
  POSTURE_WALK  = 2,    /* Walking (same as activity) */
  POSTURE_RUN   = 3     /* Running (same as activity) */
};

/* Sub-window classification result (internal) */

struct activity_subwindow
{
  float mean_magnitude;       /* Mean accel magnitude (g) */
  float peak_magnitude;       /* Peak accel magnitude (g) */
  float spectral_energy;      /* Normalized spectral energy in walk band */
  float vertical_ratio;       /* Vertical axis dominance */
  enum activity_level level;  /* Classification for this sub-window */
};

/* Full classification result */

struct activity_result
{
  enum activity_level intensity;  /* 0=rest, 1=walk, 2=brisk, 3=run */
  enum posture_type   posture;    /* 0=sit, 1=stand, 2=walk, 3=run */
  float   confidence;             /* Classification confidence 0-1 */
  float   mean_magnitude;         /* Window mean accel magnitude (g) */
  float   dominant_freq_hz;       /* Dominant frequency estimate (Hz) */
  int     num_subwindows;         /* Number of sub-windows processed */
  int     valid;                  /* 1 if result is valid */
};

/* Classifier context (maintains state between calls) */

struct activity_classifier
{
  float   accel_buf[ACTIVITY_WINDOW_SAMPLES]; /* Circular accel magnitude */
  int     buf_idx;                /* Current write index */
  int     buf_count;              /* Number of samples collected */
  int     initialized;            /* 1 if initialized */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: activity_classify_init
 *
 * Description:
 *   Initialize the activity classifier context.
 *
 * Input Parameters:
 *   ctx - Classifier context to initialize
 *
 * Returned Value:
 *   0 on success, -1 on failure
 *
 ****************************************************************************/

int activity_classify_init(struct activity_classifier *ctx);

/****************************************************************************
 * Name: activity_classify_push
 *
 * Description:
 *   Push a single accelerometer magnitude sample into the classifier.
 *   When the window is full, the classifier is ready for classification.
 *
 * Input Parameters:
 *   ctx       - Classifier context
 *   ax,ay,az  - Acceleration in g units
 *
 * Returned Value:
 *   0 on success, -1 on failure
 *
 ****************************************************************************/

int activity_classify_push(struct activity_classifier *ctx,
                           float ax, float ay, float az);

/****************************************************************************
 * Name: activity_classify_run
 *
 * Description:
 *   Run activity classification on the current window of data.
 *   Uses 1-second sub-windows with majority voting.
 *
 * Input Parameters:
 *   ctx    - Classifier context
 *   result - Output classification result
 *
 * Returned Value:
 *   0 on success, -EAGAIN if insufficient data
 *
 ****************************************************************************/

int activity_classify_run(struct activity_classifier *ctx,
                          struct activity_result *result);

/****************************************************************************
 * Name: activity_classify_single
 *
 * Description:
 *   One-shot classification: classify from a provided buffer of
 *   accelerometer magnitude samples. Does not use the internal state.
 *
 * Input Parameters:
 *   magnitudes - Array of accel magnitudes in g units
 *   count      - Number of samples
 *   imu_rate   - Sample rate in Hz
 *   result     - Output classification result
 *
 * Returned Value:
 *   0 on success, -1 on failure
 *
 ****************************************************************************/

int activity_classify_single(const float *magnitudes, int count,
                             int imu_rate,
                             struct activity_result *result);

/****************************************************************************
 * Name: activity_classify_reset
 *
 * Description:
 *   Reset the classifier state (clear buffer).
 *
 ****************************************************************************/

void activity_classify_reset(struct activity_classifier *ctx);

/****************************************************************************
 * Name: activity_level_to_string
 *
 * Description:
 *   Convert activity level enum to human-readable string.
 *
 ****************************************************************************/

const char *activity_level_to_string(enum activity_level level);

/****************************************************************************
 * Name: posture_type_to_string
 *
 * Description:
 *   Convert posture type enum to human-readable string.
 *
 ****************************************************************************/

const char *posture_type_to_string(enum posture_type posture);

#endif /* __FIRMWARE_LIBS_FEATURES_ACTIVITY_CLASSIFY_H */
