/****************************************************************************
 * Heart Rate Variability (HRV) Analysis Implementation
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <math.h>
#include <string.h>
#include "hrv.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int hrv_compute(const float *ibi_ms, int count, struct hrv_result *result)
{
  if (!ibi_ms || !result || count < 2)
    {
      return -1;
    }

  memset(result, 0, sizeof(*result));
  result->num_beats = count;

  /* Mean RR interval */

  float sum = 0.0f;
  for (int i = 0; i < count; i++)
    {
      sum += ibi_ms[i];
    }

  result->mean_rr = sum / count;

  /* Heart rate from mean IBI */

  result->hr = hrv_hr_from_ibi(result->mean_rr);

  /* SDNN: standard deviation of NN intervals */

  float sum_sq = 0.0f;
  for (int i = 0; i < count; i++)
    {
      float diff = ibi_ms[i] - result->mean_rr;
      sum_sq += diff * diff;
    }

  result->sdnn = sqrtf(sum_sq / count);

  /* RMSSD: root mean square of successive differences */

  float sum_diff_sq = 0.0f;
  int nn50_count = 0;

  for (int i = 1; i < count; i++)
    {
      float diff = ibi_ms[i] - ibi_ms[i - 1];
      sum_diff_sq += diff * diff;

      if (fabsf(diff) > 50.0f)
        {
          nn50_count++;
        }
    }

  result->rmssd = sqrtf(sum_diff_sq / (count - 1));

  /* pNN50: percentage of successive intervals differing > 50ms */

  result->pnn50 = (100.0f * nn50_count) / (count - 1);

  /* SDRR (same as SDNN for NN intervals) */

  result->sdrr = result->sdnn;

  result->valid = 1;

  return 0;
}

float hrv_hr_from_ibi(float ibi_ms)
{
  if (ibi_ms <= 0)
    {
      return 0.0f;
    }

  return 60000.0f / ibi_ms;
}
