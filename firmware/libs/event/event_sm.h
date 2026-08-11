/****************************************************************************
 * VelaSense Event State Machine
 *
 * Manages the lifecycle of emotion arousal events:
 *
 *   IDLE → MONITORING → CANDIDATE → ALERTING → CONFIRMED/REJECTED
 *            ↑                                |
 *            └──────── COOLDOWN ←─────────────┘
 *
 * States:
 *   IDLE        - Not monitoring (not worn / SQI too low)
 *   MONITORING  - Actively running inference
 *   CANDIDATE   - Confidence > threshold for 15s
 *   ALERTING    - Vibration + LCD alert, waiting for user
 *   CONFIRMED   - User selected a label
 *   REJECTED    - User marked as false positive
 *   COOLDOWN    - Post-event cooldown period
 *
 * Privacy: Events are stored as encrypted summaries only.
 * Raw waveforms are never persisted.
 ****************************************************************************/

#ifndef __FIRMWARE_LIBS_EVENT_EVENT_SM_H
#define __FIRMWARE_LIBS_EVENT_EVENT_SM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <time.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define EVENT_LABEL_MAX_LEN   16
#define EVENT_REASON_MAX_LEN  32
#define EVENT_COOLDOWN_SEC    300  /* 5 minutes default */
#define EVENT_TRIGGER_SEC     15   /* 15 seconds continuous */
#define EVENT_CONFIDENCE_THRESHOLD 0.82f

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Event states */

enum event_state
{
  EVENT_STATE_IDLE = 0,
  EVENT_STATE_MONITORING,
  EVENT_STATE_CANDIDATE,
  EVENT_STATE_ALERTING,
  EVENT_STATE_CONFIRMED,
  EVENT_STATE_REJECTED,
  EVENT_STATE_COOLDOWN
};

/* Event labels (user-confirmed) */

enum event_label
{
  EVENT_LABEL_NONE = 0,
  EVENT_LABEL_EXCITEMENT,    /* 心动 */
  EVENT_LABEL_NERVOUS,       /* 紧张 */
  EVENT_LABEL_SURPRISE,      /* 惊喜 */
  EVENT_LABEL_STRESS,        /* 压力 */
  EVENT_LABEL_FALSE_POSITIVE /* 误报 */
};

/* Arousal reason codes (bit flags, combinable) */

enum event_reason
{
  EVENT_REASON_HR_RISE       = (1 << 0),  /* Heart rate increase */
  EVENT_REASON_HRV_DROP      = (1 << 1),  /* HRV decrease (stress) */
  EVENT_REASON_EDA_SPIKE     = (1 << 2),  /* EDA response */
  EVENT_REASON_TEMP_CHANGE   = (1 << 3),  /* Skin temperature change */
  EVENT_REASON_ACTIVITY_REST = (1 << 4),  /* Confirmed at rest */
};

/* Inference input (from inference task) */

struct event_inference_input
{
  float    confidence;    /* Arousal probability 0.0-1.0 */
  uint32_t reason_flags;  /* Combination of event_reason flags */
  float    hr;            /* Current heart rate */
  float    rmssd;         /* Current RMSSD */
  float    activity;      /* Activity level 0.0-3.0 */
  int      sqi_ok;        /* 1 if SQI above threshold */
  int      is_worn;       /* 1 if device is worn */
};

/* Confirmed event record (for storage) */

struct event_record
{
  struct timespec timestamp;      /* Event time */
  enum event_label label;         /* User-confirmed label */
  float           confidence;     /* Inference confidence */
  uint32_t        reason_flags;   /* Reason codes */
  float           hr;             /* HR at event time */
  float           rmssd;          /* RMSSD at event time */
  float           activity_level; /* Activity at event time */
  uint32_t        seq;            /* Sequence number */
};

/* State machine context */

struct event_sm_ctx
{
  enum event_state    state;
  struct timespec     state_enter_time;   /* When current state entered */
  struct timespec     candidate_start;    /* When candidate state started */
  float               confidence_sum;     /* Running confidence in candidate */
  int                 confidence_count;   /* Samples in candidate state */
  uint32_t            last_event_seq;     /* Last event sequence number */
  struct timespec     last_event_time;    /* For cooldown */
  struct event_record pending_event;      /* Event being built */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: event_sm_init
 *
 * Description:
 *   Initialize the event state machine.
 *
 ****************************************************************************/

void event_sm_init(struct event_sm_ctx *ctx);

/****************************************************************************
 * Name: event_sm_update
 *
 * Description:
 *   Feed new inference results into the state machine.
 *   Returns the current state after update.
 *
 * Input Parameters:
 *   ctx   - State machine context
 *   input - Latest inference results
 *
 * Returned Value:
 *   Current state after update
 *
 ****************************************************************************/

enum event_state event_sm_update(struct event_sm_ctx *ctx,
                                 const struct event_inference_input *input);

/****************************************************************************
 * Name: event_sm_confirm
 *
 * Description:
 *   User confirms the event with a label.
 *   Transitions from ALERTING to CONFIRMED.
 *
 ****************************************************************************/

int event_sm_confirm(struct event_sm_ctx *ctx, enum event_label label);

/****************************************************************************
 * Name: event_sm_reject
 *
 * Description:
 *   User rejects the event as false positive.
 *   Transitions from ALERTING to REJECTED.
 *
 ****************************************************************************/

int event_sm_reject(struct event_sm_ctx *ctx);

/****************************************************************************
 * Name: event_sm_get_state
 *
 * Description:
 *   Get current state.
 *
 ****************************************************************************/

enum event_state event_sm_get_state(const struct event_sm_ctx *ctx);

/****************************************************************************
 * Name: event_sm_get_pending_event
 *
 * Description:
 *   Get the pending event record (valid in ALERTING state).
 *
 ****************************************************************************/

const struct event_record *event_sm_get_pending_event(
    const struct event_sm_ctx *ctx);

/****************************************************************************
 * Name: event_state_to_string
 *
 * Description:
 *   Convert state enum to human-readable string.
 *
 ****************************************************************************/

const char *event_state_to_string(enum event_state state);

/****************************************************************************
 * Name: event_label_to_string
 *
 * Description:
 *   Convert label enum to human-readable string.
 *
 ****************************************************************************/

const char *event_label_to_string(enum event_label label);

#endif /* __FIRMWARE_LIBS_EVENT_EVENT_SM_H */
