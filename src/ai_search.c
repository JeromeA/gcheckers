#include "ai_search.h"

#include <limits.h>
#include <string.h>

typedef struct {
  guint64 key;
  gint score;
  guint16 depth;
  guint8 bound;
  guint32 age;
  gboolean valid;
  gboolean has_best_move;
} GameAiTtSlot;

struct _GameAiTranspositionTable {
  guint8 *entries;
  guint32 entry_count;
  guint32 index_mask;
  guint32 age_counter;
  gsize move_size;
  gsize stride;
};

typedef struct {
  const GameBackend *backend;
  GameAiCancelFunc should_cancel;
  gpointer cancel_user_data;
  GameAiProgressFunc on_progress;
  gpointer progress_user_data;
  GameAiSearchStats *stats;
  GameAiTranspositionTable *tt;
} GameAiSearchContext;

static guint32 game_ai_tt_floor_pow2(guint64 value) {
  if (value == 0) {
    return 0;
  }

  guint64 result = 1;
  while ((result << 1) != 0 && (result << 1) <= value && (result << 1) <= G_MAXUINT32) {
    result <<= 1;
  }
  return (guint32) result;
}

static GameAiTtSlot *game_ai_tt_slot_mutable(GameAiTranspositionTable *tt, guint64 key) {
  g_return_val_if_fail(tt != NULL, NULL);

  return (GameAiTtSlot *) (tt->entries + ((key & tt->index_mask) * tt->stride));
}

static const GameAiTtSlot *game_ai_tt_slot(const GameAiTranspositionTable *tt, guint64 key) {
  g_return_val_if_fail(tt != NULL, NULL);

  return (const GameAiTtSlot *) (tt->entries + ((key & tt->index_mask) * tt->stride));
}

static guint8 *game_ai_tt_slot_move_mutable(GameAiTranspositionTable *tt, GameAiTtSlot *slot) {
  g_return_val_if_fail(tt != NULL, NULL);
  g_return_val_if_fail(slot != NULL, NULL);

  return ((guint8 *) slot) + sizeof(*slot);
}

static const guint8 *game_ai_tt_slot_move(const GameAiTtSlot *slot) {
  g_return_val_if_fail(slot != NULL, NULL);

  return ((const guint8 *) slot) + sizeof(*slot);
}

static gboolean game_ai_tt_lookup_slot(const GameAiTranspositionTable *tt, guint64 key, const GameAiTtSlot **out_slot) {
  g_return_val_if_fail(tt != NULL, FALSE);
  g_return_val_if_fail(out_slot != NULL, FALSE);

  const GameAiTtSlot *slot = game_ai_tt_slot(tt, key);
  if (!slot->valid || slot->key != key) {
    *out_slot = NULL;
    return FALSE;
  }

  *out_slot = slot;
  return TRUE;
}

static gboolean game_ai_tt_should_replace(const GameAiTranspositionTable *tt,
                                          const GameAiTtSlot *current,
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

GameAiTranspositionTable *game_ai_tt_new(gsize size_mb, gsize move_size) {
  g_return_val_if_fail(size_mb > 0, NULL);
  g_return_val_if_fail(move_size > 0, NULL);

  gsize size_bytes = size_mb * 1024 * 1024;
  gsize stride = sizeof(GameAiTtSlot) + move_size;
  gsize raw_entries = size_bytes / stride;
  guint32 entry_count = game_ai_tt_floor_pow2(raw_entries);
  if (entry_count == 0) {
    g_debug("Transposition table allocation size is too small");
    return NULL;
  }

  GameAiTranspositionTable *tt = g_new0(GameAiTranspositionTable, 1);
  tt->entries = g_malloc0(entry_count * stride);
  tt->entry_count = entry_count;
  tt->index_mask = entry_count - 1;
  tt->age_counter = 1;
  tt->move_size = move_size;
  tt->stride = stride;
  return tt;
}

void game_ai_tt_free(GameAiTranspositionTable *tt) {
  if (tt == NULL) {
    return;
  }

  g_free(tt->entries);
  g_free(tt);
}

void game_ai_tt_clear(GameAiTranspositionTable *tt) {
  g_return_if_fail(tt != NULL);

  memset(tt->entries, 0, tt->entry_count * tt->stride);
}

void game_ai_tt_new_generation(GameAiTranspositionTable *tt) {
  g_return_if_fail(tt != NULL);

  tt->age_counter++;
  if (tt->age_counter == 0) {
    tt->age_counter = 1;
    game_ai_tt_clear(tt);
  }
}

guint32 game_ai_tt_entry_count(const GameAiTranspositionTable *tt) {
  g_return_val_if_fail(tt != NULL, 0);

  return tt->entry_count;
}

gboolean game_ai_tt_lookup(const GameAiTranspositionTable *tt, guint64 key, GameAiTtEntry *out_entry) {
  const GameAiTtSlot *slot = NULL;

  g_return_val_if_fail(tt != NULL, FALSE);
  g_return_val_if_fail(out_entry != NULL, FALSE);

  memset(out_entry, 0, sizeof(*out_entry));
  if (!game_ai_tt_lookup_slot(tt, key, &slot)) {
    return FALSE;
  }

  out_entry->key = slot->key;
  out_entry->score = slot->score;
  out_entry->depth = slot->depth;
  out_entry->bound = slot->bound;
  out_entry->age = slot->age;
  out_entry->valid = slot->valid;
  out_entry->best_move_size = slot->has_best_move ? tt->move_size : 0;
  if (slot->has_best_move) {
    out_entry->best_move = g_memdup2(game_ai_tt_slot_move(slot), tt->move_size);
  }
  return TRUE;
}

void game_ai_tt_store(GameAiTranspositionTable *tt,
                      guint64 key,
                      guint depth,
                      gint score,
                      GameAiTtBound bound,
                      gconstpointer best_move) {
  g_return_if_fail(tt != NULL);
  g_return_if_fail(bound <= GAME_AI_TT_BOUND_UPPER);

  GameAiTtSlot *slot = game_ai_tt_slot_mutable(tt, key);
  if (!game_ai_tt_should_replace(tt, slot, key, depth)) {
    return;
  }

  slot->valid = TRUE;
  slot->key = key;
  slot->score = score;
  slot->depth = depth > G_MAXUINT16 ? G_MAXUINT16 : (guint16) depth;
  slot->bound = (guint8) bound;
  slot->age = tt->age_counter;
  slot->has_best_move = best_move != NULL;
  if (best_move != NULL) {
    memcpy(game_ai_tt_slot_move_mutable(tt, slot), best_move, tt->move_size);
  } else {
    memset(game_ai_tt_slot_move_mutable(tt, slot), 0, tt->move_size);
  }
}

void game_ai_tt_entry_clear(GameAiTtEntry *entry) {
  g_return_if_fail(entry != NULL);

  g_clear_pointer(&entry->best_move, g_free);
  memset(entry, 0, sizeof(*entry));
}

static void game_ai_search_count_node(GameAiSearchContext *ctx) {
  ctx->stats->nodes++;
  if (ctx->on_progress != NULL) {
    ctx->on_progress(ctx->stats, ctx->progress_user_data);
  }
}

static GameBackendOutcome game_ai_search_derived_outcome(const GameBackend *backend, gconstpointer position) {
  GameBackendOutcome outcome = backend->position_outcome(position);
  if (outcome != GAME_BACKEND_OUTCOME_ONGOING) {
    return outcome;
  }

  guint side_to_move = backend->position_turn(position);
  return side_to_move == 0 ? GAME_BACKEND_OUTCOME_SIDE_1_WIN : GAME_BACKEND_OUTCOME_SIDE_0_WIN;
}

static void game_ai_search_store_result(GameAiSearchContext *ctx,
                                        guint64 key,
                                        guint depth_remaining,
                                        gint score,
                                        GameAiTtBound bound,
                                        gconstpointer best_move) {
  if (ctx->tt == NULL) {
    return;
  }

  game_ai_tt_store(ctx->tt, key, depth_remaining, score, bound, best_move);
}

static gboolean game_ai_search_tt_probe(GameAiSearchContext *ctx,
                                        guint64 key,
                                        guint depth_remaining,
                                        gint *alpha,
                                        gint *beta,
                                        GameAiTtEntry *out_entry,
                                        gboolean *out_has_entry,
                                        gboolean *out_cutoff,
                                        gint *out_cutoff_score) {
  g_return_val_if_fail(ctx != NULL, FALSE);
  g_return_val_if_fail(alpha != NULL, FALSE);
  g_return_val_if_fail(beta != NULL, FALSE);
  g_return_val_if_fail(out_entry != NULL, FALSE);
  g_return_val_if_fail(out_has_entry != NULL, FALSE);
  g_return_val_if_fail(out_cutoff != NULL, FALSE);
  g_return_val_if_fail(out_cutoff_score != NULL, FALSE);

  *out_has_entry = FALSE;
  *out_cutoff = FALSE;
  *out_cutoff_score = 0;
  memset(out_entry, 0, sizeof(*out_entry));

  if (ctx->tt == NULL) {
    return TRUE;
  }

  ctx->stats->tt_probes++;
  if (!game_ai_tt_lookup(ctx->tt, key, out_entry)) {
    return TRUE;
  }

  *out_has_entry = TRUE;
  ctx->stats->tt_hits++;
  if (out_entry->depth < depth_remaining) {
    return TRUE;
  }

  switch ((GameAiTtBound) out_entry->bound) {
    case GAME_AI_TT_BOUND_EXACT:
      *out_cutoff = TRUE;
      *out_cutoff_score = out_entry->score;
      return TRUE;
    case GAME_AI_TT_BOUND_LOWER:
      if (out_entry->score > *alpha) {
        *alpha = out_entry->score;
      }
      break;
    case GAME_AI_TT_BOUND_UPPER:
      if (out_entry->score < *beta) {
        *beta = out_entry->score;
      }
      break;
    default:
      break;
  }

  if (*alpha >= *beta) {
    *out_cutoff = TRUE;
    *out_cutoff_score = out_entry->score;
  }

  return TRUE;
}

static gint game_ai_search_recursive(gpointer position,
                                     guint depth_remaining,
                                     guint ply_depth,
                                     gint alpha,
                                     gint beta,
                                     GameAiSearchContext *ctx,
                                     gboolean *cancelled) {
  const GameBackend *backend = ctx->backend;

  game_ai_search_count_node(ctx);

  if (ctx->should_cancel != NULL && ctx->should_cancel(ctx->cancel_user_data)) {
    *cancelled = TRUE;
    return 0;
  }

  guint64 key = backend->hash_position(position);
  GameBackendOutcome outcome = backend->position_outcome(position);
  if (outcome != GAME_BACKEND_OUTCOME_ONGOING) {
    gint score = backend->terminal_score(outcome, ply_depth);
    game_ai_search_store_result(ctx, key, depth_remaining, score, GAME_AI_TT_BOUND_EXACT, NULL);
    return score;
  }

  GameBackendMoveList moves = backend->list_moves(position);
  if (moves.count == 0) {
    gint score = backend->terminal_score(game_ai_search_derived_outcome(backend, position), ply_depth);
    backend->move_list_free(&moves);
    game_ai_search_store_result(ctx, key, depth_remaining, score, GAME_AI_TT_BOUND_EXACT, NULL);
    return score;
  }

  gboolean is_forced = moves.count == 1;
  if (depth_remaining == 0 && !is_forced) {
    gint score = backend->evaluate_static(position);
    backend->move_list_free(&moves);
    game_ai_search_store_result(ctx, key, depth_remaining, score, GAME_AI_TT_BOUND_EXACT, NULL);
    return score;
  }

  gint original_alpha = alpha;
  gint original_beta = beta;
  GameAiTtEntry tt_entry = {0};
  gboolean has_tt_entry = FALSE;
  gboolean tt_cutoff = FALSE;
  gint tt_cutoff_score = 0;

  if (!game_ai_search_tt_probe(ctx,
                               key,
                               depth_remaining,
                               &alpha,
                               &beta,
                               &tt_entry,
                               &has_tt_entry,
                               &tt_cutoff,
                               &tt_cutoff_score)) {
    g_debug("Failed to probe TT entry");
  }
  if (tt_cutoff) {
    backend->move_list_free(&moves);
    ctx->stats->tt_cutoffs++;
    game_ai_tt_entry_clear(&tt_entry);
    return tt_cutoff_score;
  }

  if (has_tt_entry && tt_entry.best_move != NULL) {
    for (gsize i = 0; i < moves.count; ++i) {
      const void *move = backend->move_list_get(&moves, i);
      if (move == NULL || !backend->moves_equal(move, tt_entry.best_move)) {
        continue;
      }

      if (i != 0) {
        guint8 *all_moves = moves.moves;
        guchar *tmp = g_malloc(backend->move_size);

        memcpy(tmp, all_moves, backend->move_size);
        memcpy(all_moves, all_moves + (i * backend->move_size), backend->move_size);
        memcpy(all_moves + (i * backend->move_size), tmp, backend->move_size);
        g_free(tmp);
      }
      break;
    }
  }
  game_ai_tt_entry_clear(&tt_entry);

  gboolean maximizing = backend->position_turn(position) == 0;
  gint best = maximizing ? INT_MIN : INT_MAX;
  gpointer best_move = NULL;

  for (gsize i = 0; i < moves.count; ++i) {
    if (ctx->should_cancel != NULL && ctx->should_cancel(ctx->cancel_user_data)) {
      *cancelled = TRUE;
      break;
    }

    const void *move = backend->move_list_get(&moves, i);
    gpointer child = g_malloc0(backend->position_size);
    if (move == NULL || child == NULL) {
      g_free(child);
      continue;
    }

    backend->position_copy(child, position);
    if (!backend->apply_move(child, move)) {
      backend->position_clear(child);
      g_free(child);
      g_debug("Skipping invalid move while searching");
      continue;
    }

    guint next_depth = is_forced ? depth_remaining : depth_remaining - 1;
    gint score = game_ai_search_recursive(child, next_depth, ply_depth + 1, alpha, beta, ctx, cancelled);

    backend->position_clear(child);
    g_free(child);

    if (*cancelled) {
      break;
    }

    if (maximizing) {
      if (score > best) {
        best = score;
        best_move = (gpointer) move;
      }
      if (best > alpha) {
        alpha = best;
      }
    } else {
      if (score < best) {
        best = score;
        best_move = (gpointer) move;
      }
      if (best < beta) {
        beta = best;
      }
    }

    if (beta <= alpha) {
      break;
    }
  }

  backend->move_list_free(&moves);
  if (*cancelled) {
    return 0;
  }

  if (best == INT_MIN || best == INT_MAX) {
    best = backend->evaluate_static(position);
    best_move = NULL;
  }

  GameAiTtBound bound = GAME_AI_TT_BOUND_EXACT;
  if (best <= original_alpha) {
    bound = GAME_AI_TT_BOUND_UPPER;
  } else if (best >= original_beta) {
    bound = GAME_AI_TT_BOUND_LOWER;
  }
  game_ai_search_store_result(ctx, key, depth_remaining, best, bound, best_move);

  return best;
}

static int game_ai_scored_move_compare_desc(const void *left, const void *right) {
  const GameAiScoredMove *a = left;
  const GameAiScoredMove *b = right;

  if (a->score < b->score) {
    return 1;
  }
  if (a->score > b->score) {
    return -1;
  }
  return 0;
}

static int game_ai_scored_move_compare_asc(const void *left, const void *right) {
  const GameAiScoredMove *a = left;
  const GameAiScoredMove *b = right;

  if (a->score < b->score) {
    return -1;
  }
  if (a->score > b->score) {
    return 1;
  }
  return 0;
}

void game_ai_search_stats_clear(GameAiSearchStats *stats) {
  g_return_if_fail(stats != NULL);

  memset(stats, 0, sizeof(*stats));
}

void game_ai_search_stats_add(GameAiSearchStats *dest, const GameAiSearchStats *src) {
  g_return_if_fail(dest != NULL);
  g_return_if_fail(src != NULL);

  dest->nodes += src->nodes;
  dest->tt_probes += src->tt_probes;
  dest->tt_hits += src->tt_hits;
  dest->tt_cutoffs += src->tt_cutoffs;
}

void game_ai_scored_move_list_free(GameAiScoredMoveList *list) {
  g_return_if_fail(list != NULL);

  for (gsize i = 0; i < list->count; ++i) {
    g_clear_pointer(&list->moves[i].move, g_free);
  }
  g_clear_pointer(&list->moves, g_free);
  list->count = 0;
}

gboolean game_ai_search_analyze_moves(const GameBackend *backend,
                                      gconstpointer position,
                                      guint max_depth,
                                      GameAiScoredMoveList *out_moves) {
  return game_ai_search_analyze_moves_cancellable(backend, position, max_depth, out_moves, NULL, NULL);
}

gboolean game_ai_search_analyze_moves_cancellable(const GameBackend *backend,
                                                  gconstpointer position,
                                                  guint max_depth,
                                                  GameAiScoredMoveList *out_moves,
                                                  GameAiCancelFunc should_cancel,
                                                  gpointer user_data) {
  GameAiSearchStats stats = {0};

  game_ai_search_stats_clear(&stats);
  return game_ai_search_analyze_moves_cancellable_with_tt(backend,
                                                          position,
                                                          max_depth,
                                                          out_moves,
                                                          should_cancel,
                                                          user_data,
                                                          NULL,
                                                          NULL,
                                                          NULL,
                                                          &stats);
}

gboolean game_ai_search_analyze_moves_cancellable_with_tt(const GameBackend *backend,
                                                          gconstpointer position,
                                                          guint max_depth,
                                                          GameAiScoredMoveList *out_moves,
                                                          GameAiCancelFunc should_cancel,
                                                          gpointer user_data,
                                                          GameAiProgressFunc on_progress,
                                                          gpointer progress_user_data,
                                                          GameAiTranspositionTable *tt,
                                                          GameAiSearchStats *out_stats) {
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(out_moves != NULL, FALSE);
  g_return_val_if_fail(out_stats != NULL, FALSE);
  if (!backend->supports_ai_search || !backend->supports_move_list) {
    g_debug("AI analysis requires a backend with AI and full move-list support");
    return FALSE;
  }
  g_return_val_if_fail(backend->position_copy != NULL, FALSE);
  g_return_val_if_fail(backend->position_clear != NULL, FALSE);
  g_return_val_if_fail(backend->position_outcome != NULL, FALSE);
  g_return_val_if_fail(backend->position_turn != NULL, FALSE);
  g_return_val_if_fail(backend->list_moves != NULL, FALSE);
  g_return_val_if_fail(backend->move_list_free != NULL, FALSE);
  g_return_val_if_fail(backend->move_list_get != NULL, FALSE);
  g_return_val_if_fail(backend->moves_equal != NULL, FALSE);
  g_return_val_if_fail(backend->apply_move != NULL, FALSE);
  g_return_val_if_fail(backend->evaluate_static != NULL, FALSE);
  g_return_val_if_fail(backend->terminal_score != NULL, FALSE);
  g_return_val_if_fail(backend->hash_position != NULL, FALSE);

  out_moves->moves = NULL;
  out_moves->count = 0;

  GameBackendMoveList moves = backend->list_moves(position);
  if (moves.count == 0) {
    backend->move_list_free(&moves);
    g_debug("No available moves for alpha-beta analysis");
    return FALSE;
  }

  if (tt != NULL) {
    game_ai_tt_new_generation(tt);
  }

  GameAiScoredMove *scored_moves = g_new0(GameAiScoredMove, moves.count);
  gsize write = 0;
  gboolean cancelled = FALSE;
  GameAiSearchContext ctx = {
      .backend = backend,
      .should_cancel = should_cancel,
      .cancel_user_data = user_data,
      .on_progress = on_progress,
      .progress_user_data = progress_user_data,
      .stats = out_stats,
      .tt = tt,
  };

  for (gsize i = 0; i < moves.count; ++i) {
    if (should_cancel != NULL && should_cancel(user_data)) {
      cancelled = TRUE;
      break;
    }

    const void *move = backend->move_list_get(&moves, i);
    gpointer child = g_malloc0(backend->position_size);
    if (move == NULL || child == NULL) {
      g_free(child);
      continue;
    }

    backend->position_copy(child, position);
    if (!backend->apply_move(child, move)) {
      backend->position_clear(child);
      g_free(child);
      g_debug("Skipping invalid root move while analyzing");
      continue;
    }

    guint next_depth = (moves.count == 1 || max_depth == 0) ? max_depth : max_depth - 1;
    guint64 nodes_before = out_stats->nodes;
    gint score = game_ai_search_recursive(child, next_depth, 1, INT_MIN, INT_MAX, &ctx, &cancelled);

    backend->position_clear(child);
    g_free(child);

    if (cancelled) {
      break;
    }

    scored_moves[write].move = g_memdup2(move, backend->move_size);
    scored_moves[write].score = score;
    scored_moves[write].nodes = out_stats->nodes - nodes_before;
    write++;
  }

  backend->move_list_free(&moves);
  if (cancelled) {
    g_free(scored_moves);
    return FALSE;
  }

  if (write == 0) {
    g_free(scored_moves);
    g_debug("Alpha-beta analysis found no valid root moves");
    return FALSE;
  }

  if (backend->position_turn(position) == 0) {
    qsort(scored_moves, write, sizeof(scored_moves[0]), game_ai_scored_move_compare_desc);
  } else {
    qsort(scored_moves, write, sizeof(scored_moves[0]), game_ai_scored_move_compare_asc);
  }

  out_moves->moves = scored_moves;
  out_moves->count = write;
  return TRUE;
}

gboolean game_ai_search_evaluate_position(const GameBackend *backend,
                                          gconstpointer position,
                                          guint max_depth,
                                          gint *out_score) {
  gpointer root = NULL;
  gboolean cancelled = FALSE;
  GameAiSearchStats stats = {0};
  GameAiSearchContext ctx = {0};

  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(out_score != NULL, FALSE);
  if (!backend->supports_ai_search || !backend->supports_move_list) {
    g_debug("Position evaluation requires a backend with AI and full move-list support");
    return FALSE;
  }
  g_return_val_if_fail(backend->position_copy != NULL, FALSE);
  g_return_val_if_fail(backend->position_clear != NULL, FALSE);
  g_return_val_if_fail(backend->position_outcome != NULL, FALSE);
  g_return_val_if_fail(backend->position_turn != NULL, FALSE);
  g_return_val_if_fail(backend->list_moves != NULL, FALSE);
  g_return_val_if_fail(backend->move_list_free != NULL, FALSE);
  g_return_val_if_fail(backend->move_list_get != NULL, FALSE);
  g_return_val_if_fail(backend->moves_equal != NULL, FALSE);
  g_return_val_if_fail(backend->apply_move != NULL, FALSE);
  g_return_val_if_fail(backend->evaluate_static != NULL, FALSE);
  g_return_val_if_fail(backend->terminal_score != NULL, FALSE);
  g_return_val_if_fail(backend->hash_position != NULL, FALSE);

  root = g_malloc0(backend->position_size);
  g_return_val_if_fail(root != NULL, FALSE);

  backend->position_copy(root, position);
  game_ai_search_stats_clear(&stats);
  ctx.backend = backend;
  ctx.stats = &stats;

  *out_score = game_ai_search_recursive(root, max_depth, 0, INT_MIN, INT_MAX, &ctx, &cancelled);
  backend->position_clear(root);
  g_free(root);

  if (cancelled) {
    g_debug("Unexpected cancellation while evaluating position");
    return FALSE;
  }

  return TRUE;
}

gboolean game_ai_search_choose_move(const GameBackend *backend,
                                    gconstpointer position,
                                    guint max_depth,
                                    gpointer out_move) {
  GameAiScoredMoveList scored_moves = {0};

  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  if (!game_ai_search_analyze_moves(backend, position, max_depth, &scored_moves)) {
    return FALSE;
  }

  gint best_score = scored_moves.moves[0].score;
  gsize best_count = 1;
  while (best_count < scored_moves.count && scored_moves.moves[best_count].score == best_score) {
    best_count++;
  }

  guint selected_index = g_random_int_range(0, (gint) best_count);
  memcpy(out_move, scored_moves.moves[selected_index].move, backend->move_size);
  game_ai_scored_move_list_free(&scored_moves);
  return TRUE;
}

gboolean game_ai_evaluate_static(const GameBackend *backend, gconstpointer position, gint *out_score) {
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(out_score != NULL, FALSE);
  if (!backend->supports_ai_search) {
    g_debug("Static evaluation requires a backend with AI support");
    return FALSE;
  }
  g_return_val_if_fail(backend->evaluate_static != NULL, FALSE);

  *out_score = backend->evaluate_static(position);
  return TRUE;
}
