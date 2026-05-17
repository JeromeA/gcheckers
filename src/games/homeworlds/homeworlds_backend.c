#include "homeworlds_backend.h"

#include "homeworlds_game.h"
#include "homeworlds_move_builder.h"
#include "homeworlds_sgf_position.h"

#include <string.h>

typedef struct {
  HomeworldsMove *moves;
  gsize count;
  gsize capacity;
} HomeworldsMoveBuffer;

static const char *homeworlds_backend_side_label(guint side) {
  switch (side) {
    case 0:
      return "Player 1";
    case 1:
      return "Player 2";
    default:
      g_debug("Unsupported Homeworlds side index");
      return "Player";
  }
}

static const char *homeworlds_backend_outcome_banner_text(GameBackendOutcome outcome) {
  switch (outcome) {
    case GAME_BACKEND_OUTCOME_SIDE_0_WIN:
      return "Player 1 wins";
    case GAME_BACKEND_OUTCOME_SIDE_1_WIN:
      return "Player 2 wins";
    case GAME_BACKEND_OUTCOME_DRAW:
      return "Draw";
    case GAME_BACKEND_OUTCOME_ONGOING:
    default:
      return NULL;
  }
}

static SgfColor homeworlds_backend_sgf_color_for_side(guint side) {
  switch (side) {
    case 0:
      return SGF_COLOR_BLACK;
    case 1:
      return SGF_COLOR_WHITE;
    default:
      g_debug("Unsupported Homeworlds side index for SGF color");
      return SGF_COLOR_NONE;
  }
}

static void homeworlds_backend_position_init(gpointer position, const GameBackendVariant * /*variant_or_null*/) {
  HomeworldsPosition *homeworlds_position = position;

  g_return_if_fail(homeworlds_position != NULL);

  homeworlds_position_init(homeworlds_position);
}

static void homeworlds_backend_position_clear(gpointer position) {
  HomeworldsPosition *homeworlds_position = position;

  g_return_if_fail(homeworlds_position != NULL);

  homeworlds_position_clear(homeworlds_position);
}

static void homeworlds_backend_position_copy(gpointer dest, gconstpointer src) {
  HomeworldsPosition *dest_position = dest;
  const HomeworldsPosition *src_position = src;

  g_return_if_fail(dest_position != NULL);
  g_return_if_fail(src_position != NULL);

  homeworlds_position_copy(dest_position, src_position);
}

static GameBackendOutcome homeworlds_backend_position_outcome(gconstpointer position) {
  const HomeworldsPosition *homeworlds_position = position;

  g_return_val_if_fail(homeworlds_position != NULL, GAME_BACKEND_OUTCOME_ONGOING);

  return homeworlds_position_outcome(homeworlds_position);
}

static guint homeworlds_backend_position_turn(gconstpointer position) {
  const HomeworldsPosition *homeworlds_position = position;

  g_return_val_if_fail(homeworlds_position != NULL, 0);

  return homeworlds_position_turn(homeworlds_position);
}

static void homeworlds_backend_move_list_free(GameBackendMoveList *moves) {
  g_return_if_fail(moves != NULL);

  g_clear_pointer(&moves->moves, g_free);
  moves->count = 0;
}

static const void *homeworlds_backend_move_list_get(const GameBackendMoveList *moves, gsize index) {
  g_return_val_if_fail(moves != NULL, NULL);
  g_return_val_if_fail(index < moves->count, NULL);
  g_return_val_if_fail(moves->moves != NULL, NULL);

  return ((const guint8 *) moves->moves) + (index * sizeof(HomeworldsMove));
}

static gboolean homeworlds_backend_moves_equal(gconstpointer left, gconstpointer right) {
  char left_text[128] = {0};
  char right_text[128] = {0};

  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  return homeworlds_move_format(left, left_text, sizeof(left_text)) &&
         homeworlds_move_format(right, right_text, sizeof(right_text)) &&
         strcmp(left_text, right_text) == 0;
}

static gboolean homeworlds_backend_move_buffer_append(HomeworldsMoveBuffer *buffer, const HomeworldsMove *move) {
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

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

static guint homeworlds_backend_setup_star_size_mask(const HomeworldsMove *move) {
  guint mask = 0;

  g_return_val_if_fail(move != NULL, 0);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    HomeworldsPyramid star = move->setup_stars[i];
    if (!homeworlds_pyramid_is_valid(star)) {
      return 0;
    }

    mask |= 1u << (homeworlds_pyramid_size(star) - 1);
  }

  return mask;
}

static guint homeworlds_backend_homeworld_star_size_mask(const HomeworldsPosition *position, guint side) {
  guint mask = 0;

  g_return_val_if_fail(position != NULL, 0);
  g_return_val_if_fail(side < 2, 0);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    HomeworldsPyramid star = position->systems[side].stars[i];
    if (!homeworlds_pyramid_is_valid(star)) {
      return 0;
    }

    mask |= 1u << (homeworlds_pyramid_size(star) - 1);
  }

  return mask;
}

static gboolean homeworlds_backend_setup_colors_are_distinct(const HomeworldsMove *move) {
  gboolean seen_colors[4] = {FALSE};

  g_return_val_if_fail(move != NULL, FALSE);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    HomeworldsPyramid star = move->setup_stars[i];
    if (!homeworlds_pyramid_is_valid(star)) {
      return FALSE;
    }

    HomeworldsColor color = homeworlds_pyramid_color(star);
    if (seen_colors[color]) {
      return FALSE;
    }
    seen_colors[color] = TRUE;
  }

  if (!homeworlds_pyramid_is_valid(move->setup_ship)) {
    return FALSE;
  }

  HomeworldsColor ship_color = homeworlds_pyramid_color(move->setup_ship);
  if (seen_colors[ship_color]) {
    return FALSE;
  }

  return TRUE;
}

static gboolean homeworlds_backend_setup_includes_green(const HomeworldsMove *move) {
  g_return_val_if_fail(move != NULL, FALSE);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    HomeworldsPyramid star = move->setup_stars[i];
    if (homeworlds_pyramid_is_valid(star) && homeworlds_pyramid_color(star) == HOMEWORLDS_COLOR_GREEN) {
      return TRUE;
    }
  }

  return homeworlds_pyramid_is_valid(move->setup_ship) &&
         homeworlds_pyramid_color(move->setup_ship) == HOMEWORLDS_COLOR_GREEN;
}

static gboolean homeworlds_backend_setup_move_is_good(const HomeworldsMoveBuilderState *state,
                                                      const HomeworldsMove *move) {
  guint star_size_mask = 0;
  guint side = 0;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  side = state->working_position.turn;
  if (move->kind != HOMEWORLDS_MOVE_KIND_SETUP || side > 1 || !homeworlds_backend_setup_colors_are_distinct(move) ||
      homeworlds_pyramid_size(move->setup_ship) != HOMEWORLDS_SIZE_LARGE) {
    return FALSE;
  }
  if (side == 0 && !homeworlds_backend_setup_includes_green(move)) {
    return FALSE;
  }

  star_size_mask = homeworlds_backend_setup_star_size_mask(move);
  if (star_size_mask == 0 || (star_size_mask & (star_size_mask - 1)) == 0) {
    return FALSE;
  }

  if (side == 1 && star_size_mask == homeworlds_backend_homeworld_star_size_mask(&state->working_position, 0)) {
    return FALSE;
  }

  return TRUE;
}

static gboolean homeworlds_backend_position_is_initial_turn(const HomeworldsPosition *position) {
  g_return_val_if_fail(position != NULL, FALSE);

  if (position->phase != HOMEWORLDS_PHASE_PLAY || position->turn != 0 ||
      homeworlds_system_ship_count_for_side(&position->systems[0], 0) != 1 ||
      homeworlds_system_ship_count_for_side(&position->systems[1], 1) != 1) {
    return FALSE;
  }

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &position->systems[system_index];

    if (system_index != 0 && homeworlds_system_ship_count_for_side(system, 0) != 0) {
      return FALSE;
    }
    if (system_index != 1 && homeworlds_system_ship_count_for_side(system, 1) != 0) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean homeworlds_backend_selected_ship_is_last_homeworld_ship(const HomeworldsMoveBuilderState *state) {
  guint side = 0;

  g_return_val_if_fail(state != NULL, FALSE);

  side = state->working_position.turn;
  if (state->selected_system_index != side) {
    return FALSE;
  }

  return homeworlds_system_ship_count_for_side(&state->working_position.systems[side], side) == 1;
}

static gboolean homeworlds_backend_construct_would_overpopulate_without_targets(
    const HomeworldsMoveBuilderState *state) {
  const HomeworldsSystem *system = NULL;
  HomeworldsPyramid source = 0;
  HomeworldsPyramid built = 0;
  guint selected_ship_slot = 0;
  guint side = 0;
  guint opponent = 0;
  gboolean found_selected_ship = FALSE;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(state->selected_system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);

  side = state->working_position.turn;
  opponent = side == 0 ? 1 : 0;
  system = &state->working_position.systems[state->selected_system_index];
  if (homeworlds_system_ship_count_for_side(system, opponent) != 0) {
    return FALSE;
  }

  for (guint ship_slot = 0; ship_slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++ship_slot) {
    if (system->ships[side][ship_slot] != state->selected_ship_pyramid) {
      continue;
    }

    selected_ship_slot = ship_slot;
    found_selected_ship = TRUE;
    break;
  }

  if (!found_selected_ship) {
    return FALSE;
  }

  source = system->ships[side][selected_ship_slot];
  if (!homeworlds_pyramid_is_valid(source) ||
      !homeworlds_system_find_smallest_bank_ship(&state->working_position,
                                                 homeworlds_pyramid_color(source),
                                                 &built)) {
    return FALSE;
  }

  return homeworlds_system_color_count(system, homeworlds_pyramid_color(built)) >= 3;
}

static gboolean homeworlds_backend_move_is_good(const HomeworldsMoveBuilderState *state, const HomeworldsMove *move) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  if (move->kind == HOMEWORLDS_MOVE_KIND_SETUP) {
    return homeworlds_backend_setup_move_is_good(state, move);
  }

  if (homeworlds_backend_position_is_initial_turn(&state->working_position) &&
      (move->step_count != 1 || move->steps[0].kind != HOMEWORLDS_STEP_CONSTRUCT)) {
    return FALSE;
  }

  for (guint i = 0; i < move->step_count; ++i) {
    if (move->steps[i].kind == HOMEWORLDS_STEP_PASS) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean homeworlds_backend_candidate_is_good(const HomeworldsMoveBuilderState *state,
                                                     const HomeworldsMoveCandidate *candidate) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(candidate != NULL, FALSE);

  if (state->stage == HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP &&
      candidate->data.kind == HOMEWORLDS_CANDIDATE_ACTION &&
      candidate->data.target_color == HOMEWORLDS_STEP_PASS) {
    return FALSE;
  }
  if (state->stage == HOMEWORLDS_BUILDER_STAGE_SELECT_ACTION &&
      candidate->data.kind == HOMEWORLDS_CANDIDATE_ACTION) {
    if (homeworlds_backend_position_is_initial_turn(&state->working_position) &&
        candidate->data.target_color != HOMEWORLDS_STEP_CONSTRUCT) {
      return FALSE;
    }
    if (homeworlds_backend_selected_ship_is_last_homeworld_ship(state) &&
        (candidate->data.target_color == HOMEWORLDS_STEP_MOVE ||
         candidate->data.target_color == HOMEWORLDS_STEP_SACRIFICE)) {
      return FALSE;
    }
    if (candidate->data.target_color == HOMEWORLDS_STEP_CONSTRUCT &&
        homeworlds_backend_construct_would_overpopulate_without_targets(state)) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean homeworlds_backend_collect_good_moves_recursive(const HomeworldsMoveBuilderState *state,
                                                                HomeworldsMoveBuffer *buffer) {
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);

  builder.builder_state = (gpointer) state;
  builder.builder_state_size = sizeof(*state);

  if (homeworlds_move_builder_is_complete(&builder)) {
    HomeworldsMove move = {0};

    if (!homeworlds_move_builder_build_move(&builder, &move)) {
      return FALSE;
    }
    if (!homeworlds_backend_move_is_good(state, &move)) {
      return TRUE;
    }

    return homeworlds_backend_move_buffer_append(buffer, &move);
  }

  candidates = homeworlds_move_builder_list_candidates(&builder);
  for (gsize i = 0; i < candidates.count; ++i) {
    const HomeworldsMoveCandidate *candidate = homeworlds_backend_move_list_get(&candidates, i);
    HomeworldsMoveBuilderState child_state = *state;
    GameBackendMoveBuilder child = {
      .builder_state = &child_state,
      .builder_state_size = sizeof(child_state),
    };

    if (candidate == NULL || !homeworlds_backend_candidate_is_good(state, candidate) ||
        !homeworlds_move_builder_step(&child, candidate)) {
      continue;
    }
    if (!homeworlds_backend_collect_good_moves_recursive(&child_state, buffer)) {
      homeworlds_backend_move_list_free(&candidates);
      return FALSE;
    }
  }

  homeworlds_backend_move_list_free(&candidates);
  return TRUE;
}

static GameBackendMoveList homeworlds_backend_list_good_moves(gconstpointer position, guint /*depth_hint*/) {
  GameBackendMoveBuilder builder = {0};
  HomeworldsMoveBuffer buffer = {0};

  g_return_val_if_fail(position != NULL, (GameBackendMoveList){0});

  if (!homeworlds_move_builder_init(position, &builder)) {
    return (GameBackendMoveList){0};
  }
  if (!homeworlds_backend_collect_good_moves_recursive(builder.builder_state, &buffer)) {
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

static gboolean homeworlds_backend_apply_move(gpointer position, gconstpointer move) {
  HomeworldsPosition *homeworlds_position = position;
  const HomeworldsMove *homeworlds_move = move;

  g_return_val_if_fail(homeworlds_position != NULL, FALSE);
  g_return_val_if_fail(homeworlds_move != NULL, FALSE);

  return homeworlds_position_apply_move(homeworlds_position, homeworlds_move);
}

static gint homeworlds_backend_evaluate_static(gconstpointer position) {
  const HomeworldsPosition *homeworlds_position = position;

  g_return_val_if_fail(homeworlds_position != NULL, 0);

  return homeworlds_position_evaluate_static(homeworlds_position);
}

static gint homeworlds_backend_terminal_score(gconstpointer position, GameBackendOutcome outcome, guint ply_depth) {
  g_return_val_if_fail(position != NULL, 0);

  return homeworlds_position_terminal_score(outcome, ply_depth);
}

static guint64 homeworlds_backend_hash_position(gconstpointer position) {
  const HomeworldsPosition *homeworlds_position = position;

  g_return_val_if_fail(homeworlds_position != NULL, 0);

  return homeworlds_position_hash(homeworlds_position);
}

static gboolean homeworlds_backend_format_move(gconstpointer move, char *buffer, gsize size) {
  const HomeworldsMove *homeworlds_move = move;

  g_return_val_if_fail(homeworlds_move != NULL, FALSE);

  return homeworlds_move_format(homeworlds_move, buffer, size);
}

static gboolean homeworlds_backend_parse_move(const char *notation, gpointer out_move) {
  HomeworldsMove *homeworlds_move = out_move;

  g_return_val_if_fail(notation != NULL, FALSE);
  g_return_val_if_fail(homeworlds_move != NULL, FALSE);

  return homeworlds_move_parse(notation, homeworlds_move);
}

const GameBackend homeworlds_game_backend = {
  .id = "homeworlds",
  .display_name = "Homeworlds",
  .variant_count = 0,
  .position_size = sizeof(HomeworldsPosition),
  .move_size = sizeof(HomeworldsMove),
  .supports_move_list = FALSE,
  .supports_move_builder = TRUE,
  .supports_ai_search = TRUE,
  .side_label = homeworlds_backend_side_label,
  .sgf_color_for_side = homeworlds_backend_sgf_color_for_side,
  .outcome_banner_text = homeworlds_backend_outcome_banner_text,
  .position_init = homeworlds_backend_position_init,
  .position_clear = homeworlds_backend_position_clear,
  .position_copy = homeworlds_backend_position_copy,
  .position_outcome = homeworlds_backend_position_outcome,
  .position_turn = homeworlds_backend_position_turn,
  .list_good_moves = homeworlds_backend_list_good_moves,
  .move_list_free = homeworlds_backend_move_list_free,
  .move_list_get = homeworlds_backend_move_list_get,
  .moves_equal = homeworlds_backend_moves_equal,
  .move_builder_init = (gboolean (*)(gconstpointer, GameBackendMoveBuilder *)) homeworlds_move_builder_init,
  .move_builder_clear = homeworlds_move_builder_clear,
  .move_builder_list_candidates = (GameBackendMoveList (*)(const GameBackendMoveBuilder *))
      homeworlds_move_builder_list_candidates,
  .move_builder_step = (gboolean (*)(GameBackendMoveBuilder *, gconstpointer)) homeworlds_move_builder_step,
  .move_builder_is_complete = (gboolean (*)(const GameBackendMoveBuilder *)) homeworlds_move_builder_is_complete,
  .move_builder_build_move = (gboolean (*)(const GameBackendMoveBuilder *, gpointer))
      homeworlds_move_builder_build_move,
  .apply_move = homeworlds_backend_apply_move,
  .evaluate_static = homeworlds_backend_evaluate_static,
  .terminal_score = homeworlds_backend_terminal_score,
  .hash_position = homeworlds_backend_hash_position,
  .format_move = homeworlds_backend_format_move,
  .parse_move = homeworlds_backend_parse_move,
  .sgf_apply_setup_node = homeworlds_sgf_position_apply_setup_node,
  .sgf_write_position_node = homeworlds_sgf_position_write_position_node,
  .supports_square_grid_board = FALSE,
};
