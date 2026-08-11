/****************************************************************************
 * PPG Bandpass Filter
 *
 * 4th-order Butterworth bandpass filter (0.5-5 Hz) for PPG signals.
 * Implemented as cascaded biquad sections for numerical stability.
 ****************************************************************************/

#ifndef __FIRMWARE_LIBS_DSP_PPG_FILTER_H
#define __FIRMWARE_LIBS_DSP_PPG_FILTER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct ppg_filter_state
{
  /* Biquad section states (2 sections for 4th order) */

  float x1[2];   /* Input delay 1 */
  float x2[2];   /* Input delay 2 */
  float y1[2];   /* Output delay 1 */
  float y2[2];   /* Output delay 2 */
  int   initialized;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: ppg_filter_init
 *
 * Description:
 *   Initialize the PPG bandpass filter.
 *
 * Input Parameters:
 *   sample_rate - PPG sample rate in Hz
 *   state       - Filter state to initialize
 *
 * Returned Value:
 *   0 on success
 *
 ****************************************************************************/

int ppg_filter_init(float sample_rate, struct ppg_filter_state *state);

/****************************************************************************
 * Name: ppg_filter_process
 *
 * Description:
 *   Apply bandpass filter to a buffer of PPG samples in-place.
 *
 * Input Parameters:
 *   data  - Array of PPG samples (filtered in-place)
 *   count - Number of samples
 *   state - Filter state (maintains continuity across calls)
 *
 * Returned Value:
 *   0 on success
 *
 ****************************************************************************/

int ppg_filter_process(float *data, int count,
                       struct ppg_filter_state *state);

/****************************************************************************
 * Name: ppg_filter_reset
 *
 * Description:
 *   Reset filter state (e.g., after signal loss).
 *
 ****************************************************************************/

void ppg_filter_reset(struct ppg_filter_state *state);

#endif /* __FIRMWARE_LIBS_DSP_PPG_FILTER_H */
