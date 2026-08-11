/****************************************************************************
 * Heart Rate Variability (HRV) Analysis
 *
 * Computes time-domain HRV metrics from beat-to-beat interval sequences:
 *   - HR: Heart rate (beats per minute)
 *   - RMSSD: Root mean square of successive differences
 *   - SDNN: Standard deviation of NN intervals
 *   - pNN50: Percentage of successive intervals differing > 50ms
 ****************************************************************************/

#ifndef __FIRMWARE_LIBS_DSP_HRV_H
#define __FIRMWARE_LIBS_DSP_HRV_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct hrv_result
{
  float hr;          /* Heart rate in BPM */
  float rmssd;       /* RMSSD in milliseconds */
  float sdnn;        /* SDNN in milliseconds */
  float pnn50;       /* pNN50 as percentage (0-100) */
  float mean_rr;     /* Mean RR interval in milliseconds */
  float sdrr;        /* Standard deviation of RR intervals */
  int   num_beats;   /* Number of beats used */
  int   valid;       /* 1 if result is valid */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: hrv_compute
 *
 * Description:
 *   Compute HRV metrics from an array of beat-to-beat intervals.
 *
 * Input Parameters:
 *   ibi_ms  - Array of inter-beat intervals in milliseconds
 *   count   - Number of intervals
 *   result  - Output HRV result
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int hrv_compute(const float *ibi_ms, int count, struct hrv_result *result);

/****************************************************************************
 * Name: hrv_hr_from_ibi
 *
 * Description:
 *   Quick HR calculation from a single IBI value.
 *
 * Input Parameters:
 *   ibi_ms - Inter-beat interval in milliseconds
 *
 * Returned Value:
 *   Heart rate in BPM
 *
 ****************************************************************************/

float hrv_hr_from_ibi(float ibi_ms);

#endif /* __FIRMWARE_LIBS_DSP_HRV_H */
