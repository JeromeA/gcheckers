#ifndef AI_ALPHA_BETA_H
#define AI_ALPHA_BETA_H

#include "game.h"

#include <glib.h>

typedef struct {
  CheckersMove move;
  gint score;
} CheckersScoredMove;

typedef struct {
  CheckersScoredMove *moves;
  size_t count;
} CheckersScoredMoveList;

typedef gboolean (*CheckersAiCancelFunc)(gpointer user_data);

gboolean checkers_ai_alpha_beta_choose_move(const Game *game, guint max_depth, CheckersMove *out_move);
gboolean checkers_ai_alpha_beta_analyze_moves(const Game *game, guint max_depth, CheckersScoredMoveList *out_moves);
gboolean checkers_ai_alpha_beta_analyze_moves_cancellable(const Game *game,
                                                          guint max_depth,
                                                          CheckersScoredMoveList *out_moves,
                                                          CheckersAiCancelFunc should_cancel,
                                                          gpointer user_data);
gboolean checkers_ai_alpha_beta_evaluate_position(const Game *game, guint max_depth, gint *out_score);
void checkers_scored_move_list_free(CheckersScoredMoveList *list);

#endif
