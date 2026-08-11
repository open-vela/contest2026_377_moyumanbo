/****************************************************************************
 * VelaSense Mimo Local Fallback Templates
 *
 * Provides offline text generation when the Mimo cloud API is unavailable
 * (no network, token exhausted, API error).  Templates are simple
 * Chinese-language strings with runtime parameter substitution.
 *
 * Template placeholders:
 *   {time}      - Localised event time
 *   {label}     - Emotion label in Chinese
 *   {activity}  - Activity context in Chinese
 *   {count}     - Event count
 *   {dominant}  - Dominant label in Chinese
 *
 * All templates include a non-medical disclaimer prefix where
 * appropriate.
 ****************************************************************************/

#ifndef __FIRMWARE_APPS_VELASENSE_MIMO_TEMPLATES_H
#define __FIRMWARE_APPS_VELASENSE_MIMO_TEMPLATES_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>

#include "mimo_client.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MIMO_TPL_MAX_TEXT        512
#define MIMO_TPL_MAX_SUGGESTIONS 4
#define MIMO_TPL_MAX_HIGHLIGHTS  4

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Local template response for diary generation */

struct mimo_tpl_diary_response
{
  char diary_text[MIMO_TPL_MAX_TEXT];
  char mood_summary[256];
  int  suggestion_count;
  char suggestions[MIMO_TPL_MAX_SUGGESTIONS][256];
};

/* Local template response for weekly report */

struct mimo_tpl_weekly_response
{
  char report_text[MIMO_TPL_MAX_TEXT];
  int  highlight_count;
  char highlights[MIMO_TPL_MAX_HIGHLIGHTS][256];
  int  recommendation_count;
  char recommendations[MIMO_TPL_MAX_HIGHLIGHTS][256];
};

/* Local template response for breathing guide */

struct mimo_tpl_breath_response
{
  char guide_text[MIMO_TPL_MAX_TEXT];
  int  duration_sec;
  char pattern[64];
};

/* Local template response for alert explanation */

struct mimo_tpl_explain_response
{
  char explanation_text[MIMO_TPL_MAX_TEXT];
  char disclaimer[256];
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: mimo_tpl_init
 *
 * Description:
 *   Initialise the template subsystem.  Loads any custom templates
 *   from persistent storage.  Must be called once before any other
 *   mimo_tpl_* function.
 *
 ****************************************************************************/

void mimo_tpl_init(void);

/****************************************************************************
 * Name: mimo_tpl_generate_diary
 *
 * Description:
 *   Generate a local emotion diary from event summaries.
 *   Uses the format: "在 {time} 感到 {label}，当时正在{activity}。"
 *
 * Input Parameters:
 *   req - The diary request (same struct used for the cloud API).
 *
 * Output Parameters:
 *   resp - Filled with locally generated text.
 *
 ****************************************************************************/

void mimo_tpl_generate_diary(
    const struct mimo_diary_request *req,
    struct mimo_tpl_diary_response *resp);

/****************************************************************************
 * Name: mimo_tpl_generate_weekly_report
 *
 * Description:
 *   Generate a local weekly summary from daily statistics.
 *   Uses the format:
 *     "今天检测到 {count} 次情绪事件，以{dominant}为主。"
 *
 * Input Parameters:
 *   req - The weekly report request.
 *
 * Output Parameters:
 *   resp - Filled with locally generated text.
 *
 ****************************************************************************/

void mimo_tpl_generate_weekly_report(
    const struct mimo_weekly_request *req,
    struct mimo_tpl_weekly_response *resp);

/****************************************************************************
 * Name: mimo_tpl_generate_breath_guide
 *
 * Description:
 *   Generate a local breathing exercise guide.
 *   Uses the format:
 *     "建议进行 4-4-6 呼吸练习。吸气 4 秒，屏息 4 秒，呼气 6 秒。"
 *
 * Input Parameters:
 *   req - The breath guide request.
 *
 * Output Parameters:
 *   resp - Filled with locally generated text.
 *
 ****************************************************************************/

void mimo_tpl_generate_breath_guide(
    const struct mimo_breath_request *req,
    struct mimo_tpl_breath_response *resp);

/****************************************************************************
 * Name: mimo_tpl_generate_explanation
 *
 * Description:
 *   Generate a local alert explanation.
 *   Uses the format:
 *     "提醒您是因为心率和心率变异性出现了变化，不代表医学诊断。"
 *
 * Input Parameters:
 *   req - The alert explanation request.
 *
 * Output Parameters:
 *   resp - Filled with locally generated text.
 *
 ****************************************************************************/

void mimo_tpl_generate_explanation(
    const struct mimo_explain_request *req,
    struct mimo_tpl_explain_response *resp);

/****************************************************************************
 * Name: mimo_tpl_label_to_chinese
 *
 * Description:
 *   Convert an English event label string to its Chinese equivalent.
 *
 *   "stress"     -> "压力"
 *   "excitement" -> "心动"
 *   "nervous"    -> "紧张"
 *   "surprise"   -> "惊喜"
 *   other        -> "其他"
 *
 * Returned Value:
 *   Pointer to a static Chinese string.  Never returns NULL.
 *
 ****************************************************************************/

const char *mimo_tpl_label_to_chinese(const char *label_en);

/****************************************************************************
 * Name: mimo_tpl_activity_to_chinese
 *
 * Description:
 *   Convert an English activity string to its Chinese equivalent.
 *
 *   "sitting"  -> "坐着"
 *   "walking"  -> "散步"
 *   "running"  -> "跑步"
 *   "sleeping" -> "休息"
 *   other      -> "活动"
 *
 * Returned Value:
 *   Pointer to a static Chinese string.  Never returns NULL.
 *
 ****************************************************************************/

const char *mimo_tpl_activity_to_chinese(const char *activity_en);

/****************************************************************************
 * Name: mimo_tpl_hr_context_to_chinese
 *
 * Description:
 *   Convert an English HR context string to its Chinese equivalent.
 *
 *   "elevated" -> "偏高"
 *   "normal"   -> "正常"
 *   "low"      -> "偏低"
 *   other      -> "变化"
 *
 * Returned Value:
 *   Pointer to a static Chinese string.  Never returns NULL.
 *
 ****************************************************************************/

const char *mimo_tpl_hr_context_to_chinese(const char *hr_ctx_en);

#endif /* __FIRMWARE_APPS_VELASENSE_MIMO_TEMPLATES_H */
