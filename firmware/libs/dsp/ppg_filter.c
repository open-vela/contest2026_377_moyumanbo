/****************************************************************************
 * PPG Bandpass Filter Implementation
 *
 * 4th-order Butterworth bandpass (0.5-5 Hz) via cascaded biquad sections.
 * Coefficients pre-computed for sample_rate = 100 Hz.
 *
 * For other sample rates, coefficients must be recomputed using the
 * bilinear transform. This initial implementation targets 100 Hz.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <string.h>
#include "ppg_filter.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Pre-computed biquad coefficients for 4th-order Butterworth bandpass
 * 0.5-5 Hz @ 100 Hz sample rate
 * Section 1: 0.5-5 Hz low-pass component
 * Section 2: 0.5-5 Hz high-pass component
 *
 * These are placeholder coefficients. For production, compute from:
 *   scipy.signal.butter(4, [0.5, 5.0], btype='band', fs=100, output='sos')
 */

/* Section 1 (low-pass stage) */

#define B0_1  0.00094469f
#define B1_1  0.00188938f
#define B2_1  0.00094469f
#define A1_1  -1.91119707f
#define A2_1  0.91497583f

/* Section 2 (high-pass stage) */

#define B0_2  0.95654322f
#define B1_2  -1.91308644f
#define B2_2  0.95654322f
#define A1_2  -1.91119707f
#define A2_2  0.91497583f

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int ppg_filter_init(float sample_rate, struct ppg_filter_state *state)
{
  if (!state)
    {
      return -1;
    }

  memset(state, 0, sizeof(*state));
  state->initialized = 1;

  /* TODO: If sample_rate != 100, recompute coefficients */

  return 0;
}

int ppg_filter_process(float *data, int count,
                       struct ppg_filter_state *state)
{
  if (!state || !state->initialized || !data)
    {
      return -1;
    }

  /* Apply cascaded biquad sections */

  for (int sec = 0; sec < 2; sec++)
    {
      float b0, b1, b2, a1, a2;

      if (sec == 0)
        {
          b0 = B0_1; b1 = B1_1; b2 = B2_1;
          a1 = A1_1; a2 = A2_1;
        }
      else
        {
          b0 = B0_2; b1 = B1_2; b2 = B2_2;
          a1 = A1_2; a2 = A2_2;
        }

      for (int i = 0; i < count; i++)
        {
          float x = data[i];
          float y = b0 * x +
                    b1 * state->x1[sec] +
                    b2 * state->x2[sec] -
                    a1 * state->y1[sec] -
                    a2 * state->y2[sec];

          state->x2[sec] = state->x1[sec];
          state->x1[sec] = x;
          state->y2[sec] = state->y1[sec];
          state->y1[sec] = y;

          data[i] = y;
        }
    }

  return 0;
}

void ppg_filter_reset(struct ppg_filter_state *state)
{
  if (state)
    {
      memset(state->x1, 0, sizeof(state->x1));
      memset(state->x2, 0, sizeof(state->x2));
      memset(state->y1, 0, sizeof(state->y1));
      memset(state->y2, 0, sizeof(state->y2));
    }
}
