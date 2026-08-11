/****************************************************************************
 * Adaptive Peak Detection for PPG Signals
 *
 * Detects systolic peaks in bandpass-filtered PPG signal to compute
 * beat-to-beat intervals (IBI) for heart rate and HRV analysis.
 *
 * Algorithm:
 *   1. Compute first derivative of PPG signal
 *   2. Find local maxima where derivative crosses zero (descending)
 *   3. Apply adaptive threshold based on running peak/trough means
 *   4. Enforce minimum distance (300ms) and maximum distance (2000ms)
 *   5. Reject abnormal beats: IBI > 1.5x or < 0.5x of median IBI
 *   6. Output peak timestamps, IBI values, and per-peak quality flags
 *
 * Features:
 *   - Adaptive threshold tracks signal amplitude changes
 *   - Motion-aware: marks peaks during high-motion periods
 *   - Quality flag per peak based on local SNR
 *   - Missing beat interpolation when gaps exceed threshold
 ****************************************************************************/

#ifndef __FIRMWARE_LIBS_DSP_PEAK_DETECT_H
#define __FIRMWARE_LIBS_DSP_PEAK_DETECT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Maximum number of peaks that can be reported per processing call.
 * At 100 Hz sample rate and ~2s worst-case window, 8 peaks is generous.
 */

#define PEAK_DETECT_MAX_PEAKS  8

/* IBI history depth for median filtering and outlier detection.
 * Must be a power of two for efficient median computation.
 */

#define PEAK_DETECT_IBI_HISTORY  16

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Peak detection configuration */

struct peak_detect_config
{
  float sample_rate;          /* PPG sample rate in Hz (e.g., 100.0) */
  float min_ibi_ms;           /* Minimum IBI in ms (300ms = 200 BPM) */
  float max_ibi_ms;           /* Maximum IBI in ms (2000ms = 30 BPM) */
  float threshold_factor;     /* Adaptive threshold factor (default 0.4) */
  float outlier_low;          /* IBI outlier low ratio (default 0.5) */
  float outlier_high;         /* IBI outlier high ratio (default 1.5) */
  float motion_threshold;     /* Motion magnitude above which peaks are
                               * flagged as motion-contaminated (default 0.3) */
  float snr_threshold;        /* Local SNR below which peak quality is
                               * flagged as low (default 3.0) */
};

/* Quality flags for a detected peak */

#define PEAK_QUALITY_VALID       0x01  /* Peak passed all validation checks */
#define PEAK_QUALITY_MOTION      0x02  /* Peak detected during motion period */
#define PEAK_QUALITY_LOW_SNR     0x04  /* Local SNR below threshold */
#define PEAK_QUALITY_INTERP      0x08  /* Peak was interpolated (missing beat) */
#define PEAK_QUALITY_OUTLIER     0x10  /* IBI is an outlier relative to median */

/* Single detected peak */

struct peak_detect_peak
{
  uint32_t sample_index;      /* Sample index of the peak */
  uint64_t timestamp_us;      /* Timestamp in microseconds */
  float    amplitude;         /* Peak amplitude (filtered PPG value) */
  float    ibi_ms;            /* IBI from previous peak in ms (0 if first) */
  float    local_snr;         /* Local signal-to-noise ratio estimate */
  uint8_t  quality;           /* Quality flags (bitmask of PEAK_QUALITY_*) */
};

/* Peak detection output from a single process call */

struct peak_detect_result
{
  struct peak_detect_peak peaks[PEAK_DETECT_MAX_PEAKS];
  int   num_peaks;            /* Number of peaks detected in this call */
  float heart_rate_bpm;       /* Estimated heart rate from recent IBIs */
  float median_ibi_ms;        /* Median IBI from recent history */
};

/* Peak detection internal state */

struct peak_detect_state
{
  /* Configuration (copied at init) */

  struct peak_detect_config config;

  /* Derivative and threshold state */

  float prev_sample;          /* Previous PPG sample for derivative */
  float prev_derivative;      /* Previous derivative for zero-crossing */
  float running_peak_mean;    /* Exponential mean of peak amplitudes */
  float running_trough_mean;  /* Exponential mean of trough amplitudes */
  float adaptive_threshold;   /* Current adaptive threshold */
  int   have_prev_sample;     /* 1 if prev_sample is valid */

  /* Peak candidate tracking */

  float   candidate_amp;      /* Amplitude of current peak candidate */
  int32_t candidate_index;    /* Sample index of current peak candidate */
  int     candidate_valid;    /* 1 if we have a valid candidate */

  /* IBI history for outlier detection and HR estimation */

  float ibi_history[PEAK_DETECT_IBI_HISTORY];
  int   ibi_count;            /* Number of IBIs stored */
  int   ibi_write_idx;        /* Write index into circular buffer */

  /* Timing state */

  int32_t last_peak_index;    /* Sample index of last accepted peak */
  int     have_last_peak;     /* 1 if last_peak_index is valid */
  uint64_t sample_count;      /* Total samples processed (for timestamps) */

  /* Motion state */

  float motion_magnitude;     /* Current motion magnitude (from IMU) */

  /* Initialization flag */

  int initialized;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: peak_detect_init
 *
 * Description:
 *   Initialize the peak detection module with the given configuration.
 *   Unconfigured fields are set to sensible defaults.
 *
 * Input Parameters:
 *   config - Peak detection configuration
 *   state  - State structure to initialize
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int peak_detect_init(const struct peak_detect_config *config,
                     struct peak_detect_state *state);

/****************************************************************************
 * Name: peak_detect_process
 *
 * Description:
 *   Process a buffer of filtered PPG samples and detect peaks.
 *   Call once per buffer of filtered PPG data (e.g., every 250ms at 100Hz).
 *
 * Input Parameters:
 *   ppg_data   - Array of bandpass-filtered PPG samples
 *   count      - Number of samples in ppg_data
 *   motion_mag - Optional: array of per-sample motion magnitudes from IMU
 *                (NULL if unavailable; peaks will still be detected but
 *                without motion-aware quality flags)
 *   state      - Peak detection state
 *   result     - Output: detected peaks and HR estimate
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int peak_detect_process(const float *ppg_data, int count,
                        const float *motion_mag,
                        struct peak_detect_state *state,
                        struct peak_detect_result *result);

/****************************************************************************
 * Name: peak_detect_reset
 *
 * Description:
 *   Reset peak detection state (e.g., after signal loss or sensor
 *   reconnection). Does not clear configuration.
 *
 * Input Parameters:
 *   state - Peak detection state to reset
 *
 ****************************************************************************/

void peak_detect_reset(struct peak_detect_state *state);

/****************************************************************************
 * Name: peak_detect_get_hr
 *
 * Description:
 *   Get the most recent heart rate estimate based on the last few IBIs.
 *
 * Input Parameters:
 *   state - Peak detection state
 *
 * Returned Value:
 *   Heart rate in BPM, or 0.0 if insufficient data
 *
 ****************************************************************************/

float peak_detect_get_hr(const struct peak_detect_state *state);

/****************************************************************************
 * Name: peak_detect_get_median_ibi
 *
 * Description:
 *   Get the median IBI from recent history.
 *
 * Input Parameters:
 *   state - Peak detection state
 *
 * Returned Value:
 *   Median IBI in milliseconds, or 0.0 if insufficient data
 *
 ****************************************************************************/

float peak_detect_get_median_ibi(const struct peak_detect_state *state);

#endif /* __FIRMWARE_LIBS_DSP_PEAK_DETECT_H */
