#include "games/homeworlds/homeworlds_backend.h"
#include "games/homeworlds/homeworlds_game.h"

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
  HOMEWORLDS_PROOF_PROBE_SECTION_ALL_MOVES,
} HomeworldsProofProbeReportSection;

typedef struct {
  guint row;
  char *notation;
} HomeworldsProofProbeMove;

typedef struct {
  HomeworldsGoodMoveTrace trace;
  gboolean called;
} HomeworldsProofProbeTraceCapture;

static void homeworlds_proof_probe_capture_trace(const HomeworldsGoodMoveTrace *trace, gpointer user_data) {
  HomeworldsProofProbeTraceCapture *capture = user_data;

  g_return_if_fail(trace != NULL);
  g_return_if_fail(capture != NULL);

  capture->trace = *trace;
  capture->called = TRUE;
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
  gboolean saw_all_moves = FALSE;

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
    if (g_strcmp0(stripped_line, "all_moves:") == 0) {
      section = HOMEWORLDS_PROOF_PROBE_SECTION_ALL_MOVES;
      saw_all_moves = TRUE;
      continue;
    }
    if (g_strcmp0(stripped_line, "position:") == 0 ||
        g_str_has_prefix(stripped_line, "all_moves_streamed:")) {
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

    if (section == HOMEWORLDS_PROOF_PROBE_SECTION_ALL_MOVES) {
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
    g_printerr("No all_moves rows found in %s.\n", path);
    homeworlds_position_clear(position);
    return FALSE;
  }
  if (!use_default_sample && homeworlds_proof_probe_has_unresolved_rows(moves)) {
    g_printerr("One or more requested all_moves rows were not found in %s.\n", path);
    homeworlds_position_clear(position);
    return FALSE;
  }
  if (!saw_all_moves && use_default_sample) {
    g_printerr("No all_moves section found in %s.\n", path);
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

static const char *homeworlds_proof_probe_pruning_mode_name(HomeworldsGoodMovePruningMode mode) {
  switch (mode) {
    case HOMEWORLDS_GOOD_MOVE_PRUNING_ON:
      return "on";
    case HOMEWORLDS_GOOD_MOVE_PRUNING_VERIFY:
      return "verify";
    case HOMEWORLDS_GOOD_MOVE_PRUNING_OFF:
    default:
      return "off";
  }
}

static gboolean homeworlds_proof_probe_find_cutoff(const HomeworldsPosition *position,
                                                   guint side,
                                                   gboolean force_pruning_off,
                                                   gint *out_cutoff,
                                                   HomeworldsGoodMoveTrace *out_trace) {
  GameBackendMoveList moves = {0};
  const HomeworldsMove *cutoff_move = NULL;
  HomeworldsProofProbeTraceCapture trace_capture = {0};
  gboolean score_ok = FALSE;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(out_cutoff != NULL, FALSE);
  g_return_val_if_fail(out_trace != NULL, FALSE);

  if (force_pruning_off) {
    g_setenv("GCHECKERS_HOMEWORLDS_GOOD_MOVE_PRUNING", "off", TRUE);
  }
  homeworlds_backend_set_good_move_trace(homeworlds_proof_probe_capture_trace, &trace_capture);
  moves = homeworlds_game_backend.list_good_moves(position, 0);
  homeworlds_backend_set_good_move_trace(NULL, NULL);
  if (moves.count == 0) {
    g_printerr("No good moves are available from the report position.\n");
    return FALSE;
  }
  if (!trace_capture.called) {
    g_printerr("Failed to capture good_moves() trace for the report position.\n");
    homeworlds_game_backend.move_list_free(&moves);
    return FALSE;
  }

  cutoff_move = homeworlds_game_backend.move_list_get(&moves, moves.count - 1);
  g_return_val_if_fail(cutoff_move != NULL, FALSE);
  *out_cutoff = homeworlds_proof_probe_score_after_move(position, cutoff_move, &score_ok);
  *out_trace = trace_capture.trace;
  homeworlds_game_backend.move_list_free(&moves);
  if (!score_ok) {
    g_printerr("Failed to score the cutoff move.\n");
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

  if (!homeworlds_backend_describe_large_yellow_sacrifice_proof(state, side, cutoff, &status)) {
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
  g_printerr("usage: %s REPORT [ALL_MOVE_ROW | MOVE_NOTATION]...\n", program_name);
  g_printerr("If no rows or moves are provided, the first %u all_moves rows are probed.\n",
             HOMEWORLDS_PROOF_PROBE_DEFAULT_SAMPLE_COUNT);
  g_printerr("Use --current-good-move-mode to keep the caller's pruning env while recomputing good_moves().\n");
}

int main(int argc, char **argv) {
  g_autoptr(GPtrArray) moves = g_ptr_array_new_with_free_func(homeworlds_proof_probe_move_free);
  HomeworldsPosition position = {0};
  HomeworldsGoodMoveTrace trace = {0};
  gboolean use_default_sample = FALSE;
  gboolean force_pruning_off = TRUE;
  guint side = 0;
  gint cutoff = 0;
  gboolean ok = TRUE;

  if (argc < 2 || g_strcmp0(argv[1], "--help") == 0) {
    homeworlds_proof_probe_print_usage(argv[0]);
    return argc < 2 ? 2 : 0;
  }

  for (gint i = 2; i < argc; ++i) {
    HomeworldsProofProbeMove *move = g_new0(HomeworldsProofProbeMove, 1);
    guint row = 0;

    if (g_strcmp0(argv[i], "--current-good-move-mode") == 0) {
      g_free(move);
      force_pruning_off = FALSE;
      continue;
    }
    if (homeworlds_proof_probe_text_is_uint(argv[i], &row)) {
      move->row = row;
    } else {
      move->notation = g_strdup(argv[i]);
    }
    g_ptr_array_add(moves, move);
  }
  use_default_sample = moves->len == 0;

  if (!homeworlds_proof_probe_read_report(argv[1], &position, moves, use_default_sample)) {
    return 1;
  }
  if (!homeworlds_proof_probe_all_requests_have_notation(moves)) {
    homeworlds_position_clear(&position);
    return 1;
  }

  side = homeworlds_position_turn(&position);
  if (!homeworlds_proof_probe_find_cutoff(&position, side, force_pruning_off, &cutoff, &trace)) {
    homeworlds_position_clear(&position);
    return 1;
  }
  g_print("cutoff=%d\n", cutoff);
  g_print("trace: generated=%" G_GSIZE_FORMAT " scored=%" G_GSIZE_FORMAT " kept=%" G_GSIZE_FORMAT
          " pruning=%s ordering=%s checked=%" G_GSIZE_FORMAT " window=%" G_GSIZE_FORMAT
          " would=%" G_GSIZE_FORMAT " pruned=%" G_GSIZE_FORMAT " verified=%" G_GSIZE_FORMAT
          " failures=%" G_GSIZE_FORMAT " ordered=%" G_GSIZE_FORMAT " reordered_lists=%" G_GSIZE_FORMAT
          " reordered_candidates=%" G_GSIZE_FORMAT "\n",
          trace.generated_leaves,
          trace.scored_moves,
          trace.kept_moves,
          homeworlds_proof_probe_pruning_mode_name(trace.pruning_mode),
          trace.ordering_enabled ? "on" : "off",
          trace.pruning_checked_branches,
          trace.pruning_window_cutoff_branches,
          trace.pruning_would_prune_branches,
          trace.pruning_pruned_branches,
          trace.pruning_verified_leaves,
          trace.pruning_verification_failures,
          trace.ordering_candidate_lists,
          trace.ordering_reordered_candidate_lists,
          trace.ordering_reordered_candidates);

  for (guint i = 0; i < moves->len; ++i) {
    ok = homeworlds_proof_probe_run_move(&position, side, cutoff, g_ptr_array_index(moves, i)) && ok;
  }

  homeworlds_position_clear(&position);
  return ok ? 0 : 1;
}
