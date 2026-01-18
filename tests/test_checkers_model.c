#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include <glib.h>

#include "../src/checkers_model.h"

static void test_model_reset_and_moves(void) {
  GCheckersModel *model = gcheckers_model_new();

  const GameState *state = gcheckers_model_peek_state(model);
  assert(state->winner == CHECKERS_WINNER_NONE);
  assert(state->turn == CHECKERS_COLOR_WHITE);

  MoveList moves = gcheckers_model_list_moves(model);
  assert(moves.count > 0);

  CheckersMove first_move = moves.moves[0];
  movelist_free(&moves);

  bool moved = gcheckers_model_apply_move(model, &first_move);
  assert(moved);

  const GameState *after_move = gcheckers_model_peek_state(model);
  assert(after_move->turn == CHECKERS_COLOR_BLACK);

  char *status = gcheckers_model_format_status(model);
  assert(status != NULL);
  g_free(status);

  gcheckers_model_reset(model);
  const GameState *after_reset = gcheckers_model_peek_state(model);
  assert(after_reset->winner == CHECKERS_WINNER_NONE);
  assert(after_reset->turn == CHECKERS_COLOR_WHITE);

  g_object_unref(model);
}

static void test_model_rejects_invalid_move(void) {
  GCheckersModel *model = gcheckers_model_new();

  CheckersMove invalid_move;
  memset(&invalid_move, 0, sizeof(invalid_move));
  invalid_move.length = 2;
  invalid_move.path[0] = 0;
  invalid_move.path[1] = 1;

  const GameState *before = gcheckers_model_peek_state(model);
  assert(before->turn == CHECKERS_COLOR_WHITE);

  bool moved = gcheckers_model_apply_move(model, &invalid_move);
  assert(!moved);

  const GameState *after = gcheckers_model_peek_state(model);
  assert(after->turn == CHECKERS_COLOR_WHITE);

  g_object_unref(model);
}

static void test_model_random_move_outputs_move(void) {
  GCheckersModel *model = gcheckers_model_new();

  CheckersMove move;
  bool moved = gcheckers_model_step_random_move(model, &move);
  assert(moved);
  assert(move.length >= 2);

  g_object_unref(model);
}

int main(void) {
  test_model_reset_and_moves();
  test_model_rejects_invalid_move();
  test_model_random_move_outputs_move();

  return 0;
}
