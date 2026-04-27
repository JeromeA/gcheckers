#include "boop_game.h"

void boop_position_init(BoopPosition *position) {
  g_return_if_fail(position != NULL);

  position->turn = 0;
  position->outcome = GAME_BACKEND_OUTCOME_ONGOING;
}

void boop_position_clear(BoopPosition *position) {
  g_return_if_fail(position != NULL);

  position->turn = 0;
  position->outcome = GAME_BACKEND_OUTCOME_ONGOING;
}

void boop_position_copy(BoopPosition *dest, const BoopPosition *src) {
  g_return_if_fail(dest != NULL);
  g_return_if_fail(src != NULL);

  *dest = *src;
}

GameBackendOutcome boop_position_outcome(const BoopPosition *position) {
  g_return_val_if_fail(position != NULL, GAME_BACKEND_OUTCOME_ONGOING);

  return (GameBackendOutcome) position->outcome;
}

guint boop_position_turn(const BoopPosition *position) {
  g_return_val_if_fail(position != NULL, 0);

  return position->turn;
}

gboolean boop_position_apply_move(BoopPosition *position, const BoopMove *move) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(position->outcome == GAME_BACKEND_OUTCOME_ONGOING, FALSE);
  g_return_val_if_fail(move->square < 36, FALSE);

  position->turn = position->turn == 0 ? 1 : 0;
  return TRUE;
}

gboolean boop_move_format(const BoopMove *move, char *buffer, gsize size) {
  guint row = 0;
  guint col = 0;

  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(size > 0, FALSE);
  g_return_val_if_fail(move->square < 36, FALSE);

  row = move->square / 6;
  col = move->square % 6;
  return g_snprintf(buffer, size, "%c%u", (char) ('a' + col), row + 1) < (gint) size;
}

gboolean boop_move_builder_init(const BoopPosition *position, GameBackendMoveBuilder *out_builder) {
  BoopMoveBuilderState *state = NULL;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(out_builder != NULL, FALSE);

  state = g_new0(BoopMoveBuilderState, 1);
  g_return_val_if_fail(state != NULL, FALSE);

  state->square = 255;
  out_builder->builder_state = state;
  out_builder->builder_state_size = sizeof(*state);
  return TRUE;
}

void boop_move_builder_clear(GameBackendMoveBuilder *builder) {
  g_return_if_fail(builder != NULL);

  g_clear_pointer(&builder->builder_state, g_free);
  builder->builder_state_size = 0;
}

GameBackendMoveList boop_move_builder_list_candidates(const GameBackendMoveBuilder *builder) {
  BoopMoveBuilderState *state = NULL;
  BoopMove *moves = NULL;

  g_return_val_if_fail(builder != NULL, (GameBackendMoveList){0});

  state = builder->builder_state;
  g_return_val_if_fail(state != NULL, (GameBackendMoveList){0});
  if (state->square != 255) {
    return (GameBackendMoveList){0};
  }

  moves = g_new0(BoopMove, 1);
  g_return_val_if_fail(moves != NULL, (GameBackendMoveList){0});
  moves[0].square = 0;
  return (GameBackendMoveList){
    .moves = moves,
    .count = 1,
  };
}

gboolean boop_move_builder_step(GameBackendMoveBuilder *builder, const BoopMove *candidate) {
  BoopMoveBuilderState *state = NULL;

  g_return_val_if_fail(builder != NULL, FALSE);
  g_return_val_if_fail(candidate != NULL, FALSE);

  state = builder->builder_state;
  g_return_val_if_fail(state != NULL, FALSE);
  if (state->square != 255 || candidate->square >= 36) {
    return FALSE;
  }

  state->square = candidate->square;
  return TRUE;
}

gboolean boop_move_builder_is_complete(const GameBackendMoveBuilder *builder) {
  BoopMoveBuilderState *state = NULL;

  g_return_val_if_fail(builder != NULL, FALSE);

  state = builder->builder_state;
  g_return_val_if_fail(state != NULL, FALSE);
  return state->square != 255;
}

gboolean boop_move_builder_build_move(const GameBackendMoveBuilder *builder, BoopMove *out_move) {
  BoopMoveBuilderState *state = NULL;

  g_return_val_if_fail(builder != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  state = builder->builder_state;
  g_return_val_if_fail(state != NULL, FALSE);
  if (state->square == 255) {
    return FALSE;
  }

  out_move->square = state->square;
  return TRUE;
}
