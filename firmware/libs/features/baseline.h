/****************************************************************************
 * Personal Baseline Management
 *
 * Maintains a 24-hour rolling baseline for physiological signals with
 * three grouping dimensions:
 *
 *   - Time bins:   24 x 1-hour bins (hour-of-day)
 *   - Activity bins: 4 levels (rest / walk / run / other)
 *   - Wear bins:     worn / not-worn
 *
 * Per-bin statistics:
 *   - Mean (exponential moving average, alpha=0.05)
 *   - Standard deviation (EMA)
 *   - Sample count
 *
 * Minimum 3 days of data (72 samples per bin at 1/hour) required
 * for a stable baseline. Baseline data is persisted to Flash
 * (encrypted) for cross-session continuity.
 *
 ****************************************************************************/

#ifndef __FIRMWARE_LIBS_FEATURES_BASELINE_H
#define __FIRMWARE_LIBS_FEATURES_BASELINE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Baseline dimensions */

#define BASELINE_TIME_BINS      24      /* 1-hour bins, 0-23 */
#define BASELINE_ACTIVITY_BINS  4       /* rest / walk / run / other */
#define BASELINE_WEAR_BINS      2       /* not-worn / worn */

/* Total number of bins */

#define BASELINE_TOTAL_BINS     (BASELINE_TIME_BINS * \
                                 BASELINE_ACTIVITY_BINS * \
                                 BASELINE_WEAR_BINS)

/* Number of signals tracked in the baseline */

#define BASELINE_SIGNAL_COUNT   2       /* HR and RMSSD */

/* EMA smoothing factor */

#define BASELINE_EMA_ALPHA      0.05f

/* Minimum days of data for stable baseline */

#define BASELINE_MIN_DAYS       3

/* Minimum samples per bin for stable statistics */

#define BASELINE_MIN_SAMPLES    3

/* Magic number for persisted data validation */

#define BASELINE_FLASH_MAGIC    0x424C5345  /* "BLSE" */

/* Current data format version */

#define BASELINE_FLASH_VERSION  1

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Activity bins for baseline grouping */

enum baseline_activity
{
  BASELINE_ACT_REST  = 0,
  BASELINE_ACT_WALK  = 1,
  BASELINE_ACT_RUN   = 2,
  BASELINE_ACT_OTHER = 3
};

/* Per-bin statistics for one signal */

struct baseline_bin_stat
{
  float    mean;          /* Exponential moving average of signal */
  float    std;           /* Exponential moving average of deviation */
  uint32_t count;         /* Total number of samples incorporated */
  float    m2;            /* Running sum of squared differences (for Welford) */
};

/* Full baseline state for one signal across all bins */

struct baseline_signal
{
  struct baseline_bin_stat bins[BASELINE_TOTAL_BINS];
  bool     is_stable;     /* True if minimum data threshold met */
  uint32_t total_days;    /* Number of distinct days with data */
  uint32_t last_update;   /* Timestamp of last update (seconds) */
};

/* Complete baseline context */

struct baseline_ctx
{
  struct baseline_signal signals[BASELINE_SIGNAL_COUNT];
  uint32_t first_sample_time;   /* Time of first sample ever recorded */
  uint32_t last_persist_time;   /* Time of last Flash write */
  int      initialized;         /* 1 if initialized */
};

/* Persisted baseline header (Flash format) */

struct baseline_flash_header
{
  uint32_t magic;             /* BASELINE_FLASH_MAGIC */
  uint32_t version;           /* BASELINE_FLASH_VERSION */
  uint32_t data_size;         /* Size of following data */
  uint32_t checksum;          /* CRC32 of following data */
  uint32_t timestamp;         /* Time of persistence */
};

/* Signal indices */

#define BASELINE_SIGNAL_HR      0
#define BASELINE_SIGNAL_RMSSD   1

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: baseline_init
 *
 * Description:
 *   Initialize the baseline context. If persisted data exists in Flash,
 *   it is loaded and validated. Otherwise, starts with empty baseline.
 *
 * Input Parameters:
 *   ctx - Baseline context to initialize
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int baseline_init(struct baseline_ctx *ctx);

/****************************************************************************
 * Name: baseline_update
 *
 * Description:
 *   Incorporate a new sample into the baseline using exponential
 *   moving average. The sample is placed into the appropriate bin
 *   based on time-of-day, activity level, and wear state.
 *
 * Input Parameters:
 *   ctx          - Baseline context
 *   signal_index - BASELINE_SIGNAL_HR or BASELINE_SIGNAL_RMSSD
 *   value        - Sample value
 *   hour         - Hour of day (0-23)
 *   activity     - Activity level
 *   is_worn      - true if device is worn
 *   timestamp    - Current timestamp in seconds
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int baseline_update(struct baseline_ctx *ctx,
                    int signal_index,
                    float value,
                    int hour,
                    enum baseline_activity activity,
                    bool is_worn,
                    uint32_t timestamp);

/****************************************************************************
 * Name: baseline_get_stats
 *
 * Description:
 *   Get the baseline statistics for a specific signal in a specific bin.
 *
 * Input Parameters:
 *   ctx          - Baseline context
 *   signal_index - BASELINE_SIGNAL_HR or BASELINE_SIGNAL_RMSSD
 *   hour         - Hour of day (0-23)
 *   activity     - Activity level
 *   is_worn      - Wear state
 *   stats        - Output: bin statistics
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int baseline_get_stats(const struct baseline_ctx *ctx,
                       int signal_index,
                       int hour,
                       enum baseline_activity activity,
                       bool is_worn,
                       struct baseline_bin_stat *stats);

/****************************************************************************
 * Name: baseline_get_deviation
 *
 * Description:
 *   Compute how many standard deviations a value is from the baseline
 *   mean for the given bin. Returns (value - mean) / std.
 *   If std is zero or baseline is not stable, returns 0.
 *
 * Input Parameters:
 *   ctx          - Baseline context
 *   signal_index - BASELINE_SIGNAL_HR or BASELINE_SIGNAL_RMSSD
 *   value        - Current value
 *   hour         - Hour of day (0-23)
 *   activity     - Activity level
 *   is_worn      - Wear state
 *
 * Returned Value:
 *   Deviation in standard deviations, or 0 if baseline not stable
 *
 ****************************************************************************/

float baseline_get_deviation(const struct baseline_ctx *ctx,
                             int signal_index,
                             float value,
                             int hour,
                             enum baseline_activity activity,
                             bool is_worn);

/****************************************************************************
 * Name: baseline_is_stable
 *
 * Description:
 *   Check if the baseline has enough data for reliable comparisons.
 *
 * Input Parameters:
 *   ctx          - Baseline context
 *   signal_index - Signal to check
 *
 * Returned Value:
 *   true if baseline is stable, false otherwise
 *
 ****************************************************************************/

bool baseline_is_stable(const struct baseline_ctx *ctx,
                        int signal_index);

/****************************************************************************
 * Name: baseline_persist
 *
 * Description:
 *   Save the current baseline to Flash storage (encrypted).
 *   Should be called periodically (e.g., every hour) and on shutdown.
 *
 * Input Parameters:
 *   ctx - Baseline context
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int baseline_persist(struct baseline_ctx *ctx);

/****************************************************************************
 * Name: baseline_load
 *
 * Description:
 *   Load baseline from Flash storage. Validates magic, version, and
 *   checksum before restoring state.
 *
 * Input Parameters:
 *   ctx - Baseline context to load into
 *
 * Returned Value:
 *   0 on success, negative errno on failure (no valid data)
 *
 ****************************************************************************/

int baseline_load(struct baseline_ctx *ctx);

/****************************************************************************
 * Name: baseline_reset
 *
 * Description:
 *   Clear all baseline data and start fresh.
 *
 ****************************************************************************/

void baseline_reset(struct baseline_ctx *ctx);

/****************************************************************************
 * Name: baseline_get_bin_index
 *
 * Description:
 *   Compute the linear bin index from time/activity/wear dimensions.
 *   Useful for debugging and diagnostics.
 *
 * Input Parameters:
 *   hour     - Hour of day (0-23)
 *   activity - Activity level
 *   is_worn  - Wear state
 *
 * Returned Value:
 *   Linear bin index [0, BASELINE_TOTAL_BINS)
 *
 ****************************************************************************/

int baseline_get_bin_index(int hour,
                           enum baseline_activity activity,
                           bool is_worn);

/****************************************************************************
 * Name: baseline_activity_from_level
 *
 * Description:
 *   Convert an activity_level (from activity_classify.h) to the
 *   corresponding baseline_activity bin.
 *
 ****************************************************************************/

enum baseline_activity
baseline_activity_from_level(int activity_level);

#endif /* __FIRMWARE_LIBS_FEATURES_BASELINE_H */
