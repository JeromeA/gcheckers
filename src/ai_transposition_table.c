#include "ai_transposition_table.h"

#include <string.h>

struct _CheckersAiTranspositionTable {
  CheckersAiTtEntry *entries;
  guint32 entry_count;
  guint32 index_mask;
  guint32 age_counter;
};

static guint32 checkers_ai_tt_floor_pow2(guint64 value) {
  if (value == 0) {
    return 0;
  }

  guint64 result = 1;
  while ((result << 1) != 0 && (result << 1) <= value && (result << 1) <= G_MAXUINT32) {
    result <<= 1;
  }
  return (guint32)result;
}

CheckersAiTranspositionTable *checkers_ai_tt_new(gsize size_mb) {
  g_return_val_if_fail(size_mb > 0, NULL);

  gsize size_bytes = size_mb * 1024 * 1024;
  gsize raw_entries = size_bytes / sizeof(CheckersAiTtEntry);
  guint32 entry_count = checkers_ai_tt_floor_pow2(raw_entries);
  if (entry_count == 0) {
    g_debug("Transposition table allocation size is too small");
    return NULL;
  }

  CheckersAiTranspositionTable *tt = g_new0(CheckersAiTranspositionTable, 1);
  tt->entries = g_new0(CheckersAiTtEntry, entry_count);
  tt->entry_count = entry_count;
  tt->index_mask = entry_count - 1;
  tt->age_counter = 1;
  return tt;
}

void checkers_ai_tt_free(CheckersAiTranspositionTable *tt) {
  if (tt == NULL) {
    return;
  }

  g_free(tt->entries);
  g_free(tt);
}

void checkers_ai_tt_clear(CheckersAiTranspositionTable *tt) {
  g_return_if_fail(tt != NULL);

  memset(tt->entries, 0, sizeof(CheckersAiTtEntry) * tt->entry_count);
}

void checkers_ai_tt_new_generation(CheckersAiTranspositionTable *tt) {
  g_return_if_fail(tt != NULL);

  tt->age_counter++;
  if (tt->age_counter == 0) {
    tt->age_counter = 1;
    checkers_ai_tt_clear(tt);
  }
}

guint32 checkers_ai_tt_entry_count(const CheckersAiTranspositionTable *tt) {
  g_return_val_if_fail(tt != NULL, 0);

  return tt->entry_count;
}

gboolean checkers_ai_tt_lookup(const CheckersAiTranspositionTable *tt, guint64 key, CheckersAiTtEntry *out_entry) {
  g_return_val_if_fail(tt != NULL, FALSE);
  g_return_val_if_fail(out_entry != NULL, FALSE);

  const CheckersAiTtEntry *entry = &tt->entries[key & tt->index_mask];
  if (!entry->valid || entry->key != key) {
    return FALSE;
  }

  *out_entry = *entry;
  return TRUE;
}

static gboolean checkers_ai_tt_should_replace(const CheckersAiTranspositionTable *tt,
                                              const CheckersAiTtEntry *current,
                                              guint64 key,
                                              guint depth) {
  if (!current->valid) {
    return TRUE;
  }

  if (current->key == key) {
    return depth >= current->depth || current->age != tt->age_counter;
  }

  if (current->age != tt->age_counter) {
    return TRUE;
  }

  return depth >= current->depth;
}

void checkers_ai_tt_store(CheckersAiTranspositionTable *tt,
                          guint64 key,
                          guint depth,
                          gint score,
                          CheckersAiTtBound bound,
                          const CheckersMove *best_move) {
  g_return_if_fail(tt != NULL);
  g_return_if_fail(bound <= CHECKERS_AI_TT_BOUND_UPPER);

  CheckersAiTtEntry *slot = &tt->entries[key & tt->index_mask];
  if (!checkers_ai_tt_should_replace(tt, slot, key, depth)) {
    return;
  }

  slot->valid = TRUE;
  slot->key = key;
  slot->score = score;
  slot->depth = depth > G_MAXUINT16 ? G_MAXUINT16 : (guint16)depth;
  slot->bound = (guint8)bound;
  slot->age = tt->age_counter;
  if (best_move != NULL) {
    slot->best_move = *best_move;
  } else {
    memset(&slot->best_move, 0, sizeof(slot->best_move));
  }
}
