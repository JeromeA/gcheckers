#include "homeworlds_backend.h"

#include <string.h>

typedef struct {
  guint turn;
} HomeworldsStubPosition;

typedef struct {
  guint step;
} HomeworldsStubBuilderState;

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
  HomeworldsStubPosition *stub_position = position;

  g_return_if_fail(stub_position != NULL);

  stub_position->turn = 0;
}

static void homeworlds_backend_position_clear(gpointer /*position*/) {
}

static void homeworlds_backend_position_copy(gpointer dest, gconstpointer src) {
  HomeworldsStubPosition *dest_position = dest;
  const HomeworldsStubPosition *src_position = src;

  g_return_if_fail(dest_position != NULL);
  g_return_if_fail(src_position != NULL);

  *dest_position = *src_position;
}

static GameBackendOutcome homeworlds_backend_position_outcome(gconstpointer /*position*/) {
  return GAME_BACKEND_OUTCOME_ONGOING;
}

static guint homeworlds_backend_position_turn(gconstpointer position) {
  const HomeworldsStubPosition *stub_position = position;

  g_return_val_if_fail(stub_position != NULL, 0);

  return stub_position->turn;
}

static GameBackendMoveList homeworlds_backend_list_good_moves(gconstpointer /*position*/,
                                                              guint /*max_count*/,
                                                              guint /*depth_hint*/) {
  return (GameBackendMoveList){0};
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

  return &((const guint8 *)moves->moves)[index];
}

static gboolean homeworlds_backend_moves_equal(gconstpointer left, gconstpointer right) {
  const guint8 *left_move = left;
  const guint8 *right_move = right;

  g_return_val_if_fail(left_move != NULL, FALSE);
  g_return_val_if_fail(right_move != NULL, FALSE);

  return *left_move == *right_move;
}

static gboolean homeworlds_backend_move_builder_init(gconstpointer /*position*/, GameBackendMoveBuilder *out_builder) {
  HomeworldsStubBuilderState *state = NULL;

  g_return_val_if_fail(out_builder != NULL, FALSE);

  state = g_new0(HomeworldsStubBuilderState, 1);
  g_return_val_if_fail(state != NULL, FALSE);

  out_builder->builder_state = state;
  out_builder->builder_state_size = sizeof(*state);
  return TRUE;
}

static void homeworlds_backend_move_builder_clear(GameBackendMoveBuilder *builder) {
  g_return_if_fail(builder != NULL);

  g_clear_pointer(&builder->builder_state, g_free);
  builder->builder_state_size = 0;
}

static GameBackendMoveList homeworlds_backend_move_builder_list_candidates(const GameBackendMoveBuilder *builder) {
  const HomeworldsStubBuilderState *state = NULL;

  g_return_val_if_fail(builder != NULL, (GameBackendMoveList){0});

  state = builder->builder_state;
  g_return_val_if_fail(state != NULL, (GameBackendMoveList){0});
  return (GameBackendMoveList){0};
}

static gboolean homeworlds_backend_move_builder_step(GameBackendMoveBuilder *builder, gconstpointer /*candidate*/) {
  HomeworldsStubBuilderState *state = NULL;

  g_return_val_if_fail(builder != NULL, FALSE);

  state = builder->builder_state;
  g_return_val_if_fail(state != NULL, FALSE);
  return FALSE;
}

static gboolean homeworlds_backend_move_builder_is_complete(const GameBackendMoveBuilder *builder) {
  const HomeworldsStubBuilderState *state = NULL;

  g_return_val_if_fail(builder != NULL, FALSE);

  state = builder->builder_state;
  g_return_val_if_fail(state != NULL, FALSE);
  return FALSE;
}

static gboolean homeworlds_backend_move_builder_build_move(const GameBackendMoveBuilder *builder, gpointer out_move) {
  g_return_val_if_fail(builder != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  return FALSE;
}

static gboolean homeworlds_backend_apply_move(gpointer /*position*/, gconstpointer /*move*/) {
  return FALSE;
}

static gboolean homeworlds_backend_format_move(gconstpointer move, char *buffer, gsize size) {
  const guint8 *encoded_move = move;

  g_return_val_if_fail(encoded_move != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(size > 0, FALSE);

  return g_snprintf(buffer, size, "stub-%u", (guint)*encoded_move) < (gint)size;
}

const GameBackend homeworlds_game_backend = {
  .id = "homeworlds",
  .display_name = "Homeworlds",
  .variant_count = 0,
  .position_size = sizeof(HomeworldsStubPosition),
  .move_size = sizeof(guint8),
  .supports_move_list = FALSE,
  .supports_move_builder = TRUE,
  .supports_ai_search = FALSE,
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
  .move_builder_init = homeworlds_backend_move_builder_init,
  .move_builder_clear = homeworlds_backend_move_builder_clear,
  .move_builder_list_candidates = homeworlds_backend_move_builder_list_candidates,
  .move_builder_step = homeworlds_backend_move_builder_step,
  .move_builder_is_complete = homeworlds_backend_move_builder_is_complete,
  .move_builder_build_move = homeworlds_backend_move_builder_build_move,
  .apply_move = homeworlds_backend_apply_move,
  .format_move = homeworlds_backend_format_move,
  .supports_square_grid_board = FALSE,
};
