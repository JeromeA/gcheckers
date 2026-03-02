#ifndef POSITION_PREDICATE_H
#define POSITION_PREDICATE_H

#include "position_search.h"

#include <glib.h>

typedef struct {
  guint depth;
  gint last_score;
  gboolean has_last_score;
} CheckersEvalNonZeroPredicate;

typedef struct {
  guint zero_depth_a;
  guint zero_depth_b;
  guint non_zero_depth;
  gint last_score_zero_a;
  gint last_score_zero_b;
  gint last_score_non_zero;
  gboolean has_last_scores;
} CheckersEvalProfilePredicate;

void checkers_eval_non_zero_predicate_init(CheckersEvalNonZeroPredicate *predicate, guint depth);
void checkers_eval_profile_predicate_init(CheckersEvalProfilePredicate *predicate,
                                          guint zero_depth_a,
                                          guint zero_depth_b,
                                          guint non_zero_depth);
gboolean checkers_position_eval_best_score(const Game *position, guint depth, gint *out_score);
gboolean checkers_position_predicate_eval_non_zero(const Game *position,
                                                   const CheckersMove *line,
                                                   guint line_length,
                                                   gpointer user_data);
gboolean checkers_position_predicate_eval_profile(const Game *position,
                                                  const CheckersMove *line,
                                                  guint line_length,
                                                  gpointer user_data);
gboolean checkers_eval_non_zero_predicate_get_last_score(const CheckersEvalNonZeroPredicate *predicate,
                                                         gint *out_score);
gboolean checkers_eval_profile_predicate_get_last_non_zero_score(const CheckersEvalProfilePredicate *predicate,
                                                                 gint *out_score);

#endif
