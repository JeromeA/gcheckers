#ifndef AI_ALPHA_BETA_H
#define AI_ALPHA_BETA_H

#include "game.h"

#include <glib.h>

gboolean checkers_ai_alpha_beta_choose_move(const Game *game, guint max_depth, CheckersMove *out_move);

#endif
