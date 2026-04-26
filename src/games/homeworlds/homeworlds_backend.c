#include "homeworlds_backend.h"

#include "homeworlds_game.h"
#include "homeworlds_move_builder.h"

#include <string.h>

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
      return "Ongoing";
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
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  return memcmp(left, right, sizeof(HomeworldsMove)) == 0;
}

static gboolean homeworlds_backend_collect_good_moves_recursive(const HomeworldsMoveBuilderState *state,
                                                                HomeworldsMove *moves,
                                                                gsize max_count,
                                                                gsize *inout_count) {
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(moves != NULL, FALSE);
  g_return_val_if_fail(inout_count != NULL, FALSE);

  if (*inout_count >= max_count) {
    return TRUE;
  }

  builder.builder_state = (gpointer) state;
  builder.builder_state_size = sizeof(*state);

  if (homeworlds_move_builder_is_complete(&builder)) {
    HomeworldsMove move = {0};

    if (!homeworlds_move_builder_build_move(&builder, &move)) {
      return FALSE;
    }

    moves[*inout_count] = move;
    (*inout_count)++;
    return TRUE;
  }

  candidates = homeworlds_move_builder_list_candidates(&builder);
  for (gsize i = 0; i < candidates.count && *inout_count < max_count; ++i) {
    const HomeworldsMoveCandidate *candidate = homeworlds_backend_move_list_get(&candidates, i);
    HomeworldsMoveBuilderState child_state = *state;
    GameBackendMoveBuilder child = {
      .builder_state = &child_state,
      .builder_state_size = sizeof(child_state),
    };

    if (candidate == NULL || !homeworlds_move_builder_step(&child, candidate)) {
      continue;
    }
    if (!homeworlds_backend_collect_good_moves_recursive(&child_state, moves, max_count, inout_count)) {
      homeworlds_backend_move_list_free(&candidates);
      return FALSE;
    }
  }

  homeworlds_backend_move_list_free(&candidates);
  return TRUE;
}

static GameBackendMoveList homeworlds_backend_list_good_moves(gconstpointer position, guint max_count, guint /*depth_hint*/) {
  GameBackendMoveBuilder builder = {0};
  HomeworldsMove *moves = NULL;
  gsize count = 0;
  gsize capped_count = max_count == 0 ? 16 : max_count;

  g_return_val_if_fail(position != NULL, (GameBackendMoveList){0});

  moves = g_new0(HomeworldsMove, capped_count);
  g_return_val_if_fail(moves != NULL, (GameBackendMoveList){0});
  if (!homeworlds_move_builder_init(position, &builder)) {
    g_free(moves);
    return (GameBackendMoveList){0};
  }
  if (!homeworlds_backend_collect_good_moves_recursive(builder.builder_state, moves, capped_count, &count)) {
    homeworlds_move_builder_clear(&builder);
    g_free(moves);
    return (GameBackendMoveList){0};
  }

  homeworlds_move_builder_clear(&builder);
  return (GameBackendMoveList){
    .moves = moves,
    .count = count,
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

static gint homeworlds_backend_terminal_score(GameBackendOutcome outcome, guint ply_depth) {
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
  .move_builder_build_move = (gboolean (*)(const GameBackendMoveBuilder *, gpointer)) homeworlds_move_builder_build_move,
  .apply_move = homeworlds_backend_apply_move,
  .evaluate_static = homeworlds_backend_evaluate_static,
  .terminal_score = homeworlds_backend_terminal_score,
  .hash_position = homeworlds_backend_hash_position,
  .format_move = homeworlds_backend_format_move,
  .supports_square_grid_board = FALSE,
};
