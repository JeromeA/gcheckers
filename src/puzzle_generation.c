#include "puzzle_generation.h"

#include <errno.h>
#include <string.h>

gint checkers_puzzle_mistake_delta(CheckersColor turn, gint best_score, gint played_score) {
  g_return_val_if_fail(turn == CHECKERS_COLOR_WHITE || turn == CHECKERS_COLOR_BLACK, 0);

  if (turn == CHECKERS_COLOR_WHITE) {
    return best_score - played_score;
  }
  return played_score - best_score;
}

gint checkers_puzzle_score_gap_to_next_best(CheckersColor turn, gint best_score, gint second_score) {
  return checkers_puzzle_mistake_delta(turn, best_score, second_score);
}

gboolean checkers_puzzle_is_mistake(CheckersColor turn, gint best_score, gint played_score, gint threshold) {
  g_return_val_if_fail(threshold >= 0, FALSE);

  gint delta = checkers_puzzle_mistake_delta(turn, best_score, played_score);
  return delta >= threshold;
}

gboolean checkers_puzzle_side_to_move_has_enough_choice(const CheckersScoredMoveList *moves, guint min_legal_moves) {
  g_return_val_if_fail(moves != NULL, FALSE);
  g_return_val_if_fail(moves->moves != NULL || moves->count == 0, FALSE);

  return moves->count >= min_legal_moves && moves->count > 0;
}

gboolean checkers_puzzle_side_to_move_has_a_single_correct_move(const CheckersScoredMoveList *moves,
                                                                CheckersColor turn,
                                                                gint min_score_gap,
                                                                gint *out_best_score,
                                                                gint *out_second_score) {
  g_return_val_if_fail(moves != NULL, FALSE);
  g_return_val_if_fail(moves->moves != NULL || moves->count == 0, FALSE);
  g_return_val_if_fail(turn == CHECKERS_COLOR_WHITE || turn == CHECKERS_COLOR_BLACK, FALSE);
  g_return_val_if_fail(min_score_gap >= 0, FALSE);

  if (moves->count == 0) {
    if (out_second_score != NULL) {
      *out_second_score = 0;
    }
    return FALSE;
  }

  gint best_score = moves->moves[0].score;
  gint second_score = moves->count > 1 ? moves->moves[1].score : best_score;

  if (out_best_score != NULL) {
    *out_best_score = best_score;
  }
  if (out_second_score != NULL) {
    *out_second_score = second_score;
  }
  return moves->count == 1 || checkers_puzzle_score_gap_to_next_best(turn, best_score, second_score) >= min_score_gap;
}

gboolean checkers_puzzle_turn_keeps_attacker_on_a_single_good_move(const CheckersScoredMoveList *moves,
                                                                   CheckersColor attacker,
                                                                   CheckersColor turn,
                                                                   gint min_score_gap,
                                                                   gint *out_best_score,
                                                                   gint *out_second_score) {
  g_return_val_if_fail(moves != NULL, FALSE);
  g_return_val_if_fail(moves->moves != NULL || moves->count == 0, FALSE);
  g_return_val_if_fail(attacker == CHECKERS_COLOR_WHITE || attacker == CHECKERS_COLOR_BLACK, FALSE);
  g_return_val_if_fail(turn == CHECKERS_COLOR_WHITE || turn == CHECKERS_COLOR_BLACK, FALSE);
  g_return_val_if_fail(min_score_gap >= 0, FALSE);

  if (turn != attacker) {
    if (moves->count == 0) {
      if (out_second_score != NULL) {
        *out_second_score = 0;
      }
      return FALSE;
    }

    gint best_score = moves->moves[0].score;
    gint second_score = moves->count > 1 ? moves->moves[1].score : best_score;
    if (out_best_score != NULL) {
      *out_best_score = best_score;
    }
    if (out_second_score != NULL) {
      *out_second_score = second_score;
    }
    return TRUE;
  }

  return checkers_puzzle_side_to_move_has_a_single_correct_move(moves,
                                                                turn,
                                                                min_score_gap,
                                                                out_best_score,
                                                                out_second_score);
}

gboolean checkers_puzzle_has_unique_best(const CheckersScoredMoveList *moves,
                                         guint min_legal_moves,
                                         CheckersColor turn,
                                         gint min_score_gap,
                                         gint *out_best_score,
                                         gint *out_second_score) {
  if (!checkers_puzzle_side_to_move_has_enough_choice(moves, min_legal_moves)) {
    if (out_second_score != NULL) {
      *out_second_score = 0;
    }
    return FALSE;
  }

  return checkers_puzzle_side_to_move_has_a_single_correct_move(moves,
                                                                turn,
                                                                min_score_gap,
                                                                out_best_score,
                                                                out_second_score);
}

static gboolean checkers_puzzle_parse_index_from_name(const char *name, guint *out_index) {
  g_return_val_if_fail(name != NULL, FALSE);
  g_return_val_if_fail(out_index != NULL, FALSE);

  if (!g_str_has_prefix(name, "puzzle-") || !g_str_has_suffix(name, ".sgf")) {
    return FALSE;
  }

  gsize len = strlen(name);
  if (len <= strlen("puzzle-.sgf")) {
    return FALSE;
  }

  g_autofree char *number_text = g_strndup(name + strlen("puzzle-"), len - strlen("puzzle-") - strlen(".sgf"));
  if (number_text[0] == '\0') {
    return FALSE;
  }
  for (const char *p = number_text; *p != '\0'; ++p) {
    if (!g_ascii_isdigit(*p)) {
      return FALSE;
    }
  }

  guint64 parsed = g_ascii_strtoull(number_text, NULL, 10);
  if (parsed > G_MAXUINT) {
    return FALSE;
  }

  *out_index = (guint)parsed;
  return TRUE;
}

gboolean checkers_puzzle_find_next_index(const char *dir_path, guint *out_next_index, GError **error) {
  g_return_val_if_fail(dir_path != NULL, FALSE);
  g_return_val_if_fail(out_next_index != NULL, FALSE);

  if (!g_file_test(dir_path, G_FILE_TEST_EXISTS)) {
    *out_next_index = 0;
    return TRUE;
  }

  g_autoptr(GDir) dir = g_dir_open(dir_path, 0, error);
  if (dir == NULL) {
    return FALSE;
  }

  gboolean found = FALSE;
  guint max_index = 0;
  const char *name = NULL;
  while ((name = g_dir_read_name(dir)) != NULL) {
    guint index = 0;
    if (!checkers_puzzle_parse_index_from_name(name, &index)) {
      continue;
    }
    if (!found || index > max_index) {
      max_index = index;
      found = TRUE;
    }
  }

  if (!found) {
    *out_next_index = 0;
    return TRUE;
  }
  if (max_index == G_MAXUINT) {
    g_set_error_literal(error,
                        g_quark_from_static_string("checkers-puzzle-generation-error"),
                        1,
                        "Puzzle index overflow");
    return FALSE;
  }

  *out_next_index = max_index + 1;
  return TRUE;
}

char *checkers_puzzle_build_indexed_path(const char *dir_path, const char *prefix, guint index) {
  g_return_val_if_fail(dir_path != NULL, NULL);
  g_return_val_if_fail(prefix != NULL, NULL);
  return g_strdup_printf("%s/%s-%04u.sgf", dir_path, prefix, index);
}

CheckersPuzzleArgType checkers_puzzle_parse_arg(const char *arg, guint *out_count) {
  g_return_val_if_fail(arg != NULL, CHECKERS_PUZZLE_ARG_INVALID);

  gchar *end = NULL;
  guint64 value = g_ascii_strtoull(arg, &end, 10);
  if (end != arg && end != NULL && *end == '\0' && value > 0 && value <= G_MAXUINT) {
    if (out_count != NULL) {
      *out_count = (guint)value;
    }
    return CHECKERS_PUZZLE_ARG_COUNT;
  }

  if (g_file_test(arg, G_FILE_TEST_IS_REGULAR)) {
    return CHECKERS_PUZZLE_ARG_FILE;
  }
  return CHECKERS_PUZZLE_ARG_INVALID;
}
