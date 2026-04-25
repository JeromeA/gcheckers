#ifndef POSITION_SEARCH_H
#define POSITION_SEARCH_H

#include "games/checkers/game.h"

#include <glib.h>

typedef struct {
  guint min_ply;
  guint max_ply;
  gboolean deduplicate_positions;
} CheckersPositionSearchOptions;

typedef struct {
  guint64 evaluated_positions;
  guint64 matched_positions;
} CheckersPositionSearchStats;

typedef gboolean (*CheckersPositionPredicateFunc)(const Game *position,
                                                  const CheckersMove *line,
                                                  guint line_length,
                                                  gpointer user_data);

typedef void (*CheckersPositionMatchFunc)(const Game *position,
                                          const CheckersMove *line,
                                          guint line_length,
                                          gpointer user_data);

gboolean checkers_position_search(const Game *root,
                                  const CheckersPositionSearchOptions *options,
                                  CheckersPositionPredicateFunc predicate,
                                  gpointer predicate_data,
                                  CheckersPositionMatchFunc on_match,
                                  gpointer match_data,
                                  CheckersPositionSearchStats *out_stats);

#endif
