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

typedef struct {
  Game root;
  guint max_checked_plies;
  guint depth;
  guint last_mistake_ply;
  gint last_best_score;
  gint last_played_score;
  gboolean has_last_mistake;
} CheckersMistakePredicate;

typedef struct {
  Game root;
  guint max_checked_plies;
  guint shallow_depth;
  guint deep_depth;
  guint last_mistake_ply;
  gint last_shallow_best_score;
  gint last_shallow_played_score;
  gint last_deep_best_score;
  gint last_deep_played_score;
  gboolean has_last_mistake;
} CheckersDeepMistakePredicate;

void checkers_eval_non_zero_predicate_init(CheckersEvalNonZeroPredicate *predicate, guint depth);
void checkers_eval_profile_predicate_init(CheckersEvalProfilePredicate *predicate,
                                          guint zero_depth_a,
                                          guint zero_depth_b,
                                          guint non_zero_depth);
void checkers_mistake_predicate_init(CheckersMistakePredicate *predicate,
                                     const Game *root,
                                     guint max_checked_plies,
                                     guint depth);
void checkers_deep_mistake_predicate_init(CheckersDeepMistakePredicate *predicate,
                                          const Game *root,
                                          guint max_checked_plies,
                                          guint shallow_depth,
                                          guint deep_depth);
gboolean checkers_position_eval_best_score(const Game *position, guint depth, gint *out_score);
gboolean checkers_position_predicate_eval_non_zero(const Game *position,
                                                   const CheckersMove *line,
                                                   guint line_length,
                                                   gpointer user_data);
gboolean checkers_position_predicate_eval_profile(const Game *position,
                                                  const CheckersMove *line,
                                                  guint line_length,
                                                  gpointer user_data);
gboolean checkers_position_predicate_has_mistake(const Game *position,
                                                 const CheckersMove *line,
                                                 guint line_length,
                                                 gpointer user_data);
gboolean checkers_position_predicate_has_deep_mistake(const Game *position,
                                                      const CheckersMove *line,
                                                      guint line_length,
                                                      gpointer user_data);
gboolean checkers_eval_non_zero_predicate_get_last_score(const CheckersEvalNonZeroPredicate *predicate,
                                                         gint *out_score);
gboolean checkers_eval_profile_predicate_get_last_non_zero_score(const CheckersEvalProfilePredicate *predicate,
                                                                 gint *out_score);
gboolean checkers_mistake_predicate_get_last_details(const CheckersMistakePredicate *predicate,
                                                     guint *out_ply,
                                                     gint *out_best_score,
                                                     gint *out_played_score);
gboolean checkers_deep_mistake_predicate_get_last_details(const CheckersDeepMistakePredicate *predicate,
                                                          guint *out_ply,
                                                          gint *out_shallow_best_score,
                                                          gint *out_shallow_played_score,
                                                          gint *out_deep_best_score,
                                                          gint *out_deep_played_score);

#endif
