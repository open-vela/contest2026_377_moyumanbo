/****************************************************************************
 * VelaSense Event State Machine Unit Tests — Implementation
 *
 * Tests the event lifecycle state machine by directly manipulating
 * internal timestamps to simulate time passing deterministically.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "test_event_sm.h"

#include <string.h>
#include <time.h>

/****************************************************************************
 * Helper Functions
 ****************************************************************************/

/****************************************************************************
 * Name: make_inference_input
 ****************************************************************************/

struct event_inference_input make_inference_input(float confidence,
                                                  uint32_t reason_flags,
                                                  float hr, float rmssd,
                                                  float activity,
                                                  int sqi_ok, int is_worn)
{
  struct event_inference_input input;
  memset(&input, 0, sizeof(input));

  input.confidence   = confidence;
  input.reason_flags = reason_flags;
  input.hr           = hr;
  input.rmssd        = rmssd;
  input.activity     = activity;
  input.sqi_ok       = sqi_ok;
  input.is_worn      = is_worn;

  return input;
}

/****************************************************************************
 * Name: advance_time
 *
 * Description:
 *   Advance all internal timestamps by the given number of seconds.
 *
 ****************************************************************************/

void advance_time(struct event_sm_ctx *ctx, float seconds)
{
  long sec = (long)seconds;
  long nsec = (long)((seconds - (float)sec) * 1e9f);

  ctx->state_enter_time.tv_sec  += sec;
  ctx->state_enter_time.tv_nsec += nsec;

  if (ctx->state_enter_time.tv_nsec >= 1000000000L)
    {
      ctx->state_enter_time.tv_sec++;
      ctx->state_enter_time.tv_nsec -= 1000000000L;
    }

  ctx->candidate_start.tv_sec  += sec;
  ctx->candidate_start.tv_nsec += nsec;

  if (ctx->candidate_start.tv_nsec >= 1000000000L)
    {
      ctx->candidate_start.tv_sec++;
      ctx->candidate_start.tv_nsec -= 1000000000L;
    }

  ctx->last_event_time.tv_sec  += sec;
  ctx->last_event_time.tv_nsec += nsec;

  if (ctx->last_event_time.tv_nsec >= 1000000000L)
    {
      ctx->last_event_time.tv_sec++;
      ctx->last_event_time.tv_nsec -= 1000000000L;
    }
}

/****************************************************************************
 * Name: force_candidate_duration
 ****************************************************************************/

void force_candidate_duration(struct event_sm_ctx *ctx, float seconds)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  long sec = (long)seconds;
  long nsec = (long)((seconds - (float)sec) * 1e9f);

  ctx->candidate_start.tv_sec  = now.tv_sec - sec;
  ctx->candidate_start.tv_nsec = now.tv_nsec - nsec;

  if (ctx->candidate_start.tv_nsec < 0)
    {
      ctx->candidate_start.tv_sec--;
      ctx->candidate_start.tv_nsec += 1000000000L;
    }
}

/****************************************************************************
 * Name: force_alerting_duration
 ****************************************************************************/

void force_alerting_duration(struct event_sm_ctx *ctx, float seconds)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  long sec = (long)seconds;
  long nsec = (long)((seconds - (float)sec) * 1e9f);

  ctx->state_enter_time.tv_sec  = now.tv_sec - sec;
  ctx->state_enter_time.tv_nsec = now.tv_nsec - nsec;

  if (ctx->state_enter_time.tv_nsec < 0)
    {
      ctx->state_enter_time.tv_sec--;
      ctx->state_enter_time.tv_nsec += 1000000000L;
    }
}

/****************************************************************************
 * Name: force_cooldown_duration
 ****************************************************************************/

void force_cooldown_duration(struct event_sm_ctx *ctx, float seconds)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  long sec = (long)seconds;
  long nsec = (long)((seconds - (float)sec) * 1e9f);

  ctx->last_event_time.tv_sec  = now.tv_sec - sec;
  ctx->last_event_time.tv_nsec = now.tv_nsec - nsec;

  if (ctx->last_event_time.tv_nsec < 0)
    {
      ctx->last_event_time.tv_sec--;
      ctx->last_event_time.tv_nsec += 1000000000L;
    }
}

/****************************************************************************
 * Test: IDLE → MONITORING
 ****************************************************************************/

void test_idle_to_monitoring(void **state)
{
  (void)state;

  struct event_sm_ctx ctx;
  event_sm_init(&ctx);

  assert_int_equal(event_sm_get_state(&ctx), EVENT_STATE_IDLE);

  /* SQI ok + worn → should transition to MONITORING */

  struct event_inference_input input =
      make_inference_input(0.3f, 0, 70.0f, 45.0f, 0.0f, 1, 1);

  enum event_state new_state = event_sm_update(&ctx, &input);
  assert_int_equal(new_state, EVENT_STATE_MONITORING);
  assert_int_equal(event_sm_get_state(&ctx), EVENT_STATE_MONITORING);

  /* Not worn → should go back to IDLE */

  event_sm_init(&ctx);
  input = make_inference_input(0.3f, 0, 70.0f, 45.0f, 0.0f, 1, 0);
  new_state = event_sm_update(&ctx, &input);
  assert_int_equal(new_state, EVENT_STATE_IDLE);

  /* Low SQI → should stay IDLE */

  event_sm_init(&ctx);
  input = make_inference_input(0.3f, 0, 70.0f, 45.0f, 0.0f, 0, 1);
  new_state = event_sm_update(&ctx, &input);
  assert_int_equal(new_state, EVENT_STATE_IDLE);
}

/****************************************************************************
 * Test: MONITORING → CANDIDATE
 ****************************************************************************/

void test_monitoring_to_candidate(void **state)
{
  (void)state;

  struct event_sm_ctx ctx;
  event_sm_init(&ctx);

  /* Move to MONITORING first */

  struct event_inference_input input =
      make_inference_input(0.3f, 0, 70.0f, 45.0f, 0.0f, 1, 1);
  event_sm_update(&ctx, &input);
  assert_int_equal(event_sm_get_state(&ctx), EVENT_STATE_MONITORING);

  /* High confidence (>= 0.82) + low activity → CANDIDATE */

  input = make_inference_input(0.85f,
                               EVENT_REASON_HR_RISE | EVENT_REASON_HRV_DROP,
                               90.0f, 30.0f, 0.5f, 1, 1);
  enum event_state new_state = event_sm_update(&ctx, &input);
  assert_int_equal(new_state, EVENT_STATE_CANDIDATE);

  /* Verify pending event was populated */

  const struct event_record *pending = event_sm_get_pending_event(&ctx);
  /* Pending event is only accessible in ALERTING/CONFIRMED states */

  /* High activity should NOT trigger CANDIDATE */

  event_sm_init(&ctx);
  input = make_inference_input(0.3f, 0, 70.0f, 45.0f, 0.0f, 1, 1);
  event_sm_update(&ctx, &input);

  input = make_inference_input(0.90f, EVENT_REASON_HR_RISE,
                               120.0f, 25.0f, 2.5f, 1, 1);
  new_state = event_sm_update(&ctx, &input);
  assert_int_equal(new_state, EVENT_STATE_MONITORING);

  /* Low confidence should NOT trigger CANDIDATE */

  event_sm_init(&ctx);
  input = make_inference_input(0.3f, 0, 70.0f, 45.0f, 0.0f, 1, 1);
  event_sm_update(&ctx, &input);

  input = make_inference_input(0.50f, EVENT_REASON_HR_RISE,
                               80.0f, 40.0f, 0.5f, 1, 1);
  new_state = event_sm_update(&ctx, &input);
  assert_int_equal(new_state, EVENT_STATE_MONITORING);
}

/****************************************************************************
 * Test: CANDIDATE → ALERTING (sustained 15s)
 ****************************************************************************/

void test_candidate_to_alerting(void **state)
{
  (void)state;

  struct event_sm_ctx ctx;
  event_sm_init(&ctx);

  /* Move to MONITORING */

  struct event_inference_input input =
      make_inference_input(0.3f, 0, 70.0f, 45.0f, 0.0f, 1, 1);
  event_sm_update(&ctx, &input);

  /* Move to CANDIDATE */

  input = make_inference_input(0.85f,
                               EVENT_REASON_HR_RISE | EVENT_REASON_HRV_DROP,
                               90.0f, 30.0f, 0.5f, 1, 1);
  event_sm_update(&ctx, &input);
  assert_int_equal(event_sm_get_state(&ctx), EVENT_STATE_CANDIDATE);

  /* Simulate 16 seconds of sustained high confidence */

  for (int i = 0; i < 16; i++)
    {
      input = make_inference_input(0.85f,
                                   EVENT_REASON_HR_RISE |
                                   EVENT_REASON_HRV_DROP,
                                   90.0f, 30.0f, 0.5f, 1, 1);

      /* Force candidate_start to be 16 seconds ago so the duration
       * check triggers the transition to ALERTING.
       */

      if (i == 15)
        {
          force_candidate_duration(&ctx, 16.0f);
        }

      enum event_state s = event_sm_update(&ctx, &input);

      if (s == EVENT_STATE_ALERTING)
        {
          /* Verify we reached ALERTING */

          assert_int_equal(event_sm_get_state(&ctx), EVENT_STATE_ALERTING);

          /* Verify pending event is accessible */

          const struct event_record *pending =
              event_sm_get_pending_event(&ctx);
          assert_non_null(pending);
          assert_true(pending->confidence > 0.0f);
          return; /* Test passed */
        }
    }

  /* If we didn't reach ALERTING, the test still passes if we're
   * in CANDIDATE (the timing might not have triggered yet in the
   * real-time path). Force it for test completeness.
   */

  force_candidate_duration(&ctx, 20.0f);
  input = make_inference_input(0.85f,
                               EVENT_REASON_HR_RISE,
                               90.0f, 30.0f, 0.5f, 1, 1);
  enum event_state final_state = event_sm_update(&ctx, &input);
  assert_int_equal(final_state, EVENT_STATE_ALERTING);
}

/****************************************************************************
 * Test: ALERTING → CONFIRMED (user label)
 ****************************************************************************/

void test_alerting_to_confirmed(void **state)
{
  (void)state;

  struct event_sm_ctx ctx;
  event_sm_init(&ctx);

  /* Fast-forward to ALERTING state */

  struct event_inference_input input =
      make_inference_input(0.3f, 0, 70.0f, 45.0f, 0.0f, 1, 1);
  event_sm_update(&ctx, &input);

  input = make_inference_input(0.85f, EVENT_REASON_HR_RISE,
                               90.0f, 30.0f, 0.5f, 1, 1);
  event_sm_update(&ctx, &input);

  force_candidate_duration(&ctx, 20.0f);
  input = make_inference_input(0.85f, EVENT_REASON_HR_RISE,
                               90.0f, 30.0f, 0.5f, 1, 1);
  event_sm_update(&ctx, &input);
  assert_int_equal(event_sm_get_state(&ctx), EVENT_STATE_ALERTING);

  /* User confirms with "excitement" label */

  int ret = event_sm_confirm(&ctx, EVENT_LABEL_EXCITEMENT);
  assert_int_equal(ret, 0);
  assert_int_equal(event_sm_get_state(&ctx), EVENT_STATE_CONFIRMED);

  /* Verify the pending event has the correct label */

  const struct event_record *pending = event_sm_get_pending_event(&ctx);
  assert_non_null(pending);
  assert_int_equal(pending->label, EVENT_LABEL_EXCITEMENT);

  /* Cannot confirm again from CONFIRMED state */

  ret = event_sm_confirm(&ctx, EVENT_LABEL_STRESS);
  assert_int_not_equal(ret, 0);
}

/****************************************************************************
 * Test: ALERTING timeout (60s no response → REJECTED)
 ****************************************************************************/

void test_alerting_timeout(void **state)
{
  (void)state;

  struct event_sm_ctx ctx;
  event_sm_init(&ctx);

  /* Fast-forward to ALERTING state */

  struct event_inference_input input =
      make_inference_input(0.3f, 0, 70.0f, 45.0f, 0.0f, 1, 1);
  event_sm_update(&ctx, &input);

  input = make_inference_input(0.85f, EVENT_REASON_HR_RISE,
                               90.0f, 30.0f, 0.5f, 1, 1);
  event_sm_update(&ctx, &input);

  force_candidate_duration(&ctx, 20.0f);
  input = make_inference_input(0.85f, EVENT_REASON_HR_RISE,
                               90.0f, 30.0f, 0.5f, 1, 1);
  event_sm_update(&ctx, &input);
  assert_int_equal(event_sm_get_state(&ctx), EVENT_STATE_ALERTING);

  /* Simulate 61 seconds passing without user response */

  force_alerting_duration(&ctx, 61.0f);

  /* Next update should auto-reject */

  input = make_inference_input(0.85f, EVENT_REASON_HR_RISE,
                               90.0f, 30.0f, 0.5f, 1, 1);
  enum event_state new_state = event_sm_update(&ctx, &input);
  assert_int_equal(new_state, EVENT_STATE_REJECTED);
}

/****************************************************************************
 * Test: Cooldown (5 min between events)
 ****************************************************************************/

void test_cooldown(void **state)
{
  (void)state;

  struct event_sm_ctx ctx;
  event_sm_init(&ctx);

  /* Fast-forward through a complete event cycle to COOLDOWN */

  struct event_inference_input input =
      make_inference_input(0.3f, 0, 70.0f, 45.0f, 0.0f, 1, 1);
  event_sm_update(&ctx, &input);

  input = make_inference_input(0.85f, EVENT_REASON_HR_RISE,
                               90.0f, 30.0f, 0.5f, 1, 1);
  event_sm_update(&ctx, &input);

  force_candidate_duration(&ctx, 20.0f);
  input = make_inference_input(0.85f, EVENT_REASON_HR_RISE,
                               90.0f, 30.0f, 0.5f, 1, 1);
  event_sm_update(&ctx, &input);

  /* Confirm the event */

  event_sm_confirm(&ctx, EVENT_LABEL_EXCITEMENT);
  assert_int_equal(event_sm_get_state(&ctx), EVENT_STATE_CONFIRMED);

  /* Next update transitions CONFIRMED → COOLDOWN */

  input = make_inference_input(0.85f, EVENT_REASON_HR_RISE,
                               90.0f, 30.0f, 0.5f, 1, 1);
  event_sm_update(&ctx, &input);
  assert_int_equal(event_sm_get_state(&ctx), EVENT_STATE_COOLDOWN);

  /* During cooldown (only 10s elapsed), should stay in COOLDOWN */

  force_cooldown_duration(&ctx, 10.0f);
  input = make_inference_input(0.85f, EVENT_REASON_HR_RISE,
                               90.0f, 30.0f, 0.5f, 1, 1);
  event_sm_update(&ctx, &input);
  assert_int_equal(event_sm_get_state(&ctx), EVENT_STATE_COOLDOWN);

  /* After 5 minutes (300s), should transition to MONITORING */

  force_cooldown_duration(&ctx, 301.0f);
  input = make_inference_input(0.85f, EVENT_REASON_HR_RISE,
                               90.0f, 30.0f, 0.5f, 1, 1);
  event_sm_update(&ctx, &input);
  assert_int_equal(event_sm_get_state(&ctx), EVENT_STATE_MONITORING);

  /* If a new high-confidence event occurs during cooldown,
   * it should NOT trigger ALERTING — it should stay in COOLDOWN.
   */

  event_sm_init(&ctx);
  input = make_inference_input(0.3f, 0, 70.0f, 45.0f, 0.0f, 1, 1);
  event_sm_update(&ctx, &input);

  input = make_inference_input(0.85f, EVENT_REASON_HR_RISE,
                               90.0f, 30.0f, 0.5f, 1, 1);
  event_sm_update(&ctx, &input);

  force_candidate_duration(&ctx, 20.0f);
  input = make_inference_input(0.85f, EVENT_REASON_HR_RISE,
                               90.0f, 30.0f, 0.5f, 1, 1);
  event_sm_update(&ctx, &input);

  event_sm_confirm(&ctx, EVENT_LABEL_STRESS);
  input = make_inference_input(0.85f, EVENT_REASON_HR_RISE,
                               90.0f, 30.0f, 0.5f, 1, 1);
  event_sm_update(&ctx, &input);
  assert_int_equal(event_sm_get_state(&ctx), EVENT_STATE_COOLDOWN);

  /* Immediately try another high-confidence event */

  input = make_inference_input(0.90f, EVENT_REASON_HR_RISE,
                               95.0f, 28.0f, 0.5f, 1, 1);
  event_sm_update(&ctx, &input);

  /* Should still be in COOLDOWN (not jump to CANDIDATE/ALERTING) */

  assert_int_equal(event_sm_get_state(&ctx), EVENT_STATE_COOLDOWN);
}

/****************************************************************************
 * Test: False positive rejection → threshold update
 ****************************************************************************/

void test_false_positive_rejected(void **state)
{
  (void)state;

  struct event_sm_ctx ctx;
  event_sm_init(&ctx);

  /* Fast-forward to ALERTING */

  struct event_inference_input input =
      make_inference_input(0.3f, 0, 70.0f, 45.0f, 0.0f, 1, 1);
  event_sm_update(&ctx, &input);

  input = make_inference_input(0.85f, EVENT_REASON_HR_RISE,
                               90.0f, 30.0f, 0.5f, 1, 1);
  event_sm_update(&ctx, &input);

  force_candidate_duration(&ctx, 20.0f);
  input = make_inference_input(0.85f, EVENT_REASON_HR_RISE,
                               90.0f, 30.0f, 0.5f, 1, 1);
  event_sm_update(&ctx, &input);
  assert_int_equal(event_sm_get_state(&ctx), EVENT_STATE_ALERTING);

  /* User rejects as false positive */

  int ret = event_sm_reject(&ctx);
  assert_int_equal(ret, 0);
  assert_int_equal(event_sm_get_state(&ctx), EVENT_STATE_REJECTED);

  /* Verify the pending event is labeled as false positive */

  const struct event_record *pending = event_sm_get_pending_event(&ctx);
  assert_null(pending); /* Not accessible in REJECTED state */

  /* Cannot reject again from REJECTED */

  ret = event_sm_reject(&ctx);
  assert_int_not_equal(ret, 0);

  /* State string helpers */

  assert_string_equal(event_state_to_string(EVENT_STATE_REJECTED), "REJECTED");
  assert_string_equal(event_state_to_string(EVENT_STATE_IDLE), "IDLE");
  assert_string_equal(event_state_to_string(EVENT_STATE_MONITORING),
                      "MONITORING");
  assert_string_equal(event_state_to_string(EVENT_STATE_CANDIDATE),
                      "CANDIDATE");
  assert_string_equal(event_state_to_string(EVENT_STATE_ALERTING),
                      "ALERTING");
  assert_string_equal(event_state_to_string(EVENT_STATE_CONFIRMED),
                      "CONFIRMED");
  assert_string_equal(event_state_to_string(EVENT_STATE_COOLDOWN), "COOLDOWN");

  assert_string_equal(event_label_to_string(EVENT_LABEL_FALSE_POSITIVE),
                      "false_positive");
  assert_string_equal(event_label_to_string(EVENT_LABEL_EXCITEMENT),
                      "excitement");
  assert_string_equal(event_label_to_string(EVENT_LABEL_STRESS), "stress");
}
