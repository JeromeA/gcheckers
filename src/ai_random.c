#include "ai_random.h"

#include <glib.h>

gboolean checkers_ai_random_choose_move(const Game *game, GRand *rng, CheckersMove *out_move) {
  g_return_val_if_fail(game != NULL, FALSE);
  g_return_val_if_fail(rng != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  MoveList moves = game->available_moves(game);
  if (moves.count == 0) {
    movelist_free(&moves);
    g_debug("No available moves for random AI\n");
    return FALSE;
  }

  guint choice = g_rand_int_range(rng, 0, (gint)moves.count);
  *out_move = moves.moves[choice];
  movelist_free(&moves);
  return TRUE;
}
