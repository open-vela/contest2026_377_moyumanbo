/****************************************************************************
 * Motion Artifact Suppression for PPG Signals
 *
 * Uses IMU (accelerometer) data as a reference to remove motion-induced
 * artifacts from PPG signals via Adaptive Noise Cancellation (ANC).
 *
 * Two algorithms are provided:
 *
 *   1. LMS Adaptive Filter (default, time-domain)
 *      - PPG_clean = PPG_raw - w * IMU_reference
 *      - 16-tap FIR filter with LMS weight update
 *      - Adaptive step size: increases during motion, decreases when static
 *      - Low latency (< 1 sample period)
 *
 *   2. Wiener Filter (frequency-domain alternative)
 *      - Estimates noise spectrum from IMU reference
 *      - Applies frequency-domain Wiener filter to PPG
 *      - Better for periodic motion (walking, running)
 *      - Higher latency (one FFT frame)
 *
 * Features:
 *   - Auto-adapts to different motion types
 *   - Preserves cardiac signal morphology
 *   - Graceful degradation: pass-through when IMU unavailable
 *   - Motion detection via accelerometer variance
 ****************************************************************************/

#ifndef __FIRMWARE_LIBS_DSP_MOTION_ARTIFACT_H
#define __FIRMWARE_LIBS_DSP_MOTION_ARTIFACT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* LMS filter order (number of taps) */

#define MA_LMS_FILTER_ORDER    16

/* Wiener filter FFT size (must be power of two, >= 2 * filter order) */

#define MA_WIENER_FFT_SIZE     64

/* Wiener filter overlap size (50% overlap-add) */

#define MA_WIENER_OVERLAP      32

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Motion artifact suppression mode */

enum motion_artifact_mode
{
  MA_MODE_LMS = 0,            /* LMS adaptive filter (time-domain) */
  MA_MODE_WIENER              /* Wiener filter (frequency-domain) */
};

/* Motion artifact suppression configuration */

struct motion_artifact_config
{
  float sample_rate;          /* Sample rate in Hz (e.g., 100.0) */
  enum motion_artifact_mode mode;  /* Algorithm selection */

  /* LMS parameters */

  float lms_step_size;        /* Base step size mu (default 0.01) */
  float lms_step_motion;      /* Step size during motion (default 0.05) */
  float lms_step_static;      /* Step size when static (default 0.001) */
  float lms_leakage;          /* Leakage factor for weight decay (default
                               * 0.999 to prevent drift, 1.0 = no leakage) */

  /* Motion detection parameters */

  float motion_var_threshold; /* Accel variance threshold for motion
                               * detection (default 0.01 g^2) */
  int   motion_window;        /* Window size in samples for variance
                               * estimation (default 50 = 500ms at 100Hz) */
  float motion_hold_time_ms;  /* How long to keep filtering after motion
                               * stops (default 1000ms) */

  /* Safety parameters */

  float max_output_ratio;     /* Max ratio of |output| / |input| to prevent
                               * over-correction (default 2.0) */
};

/* LMS filter internal state */

struct ma_lms_state
{
  float weights[MA_LMS_FILTER_ORDER];  /* Adaptive filter weights */
  float delay_line[MA_LMS_FILTER_ORDER]; /* Reference signal delay line */
  int   delay_idx;                      /* Circular write index */
  float step_size;                      /* Current adaptive step size */
  float power_estimate;                 /* Running power of reference signal */
};

/* Complex number for frequency-domain processing */

struct ma_complex
{
  float re;
  float im;
};

/* Wiener filter internal state */

struct ma_wiener_state
{
  struct ma_complex noise_spectrum[MA_WIENER_FFT_SIZE / 2 + 1];
  struct ma_complex ppg_spectrum[MA_WIENER_FFT_SIZE / 2 + 1];
  float input_overlap[MA_WIENER_OVERLAP];  /* Overlap buffer for OLA */
  float output_overlap[MA_WIENER_OVERLAP];
  int   frame_count;                       /* Frames collected so far */
  int   spectrum_ready;                    /* 1 if noise estimate valid */
};

/* Motion detection state */

struct ma_motion_state
{
  float accel_buffer[128];    /* Circular buffer for variance estimation */
  int   buffer_idx;
  int   buffer_filled;
  int   window_size;
  float current_variance;
  int   motion_detected;
  float hold_timer_ms;        /* Countdown for motion hold */
};

/* Top-level motion artifact suppression state */

struct motion_artifact_state
{
  struct motion_artifact_config config;

  /* Algorithm state (union to save RAM) */

  union
  {
    struct ma_lms_state     lms;
    struct ma_wiener_state  wiener;
  } filter;

  /* Motion detection (always active) */

  struct ma_motion_state motion;

  /* Statistics */

  float input_power;          /* Running input power estimate */
  float output_power;         /* Running output power estimate */
  float suppression_db;       /* Estimated suppression in dB */
  int   samples_processed;    /* Total samples processed */
  int   imu_unavailable;      /* 1 if IMU data was NULL on last call */

  int   initialized;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: motion_artifact_init
 *
 * Description:
 *   Initialize the motion artifact suppression module.
 *
 * Input Parameters:
 *   config - Configuration (NULL for defaults)
 *   state  - State structure to initialize
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int motion_artifact_init(const struct motion_artifact_config *config,
                         struct motion_artifact_state *state);

/****************************************************************************
 * Name: motion_artifact_process
 *
 * Description:
 *   Process a buffer of PPG samples using IMU data as reference.
 *   Applies the configured filter algorithm and outputs cleaned PPG.
 *
 *   If imu_data is NULL, the PPG buffer is passed through unmodified
 *   (graceful degradation).
 *
 * Input Parameters:
 *   ppg_in    - Input PPG samples (raw, unfiltered)
 *   imu_accel - Accelerometer magnitude samples (sqrt(ax^2+ay^2+az^2)),
 *               or NULL if IMU is unavailable. Must be the same sample
 *               rate as PPG, or downsampled to match.
 *   count     - Number of samples
 *   ppg_out   - Output cleaned PPG samples (may alias ppg_in for
 *               in-place processing)
 *   state     - Motion artifact suppression state
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int motion_artifact_process(const float *ppg_in,
                            const float *imu_accel,
                            int count,
                            float *ppg_out,
                            struct motion_artifact_state *state);

/****************************************************************************
 * Name: motion_artifact_reset
 *
 * Description:
 *   Reset filter state (e.g., after signal loss). Configuration
 *   is preserved.
 *
 * Input Parameters:
 *   state - State structure to reset
 *
 ****************************************************************************/

void motion_artifact_reset(struct motion_artifact_state *state);

/****************************************************************************
 * Name: motion_artifact_is_motion
 *
 * Description:
 *   Check if significant motion is currently detected.
 *
 * Input Parameters:
 *   state - Motion artifact state
 *
 * Returned Value:
 *   1 if motion detected, 0 if stationary, negative on error
 *
 ****************************************************************************/

int motion_artifact_is_motion(const struct motion_artifact_state *state);

/****************************************************************************
 * Name: motion_artifact_get_suppression
 *
 * Description:
 *   Get the estimated noise suppression in decibels.
 *
 * Input Parameters:
 *   state - Motion artifact state
 *
 * Returned Value:
 *   Suppression in dB (positive = suppression active), or 0.0 if
 *   no suppression applied.
 *
 ****************************************************************************/

float motion_artifact_get_suppression(
    const struct motion_artifact_state *state);

/****************************************************************************
 * Name: motion_artifact_set_mode
 *
 * Description:
 *   Switch between LMS and Wiener filter modes at runtime.
 *   Resets the filter state for the new mode.
 *
 * Input Parameters:
 *   mode  - New filter mode
 *   state - Motion artifact state
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int motion_artifact_set_mode(enum motion_artifact_mode mode,
                             struct motion_artifact_state *state);

#endif /* __FIRMWARE_LIBS_DSP_MOTION_ARTIFACT_H */
