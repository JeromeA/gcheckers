#ifndef CHECKERS_PUZZLE_GENERATION_H
#define CHECKERS_PUZZLE_GENERATION_H

#include "ai_alpha_beta.h"
#include "board.h"

#include <glib.h>

typedef enum {
  CHECKERS_PUZZLE_ARG_INVALID = 0,
  CHECKERS_PUZZLE_ARG_COUNT,
  CHECKERS_PUZZLE_ARG_FILE,
} CheckersPuzzleArgType;

gint checkers_puzzle_mistake_delta(CheckersColor turn, gint best_score, gint played_score);
gboolean checkers_puzzle_is_mistake(CheckersColor turn, gint best_score, gint played_score, gint threshold);
gboolean checkers_puzzle_has_unique_best(const CheckersScoredMoveList *moves,
                                         guint min_legal_moves,
                                         gint *out_best_score,
                                         guint *out_best_count);
gboolean checkers_puzzle_find_next_index(const char *dir_path, guint *out_next_index, GError **error);
char *checkers_puzzle_build_indexed_path(const char *dir_path, const char *prefix, guint index);
CheckersPuzzleArgType checkers_puzzle_parse_arg(const char *arg, guint *out_count);

#endif
