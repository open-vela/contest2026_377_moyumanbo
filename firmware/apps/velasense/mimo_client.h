/****************************************************************************
 * VelaSense Mimo Token Plan Client
 *
 * HTTP client for the Mimo cloud API.  Runs on the PHONE gateway, not on
 * the wearable device.  The device prepares a privacy-checked payload; this
 * module serialises it to JSON, posts it over HTTPS, and parses the reply.
 *
 * Endpoints:
 *   POST /api/v1/emotion-diary   - daily emotion diary generation
 *   POST /api/v1/weekly-report   - weekly summary report
 *   POST /api/v1/breath-guide    - personalised breathing exercise
 *   POST /api/v1/explain-alert   - natural-language event explanation
 *
 * All uploads go through privacy_policy_validate() first.
 * On network failure the caller falls back to mimo_templates.
 ****************************************************************************/

#ifndef __FIRMWARE_APPS_VELASENSE_MIMO_CLIENT_H
#define __FIRMWARE_APPS_VELASENSE_MIMO_CLIENT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MIMO_API_BASE_URL       "https://api.mimoai.cn"
#define MIMO_API_VERSION        "v1"
#define MIMO_DEVICE_ID          "velasense-377"
#define MIMO_DATA_VERSION       "1.0"

#define MIMO_MAX_EVENTS         32
#define MIMO_MAX_RECOMMEND      8
#define MIMO_MAX_SUGGESTIONS    4
#define MIMO_MAX_TEXT_LEN       1024
#define MIMO_MAX_REASON_CODES   5
#define MIMO_MAX_DAILY_TOKENS   50

#define MIMO_HTTP_TIMEOUT_MS    10000
#define MIMO_RETRY_COUNT        2
#define MIMO_RETRY_DELAY_MS     2000

#define MIMO_EVENT_LABEL_STRESS       "stress"
#define MIMO_EVENT_LABEL_EXCITEMENT   "excitement"
#define MIMO_EVENT_LABEL_NERVOUS      "nervous"
#define MIMO_EVENT_LABEL_SURPRISE     "surprise"
#define MIMO_EVENT_LABEL_OTHER        "other"

#define MIMO_HR_CONTEXT_ELEVATED      "elevated"
#define MIMO_HR_CONTEXT_NORMAL        "normal"
#define MIMO_HR_CONTEXT_LOW           "low"

#define MIMO_ACTIVITY_SITTING         "sitting"
#define MIMO_ACTIVITY_WALKING         "walking"
#define MIMO_ACTIVITY_RUNNING         "running"
#define MIMO_ACTIVITY_SLEEPING        "sleeping"
#define MIMO_ACTIVITY_UNKNOWN         "unknown"

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Return codes for all Mimo client operations */

enum mimo_status
{
  MIMO_OK = 0,              /* Success */
  MIMO_ERR_NETWORK,         /* Network unreachable / timeout */
  MIMO_ERR_HTTP,            /* Non-2xx HTTP status */
  MIMO_ERR_PARSE,           /* JSON parse failure */
  MIMO_ERR_PRIVACY,         /* Privacy policy rejected the payload */
  MIMO_ERR_TOKEN_EXHAUSTED, /* Daily token budget exhausted */
  MIMO_ERR_AUTH,            /* Authentication failure */
  MIMO_ERR_INVALID_PARAM,   /* Bad input parameter */
  MIMO_ERR_BUFFER_TOO_SMALL /* Output buffer too small */
};

/* Reason codes that explain an arousal event (string tags) */

struct mimo_reason_code
{
  char code[32]; /* e.g. "hr_rise", "hrv_drop", "eda_spike" */
};

/* Single emotion event to upload (privacy-sanitised) */

struct mimo_event
{
  char    time[40];         /* ISO 8601 with timezone */
  char    label[24];        /* Event label string */
  int     confidence;       /* 0-100 integer */
  char    activity[24];     /* Activity context */
  char    hr_context[16];   /* "elevated" / "normal" / "low" */
  int     reason_count;     /* Number of reason codes */
  struct  mimo_reason_code reason_codes[MIMO_MAX_REASON_CODES];
};

/* Daily summary statistics */

struct mimo_daily_summary
{
  int     total_events;     /* Number of events today */
  char    dominant_label[24]; /* Most frequent label */
  int     avg_hr;           /* Average heart rate (bpm) */
  int     stress_periods;   /* Count of stress events */
};

/* Upload payload for emotion diary endpoint */

struct mimo_diary_request
{
  int                       event_count;
  struct mimo_event         events[MIMO_MAX_EVENTS];
  int                       avg_hr;
  int                       stress_level;    /* 0-100 */
  char                      user_note[256];  /* User-provided context */
  struct mimo_daily_summary daily_summary;
};

/* Response from emotion diary endpoint */

struct mimo_diary_response
{
  char diary_text[MIMO_MAX_TEXT_LEN];
  char mood_summary[256];
  int  suggestion_count;
  char suggestions[MIMO_MAX_SUGGESTIONS][256];
};

/* Upload payload for weekly report endpoint */

struct mimo_weekly_request
{
  int                       event_count;
  struct mimo_event         events[MIMO_MAX_EVENTS];
  int                       daily_stats_count;
  struct mimo_daily_summary daily_stats[7];
};

/* Response from weekly report endpoint */

struct mimo_weekly_response
{
  char report_text[MIMO_MAX_TEXT_LEN];
  int  highlight_count;
  char highlights[MIMO_MAX_RECOMMEND][256];
  int  recommendation_count;
  char recommendations[MIMO_MAX_RECOMMEND][256];
};

/* Upload payload for breathing guide endpoint */

struct mimo_breath_request
{
  int  current_stress;      /* 0-100 */
  char preference[64];      /* e.g. "relax", "focus", "sleep" */
};

/* Response from breathing guide endpoint */

struct mimo_breath_response
{
  char guide_text[MIMO_MAX_TEXT_LEN];
  int  duration_sec;        /* Recommended session length */
  char pattern[64];         /* e.g. "4-4-6" */
};

/* Upload payload for alert explanation endpoint */

struct mimo_explain_request
{
  struct mimo_event event;
  char              user_question[256];
};

/* Response from alert explanation endpoint */

struct mimo_explain_response
{
  char explanation_text[MIMO_MAX_TEXT_LEN];
  char disclaimer[256];
};

/* Token budget tracker */

struct mimo_token_budget
{
  int  used_today;          /* Tokens consumed today */
  int  daily_limit;         /* Max tokens per day */
  time_t last_reset;        /* When the counter was last reset */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: mimo_client_init
 *
 * Description:
 *   Initialise the Mimo client.  Must be called once before any other
 *   mimo_client_* function.  Sets up the HTTP session, loads the auth
 *   token from persistent storage, and resets the daily budget if the
 *   date has rolled over.
 *
 * Returned Value:
 *   MIMO_OK on success.
 *
 ****************************************************************************/

enum mimo_status mimo_client_init(void);

/****************************************************************************
 * Name: mimo_client_deinit
 *
 * Description:
 *   Release all resources held by the Mimo client.
 *
 ****************************************************************************/

void mimo_client_deinit(void);

/****************************************************************************
 * Name: mimo_client_set_auth_token
 *
 * Description:
 *   Store the bearer token used for Mimo API authentication.
 *   The token is kept in memory only; the caller is responsible for
 *   persisting it if needed.
 *
 * Input Parameters:
 *   token - Null-terminated JWT or API key string.
 *
 * Returned Value:
 *   MIMO_OK on success, MIMO_ERR_INVALID_PARAM if token is NULL.
 *
 ****************************************************************************/

enum mimo_status mimo_client_set_auth_token(const char *token);

/****************************************************************************
 * Name: mimo_client_get_token_budget
 *
 * Description:
 *   Query the current daily token budget status.
 *
 * Output Parameters:
 *   budget - Filled with current usage and limits.
 *
 * Returned Value:
 *   MIMO_OK on success.
 *
 ****************************************************************************/

enum mimo_status mimo_client_get_token_budget(struct mimo_token_budget *budget);

/****************************************************************************
 * Name: mimo_client_post_diary
 *
 * Description:
 *   Upload emotion events and request a generated diary entry.
 *   The request payload must have already passed privacy_policy_validate().
 *
 * Input Parameters:
 *   req  - Diary generation request.
 *
 * Output Parameters:
 *   resp - Diary generation response.
 *
 * Returned Value:
 *   MIMO_OK on success, or an error code.
 *
 ****************************************************************************/

enum mimo_status mimo_client_post_diary(
    const struct mimo_diary_request *req,
    struct mimo_diary_response *resp);

/****************************************************************************
 * Name: mimo_client_post_weekly_report
 *
 * Description:
 *   Upload weekly data and request a summary report.
 *
 * Input Parameters:
 *   req  - Weekly report request.
 *
 * Output Parameters:
 *   resp - Weekly report response.
 *
 * Returned Value:
 *   MIMO_OK on success, or an error code.
 *
 ****************************************************************************/

enum mimo_status mimo_client_post_weekly_report(
    const struct mimo_weekly_request *req,
    struct mimo_weekly_response *resp);

/****************************************************************************
 * Name: mimo_client_post_breath_guide
 *
 * Description:
 *   Request a personalised breathing exercise guide.
 *
 * Input Parameters:
 *   req  - Breath guide request.
 *
 * Output Parameters:
 *   resp - Breath guide response.
 *
 * Returned Value:
 *   MIMO_OK on success, or an error code.
 *
 ****************************************************************************/

enum mimo_status mimo_client_post_breath_guide(
    const struct mimo_breath_request *req,
    struct mimo_breath_response *resp);

/****************************************************************************
 * Name: mimo_client_post_explain_alert
 *
 * Description:
 *   Request a natural-language explanation for a specific event.
 *
 * Input Parameters:
 *   req  - Alert explanation request.
 *
 * Output Parameters:
 *   resp - Alert explanation response.
 *
 * Returned Value:
 *   MIMO_OK on success, or an error code.
 *
 ****************************************************************************/

enum mimo_status mimo_client_post_explain_alert(
    const struct mimo_explain_request *req,
    struct mimo_explain_response *resp);

/****************************************************************************
 * Name: mimo_status_to_string
 *
 * Description:
 *   Convert a mimo_status code to a human-readable string.
 *
 ****************************************************************************/

const char *mimo_status_to_string(enum mimo_status status);

#endif /* __FIRMWARE_APPS_VELASENSE_MIMO_CLIENT_H */
