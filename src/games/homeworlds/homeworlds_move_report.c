#include "homeworlds_move_report.h"

#include "homeworlds_backend.h"
#include "homeworlds_game.h"
#include "homeworlds_move_builder.h"

#include <string.h>

typedef struct {
  HomeworldsMove *moves;
  gsize count;
  gsize capacity;
} HomeworldsMoveReportBuffer;

typedef struct {
  guint system_index;
  HomeworldsColor color;
} HomeworldsMoveReportCatastropheChoice;

static gboolean homeworlds_move_report_moves_equal(const HomeworldsMove *left, const HomeworldsMove *right) {
  char left_text[128] = {0};
  char right_text[128] = {0};

  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  return homeworlds_move_format(left, left_text, sizeof(left_text)) &&
         homeworlds_move_format(right, right_text, sizeof(right_text)) &&
         strcmp(left_text, right_text) == 0;
}

static gboolean homeworlds_move_report_buffer_append(HomeworldsMoveReportBuffer *buffer,
                                                     const HomeworldsMove *move) {
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  for (gsize i = 0; i < buffer->count; ++i) {
    if (homeworlds_move_report_moves_equal(&buffer->moves[i], move)) {
      return TRUE;
    }
  }

  if (buffer->count == buffer->capacity) {
    gsize next_capacity = buffer->capacity == 0 ? 16 : buffer->capacity * 2;
    HomeworldsMove *next_moves = g_realloc_n(buffer->moves, next_capacity, sizeof(*next_moves));
    g_return_val_if_fail(next_moves != NULL, FALSE);
    buffer->moves = next_moves;
    buffer->capacity = next_capacity;
  }

  buffer->moves[buffer->count++] = *move;
  return TRUE;
}

static guint homeworlds_move_report_collect_catastrophe_choices(
    const HomeworldsMoveBuilderState *state,
    HomeworldsMoveReportCatastropheChoice *out_choices,
    guint max_choices) {
  guint count = 0;

  g_return_val_if_fail(state != NULL, 0);
  g_return_val_if_fail(out_choices != NULL || max_choices == 0, 0);

  if (state->working_position.phase != HOMEWORLDS_PHASE_PLAY ||
      state->move.step_count >= HOMEWORLDS_MAX_MOVE_STEPS ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP) {
    return 0;
  }

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &state->working_position.systems[system_index];

    for (guint color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
      if (homeworlds_system_color_count(system, (HomeworldsColor) color) < 4) {
        continue;
      }
      if (count < max_choices) {
        out_choices[count] = (HomeworldsMoveReportCatastropheChoice){
          .system_index = system_index,
          .color = (HomeworldsColor) color,
        };
      }
      count++;
    }
  }

  return MIN(count, max_choices);
}

static gboolean homeworlds_move_report_apply_catastrophe_choice(
    HomeworldsMoveBuilderState *state,
    const HomeworldsMoveReportCatastropheChoice *choice) {
  GameBackendMoveBuilder builder = {0};

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(choice != NULL, FALSE);
  g_return_val_if_fail(choice->system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);

  builder.builder_state = state;
  builder.builder_state_size = sizeof(*state);
  return homeworlds_move_builder_apply_catastrophe_step(&builder, choice->system_index, choice->color);
}

static gboolean homeworlds_move_report_collect_all_moves_recursive(const HomeworldsMoveBuilderState *state,
                                                                   HomeworldsMoveReportBuffer *buffer) {
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  HomeworldsMoveReportCatastropheChoice catastrophes[HOMEWORLDS_SYSTEM_SLOT_COUNT * 4] = {0};
  guint catastrophe_count = 0;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);

  builder.builder_state = (gpointer) state;
  builder.builder_state_size = sizeof(*state);
  if (state->move.step_count >= HOMEWORLDS_MAX_MOVE_STEPS &&
      !homeworlds_move_builder_is_complete(&builder)) {
    return TRUE;
  }

  if (homeworlds_move_builder_is_complete(&builder)) {
    HomeworldsMove move = {0};

    if (!homeworlds_move_builder_build_move(&builder, &move) ||
        !homeworlds_move_report_buffer_append(buffer, &move)) {
      return FALSE;
    }
  }

  catastrophe_count =
      homeworlds_move_report_collect_catastrophe_choices(state, catastrophes, G_N_ELEMENTS(catastrophes));
  for (guint i = 0; i < catastrophe_count; ++i) {
    HomeworldsMoveBuilderState child_state = *state;

    if (!homeworlds_move_report_apply_catastrophe_choice(&child_state, &catastrophes[i])) {
      continue;
    }
    if (!homeworlds_move_report_collect_all_moves_recursive(&child_state, buffer)) {
      return FALSE;
    }
  }

  if (homeworlds_move_builder_is_complete(&builder)) {
    return TRUE;
  }

  candidates = homeworlds_move_builder_list_candidates(&builder);
  for (guint pass = 0; pass < 2; ++pass) {
    gboolean want_pass = pass == 0;

    for (gsize i = 0; i < candidates.count; ++i) {
      const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *) candidates.moves)[i];
      HomeworldsMoveBuilderState child_state = *state;
      GameBackendMoveBuilder child = {
        .builder_state = &child_state,
        .builder_state_size = sizeof(child_state),
      };
      gboolean is_pass = candidate->data.kind == HOMEWORLDS_CANDIDATE_ACTION &&
                         candidate->data.target_color == HOMEWORLDS_STEP_PASS;

      if (is_pass != want_pass) {
        continue;
      }

      if (!homeworlds_move_builder_step(&child, candidate)) {
        continue;
      }
      if (!homeworlds_move_report_collect_all_moves_recursive(&child_state, buffer)) {
        homeworlds_game_backend.move_list_free(&candidates);
        return FALSE;
      }
    }
  }

  homeworlds_game_backend.move_list_free(&candidates);
  return TRUE;
}

static GameBackendMoveList homeworlds_move_report_list_all_moves(const HomeworldsPosition *position) {
  GameBackendMoveBuilder builder = {0};
  HomeworldsMoveReportBuffer buffer = {0};

  g_return_val_if_fail(position != NULL, (GameBackendMoveList){0});

  if (!homeworlds_move_builder_init(position, &builder)) {
    return (GameBackendMoveList){0};
  }
  if (!homeworlds_move_report_collect_all_moves_recursive(builder.builder_state, &buffer)) {
    homeworlds_move_builder_clear(&builder);
    g_free(buffer.moves);
    return (GameBackendMoveList){0};
  }

  homeworlds_move_builder_clear(&builder);
  return (GameBackendMoveList){
    .moves = buffer.moves,
    .count = buffer.count,
  };
}

static gboolean homeworlds_move_report_move_list_contains(const GameBackendMoveList *moves,
                                                          const HomeworldsMove *move) {
  g_return_val_if_fail(moves != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  for (gsize i = 0; i < moves->count; ++i) {
    const HomeworldsMove *candidate = homeworlds_game_backend.move_list_get(moves, i);

    if (candidate != NULL && homeworlds_move_report_moves_equal(candidate, move)) {
      return TRUE;
    }
  }

  return FALSE;
}

static void homeworlds_move_report_append_move_list_text(GString *text,
                                                         const GameBackendMoveList *moves,
                                                         const char *title,
                                                         const GameBackendMoveList *exclude) {
  guint displayed = 0;

  g_return_if_fail(text != NULL);
  g_return_if_fail(moves != NULL);
  g_return_if_fail(title != NULL);

  g_string_append_printf(text, "%s:\n", title);
  for (gsize i = 0; i < moves->count; ++i) {
    const HomeworldsMove *move = homeworlds_game_backend.move_list_get(moves, i);
    char notation[128] = {0};

    if (move == NULL || (exclude != NULL && homeworlds_move_report_move_list_contains(exclude, move))) {
      continue;
    }
    if (!homeworlds_move_format(move, notation, sizeof(notation))) {
      continue;
    }

    displayed++;
    g_string_append_printf(text, "%u. %s\n", displayed, notation);
  }

  if (displayed == 0) {
    g_string_append(text, "None\n");
  }
}

char *homeworlds_move_report_format(const HomeworldsPosition *position) {
  GameBackendMoveList good_moves = {0};
  GameBackendMoveList all_moves = {0};
  GString *text = NULL;
  g_autofree char *good_title = NULL;
  g_autofree char *other_title = NULL;

  g_return_val_if_fail(position != NULL, NULL);

  if (position->phase == HOMEWORLDS_PHASE_FINISHED) {
    return g_strdup("No moves.");
  }
  if (position->phase != HOMEWORLDS_PHASE_PLAY) {
    return g_strdup("Move report is available during play.");
  }

  good_moves = homeworlds_game_backend.list_good_moves(position, 0);
  all_moves = homeworlds_move_report_list_all_moves(position);
  text = g_string_new(NULL);
  g_return_val_if_fail(text != NULL, NULL);

  good_title = g_strdup_printf("good_moves() (%zu)", good_moves.count);
  other_title = g_strdup_printf("all possible moves minus good_moves() (%zu total before filtering)", all_moves.count);
  homeworlds_move_report_append_move_list_text(text, &good_moves, good_title, NULL);
  g_string_append_c(text, '\n');
  homeworlds_move_report_append_move_list_text(text, &all_moves, other_title, &good_moves);

  homeworlds_game_backend.move_list_free(&good_moves);
  g_free(all_moves.moves);
  return g_string_free(text, FALSE);
}
