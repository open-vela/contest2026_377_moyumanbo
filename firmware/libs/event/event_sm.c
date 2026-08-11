/****************************************************************************
 * VelaSense Event State Machine Implementation
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <string.h>
#include <time.h>
#include "event_sm.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void get_monotonic_time(struct timespec *ts)
{
  clock_gettime(CLOCK_MONOTONIC, ts);
}

static float time_diff_sec(const struct timespec *a,
                           const struct timespec *b)
{
  return (float)(a->tv_sec - b->tv_sec) +
         (float)(a->tv_nsec - b->tv_nsec) / 1e9f;
}

static int is_in_cooldown(const struct event_sm_ctx *ctx)
{
  struct timespec now;
  get_monotonic_time(&now);
  return time_diff_sec(&now, &ctx->last_event_time) < EVENT_COOLDOWN_SEC;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void event_sm_init(struct event_sm_ctx *ctx)
{
  memset(ctx, 0, sizeof(*ctx));
  ctx->state = EVENT_STATE_IDLE;
  get_monotonic_time(&ctx->state_enter_time);
}

enum event_state event_sm_update(struct event_sm_ctx *ctx,
                                 const struct event_inference_input *input)
{
  struct timespec now;
  get_monotonic_time(&now);

  if (!input)
    {
      return ctx->state;
    }

  switch (ctx->state)
    {
      case EVENT_STATE_IDLE:
        {
          /* Transition to MONITORING if conditions are met */

          if (input->sqi_ok && input->is_worn)
            {
              ctx->state = EVENT_STATE_MONITORING;
              ctx->state_enter_time = now;
            }
        }
        break;

      case EVENT_STATE_MONITORING:
        {
          /* Check if we should stop monitoring */

          if (!input->sqi_ok || !input->is_worn)
            {
              ctx->state = EVENT_STATE_IDLE;
              ctx->state_enter_time = now;
              break;
            }

          /* Check if confidence exceeds threshold → CANDIDATE */

          if (input->confidence >= EVENT_CONFIDENCE_THRESHOLD &&
              input->activity < 2.0f)  /* Not high-intensity activity */
            {
              ctx->state = EVENT_STATE_CANDIDATE;
              ctx->candidate_start = now;
              ctx->confidence_sum = input->confidence;
              ctx->confidence_count = 1;

              /* Build pending event */

              memset(&ctx->pending_event, 0, sizeof(ctx->pending_event));
              ctx->pending_event.confidence = input->confidence;
              ctx->pending_event.reason_flags = input->reason_flags;
              ctx->pending_event.hr = input->hr;
              ctx->pending_event.rmssd = input->rmssd;
              ctx->pending_event.activity_level = input->activity;
              ctx->state_enter_time = now;
            }
        }
        break;

      case EVENT_STATE_CANDIDATE:
        {
          /* Check if conditions still met */

          if (!input->sqi_ok || !input->is_worn ||
              input->activity >= 2.0f)
            {
              /* Conditions lost → back to MONITORING */

              ctx->state = EVENT_STATE_MONITORING;
              ctx->state_enter_time = now;
              break;
            }

          /* Accumulate confidence */

          if (input->confidence >= EVENT_CONFIDENCE_THRESHOLD)
            {
              ctx->confidence_sum += input->confidence;
              ctx->confidence_count++;

              /* Update pending event with latest values */

              ctx->pending_event.confidence =
                  ctx->confidence_sum / ctx->confidence_count;
              ctx->pending_event.hr = input->hr;
              ctx->pending_event.rmssd = input->rmssd;
            }
          else
            {
              /* Confidence dropped → back to MONITORING */

              ctx->state = EVENT_STATE_MONITORING;
              ctx->state_enter_time = now;
              break;
            }

          /* Check if candidate duration exceeds trigger threshold */

          float duration = time_diff_sec(&now, &ctx->candidate_start);
          if (duration >= EVENT_TRIGGER_SEC)
            {
              /* Check cooldown */

              if (is_in_cooldown(ctx))
                {
                  ctx->state = EVENT_STATE_COOLDOWN;
                  ctx->state_enter_time = now;
                }
              else
                {
                  /* Trigger alert! */

                  ctx->state = EVENT_STATE_ALERTING;
                  ctx->state_enter_time = now;

                  /* Record timestamp */

                  clock_gettime(CLOCK_REALTIME,
                                &ctx->pending_event.timestamp);
                  ctx->pending_event.seq = ++ctx->last_event_seq;
                }
            }
        }
        break;

      case EVENT_STATE_ALERTING:
        {
          /* Waiting for user confirmation.
           * Timeout after 60 seconds → auto-reject.
           */

          float duration = time_diff_sec(&now, &ctx->state_enter_time);
          if (duration > 60.0f)
            {
              /* Auto-reject after timeout */

              ctx->state = EVENT_STATE_REJECTED;
              ctx->state_enter_time = now;
            }
        }
        break;

      case EVENT_STATE_CONFIRMED:
      case EVENT_STATE_REJECTED:
        {
          /* Transition to COOLDOWN */

          ctx->last_event_time = now;
          ctx->state = EVENT_STATE_COOLDOWN;
          ctx->state_enter_time = now;
        }
        break;

      case EVENT_STATE_COOLDOWN:
        {
          /* Wait for cooldown period */

          float duration = time_diff_sec(&now, &ctx->last_event_time);
          if (duration >= EVENT_COOLDOWN_SEC)
            {
              ctx->state = EVENT_STATE_MONITORING;
              ctx->state_enter_time = now;
            }
        }
        break;
    }

  return ctx->state;
}

int event_sm_confirm(struct event_sm_ctx *ctx, enum event_label label)
{
  if (ctx->state != EVENT_STATE_ALERTING || label == EVENT_LABEL_NONE)
    {
      return -1;
    }

  ctx->pending_event.label = label;
  ctx->state = EVENT_STATE_CONFIRMED;
  get_monotonic_time(&ctx->state_enter_time);

  return 0;
}

int event_sm_reject(struct event_sm_ctx *ctx)
{
  if (ctx->state != EVENT_STATE_ALERTING)
    {
      return -1;
    }

  ctx->pending_event.label = EVENT_LABEL_FALSE_POSITIVE;
  ctx->state = EVENT_STATE_REJECTED;
  get_monotonic_time(&ctx->state_enter_time);

  return 0;
}

enum event_state event_sm_get_state(const struct event_sm_ctx *ctx)
{
  return ctx->state;
}

const struct event_record *event_sm_get_pending_event(
    const struct event_sm_ctx *ctx)
{
  if (ctx->state == EVENT_STATE_ALERTING ||
      ctx->state == EVENT_STATE_CONFIRMED)
    {
      return &ctx->pending_event;
    }

  return NULL;
}

const char *event_state_to_string(enum event_state state)
{
  switch (state)
    {
      case EVENT_STATE_IDLE:        return "IDLE";
      case EVENT_STATE_MONITORING:  return "MONITORING";
      case EVENT_STATE_CANDIDATE:   return "CANDIDATE";
      case EVENT_STATE_ALERTING:    return "ALERTING";
      case EVENT_STATE_CONFIRMED:   return "CONFIRMED";
      case EVENT_STATE_REJECTED:    return "REJECTED";
      case EVENT_STATE_COOLDOWN:    return "COOLDOWN";
      default:                      return "UNKNOWN";
    }
}

const char *event_label_to_string(enum event_label label)
{
  switch (label)
    {
      case EVENT_LABEL_NONE:          return "none";
      case EVENT_LABEL_EXCITEMENT:    return "excitement";
      case EVENT_LABEL_NERVOUS:       return "nervous";
      case EVENT_LABEL_SURPRISE:      return "surprise";
      case EVENT_LABEL_STRESS:        return "stress";
      case EVENT_LABEL_FALSE_POSITIVE: return "false_positive";
      default:                        return "unknown";
    }
}
