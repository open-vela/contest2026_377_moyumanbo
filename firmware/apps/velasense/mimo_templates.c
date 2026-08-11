/****************************************************************************
 * VelaSense Mimo Local Fallback Templates - Implementation
 *
 * Offline text generation when the Mimo cloud API is unavailable.
 * All output is Chinese-language with non-medical disclaimers.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <syslog.h>

#include "mimo_templates.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TAG "mimo_tpl"

/* Template format strings (Chinese) */

#define TPL_DIARY_EVENT \
  "在 %s 感到%s，当时正在%s。"

#define TPL_DIARY_DISCLAIMER \
  "以上内容由本地模板生成，仅供参考，不代表医学诊断。"

#define TPL_DAILY_SUMMARY \
  "今天检测到 %d 次情绪事件，以%s为主。"

#define TPL_WEEKLY_INTRO \
  "本周情绪报告（本地生成）："

#define TPL_WEEKLY_DAY \
  "  · 第%d天：%d 次事件，主要为%s。"

#define TPL_WEEKLY_DISCLAIMER \
  "本报告由本地模板生成，仅供参考，不代表医学诊断。"

#define TPL_BREATH_GUIDE \
  "建议进行 4-4-6 呼吸练习。吸气 4 秒，屏息 4 秒，呼气 6 秒。" \
  "重复 5 个循环，有助于放松身心。"

#define TPL_BREATH_STRESS_PREFIX \
  "当前压力水平偏高。"

#define TPL_EXPLAIN_DEFAULT \
  "提醒您是因为心率和心率变异性出现了变化，不代表医学诊断。"

#define TPL_EXPLAIN_DISCLAIMER \
  "本解释仅供参考。如有健康疑虑，请咨询专业医疗人员。"

#define TPL_SUGGEST_BREATH \
  "建议尝试深呼吸放松"

#define TPL_SUGGEST_REST \
  "适当休息有助于恢复"

#define TPL_SUGGEST_ACTIVITY \
  "轻度运动可能有助于调节情绪"

#define TPL_SUGGEST_NOTE \
  "记录下当时的情境有助于回顾"

#define TPL_RECOMMEND_ROUTINE \
  "建议保持规律的作息时间"

#define TPL_RECOMMEND_EXERCISE \
  "适量运动有助于情绪调节"

#define TPL_RECOMMEND_SLEEP \
  "充足睡眠是情绪健康的基础"

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Label mapping entry */

struct label_map
{
  const char *en;
  const char *zh;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct label_map g_label_map[] =
{
  { "stress",     "\xe5\x8e\x8b\xe5\x8a\x9b" },           /* 压力 */
  { "excitement", "\xe5\xbf\x83\xe5\x8a\xa8" },           /* 心动 */
  { "nervous",    "\xe7\xb4\xa7\xe5\xbc\xa0" },           /* 紧张 */
  { "surprise",   "\xe6\x83\x8a\xe5\x96\x9c" },           /* 惊喜 */
  { "other",      "\xe5\x85\xb6\xe4\xbb\x96" },           /* 其他 */
  { NULL, NULL }
};

static const struct label_map g_activity_map[] =
{
  { "sitting",  "\xe5\x9d\x90\xe7\x9d\x80" },             /* 坐着 */
  { "walking",  "\xe6\x95\xa3\xe6\xad\xa5" },             /* 散步 */
  { "running",  "\xe8\xb7\x91\xe6\xad\xa5" },             /* 跑步 */
  { "sleeping", "\xe4\xbc\x91\xe6\x81\xaf" },             /* 休息 */
  { "unknown",  "\xe6\xb4\xbb\xe5\x8a\xa8" },             /* 活动 */
  { NULL, NULL }
};

static const struct label_map g_hr_context_map[] =
{
  { "elevated", "\xe5\x81\x8f\xe9\xab\x98" },             /* 偏高 */
  { "normal",   "\xe6\xad\xa3\xe5\xb8\xb8" },             /* 正常 */
  { "low",      "\xe5\x81\x8f\xe4\xbd\x8e" },             /* 偏低 */
  { NULL, NULL }
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lookup_map
 *
 * Description:
 *   Look up an English key in a label_map table.  Returns the Chinese
 *   string for the first match, or the "other" entry if not found.
 *   Returns a fallback string if even "other" is missing.
 *
 ****************************************************************************/

static const char *lookup_map(const struct label_map *map,
                              const char *key,
                              const char *fallback)
{
  int i;

  if (key == NULL)
    {
      return fallback;
    }

  for (i = 0; map[i].en != NULL; i++)
    {
      if (strcmp(map[i].en, key) == 0)
        {
          return map[i].zh;
        }
    }

  /* Try "other" entry */

  for (i = 0; map[i].en != NULL; i++)
    {
      if (strcmp(map[i].en, "other") == 0)
        {
          return map[i].zh;
        }
    }

  return fallback;
}

/****************************************************************************
 * Name: format_event_time
 *
 * Description:
 *   Extract a human-readable time string from an ISO 8601 timestamp.
 *   "2026-08-11T14:30:00+08:00" -> "14:30"
 *   Falls back to the raw string if parsing fails.
 *
 ****************************************************************************/

static void format_event_time(const char *iso_time,
                              char *out, size_t out_size)
{
  const char *t;

  if (iso_time == NULL || iso_time[0] == '\0')
    {
      snprintf(out, out_size, "unknown");
      return;
    }

  /* Find the 'T' separator and extract HH:MM */

  t = strchr(iso_time, 'T');
  if (t != NULL && strlen(t) >= 6)
    {
      snprintf(out, out_size, "%c%c:%c%c", t[1], t[2], t[4], t[5]);
    }
  else
    {
      snprintf(out, out_size, "%s", iso_time);
    }
}

/****************************************************************************
 * Name: determine_dominant_label
 *
 * Description:
 *   Find the most common label among the events.
 *
 ****************************************************************************/

static const char *determine_dominant_label(
    const struct mimo_event *events, int count)
{
  int counts[5] = { 0, 0, 0, 0, 0 }; /* stress, excitement, nervous, surprise, other */
  int i;
  int max_idx = 0;

  for (i = 0; i < count; i++)
    {
      if (strcmp(events[i].label, "stress") == 0)
        {
          counts[0]++;
        }
      else if (strcmp(events[i].label, "excitement") == 0)
        {
          counts[1]++;
        }
      else if (strcmp(events[i].label, "nervous") == 0)
        {
          counts[2]++;
        }
      else if (strcmp(events[i].label, "surprise") == 0)
        {
          counts[3]++;
        }
      else
        {
          counts[4]++;
        }
    }

  for (i = 1; i < 5; i++)
    {
      if (counts[i] > counts[max_idx])
        {
          max_idx = i;
        }
    }

  switch (max_idx)
    {
      case 0: return "stress";
      case 1: return "excitement";
      case 2: return "nervous";
      case 3: return "surprise";
      default: return "other";
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void mimo_tpl_init(void)
{
  syslog(LOG_INFO, "[%s] Template subsystem initialised\n", TAG);
}

void mimo_tpl_generate_diary(
    const struct mimo_diary_request *req,
    struct mimo_tpl_diary_response *resp)
{
  int  i;
  int  off = 0;
  int  n;
  char time_buf[16];

  memset(resp, 0, sizeof(*resp));

  syslog(LOG_INFO,
         "[%s] Generating local diary (%d events)\n",
         TAG, req->event_count);

  /* Generate one line per event */

  for (i = 0; i < req->event_count && i < MIMO_MAX_EVENTS; i++)
    {
      const struct mimo_event *evt = &req->events[i];
      const char *label_zh = mimo_tpl_label_to_chinese(evt->label);
      const char *act_zh = mimo_tpl_activity_to_chinese(evt->activity);

      format_event_time(evt->time, time_buf, sizeof(time_buf));

      n = snprintf(resp->diary_text + off,
                   sizeof(resp->diary_text) - off,
                   TPL_DIARY_EVENT "\n",
                   time_buf, label_zh, act_zh);
      if (n < 0 || (size_t)n >= sizeof(resp->diary_text) - off)
        {
          break;
        }

      off += n;
    }

  /* Append disclaimer */

  snprintf(resp->diary_text + off,
           sizeof(resp->diary_text) - off,
           "\n%s", TPL_DIARY_DISCLAIMER);

  /* Mood summary */

  if (req->event_count > 0)
    {
      const char *dominant = determine_dominant_label(
          req->events, req->event_count);
      const char *dominant_zh = mimo_tpl_label_to_chinese(dominant);

      snprintf(resp->mood_summary, sizeof(resp->mood_summary),
               TPL_DAILY_SUMMARY, req->event_count, dominant_zh);
    }
  else
    {
      snprintf(resp->mood_summary, sizeof(resp->mood_summary),
               "今天暂无情绪事件记录。");
    }

  /* Suggestions */

  resp->suggestion_count = 0;

  if (req->stress_level > 60)
    {
      snprintf(resp->suggestions[resp->suggestion_count],
               256, "%s", TPL_SUGGEST_BREATH);
      resp->suggestion_count++;
    }

  snprintf(resp->suggestions[resp->suggestion_count],
           256, "%s", TPL_SUGGEST_REST);
  resp->suggestion_count++;

  if (resp->suggestion_count < MIMO_TPL_MAX_SUGGESTIONS)
    {
      snprintf(resp->suggestions[resp->suggestion_count],
               256, "%s", TPL_SUGGEST_NOTE);
      resp->suggestion_count++;
    }

  syslog(LOG_INFO,
         "[%s] Local diary generated (%zu chars, %d suggestions)\n",
         TAG, strlen(resp->diary_text), resp->suggestion_count);
}

void mimo_tpl_generate_weekly_report(
    const struct mimo_weekly_request *req,
    struct mimo_tpl_weekly_response *resp)
{
  int i;
  int off = 0;
  int n;

  memset(resp, 0, sizeof(*resp));

  syslog(LOG_INFO,
         "[%s] Generating local weekly report "
         "(%d events, %d days)\n",
         TAG, req->event_count, req->daily_stats_count);

  /* Intro */

  n = snprintf(resp->report_text + off,
               sizeof(resp->report_text) - off,
               "%s\n", TPL_WEEKLY_INTRO);
  off += n;

  /* Per-day summary */

  for (i = 0; i < req->daily_stats_count && i < 7; i++)
    {
      const struct mimo_daily_summary *day = &req->daily_stats[i];
      const char *dominant_zh = mimo_tpl_label_to_chinese(
          day->dominant_label);

      n = snprintf(resp->report_text + off,
                   sizeof(resp->report_text) - off,
                   TPL_WEEKLY_DAY "\n",
                   i + 1, day->total_events, dominant_zh);
      if (n < 0 || (size_t)n >= sizeof(resp->report_text) - off)
        {
          break;
        }

      off += n;
    }

  /* Weekly totals */

  if (req->event_count > 0)
    {
      n = snprintf(resp->report_text + off,
                   sizeof(resp->report_text) - off,
                   "\n本周共检测到 %d 次情绪事件。\n",
                   req->event_count);
      off += n;
    }

  /* Disclaimer */

  snprintf(resp->report_text + off,
           sizeof(resp->report_text) - off,
           "\n%s", TPL_WEEKLY_DISCLAIMER);

  /* Highlights */

  resp->highlight_count = 0;

  if (req->daily_stats_count > 0)
    {
      /* Find the day with the most events */

      int max_day = 0;
      for (i = 1; i < req->daily_stats_count; i++)
        {
          if (req->daily_stats[i].total_events >
              req->daily_stats[max_day].total_events)
            {
              max_day = i;
            }
        }

      if (req->daily_stats[max_day].total_events > 0)
        {
          snprintf(resp->highlights[resp->highlight_count], 256,
                   "第%d天事件最多，共 %d 次",
                   max_day + 1,
                   req->daily_stats[max_day].total_events);
          resp->highlight_count++;
        }
    }

  /* Recommendations */

  resp->recommendation_count = 0;

  snprintf(resp->recommendations[resp->recommendation_count], 256,
           "%s", TPL_RECOMMEND_ROUTINE);
  resp->recommendation_count++;

  snprintf(resp->recommendations[resp->recommendation_count], 256,
           "%s", TPL_RECOMMEND_EXERCISE);
  resp->recommendation_count++;

  snprintf(resp->recommendations[resp->recommendation_count], 256,
           "%s", TPL_RECOMMEND_SLEEP);
  resp->recommendation_count++;

  syslog(LOG_INFO,
         "[%s] Local weekly report generated "
         "(%zu chars, %d highlights, %d recs)\n",
         TAG, strlen(resp->report_text),
         resp->highlight_count, resp->recommendation_count);
}

void mimo_tpl_generate_breath_guide(
    const struct mimo_breath_request *req,
    struct mimo_tpl_breath_response *resp)
{
  int off = 0;
  int n;

  memset(resp, 0, sizeof(*resp));

  syslog(LOG_INFO,
         "[%s] Generating local breath guide (stress=%d)\n",
         TAG, req->current_stress);

  /* Stress prefix if elevated */

  if (req->current_stress > 60)
    {
      n = snprintf(resp->guide_text + off,
                   sizeof(resp->guide_text) - off,
                   "%s\n\n", TPL_BREATH_STRESS_PREFIX);
      off += n;
    }

  /* Main guide */

  snprintf(resp->guide_text + off,
           sizeof(resp->guide_text) - off,
           "%s", TPL_BREATH_GUIDE);

  /* Pattern and duration */

  resp->duration_sec = 150; /* 5 cycles * (4+4+6) seconds */
  snprintf(resp->pattern, sizeof(resp->pattern), "4-4-6");

  syslog(LOG_INFO,
         "[%s] Local breath guide generated "
         "(pattern=%s, %ds)\n",
         TAG, resp->pattern, resp->duration_sec);
}

void mimo_tpl_generate_explanation(
    const struct mimo_explain_request *req,
    struct mimo_tpl_explain_response *resp)
{
  memset(resp, 0, sizeof(*resp));

  syslog(LOG_INFO,
         "[%s] Generating local explanation (label=%s)\n",
         TAG, req->event.label);

  /* Explanation based on reason codes */

  if (req->event.reason_count > 0)
    {
      int has_hrv = 0;
      int has_hr = 0;
      int i;

      for (i = 0; i < req->event.reason_count; i++)
        {
          if (strcmp(req->event.reason_codes[i].code, "hrv_drop") == 0)
            {
              has_hrv = 1;
            }
          if (strcmp(req->event.reason_codes[i].code, "hr_rise") == 0)
            {
              has_hr = 1;
            }
        }

      if (has_hr && has_hrv)
        {
          snprintf(resp->explanation_text,
                   sizeof(resp->explanation_text),
                   "%s", TPL_EXPLAIN_DEFAULT);
        }
      else if (has_hr)
        {
          snprintf(resp->explanation_text,
                   sizeof(resp->explanation_text),
                   "提醒您是因为检测到心率出现变化，不代表医学诊断。");
        }
      else if (has_hrv)
        {
          snprintf(resp->explanation_text,
                   sizeof(resp->explanation_text),
                   "提醒您是因为检测到心率变异性出现变化，不代表医学诊断。");
        }
      else
        {
          snprintf(resp->explanation_text,
                   sizeof(resp->explanation_text),
                   "%s", TPL_EXPLAIN_DEFAULT);
        }
    }
  else
    {
      snprintf(resp->explanation_text,
               sizeof(resp->explanation_text),
               "%s", TPL_EXPLAIN_DEFAULT);
    }

  /* HR context detail */

  if (req->event.hr_context[0] != '\0')
    {
      const char *hr_zh = mimo_tpl_hr_context_to_chinese(
          req->event.hr_context);

      int len = strlen(resp->explanation_text);
      snprintf(resp->explanation_text + len,
               sizeof(resp->explanation_text) - len,
               "当时心率%s。", hr_zh);
    }

  /* Disclaimer */

  snprintf(resp->disclaimer, sizeof(resp->disclaimer),
           "%s", TPL_EXPLAIN_DISCLAIMER);

  syslog(LOG_INFO,
         "[%s] Local explanation generated (%zu chars)\n",
         TAG, strlen(resp->explanation_text));
}

const char *mimo_tpl_label_to_chinese(const char *label_en)
{
  return lookup_map(g_label_map, label_en,
                    "\xe5\x85\xb6\xe4\xbb\x96");  /* 其他 */
}

const char *mimo_tpl_activity_to_chinese(const char *activity_en)
{
  return lookup_map(g_activity_map, activity_en,
                    "\xe6\xb4\xbb\xe5\x8a\xa8");  /* 活动 */
}

const char *mimo_tpl_hr_context_to_chinese(const char *hr_ctx_en)
{
  return lookup_map(g_hr_context_map, hr_ctx_en,
                    "\xe5\x8f\x98\xe5\x8c\x96");  /* 变化 */
}
