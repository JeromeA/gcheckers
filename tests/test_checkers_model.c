#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include <glib.h>

#include "../src/checkers_model.h"

static gboolean test_checkers_model_move_in_list(const MoveList *moves, const CheckersMove *move) {
  g_return_val_if_fail(moves != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  for (size_t i = 0; i < moves->count; ++i) {
    const CheckersMove *candidate = &moves->moves[i];
    if (candidate->length != move->length || candidate->captures != move->captures) {
      continue;
    }

    if (memcmp(candidate->path, move->path, move->length * sizeof(move->path[0])) == 0) {
      return TRUE;
    }
  }

  return FALSE;
}

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

static void test_model_choose_random_move_returns_legal_move(void) {
  GCheckersModel *model = gcheckers_model_new();

  MoveList moves = gcheckers_model_list_moves(model);
  assert(moves.count > 0);

  CheckersMove move = {0};
  bool selected = gcheckers_model_choose_random_move(model, &move);
  assert(selected);
  assert(move.length >= 2);
  assert(test_checkers_model_move_in_list(&moves, &move));

  movelist_free(&moves);
  g_object_unref(model);
}

static void test_model_choose_best_move_returns_legal_move(void) {
  GCheckersModel *model = gcheckers_model_new();

  MoveList moves = gcheckers_model_list_moves(model);
  assert(moves.count > 0);

  CheckersMove depth4 = {0};
  CheckersMove depth8 = {0};
  bool selected_4 = gcheckers_model_choose_best_move(model, 4, &depth4);
  bool selected_8 = gcheckers_model_choose_best_move(model, 8, &depth8);
  assert(selected_4);
  assert(selected_8);
  assert(depth4.length >= 2);
  assert(depth8.length >= 2);
  assert(test_checkers_model_move_in_list(&moves, &depth4));
  assert(test_checkers_model_move_in_list(&moves, &depth8));

  movelist_free(&moves);
  g_object_unref(model);
}

static void test_model_peek_last_move(void) {
  GCheckersModel *model = gcheckers_model_new();

  const CheckersMove *last_move = gcheckers_model_peek_last_move(model);
  assert(last_move == NULL);

  MoveList moves = gcheckers_model_list_moves(model);
  assert(moves.count > 0);
  CheckersMove first_move = moves.moves[0];
  movelist_free(&moves);

  bool moved = gcheckers_model_apply_move(model, &first_move);
  assert(moved);

  last_move = gcheckers_model_peek_last_move(model);
  assert(last_move != NULL);
  assert(last_move->length == first_move.length);
  assert(last_move->captures == first_move.captures);
  assert(memcmp(last_move->path,
                first_move.path,
                first_move.length * sizeof(first_move.path[0])) == 0);

  g_object_unref(model);
}

static void test_model_reset_clears_last_move(void) {
  GCheckersModel *model = gcheckers_model_new();

  const CheckersMove *last_move = gcheckers_model_peek_last_move(model);
  assert(last_move == NULL);

  MoveList moves = gcheckers_model_list_moves(model);
  assert(moves.count > 0);
  CheckersMove first_move = moves.moves[0];
  movelist_free(&moves);

  bool moved = gcheckers_model_apply_move(model, &first_move);
  assert(moved);
  last_move = gcheckers_model_peek_last_move(model);
  assert(last_move != NULL);

  gcheckers_model_reset(model);
  last_move = gcheckers_model_peek_last_move(model);
  assert(last_move == NULL);

  g_object_unref(model);
}

int main(void) {
  test_model_reset_and_moves();
  test_model_rejects_invalid_move();
  test_model_random_move_outputs_move();
  test_model_choose_random_move_returns_legal_move();
  test_model_choose_best_move_returns_legal_move();
  test_model_peek_last_move();
  test_model_reset_clears_last_move();

  return 0;
}
