#ifndef AI_ALPHA_BETA_H
#define AI_ALPHA_BETA_H

#include "ai_transposition_table.h"
#include "game.h"

#include <glib.h>

typedef struct {
  CheckersMove move;
  gint score;
  guint64 nodes;
} CheckersScoredMove;

typedef struct {
  CheckersScoredMove *moves;
  size_t count;
} CheckersScoredMoveList;

typedef gboolean (*CheckersAiCancelFunc)(gpointer user_data);

/* Caller-owned stats container used by alpha-beta analysis APIs. */
typedef struct {
  guint64 nodes;
  guint64 tt_probes;
  guint64 tt_hits;
  guint64 tt_cutoffs;
} CheckersAiSearchStats;

typedef void (*CheckersAiProgressFunc)(const CheckersAiSearchStats *stats, gpointer user_data);

void checkers_ai_search_stats_clear(CheckersAiSearchStats *stats);
void checkers_ai_search_stats_add(CheckersAiSearchStats *dest, const CheckersAiSearchStats *src);

gboolean checkers_ai_alpha_beta_choose_move(const Game *game, guint max_depth, CheckersMove *out_move);
gboolean checkers_ai_alpha_beta_analyze_moves(const Game *game, guint max_depth, CheckersScoredMoveList *out_moves);
gboolean checkers_ai_alpha_beta_analyze_moves_cancellable(const Game *game,
                                                          guint max_depth,
                                                          CheckersScoredMoveList *out_moves,
                                                          CheckersAiCancelFunc should_cancel,
                                                          gpointer user_data);
gboolean checkers_ai_alpha_beta_analyze_moves_cancellable_with_tt(
    const Game *game,
    guint max_depth,
    CheckersScoredMoveList *out_moves,
    CheckersAiCancelFunc should_cancel,
    gpointer user_data,
    CheckersAiProgressFunc on_progress,
    gpointer progress_user_data,
    CheckersAiTranspositionTable *tt,
    /*
     * Caller-owned in/out cumulative stats.
     * Counters are added to existing values; call checkers_ai_search_stats_clear()
     * once before starting a new analysis session.
     */
    CheckersAiSearchStats *out_stats);
gboolean checkers_ai_alpha_beta_evaluate_position(const Game *game, guint max_depth, gint *out_score);
gboolean checkers_ai_evaluate_static_material(const Game *game, gint *out_score);
void checkers_scored_move_list_free(CheckersScoredMoveList *list);

#endif
