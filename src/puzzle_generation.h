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
gint checkers_puzzle_score_gap_to_next_best(CheckersColor turn, gint best_score, gint second_score);
gboolean checkers_puzzle_is_mistake(CheckersColor turn, gint best_score, gint played_score, gint threshold);
gboolean checkers_puzzle_side_to_move_has_enough_choice(const CheckersScoredMoveList *moves, guint min_legal_moves);
gboolean checkers_puzzle_side_to_move_has_a_single_correct_move(const CheckersScoredMoveList *moves,
                                                                CheckersColor turn,
                                                                gint min_score_gap,
                                                                gint *out_best_score,
                                                                gint *out_second_score);
gboolean checkers_puzzle_turn_keeps_attacker_on_a_single_good_move(const CheckersScoredMoveList *moves,
                                                                   CheckersColor attacker,
                                                                   CheckersColor turn,
                                                                   gint min_score_gap,
                                                                   gint *out_best_score,
                                                                   gint *out_second_score);
gboolean checkers_puzzle_has_unique_best(const CheckersScoredMoveList *moves,
                                         guint min_legal_moves,
                                         CheckersColor turn,
                                         gint min_score_gap,
                                         gint *out_best_score,
                                         gint *out_second_score);
gboolean checkers_puzzle_solution_shape_is_interesting(const CheckersMove *moves, guint move_count);
gboolean checkers_puzzle_solution_has_no_immediate_recapture(const CheckersMove *solution_moves,
                                                             guint solution_move_count,
                                                             const CheckersMove *next_move);
char *checkers_puzzle_build_solution_key(const CheckersMove *moves, guint move_count);
gboolean checkers_puzzle_find_next_index(const char *dir_path, guint *out_next_index, GError **error);
char *checkers_puzzle_build_indexed_path(const char *dir_path, const char *prefix, guint index);
CheckersPuzzleArgType checkers_puzzle_parse_arg(const char *arg, guint *out_count);

#endif
