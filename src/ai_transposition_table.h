#ifndef AI_TRANSPOSITION_TABLE_H
#define AI_TRANSPOSITION_TABLE_H

#include "ai_search.h"
#include "game.h"

#include <glib.h>

typedef enum {
  CHECKERS_AI_TT_BOUND_EXACT = 0,
  CHECKERS_AI_TT_BOUND_LOWER = 1,
  CHECKERS_AI_TT_BOUND_UPPER = 2,
} CheckersAiTtBound;

/*
 * Transposition-table entries are ephemeral search cache data.
 * They are valid only for pruning/move-ordering during in-progress search and
 * must never be treated as authoritative persisted analysis results.
 */
typedef struct {
  guint64 key;
  CheckersMove best_move;
  gint score;
  guint16 depth;
  guint8 bound;
  guint32 age;
  gboolean valid;
} CheckersAiTtEntry;

typedef struct _CheckersAiTranspositionTable CheckersAiTranspositionTable;

CheckersAiTranspositionTable *checkers_ai_tt_new(gsize size_mb);
void checkers_ai_tt_free(CheckersAiTranspositionTable *tt);
void checkers_ai_tt_clear(CheckersAiTranspositionTable *tt);
void checkers_ai_tt_new_generation(CheckersAiTranspositionTable *tt);
guint32 checkers_ai_tt_entry_count(const CheckersAiTranspositionTable *tt);
gboolean checkers_ai_tt_lookup(const CheckersAiTranspositionTable *tt, guint64 key, CheckersAiTtEntry *out_entry);
void checkers_ai_tt_store(CheckersAiTranspositionTable *tt,
                          guint64 key,
                          guint depth,
                          gint score,
                          CheckersAiTtBound bound,
                          const CheckersMove *best_move);
GameAiTranspositionTable *checkers_ai_tt_peek_generic(CheckersAiTranspositionTable *tt);

#endif
