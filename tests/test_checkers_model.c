#include <assert.h>
#include <stdbool.h>

#include <glib.h>

#include "../src/checkers_model.h"

static void test_model_reset_and_moves(void) {
  GCheckersModel *model = gcheckers_model_new();

  const GameState *state = gcheckers_model_peek_state(model);
  assert(state->winner == CHECKERS_WINNER_NONE);
  assert(state->turn == CHECKERS_COLOR_WHITE);

  bool moved = gcheckers_model_step_random_move(model);
  assert(moved);

  const GameState *after_move = gcheckers_model_peek_state(model);
  assert(after_move->turn != CHECKERS_COLOR_WHITE);

  char *status = gcheckers_model_format_status(model);
  assert(status != NULL);
  g_free(status);

  gcheckers_model_reset(model);
  const GameState *after_reset = gcheckers_model_peek_state(model);
  assert(after_reset->winner == CHECKERS_WINNER_NONE);
  assert(after_reset->turn == CHECKERS_COLOR_WHITE);

  g_object_unref(model);
}

int main(void) {
  test_model_reset_and_moves();

  return 0;
}
