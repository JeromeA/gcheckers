#include "boop_backend.h"

#include "boop_game.h"

#include <string.h>

static const char *boop_backend_side_label(guint side) {
  switch (side) {
    case 0:
      return "Player 1";
    case 1:
      return "Player 2";
    default:
      g_debug("Unsupported boop side index");
      return "Player";
  }
}

static const char *boop_backend_outcome_banner_text(GameBackendOutcome outcome) {
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

static void boop_backend_position_init(gpointer position, const GameBackendVariant * /*variant_or_null*/) {
  BoopPosition *boop_position = position;

  g_return_if_fail(boop_position != NULL);

  boop_position_init(boop_position);
}

static void boop_backend_position_clear(gpointer position) {
  BoopPosition *boop_position = position;

  g_return_if_fail(boop_position != NULL);

  boop_position_clear(boop_position);
}

static void boop_backend_position_copy(gpointer dest, gconstpointer src) {
  BoopPosition *dest_position = dest;
  const BoopPosition *src_position = src;

  g_return_if_fail(dest_position != NULL);
  g_return_if_fail(src_position != NULL);

  boop_position_copy(dest_position, src_position);
}

static GameBackendOutcome boop_backend_position_outcome(gconstpointer position) {
  const BoopPosition *boop_position = position;

  g_return_val_if_fail(boop_position != NULL, GAME_BACKEND_OUTCOME_ONGOING);

  return boop_position_outcome(boop_position);
}

static guint boop_backend_position_turn(gconstpointer position) {
  const BoopPosition *boop_position = position;

  g_return_val_if_fail(boop_position != NULL, 0);

  return boop_position_turn(boop_position);
}

static void boop_backend_move_list_free(GameBackendMoveList *moves) {
  g_return_if_fail(moves != NULL);

  g_clear_pointer(&moves->moves, g_free);
  moves->count = 0;
}

static const void *boop_backend_move_list_get(const GameBackendMoveList *moves, gsize index) {
  const BoopMove *values = NULL;

  g_return_val_if_fail(moves != NULL, NULL);
  g_return_val_if_fail(index < moves->count, NULL);

  values = moves->moves;
  g_return_val_if_fail(values != NULL, NULL);
  return &values[index];
}

static gboolean boop_backend_moves_equal(gconstpointer left, gconstpointer right) {
  const BoopMove *left_move = left;
  const BoopMove *right_move = right;

  g_return_val_if_fail(left_move != NULL, FALSE);
  g_return_val_if_fail(right_move != NULL, FALSE);

  return left_move->square == right_move->square;
}

static gboolean boop_backend_apply_move(gpointer position, gconstpointer move) {
  BoopPosition *boop_position = position;
  const BoopMove *boop_move = move;

  g_return_val_if_fail(boop_position != NULL, FALSE);
  g_return_val_if_fail(boop_move != NULL, FALSE);

  return boop_position_apply_move(boop_position, boop_move);
}

static gboolean boop_backend_format_move(gconstpointer move, char *buffer, gsize size) {
  const BoopMove *boop_move = move;

  g_return_val_if_fail(boop_move != NULL, FALSE);

  return boop_move_format(boop_move, buffer, size);
}

const GameBackend boop_game_backend = {
  .id = "boop",
  .display_name = "Boop",
  .variant_count = 0,
  .position_size = sizeof(BoopPosition),
  .move_size = sizeof(BoopMove),
  .supports_move_list = FALSE,
  .supports_move_builder = TRUE,
  .supports_ai_search = FALSE,
  .side_label = boop_backend_side_label,
  .outcome_banner_text = boop_backend_outcome_banner_text,
  .position_init = boop_backend_position_init,
  .position_clear = boop_backend_position_clear,
  .position_copy = boop_backend_position_copy,
  .position_outcome = boop_backend_position_outcome,
  .position_turn = boop_backend_position_turn,
  .move_list_free = boop_backend_move_list_free,
  .move_list_get = boop_backend_move_list_get,
  .moves_equal = boop_backend_moves_equal,
  .move_builder_init = (gboolean (*)(gconstpointer, GameBackendMoveBuilder *)) boop_move_builder_init,
  .move_builder_clear = boop_move_builder_clear,
  .move_builder_list_candidates = (GameBackendMoveList (*)(const GameBackendMoveBuilder *))
      boop_move_builder_list_candidates,
  .move_builder_step = (gboolean (*)(GameBackendMoveBuilder *, gconstpointer)) boop_move_builder_step,
  .move_builder_is_complete = (gboolean (*)(const GameBackendMoveBuilder *)) boop_move_builder_is_complete,
  .move_builder_build_move = (gboolean (*)(const GameBackendMoveBuilder *, gpointer)) boop_move_builder_build_move,
  .apply_move = boop_backend_apply_move,
  .format_move = boop_backend_format_move,
  .supports_square_grid_board = FALSE,
};
