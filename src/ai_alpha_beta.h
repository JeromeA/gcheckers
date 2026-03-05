#ifndef AI_ALPHA_BETA_H
#define AI_ALPHA_BETA_H

#include "ai_transposition_table.h"
#include "game.h"

#include <glib.h>

typedef struct {
  CheckersMove move;
  gint score;
} CheckersScoredMove;

typedef struct {
  CheckersScoredMove *moves;
  size_t count;
} CheckersScoredMoveList;

typedef gboolean (*CheckersAiCancelFunc)(gpointer user_data);
typedef void (*CheckersAiProgressFunc)(guint64 nodes, gpointer user_data);

/* Caller-owned stats container used by alpha-beta analysis APIs. */
typedef struct {
  guint64 nodes;
  guint64 tt_probes;
  guint64 tt_hits;
  guint64 tt_cutoffs;
} CheckersAiSearchStats;

void checkers_ai_search_stats_clear(CheckersAiSearchStats *stats);
void checkers_ai_search_stats_add(CheckersAiSearchStats *dest, const CheckersAiSearchStats *src);

gboolean checkers_ai_alpha_beta_choose_move(const Game *game, guint max_depth, CheckersMove *out_move);
gboolean checkers_ai_alpha_beta_analyze_moves(const Game *game, guint max_depth, CheckersScoredMoveList *out_moves);
gboolean checkers_ai_alpha_beta_analyze_moves_with_nodes(const Game *game,
                                                         guint max_depth,
                                                         CheckersScoredMoveList *out_moves,
                                                         guint64 *out_nodes);
gboolean checkers_ai_alpha_beta_analyze_moves_cancellable(const Game *game,
                                                          guint max_depth,
                                                          CheckersScoredMoveList *out_moves,
                                                          CheckersAiCancelFunc should_cancel,
                                                          gpointer user_data);
gboolean checkers_ai_alpha_beta_analyze_moves_cancellable_with_nodes(const Game *game,
                                                                     guint max_depth,
                                                                     CheckersScoredMoveList *out_moves,
                                                                     CheckersAiCancelFunc should_cancel,
                                                                     gpointer user_data,
                                                                     guint64 *out_nodes);
gboolean checkers_ai_alpha_beta_analyze_moves_cancellable_with_nodes_progress(
    const Game *game,
    guint max_depth,
    CheckersScoredMoveList *out_moves,
    CheckersAiCancelFunc should_cancel,
    gpointer user_data,
    guint64 *out_nodes,
    CheckersAiProgressFunc on_progress,
    gpointer progress_user_data);
gboolean checkers_ai_alpha_beta_analyze_moves_cancellable_with_tt(
    const Game *game,
    guint max_depth,
    CheckersScoredMoveList *out_moves,
    CheckersAiCancelFunc should_cancel,
    gpointer user_data,
    guint64 *out_nodes,
    CheckersAiProgressFunc on_progress,
    gpointer progress_user_data,
    CheckersAiTranspositionTable *tt,
    /*
     * Optional caller-owned in/out stats.
     * - If NULL, stats are not collected.
     * - If non-NULL, counters are added to the existing values.
     *   Call checkers_ai_search_stats_clear() before invoking to get per-call stats.
     */
    CheckersAiSearchStats *out_stats);
gboolean checkers_ai_alpha_beta_evaluate_position(const Game *game, guint max_depth, gint *out_score);
void checkers_scored_move_list_free(CheckersScoredMoveList *list);

#endif
