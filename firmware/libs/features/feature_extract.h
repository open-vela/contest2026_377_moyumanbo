/****************************************************************************
 * Feature Extraction Module
 *
 * Extracts a 15-element feature vector from the latest 60-second sensor
 * window for emotion arousal inference:
 *
 *   Index  Signal              Unit     Source
 *   ─────────────────────────────────────────────────
 *   [0]    HR                  bpm      PPG peak detection
 *   [1]    HR_slope            bpm/min  Linear regression on IBI
 *   [2]    RMSSD               ms       HRV time-domain
 *   [3]    SDNN                ms       HRV time-domain
 *   [4]    IBI_dispersion      %        Coefficient of variation
 *   [5]    activity_intensity  0-3      IMU classifier
 *   [6]    posture             0-3      IMU orientation
 *   [7]    SCL_baseline        uS       EDA tonic level
 *   [8]    SCR_count           /min     EDA phasic events
 *   [9]    SCR_amplitude       uS       Mean SCR amplitude
 *   [10]   temp_slope          C/min    Skin temperature rate
 *   [11]   ppg_sqi             0-1      Signal quality index
 *   [12]   hr_deviation        sigma    HR vs personal baseline
 *   [13]   rmssd_deviation     sigma    RMSSD vs personal baseline
 *   [14]   time_of_day         0-1      Normalized time
 *
 * Called every 5 seconds by the inference task on a 60-second sliding
 * window of sensor data.
 *
 ****************************************************************************/

#ifndef __FIRMWARE_LIBS_FEATURES_FEATURE_EXTRACT_H
#define __FIRMWARE_LIBS_FEATURES_FEATURE_EXTRACT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>

#include "baseline.h"
#include "activity_classify.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Feature vector size */

#define FEATURE_COUNT           15

/* Sensor window parameters */

#define FEATURE_WINDOW_SEC      60      /* 60-second analysis window */
#define FEATURE_PPG_RATE        100     /* PPG sample rate (Hz) */
#define FEATURE_IMU_RATE        100     /* IMU sample rate (Hz) */
#define FEATURE_EDA_RATE        32      /* EDA sample rate (Hz) */
#define FEATURE_TEMP_RATE       1       /* Temperature sample rate (Hz) */

/* Maximum samples in the window */

#define FEATURE_PPG_MAX_SAMPLES  (FEATURE_WINDOW_SEC * FEATURE_PPG_RATE)
#define FEATURE_IMU_MAX_SAMPLES  (FEATURE_WINDOW_SEC * FEATURE_IMU_RATE)
#define FEATURE_EDA_MAX_SAMPLES  (FEATURE_WINDOW_SEC * FEATURE_EDA_RATE)
#define FEATURE_TEMP_MAX_SAMPLES (FEATURE_WINDOW_SEC * FEATURE_TEMP_RATE)

/* Maximum IBI (inter-beat interval) count in 60 seconds.
 * At 40 bpm minimum, 60s yields at most 40 beats. */

#define FEATURE_IBI_MAX_COUNT   48

/* SCR detection thresholds */

#define FEATURE_SCR_THRESHOLD_UV  0.05f  /* Minimum SCR amplitude (uS) */
#define FEATURE_SCR_MIN_DURATION  500    /* Minimum SCR duration (ms) */

/* Feature indices */

#define FEAT_IDX_HR             0
#define FEAT_IDX_HR_SLOPE       1
#define FEAT_IDX_RMSSD          2
#define FEAT_IDX_SDNN           3
#define FEAT_IDX_IBI_DISP       4
#define FEAT_IDX_ACTIVITY       5
#define FEAT_IDX_POSTURE        6
#define FEAT_IDX_SCL_BASELINE   7
#define FEAT_IDX_SCR_COUNT      8
#define FEAT_IDX_SCR_AMP        9
#define FEAT_IDX_TEMP_SLOPE     10
#define FEAT_IDX_PPG_SQI        11
#define FEAT_IDX_HR_DEV         12
#define FEAT_IDX_RMSSD_DEV      13
#define FEAT_IDX_TIME_OF_DAY    14

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* PPG peak data (output of DSP peak detection) */

struct feature_ppg_peak
{
  uint32_t timestamp_ms;       /* Peak timestamp in milliseconds */
  float    ibi_ms;             /* Inter-beat interval from previous peak */
  float    amplitude;          /* Peak amplitude (for quality assessment) */
  int      is_valid;           /* 1 if beat passed morphology check */
};

/* EDA event data (output of SCR detector) */

struct feature_eda_event
{
  uint32_t timestamp_ms;       /* Event onset timestamp */
  float    amplitude;          /* SCR amplitude in uS */
  float    rise_time_ms;       /* Rise time in milliseconds */
  int      is_valid;           /* 1 if event meets criteria */
};

/* Temperature sample */

struct feature_temp_sample
{
  uint32_t timestamp_ms;       /* Sample timestamp */
  float    temperature_c;      /* Temperature in degrees Celsius */
};

/* Raw sensor window input for feature extraction */

struct feature_sensor_window
{
  /* PPG data */

  const struct feature_ppg_peak *ibi_array;  /* Array of IBI data */
  int      ibi_count;              /* Number of IBI entries */
  float    ppg_sqi;                /* PPG signal quality index (0-1) */

  /* IMU data */

  const float *accel_x;            /* Accel X in g units */
  const float *accel_y;            /* Accel Y in g units */
  const float *accel_z;            /* Accel Z in g units */
  int      accel_count;            /* Number of accel samples */
  int      imu_rate_hz;            /* IMU sample rate */

  /* EDA data */

  float    eda_scl_current;        /* Current SCL level (uS) */
  float    eda_scl_baseline;       /* SCL tonic baseline (uS) */
  const struct feature_eda_event *scr_events;  /* SCR event array */
  int      scr_event_count;        /* Number of SCR events */

  /* Temperature data */

  const struct feature_temp_sample *temp_array;  /* Temp samples */
  int      temp_count;             /* Number of temperature samples */

  /* Context */

  int      hour_of_day;            /* Current hour (0-23) */
  int      minute_of_hour;         /* Current minute (0-59) */
  int      is_worn;                /* 1 if device is worn */
};

/* Complete feature extraction result */

struct feature_result
{
  float    features[FEATURE_COUNT]; /* The 15-element feature vector */
  uint32_t timestamp_ms;            /* Extraction timestamp */
  int      valid_mask;              /* Bitmask: bit i = 1 if feature i valid */
  int      all_valid;               /* 1 if all features valid */
};

/* Feature extraction context (maintains state between calls) */

struct feature_ctx
{
  struct baseline_ctx  baseline;    /* Personal baseline */
  struct activity_classifier act_cls; /* Activity classifier */

  /* Previous window values for slope computation */

  float    prev_hr;
  float    prev_rmssd;
  uint32_t prev_timestamp_ms;
  int      has_prev;

  /* Temperature history for slope computation */

  float    temp_history[4];         /* Last 4 temperature readings */
  uint32_t temp_timestamps[4];
  int      temp_hist_idx;
  int      temp_hist_count;

  int      initialized;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: feature_extract_init
 *
 * Description:
 *   Initialize the feature extraction context, including baseline
 *   and activity classifier subsystems.
 *
 * Input Parameters:
 *   ctx - Feature extraction context to initialize
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int feature_extract_init(struct feature_ctx *ctx);

/****************************************************************************
 * Name: feature_extract_run
 *
 * Description:
 *   Extract all 15 features from the given sensor window.
 *   This is the main entry point called every 5 seconds by the
 *   inference task.
 *
 * Input Parameters:
 *   ctx    - Feature extraction context
 *   window - Sensor data for the 60-second window
 *   result - Output feature vector and metadata
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int feature_extract_run(struct feature_ctx *ctx,
                        const struct feature_sensor_window *window,
                        struct feature_result *result);

/****************************************************************************
 * Name: feature_extract_update_temp
 *
 * Description:
 *   Feed a new temperature reading into the feature extractor.
 *   Called at the temperature sensor rate (1 Hz) to maintain
 *   the history needed for slope computation.
 *
 * Input Parameters:
 *   ctx          - Feature extraction context
 *   temperature  - Temperature in degrees Celsius
 *   timestamp_ms - Timestamp in milliseconds
 *
 ****************************************************************************/

void feature_extract_update_temp(struct feature_ctx *ctx,
                                 float temperature,
                                 uint32_t timestamp_ms);

/****************************************************************************
 * Name: feature_extract_reset
 *
 * Description:
 *   Reset the feature extraction context, clearing all history.
 *
 ****************************************************************************/

void feature_extract_reset(struct feature_ctx *ctx);

/****************************************************************************
 * Name: feature_name
 *
 * Description:
 *   Get the human-readable name of a feature by index.
 *
 * Input Parameters:
 *   index - Feature index (0-14)
 *
 * Returned Value:
 *   Feature name string, or "unknown" if index is out of range
 *
 ****************************************************************************/

const char *feature_name(int index);

/****************************************************************************
 * Name: feature_unit
 *
 * Description:
 *   Get the unit string of a feature by index.
 *
 ****************************************************************************/

const char *feature_unit(int index);

#endif /* __FIRMWARE_LIBS_FEATURES_FEATURE_EXTRACT_H */
