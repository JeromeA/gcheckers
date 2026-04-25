#include "ai_transposition_table.h"

#include <string.h>

struct _CheckersAiTranspositionTable {
  GameAiTranspositionTable *table;
};

CheckersAiTranspositionTable *checkers_ai_tt_new(gsize size_mb) {
  g_return_val_if_fail(size_mb > 0, NULL);

  CheckersAiTranspositionTable *tt = g_new0(CheckersAiTranspositionTable, 1);
  tt->table = game_ai_tt_new(size_mb, sizeof(CheckersMove));
  if (tt->table == NULL) {
    g_free(tt);
    return NULL;
  }

  return tt;
}

void checkers_ai_tt_free(CheckersAiTranspositionTable *tt) {
  if (tt == NULL) {
    return;
  }

  game_ai_tt_free(tt->table);
  g_free(tt);
}

void checkers_ai_tt_clear(CheckersAiTranspositionTable *tt) {
  g_return_if_fail(tt != NULL);

  game_ai_tt_clear(tt->table);
}

void checkers_ai_tt_new_generation(CheckersAiTranspositionTable *tt) {
  g_return_if_fail(tt != NULL);

  game_ai_tt_new_generation(tt->table);
}

guint32 checkers_ai_tt_entry_count(const CheckersAiTranspositionTable *tt) {
  g_return_val_if_fail(tt != NULL, 0);

  return game_ai_tt_entry_count(tt->table);
}

gboolean checkers_ai_tt_lookup(const CheckersAiTranspositionTable *tt, guint64 key, CheckersAiTtEntry *out_entry) {
  GameAiTtEntry generic_entry = {0};

  g_return_val_if_fail(tt != NULL, FALSE);
  g_return_val_if_fail(out_entry != NULL, FALSE);

  memset(out_entry, 0, sizeof(*out_entry));
  if (!game_ai_tt_lookup(tt->table, key, &generic_entry)) {
    return FALSE;
  }

  out_entry->key = generic_entry.key;
  out_entry->score = generic_entry.score;
  out_entry->depth = generic_entry.depth;
  out_entry->bound = generic_entry.bound;
  out_entry->age = generic_entry.age;
  out_entry->valid = generic_entry.valid;
  if (generic_entry.best_move != NULL) {
    memcpy(&out_entry->best_move, generic_entry.best_move, sizeof(out_entry->best_move));
  } else {
    memset(&out_entry->best_move, 0, sizeof(out_entry->best_move));
  }
  game_ai_tt_entry_clear(&generic_entry);
  return TRUE;
}

void checkers_ai_tt_store(CheckersAiTranspositionTable *tt,
                          guint64 key,
                          guint depth,
                          gint score,
                          CheckersAiTtBound bound,
                          const CheckersMove *best_move) {
  g_return_if_fail(tt != NULL);
  g_return_if_fail(bound <= CHECKERS_AI_TT_BOUND_UPPER);

  game_ai_tt_store(tt->table, key, depth, score, (GameAiTtBound) bound, best_move);
}

GameAiTranspositionTable *checkers_ai_tt_peek_generic(CheckersAiTranspositionTable *tt) {
  g_return_val_if_fail(tt != NULL, NULL);

  return tt->table;
}
