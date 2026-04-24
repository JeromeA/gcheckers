#ifndef AI_SEARCH_H
#define AI_SEARCH_H

#include "game_backend.h"

#include <glib.h>

typedef struct {
  gpointer move;
  gint score;
  guint64 nodes;
} GameAiScoredMove;

typedef struct {
  GameAiScoredMove *moves;
  gsize count;
} GameAiScoredMoveList;

typedef gboolean (*GameAiCancelFunc)(gpointer user_data);

typedef struct {
  guint64 nodes;
  guint64 tt_probes;
  guint64 tt_hits;
  guint64 tt_cutoffs;
} GameAiSearchStats;

typedef void (*GameAiProgressFunc)(const GameAiSearchStats *stats, gpointer user_data);

typedef enum {
  GAME_AI_TT_BOUND_EXACT = 0,
  GAME_AI_TT_BOUND_LOWER = 1,
  GAME_AI_TT_BOUND_UPPER = 2,
} GameAiTtBound;

typedef struct {
  guint64 key;
  gpointer best_move;
  gsize best_move_size;
  gint score;
  guint16 depth;
  guint8 bound;
  guint32 age;
  gboolean valid;
} GameAiTtEntry;

typedef struct _GameAiTranspositionTable GameAiTranspositionTable;

void game_ai_search_stats_clear(GameAiSearchStats *stats);
void game_ai_search_stats_add(GameAiSearchStats *dest, const GameAiSearchStats *src);
void game_ai_scored_move_list_free(GameAiScoredMoveList *list);

GameAiTranspositionTable *game_ai_tt_new(gsize size_mb, gsize move_size);
void game_ai_tt_free(GameAiTranspositionTable *tt);
void game_ai_tt_clear(GameAiTranspositionTable *tt);
void game_ai_tt_new_generation(GameAiTranspositionTable *tt);
guint32 game_ai_tt_entry_count(const GameAiTranspositionTable *tt);
gboolean game_ai_tt_lookup(const GameAiTranspositionTable *tt, guint64 key, GameAiTtEntry *out_entry);
void game_ai_tt_store(GameAiTranspositionTable *tt,
                      guint64 key,
                      guint depth,
                      gint score,
                      GameAiTtBound bound,
                      gconstpointer best_move);
void game_ai_tt_entry_clear(GameAiTtEntry *entry);

gboolean game_ai_search_analyze_moves(const GameBackend *backend,
                                      gconstpointer position,
                                      guint max_depth,
                                      GameAiScoredMoveList *out_moves);
gboolean game_ai_search_analyze_moves_cancellable(const GameBackend *backend,
                                                  gconstpointer position,
                                                  guint max_depth,
                                                  GameAiScoredMoveList *out_moves,
                                                  GameAiCancelFunc should_cancel,
                                                  gpointer user_data);
gboolean game_ai_search_analyze_moves_cancellable_with_tt(const GameBackend *backend,
                                                          gconstpointer position,
                                                          guint max_depth,
                                                          GameAiScoredMoveList *out_moves,
                                                          GameAiCancelFunc should_cancel,
                                                          gpointer user_data,
                                                          GameAiProgressFunc on_progress,
                                                          gpointer progress_user_data,
                                                          GameAiTranspositionTable *tt,
                                                          GameAiSearchStats *out_stats);
gboolean game_ai_search_evaluate_position(const GameBackend *backend,
                                          gconstpointer position,
                                          guint max_depth,
                                          gint *out_score);
gboolean game_ai_search_choose_move(const GameBackend *backend,
                                    gconstpointer position,
                                    guint max_depth,
                                    gpointer out_move);
gboolean game_ai_evaluate_static(const GameBackend *backend, gconstpointer position, gint *out_score);

#endif
