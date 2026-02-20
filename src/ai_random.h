#ifndef AI_RANDOM_H
#define AI_RANDOM_H

#include "game.h"

#include <glib.h>

gboolean checkers_ai_random_choose_move(const Game *game, GRand *rng, CheckersMove *out_move);

#endif
