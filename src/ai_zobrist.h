#ifndef AI_ZOBRIST_H
#define AI_ZOBRIST_H

#include "game.h"

#include <glib.h>

guint64 checkers_ai_zobrist_key(const Game *game);

#endif
