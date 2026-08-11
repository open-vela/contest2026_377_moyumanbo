/****************************************************************************
 * Motion Artifact Suppression — Implementation
 *
 * LMS adaptive filter and Wiener filter for removing IMU-correlated
 * motion artifacts from PPG signals.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <math.h>
#include <string.h>
#include "motion_artifact.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Default configuration values */

#define DEFAULT_LMS_STEP        0.01f
#define DEFAULT_LMS_STEP_MOTION 0.05f
#define DEFAULT_LMS_STEP_STATIC 0.001f
#define DEFAULT_LMS_LEAKAGE     0.999f
#define DEFAULT_MOTION_VAR_THR  0.01f
#define DEFAULT_MOTION_WINDOW   50
#define DEFAULT_MOTION_HOLD_MS  1000.0f
#define DEFAULT_MAX_RATIO       2.0f

/* Power estimate smoothing coefficient */

#define POWER_EMA_ALPHA         0.01f

/* Minimum power estimate to avoid division by zero */

#define MIN_POWER               1e-10f

/****************************************************************************
 * Private Data — Twiddle factors for FFT (pre-computed)
 *
 * For MA_WIENER_FFT_SIZE = 64, we need 32 twiddle factors.
 * Stored as (cos, sin) pairs for each stage.
 ****************************************************************************/

/* Coefficients for a radix-2 DIT FFT, N=64.
 * These are W_N^k = exp(-j*2*pi*k/N) for k = 0..N/2-1.
 * Pre-computed to avoid runtime trig on embedded targets.
 */

static const float g_fft_cos[32] =
{
   1.00000000f,  0.99518473f,  0.98078528f,  0.95694034f,
   0.92387953f,  0.88192126f,  0.83146961f,  0.77301045f,
   0.70710678f,  0.63439328f,  0.55557023f,  0.47139674f,
   0.38268343f,  0.29028468f,  0.19509032f,  0.09801714f,
   0.00000000f, -0.09801714f, -0.19509032f, -0.29028468f,
  -0.38268343f, -0.47139674f, -0.55557023f, -0.63439328f,
  -0.70710678f, -0.77301045f, -0.83146961f, -0.88192126f,
  -0.92387953f, -0.95694034f, -0.98078528f, -0.99518473f
};

static const float g_fft_sin[32] =
{
   0.00000000f, -0.09801714f, -0.19509032f, -0.29028468f,
  -0.38268343f, -0.47139674f, -0.55557023f, -0.63439328f,
  -0.70710678f, -0.77301045f, -0.83146961f, -0.88192126f,
  -0.92387953f, -0.95694034f, -0.98078528f, -0.99518473f,
  -1.00000000f, -0.99518473f, -0.98078528f, -0.95694034f,
  -0.92387953f, -0.88192126f, -0.83146961f, -0.77301045f,
  -0.70710678f, -0.63439328f, -0.55557023f, -0.47139674f,
  -0.38268343f, -0.29028468f, -0.19509032f, -0.09801714f
};

/****************************************************************************
 * Private Functions — FFT (Radix-2 DIT, N=64)
 ****************************************************************************/

/****************************************************************************
 * Name: fft_radix2_64
 *
 * Description:
 *   In-place radix-2 decimation-in-time FFT for N=64 complex samples.
 *   Input/output in struct ma_complex array of length 64.
 *   Bit-reversal permutation is performed first, then butterfly stages.
 *
 ****************************************************************************/

static void fft_radix2_64(struct ma_complex *x, int inverse)
{
  const int n = 64;
  const int log2n = 6;

  /* Bit-reversal permutation for N=64 */

  static const uint8_t bitrev[64] =
  {
     0, 32, 16, 48,  8, 40, 24, 56,
     4, 36, 20, 52, 12, 44, 28, 60,
     2, 34, 18, 50, 10, 42, 26, 58,
     6, 38, 22, 54, 14, 46, 30, 62,
     1, 33, 17, 49,  9, 41, 25, 57,
     5, 37, 21, 53, 13, 45, 29, 61,
     3, 35, 19, 51, 11, 43, 27, 59,
     7, 39, 23, 55, 15, 47, 31, 63
  };

  /* Apply bit-reversal */

  for (int i = 0; i < n; i++)
    {
      int j = bitrev[i];

      if (j > i)
        {
          struct ma_complex tmp = x[i];
          x[i] = x[j];
          x[j] = tmp;
        }
    }

  /* Butterfly stages */

  for (int stage = 0; stage < log2n; stage++)
    {
      int half_m = 1 << stage;       /* Half the butterfly size */
      int m = half_m << 1;           /* Full butterfly size */

      for (int k = 0; k < n; k += m)
        {
          for (int j = 0; j < half_m; j++)
            {
              /* Twiddle index: j * (N/m) = j * (64 / (2*half_m))
               * For stage s: stride = N / 2^(s+1) = 32 / half_m
               * But we index into the pre-computed table of size 32.
               * twiddle_idx = j * (32 / half_m)
               */

              int twiddle_idx = j * (32 / half_m);
              if (twiddle_idx >= 32)
                {
                  twiddle_idx = 0;
                }

              float wr = g_fft_cos[twiddle_idx];
              float wi = g_fft_sin[twiddle_idx];

              if (inverse)
                {
                  wi = -wi;
                }

              int idx1 = k + j;
              int idx2 = k + j + half_m;

              /* Complex multiply: t = W * x[idx2] */

              float tr = wr * x[idx2].re - wi * x[idx2].im;
              float ti = wr * x[idx2].im + wi * x[idx2].re;

              /* Butterfly */

              x[idx2].re = x[idx1].re - tr;
              x[idx2].im = x[idx1].im - ti;
              x[idx1].re = x[idx1].re + tr;
              x[idx1].im = x[idx1].im + ti;
            }
        }
    }

  /* Scale for inverse FFT */

  if (inverse)
    {
      float scale = 1.0f / n;

      for (int i = 0; i < n; i++)
        {
          x[i].re *= scale;
          x[i].im *= scale;
        }
    }
}

/****************************************************************************
 * Private Functions — Motion Detection
 ****************************************************************************/

/****************************************************************************
 * Name: motion_detect_update
 *
 * Description:
 *   Update motion detection state with a new accelerometer magnitude
 *   sample. Computes running variance over a sliding window.
 *
 ****************************************************************************/

static void motion_detect_update(struct ma_motion_state *ms,
                                 float accel_mag)
{
  /* Store sample in circular buffer */

  ms->accel_buffer[ms->buffer_idx] = accel_mag;
  ms->buffer_idx++;

  int win = ms->window_size;

  if (win > 128)
    {
      win = 128;
    }

  if (ms->buffer_idx >= win)
    {
      ms->buffer_idx = 0;
      ms->buffer_filled = 1;
    }

  /* Compute variance over the filled portion of the buffer */

  int n = ms->buffer_filled ? win : ms->buffer_idx;

  if (n < 2)
    {
      return;
    }

  float sum = 0.0f;
  float sum_sq = 0.0f;

  for (int i = 0; i < n; i++)
    {
      float v = ms->accel_buffer[i];
      sum += v;
      sum_sq += v * v;
    }

  float mean = sum / n;
  float var = (sum_sq / n) - (mean * mean);

  if (var < 0.0f)
    {
      var = 0.0f;
    }

  ms->current_variance = var;

  /* Motion detection with hysteresis */

  if (var > 0.005f)  /* Lower threshold to detect onset */
    {
      ms->motion_detected = 1;
      ms->hold_timer_ms = 0.0f; /* Reset hold timer */
    }
  else if (ms->motion_detected)
    {
      /* Motion was active — use hold timer before declaring stationary */
      /* hold_timer_ms is decremented externally by process() */
    }
}

/****************************************************************************
 * Private Functions — LMS Adaptive Filter
 ****************************************************************************/

/****************************************************************************
 * Name: lms_process_sample
 *
 * Description:
 *   Process one sample through the LMS adaptive filter.
 *   Updates filter weights using the standard LMS algorithm:
 *     y(n) = w^T * x(n)          (filter output)
 *     e(n) = d(n) - y(n)         (error)
 *     w(n+1) = w(n) + 2*mu*e(n)*x(n)  (weight update with leakage)
 *
 * Input Parameters:
 *   lms       - LMS filter state
 *   ppg_sample - Desired signal (raw PPG)
 *   imu_sample - Reference signal (accelerometer magnitude)
 *   step_size  - Current step size (adaptive)
 *
 * Returned Value:
 *   Cleaned PPG sample (error signal)
 *
 ****************************************************************************/

static float lms_process_sample(struct ma_lms_state *lms,
                                float ppg_sample,
                                float imu_sample,
                                float step_size)
{
  /* Insert new reference sample into delay line */

  lms->delay_line[lms->delay_idx] = imu_sample;

  /* Compute filter output: y = sum(w[i] * x[n-i]) */

  float y = 0.0f;

  for (int i = 0; i < MA_LMS_FILTER_ORDER; i++)
    {
      int idx = (lms->delay_idx - i + MA_LMS_FILTER_ORDER) %
                MA_LMS_FILTER_ORDER;
      y += lms->weights[i] * lms->delay_line[idx];
    }

  /* Error signal: e = desired - output = PPG_clean */

  float e = ppg_sample - y;

  /* Update reference power estimate for normalization */

  float ref_power = imu_sample * imu_sample;
  lms->power_estimate = 0.999f * lms->power_estimate + 0.001f * ref_power;

  /* Normalized step size to improve convergence */

  float norm_step = step_size / (lms->power_estimate + MIN_POWER);

  /* Clamp normalized step size for stability */

  if (norm_step > 1.0f)
    {
      norm_step = 1.0f;
    }

  /* Update weights: w(n+1) = leakage * w(n) + 2*mu*e(n)*x(n) */

  float leakage = 0.999f; /* Slight leakage to prevent drift */

  for (int i = 0; i < MA_LMS_FILTER_ORDER; i++)
    {
      int idx = (lms->delay_idx - i + MA_LMS_FILTER_ORDER) %
                MA_LMS_FILTER_ORDER;
      lms->weights[i] = leakage * lms->weights[i] +
                         2.0f * norm_step * e * lms->delay_line[idx];
    }

  /* Advance circular index */

  lms->delay_idx = (lms->delay_idx + 1) % MA_LMS_FILTER_ORDER;

  return e;
}

/****************************************************************************
 * Private Functions — Wiener Filter
 ****************************************************************************/

/****************************************************************************
 * Name: wiener_process_frame
 *
 * Description:
 *   Process one FFT frame through the Wiener filter.
 *   Applies overlap-add reconstruction.
 *
 *   Wiener filter transfer function:
 *     H(f) = 1 - S_nn(f) / S_xx(f)
 *   where S_nn is the noise power spectrum (estimated from IMU) and
 *   S_xx is the noisy input power spectrum.
 *
 ****************************************************************************/

static void wiener_process_frame(struct ma_wiener_state *ws,
                                 const float *ppg_frame,
                                 const float *imu_frame,
                                 float *output)
{
  const int n = MA_WIENER_FFT_SIZE;
  const int half = n / 2;

  /* Prepare PPG spectrum */

  struct ma_complex ppg_fft[64];

  for (int i = 0; i < n; i++)
    {
      ppg_fft[i].re = ppg_frame[i];
      ppg_fft[i].im = 0.0f;
    }

  fft_radix2_64(ppg_fft, 0);

  /* Prepare IMU spectrum */

  struct ma_complex imu_fft[64];

  for (int i = 0; i < n; i++)
    {
      imu_fft[i].re = imu_frame[i];
      imu_fft[i].im = 0.0f;
    }

  fft_radix2_64(imu_fft, 0);

  /* Estimate noise spectrum from IMU.
   * Update the running noise spectrum estimate:
   *   S_nn(f) = alpha * |IMU(f)|^2 + (1-alpha) * S_nn(f)
   */

  for (int i = 0; i <= half; i++)
    {
      float imu_power = imu_fft[i].re * imu_fft[i].re +
                        imu_fft[i].im * imu_fft[i].im;

      ws->noise_spectrum[i].re =
        0.1f * imu_power + 0.9f * ws->noise_spectrum[i].re;
      ws->noise_spectrum[i].im = 0.0f;
    }

  /* Apply Wiener filter:
   *   H(f) = max(0, 1 - S_nn(f) / (|PPG(f)|^2 + eps))
   *   Output(f) = H(f) * PPG(f)
   */

  struct ma_complex out_fft[64];

  for (int i = 0; i <= half; i++)
    {
      float ppg_power = ppg_fft[i].re * ppg_fft[i].re +
                        ppg_fft[i].im * ppg_fft[i].im;

      float h;

      if (ppg_power > MIN_POWER)
        {
          h = 1.0f - ws->noise_spectrum[i].re / (ppg_power + MIN_POWER);
        }
      else
        {
          h = 1.0f; /* No noise estimate, pass through */
        }

      /* Clamp H to [0, 1] to prevent amplification of noise */

      if (h < 0.0f)
        {
          h = 0.0f;
        }

      if (h > 1.0f)
        {
          h = 1.0f;
        }

      out_fft[i].re = h * ppg_fft[i].re;
      out_fft[i].im = h * ppg_fft[i].im;

      /* Mirror for negative frequencies */

      if (i > 0 && i < half)
        {
          out_fft[n - i].re = out_fft[i].re;
          out_fft[n - i].im = -out_fft[i].im;
        }
    }

  out_fft[0].im = 0.0f;
  out_fft[half].im = 0.0f;

  /* Inverse FFT */

  fft_radix2_64(out_fft, 1);

  /* Overlap-add: add the second half of current frame to output,
   * and carry the first half in the overlap buffer.
   *
   * For the first frame, output the second half directly.
   * For subsequent frames, output: overlap[0..31] + current[0..31],
   * then store current[32..63] as the new overlap.
   */

  if (ws->frame_count > 0)
    {
      /* Output: previous overlap + first half of current frame */

      for (int i = 0; i < MA_WIENER_OVERLAP; i++)
        {
          output[i] = ws->output_overlap[i] + out_fft[i].re;
        }
    }
  else
    {
      /* First frame: output first half directly (no previous overlap) */

      for (int i = 0; i < MA_WIENER_OVERLAP; i++)
        {
          output[i] = out_fft[i].re;
        }
    }

  /* Store second half as overlap for next frame */

  for (int i = 0; i < MA_WIENER_OVERLAP; i++)
    {
      ws->output_overlap[i] = out_fft[i + MA_WIENER_OVERLAP].re;
    }

  ws->frame_count++;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int motion_artifact_init(const struct motion_artifact_config *config,
                         struct motion_artifact_state *state)
{
  if (!state)
    {
      return -1;
    }

  memset(state, 0, sizeof(*state));

  /* Apply configuration with defaults */

  if (config)
    {
      state->config = *config;
    }
  else
    {
      state->config.mode = MA_MODE_LMS;
    }

  if (state->config.sample_rate <= 0.0f)
    {
      state->config.sample_rate = 100.0f;
    }

  if (state->config.lms_step_size <= 0.0f)
    {
      state->config.lms_step_size = DEFAULT_LMS_STEP;
    }

  if (state->config.lms_step_motion <= 0.0f)
    {
      state->config.lms_step_motion = DEFAULT_LMS_STEP_MOTION;
    }

  if (state->config.lms_step_static <= 0.0f)
    {
      state->config.lms_step_static = DEFAULT_LMS_STEP_STATIC;
    }

  if (state->config.lms_leakage <= 0.0f)
    {
      state->config.lms_leakage = DEFAULT_LMS_LEAKAGE;
    }

  if (state->config.motion_var_threshold <= 0.0f)
    {
      state->config.motion_var_threshold = DEFAULT_MOTION_VAR_THR;
    }

  if (state->config.motion_window <= 0)
    {
      state->config.motion_window = DEFAULT_MOTION_WINDOW;
    }

  if (state->config.motion_hold_time_ms <= 0.0f)
    {
      state->config.motion_hold_time_ms = DEFAULT_MOTION_HOLD_MS;
    }

  if (state->config.max_output_ratio <= 0.0f)
    {
      state->config.max_output_ratio = DEFAULT_MAX_RATIO;
    }

  /* Initialize motion detection state */

  state->motion.window_size = state->config.motion_window;
  if (state->motion.window_size > 128)
    {
      state->motion.window_size = 128;
    }

  /* Initialize filter state for the selected mode */

  if (state->config.mode == MA_MODE_LMS)
    {
      memset(&state->filter.lms, 0, sizeof(state->filter.lms));
      state->filter.lms.step_size = state->config.lms_step_static;
    }
  else
    {
      memset(&state->filter.wiener, 0, sizeof(state->filter.wiener));
    }

  state->initialized = 1;

  return 0;
}

int motion_artifact_process(const float *ppg_in,
                            const float *imu_accel,
                            int count,
                            float *ppg_out,
                            struct motion_artifact_state *state)
{
  if (!state || !state->initialized || !ppg_in || !ppg_out || count < 1)
    {
      return -1;
    }

  /* ----------------------------------------------------------------
   * Graceful degradation: if IMU is unavailable, pass through.
   * ---------------------------------------------------------------- */

  if (!imu_accel)
    {
      if (ppg_in != ppg_out)
        {
          memcpy(ppg_out, ppg_in, count * sizeof(float));
        }

      state->imu_unavailable = 1;
      return 0;
    }

  state->imu_unavailable = 0;

  /* ----------------------------------------------------------------
   * LMS mode: sample-by-sample adaptive filtering
   * ---------------------------------------------------------------- */

  if (state->config.mode == MA_MODE_LMS)
    {
      struct ma_lms_state *lms = &state->filter.lms;

      for (int i = 0; i < count; i++)
        {
          /* Update motion detection */

          motion_detect_update(&state->motion, imu_accel[i]);

          /* Adapt step size based on motion state */

          if (state->motion.motion_detected)
            {
              /* Smoothly ramp up step size during motion */

              lms->step_size = 0.9f * lms->step_size +
                               0.1f * state->config.lms_step_motion;
            }
          else
            {
              /* Smoothly ramp down step size when static */

              lms->step_size = 0.99f * lms->step_size +
                               0.01f * state->config.lms_step_static;
            }

          /* Process one sample */

          float cleaned = lms_process_sample(lms, ppg_in[i],
                                             imu_accel[i],
                                             lms->step_size);

          /* Safety clamp: prevent over-correction.
           * Limit output magnitude to max_ratio * input magnitude.
           */

          float max_amp = fabsf(ppg_in[i]) * state->config.max_output_ratio;

          if (max_amp < 0.001f)
            {
              max_amp = 0.001f;
            }

          if (cleaned > max_amp)
            {
              cleaned = max_amp;
            }
          else if (cleaned < -max_amp)
            {
              cleaned = -max_amp;
            }

          ppg_out[i] = cleaned;

          /* Update power estimates for suppression measurement */

          state->input_power = (1.0f - POWER_EMA_ALPHA) *
                               state->input_power +
                               POWER_EMA_ALPHA * ppg_in[i] * ppg_in[i];

          float diff = ppg_in[i] - cleaned;
          state->output_power = (1.0f - POWER_EMA_ALPHA) *
                                state->output_power +
                                POWER_EMA_ALPHA * diff * diff;
        }

      state->samples_processed += count;

      /* Compute suppression in dB */

      if (state->input_power > MIN_POWER)
        {
          state->suppression_db = 10.0f * log10f(
            state->output_power / state->input_power + MIN_POWER);
        }

      /* Handle motion hold timer */

      if (!state->motion.motion_detected &&
          state->motion.hold_timer_ms > 0.0f)
        {
          float elapsed_ms = (float)count * 1000.0f /
                             state->config.sample_rate;
          state->motion.hold_timer_ms -= elapsed_ms;

          if (state->motion.hold_timer_ms <= 0.0f)
            {
              state->motion.motion_detected = 0;
            }
        }
    }

  /* ----------------------------------------------------------------
   * Wiener mode: frame-based frequency-domain filtering
   * ---------------------------------------------------------------- */

  else if (state->config.mode == MA_MODE_WIENER)
    {
      struct ma_wiener_state *ws = &state->filter.wiener;
      int processed = 0;

      /* Process in frames of MA_WIENER_FFT_SIZE with 50% overlap */

      while (processed + MA_WIENER_FFT_SIZE <= count)
        {
          /* Update motion detection for this frame */

          for (int i = 0; i < MA_WIENER_FFT_SIZE; i++)
            {
              motion_detect_update(&state->motion,
                                   imu_accel[processed + i]);
            }

          /* Apply Hanning window to input frame */

          float ppg_windowed[MA_WIENER_FFT_SIZE];
          float imu_windowed[MA_WIENER_FFT_SIZE];

          for (int i = 0; i < MA_WIENER_FFT_SIZE; i++)
            {
              /* Hanning window: 0.5 * (1 - cos(2*pi*n/(N-1))) */

              float w = 0.5f * (1.0f - cosf(
                6.2831853f * i / (MA_WIENER_FFT_SIZE - 1)));

              ppg_windowed[i] = ppg_in[processed + i] * w;
              imu_windowed[i] = imu_accel[processed + i] * w;
            }

          /* Process frame */

          float frame_out[MA_WIENER_OVERLAP];

          wiener_process_frame(ws, ppg_windowed, imu_windowed, frame_out);

          /* Copy overlap-added output */

          if (ws->frame_count > 1 || processed == 0)
            {
              for (int i = 0; i < MA_WIENER_OVERLAP; i++)
                {
                  int out_idx = processed + i;

                  if (out_idx < count)
                    {
                      ppg_out[out_idx] = frame_out[i];
                    }
                }
            }

          processed += MA_WIENER_OVERLAP;
        }

      /* Handle remaining samples (fewer than one frame) by passing through */

      while (processed < count)
        {
          ppg_out[processed] = ppg_in[processed];
          processed++;
        }

      state->samples_processed += count;
    }

  return 0;
}

void motion_artifact_reset(struct motion_artifact_state *state)
{
  if (!state)
    {
      return;
    }

  /* Preserve configuration */

  struct motion_artifact_config saved = state->config;
  int was_init = state->initialized;

  memset(state, 0, sizeof(*state));

  state->config = saved;
  state->initialized = was_init;

  /* Re-initialize motion detection window size */

  state->motion.window_size = state->config.motion_window;
  if (state->motion.window_size > 128)
    {
      state->motion.window_size = 128;
    }

  /* Reset filter state */

  if (state->config.mode == MA_MODE_LMS)
    {
      state->filter.lms.step_size = state->config.lms_step_static;
    }
}

int motion_artifact_is_motion(const struct motion_artifact_state *state)
{
  if (!state)
    {
      return -1;
    }

  return state->motion.motion_detected ? 1 : 0;
}

float motion_artifact_get_suppression(
    const struct motion_artifact_state *state)
{
  if (!state || state->samples_processed < 1)
    {
      return 0.0f;
    }

  /* Return magnitude of suppression (positive value) */

  if (state->suppression_db < 0.0f)
    {
      return -state->suppression_db;
    }

  return 0.0f;
}

int motion_artifact_set_mode(enum motion_artifact_mode mode,
                             struct motion_artifact_state *state)
{
  if (!state)
    {
      return -1;
    }

  state->config.mode = mode;

  /* Reset filter state for the new mode */

  memset(&state->filter, 0, sizeof(state->filter));

  if (mode == MA_MODE_LMS)
    {
      state->filter.lms.step_size = state->config.lms_step_static;
    }

  state->samples_processed = 0;
  state->input_power = 0.0f;
  state->output_power = 0.0f;
  state->suppression_db = 0.0f;

  return 0;
}
