#include "homeworlds_backend.h"

#include "homeworlds_game.h"
#include "homeworlds_move_builder.h"
#include "homeworlds_sgf_position.h"

#include <stdlib.h>
#include <string.h>

enum {
  HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_LIMIT = 512,
  HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_WINDOW = 50,
};

typedef struct {
  guint system_index;
  HomeworldsColor color;
  HomeworldsSystemRef system_ref;
} HomeworldsProfitableCatastrophe;

typedef struct {
  HomeworldsProfitableCatastrophe root_catastrophes[HOMEWORLDS_SYSTEM_SLOT_COUNT * 4];
  guint root_catastrophe_count;
} HomeworldsGoodMoveContext;

typedef struct {
  HomeworldsMove move;
  gint score;
  gsize original_index;
} HomeworldsScoredMove;

typedef struct {
  const HomeworldsPosition *position;
  HomeworldsScoredMove *moves;
  gsize count;
  gsize capacity;
  gsize leaves_seen;
  gsize scored_moves;
  gsize next_original_index;
  guint side;
  gint best_score;
  gboolean has_best_score;
  gboolean prune_by_score;
} HomeworldsMoveBuffer;

static HomeworldsGoodMoveTraceFunc homeworlds_backend_good_move_trace_func = NULL;
static gpointer homeworlds_backend_good_move_trace_user_data = NULL;

static gboolean homeworlds_backend_score_after_move(const HomeworldsPosition *position,
                                                    const HomeworldsMove *move,
                                                    gint *out_score);
static gboolean homeworlds_backend_score_is_inside_prune_window(guint side, gint score, gint best_score);

void homeworlds_backend_set_good_move_trace(HomeworldsGoodMoveTraceFunc trace_func, gpointer user_data) {
  homeworlds_backend_good_move_trace_func = trace_func;
  homeworlds_backend_good_move_trace_user_data = user_data;
}

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

  homeworlds_move_list_free(moves);
}

static const void *homeworlds_backend_move_list_get(const GameBackendMoveList *moves, gsize index) {
  g_return_val_if_fail(moves != NULL, NULL);

  return homeworlds_move_list_get(moves, index);
}

static gboolean homeworlds_backend_system_refs_equal(const HomeworldsSystemRef *left,
                                                     const HomeworldsSystemRef *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  if (left->kind != right->kind) {
    return FALSE;
  }

  switch ((HomeworldsSystemRefKind)left->kind) {
    case HOMEWORLDS_SYSTEM_REF_HOMEWORLD:
      return left->homeworld_side == right->homeworld_side;
    case HOMEWORLDS_SYSTEM_REF_SYSTEM:
      return left->system_index == right->system_index && left->star == right->star;
    case HOMEWORLDS_SYSTEM_REF_NONE:
    default:
      return TRUE;
  }
}

static gboolean homeworlds_backend_moves_equal(gconstpointer left, gconstpointer right) {
  const HomeworldsMove *left_move = left;
  const HomeworldsMove *right_move = right;

  g_return_val_if_fail(left_move != NULL, FALSE);
  g_return_val_if_fail(right_move != NULL, FALSE);

  return homeworlds_moves_equal(left_move, right_move);
}

static gboolean homeworlds_backend_profitable_catastrophes_equal(const HomeworldsProfitableCatastrophe *left,
                                                                 const HomeworldsProfitableCatastrophe *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  return left->color == right->color &&
         homeworlds_backend_system_refs_equal(&left->system_ref, &right->system_ref);
}

static gboolean homeworlds_backend_move_has_profitable_catastrophe(
    const HomeworldsMove *move,
    const HomeworldsProfitableCatastrophe *catastrophe) {
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(catastrophe != NULL, FALSE);

  if (move->kind != HOMEWORLDS_MOVE_KIND_TURN) {
    return FALSE;
  }

  for (guint i = 0; i < move->step_count; ++i) {
    const HomeworldsTurnStep *step = &move->steps[i];

    if (step->kind == HOMEWORLDS_STEP_CATASTROPHE &&
        step->target_color == catastrophe->color &&
        homeworlds_backend_system_refs_equal(&step->target_system, &catastrophe->system_ref)) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean homeworlds_backend_move_satisfies_root_catastrophe_requirement(
    const HomeworldsMove *move,
    const HomeworldsGoodMoveContext *context) {
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);

  if (context->root_catastrophe_count == 0) {
    return TRUE;
  }

  for (guint i = 0; i < context->root_catastrophe_count; ++i) {
    if (homeworlds_backend_move_has_profitable_catastrophe(move, &context->root_catastrophes[i])) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean homeworlds_backend_catastrophe_is_root_required(
    const HomeworldsGoodMoveContext *context,
    const HomeworldsProfitableCatastrophe *catastrophe) {
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(catastrophe != NULL, FALSE);

  for (guint i = 0; i < context->root_catastrophe_count; ++i) {
    if (homeworlds_backend_profitable_catastrophes_equal(catastrophe, &context->root_catastrophes[i])) {
      return TRUE;
    }
  }

  return FALSE;
}

static void homeworlds_backend_move_buffer_clear(HomeworldsMoveBuffer *buffer) {
  g_return_if_fail(buffer != NULL);

  g_clear_pointer(&buffer->moves, g_free);
  buffer->position = NULL;
  buffer->count = 0;
  buffer->capacity = 0;
  buffer->leaves_seen = 0;
  buffer->scored_moves = 0;
  buffer->next_original_index = 0;
  buffer->side = 0;
  buffer->best_score = 0;
  buffer->has_best_score = FALSE;
  buffer->prune_by_score = FALSE;
}

static void homeworlds_backend_move_buffer_init(HomeworldsMoveBuffer *buffer, const HomeworldsPosition *position) {
  g_return_if_fail(buffer != NULL);
  g_return_if_fail(position != NULL);

  memset(buffer, 0, sizeof(*buffer));
  buffer->position = position;
  buffer->side = position->turn;
  buffer->prune_by_score = position->phase == HOMEWORLDS_PHASE_PLAY;
}

static gboolean homeworlds_backend_score_is_better(guint side, gint score, gint other_score) {
  g_return_val_if_fail(side < 2, FALSE);

  if (side == 0) {
    return score > other_score;
  }
  return score < other_score;
}

static gint homeworlds_backend_scored_move_order_compare(guint side,
                                                         const HomeworldsScoredMove *left,
                                                         const HomeworldsScoredMove *right) {
  g_return_val_if_fail(side < 2, 0);
  g_return_val_if_fail(left != NULL, 0);
  g_return_val_if_fail(right != NULL, 0);

  if (homeworlds_backend_score_is_better(side, left->score, right->score)) {
    return -1;
  }
  if (homeworlds_backend_score_is_better(side, right->score, left->score)) {
    return 1;
  }
  if (left->original_index < right->original_index) {
    return -1;
  }
  if (left->original_index > right->original_index) {
    return 1;
  }
  return 0;
}

static gboolean homeworlds_backend_move_buffer_reserve_slot(HomeworldsMoveBuffer *buffer) {
  gsize next_capacity = 0;

  g_return_val_if_fail(buffer != NULL, FALSE);

  if (buffer->count < buffer->capacity) {
    return TRUE;
  }

  next_capacity = buffer->capacity == 0 ? 16 : buffer->capacity * 2;
  if (buffer->prune_by_score) {
    next_capacity = MIN(next_capacity, (gsize) HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_LIMIT);
  }
  g_return_val_if_fail(next_capacity > buffer->capacity, FALSE);

  HomeworldsScoredMove *next_moves = g_realloc_n(buffer->moves, next_capacity, sizeof(*next_moves));
  g_return_val_if_fail(next_moves != NULL, FALSE);
  buffer->moves = next_moves;
  buffer->capacity = next_capacity;
  return TRUE;
}

static void homeworlds_backend_move_buffer_remove_at(HomeworldsMoveBuffer *buffer, gsize index) {
  g_return_if_fail(buffer != NULL);
  g_return_if_fail(index < buffer->count);

  if (index + 1 < buffer->count) {
    memmove(&buffer->moves[index],
            &buffer->moves[index + 1],
            (buffer->count - index - 1) * sizeof(buffer->moves[0]));
  }
  buffer->count--;
}

static void homeworlds_backend_move_buffer_prune_score_window(HomeworldsMoveBuffer *buffer) {
  gsize i = 0;

  g_return_if_fail(buffer != NULL);

  while (i < buffer->count) {
    if (homeworlds_backend_score_is_inside_prune_window(buffer->side, buffer->moves[i].score, buffer->best_score)) {
      i++;
      continue;
    }
    homeworlds_backend_move_buffer_remove_at(buffer, i);
  }
}

static gboolean homeworlds_backend_move_buffer_insert_scored(HomeworldsMoveBuffer *buffer,
                                                             const HomeworldsScoredMove *scored_move) {
  gsize index = 0;

  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(scored_move != NULL, FALSE);

  if (!homeworlds_backend_move_buffer_reserve_slot(buffer)) {
    return FALSE;
  }

  while (index < buffer->count &&
         homeworlds_backend_scored_move_order_compare(buffer->side, &buffer->moves[index], scored_move) <= 0) {
    index++;
  }
  if (index < buffer->count) {
    memmove(&buffer->moves[index + 1], &buffer->moves[index], (buffer->count - index) * sizeof(buffer->moves[0]));
  }

  buffer->moves[index] = *scored_move;
  buffer->count++;
  return TRUE;
}

static gboolean homeworlds_backend_move_buffer_append_unsorted(HomeworldsMoveBuffer *buffer,
                                                               const HomeworldsMove *move) {
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  if (!homeworlds_backend_move_buffer_reserve_slot(buffer)) {
    return FALSE;
  }

  buffer->moves[buffer->count++] = (HomeworldsScoredMove){
    .move = *move,
    .score = 0,
    .original_index = buffer->next_original_index++,
  };
  return TRUE;
}

static gboolean homeworlds_backend_move_buffer_append_scored(HomeworldsMoveBuffer *buffer, const HomeworldsMove *move) {
  HomeworldsScoredMove scored_move = {0};
  gint score = 0;

  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(buffer->position != NULL, FALSE);

  if (!homeworlds_backend_score_after_move(buffer->position, move, &score)) {
    return FALSE;
  }

  buffer->scored_moves++;
  scored_move = (HomeworldsScoredMove){
    .move = *move,
    .score = score,
    .original_index = buffer->next_original_index++,
  };

  if (!buffer->has_best_score || homeworlds_backend_score_is_better(buffer->side, score, buffer->best_score)) {
    buffer->best_score = score;
    buffer->has_best_score = TRUE;
    homeworlds_backend_move_buffer_prune_score_window(buffer);
  } else if (!homeworlds_backend_score_is_inside_prune_window(buffer->side, score, buffer->best_score)) {
    return TRUE;
  }

  if (buffer->count == HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_LIMIT) {
    g_return_val_if_fail(buffer->count > 0, FALSE);
    if (homeworlds_backend_scored_move_order_compare(buffer->side, &scored_move, &buffer->moves[buffer->count - 1]) >=
        0) {
      return TRUE;
    }
    homeworlds_backend_move_buffer_remove_at(buffer, buffer->count - 1);
  }

  return homeworlds_backend_move_buffer_insert_scored(buffer, &scored_move);
}

static gboolean homeworlds_backend_move_buffer_append(HomeworldsMoveBuffer *buffer, const HomeworldsMove *move) {
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  buffer->leaves_seen++;
  if (buffer->prune_by_score) {
    return homeworlds_backend_move_buffer_append_scored(buffer, move);
  }
  return homeworlds_backend_move_buffer_append_unsorted(buffer, move);
}

static HomeworldsMove *homeworlds_backend_move_buffer_copy_moves(const HomeworldsMoveBuffer *buffer) {
  HomeworldsMove *moves = NULL;

  g_return_val_if_fail(buffer != NULL, NULL);

  if (buffer->count == 0) {
    return NULL;
  }

  moves = g_new0(HomeworldsMove, buffer->count);
  for (gsize i = 0; i < buffer->count; ++i) {
    moves[i] = buffer->moves[i].move;
  }
  return moves;
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

static gboolean homeworlds_backend_setup_has_required_colors(const HomeworldsMove *move) {
  gboolean seen_colors[4] = {FALSE};

  g_return_val_if_fail(move != NULL, FALSE);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    HomeworldsPyramid star = move->setup_stars[i];
    if (!homeworlds_pyramid_is_valid(star)) {
      return FALSE;
    }

    seen_colors[homeworlds_pyramid_color(star)] = TRUE;
  }

  if (!homeworlds_pyramid_is_valid(move->setup_ship)) {
    return FALSE;
  }
  seen_colors[homeworlds_pyramid_color(move->setup_ship)] = TRUE;

  return seen_colors[HOMEWORLDS_COLOR_GREEN] &&
         seen_colors[HOMEWORLDS_COLOR_BLUE] &&
         (seen_colors[HOMEWORLDS_COLOR_RED] || seen_colors[HOMEWORLDS_COLOR_YELLOW]);
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
  if (!homeworlds_backend_setup_has_required_colors(move)) {
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

    if (system_index != 0 && homeworlds_system_has_ships_for_side(system, 0)) {
      return FALSE;
    }
    if (system_index != 1 && homeworlds_system_has_ships_for_side(system, 1)) {
      return FALSE;
    }
  }

  return TRUE;
}

static guint homeworlds_backend_system_ship_pips_for_color(const HomeworldsSystem *system,
                                                           HomeworldsColor color,
                                                           guint side) {
  guint pips = 0;

  g_return_val_if_fail(system != NULL, 0);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, 0);
  g_return_val_if_fail(side < 2, 0);

  for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    HomeworldsPyramid ship = system->ships[side][slot];

    if (!homeworlds_pyramid_is_valid(ship)) {
      break;
    }
    if (homeworlds_pyramid_color(ship) != color) {
      continue;
    }
    pips += homeworlds_pyramid_size(ship);
  }

  return pips;
}

static gboolean homeworlds_backend_system_has_unfavorable_catastrophe(const HomeworldsSystem *system, guint side) {
  guint opponent = 0;

  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);

  opponent = side == 0 ? 1 : 0;
  for (guint color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
    guint own_pips = 0;
    guint opponent_pips = 0;

    if (homeworlds_system_color_count(system, (HomeworldsColor) color) < 4) {
      continue;
    }

    own_pips = homeworlds_backend_system_ship_pips_for_color(system, (HomeworldsColor) color, side);
    opponent_pips = homeworlds_backend_system_ship_pips_for_color(system, (HomeworldsColor) color, opponent);
    if (own_pips > opponent_pips) {
      return TRUE;
    }
  }

  return FALSE;
}

static const HomeworldsTurnStep *homeworlds_backend_appended_step(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state) {
  g_return_val_if_fail(state != NULL, NULL);
  g_return_val_if_fail(child_state != NULL, NULL);

  if (state->move.kind != HOMEWORLDS_MOVE_KIND_TURN ||
      child_state->move.kind != HOMEWORLDS_MOVE_KIND_TURN ||
      child_state->move.step_count != state->move.step_count + 1) {
    return NULL;
  }

  return &child_state->move.steps[child_state->move.step_count - 1];
}

static gboolean homeworlds_backend_resolve_actor_system(const HomeworldsMoveBuilderState *state,
                                                        const HomeworldsTurnStep *step,
                                                        guint *out_system_index) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);
  g_return_val_if_fail(out_system_index != NULL, FALSE);

  return homeworlds_position_resolve_system_ref(&state->working_position, &step->actor.system, out_system_index);
}

static gboolean homeworlds_backend_step_removes_last_homeworld_ship(const HomeworldsMoveBuilderState *state,
                                                                    const HomeworldsTurnStep *step) {
  guint side = 0;
  guint system_index = HOMEWORLDS_INVALID_INDEX;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->kind != HOMEWORLDS_STEP_MOVE &&
      step->kind != HOMEWORLDS_STEP_DISCOVER &&
      step->kind != HOMEWORLDS_STEP_SACRIFICE) {
    return FALSE;
  }
  if (!homeworlds_backend_resolve_actor_system(state, step, &system_index)) {
    return FALSE;
  }

  side = state->working_position.turn;
  return system_index == side &&
         homeworlds_system_ship_count_for_side(&state->working_position.systems[side], side) == 1;
}

static gboolean homeworlds_backend_step_is_redundant_small_sacrifice(const HomeworldsMoveBuilderState *state,
                                                                     const HomeworldsTurnStep *step) {
  guint side = 0;
  guint system_index = HOMEWORLDS_INVALID_INDEX;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->kind != HOMEWORLDS_STEP_SACRIFICE ||
      !homeworlds_pyramid_is_valid(step->actor.ship) ||
      homeworlds_pyramid_size(step->actor.ship) != HOMEWORLDS_SIZE_SMALL ||
      !homeworlds_backend_resolve_actor_system(state, step, &system_index)) {
    return FALSE;
  }

  side = state->working_position.turn;
  return homeworlds_system_has_access_to_color(&state->working_position.systems[system_index],
                                               side,
                                               homeworlds_pyramid_color(step->actor.ship));
}

static gboolean homeworlds_backend_step_creates_unfavorable_build_catastrophe(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state,
    const HomeworldsTurnStep *step) {
  guint side = 0;
  guint system_index = HOMEWORLDS_INVALID_INDEX;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->kind != HOMEWORLDS_STEP_BUILD ||
      !homeworlds_backend_resolve_actor_system(state, step, &system_index)) {
    return FALSE;
  }

  side = state->working_position.turn;
  if (homeworlds_backend_system_has_unfavorable_catastrophe(&state->working_position.systems[system_index], side)) {
    return FALSE;
  }

  return homeworlds_backend_system_has_unfavorable_catastrophe(&child_state->working_position.systems[system_index],
                                                               side);
}

static gboolean homeworlds_backend_step_creates_unfavorable_trade_catastrophe(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state,
    const HomeworldsTurnStep *step) {
  guint side = 0;
  guint system_index = HOMEWORLDS_INVALID_INDEX;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->kind != HOMEWORLDS_STEP_TRADE ||
      !homeworlds_backend_resolve_actor_system(state, step, &system_index)) {
    return FALSE;
  }

  side = state->working_position.turn;
  if (homeworlds_backend_system_has_unfavorable_catastrophe(&state->working_position.systems[system_index], side)) {
    return FALSE;
  }

  return homeworlds_backend_system_has_unfavorable_catastrophe(&child_state->working_position.systems[system_index],
                                                               side);
}

static gboolean homeworlds_backend_step_enters_unfavorable_catastrophe(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state,
    const HomeworldsTurnStep *step) {
  guint side = 0;
  guint target_system_index = HOMEWORLDS_INVALID_INDEX;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->kind != HOMEWORLDS_STEP_MOVE && step->kind != HOMEWORLDS_STEP_DISCOVER) {
    return FALSE;
  }
  if (!homeworlds_position_resolve_system_ref(&child_state->working_position,
                                              &step->target_system,
                                              &target_system_index)) {
    return FALSE;
  }

  side = state->working_position.turn;
  return homeworlds_backend_system_has_unfavorable_catastrophe(
      &child_state->working_position.systems[target_system_index],
      side);
}

static gboolean homeworlds_backend_child_state_is_good_after_step(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state) {
  const HomeworldsTurnStep *step = NULL;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);

  step = homeworlds_backend_appended_step(state, child_state);
  if (step == NULL) {
    return TRUE;
  }

  return !homeworlds_backend_step_removes_last_homeworld_ship(state, step) &&
         !homeworlds_backend_step_is_redundant_small_sacrifice(state, step) &&
         !homeworlds_backend_step_creates_unfavorable_build_catastrophe(state, child_state, step) &&
         !homeworlds_backend_step_creates_unfavorable_trade_catastrophe(state, child_state, step) &&
         !homeworlds_backend_step_enters_unfavorable_catastrophe(state, child_state, step);
}

static gboolean homeworlds_backend_move_has_pass(const HomeworldsMove *move) {
  g_return_val_if_fail(move != NULL, FALSE);

  if (move->kind != HOMEWORLDS_MOVE_KIND_TURN) {
    return FALSE;
  }

  for (guint i = 0; i < move->step_count; ++i) {
    if (move->steps[i].kind == HOMEWORLDS_STEP_PASS) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean homeworlds_backend_move_is_good(const HomeworldsMoveBuilderState *state,
                                                const HomeworldsMove *move,
                                                gboolean allow_pass) {
  gboolean move_has_pass = FALSE;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  if (move->kind == HOMEWORLDS_MOVE_KIND_SETUP) {
    return homeworlds_backend_setup_move_is_good(state, move);
  }

  move_has_pass = homeworlds_backend_move_has_pass(move);
  if (homeworlds_backend_position_is_initial_turn(&state->working_position) &&
      !(allow_pass && move_has_pass) &&
      (move->step_count != 1 || move->steps[0].kind != HOMEWORLDS_STEP_BUILD)) {
    return FALSE;
  }

  if (move_has_pass && !allow_pass) {
    return FALSE;
  }

  return TRUE;
}

static gboolean homeworlds_backend_candidate_is_pass(const HomeworldsMoveCandidate *candidate) {
  g_return_val_if_fail(candidate != NULL, FALSE);

  return candidate->data.kind == HOMEWORLDS_CANDIDATE_ACTION &&
         candidate->data.target_color == HOMEWORLDS_STEP_PASS;
}

static gboolean homeworlds_backend_state_can_use_pass_fallback(const HomeworldsMoveBuilderState *state) {
  g_return_val_if_fail(state != NULL, FALSE);

  if (state->pending_actions_remaining > 0) {
    return FALSE;
  }

  for (guint i = 0; i < state->move.step_count; ++i) {
    if (state->move.steps[i].kind != HOMEWORLDS_STEP_CATASTROPHE) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean homeworlds_backend_state_is_catastrophe_boundary(const HomeworldsMoveBuilderState *state) {
  g_return_val_if_fail(state != NULL, FALSE);

  return state->stage == HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP ||
         state->stage == HOMEWORLDS_BUILDER_STAGE_COMPLETE;
}

static guint homeworlds_backend_collect_profitable_catastrophes(const HomeworldsMoveBuilderState *state,
                                                                HomeworldsProfitableCatastrophe *out_catastrophes,
                                                                guint max_catastrophes) {
  guint count = 0;
  guint side = 0;
  guint opponent = 0;

  g_return_val_if_fail(state != NULL, 0);
  g_return_val_if_fail(out_catastrophes != NULL || max_catastrophes == 0, 0);

  if (state->working_position.phase != HOMEWORLDS_PHASE_PLAY ||
      state->move.step_count >= HOMEWORLDS_MAX_MOVE_STEPS ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP) {
    return 0;
  }

  side = state->working_position.turn;
  opponent = side == 0 ? 1 : 0;
  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &state->working_position.systems[system_index];

    for (guint color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
      guint own_pips = 0;
      guint opponent_pips = 0;

      if (homeworlds_system_color_count(system, (HomeworldsColor) color) < 4) {
        continue;
      }

      own_pips = homeworlds_backend_system_ship_pips_for_color(system, (HomeworldsColor) color, side);
      opponent_pips = homeworlds_backend_system_ship_pips_for_color(system, (HomeworldsColor) color, opponent);
      if (opponent_pips <= own_pips) {
        continue;
      }

      if (count < max_catastrophes) {
        HomeworldsSystemRef system_ref = {0};

        if (!homeworlds_position_system_ref_for_index(&state->working_position, system_index, &system_ref)) {
          continue;
        }
        out_catastrophes[count] = (HomeworldsProfitableCatastrophe){
          .system_index = system_index,
          .color = (HomeworldsColor) color,
          .system_ref = system_ref,
        };
      }
      count++;
    }
  }

  return MIN(count, max_catastrophes);
}

static gboolean homeworlds_backend_apply_profitable_catastrophe(HomeworldsMoveBuilderState *state,
                                                                const HomeworldsProfitableCatastrophe *catastrophe) {
  GameBackendMoveBuilder builder = {0};
  HomeworldsTurnStep step = {
    .kind = HOMEWORLDS_STEP_CATASTROPHE,
    .target_color = catastrophe->color,
  };

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(catastrophe != NULL, FALSE);
  g_return_val_if_fail(catastrophe->system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);

  builder.builder_state = state;
  builder.builder_state_size = sizeof(*state);
  if (state->stage != HOMEWORLDS_BUILDER_STAGE_COMPLETE) {
    return homeworlds_move_builder_apply_catastrophe(&builder, catastrophe->system_index, catastrophe->color);
  }

  if (state->move.step_count >= HOMEWORLDS_MAX_MOVE_STEPS ||
      !homeworlds_position_system_ref_for_index(&state->working_position,
                                                catastrophe->system_index,
                                                &step.target_system)) {
    return FALSE;
  }

  state->move.steps[state->move.step_count++] = step;
  if (!homeworlds_position_apply_turn_step(&state->working_position, &step)) {
    state->move.step_count--;
    return FALSE;
  }
  return TRUE;
}

static gboolean homeworlds_backend_collect_good_moves_recursive(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsGenerationContext *generation_context,
    const HomeworldsGoodMoveContext *context,
    HomeworldsMoveBuffer *buffer,
    gboolean allow_pass_move,
    gboolean *out_covered) {
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  HomeworldsProfitableCatastrophe catastrophes[HOMEWORLDS_SYSTEM_SLOT_COUNT * 4] = {0};
  guint catastrophe_count = 0;
  gboolean forced_catastrophe_seen = FALSE;
  const HomeworldsMoveCandidate *pass_candidate = NULL;
  gboolean duplicate = FALSE;
  gboolean covered = FALSE;
  gboolean candidate_covered = FALSE;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(generation_context != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(out_covered != NULL, FALSE);

  *out_covered = FALSE;
  if (!homeworlds_generation_visit_state(generation_context, state, &duplicate)) {
    return FALSE;
  }
  if (duplicate) {
    *out_covered = TRUE;
    return TRUE;
  }

  builder.builder_state = (gpointer) state;
  builder.builder_state_size = sizeof(*state);

  if (homeworlds_backend_state_is_catastrophe_boundary(state)) {
    catastrophe_count =
        homeworlds_backend_collect_profitable_catastrophes(state, catastrophes, G_N_ELEMENTS(catastrophes));
  }
  if (catastrophe_count > 0) {
    for (guint i = 0; i < catastrophe_count; ++i) {
      HomeworldsMoveBuilderState child_state = *state;
      HomeworldsGenerationContext child_context = {0};
      HomeworldsGenerationDedupe child_dedupe = {0};
      gboolean prune_child = FALSE;
      gboolean child_covered = FALSE;

      if (homeworlds_backend_catastrophe_is_root_required(context, &catastrophes[i])) {
        continue;
      }
      forced_catastrophe_seen = TRUE;
      if (!homeworlds_backend_apply_profitable_catastrophe(&child_state, &catastrophes[i])) {
        continue;
      }
      if (!homeworlds_generation_prepare_child_context(generation_context,
                                                       state,
                                                       &child_state,
                                                       &child_context,
                                                       &child_dedupe,
                                                       &prune_child)) {
        return FALSE;
      }
      if (prune_child) {
        covered = TRUE;
        homeworlds_generation_dedupe_clear(&child_dedupe);
        continue;
      }
      if (!homeworlds_backend_collect_good_moves_recursive(&child_state,
                                                           &child_context,
                                                           context,
                                                           buffer,
                                                           allow_pass_move,
                                                           &child_covered)) {
        homeworlds_generation_dedupe_clear(&child_dedupe);
        return FALSE;
      }
      covered = covered || child_covered;
      homeworlds_generation_dedupe_clear(&child_dedupe);
    }

    if (forced_catastrophe_seen) {
      *out_covered = covered;
      return TRUE;
    }
  }

  for (guint i = 0; i < catastrophe_count; ++i) {
    HomeworldsMoveBuilderState child_state = *state;
    HomeworldsGenerationContext child_context = {0};
    HomeworldsGenerationDedupe child_dedupe = {0};
    gboolean prune_child = FALSE;
    gboolean child_covered = FALSE;

    if (!homeworlds_backend_catastrophe_is_root_required(context, &catastrophes[i]) ||
        homeworlds_backend_move_has_profitable_catastrophe(&state->move, &catastrophes[i]) ||
        !homeworlds_backend_apply_profitable_catastrophe(&child_state, &catastrophes[i])) {
      continue;
    }
    if (!homeworlds_generation_prepare_child_context(generation_context,
                                                     state,
                                                     &child_state,
                                                     &child_context,
                                                     &child_dedupe,
                                                     &prune_child)) {
      return FALSE;
    }
    if (prune_child) {
      covered = TRUE;
      homeworlds_generation_dedupe_clear(&child_dedupe);
      continue;
    }
    if (!homeworlds_backend_collect_good_moves_recursive(&child_state,
                                                         &child_context,
                                                         context,
                                                         buffer,
                                                         allow_pass_move,
                                                         &child_covered)) {
      homeworlds_generation_dedupe_clear(&child_dedupe);
      return FALSE;
    }
    covered = covered || child_covered;
    homeworlds_generation_dedupe_clear(&child_dedupe);
  }

  if (homeworlds_move_builder_is_complete(&builder)) {
    HomeworldsMove move = {0};

    if (!homeworlds_move_builder_build_move(&builder, &move)) {
      return FALSE;
    }
    if (!homeworlds_backend_move_is_good(state, &move, allow_pass_move)) {
      return TRUE;
    }
    if (!homeworlds_backend_move_satisfies_root_catastrophe_requirement(&move, context)) {
      return TRUE;
    }

    if (!homeworlds_backend_move_buffer_append(buffer, &move)) {
      return FALSE;
    }
    *out_covered = TRUE;
    return TRUE;
  }

  candidates = homeworlds_move_builder_list_candidates(&builder);
  for (gsize i = 0; i < candidates.count; ++i) {
    const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *) candidates.moves)[i];
    HomeworldsMoveBuilderState child_state = *state;
    HomeworldsGenerationContext child_context = {0};
    HomeworldsGenerationDedupe child_dedupe = {0};
    GameBackendMoveBuilder child = {
      .builder_state = &child_state,
      .builder_state_size = sizeof(child_state),
    };
    gboolean prune_child = FALSE;
    gboolean child_covered = FALSE;

    if (candidate != NULL && homeworlds_backend_candidate_is_pass(candidate)) {
      pass_candidate = candidate;
      continue;
    }

    if (candidate == NULL ||
        !homeworlds_move_builder_step(&child, candidate)) {
      continue;
    }
    if (!homeworlds_generation_prepare_child_context(generation_context,
                                                     state,
                                                     &child_state,
                                                     &child_context,
                                                     &child_dedupe,
                                                     &prune_child)) {
      homeworlds_backend_move_list_free(&candidates);
      return FALSE;
    }
    if (prune_child) {
      covered = TRUE;
      candidate_covered = TRUE;
      homeworlds_generation_dedupe_clear(&child_dedupe);
      continue;
    }
    if (!homeworlds_backend_child_state_is_good_after_step(state, &child_state)) {
      homeworlds_generation_dedupe_clear(&child_dedupe);
      continue;
    }
    if (!homeworlds_backend_collect_good_moves_recursive(&child_state,
                                                         &child_context,
                                                         context,
                                                         buffer,
                                                         allow_pass_move,
                                                         &child_covered)) {
      homeworlds_generation_dedupe_clear(&child_dedupe);
      homeworlds_backend_move_list_free(&candidates);
      return FALSE;
    }
    covered = covered || child_covered;
    candidate_covered = candidate_covered || child_covered;
    homeworlds_generation_dedupe_clear(&child_dedupe);
  }

  if (pass_candidate != NULL &&
      !candidate_covered &&
      homeworlds_backend_state_can_use_pass_fallback(state)) {
    HomeworldsMoveBuilderState child_state = *state;
    HomeworldsGenerationContext child_context = {0};
    HomeworldsGenerationDedupe child_dedupe = {0};
    GameBackendMoveBuilder child = {
      .builder_state = &child_state,
      .builder_state_size = sizeof(child_state),
    };
    gboolean prune_child = FALSE;
    gboolean child_covered = FALSE;

    if (homeworlds_move_builder_step(&child, pass_candidate)) {
      if (!homeworlds_generation_prepare_child_context(generation_context,
                                                       state,
                                                       &child_state,
                                                       &child_context,
                                                       &child_dedupe,
                                                       &prune_child)) {
        homeworlds_backend_move_list_free(&candidates);
        return FALSE;
      }
      if (prune_child) {
        covered = TRUE;
      } else if (homeworlds_backend_child_state_is_good_after_step(state, &child_state)) {
        if (!homeworlds_backend_collect_good_moves_recursive(&child_state,
                                                             &child_context,
                                                             context,
                                                             buffer,
                                                             TRUE,
                                                             &child_covered)) {
          homeworlds_generation_dedupe_clear(&child_dedupe);
          homeworlds_backend_move_list_free(&candidates);
          return FALSE;
        }
        covered = covered || child_covered;
      }
    }
    homeworlds_generation_dedupe_clear(&child_dedupe);
  }

  homeworlds_backend_move_list_free(&candidates);
  *out_covered = covered;
  return TRUE;
}

static gboolean homeworlds_backend_score_after_move(const HomeworldsPosition *position,
                                                    const HomeworldsMove *move,
                                                    gint *out_score) {
  HomeworldsPosition child = {0};
  GameBackendOutcome outcome = GAME_BACKEND_OUTCOME_ONGOING;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(out_score != NULL, FALSE);

  homeworlds_position_copy(&child, position);
  if (!homeworlds_position_apply_move(&child, move)) {
    g_debug("Skipping invalid Homeworlds move while static-pruning good_moves()");
    homeworlds_position_clear(&child);
    return FALSE;
  }

  outcome = homeworlds_position_outcome(&child);
  *out_score = outcome == GAME_BACKEND_OUTCOME_ONGOING
      ? homeworlds_position_evaluate_static(&child)
      : homeworlds_position_terminal_score(outcome, 1);
  homeworlds_position_clear(&child);
  return TRUE;
}

static gboolean homeworlds_backend_score_is_inside_prune_window(guint side, gint score, gint best_score) {
  g_return_val_if_fail(side < 2, FALSE);

  if (side == 0) {
    return score >= best_score - HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_WINDOW;
  }
  return score <= best_score + HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_WINDOW;
}

static void homeworlds_backend_trace_good_moves(const HomeworldsPosition *position,
                                                guint depth_hint,
                                                guint side,
                                                gsize generated_leaves,
                                                gsize scored_moves,
                                                gsize kept_moves) {
  g_return_if_fail(position != NULL);

  if (homeworlds_backend_good_move_trace_func == NULL) {
    return;
  }

  HomeworldsGoodMoveTrace trace = {
    .position = position,
    .depth_hint = depth_hint,
    .side = side,
    .generated_leaves = generated_leaves,
    .scored_moves = scored_moves,
    .kept_moves = kept_moves,
  };

  homeworlds_backend_good_move_trace_func(&trace, homeworlds_backend_good_move_trace_user_data);
}

static GameBackendMoveList homeworlds_backend_list_good_moves(gconstpointer position, guint depth_hint) {
  const HomeworldsPosition *homeworlds_position = position;
  GameBackendMoveBuilder builder = {0};
  HomeworldsGenerationContext generation_context = {0};
  HomeworldsGoodMoveContext context = {0};
  HomeworldsMoveBuffer buffer = {0};
  HomeworldsMove *moves = NULL;
  gsize count = 0;
  gboolean covered = FALSE;

  g_return_val_if_fail(homeworlds_position != NULL, (GameBackendMoveList){0});

  homeworlds_backend_move_buffer_init(&buffer, homeworlds_position);
  if (!homeworlds_move_builder_init(homeworlds_position, &builder)) {
    homeworlds_backend_move_buffer_clear(&buffer);
    return (GameBackendMoveList){0};
  }
  context.root_catastrophe_count = homeworlds_backend_collect_profitable_catastrophes(
      builder.builder_state,
      context.root_catastrophes,
      G_N_ELEMENTS(context.root_catastrophes));
  homeworlds_generation_context_init(&generation_context);
  if (!homeworlds_backend_collect_good_moves_recursive(builder.builder_state,
                                                       &generation_context,
                                                       &context,
                                                       &buffer,
                                                       FALSE,
                                                       &covered)) {
    homeworlds_move_builder_clear(&builder);
    homeworlds_backend_move_buffer_clear(&buffer);
    return (GameBackendMoveList){0};
  }

  count = buffer.count;
  moves = homeworlds_backend_move_buffer_copy_moves(&buffer);
  homeworlds_backend_trace_good_moves(homeworlds_position,
                                      depth_hint,
                                      homeworlds_position->turn,
                                      buffer.leaves_seen,
                                      buffer.scored_moves,
                                      count);

  homeworlds_move_builder_clear(&builder);
  homeworlds_backend_move_buffer_clear(&buffer);
  return (GameBackendMoveList){
    .moves = moves,
    .count = count,
  };
}

static gboolean homeworlds_backend_stream_moves(gconstpointer position,
                                                GameBackendMoveStreamFunc stream_func,
                                                gpointer user_data) {
  const HomeworldsPosition *homeworlds_position = position;

  g_return_val_if_fail(homeworlds_position != NULL, FALSE);
  g_return_val_if_fail(stream_func != NULL, FALSE);

  return homeworlds_position_stream_all_moves(homeworlds_position, stream_func, user_data);
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
  .supports_ascii_game_io = TRUE,
  .ascii_game_file_description = "Homeworlds text game files",
  .ascii_game_file_extension = "txt",
  .side_label = homeworlds_backend_side_label,
  .sgf_color_for_side = homeworlds_backend_sgf_color_for_side,
  .outcome_banner_text = homeworlds_backend_outcome_banner_text,
  .position_init = homeworlds_backend_position_init,
  .position_clear = homeworlds_backend_position_clear,
  .position_copy = homeworlds_backend_position_copy,
  .position_outcome = homeworlds_backend_position_outcome,
  .position_turn = homeworlds_backend_position_turn,
  .stream_moves = homeworlds_backend_stream_moves,
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
