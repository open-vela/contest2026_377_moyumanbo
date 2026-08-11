/****************************************************************************
 * VelaSense Privacy Policy Enforcement - Implementation
 *
 * Validates and sanitises all data before it leaves the device.
 * This is the single point of control for data upload policy.
 *
 * Check order (fail-fast):
 *   1. User authorization
 *   2. Raw waveform detection
 *   3. Location data detection
 *   4. Contact information detection
 *   5. Medical diagnosis language detection
 *   6. Relationship inference detection
 *   7. User note sanitisation
 *   8. Daily token budget
 *   9. Audit log entry
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <syslog.h>

#include "privacy_policy.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TAG "privacy"

/* Patterns that indicate raw waveform data in a payload */

#define RAW_DATA_MARKER_PPG    "ppg_raw"
#define RAW_DATA_MARKER_EDA    "eda_raw"
#define RAW_DATA_MARKER_WAVE   "waveform"
#define RAW_DATA_MARKER_SAMPLE "samples"

/* Patterns that indicate location data */

#define LOCATION_MARKER_GPS    "gps"
#define LOCATION_MARKER_LAT    "latitude"
#define LOCATION_MARKER_LON    "longitude"
#define LOCATION_MARKER_COORD  "coordinate"
#define LOCATION_MARKER_LOC    "location"

/* Patterns that indicate contact information */

#define CONTACT_MARKER_PHONE   "phone"
#define CONTACT_MARKER_EMAIL   "email"
#define CONTACT_MARKER_WECHAT  "wechat"
#define CONTACT_MARKER_CONTACT "contact"

/* Patterns that indicate medical diagnosis */

#define DIAG_MARKER_DIAGNOSIS  "diagnosis"
#define DIAG_MARKER_DISEASE    "disease"
#define DIAG_MARKER_DISORDER   "disorder"
#define DIAG_MARKER_CLINICAL   "clinical"
#define DIAG_MARKER_TREATMENT  "treatment"
#define DIAG_MARKER_PRESCRIBE  "prescribe"

/* Patterns that indicate relationship inference */

#define REL_MARKER_RELATION    "relationship"
#define REL_MARKER_PARTNER     "partner"
#define REL_MARKER_FAMILY      "family"
#define REL_MARKER_COLLEAGUE   "colleague"
#define REL_MARKER_FRIEND      "friend"

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void reset_daily_budget(struct privacy_policy_ctx *ctx);
static bool contains_any(const char *text, const char *patterns[],
                         int pattern_count);
static bool is_valid_event_label(const char *label);
static bool is_valid_hr_context(const char *ctx);
static bool is_valid_activity(const char *activity);
static void audit_log(struct privacy_policy_ctx *ctx,
                      enum privacy_upload_type type,
                      int event_count, bool had_note,
                      bool passed, enum privacy_result reject_code);

/****************************************************************************
 * Private Data - Forbidden Patterns
 ****************************************************************************/

static const char *g_raw_data_patterns[] =
{
  RAW_DATA_MARKER_PPG,
  RAW_DATA_MARKER_EDA,
  RAW_DATA_MARKER_WAVE,
  RAW_DATA_MARKER_SAMPLE
};

static const char *g_location_patterns[] =
{
  LOCATION_MARKER_GPS,
  LOCATION_MARKER_LAT,
  LOCATION_MARKER_LON,
  LOCATION_MARKER_COORD,
  /* Note: "location" is too common in English; check activity fields only */
};

static const char *g_contact_patterns[] =
{
  CONTACT_MARKER_PHONE,
  CONTACT_MARKER_EMAIL,
  CONTACT_MARKER_WECHAT,
  CONTACT_MARKER_CONTACT
};

static const char *g_diagnosis_patterns[] =
{
  DIAG_MARKER_DIAGNOSIS,
  DIAG_MARKER_DISEASE,
  DIAG_MARKER_DISORDER,
  DIAG_MARKER_CLINICAL,
  DIAG_MARKER_TREATMENT,
  DIAG_MARKER_PRESCRIBE
};

static const char *g_relationship_patterns[] =
{
  REL_MARKER_RELATION,
  REL_MARKER_PARTNER,
  REL_MARKER_FAMILY,
  REL_MARKER_COLLEAGUE,
  REL_MARKER_FRIEND
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: reset_daily_budget
 *
 * Description:
 *   Reset the daily token counter if the calendar day has changed.
 *
 ****************************************************************************/

static void reset_daily_budget(struct privacy_policy_ctx *ctx)
{
  time_t now;
  struct tm now_tm;
  struct tm reset_tm;

  time(&now);

  localtime_r(&now, &now_tm);
  localtime_r(&ctx->last_reset, &reset_tm);

  if (now_tm.tm_yday != reset_tm.tm_yday ||
      now_tm.tm_year != reset_tm.tm_year)
    {
      syslog(LOG_INFO, "[%s] Daily token budget reset\n", TAG);
      ctx->daily_token_count = 0;
      ctx->last_reset = now;
    }
}

/****************************************************************************
 * Name: str_lower_inplace
 *
 * Description:
 *   Convert a string to lowercase in-place.  Used for case-insensitive
 *   pattern matching.
 *
 ****************************************************************************/

static void str_lower_inplace(char *s)
{
  while (*s)
    {
      *s = tolower((unsigned char)*s);
      s++;
    }
}

/****************************************************************************
 * Name: contains_any
 *
 * Description:
 *   Check if `text` contains any of the given patterns (case-insensitive).
 *   Returns true on the first match.
 *
 ****************************************************************************/

static bool contains_any(const char *text, const char *patterns[],
                         int pattern_count)
{
  char lower_buf[1024];
  int  i;
  size_t len;

  if (text == NULL || text[0] == '\0')
    {
      return false;
    }

  len = strlen(text);
  if (len >= sizeof(lower_buf))
    {
      len = sizeof(lower_buf) - 1;
    }

  memcpy(lower_buf, text, len);
  lower_buf[len] = '\0';
  str_lower_inplace(lower_buf);

  for (i = 0; i < pattern_count; i++)
    {
      if (strstr(lower_buf, patterns[i]) != NULL)
        {
          syslog(LOG_WARNING,
                 "[%s] Forbidden pattern detected: \"%s\"\n",
                 TAG, patterns[i]);
          return true;
        }
    }

  return false;
}

/****************************************************************************
 * Name: is_valid_event_label
 *
 * Description:
 *   Check if a label string is one of the allowed event labels.
 *
 ****************************************************************************/

static bool is_valid_event_label(const char *label)
{
  return (strcmp(label, "stress") == 0 ||
          strcmp(label, "excitement") == 0 ||
          strcmp(label, "nervous") == 0 ||
          strcmp(label, "surprise") == 0 ||
          strcmp(label, "other") == 0);
}

/****************************************************************************
 * Name: is_valid_hr_context
 *
 * Description:
 *   Check if an HR context string is one of the allowed values.
 *
 ****************************************************************************/

static bool is_valid_hr_context(const char *ctx)
{
  return (strcmp(ctx, "elevated") == 0 ||
          strcmp(ctx, "normal") == 0 ||
          strcmp(ctx, "low") == 0);
}

/****************************************************************************
 * Name: is_valid_activity
 *
 * Description:
 *   Check if an activity string is one of the allowed values.
 *
 ****************************************************************************/

static bool is_valid_activity(const char *activity)
{
  return (strcmp(activity, "sitting") == 0 ||
          strcmp(activity, "walking") == 0 ||
          strcmp(activity, "running") == 0 ||
          strcmp(activity, "sleeping") == 0 ||
          strcmp(activity, "unknown") == 0);
}

/****************************************************************************
 * Name: audit_log
 *
 * Description:
 *   Record an upload attempt in the circular audit log.
 *
 ****************************************************************************/

static void audit_log(struct privacy_policy_ctx *ctx,
                      enum privacy_upload_type type,
                      int event_count, bool had_note,
                      bool passed, enum privacy_result reject_code)
{
  struct privacy_audit_entry *entry;

  entry = &ctx->log[ctx->log_head];

  entry->timestamp    = time(NULL);
  entry->upload_type  = (int)type;
  entry->event_count  = event_count;
  entry->had_user_note = had_note;
  entry->passed       = passed;
  entry->reject_reason = (int)reject_code;

  ctx->log_head = (ctx->log_head + 1) % PRIVACY_MAX_LOG_ENTRIES;
  if (ctx->log_count < PRIVACY_MAX_LOG_ENTRIES)
    {
      ctx->log_count++;
    }

  syslog(LOG_INFO,
         "[%s] AUDIT: type=%d events=%d note=%d "
         "passed=%d reason=%d\n",
         TAG, type, event_count, had_note,
         passed, reject_code);
}

/****************************************************************************
 * Name: validate_event
 *
 * Description:
 *   Validate a single event struct.  Checks for forbidden patterns in
 *   reason code strings and validates field values.
 *
 ****************************************************************************/

static enum privacy_result validate_event(const struct mimo_event *evt)
{
  int i;

  /* Validate label */

  if (!is_valid_event_label(evt->label))
    {
      syslog(LOG_WARNING, "[%s] Invalid event label: %s\n",
             TAG, evt->label);
      return PRIVACY_ERR_INVALID_PARAM;
    }

  /* Validate HR context */

  if (!is_valid_hr_context(evt->hr_context))
    {
      syslog(LOG_WARNING, "[%s] Invalid HR context: %s\n",
             TAG, evt->hr_context);
      return PRIVACY_ERR_INVALID_PARAM;
    }

  /* Validate activity */

  if (!is_valid_activity(evt->activity))
    {
      syslog(LOG_WARNING, "[%s] Invalid activity: %s\n",
             TAG, evt->activity);
      return PRIVACY_ERR_INVALID_PARAM;
    }

  /* Validate confidence range */

  if (evt->confidence < 0 || evt->confidence > 100)
    {
      syslog(LOG_WARNING, "[%s] Invalid confidence: %d\n",
             TAG, evt->confidence);
      return PRIVACY_ERR_INVALID_PARAM;
    }

  /* Check reason codes for forbidden patterns */

  for (i = 0; i < evt->reason_count; i++)
    {
      if (contains_any(evt->reason_codes[i].code,
                        g_raw_data_patterns, 4))
        {
          return PRIVACY_ERR_RAW_DATA;
        }

      if (contains_any(evt->reason_codes[i].code,
                        g_location_patterns, 3))
        {
          return PRIVACY_ERR_LOCATION;
        }

      if (contains_any(evt->reason_codes[i].code,
                        g_contact_patterns, 4))
        {
          return PRIVACY_ERR_CONTACT;
        }

      if (contains_any(evt->reason_codes[i].code,
                        g_diagnosis_patterns, 6))
        {
          return PRIVACY_ERR_DIAGNOSIS;
        }

      if (contains_any(evt->reason_codes[i].code,
                        g_relationship_patterns, 5))
        {
          return PRIVACY_ERR_RELATIONSHIP;
        }
    }

  return PRIVACY_OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void privacy_policy_init(struct privacy_policy_ctx *ctx)
{
  memset(ctx, 0, sizeof(*ctx));

  ctx->daily_token_limit = MIMO_MAX_DAILY_TOKENS;
  ctx->last_reset = time(NULL);

  /* Default: no authorization until user explicitly grants */

  ctx->auth_flags = 0;

  syslog(LOG_INFO,
         "[%s] Privacy policy engine initialised "
         "(daily limit: %d tokens)\n",
         TAG, MIMO_MAX_DAILY_TOKENS);
}

void privacy_policy_set_authorization(struct privacy_policy_ctx *ctx,
                                      enum privacy_upload_type type,
                                      bool grant)
{
  uint32_t flag = 0;

  switch (type)
    {
      case PRIVACY_UPLOAD_DIARY:
        flag = PRIVACY_AUTH_DIARY;
        break;
      case PRIVACY_UPLOAD_WEEKLY_REPORT:
        flag = PRIVACY_AUTH_WEEKLY_REPORT;
        break;
      case PRIVACY_UPLOAD_BREATH_GUIDE:
        flag = PRIVACY_AUTH_BREATH_GUIDE;
        break;
      case PRIVACY_UPLOAD_EXPLAIN_ALERT:
        flag = PRIVACY_AUTH_EXPLAIN_ALERT;
        break;
      default:
        return;
    }

  if (grant)
    {
      ctx->auth_flags |= flag;
    }
  else
    {
      ctx->auth_flags &= ~flag;
    }

  syslog(LOG_INFO,
         "[%s] Authorization type=%d grant=%d flags=0x%02x\n",
         TAG, type, grant, ctx->auth_flags);
}

bool privacy_policy_is_authorized(const struct privacy_policy_ctx *ctx,
                                  enum privacy_upload_type type)
{
  uint32_t flag = 0;

  switch (type)
    {
      case PRIVACY_UPLOAD_DIARY:
        flag = PRIVACY_AUTH_DIARY;
        break;
      case PRIVACY_UPLOAD_WEEKLY_REPORT:
        flag = PRIVACY_AUTH_WEEKLY_REPORT;
        break;
      case PRIVACY_UPLOAD_BREATH_GUIDE:
        flag = PRIVACY_AUTH_BREATH_GUIDE;
        break;
      case PRIVACY_UPLOAD_EXPLAIN_ALERT:
        flag = PRIVACY_AUTH_EXPLAIN_ALERT;
        break;
      default:
        return false;
    }

  return (ctx->auth_flags & flag) != 0;
}

enum privacy_result privacy_policy_validate_diary(
    struct privacy_policy_ctx *ctx,
    const struct mimo_diary_request *req)
{
  enum privacy_result result;
  int i;

  if (ctx == NULL || req == NULL)
    {
      return PRIVACY_ERR_INVALID_PARAM;
    }

  /* Step 1: User authorization check */

  if (!privacy_policy_is_authorized(ctx, PRIVACY_UPLOAD_DIARY))
    {
      audit_log(ctx, PRIVACY_UPLOAD_DIARY,
                req->event_count,
                req->user_note[0] != '\0',
                false, PRIVACY_ERR_NOT_AUTHORIZED);
      return PRIVACY_ERR_NOT_AUTHORIZED;
    }

  /* Step 2: Validate each event */

  for (i = 0; i < req->event_count; i++)
    {
      result = validate_event(&req->events[i]);
      if (result != PRIVACY_OK)
        {
          audit_log(ctx, PRIVACY_UPLOAD_DIARY,
                    req->event_count,
                    req->user_note[0] != '\0',
                    false, result);
          return result;
        }
    }

  /* Step 3: Check user note for forbidden content */

  if (req->user_note[0] != '\0')
    {
      /* Check for raw data references */

      if (contains_any(req->user_note,
                        g_raw_data_patterns, 4))
        {
          audit_log(ctx, PRIVACY_UPLOAD_DIARY,
                    req->event_count, true,
                    false, PRIVACY_ERR_RAW_DATA);
          return PRIVACY_ERR_RAW_DATA;
        }

      /* Check for location data in note */

      if (contains_any(req->user_note,
                        g_location_patterns, 3))
        {
          audit_log(ctx, PRIVACY_UPLOAD_DIARY,
                    req->event_count, true,
                    false, PRIVACY_ERR_LOCATION);
          return PRIVACY_ERR_LOCATION;
        }

      /* Check for contact information in note */

      if (contains_any(req->user_note,
                        g_contact_patterns, 4))
        {
          audit_log(ctx, PRIVACY_UPLOAD_DIARY,
                    req->event_count, true,
                    false, PRIVACY_ERR_CONTACT);
          return PRIVACY_ERR_CONTACT;
        }

      /* Check for medical diagnosis language in note */

      if (contains_any(req->user_note,
                        g_diagnosis_patterns, 6))
        {
          audit_log(ctx, PRIVACY_UPLOAD_DIARY,
                    req->event_count, true,
                    false, PRIVACY_ERR_DIAGNOSIS);
          return PRIVACY_ERR_DIAGNOSIS;
        }

      /* Check for relationship inference in note */

      if (contains_any(req->user_note,
                        g_relationship_patterns, 5))
        {
          audit_log(ctx, PRIVACY_UPLOAD_DIARY,
                    req->event_count, true,
                    false, PRIVACY_ERR_RELATIONSHIP);
          return PRIVACY_ERR_RELATIONSHIP;
        }
    }

  /* Step 4: Daily token budget check */

  reset_daily_budget(ctx);

  if (ctx->daily_token_count >= ctx->daily_token_limit)
    {
      audit_log(ctx, PRIVACY_UPLOAD_DIARY,
                req->event_count,
                req->user_note[0] != '\0',
                false, PRIVACY_ERR_TOKEN_BUDGET);
      return PRIVACY_ERR_TOKEN_BUDGET;
    }

  /* All checks passed */

  audit_log(ctx, PRIVACY_UPLOAD_DIARY,
            req->event_count,
            req->user_note[0] != '\0',
            true, PRIVACY_OK);

  return PRIVACY_OK;
}

enum privacy_result privacy_policy_validate_weekly(
    struct privacy_policy_ctx *ctx,
    const struct mimo_weekly_request *req)
{
  enum privacy_result result;
  int i;

  if (ctx == NULL || req == NULL)
    {
      return PRIVACY_ERR_INVALID_PARAM;
    }

  /* Authorization check */

  if (!privacy_policy_is_authorized(ctx, PRIVACY_UPLOAD_WEEKLY_REPORT))
    {
      audit_log(ctx, PRIVACY_UPLOAD_WEEKLY_REPORT,
                req->event_count, false,
                false, PRIVACY_ERR_NOT_AUTHORIZED);
      return PRIVACY_ERR_NOT_AUTHORIZED;
    }

  /* Validate events */

  for (i = 0; i < req->event_count; i++)
    {
      result = validate_event(&req->events[i]);
      if (result != PRIVACY_OK)
        {
          audit_log(ctx, PRIVACY_UPLOAD_WEEKLY_REPORT,
                    req->event_count, false,
                    false, result);
          return result;
        }
    }

  /* Token budget */

  reset_daily_budget(ctx);

  if (ctx->daily_token_count >= ctx->daily_token_limit)
    {
      audit_log(ctx, PRIVACY_UPLOAD_WEEKLY_REPORT,
                req->event_count, false,
                false, PRIVACY_ERR_TOKEN_BUDGET);
      return PRIVACY_ERR_TOKEN_BUDGET;
    }

  audit_log(ctx, PRIVACY_UPLOAD_WEEKLY_REPORT,
            req->event_count, false,
            true, PRIVACY_OK);

  return PRIVACY_OK;
}

enum privacy_result privacy_policy_validate_breath(
    struct privacy_policy_ctx *ctx,
    const struct mimo_breath_request *req)
{
  if (ctx == NULL || req == NULL)
    {
      return PRIVACY_ERR_INVALID_PARAM;
    }

  /* Authorization check */

  if (!privacy_policy_is_authorized(ctx, PRIVACY_UPLOAD_BREATH_GUIDE))
    {
      audit_log(ctx, PRIVACY_UPLOAD_BREATH_GUIDE,
                0, false,
                false, PRIVACY_ERR_NOT_AUTHORIZED);
      return PRIVACY_ERR_NOT_AUTHORIZED;
    }

  /* Stress level range check */

  if (req->current_stress < 0 || req->current_stress > 100)
    {
      audit_log(ctx, PRIVACY_UPLOAD_BREATH_GUIDE,
                0, false,
                false, PRIVACY_ERR_INVALID_PARAM);
      return PRIVACY_ERR_INVALID_PARAM;
    }

  /* Token budget */

  reset_daily_budget(ctx);

  if (ctx->daily_token_count >= ctx->daily_token_limit)
    {
      audit_log(ctx, PRIVACY_UPLOAD_BREATH_GUIDE,
                0, false,
                false, PRIVACY_ERR_TOKEN_BUDGET);
      return PRIVACY_ERR_TOKEN_BUDGET;
    }

  audit_log(ctx, PRIVACY_UPLOAD_BREATH_GUIDE,
            0, false,
            true, PRIVACY_OK);

  return PRIVACY_OK;
}

enum privacy_result privacy_policy_validate_explain(
    struct privacy_policy_ctx *ctx,
    const struct mimo_explain_request *req)
{
  enum privacy_result result;

  if (ctx == NULL || req == NULL)
    {
      return PRIVACY_ERR_INVALID_PARAM;
    }

  /* Authorization check */

  if (!privacy_policy_is_authorized(ctx, PRIVACY_UPLOAD_EXPLAIN_ALERT))
    {
      audit_log(ctx, PRIVACY_UPLOAD_EXPLAIN_ALERT,
                1, req->user_question[0] != '\0',
                false, PRIVACY_ERR_NOT_AUTHORIZED);
      return PRIVACY_ERR_NOT_AUTHORIZED;
    }

  /* Validate the event */

  result = validate_event(&req->event);
  if (result != PRIVACY_OK)
    {
      audit_log(ctx, PRIVACY_UPLOAD_EXPLAIN_ALERT,
                1, req->user_question[0] != '\0',
                false, result);
      return result;
    }

  /* Check user question for forbidden content */

  if (req->user_question[0] != '\0')
    {
      if (contains_any(req->user_question,
                        g_diagnosis_patterns, 6))
        {
          audit_log(ctx, PRIVACY_UPLOAD_EXPLAIN_ALERT,
                    1, true,
                    false, PRIVACY_ERR_DIAGNOSIS);
          return PRIVACY_ERR_DIAGNOSIS;
        }

      if (contains_any(req->user_question,
                        g_relationship_patterns, 5))
        {
          audit_log(ctx, PRIVACY_UPLOAD_EXPLAIN_ALERT,
                    1, true,
                    false, PRIVACY_ERR_RELATIONSHIP);
          return PRIVACY_ERR_RELATIONSHIP;
        }
    }

  /* Token budget */

  reset_daily_budget(ctx);

  if (ctx->daily_token_count >= ctx->daily_token_limit)
    {
      audit_log(ctx, PRIVACY_UPLOAD_EXPLAIN_ALERT,
                1, req->user_question[0] != '\0',
                false, PRIVACY_ERR_TOKEN_BUDGET);
      return PRIVACY_ERR_TOKEN_BUDGET;
    }

  audit_log(ctx, PRIVACY_UPLOAD_EXPLAIN_ALERT,
            1, req->user_question[0] != '\0',
            true, PRIVACY_OK);

  return PRIVACY_OK;
}

bool privacy_policy_check_token_budget(struct privacy_policy_ctx *ctx)
{
  if (ctx == NULL)
    {
      return false;
    }

  reset_daily_budget(ctx);

  return (ctx->daily_token_count < ctx->daily_token_limit);
}

void privacy_policy_consume_token(struct privacy_policy_ctx *ctx)
{
  if (ctx == NULL)
    {
      return;
    }

  reset_daily_budget(ctx);
  ctx->daily_token_count++;

  syslog(LOG_DEBUG,
         "[%s] Token consumed: %d/%d\n",
         TAG, ctx->daily_token_count, ctx->daily_token_limit);
}

void privacy_policy_log_upload(struct privacy_policy_ctx *ctx,
                               enum privacy_upload_type type,
                               int event_count,
                               bool had_note,
                               bool passed,
                               enum privacy_result reject_code)
{
  if (ctx == NULL)
    {
      return;
    }

  audit_log(ctx, type, event_count, had_note, passed, reject_code);
}

int privacy_policy_get_log_count(const struct privacy_policy_ctx *ctx)
{
  if (ctx == NULL)
    {
      return 0;
    }

  return ctx->log_count;
}

bool privacy_policy_get_log_entry(const struct privacy_policy_ctx *ctx,
                                  int index,
                                  struct privacy_audit_entry *entry)
{
  int actual_idx;

  if (ctx == NULL || entry == NULL || index < 0 ||
      index >= ctx->log_count)
    {
      return false;
    }

  /* Convert linear index to circular buffer index */

  actual_idx = (ctx->log_head - ctx->log_count + index +
                PRIVACY_MAX_LOG_ENTRIES) % PRIVACY_MAX_LOG_ENTRIES;

  memcpy(entry, &ctx->log[actual_idx], sizeof(*entry));

  return true;
}

enum privacy_result privacy_policy_strip_user_note(char *note)
{
  enum privacy_result result = PRIVACY_OK;
  char *pos;

  if (note == NULL)
    {
      return PRIVACY_ERR_INVALID_PARAM;
    }

  /* Truncate if too long */

  if (strlen(note) > PRIVACY_MAX_NOTE_LEN)
    {
      note[PRIVACY_MAX_NOTE_LEN] = '\0';
      result = PRIVACY_ERR_USER_NOTE_LEN;
      syslog(LOG_WARNING, "[%s] User note truncated to %d chars\n",
             TAG, PRIVACY_MAX_NOTE_LEN);
    }

  /* Strip phone number patterns (sequences of 7+ digits) */

  pos = note;
  while (*pos)
    {
      if (isdigit((unsigned char)*pos))
        {
          int digit_count = 0;
          char *start = pos;

          while (*pos && isdigit((unsigned char)*pos))
            {
              digit_count++;
              pos++;
            }

          if (digit_count >= 7)
            {
              /* Replace digits with asterisks */

              char *p;
              for (p = start; p < pos; p++)
                {
                  *p = '*';
                }

              syslog(LOG_INFO,
                     "[%s] Stripped phone number from user note\n",
                     TAG);
            }
        }
      else
        {
          pos++;
        }
    }

  /* Strip email patterns (contains @ and .) */

  pos = strstr(note, "@");
  if (pos != NULL)
    {
      /* Check if it looks like an email (has . after @) */

      char *dot = strchr(pos, '.');
      if (dot != NULL && dot - pos > 1 && *(dot + 1) != '\0')
        {
          /* Find start of email (space or start of string) */

          char *start = pos;
          while (start > note && !isspace((unsigned char)*(start - 1)))
            {
              start--;
            }

          /* Find end of email */

          char *end = dot + 1;
          while (*end && !isspace((unsigned char)*end))
            {
              end++;
            }

          /* Replace with asterisks */

          char *p;
          for (p = start; p < end; p++)
            {
              *p = '*';
            }

          syslog(LOG_INFO,
                 "[%s] Stripped email from user note\n",
                 TAG);
        }
    }

  return result;
}

const char *privacy_policy_result_to_string(enum privacy_result result)
{
  switch (result)
    {
      case PRIVACY_OK:
        return "OK";
      case PRIVACY_ERR_NOT_AUTHORIZED:
        return "user not authorized";
      case PRIVACY_ERR_RAW_DATA:
        return "raw waveform data detected";
      case PRIVACY_ERR_LOCATION:
        return "location data detected";
      case PRIVACY_ERR_CONTACT:
        return "contact information detected";
      case PRIVACY_ERR_DIAGNOSIS:
        return "medical diagnosis language detected";
      case PRIVACY_ERR_RELATIONSHIP:
        return "relationship inference detected";
      case PRIVACY_ERR_TOKEN_BUDGET:
        return "daily token budget exhausted";
      case PRIVACY_ERR_INVALID_PARAM:
        return "invalid parameter";
      case PRIVACY_ERR_USER_NOTE_LEN:
        return "user note too long";
      default:
        return "unknown error";
    }
}
