#include "position_predicate.h"

#include "ai_alpha_beta.h"

#include <string.h>

static gboolean checkers_moves_equal(const CheckersMove *left, const CheckersMove *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  if (left->length != right->length || left->captures != right->captures) {
    return FALSE;
  }
  return memcmp(left->path, right->path, left->length * sizeof(left->path[0])) == 0;
}

static gboolean checkers_score_worse_for_turn(gint candidate_score, gint best_score, CheckersColor turn) {
  g_return_val_if_fail(turn == CHECKERS_COLOR_WHITE || turn == CHECKERS_COLOR_BLACK, FALSE);

  if (turn == CHECKERS_COLOR_WHITE) {
    return candidate_score < best_score;
  }
  return candidate_score > best_score;
}

static gboolean checkers_score_played_move(const Game *position,
                                           const CheckersMove *played_move,
                                           guint depth,
                                           gint *out_best_score,
                                           gint *out_played_score) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(played_move != NULL, FALSE);
  g_return_val_if_fail(depth > 0, FALSE);
  g_return_val_if_fail(out_best_score != NULL, FALSE);
  g_return_val_if_fail(out_played_score != NULL, FALSE);

  CheckersScoredMoveList scored_moves = {0};
  gboolean ok = checkers_ai_alpha_beta_analyze_moves(position, depth, &scored_moves);
  if (!ok || scored_moves.count == 0) {
    g_debug("Failed to score position while checking move quality");
    checkers_scored_move_list_free(&scored_moves);
    return FALSE;
  }

  *out_best_score = scored_moves.moves[0].score;
  gboolean found = FALSE;
  gint played_score = 0;
  for (size_t i = 0; i < scored_moves.count; ++i) {
    if (!checkers_moves_equal(played_move, &scored_moves.moves[i].move)) {
      continue;
    }
    found = TRUE;
    played_score = scored_moves.moves[i].score;
    break;
  }

  checkers_scored_move_list_free(&scored_moves);
  if (!found) {
    g_debug("Line move not found among legal scored moves");
    return FALSE;
  }

  *out_played_score = played_score;
  return TRUE;
}

void checkers_eval_non_zero_predicate_init(CheckersEvalNonZeroPredicate *predicate, guint depth) {
  g_return_if_fail(predicate != NULL);
  g_return_if_fail(depth > 0);

  predicate->depth = depth;
  predicate->last_score = 0;
  predicate->has_last_score = FALSE;
}

void checkers_eval_profile_predicate_init(CheckersEvalProfilePredicate *predicate,
                                          guint zero_depth_a,
                                          guint zero_depth_b,
                                          guint non_zero_depth) {
  g_return_if_fail(predicate != NULL);
  g_return_if_fail(zero_depth_a > 0);
  g_return_if_fail(zero_depth_b > 0);
  g_return_if_fail(non_zero_depth > 0);

  predicate->zero_depth_a = zero_depth_a;
  predicate->zero_depth_b = zero_depth_b;
  predicate->non_zero_depth = non_zero_depth;
  predicate->last_score_zero_a = 0;
  predicate->last_score_zero_b = 0;
  predicate->last_score_non_zero = 0;
  predicate->has_last_scores = FALSE;
}

void checkers_mistake_predicate_init(CheckersMistakePredicate *predicate,
                                     const Game *root,
                                     guint max_checked_plies,
                                     guint depth) {
  g_return_if_fail(predicate != NULL);
  g_return_if_fail(root != NULL);
  g_return_if_fail(max_checked_plies > 0);
  g_return_if_fail(depth > 0);

  predicate->root = *root;
  predicate->max_checked_plies = max_checked_plies;
  predicate->depth = depth;
  predicate->last_mistake_ply = 0;
  predicate->last_best_score = 0;
  predicate->last_played_score = 0;
  predicate->has_last_mistake = FALSE;
}

void checkers_deep_mistake_predicate_init(CheckersDeepMistakePredicate *predicate,
                                          const Game *root,
                                          guint max_checked_plies,
                                          guint shallow_depth,
                                          guint deep_depth) {
  g_return_if_fail(predicate != NULL);
  g_return_if_fail(root != NULL);
  g_return_if_fail(max_checked_plies > 0);
  g_return_if_fail(shallow_depth > 0);
  g_return_if_fail(deep_depth > 0);

  predicate->root = *root;
  predicate->max_checked_plies = max_checked_plies;
  predicate->shallow_depth = shallow_depth;
  predicate->deep_depth = deep_depth;
  predicate->last_mistake_ply = 0;
  predicate->last_shallow_best_score = 0;
  predicate->last_shallow_played_score = 0;
  predicate->last_deep_best_score = 0;
  predicate->last_deep_played_score = 0;
  predicate->has_last_mistake = FALSE;
}

gboolean checkers_position_eval_best_score(const Game *position, guint depth, gint *out_score) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(depth > 0, FALSE);
  g_return_val_if_fail(out_score != NULL, FALSE);

  return checkers_ai_alpha_beta_evaluate_position(position, depth, out_score);
}

gboolean checkers_position_predicate_eval_non_zero(const Game *position,
                                                   const CheckersMove */*line*/,
                                                   guint /*line_length*/,
                                                   gpointer user_data) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(user_data != NULL, FALSE);

  CheckersEvalNonZeroPredicate *predicate = user_data;
  gint score = 0;
  gboolean ok = checkers_position_eval_best_score(position, predicate->depth, &score);
  predicate->has_last_score = ok;
  if (!ok) {
    predicate->last_score = 0;
    g_debug("Failed to evaluate position for non-zero predicate");
    return FALSE;
  }

  predicate->last_score = score;
  return score != 0;
}

gboolean checkers_position_predicate_eval_profile(const Game *position,
                                                  const CheckersMove */*line*/,
                                                  guint /*line_length*/,
                                                  gpointer user_data) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(user_data != NULL, FALSE);

  CheckersEvalProfilePredicate *predicate = user_data;
  gint score_zero_a = 0;
  gint score_zero_b = 0;
  gint score_non_zero = 0;

  gboolean ok_a = checkers_position_eval_best_score(position, predicate->zero_depth_a, &score_zero_a);
  gboolean ok_b = checkers_position_eval_best_score(position, predicate->zero_depth_b, &score_zero_b);
  gboolean ok_c = checkers_position_eval_best_score(position, predicate->non_zero_depth, &score_non_zero);
  predicate->has_last_scores = ok_a && ok_b && ok_c;
  if (!predicate->has_last_scores) {
    predicate->last_score_zero_a = 0;
    predicate->last_score_zero_b = 0;
    predicate->last_score_non_zero = 0;
    g_debug("Failed to evaluate position for profile predicate");
    return FALSE;
  }

  predicate->last_score_zero_a = score_zero_a;
  predicate->last_score_zero_b = score_zero_b;
  predicate->last_score_non_zero = score_non_zero;

  return score_zero_a == 0 && score_zero_b == 0 && score_non_zero != 0;
}

gboolean checkers_position_predicate_has_mistake(const Game */*position*/,
                                                 const CheckersMove *line,
                                                 guint line_length,
                                                 gpointer user_data) {
  g_return_val_if_fail(line != NULL, FALSE);
  g_return_val_if_fail(user_data != NULL, FALSE);

  CheckersMistakePredicate *predicate = user_data;
  if (line_length < predicate->max_checked_plies) {
    g_debug("Insufficient line length for mistake predicate");
    predicate->has_last_mistake = FALSE;
    return FALSE;
  }

  Game current = predicate->root;
  predicate->has_last_mistake = FALSE;
  for (guint ply = 0; ply < predicate->max_checked_plies; ++ply) {
    CheckersColor turn = current.state.turn;
    gint best_score = 0;
    gint played_score = 0;
    if (!checkers_score_played_move(&current, &line[ply], predicate->depth, &best_score, &played_score)) {
      return FALSE;
    }

    if (checkers_score_worse_for_turn(played_score, best_score, turn)) {
      predicate->last_mistake_ply = ply + 1;
      predicate->last_best_score = best_score;
      predicate->last_played_score = played_score;
      predicate->has_last_mistake = TRUE;
      return TRUE;
    }

    if (game_apply_move(&current, &line[ply]) != 0) {
      g_debug("Failed to replay line while checking mistakes");
      return FALSE;
    }
  }

  return FALSE;
}

gboolean checkers_position_predicate_has_deep_mistake(const Game */*position*/,
                                                      const CheckersMove *line,
                                                      guint line_length,
                                                      gpointer user_data) {
  g_return_val_if_fail(line != NULL, FALSE);
  g_return_val_if_fail(user_data != NULL, FALSE);

  CheckersDeepMistakePredicate *predicate = user_data;
  if (line_length < predicate->max_checked_plies) {
    g_debug("Insufficient line length for deep mistake predicate");
    predicate->has_last_mistake = FALSE;
    return FALSE;
  }

  Game current = predicate->root;
  predicate->has_last_mistake = FALSE;
  for (guint ply = 0; ply < predicate->max_checked_plies; ++ply) {
    CheckersColor turn = current.state.turn;
    gint shallow_best_score = 0;
    gint shallow_played_score = 0;
    if (!checkers_score_played_move(&current,
                                    &line[ply],
                                    predicate->shallow_depth,
                                    &shallow_best_score,
                                    &shallow_played_score)) {
      return FALSE;
    }

    gint deep_best_score = 0;
    gint deep_played_score = 0;
    if (!checkers_score_played_move(&current,
                                    &line[ply],
                                    predicate->deep_depth,
                                    &deep_best_score,
                                    &deep_played_score)) {
      return FALSE;
    }

    gboolean shallow_is_mistake = checkers_score_worse_for_turn(shallow_played_score, shallow_best_score, turn);
    gboolean deep_is_mistake = checkers_score_worse_for_turn(deep_played_score, deep_best_score, turn);
    if (!shallow_is_mistake && deep_is_mistake) {
      predicate->last_mistake_ply = ply + 1;
      predicate->last_shallow_best_score = shallow_best_score;
      predicate->last_shallow_played_score = shallow_played_score;
      predicate->last_deep_best_score = deep_best_score;
      predicate->last_deep_played_score = deep_played_score;
      predicate->has_last_mistake = TRUE;
      return TRUE;
    }

    if (game_apply_move(&current, &line[ply]) != 0) {
      g_debug("Failed to replay line while checking deep mistakes");
      return FALSE;
    }
  }

  return FALSE;
}

gboolean checkers_eval_non_zero_predicate_get_last_score(const CheckersEvalNonZeroPredicate *predicate,
                                                         gint *out_score) {
  g_return_val_if_fail(predicate != NULL, FALSE);
  g_return_val_if_fail(out_score != NULL, FALSE);

  if (!predicate->has_last_score) {
    g_debug("No cached score available in non-zero predicate");
    return FALSE;
  }

  *out_score = predicate->last_score;
  return TRUE;
}

gboolean checkers_eval_profile_predicate_get_last_non_zero_score(const CheckersEvalProfilePredicate *predicate,
                                                                 gint *out_score) {
  g_return_val_if_fail(predicate != NULL, FALSE);
  g_return_val_if_fail(out_score != NULL, FALSE);

  if (!predicate->has_last_scores) {
    g_debug("No cached scores available in profile predicate");
    return FALSE;
  }

  *out_score = predicate->last_score_non_zero;
  return TRUE;
}

gboolean checkers_mistake_predicate_get_last_details(const CheckersMistakePredicate *predicate,
                                                     guint *out_ply,
                                                     gint *out_best_score,
                                                     gint *out_played_score) {
  g_return_val_if_fail(predicate != NULL, FALSE);
  g_return_val_if_fail(out_ply != NULL, FALSE);
  g_return_val_if_fail(out_best_score != NULL, FALSE);
  g_return_val_if_fail(out_played_score != NULL, FALSE);

  if (!predicate->has_last_mistake) {
    g_debug("No cached mistake details available");
    return FALSE;
  }

  *out_ply = predicate->last_mistake_ply;
  *out_best_score = predicate->last_best_score;
  *out_played_score = predicate->last_played_score;
  return TRUE;
}

gboolean checkers_deep_mistake_predicate_get_last_details(const CheckersDeepMistakePredicate *predicate,
                                                          guint *out_ply,
                                                          gint *out_shallow_best_score,
                                                          gint *out_shallow_played_score,
                                                          gint *out_deep_best_score,
                                                          gint *out_deep_played_score) {
  g_return_val_if_fail(predicate != NULL, FALSE);
  g_return_val_if_fail(out_ply != NULL, FALSE);
  g_return_val_if_fail(out_shallow_best_score != NULL, FALSE);
  g_return_val_if_fail(out_shallow_played_score != NULL, FALSE);
  g_return_val_if_fail(out_deep_best_score != NULL, FALSE);
  g_return_val_if_fail(out_deep_played_score != NULL, FALSE);

  if (!predicate->has_last_mistake) {
    g_debug("No cached deep mistake details available");
    return FALSE;
  }

  *out_ply = predicate->last_mistake_ply;
  *out_shallow_best_score = predicate->last_shallow_best_score;
  *out_shallow_played_score = predicate->last_shallow_played_score;
  *out_deep_best_score = predicate->last_deep_best_score;
  *out_deep_played_score = predicate->last_deep_played_score;
  return TRUE;
}
