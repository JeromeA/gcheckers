#include "games/homeworlds/homeworlds_backend.h"
#include "games/homeworlds/homeworlds_game.h"
#include "games/homeworlds/homeworlds_position_text.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  HOMEWORLDS_PROOF_PROBE_DEFAULT_SAMPLE_COUNT = 5,
};

typedef enum {
  HOMEWORLDS_PROOF_PROBE_SECTION_NONE = 0,
  HOMEWORLDS_PROOF_PROBE_SECTION_REPLAY_MOVES,
  HOMEWORLDS_PROOF_PROBE_SECTION_REPORT_MOVES,
} HomeworldsProofProbeReportSection;

typedef struct {
  guint row;
  char *notation;
} HomeworldsProofProbeMove;

typedef struct {
  HomeworldsGoodMoveTrace trace;
  char *goal_report;
  gboolean called;
} HomeworldsProofProbeTraceCapture;

typedef struct {
  gsize id;
  char kind[64];
  gint bound_min;
  gint bound_max;
  gsize leaf_upper_bound;
  char prefix[128];
  char reason[128];
} HomeworldsProofProbeCreateEvent;

static void homeworlds_proof_probe_capture_trace(const HomeworldsGoodMoveTrace *trace, gpointer user_data) {
  HomeworldsProofProbeTraceCapture *capture = user_data;

  g_return_if_fail(trace != NULL);
  g_return_if_fail(capture != NULL);

  g_free(capture->goal_report);
  capture->goal_report = g_strdup(trace->goal_report);
  capture->trace = *trace;
  capture->trace.moves = NULL;
  capture->trace.move_count = 0;
  capture->trace.goal_report = capture->goal_report;
  capture->called = TRUE;
}

static void homeworlds_proof_probe_trace_capture_clear(HomeworldsProofProbeTraceCapture *capture) {
  g_return_if_fail(capture != NULL);

  g_clear_pointer(&capture->goal_report, g_free);
  capture->trace.goal_report = NULL;
}

static void homeworlds_proof_probe_trace_clear(HomeworldsGoodMoveTrace *trace) {
  g_return_if_fail(trace != NULL);

  g_free((char *) trace->goal_report);
  trace->goal_report = NULL;
}

static void homeworlds_proof_probe_move_free(gpointer data) {
  HomeworldsProofProbeMove *move = data;

  if (move == NULL) {
    return;
  }
  g_free(move->notation);
  g_free(move);
}

static gboolean homeworlds_proof_probe_text_is_uint(const char *text, guint *out_value) {
  guint64 parsed = 0;
  char *end = NULL;

  g_return_val_if_fail(text != NULL, FALSE);
  g_return_val_if_fail(out_value != NULL, FALSE);

  if (text[0] == '\0') {
    return FALSE;
  }
  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    if (!g_ascii_isdigit(*cursor)) {
      return FALSE;
    }
  }

  errno = 0;
  parsed = g_ascii_strtoull(text, &end, 10);
  if (errno != 0 || end == text || end == NULL || *end != '\0' || parsed == 0 || parsed > G_MAXUINT) {
    return FALSE;
  }

  *out_value = (guint)parsed;
  return TRUE;
}

static gboolean homeworlds_proof_probe_parse_numbered_line(const char *line,
                                                           guint *out_number,
                                                           char **out_notation) {
  g_autofree char *copy = NULL;
  char *stripped = NULL;
  char *dot = NULL;
  char *number_text = NULL;
  char *notation = NULL;
  guint64 parsed = 0;
  char *end = NULL;

  g_return_val_if_fail(line != NULL, FALSE);
  g_return_val_if_fail(out_number != NULL, FALSE);
  g_return_val_if_fail(out_notation != NULL, FALSE);

  *out_number = 0;
  *out_notation = NULL;
  copy = g_strdup(line);
  stripped = g_strstrip(copy);
  dot = strchr(stripped, '.');
  if (dot == NULL) {
    return FALSE;
  }

  *dot = '\0';
  number_text = g_strstrip(stripped);
  notation = g_strstrip(dot + 1);
  if (number_text[0] == '\0' || notation[0] == '\0') {
    return FALSE;
  }
  for (const char *cursor = number_text; *cursor != '\0'; ++cursor) {
    if (!g_ascii_isdigit(*cursor)) {
      return FALSE;
    }
  }

  errno = 0;
  parsed = g_ascii_strtoull(number_text, &end, 10);
  if (errno != 0 || end == number_text || end == NULL || *end != '\0' || parsed == 0 || parsed > G_MAXUINT) {
    return FALSE;
  }

  *out_number = (guint)parsed;
  *out_notation = g_strdup(notation);
  return TRUE;
}

static gboolean homeworlds_proof_probe_parse_create_line(const char *line,
                                                         HomeworldsProofProbeCreateEvent *out_event) {
  HomeworldsProofProbeCreateEvent event = {0};
  gint offset = 0;
  gint matched = 0;

  g_return_val_if_fail(line != NULL, FALSE);
  g_return_val_if_fail(out_event != NULL, FALSE);

  matched = sscanf(line,
                   "create #%zu %63s bounds=[%d,%d] leaves<=%zu %n",
                   &event.id,
                   event.kind,
                   &event.bound_min,
                   &event.bound_max,
                   &event.leaf_upper_bound,
                   &offset);
  if (matched < 5) {
    return FALSE;
  }

  if (line[offset] != '\0') {
    const char *rest = line + offset;

    if (g_str_has_prefix(rest, "prefix=[")) {
      const char *prefix_start = rest + strlen("prefix=[");
      const char *prefix_end = strchr(prefix_start, ']');

      if (prefix_end != NULL) {
        gsize prefix_length = MIN((gsize) (prefix_end - prefix_start), sizeof(event.prefix) - 1);
        g_autofree char *reason = g_strdup(prefix_end + 1);

        memcpy(event.prefix, prefix_start, prefix_length);
        event.prefix[prefix_length] = '\0';
        g_strstrip(reason);
        g_strlcpy(event.reason, reason, sizeof(event.reason));
      }
    } else {
      g_autofree char *reason = g_strdup(rest);

      g_strstrip(reason);
      g_strlcpy(event.reason, reason, sizeof(event.reason));
    }
  }

  *out_event = event;
  return TRUE;
}

static gboolean homeworlds_proof_probe_parse_branch_line(const char *line,
                                                         const char *prefix,
                                                         gsize *out_id,
                                                         char *kind,
                                                         gsize kind_size,
                                                         gint *out_bound_min,
                                                         gint *out_bound_max,
                                                         gsize *out_leaf_upper_bound) {
  char format[128] = {0};

  g_return_val_if_fail(line != NULL, FALSE);
  g_return_val_if_fail(prefix != NULL, FALSE);
  g_return_val_if_fail(out_id != NULL, FALSE);
  g_return_val_if_fail(kind != NULL, FALSE);
  g_return_val_if_fail(kind_size > 0, FALSE);
  g_return_val_if_fail(out_bound_min != NULL, FALSE);
  g_return_val_if_fail(out_bound_max != NULL, FALSE);
  g_return_val_if_fail(out_leaf_upper_bound != NULL, FALSE);

  g_snprintf(format, sizeof(format), "%s #%%zu %%63s bounds=[%%d,%%d] leaves<=%%zu", prefix);
  return sscanf(line,
                format,
                out_id,
                kind,
                out_bound_min,
                out_bound_max,
                out_leaf_upper_bound) == 5;
}

static gboolean homeworlds_proof_probe_parse_split_result_line(const char *line,
                                                               gsize *out_id,
                                                               gsize *out_created,
                                                               guint *out_queue) {
  g_return_val_if_fail(line != NULL, FALSE);
  g_return_val_if_fail(out_id != NULL, FALSE);
  g_return_val_if_fail(out_created != NULL, FALSE);
  g_return_val_if_fail(out_queue != NULL, FALSE);

  return sscanf(line, "split-result #%zu created+=%zu queue=%u", out_id, out_created, out_queue) == 3;
}

static gboolean homeworlds_proof_probe_parse_skip_line(const char *line,
                                                       gsize *out_id,
                                                       gint *out_cutoff,
                                                       gint *out_bound_min,
                                                       gint *out_bound_max) {
  g_return_val_if_fail(line != NULL, FALSE);
  g_return_val_if_fail(out_id != NULL, FALSE);
  g_return_val_if_fail(out_cutoff != NULL, FALSE);
  g_return_val_if_fail(out_bound_min != NULL, FALSE);
  g_return_val_if_fail(out_bound_max != NULL, FALSE);

  return sscanf(line,
                "skip #%zu cutoff=%d bounds=[%d,%d]",
                out_id,
                out_cutoff,
                out_bound_min,
                out_bound_max) == 4;
}

static gboolean homeworlds_proof_probe_extract_token_value(const char *line,
                                                           const char *key,
                                                           char *buffer,
                                                           gsize buffer_size) {
  const char *start = NULL;
  const char *end = NULL;
  gsize length = 0;

  g_return_val_if_fail(line != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(buffer_size > 0, FALSE);

  start = strstr(line, key);
  if (start == NULL) {
    return FALSE;
  }
  start += strlen(key);
  end = start;
  while (*end != '\0' && !g_ascii_isspace(*end)) {
    end++;
  }

  length = MIN((gsize) (end - start), buffer_size - 1);
  memcpy(buffer, start, length);
  buffer[length] = '\0';
  return TRUE;
}

static gboolean homeworlds_proof_probe_extract_bracket_value(const char *line,
                                                             const char *key,
                                                             char *buffer,
                                                             gsize buffer_size) {
  const char *start = NULL;
  const char *end = NULL;
  gsize length = 0;

  g_return_val_if_fail(line != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(buffer_size > 0, FALSE);

  start = strstr(line, key);
  if (start == NULL) {
    return FALSE;
  }
  start += strlen(key);
  end = strchr(start, ']');
  if (end == NULL) {
    return FALSE;
  }

  length = MIN((gsize) (end - start), buffer_size - 1);
  memcpy(buffer, start, length);
  buffer[length] = '\0';
  return TRUE;
}

static void homeworlds_proof_probe_format_score(gint score, char *buffer, gsize buffer_size) {
  g_return_if_fail(buffer != NULL);
  g_return_if_fail(buffer_size > 0);

  if (score == G_MININT) {
    g_strlcpy(buffer, "-inf", buffer_size);
    return;
  }
  if (score == G_MAXINT) {
    g_strlcpy(buffer, "+inf", buffer_size);
    return;
  }

  g_snprintf(buffer, buffer_size, "%d", score);
}

static void homeworlds_proof_probe_format_bounds(gint bound_min,
                                                 gint bound_max,
                                                 char *buffer,
                                                 gsize buffer_size) {
  char min_text[32] = {0};
  char max_text[32] = {0};

  g_return_if_fail(buffer != NULL);
  g_return_if_fail(buffer_size > 0);

  homeworlds_proof_probe_format_score(bound_min, min_text, sizeof(min_text));
  homeworlds_proof_probe_format_score(bound_max, max_text, sizeof(max_text));
  if (bound_min == bound_max) {
    g_snprintf(buffer, buffer_size, "exact=%s", min_text);
    return;
  }

  g_snprintf(buffer, buffer_size, "bounds=[%s,%s]", min_text, max_text);
}

static void homeworlds_proof_probe_format_leaf_bound(gsize leaf_upper_bound,
                                                     char *buffer,
                                                     gsize buffer_size) {
  g_return_if_fail(buffer != NULL);
  g_return_if_fail(buffer_size > 0);

  if (leaf_upper_bound == G_MAXSIZE) {
    g_strlcpy(buffer, "unknown", buffer_size);
    return;
  }

  g_snprintf(buffer, buffer_size, "%" G_GSIZE_FORMAT, leaf_upper_bound);
}

static void homeworlds_proof_probe_print_create_event(const HomeworldsProofProbeCreateEvent *event) {
  char bound_text[64] = {0};
  char leaf_text[32] = {0};

  g_return_if_fail(event != NULL);

  homeworlds_proof_probe_format_bounds(event->bound_min,
                                       event->bound_max,
                                       bound_text,
                                       sizeof(bound_text));
  homeworlds_proof_probe_format_leaf_bound(event->leaf_upper_bound, leaf_text, sizeof(leaf_text));

  g_print("  #%zu %s %s leaves<=%s",
          event->id,
          event->kind,
          bound_text,
          leaf_text);
  if (event->reason[0] != '\0') {
    g_print(" - %s", event->reason);
  }
  if (event->prefix[0] != '\0') {
    g_print("; prefix %s", event->prefix);
  }
  g_print("\n");
}

static void homeworlds_proof_probe_format_change_field(const char *value, char *buffer, gsize buffer_size) {
  const char *arrow = NULL;
  g_autofree char *old_value = NULL;
  const char *new_value = NULL;

  g_return_if_fail(value != NULL);
  g_return_if_fail(buffer != NULL);
  g_return_if_fail(buffer_size > 0);

  arrow = strstr(value, "->");
  if (arrow == NULL) {
    g_strlcpy(buffer, value, buffer_size);
    return;
  }

  old_value = g_strndup(value, (gsize)(arrow - value));
  new_value = arrow + 2;
  if (g_strcmp0(old_value, new_value) == 0) {
    g_snprintf(buffer, buffer_size, "%s=", old_value);
    return;
  }

  g_strlcpy(buffer, value, buffer_size);
}

static void homeworlds_proof_probe_print_collection_result(const char *line,
                                                           const char *summary,
                                                           const char *indent) {
  char covered[16] = {0};
  char leaves[32] = {0};
  char scored[32] = {0};
  char kept[32] = {0};
  char best[64] = {0};
  char cutoff[64] = {0};
  char kept_text[32] = {0};
  char best_text[64] = {0};
  char cutoff_text[64] = {0};
  char pruned[32] = {0};
  char duplicate[32] = {0};
  char step_reject[32] = {0};
  char bad_move[32] = {0};
  char goal_filter_reject[32] = {0};
  char root_cat_reject[32] = {0};
  char window_reject[32] = {0};
  char full_reject[32] = {0};
  gboolean showed_filter = FALSE;

  g_return_if_fail(line != NULL);
  g_return_if_fail(summary != NULL);
  g_return_if_fail(indent != NULL);

  if (!homeworlds_proof_probe_extract_token_value(line, "covered=", covered, sizeof(covered)) ||
      !homeworlds_proof_probe_extract_token_value(line, "leaves+=", leaves, sizeof(leaves)) ||
      !homeworlds_proof_probe_extract_token_value(line, "scored+=", scored, sizeof(scored)) ||
      !homeworlds_proof_probe_extract_token_value(line, "kept=", kept, sizeof(kept)) ||
      !homeworlds_proof_probe_extract_token_value(line, "best=", best, sizeof(best)) ||
      !homeworlds_proof_probe_extract_token_value(line, "cutoff=", cutoff, sizeof(cutoff)) ||
      !homeworlds_proof_probe_extract_token_value(line, "pruned+=", pruned, sizeof(pruned))) {
    g_print("%sresult: %s\n", indent, line);
    return;
  }
  homeworlds_proof_probe_format_change_field(kept, kept_text, sizeof(kept_text));
  homeworlds_proof_probe_format_change_field(best, best_text, sizeof(best_text));
  homeworlds_proof_probe_format_change_field(cutoff, cutoff_text, sizeof(cutoff_text));

  g_print("%sresult:", indent);
  if (summary[0] != '\0') {
    g_print(" %s;", summary);
  }
  g_print(" leaves +%s, scored +%s, kept %s, best %s, cutoff %s, pruned descendants +%s",
          leaves,
          scored,
          kept_text,
          best_text,
          cutoff_text,
          pruned);
  homeworlds_proof_probe_extract_token_value(line, "duplicate+=", duplicate, sizeof(duplicate));
  homeworlds_proof_probe_extract_token_value(line, "step_reject+=", step_reject, sizeof(step_reject));
  homeworlds_proof_probe_extract_token_value(line, "bad_move+=", bad_move, sizeof(bad_move));
  homeworlds_proof_probe_extract_token_value(line,
                                             "goal_filter_reject+=",
                                             goal_filter_reject,
                                             sizeof(goal_filter_reject));
  homeworlds_proof_probe_extract_token_value(line, "root_cat_reject+=", root_cat_reject, sizeof(root_cat_reject));
  homeworlds_proof_probe_extract_token_value(line, "window_reject+=", window_reject, sizeof(window_reject));
  homeworlds_proof_probe_extract_token_value(line, "full_reject+=", full_reject, sizeof(full_reject));
  if ((duplicate[0] != '\0' && g_strcmp0(duplicate, "0") != 0) ||
      (step_reject[0] != '\0' && g_strcmp0(step_reject, "0") != 0) ||
      (bad_move[0] != '\0' && g_strcmp0(bad_move, "0") != 0) ||
      (goal_filter_reject[0] != '\0' && g_strcmp0(goal_filter_reject, "0") != 0) ||
      (root_cat_reject[0] != '\0' && g_strcmp0(root_cat_reject, "0") != 0) ||
      (window_reject[0] != '\0' && g_strcmp0(window_reject, "0") != 0) ||
      (full_reject[0] != '\0' && g_strcmp0(full_reject, "0") != 0)) {
    g_print("; filters");
    if (duplicate[0] != '\0' && g_strcmp0(duplicate, "0") != 0) {
      g_print("%s duplicate +%s", showed_filter ? "," : ":", duplicate);
      showed_filter = TRUE;
    }
    if (step_reject[0] != '\0' && g_strcmp0(step_reject, "0") != 0) {
      g_print("%s bad-step +%s", showed_filter ? "," : ":", step_reject);
      showed_filter = TRUE;
    }
    if (bad_move[0] != '\0' && g_strcmp0(bad_move, "0") != 0) {
      g_print("%s bad-move +%s", showed_filter ? "," : ":", bad_move);
      showed_filter = TRUE;
    }
    if (goal_filter_reject[0] != '\0' && g_strcmp0(goal_filter_reject, "0") != 0) {
      g_print("%s outside-goal +%s", showed_filter ? "," : ":", goal_filter_reject);
      showed_filter = TRUE;
    }
    if (root_cat_reject[0] != '\0' && g_strcmp0(root_cat_reject, "0") != 0) {
      g_print("%s missing-root-cat +%s", showed_filter ? "," : ":", root_cat_reject);
      showed_filter = TRUE;
    }
    if (window_reject[0] != '\0' && g_strcmp0(window_reject, "0") != 0) {
      g_print("%s outside-window +%s", showed_filter ? "," : ":", window_reject);
      showed_filter = TRUE;
    }
    if (full_reject[0] != '\0' && g_strcmp0(full_reject, "0") != 0) {
      g_print("%s full-buffer +%s", showed_filter ? "," : ":", full_reject);
    }
  }
  g_print("\n");
}

static void homeworlds_proof_probe_print_later_iterations(char **lines,
                                                          guint line_count,
                                                          guint start_index,
                                                          guint max_selected) {
  guint index = start_index;
  guint shown = 0;

  g_return_if_fail(lines != NULL);
  g_return_if_fail(max_selected > 0);

  while (index < line_count && shown < max_selected) {
    char kind[64] = {0};
    char bound_text[64] = {0};
    char leaf_text[32] = {0};
    char prefix[128] = {0};
    char goal[128] = {0};
    gsize id = 0;
    gint bound_min = 0;
    gint bound_max = 0;
    gsize leaf_upper_bound = 0;

    if (!homeworlds_proof_probe_parse_branch_line(lines[index],
                                                  "select",
                                                  &id,
                                                  kind,
                                                  sizeof(kind),
                                                  &bound_min,
                                                  &bound_max,
                                                  &leaf_upper_bound)) {
      index++;
      continue;
    }

    homeworlds_proof_probe_format_bounds(bound_min, bound_max, bound_text, sizeof(bound_text));
    homeworlds_proof_probe_format_leaf_bound(leaf_upper_bound, leaf_text, sizeof(leaf_text));
    homeworlds_proof_probe_extract_bracket_value(lines[index], "prefix=[", prefix, sizeof(prefix));
    homeworlds_proof_probe_extract_bracket_value(lines[index], "goal=[", goal, sizeof(goal));
    g_print("\nselect #%zu %s %s leaves<=%s", id, kind, bound_text, leaf_text);
    if (prefix[0] != '\0') {
      g_print(" prefix=%s", prefix);
    }
    if (goal[0] != '\0') {
      g_print(" goal=%s", goal);
    }
    g_print("\n");
    shown++;
    index++;

    while (index < line_count) {
      HomeworldsProofProbeCreateEvent create_event = {0};
      char explore_kind[64] = {0};
      gsize explore_id = 0;
      gint explore_min = 0;
      gint explore_max = 0;
      gsize explore_leaf_upper_bound = 0;
      gsize split_id = 0;
      gsize created = 0;
      guint queue = 0;
      gsize skip_id = 0;
      gint cutoff = 0;
      gint skip_min = 0;
      gint skip_max = 0;
      gsize next_id = 0;
      char next_kind[64] = {0};
      gint next_bound_min = 0;
      gint next_bound_max = 0;
      gsize next_leaf_upper_bound = 0;

      if (homeworlds_proof_probe_parse_branch_line(lines[index],
                                                   "select",
                                                   &next_id,
                                                   next_kind,
                                                   sizeof(next_kind),
                                                   &next_bound_min,
                                                   &next_bound_max,
                                                   &next_leaf_upper_bound)) {
        break;
      }
      if (homeworlds_proof_probe_parse_create_line(lines[index], &create_event)) {
        homeworlds_proof_probe_print_create_event(&create_event);
        index++;
        continue;
      }
      if (homeworlds_proof_probe_parse_split_result_line(lines[index], &split_id, &created, &queue) &&
          split_id == id) {
        g_print("  result: expanded into %zu child branches; queue now has %u branches; no leaves scored here\n",
                created,
                queue);
        index++;
        break;
      }
      if (g_str_has_prefix(lines[index], "goal-split-fallback #")) {
        g_print("  result: kept as one broad yellow branch because the goal split was not bounded enough\n");
        index++;
        continue;
      }
      if (homeworlds_proof_probe_parse_skip_line(lines[index], &skip_id, &cutoff, &skip_min, &skip_max) &&
          skip_id == id) {
        char skip_text[64] = {0};

        homeworlds_proof_probe_format_bounds(skip_min, skip_max, skip_text, sizeof(skip_text));
        g_print("  result: skipped because cutoff %d cannot be reached from %s\n", cutoff, skip_text);
        index++;
        break;
      }

      if (homeworlds_proof_probe_parse_branch_line(lines[index],
                                                   "explore",
                                                   &explore_id,
                                                   explore_kind,
                                                   sizeof(explore_kind),
                                                   &explore_min,
                                                   &explore_max,
                                                   &explore_leaf_upper_bound) &&
          explore_id == id) {
        char explore_bound_text[64] = {0};

        homeworlds_proof_probe_format_bounds(explore_min,
                                             explore_max,
                                             explore_bound_text,
                                             sizeof(explore_bound_text));
        g_print("  action: recursively explore this branch (%s)\n", explore_bound_text);
        index++;
        continue;
      }
      if (g_str_has_prefix(lines[index], "explore-result #")) {
        homeworlds_proof_probe_print_collection_result(lines[index],
                                                       "",
                                                       "  ");
        index++;
        break;
      }
      if (g_str_has_prefix(lines[index], "direct-result #")) {
        homeworlds_proof_probe_print_collection_result(lines[index],
                                                       "directly collected selected branch moves",
                                                       "  ");
        index++;
        continue;
      }
      g_print("  trace: %s\n", lines[index]);
      index++;
    }
  }

  if (shown == max_selected) {
    g_print("\ncompact report stopped after %u selected branches after #0 expansion\n", max_selected);
  }
}

static void homeworlds_proof_probe_print_iteration_report(const char *goal_report, guint iteration_limit) {
  g_auto(GStrv) lines = NULL;
  guint line_count = 0;
  guint index = 0;
  gsize root_id = 0;
  gsize split_id = 0;
  gsize split_created = 0;
  guint split_queue = 0;
  char root_kind[64] = {0};
  gint root_min = 0;
  gint root_max = 0;
  gsize root_leaf_upper_bound = 0;

  if (goal_report == NULL || goal_report[0] == '\0') {
    g_print("\n#0 expansion:\n  no goal-tree report was captured\n");
    return;
  }

  lines = g_strsplit(goal_report, "\n", -1);
  g_return_if_fail(lines != NULL);

  while (lines[line_count] != NULL) {
    line_count++;
  }

  g_print("\n#0 expansion:\n");
  if (line_count < 2 ||
      !homeworlds_proof_probe_parse_branch_line(lines[1],
                                                "select",
                                                &root_id,
                                                root_kind,
                                                sizeof(root_kind),
                                                &root_min,
                                                &root_max,
                                                &root_leaf_upper_bound) ||
      root_id != 0) {
    g_print("  goal report does not start with #0 selection\n");
    return;
  }

  index = 2;
  while (index < line_count) {
    HomeworldsProofProbeCreateEvent event = {0};

    if (g_str_has_prefix(lines[index], "direct-result #0 single-steps")) {
      homeworlds_proof_probe_print_collection_result(lines[index],
                                                     "directly collected root single-step moves",
                                                     "    ");
      index++;
      continue;
    }
    if (g_str_has_prefix(lines[index], "direct-result #0 pass-fallback")) {
      homeworlds_proof_probe_print_collection_result(lines[index],
                                                     "directly collected root pass fallback",
                                                     "    ");
      index++;
      continue;
    }
    if (homeworlds_proof_probe_parse_split_result_line(lines[index], &split_id, &split_created, &split_queue) &&
        split_id == 0) {
      index++;
      break;
    }
    if (!homeworlds_proof_probe_parse_create_line(lines[index], &event)) {
      break;
    }
    homeworlds_proof_probe_print_create_event(&event);
    index++;
  }

  if (split_id == 0 && split_created > 0) {
    g_print("  queue after #0 expansion: %u branches\n", split_queue);
  }

  homeworlds_proof_probe_print_later_iterations(lines, line_count, index, iteration_limit);
}

static gboolean homeworlds_proof_probe_print_position(const HomeworldsPosition *position) {
  g_autofree char *ascii = NULL;

  g_return_val_if_fail(position != NULL, FALSE);

  ascii = homeworlds_position_format_ascii(position);
  if (ascii == NULL) {
    g_printerr("Failed to format Homeworlds position.\n");
    return FALSE;
  }

  g_print("position:\n%s", ascii);
  return TRUE;
}

static gboolean homeworlds_proof_probe_move_has_notation(const HomeworldsProofProbeMove *move) {
  g_return_val_if_fail(move != NULL, FALSE);

  return move->notation != NULL && move->notation[0] != '\0';
}

static gboolean homeworlds_proof_probe_request_is_unresolved_row(gconstpointer data) {
  const HomeworldsProofProbeMove *move = data;

  g_return_val_if_fail(move != NULL, FALSE);

  return move->row > 0 && !homeworlds_proof_probe_move_has_notation(move);
}

static gboolean homeworlds_proof_probe_store_requested_row(GPtrArray *moves,
                                                           guint row,
                                                           const char *notation) {
  g_return_val_if_fail(moves != NULL, FALSE);
  g_return_val_if_fail(notation != NULL, FALSE);

  for (guint i = 0; i < moves->len; ++i) {
    HomeworldsProofProbeMove *move = g_ptr_array_index(moves, i);

    if (move->row != row || homeworlds_proof_probe_move_has_notation(move)) {
      continue;
    }

    move->notation = g_strdup(notation);
    return TRUE;
  }
  return FALSE;
}

static gboolean homeworlds_proof_probe_has_unresolved_rows(GPtrArray *moves) {
  g_return_val_if_fail(moves != NULL, FALSE);

  for (guint i = 0; i < moves->len; ++i) {
    if (homeworlds_proof_probe_request_is_unresolved_row(g_ptr_array_index(moves, i))) {
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean homeworlds_proof_probe_all_requests_have_notation(GPtrArray *moves) {
  g_return_val_if_fail(moves != NULL, FALSE);

  for (guint i = 0; i < moves->len; ++i) {
    if (!homeworlds_proof_probe_move_has_notation(g_ptr_array_index(moves, i))) {
      return FALSE;
    }
  }
  return TRUE;
}

static gboolean homeworlds_proof_probe_apply_replay_line(HomeworldsPosition *position,
                                                         guint line_number,
                                                         const char *notation) {
  HomeworldsMove move = {0};

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(notation != NULL, FALSE);

  if (!homeworlds_move_parse(notation, &move) || !homeworlds_position_apply_move(position, &move)) {
    g_printerr("Failed to replay report move %u: %s\n", line_number, notation);
    return FALSE;
  }
  return TRUE;
}

static gboolean homeworlds_proof_probe_read_report(const char *path,
                                                   HomeworldsPosition *position,
                                                   GPtrArray *moves,
                                                   gboolean use_default_sample) {
  g_autoptr(GIOChannel) channel = NULL;
  g_autoptr(GError) error = NULL;
  HomeworldsProofProbeReportSection section = HOMEWORLDS_PROOF_PROBE_SECTION_NONE;
  gsize default_sample_count = 0;
  gboolean saw_report_moves = FALSE;

  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(moves != NULL, FALSE);

  channel = g_io_channel_new_file(path, "r", &error);
  if (channel == NULL) {
    g_printerr("Failed to open %s: %s\n", path, error != NULL ? error->message : "unknown error");
    return FALSE;
  }

  homeworlds_position_init(position);
  while (TRUE) {
    g_autofree char *line = NULL;
    g_autofree char *stripped_line = NULL;
    GIOStatus status = g_io_channel_read_line(channel, &line, NULL, NULL, &error);

    if (status == G_IO_STATUS_EOF) {
      break;
    }
    if (status != G_IO_STATUS_NORMAL) {
      g_printerr("Failed to read %s: %s\n", path, error != NULL ? error->message : "unknown error");
      homeworlds_position_clear(position);
      return FALSE;
    }

    stripped_line = g_strdup(line);
    g_strstrip(stripped_line);
    if (g_strcmp0(stripped_line, "moves:") == 0) {
      section = HOMEWORLDS_PROOF_PROBE_SECTION_REPLAY_MOVES;
      continue;
    }
    if (g_strcmp0(stripped_line, "all_moves:") == 0 || g_strcmp0(stripped_line, "good_moves:") == 0) {
      section = HOMEWORLDS_PROOF_PROBE_SECTION_REPORT_MOVES;
      saw_report_moves = TRUE;
      continue;
    }
    if (g_strcmp0(stripped_line, "position:") == 0 ||
        g_str_has_prefix(stripped_line, "all_moves_streamed:") ||
        g_str_has_prefix(stripped_line, "good_moves_count:")) {
      section = HOMEWORLDS_PROOF_PROBE_SECTION_NONE;
      continue;
    }
    if (stripped_line[0] == '\0') {
      section = HOMEWORLDS_PROOF_PROBE_SECTION_NONE;
      continue;
    }

    if (section == HOMEWORLDS_PROOF_PROBE_SECTION_REPLAY_MOVES) {
      g_autofree char *notation = NULL;
      guint number = 0;

      if (!homeworlds_proof_probe_parse_numbered_line(line, &number, &notation)) {
        g_printerr("Invalid replay move line in %s: %s\n", path, stripped_line);
        homeworlds_position_clear(position);
        return FALSE;
      }
      if (!homeworlds_proof_probe_apply_replay_line(position, number, notation)) {
        homeworlds_position_clear(position);
        return FALSE;
      }
      continue;
    }

    if (section == HOMEWORLDS_PROOF_PROBE_SECTION_REPORT_MOVES) {
      g_autofree char *notation = NULL;
      guint number = 0;

      if (!homeworlds_proof_probe_parse_numbered_line(line, &number, &notation)) {
        continue;
      }
      if (use_default_sample && default_sample_count < HOMEWORLDS_PROOF_PROBE_DEFAULT_SAMPLE_COUNT) {
        HomeworldsProofProbeMove *move = g_new0(HomeworldsProofProbeMove, 1);

        move->row = number;
        move->notation = g_strdup(notation);
        g_ptr_array_add(moves, move);
        default_sample_count++;
      } else {
        homeworlds_proof_probe_store_requested_row(moves, number, notation);
      }
    }
  }

  if (use_default_sample && moves->len == 0) {
    g_printerr("No report move rows found in %s.\n", path);
    homeworlds_position_clear(position);
    return FALSE;
  }
  if (!use_default_sample && homeworlds_proof_probe_has_unresolved_rows(moves)) {
    g_printerr("One or more requested report move rows were not found in %s.\n", path);
    homeworlds_position_clear(position);
    return FALSE;
  }
  if (!saw_report_moves && use_default_sample) {
    g_printerr("No report move section found in %s.\n", path);
    homeworlds_position_clear(position);
    return FALSE;
  }
  return TRUE;
}

static gint homeworlds_proof_probe_score_after_move(const HomeworldsPosition *position,
                                                    const HomeworldsMove *move,
                                                    gboolean *out_ok) {
  HomeworldsPosition child = {0};
  GameBackendOutcome outcome = GAME_BACKEND_OUTCOME_ONGOING;
  gint score = 0;

  g_return_val_if_fail(position != NULL, 0);
  g_return_val_if_fail(move != NULL, 0);
  g_return_val_if_fail(out_ok != NULL, 0);

  *out_ok = FALSE;
  homeworlds_position_copy(&child, position);
  if (!homeworlds_position_apply_move(&child, move)) {
    homeworlds_position_clear(&child);
    return 0;
  }

  outcome = homeworlds_position_outcome(&child);
  score = outcome == GAME_BACKEND_OUTCOME_ONGOING
      ? homeworlds_position_evaluate_static(&child)
      : homeworlds_position_terminal_score(outcome, 1);
  homeworlds_position_clear(&child);
  *out_ok = TRUE;
  return score;
}

static gboolean homeworlds_proof_probe_score_reaches_cutoff(guint side, gint score, gint cutoff) {
  g_return_val_if_fail(side < 2, FALSE);

  return side == 0 ? score >= cutoff : score <= cutoff;
}

static gboolean homeworlds_proof_probe_find_cutoff(const HomeworldsPosition *position,
                                                   guint side,
                                                   guint iteration_limit,
                                                   gint *out_cutoff,
                                                   HomeworldsGoodMoveTrace *out_trace) {
  GameBackendMoveList moves = {0};
  const HomeworldsMove *cutoff_move = NULL;
  HomeworldsProofProbeTraceCapture trace_capture = {0};
  gsize selected_branch_limit = 0;
  gboolean score_ok = FALSE;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(out_cutoff != NULL, FALSE);
  g_return_val_if_fail(out_trace != NULL, FALSE);

  if (iteration_limit > 0) {
    selected_branch_limit = (gsize)iteration_limit + 1;
  }
  homeworlds_backend_set_good_move_trace(homeworlds_proof_probe_capture_trace, &trace_capture);
  homeworlds_backend_set_good_move_trace_branch_limit(selected_branch_limit);
  moves = homeworlds_game_backend.list_good_moves(position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  homeworlds_backend_set_good_move_trace_branch_limit(0);
  homeworlds_backend_set_good_move_trace(NULL, NULL);
  if (moves.count == 0) {
    g_printerr("No good moves are available from the report position.\n");
    homeworlds_proof_probe_trace_capture_clear(&trace_capture);
    return FALSE;
  }
  if (!trace_capture.called) {
    g_printerr("Failed to capture good_moves() trace for the report position.\n");
    homeworlds_game_backend.move_list_free(&moves);
    homeworlds_proof_probe_trace_capture_clear(&trace_capture);
    return FALSE;
  }

  cutoff_move = homeworlds_game_backend.move_list_get(&moves, moves.count - 1);
  g_return_val_if_fail(cutoff_move != NULL, FALSE);
  *out_cutoff = homeworlds_proof_probe_score_after_move(position, cutoff_move, &score_ok);
  *out_trace = trace_capture.trace;
  trace_capture.goal_report = NULL;
  trace_capture.trace.goal_report = NULL;
  homeworlds_game_backend.move_list_free(&moves);
  if (!score_ok) {
    g_printerr("Failed to score the cutoff move.\n");
    homeworlds_proof_probe_trace_clear(out_trace);
    return FALSE;
  }
  return TRUE;
}

static gboolean homeworlds_proof_probe_append_step(HomeworldsMoveBuilderState *state,
                                                   const HomeworldsTurnStep *step) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (state->move.step_count >= HOMEWORLDS_MAX_MOVE_STEPS) {
    g_printerr("Move exceeds the maximum Homeworlds step count.\n");
    return FALSE;
  }

  state->move.kind = HOMEWORLDS_MOVE_KIND_TURN;
  state->move.steps[state->move.step_count++] = *step;
  return TRUE;
}

static gboolean homeworlds_proof_probe_apply_step(HomeworldsMoveBuilderState *state,
                                                  const HomeworldsTurnStep *step) {
  gboolean forced_action = FALSE;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (!homeworlds_proof_probe_append_step(state, step)) {
    return FALSE;
  }

  if (step->kind == HOMEWORLDS_STEP_SACRIFICE) {
    if (!homeworlds_pyramid_is_valid(step->actor.ship) ||
        !homeworlds_position_apply_turn_step(&state->working_position, step)) {
      return FALSE;
    }
    state->pending_actions_remaining = homeworlds_pyramid_size(step->actor.ship);
    state->forced_action_color = homeworlds_pyramid_color(step->actor.ship);
  } else if (step->kind == HOMEWORLDS_STEP_CATASTROPHE) {
    if (!homeworlds_position_apply_turn_step(&state->working_position, step)) {
      return FALSE;
    }
  } else {
    forced_action = state->pending_actions_remaining > 0;
    if (forced_action && step->kind != HOMEWORLDS_STEP_PASS) {
      if (!homeworlds_position_apply_forced_action_step(&state->working_position, step)) {
        return FALSE;
      }
    } else if (!homeworlds_position_apply_turn_step(&state->working_position, step)) {
      return FALSE;
    }
    if (forced_action) {
      state->pending_actions_remaining--;
    }
  }

  state->stage = state->pending_actions_remaining > 0 ? HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP
                                                      : HOMEWORLDS_BUILDER_STAGE_COMPLETE;
  return TRUE;
}

static const char *homeworlds_proof_probe_result_name(HomeworldsGoodMoveProofResult result) {
  switch (result) {
    case HOMEWORLDS_GOOD_MOVE_PROOF_NOT_ACTIVE:
      return "not-active";
    case HOMEWORLDS_GOOD_MOVE_PROOF_NOT_PLAY:
      return "not-play";
    case HOMEWORLDS_GOOD_MOVE_PROOF_COMPLETE:
      return "complete";
    case HOMEWORLDS_GOOD_MOVE_PROOF_UNSUPPORTED_WEIGHTS:
      return "unsupported-weights";
    case HOMEWORLDS_GOOD_MOVE_PROOF_UNCERTAIN:
      return "uncertain";
    case HOMEWORLDS_GOOD_MOVE_PROOF_KEEP:
      return "keep";
    case HOMEWORLDS_GOOD_MOVE_PROOF_REJECT:
      return "reject";
    default:
      return "unknown";
  }
}

static void homeworlds_proof_probe_print_status(const HomeworldsMoveBuilderState *state,
                                                guint side,
                                                gint cutoff,
                                                HomeworldsGoodMoveProofStatus *out_status) {
  HomeworldsGoodMoveProofStatus status = {0};

  g_return_if_fail(state != NULL);
  g_return_if_fail(side < 2);
  g_return_if_fail(out_status != NULL);

  if (!homeworlds_backend_describe_yellow_sacrifice_proof(state, side, cutoff, &status)) {
    g_print("    proof=error\n");
    *out_status = status;
    return;
  }

  if (status.result == HOMEWORLDS_GOOD_MOVE_PROOF_KEEP ||
      status.result == HOMEWORLDS_GOOD_MOVE_PROOF_REJECT) {
    g_print("    pending=%u score=%d buildable=%d catastrophe=%u bound=%d cutoff=%d proof=%s\n",
            status.pending_actions_remaining,
            status.current_score,
            status.buildable_gain,
            status.catastrophe_gain,
            status.bound,
            status.cutoff,
            homeworlds_proof_probe_result_name(status.result));
  } else {
    g_print("    pending=%u score=%d cutoff=%d proof=%s\n",
            status.pending_actions_remaining,
            status.current_score,
            status.cutoff,
            homeworlds_proof_probe_result_name(status.result));
  }

  *out_status = status;
}

static gboolean homeworlds_proof_probe_format_step(const HomeworldsTurnStep *step,
                                                   char *buffer,
                                                   gsize buffer_size) {
  HomeworldsMove step_move = {
    .kind = HOMEWORLDS_MOVE_KIND_TURN,
    .step_count = 1,
  };

  g_return_val_if_fail(step != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(buffer_size > 0, FALSE);

  step_move.steps[0] = *step;
  return homeworlds_move_format(&step_move, buffer, buffer_size);
}

static gboolean homeworlds_proof_probe_run_move(const HomeworldsPosition *position,
                                                guint side,
                                                gint cutoff,
                                                const HomeworldsProofProbeMove *probe_move) {
  GameBackendMoveBuilder builder = {0};
  HomeworldsMoveBuilderState *state = NULL;
  HomeworldsMove move = {0};
  char prefix[512] = {0};
  gboolean stopped = FALSE;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(probe_move != NULL, FALSE);
  g_return_val_if_fail(probe_move->notation != NULL, FALSE);

  if (probe_move->row > 0) {
    g_print("\n%u. %s\n", probe_move->row, probe_move->notation);
  } else {
    g_print("\nmove. %s\n", probe_move->notation);
  }

  if (!homeworlds_move_parse(probe_move->notation, &move)) {
    g_printerr("Failed to parse move: %s\n", probe_move->notation);
    return FALSE;
  }
  if (!homeworlds_move_builder_init(position, &builder)) {
    g_printerr("Failed to initialize Homeworlds move builder.\n");
    return FALSE;
  }

  state = builder.builder_state;
  for (guint i = 0; i < move.step_count; ++i) {
    HomeworldsGoodMoveProofStatus status = {0};
    char step_text[128] = {0};

    if (!homeworlds_proof_probe_format_step(&move.steps[i], step_text, sizeof(step_text))) {
      g_snprintf(step_text, sizeof(step_text), "step-%u", i + 1);
    }
    if (prefix[0] != '\0') {
      g_strlcat(prefix, " ", sizeof(prefix));
    }
    g_strlcat(prefix, step_text, sizeof(prefix));

    if (!homeworlds_proof_probe_apply_step(state, &move.steps[i])) {
      g_printerr("Failed to apply prefix: %s\n", prefix);
      homeworlds_move_builder_clear(&builder);
      return FALSE;
    }

    g_print("  after %s\n", prefix);
    homeworlds_proof_probe_print_status(state, side, cutoff, &status);
    if (status.result == HOMEWORLDS_GOOD_MOVE_PROOF_REJECT) {
      g_print("    stop: rejected here\n");
      stopped = TRUE;
      break;
    }
  }

  if (!stopped && state->stage == HOMEWORLDS_BUILDER_STAGE_COMPLETE) {
    gboolean score_ok = FALSE;
    gint score = homeworlds_proof_probe_score_after_move(position, &move, &score_ok);

    if (score_ok) {
      g_print("  complete: final_score=%d %s cutoff\n",
              score,
              homeworlds_proof_probe_score_reaches_cutoff(side, score, cutoff) ? "reaches" : "below");
    }
  }

  homeworlds_move_builder_clear(&builder);
  return TRUE;
}

static void homeworlds_proof_probe_print_usage(const char *program_name) {
  g_printerr("usage: %s [--iterations COUNT] REPORT [REPORT_MOVE_ROW | MOVE_NOTATION]...\n", program_name);
  g_printerr("  --iterations COUNT  print #0 expansion, then COUNT selected scheduler branches.\n");
  g_printerr("If no rows or moves are provided, the first %u report move rows are probed.\n",
             HOMEWORLDS_PROOF_PROBE_DEFAULT_SAMPLE_COUNT);
}

int main(int argc, char **argv) {
  g_autoptr(GPtrArray) moves = g_ptr_array_new_with_free_func(homeworlds_proof_probe_move_free);
  HomeworldsPosition position = {0};
  HomeworldsGoodMoveTrace trace = {0};
  gboolean use_default_sample = FALSE;
  guint iteration_limit = 0;
  const char *report_path = NULL;
  guint side = 0;
  gint cutoff = 0;
  gboolean ok = TRUE;

  if (argc < 2) {
    homeworlds_proof_probe_print_usage(argv[0]);
    return 2;
  }

  for (gint i = 1; i < argc; ++i) {
    HomeworldsProofProbeMove *move = g_new0(HomeworldsProofProbeMove, 1);
    guint row = 0;

    if (g_strcmp0(argv[i], "--help") == 0) {
      homeworlds_proof_probe_print_usage(argv[0]);
      g_free(move);
      return 0;
    }
    if (g_str_has_prefix(argv[i], "--iterations=")) {
      if (!homeworlds_proof_probe_text_is_uint(argv[i] + strlen("--iterations="), &iteration_limit)) {
        homeworlds_proof_probe_print_usage(argv[0]);
        g_free(move);
        return 2;
      }
      g_free(move);
      continue;
    }
    if (g_strcmp0(argv[i], "--iterations") == 0) {
      if (i + 1 >= argc || !homeworlds_proof_probe_text_is_uint(argv[i + 1], &iteration_limit)) {
        homeworlds_proof_probe_print_usage(argv[0]);
        g_free(move);
        return 2;
      }
      i++;
      g_free(move);
      continue;
    }
    if (report_path == NULL) {
      report_path = argv[i];
      g_free(move);
      continue;
    }

    if (homeworlds_proof_probe_text_is_uint(argv[i], &row)) {
      move->row = row;
    } else {
      move->notation = g_strdup(argv[i]);
    }
    g_ptr_array_add(moves, move);
  }
  if (report_path == NULL) {
    homeworlds_proof_probe_print_usage(argv[0]);
    return 2;
  }
  use_default_sample = moves->len == 0 && iteration_limit == 0;

  if (!homeworlds_proof_probe_read_report(report_path, &position, moves, use_default_sample)) {
    return 1;
  }
  if (!homeworlds_proof_probe_all_requests_have_notation(moves)) {
    homeworlds_position_clear(&position);
    return 1;
  }

  side = homeworlds_position_turn(&position);
  if (!homeworlds_proof_probe_find_cutoff(&position, side, iteration_limit, &cutoff, &trace)) {
    homeworlds_position_clear(&position);
    return 1;
  }
  if (iteration_limit > 0 && !homeworlds_proof_probe_print_position(&position)) {
    homeworlds_proof_probe_trace_clear(&trace);
    homeworlds_position_clear(&position);
    return 1;
  }

  g_print("cutoff=%d\n", cutoff);
  g_print("trace: generated=%" G_GSIZE_FORMAT " scored=%" G_GSIZE_FORMAT " kept=%" G_GSIZE_FORMAT
          " checked=%" G_GSIZE_FORMAT " window=%" G_GSIZE_FORMAT " pruned=%" G_GSIZE_FORMAT
          " ordered=%" G_GSIZE_FORMAT " reordered_lists=%" G_GSIZE_FORMAT
          " reordered_candidates=%" G_GSIZE_FORMAT " single_step_passes=%" G_GSIZE_FORMAT
          " single_step_moves=%" G_GSIZE_FORMAT " goal_created=%" G_GSIZE_FORMAT
          " goal_selected=%" G_GSIZE_FORMAT " goal_split=%" G_GSIZE_FORMAT
          " goal_direct=%" G_GSIZE_FORMAT " goal_skipped=%" G_GSIZE_FORMAT
          " goal_exhausted=%" G_GSIZE_FORMAT "\n",
          trace.generated_leaves,
          trace.scored_moves,
          trace.kept_moves,
          trace.pruning_checked_branches,
          trace.pruning_window_cutoff_branches,
          trace.pruning_pruned_branches,
          trace.ordering_candidate_lists,
          trace.ordering_reordered_candidate_lists,
          trace.ordering_reordered_candidates,
          trace.ordering_single_step_passes,
          trace.ordering_single_step_moves,
          trace.goal_branches_created,
          trace.goal_branches_selected,
          trace.goal_branches_split,
          trace.goal_branches_direct,
          trace.goal_branches_skipped,
          trace.goal_branches_exhausted);

  if (iteration_limit > 0) {
    homeworlds_proof_probe_print_iteration_report(trace.goal_report, iteration_limit);
  }

  for (guint i = 0; i < moves->len; ++i) {
    ok = homeworlds_proof_probe_run_move(&position, side, cutoff, g_ptr_array_index(moves, i)) && ok;
  }

  homeworlds_proof_probe_trace_clear(&trace);
  homeworlds_position_clear(&position);
  return ok ? 0 : 1;
}
