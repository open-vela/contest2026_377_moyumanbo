/****************************************************************************
 * VelaSense Event State Machine Unit Tests
 *
 * Tests for the event lifecycle state machine:
 *   IDLE → MONITORING → CANDIDATE → ALERTING → CONFIRMED/REJECTED
 *   COOLDOWN transitions and timeout handling
 *
 * Framework: CMocka
 ****************************************************************************/

#ifndef __FIRMWARE_TESTS_TEST_EVENT_SM_H
#define __FIRMWARE_TESTS_TEST_EVENT_SM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <time.h>

#include "../libs/event/event_sm.h"

/****************************************************************************
 * Test Function Prototypes
 ****************************************************************************/

void test_idle_to_monitoring(void **state);
void test_monitoring_to_candidate(void **state);
void test_candidate_to_alerting(void **state);
void test_alerting_to_confirmed(void **state);
void test_alerting_timeout(void **state);
void test_cooldown(void **state);
void test_false_positive_rejected(void **state);

/****************************************************************************
 * Helper Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: make_inference_input
 *
 * Description:
 *   Create an event_inference_input with the given parameters.
 *
 ****************************************************************************/

struct event_inference_input make_inference_input(float confidence,
                                                  uint32_t reason_flags,
                                                  float hr, float rmssd,
                                                  float activity,
                                                  int sqi_ok, int is_worn);

/****************************************************************************
 * Name: advance_time
 *
 * Description:
 *   Advance the state machine's internal timestamps by the given
 *   number of seconds. This simulates time passing without actually
 *   waiting, enabling deterministic tests.
 *
 ****************************************************************************/

void advance_time(struct event_sm_ctx *ctx, float seconds);

/****************************************************************************
 * Name: force_candidate_duration
 *
 * Description:
 *   Manipulate the candidate_start timestamp to simulate the given
 *   duration having elapsed since entering CANDIDATE state.
 *
 ****************************************************************************/

void force_candidate_duration(struct event_sm_ctx *ctx, float seconds);

/****************************************************************************
 * Name: force_alerting_duration
 *
 * Description:
 *   Manipulate the state_enter_time to simulate the given duration
 *   having elapsed since entering ALERTING state.
 *
 ****************************************************************************/

void force_alerting_duration(struct event_sm_ctx *ctx, float seconds);

/****************************************************************************
 * Name: force_cooldown_duration
 *
 * Description:
 *   Manipulate the last_event_time to simulate the given duration
 *   having elapsed since the last event.
 *
 ****************************************************************************/

void force_cooldown_duration(struct event_sm_ctx *ctx, float seconds);

#endif /* __FIRMWARE_TESTS_TEST_EVENT_SM_H */
