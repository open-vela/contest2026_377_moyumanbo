/****************************************************************************
 * VelaSense Privacy Policy Enforcement
 *
 * Validates and sanitises all data before it leaves the device.
 * This module is the single point of control for data upload policy.
 *
 * CRITICAL CONSTRAINTS (enforced at runtime):
 *   1. NO raw PPG/EDA waveforms may be uploaded
 *   2. NO medical diagnosis language in uploaded data
 *   3. NO relationship inference
 *   4. Only confirmed event summaries
 *   5. Aggregated statistics only (counts, trends)
 *   6. User-provided context notes are preserved as-is
 *   7. Precise location is stripped; only activity context remains
 *   8. Contact information is stripped
 *   9. Non-medical disclaimer is appended
 *  10. Daily token budget is checked
 *  11. Every upload is logged (without content) for audit
 *
 * Call privacy_policy_validate() before ANY network transmission.
 ****************************************************************************/

#ifndef __FIRMWARE_APPS_VELASENSE_PRIVACY_POLICY_H
#define __FIRMWARE_APPS_VELASENSE_PRIVACY_POLICY_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "mimo_client.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PRIVACY_MAX_LOG_ENTRIES    64
#define PRIVACY_MAX_NOTE_LEN       256
#define PRIVACY_DISCLAIMER \
  "本内容仅供参考，不构成医学诊断、治疗建议或健康指导。" \
  "如有健康疑虑，请咨询专业医疗人员。"

/* User authorisation flags (bit field) */

#define PRIVACY_AUTH_DIARY          (1 << 0)
#define PRIVACY_AUTH_WEEKLY_REPORT  (1 << 1)
#define PRIVACY_AUTH_BREATH_GUIDE   (1 << 2)
#define PRIVACY_AUTH_EXPLAIN_ALERT  (1 << 3)
#define PRIVACY_AUTH_ALL            0x0f

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Privacy check result codes */

enum privacy_result
{
  PRIVACY_OK = 0,             /* All checks passed */
  PRIVACY_ERR_NOT_AUTHORIZED, /* User has not authorized this upload type */
  PRIVACY_ERR_RAW_DATA,       /* Payload contains raw waveform data */
  PRIVACY_ERR_LOCATION,       /* Payload contains precise location */
  PRIVACY_ERR_CONTACT,        /* Payload contains contact information */
  PRIVACY_ERR_DIAGNOSIS,      /* Payload contains medical diagnosis */
  PRIVACY_ERR_RELATIONSHIP,   /* Payload contains relationship inference */
  PRIVACY_ERR_TOKEN_BUDGET,   /* Daily token budget exhausted */
  PRIVACY_ERR_INVALID_PARAM,  /* NULL or invalid parameter */
  PRIVACY_ERR_USER_NOTE_LEN   /* User note exceeds maximum length */
};

/* Upload type for authorization checks */

enum privacy_upload_type
{
  PRIVACY_UPLOAD_DIARY = 0,
  PRIVACY_UPLOAD_WEEKLY_REPORT,
  PRIVACY_UPLOAD_BREATH_GUIDE,
  PRIVACY_UPLOAD_EXPLAIN_ALERT
};

/* Audit log entry (content-free) */

struct privacy_audit_entry
{
  time_t  timestamp;          /* When the upload occurred */
  int     upload_type;        /* privacy_upload_type */
  int     event_count;        /* Number of events in payload */
  bool    had_user_note;      /* Whether user note was included */
  bool    passed;             /* Whether validation passed */
  int     reject_reason;      /* privacy_result if rejected */
};

/* Privacy policy context */

struct privacy_policy_ctx
{
  uint32_t auth_flags;        /* Bitmask of PRIVACY_AUTH_* flags */
  int      daily_token_count; /* Tokens used today */
  int      daily_token_limit; /* Max tokens per day */
  time_t   last_reset;        /* When daily counter was last reset */
  int      log_count;         /* Number of audit log entries */
  int      log_head;          /* Circular buffer head index */
  struct privacy_audit_entry log[PRIVACY_MAX_LOG_ENTRIES];
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: privacy_policy_init
 *
 * Description:
 *   Initialise the privacy policy engine.  Must be called once before
 *   any other privacy_policy_* function.  Loads user authorization
 *   settings from persistent storage.
 *
 * Output Parameters:
 *   ctx - Initialised policy context.
 *
 ****************************************************************************/

void privacy_policy_init(struct privacy_policy_ctx *ctx);

/****************************************************************************
 * Name: privacy_policy_set_authorization
 *
 * Description:
 *   Set or clear the user's authorization for a specific upload type.
 *   Authorization must be explicitly granted by the user through the
 *   phone-side UI.
 *
 * Input Parameters:
 *   ctx   - Policy context.
 *   type  - Upload type to authorize/deauthorize.
 *   grant - true to grant, false to revoke.
 *
 ****************************************************************************/

void privacy_policy_set_authorization(struct privacy_policy_ctx *ctx,
                                      enum privacy_upload_type type,
                                      bool grant);

/****************************************************************************
 * Name: privacy_policy_is_authorized
 *
 * Description:
 *   Check whether the user has authorized a specific upload type.
 *
 * Returned Value:
 *   true if authorized, false otherwise.
 *
 ****************************************************************************/

bool privacy_policy_is_authorized(const struct privacy_policy_ctx *ctx,
                                  enum privacy_upload_type type);

/****************************************************************************
 * Name: privacy_policy_validate_diary
 *
 * Description:
 *   Validate a diary request payload against all privacy constraints.
 *   This is the primary entry point for diary uploads.
 *
 * Input Parameters:
 *   ctx - Policy context.
 *   req - Diary request to validate.
 *
 * Returned Value:
 *   PRIVACY_OK if all checks pass, or the specific error code.
 *
 ****************************************************************************/

enum privacy_result privacy_policy_validate_diary(
    struct privacy_policy_ctx *ctx,
    const struct mimo_diary_request *req);

/****************************************************************************
 * Name: privacy_policy_validate_weekly
 *
 * Description:
 *   Validate a weekly report request payload.
 *
 * Input Parameters:
 *   ctx - Policy context.
 *   req - Weekly report request to validate.
 *
 * Returned Value:
 *   PRIVACY_OK if all checks pass, or the specific error code.
 *
 ****************************************************************************/

enum privacy_result privacy_policy_validate_weekly(
    struct privacy_policy_ctx *ctx,
    const struct mimo_weekly_request *req);

/****************************************************************************
 * Name: privacy_policy_validate_breath
 *
 * Description:
 *   Validate a breath guide request payload.
 *
 * Input Parameters:
 *   ctx - Policy context.
 *   req - Breath guide request to validate.
 *
 * Returned Value:
 *   PRIVACY_OK if all checks pass, or the specific error code.
 *
 ****************************************************************************/

enum privacy_result privacy_policy_validate_breath(
    struct privacy_policy_ctx *ctx,
    const struct mimo_breath_request *req);

/****************************************************************************
 * Name: privacy_policy_validate_explain
 *
 * Description:
 *   Validate an explain-alert request payload.
 *
 * Input Parameters:
 *   ctx - Policy context.
 *   req - Explain request to validate.
 *
 * Returned Value:
 *   PRIVACY_OK if all checks pass, or the specific error code.
 *
 ****************************************************************************/

enum privacy_result privacy_policy_validate_explain(
    struct privacy_policy_ctx *ctx,
    const struct mimo_explain_request *req);

/****************************************************************************
 * Name: privacy_policy_check_token_budget
 *
 * Description:
 *   Check whether the daily token budget allows another upload.
 *   If the day has rolled over, the counter is reset automatically.
 *
 * Input Parameters:
 *   ctx - Policy context.
 *
 * Returned Value:
 *   true if tokens are available, false if budget is exhausted.
 *
 ****************************************************************************/

bool privacy_policy_check_token_budget(struct privacy_policy_ctx *ctx);

/****************************************************************************
 * Name: privacy_policy_consume_token
 *
 * Description:
 *   Consume one token from the daily budget.  Call after a successful
 *   upload.
 *
 * Input Parameters:
 *   ctx - Policy context.
 *
 ****************************************************************************/

void privacy_policy_consume_token(struct privacy_policy_ctx *ctx);

/****************************************************************************
 * Name: privacy_policy_log_upload
 *
 * Description:
 *   Record an upload attempt in the audit log (content-free).
 *
 * Input Parameters:
 *   ctx          - Policy context.
 *   type         - Upload type.
 *   event_count  - Number of events in the payload.
 *   had_note     - Whether a user note was included.
 *   passed       - Whether validation passed.
 *   reject_code  - Rejection reason (if passed == false).
 *
 ****************************************************************************/

void privacy_policy_log_upload(struct privacy_policy_ctx *ctx,
                               enum privacy_upload_type type,
                               int event_count,
                               bool had_note,
                               bool passed,
                               enum privacy_result reject_code);

/****************************************************************************
 * Name: privacy_policy_get_log_count
 *
 * Description:
 *   Get the number of audit log entries.
 *
 * Returned Value:
 *   Number of entries in the audit log.
 *
 ****************************************************************************/

int privacy_policy_get_log_count(const struct privacy_policy_ctx *ctx);

/****************************************************************************
 * Name: privacy_policy_get_log_entry
 *
 * Description:
 *   Retrieve an audit log entry by index (0 = oldest).
 *
 * Input Parameters:
 *   ctx   - Policy context.
 *   index - Entry index (0 to log_count-1).
 *
 * Output Parameters:
 *   entry - Filled with the log entry data.
 *
 * Returned Value:
 *   true if the entry was retrieved, false if index is out of range.
 *
 ****************************************************************************/

bool privacy_policy_get_log_entry(const struct privacy_policy_ctx *ctx,
                                  int index,
                                  struct privacy_audit_entry *entry);

/****************************************************************************
 * Name: privacy_policy_strip_user_note
 *
 * Description:
 *   Sanitise a user note by:
 *   - Truncating to PRIVACY_MAX_NOTE_LEN
 *   - Removing any potential contact info patterns (phone numbers,
 *     email addresses)
 *   - Removing any potential location data
 *
 *   The note is modified in-place.
 *
 * Input Parameters:
 *   note - User note string to sanitise.
 *
 * Returned Value:
 *   PRIVACY_OK on success, PRIVACY_ERR_USER_NOTE_LEN if truncated.
 *
 ****************************************************************************/

enum privacy_result privacy_policy_strip_user_note(char *note);

/****************************************************************************
 * Name: privacy_policy_result_to_string
 *
 * Description:
 *   Convert a privacy_result code to a human-readable string.
 *
 ****************************************************************************/

const char *privacy_policy_result_to_string(enum privacy_result result);

#endif /* __FIRMWARE_APPS_VELASENSE_PRIVACY_POLICY_H */
