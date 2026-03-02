#include "position_predicate.h"

#include "ai_alpha_beta.h"

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
