/****************************************************************************
 * VelaSense Mimo Token Plan Client - Implementation
 *
 * HTTP client for the Mimo cloud API.  This file runs on the phone-side
 * gateway application, not on the NuttX wearable.
 *
 * JSON serialisation is hand-rolled to avoid external dependencies.  For
 * production use, replace the serialise/parse helpers with cJSON or
 * a similar library.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <syslog.h>

#include "mimo_client.h"
#include "privacy_policy.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TAG "mimo_client"

#define HTTP_METHOD_POST  "POST"
#define CONTENT_TYPE_JSON "application/json; charset=utf-8"
#define AUTH_BEARER_FMT   "Bearer %s"

/* JSON buffer sizes */

#define JSON_REQ_BUF_SIZE  4096
#define JSON_RESP_BUF_SIZE 4096

/* Endpoint paths */

#define PATH_EMOTION_DIARY  "/api/v1/emotion-diary"
#define PATH_WEEKLY_REPORT  "/api/v1/weekly-report"
#define PATH_BREATH_GUIDE   "/api/v1/breath-guide"
#define PATH_EXPLAIN_ALERT  "/api/v1/explain-alert"

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Opaque HTTP session context (platform-specific internals hidden here) */

struct http_session
{
  char auth_token[512];
  bool initialised;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct http_session g_session;
static struct mimo_token_budget g_budget;

/****************************************************************************
 * Private Function Prototypes - HTTP Layer (platform stubs)
 ****************************************************************************/

static int http_post_json(const char *url, const char *bearer_token,
                          const char *json_body,
                          char *resp_buf, size_t resp_buf_size,
                          int *http_status);

/****************************************************************************
 * Private Function Prototypes - JSON Serialisation
 ****************************************************************************/

static int serialise_event(char *buf, size_t bufsize,
                           const struct mimo_event *evt);

static int serialise_daily_summary(char *buf, size_t bufsize,
                                   const struct mimo_daily_summary *sum);

static int serialise_diary_request(char *buf, size_t bufsize,
                                   const struct mimo_diary_request *req);

static int serialise_weekly_request(char *buf, size_t bufsize,
                                    const struct mimo_weekly_request *req);

static int serialise_breath_request(char *buf, size_t bufsize,
                                    const struct mimo_breath_request *req);

static int serialise_explain_request(char *buf, size_t bufsize,
                                     const struct mimo_explain_request *req);

/****************************************************************************
 * Private Function Prototypes - JSON Parsing
 ****************************************************************************/

static int parse_diary_response(const char *json,
                                struct mimo_diary_response *resp);

static int parse_weekly_response(const char *json,
                                 struct mimo_weekly_response *resp);

static int parse_breath_response(const char *json,
                                 struct mimo_breath_response *resp);

static int parse_explain_response(const char *json,
                                  struct mimo_explain_response *resp);

/****************************************************************************
 * Private Function Prototypes - Helpers
 ****************************************************************************/

static void reset_daily_budget_if_needed(void);
static bool check_token_budget(void);
static void consume_token(void);
static int  build_url(char *buf, size_t bufsize, const char *path);

/* Minimal JSON string extractor (production: use cJSON) */

static int json_get_string(const char *json, const char *key,
                           char *out, size_t out_size);

static int json_get_int(const char *json, const char *key, int *out);

static int json_get_string_array(const char *json, const char *key,
                                 char array[][256], int max_count);

/****************************************************************************
 * Private Functions - HTTP Layer Stubs
 ****************************************************************************/

/****************************************************************************
 * Name: http_post_json
 *
 * Description:
 *   Platform-specific HTTP POST with JSON body.  On Android/iOS this
 *   would call the native HTTP stack; on POSIX it uses libcurl or
 *   similar.  Stubbed here for portability.
 *
 *   In a real build this function is provided by the platform layer
 *   and linked at compile time.
 *
 ****************************************************************************/

static int http_post_json(const char *url, const char *bearer_token,
                          const char *json_body,
                          char *resp_buf, size_t resp_buf_size,
                          int *http_status)
{
  /* ------------------------------------------------------------------ *
   * PLATFORM STUB                                                       *
   * Replace with:                                                       *
   *   - Android: HttpURLConnection / OkHttp                            *
   *   - iOS:     NSURLSession                                           *
   *   - POSIX:   libcurl                                                *
   *                                                                     *
   * The caller expects:                                                  *
   *   - HTTPS POST to `url`                                             *
   *   - Header: Authorization: Bearer <token>                          *
   *   - Header: Content-Type: application/json; charset=utf-8          *
   *   - Timeout: MIMO_HTTP_TIMEOUT_MS                                   *
   *   - On success: copy response body into resp_buf, set http_status  *
   *   - On failure: return negative errno, set *http_status = -1       *
   * ------------------------------------------------------------------ */

  (void)url;
  (void)bearer_token;
  (void)json_body;
  (void)resp_buf;
  (void)resp_buf_size;

  *http_status = -1;

  syslog(LOG_WARNING,
         "[%s] http_post_json: platform stub called - "
         "link real HTTP backend\n", TAG);

  return -1;
}

/****************************************************************************
 * Private Functions - JSON Serialisation
 ****************************************************************************/

/****************************************************************************
 * Name: serialise_event
 *
 * Description:
 *   Serialise a single mimo_event to JSON.  Format:
 *   {"time":"...","label":"...","confidence":87,"activity":"...",
 *    "hr_context":"...","reason_codes":["hr_rise","hrv_drop"]}
 *
 ****************************************************************************/

static int serialise_event(char *buf, size_t bufsize,
                           const struct mimo_event *evt)
{
  int off = 0;
  int n;
  int i;

  n = snprintf(buf + off, bufsize - off,
               "{\"time\":\"%s\",\"label\":\"%s\","
               "\"confidence\":%d,\"activity\":\"%s\","
               "\"hr_context\":\"%s\",\"reason_codes\":[",
               evt->time, evt->label, evt->confidence,
               evt->activity, evt->hr_context);
  if (n < 0 || (size_t)n >= bufsize - off)
    {
      return -1;
    }

  off += n;

  for (i = 0; i < evt->reason_count; i++)
    {
      if (i > 0)
        {
          if ((size_t)off >= bufsize - 1)
            {
              return -1;
            }

          buf[off++] = ',';
        }

      n = snprintf(buf + off, bufsize - off,
                   "\"%s\"", evt->reason_codes[i].code);
      if (n < 0 || (size_t)n >= bufsize - off)
        {
          return -1;
        }

      off += n;
    }

  n = snprintf(buf + off, bufsize - off, "]}");
  if (n < 0 || (size_t)n >= bufsize - off)
    {
      return -1;
    }

  off += n;

  return off;
}

/****************************************************************************
 * Name: serialise_daily_summary
 *
 * Description:
 *   Serialise daily summary to JSON.
 *
 ****************************************************************************/

static int serialise_daily_summary(char *buf, size_t bufsize,
                                   const struct mimo_daily_summary *sum)
{
  return snprintf(buf, bufsize,
                  "{\"total_events\":%d,"
                  "\"dominant_label\":\"%s\","
                  "\"avg_hr\":%d,"
                  "\"stress_periods\":%d}",
                  sum->total_events, sum->dominant_label,
                  sum->avg_hr, sum->stress_periods);
}

/****************************************************************************
 * Name: serialise_diary_request
 *
 * Description:
 *   Build the full JSON body for POST /api/v1/emotion-diary.
 *   Format:
 *     {"version":"1.0","device_id":"velasense-377",
 *      "events":[...],"avg_hr":72,"stress_level":50,
 *      "user_note":"...","daily_summary":{...}}
 *
 ****************************************************************************/

static int serialise_diary_request(char *buf, size_t bufsize,
                                   const struct mimo_diary_request *req)
{
  int off = 0;
  int n;
  int i;

  /* Header */

  n = snprintf(buf + off, bufsize - off,
               "{\"version\":\"%s\",\"device_id\":\"%s\","
               "\"avg_hr\":%d,\"stress_level\":%d,"
               "\"user_note\":\"%s\",\"events\":[",
               MIMO_DATA_VERSION, MIMO_DEVICE_ID,
               req->avg_hr, req->stress_level, req->user_note);
  if (n < 0 || (size_t)n >= bufsize - off)
    {
      return -1;
    }

  off += n;

  /* Events array */

  for (i = 0; i < req->event_count; i++)
    {
      if (i > 0)
        {
          if ((size_t)off >= bufsize - 1)
            {
              return -1;
            }

          buf[off++] = ',';
        }

      n = serialise_event(buf + off, bufsize - off, &req->events[i]);
      if (n < 0)
        {
          return -1;
        }

      off += n;
    }

  /* Close events, add daily summary */

  n = snprintf(buf + off, bufsize - off, "],\"daily_summary\":");
  if (n < 0 || (size_t)n >= bufsize - off)
    {
      return -1;
    }

  off += n;

  n = serialise_daily_summary(buf + off, bufsize - off,
                              &req->daily_summary);
  if (n < 0)
    {
      return -1;
    }

  off += n;

  /* Close root object */

  n = snprintf(buf + off, bufsize - off, "}");
  if (n < 0 || (size_t)n >= bufsize - off)
    {
      return -1;
    }

  off += n;

  return off;
}

/****************************************************************************
 * Name: serialise_weekly_request
 *
 * Description:
 *   Build the full JSON body for POST /api/v1/weekly-report.
 *
 ****************************************************************************/

static int serialise_weekly_request(char *buf, size_t bufsize,
                                    const struct mimo_weekly_request *req)
{
  int off = 0;
  int n;
  int i;

  n = snprintf(buf + off, bufsize - off,
               "{\"version\":\"%s\",\"device_id\":\"%s\","
               "\"week_events\":[",
               MIMO_DATA_VERSION, MIMO_DEVICE_ID);
  if (n < 0 || (size_t)n >= bufsize - off)
    {
      return -1;
    }

  off += n;

  for (i = 0; i < req->event_count; i++)
    {
      if (i > 0)
        {
          buf[off++] = ',';
        }

      n = serialise_event(buf + off, bufsize - off, &req->events[i]);
      if (n < 0)
        {
          return -1;
        }

      off += n;
    }

  n = snprintf(buf + off, bufsize - off, "],\"daily_stats\":[");
  if (n < 0 || (size_t)n >= bufsize - off)
    {
      return -1;
    }

  off += n;

  for (i = 0; i < req->daily_stats_count; i++)
    {
      if (i > 0)
        {
          buf[off++] = ',';
        }

      n = serialise_daily_summary(buf + off, bufsize - off,
                                  &req->daily_stats[i]);
      if (n < 0)
        {
          return -1;
        }

      off += n;
    }

  n = snprintf(buf + off, bufsize - off, "]}");
  if (n < 0 || (size_t)n >= bufsize - off)
    {
      return -1;
    }

  off += n;

  return off;
}

/****************************************************************************
 * Name: serialise_breath_request
 *
 * Description:
 *   Build JSON body for POST /api/v1/breath-guide.
 *
 ****************************************************************************/

static int serialise_breath_request(char *buf, size_t bufsize,
                                    const struct mimo_breath_request *req)
{
  return snprintf(buf, bufsize,
                  "{\"version\":\"%s\",\"device_id\":\"%s\","
                  "\"current_stress\":%d,\"user_preference\":\"%s\"}",
                  MIMO_DATA_VERSION, MIMO_DEVICE_ID,
                  req->current_stress, req->preference);
}

/****************************************************************************
 * Name: serialise_explain_request
 *
 * Description:
 *   Build JSON body for POST /api/v1/explain-alert.
 *
 ****************************************************************************/

static int serialise_explain_request(char *buf, size_t bufsize,
                                     const struct mimo_explain_request *req)
{
  int off = 0;
  int n;

  n = snprintf(buf + off, bufsize - off,
               "{\"version\":\"%s\",\"device_id\":\"%s\","
               "\"user_question\":\"%s\",\"event\":",
               MIMO_DATA_VERSION, MIMO_DEVICE_ID,
               req->user_question);
  if (n < 0 || (size_t)n >= bufsize - off)
    {
      return -1;
    }

  off += n;

  n = serialise_event(buf + off, bufsize - off, &req->event);
  if (n < 0)
    {
      return -1;
    }

  off += n;

  if ((size_t)off >= bufsize - 1)
    {
      return -1;
    }

  buf[off++] = '}';

  return off;
}

/****************************************************************************
 * Private Functions - JSON Parsing (minimal, production: use cJSON)
 ****************************************************************************/

static int json_get_string(const char *json, const char *key,
                           char *out, size_t out_size)
{
  char search[128];
  const char *pos;
  const char *start;
  const char *end;
  size_t len;

  snprintf(search, sizeof(search), "\"%s\":\"", key);
  pos = strstr(json, search);
  if (pos == NULL)
    {
      return -1;
    }

  pos += strlen(search);
  start = pos;

  end = strchr(start, '"');
  if (end == NULL)
    {
      return -1;
    }

  len = (size_t)(end - start);
  if (len >= out_size)
    {
      len = out_size - 1;
    }

  memcpy(out, start, len);
  out[len] = '\0';

  return (int)len;
}

static int json_get_int(const char *json, const char *key, int *out)
{
  char search[128];
  const char *pos;

  snprintf(search, sizeof(search), "\"%s\":", key);
  pos = strstr(json, search);
  if (pos == NULL)
    {
      return -1;
    }

  pos += strlen(search);

  *out = atoi(pos);

  return 0;
}

static int json_get_string_array(const char *json, const char *key,
                                 char array[][256], int max_count)
{
  char search[128];
  const char *pos;
  const char *end;
  int count = 0;

  snprintf(search, sizeof(search), "\"%s\":[", key);
  pos = strstr(json, search);
  if (pos == NULL)
    {
      return 0;
    }

  pos += strlen(search);

  end = strchr(pos, ']');
  if (end == NULL)
    {
      return 0;
    }

  /* Walk through "item1","item2",... */

  while (pos < end && count < max_count)
    {
      const char *q1 = strchr(pos, '"');
      if (q1 == NULL || q1 >= end)
        {
          break;
        }

      q1++;
      const char *q2 = strchr(q1, '"');
      if (q2 == NULL || q2 >= end)
        {
          break;
        }

      size_t len = (size_t)(q2 - q1);
      if (len >= 256)
        {
          len = 255;
        }

      memcpy(array[count], q1, len);
      array[count][len] = '\0';
      count++;

      pos = q2 + 1;
    }

  return count;
}

/****************************************************************************
 * Name: parse_diary_response
 *
 * Description:
 *   Parse the JSON response from the emotion diary endpoint.
 *   Expected format:
 *     {"diary_text":"...","mood_summary":"...",
 *      "suggestions":["...","..."]}
 *
 ****************************************************************************/

static int parse_diary_response(const char *json,
                                struct mimo_diary_response *resp)
{
  memset(resp, 0, sizeof(*resp));

  if (json_get_string(json, "diary_text",
                      resp->diary_text, sizeof(resp->diary_text)) < 0)
    {
      syslog(LOG_ERR, "[%s] Missing diary_text in response\n", TAG);
      return -1;
    }

  json_get_string(json, "mood_summary",
                  resp->mood_summary, sizeof(resp->mood_summary));

  resp->suggestion_count =
      json_get_string_array(json, "suggestions",
                            resp->suggestions, MIMO_MAX_SUGGESTIONS);

  return 0;
}

/****************************************************************************
 * Name: parse_weekly_response
 *
 * Description:
 *   Parse the JSON response from the weekly report endpoint.
 *
 ****************************************************************************/

static int parse_weekly_response(const char *json,
                                 struct mimo_weekly_response *resp)
{
  memset(resp, 0, sizeof(*resp));

  if (json_get_string(json, "report_text",
                      resp->report_text, sizeof(resp->report_text)) < 0)
    {
      syslog(LOG_ERR, "[%s] Missing report_text in response\n", TAG);
      return -1;
    }

  resp->highlight_count =
      json_get_string_array(json, "highlights",
                            resp->highlights, MIMO_MAX_RECOMMEND);

  resp->recommendation_count =
      json_get_string_array(json, "recommendations",
                            resp->recommendations, MIMO_MAX_RECOMMEND);

  return 0;
}

/****************************************************************************
 * Name: parse_breath_response
 *
 * Description:
 *   Parse the JSON response from the breathing guide endpoint.
 *
 ****************************************************************************/

static int parse_breath_response(const char *json,
                                 struct mimo_breath_response *resp)
{
  memset(resp, 0, sizeof(*resp));

  if (json_get_string(json, "guide_text",
                      resp->guide_text, sizeof(resp->guide_text)) < 0)
    {
      syslog(LOG_ERR, "[%s] Missing guide_text in response\n", TAG);
      return -1;
    }

  json_get_int(json, "duration_sec", &resp->duration_sec);

  json_get_string(json, "pattern",
                  resp->pattern, sizeof(resp->pattern));

  return 0;
}

/****************************************************************************
 * Name: parse_explain_response
 *
 * Description:
 *   Parse the JSON response from the alert explanation endpoint.
 *
 ****************************************************************************/

static int parse_explain_response(const char *json,
                                  struct mimo_explain_response *resp)
{
  memset(resp, 0, sizeof(*resp));

  if (json_get_string(json, "explanation_text",
                      resp->explanation_text,
                      sizeof(resp->explanation_text)) < 0)
    {
      syslog(LOG_ERR, "[%s] Missing explanation_text in response\n", TAG);
      return -1;
    }

  json_get_string(json, "disclaimer",
                  resp->disclaimer, sizeof(resp->disclaimer));

  return 0;
}

/****************************************************************************
 * Private Functions - Helpers
 ****************************************************************************/

static void reset_daily_budget_if_needed(void)
{
  time_t now;
  struct tm now_tm;
  struct tm reset_tm;

  time(&now);

  localtime_r(&now, &now_tm);
  localtime_r(&g_budget.last_reset, &reset_tm);

  if (now_tm.tm_yday != reset_tm.tm_yday ||
      now_tm.tm_year != reset_tm.tm_year)
    {
      syslog(LOG_INFO, "[%s] Daily token budget reset\n", TAG);
      g_budget.used_today = 0;
      g_budget.last_reset = now;
    }
}

static bool check_token_budget(void)
{
  reset_daily_budget_if_needed();

  if (g_budget.used_today >= g_budget.daily_limit)
    {
      syslog(LOG_WARNING,
             "[%s] Token budget exhausted: %d/%d\n",
             TAG, g_budget.used_today, g_budget.daily_limit);
      return false;
    }

  return true;
}

static void consume_token(void)
{
  g_budget.used_today++;
  syslog(LOG_DEBUG, "[%s] Token consumed: %d/%d\n",
         TAG, g_budget.used_today, g_budget.daily_limit);
}

static int build_url(char *buf, size_t bufsize, const char *path)
{
  return snprintf(buf, bufsize, "%s%s", MIMO_API_BASE_URL, path);
}

/****************************************************************************
 * Private Functions - Generic Endpoint Caller
 ****************************************************************************/

/****************************************************************************
 * Name: do_api_call
 *
 * Description:
 *   Generic helper: serialise request, check budget, POST, parse
 *   response.  All four endpoints share this flow.
 *
 ****************************************************************************/

static enum mimo_status do_api_call(const char *path,
                                    const char *req_json,
                                    char *resp_json, size_t resp_json_size,
                                    int *http_status)
{
  char url[256];
  int  retry;
  int  ret;

  if (!g_session.initialised)
    {
      syslog(LOG_ERR, "[%s] Client not initialised\n", TAG);
      return MIMO_ERR_INVALID_PARAM;
    }

  if (!check_token_budget())
    {
      return MIMO_ERR_TOKEN_EXHAUSTED;
    }

  build_url(url, sizeof(url), path);

  /* Retry loop */

  for (retry = 0; retry <= MIMO_RETRY_COUNT; retry++)
    {
      if (retry > 0)
        {
          syslog(LOG_INFO, "[%s] Retry %d/%d for %s\n",
                 TAG, retry, MIMO_RETRY_COUNT, path);

          /* Simple back-off (production: use platform sleep) */

          struct timespec ts;
          ts.tv_sec = MIMO_RETRY_DELAY_MS / 1000;
          ts.tv_nsec = (MIMO_RETRY_DELAY_MS % 1000) * 1000000L;
          nanosleep(&ts, NULL);
        }

      ret = http_post_json(url, g_session.auth_token,
                           req_json,
                           resp_json, resp_json_size,
                           http_status);
      if (ret == 0 && *http_status >= 200 && *http_status < 300)
        {
          consume_token();
          return MIMO_OK;
        }

      syslog(LOG_WARNING,
             "[%s] HTTP %s returned %d (status=%d)\n",
             TAG, path, *http_status, ret);
    }

  /* Classify the failure */

  if (*http_status == 401 || *http_status == 403)
    {
      return MIMO_ERR_AUTH;
    }

  if (*http_status == 429)
    {
      return MIMO_ERR_TOKEN_EXHAUSTED;
    }

  if (*http_status < 0)
    {
      return MIMO_ERR_NETWORK;
    }

  return MIMO_ERR_HTTP;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

enum mimo_status mimo_client_init(void)
{
  memset(&g_session, 0, sizeof(g_session));
  memset(&g_budget, 0, sizeof(g_budget));

  g_budget.daily_limit = MIMO_MAX_DAILY_TOKENS;
  g_budget.last_reset = time(NULL);
  g_session.initialised = true;

  syslog(LOG_INFO, "[%s] Mimo client initialised "
         "(daily token limit: %d)\n",
         TAG, MIMO_MAX_DAILY_TOKENS);

  return MIMO_OK;
}

void mimo_client_deinit(void)
{
  memset(&g_session, 0, sizeof(g_session));
  syslog(LOG_INFO, "[%s] Mimo client deinitialised\n", TAG);
}

enum mimo_status mimo_client_set_auth_token(const char *token)
{
  if (token == NULL)
    {
      return MIMO_ERR_INVALID_PARAM;
    }

  size_t len = strlen(token);
  if (len >= sizeof(g_session.auth_token))
    {
      return MIMO_ERR_INVALID_PARAM;
    }

  memcpy(g_session.auth_token, token, len + 1);

  syslog(LOG_INFO, "[%s] Auth token set (%zu bytes)\n", TAG, len);

  return MIMO_OK;
}

enum mimo_status mimo_client_get_token_budget(struct mimo_token_budget *budget)
{
  if (budget == NULL)
    {
      return MIMO_ERR_INVALID_PARAM;
    }

  reset_daily_budget_if_needed();

  memcpy(budget, &g_budget, sizeof(*budget));

  return MIMO_OK;
}

enum mimo_status mimo_client_post_diary(
    const struct mimo_diary_request *req,
    struct mimo_diary_response *resp)
{
  char req_json[JSON_REQ_BUF_SIZE];
  char resp_json[JSON_RESP_BUF_SIZE];
  int  http_status;
  int  n;
  enum mimo_status status;

  if (req == NULL || resp == NULL)
    {
      return MIMO_ERR_INVALID_PARAM;
    }

  /* Serialise */

  n = serialise_diary_request(req_json, sizeof(req_json), req);
  if (n < 0)
    {
      syslog(LOG_ERR, "[%s] Diary request serialisation failed\n", TAG);
      return MIMO_ERR_INVALID_PARAM;
    }

  syslog(LOG_INFO, "[%s] Requesting emotion diary (%d events)\n",
         TAG, req->event_count);

  /* Privacy audit log (no content) */

  syslog(LOG_INFO, "[%s] AUDIT: diary upload, %d events, "
         "avg_hr=%d, note_len=%zu\n",
         TAG, req->event_count, req->avg_hr,
         strlen(req->user_note));

  /* Call API */

  status = do_api_call(PATH_EMOTION_DIARY,
                       req_json, resp_json, sizeof(resp_json),
                       &http_status);
  if (status != MIMO_OK)
    {
      return status;
    }

  /* Parse response */

  if (parse_diary_response(resp_json, resp) < 0)
    {
      syslog(LOG_ERR, "[%s] Failed to parse diary response\n", TAG);
      return MIMO_ERR_PARSE;
    }

  syslog(LOG_INFO, "[%s] Emotion diary received (%zu chars)\n",
         TAG, strlen(resp->diary_text));

  return MIMO_OK;
}

enum mimo_status mimo_client_post_weekly_report(
    const struct mimo_weekly_request *req,
    struct mimo_weekly_response *resp)
{
  char req_json[JSON_REQ_BUF_SIZE];
  char resp_json[JSON_RESP_BUF_SIZE];
  int  http_status;
  int  n;
  enum mimo_status status;

  if (req == NULL || resp == NULL)
    {
      return MIMO_ERR_INVALID_PARAM;
    }

  n = serialise_weekly_request(req_json, sizeof(req_json), req);
  if (n < 0)
    {
      return MIMO_ERR_INVALID_PARAM;
    }

  syslog(LOG_INFO, "[%s] Requesting weekly report (%d events, %d days)\n",
         TAG, req->event_count, req->daily_stats_count);

  syslog(LOG_INFO, "[%s] AUDIT: weekly report upload, %d events\n",
         TAG, req->event_count);

  status = do_api_call(PATH_WEEKLY_REPORT,
                       req_json, resp_json, sizeof(resp_json),
                       &http_status);
  if (status != MIMO_OK)
    {
      return status;
    }

  if (parse_weekly_response(resp_json, resp) < 0)
    {
      return MIMO_ERR_PARSE;
    }

  syslog(LOG_INFO, "[%s] Weekly report received (%zu chars, %d highlights)\n",
         TAG, strlen(resp->report_text), resp->highlight_count);

  return MIMO_OK;
}

enum mimo_status mimo_client_post_breath_guide(
    const struct mimo_breath_request *req,
    struct mimo_breath_response *resp)
{
  char req_json[JSON_REQ_BUF_SIZE];
  char resp_json[JSON_RESP_BUF_SIZE];
  int  http_status;
  int  n;
  enum mimo_status status;

  if (req == NULL || resp == NULL)
    {
      return MIMO_ERR_INVALID_PARAM;
    }

  n = serialise_breath_request(req_json, sizeof(req_json), req);
  if (n < 0)
    {
      return MIMO_ERR_INVALID_PARAM;
    }

  syslog(LOG_INFO, "[%s] Requesting breath guide (stress=%d, pref=%s)\n",
         TAG, req->current_stress, req->preference);

  syslog(LOG_INFO, "[%s] AUDIT: breath guide request\n", TAG);

  status = do_api_call(PATH_BREATH_GUIDE,
                       req_json, resp_json, sizeof(resp_json),
                       &http_status);
  if (status != MIMO_OK)
    {
      return status;
    }

  if (parse_breath_response(resp_json, resp) < 0)
    {
      return MIMO_ERR_PARSE;
    }

  syslog(LOG_INFO, "[%s] Breath guide received (pattern=%s, %ds)\n",
         TAG, resp->pattern, resp->duration_sec);

  return MIMO_OK;
}

enum mimo_status mimo_client_post_explain_alert(
    const struct mimo_explain_request *req,
    struct mimo_explain_response *resp)
{
  char req_json[JSON_REQ_BUF_SIZE];
  char resp_json[JSON_RESP_BUF_SIZE];
  int  http_status;
  int  n;
  enum mimo_status status;

  if (req == NULL || resp == NULL)
    {
      return MIMO_ERR_INVALID_PARAM;
    }

  n = serialise_explain_request(req_json, sizeof(req_json), req);
  if (n < 0)
    {
      return MIMO_ERR_INVALID_PARAM;
    }

  syslog(LOG_INFO, "[%s] Requesting alert explanation (label=%s)\n",
         TAG, req->event.label);

  syslog(LOG_INFO, "[%s] AUDIT: explain alert request\n", TAG);

  status = do_api_call(PATH_EXPLAIN_ALERT,
                       req_json, resp_json, sizeof(resp_json),
                       &http_status);
  if (status != MIMO_OK)
    {
      return status;
    }

  if (parse_explain_response(resp_json, resp) < 0)
    {
      return MIMO_ERR_PARSE;
    }

  syslog(LOG_INFO, "[%s] Alert explanation received (%zu chars)\n",
         TAG, strlen(resp->explanation_text));

  return MIMO_OK;
}

const char *mimo_status_to_string(enum mimo_status status)
{
  switch (status)
    {
      case MIMO_OK:
        return "OK";
      case MIMO_ERR_NETWORK:
        return "network error";
      case MIMO_ERR_HTTP:
        return "HTTP error";
      case MIMO_ERR_PARSE:
        return "JSON parse error";
      case MIMO_ERR_PRIVACY:
        return "privacy policy rejected";
      case MIMO_ERR_TOKEN_EXHAUSTED:
        return "token budget exhausted";
      case MIMO_ERR_AUTH:
        return "authentication failed";
      case MIMO_ERR_INVALID_PARAM:
        return "invalid parameter";
      case MIMO_ERR_BUFFER_TOO_SMALL:
        return "buffer too small";
      default:
        return "unknown error";
    }
}
