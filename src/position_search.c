#include "position_search.h"

#include <string.h>

typedef struct {
  uint8_t board_size;
  uint8_t turn;
  uint8_t winner;
  uint8_t board_data[CHECKERS_MAX_SQUARES];
} CheckersPositionKey;

typedef struct {
  const CheckersPositionSearchOptions *options;
  CheckersPositionPredicateFunc predicate;
  gpointer predicate_data;
  CheckersPositionMatchFunc on_match;
  gpointer match_data;
  CheckersPositionSearchStats *stats;
  GHashTable *seen_positions;
} CheckersPositionSearchCtx;

static GBytes *checkers_position_search_key_new(const Game *position) {
  g_return_val_if_fail(position != NULL, NULL);

  CheckersPositionKey key = {0};
  key.board_size = position->state.board.board_size;
  key.turn = (uint8_t)position->state.turn;
  key.winner = (uint8_t)position->state.winner;
  memcpy(key.board_data, position->state.board.data, sizeof(key.board_data));

  return g_bytes_new(&key, sizeof(key));
}

static gboolean checkers_position_search_mark_seen(CheckersPositionSearchCtx *ctx, const Game *position) {
  g_return_val_if_fail(ctx != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);

  if (!ctx->options->deduplicate_positions) {
    return TRUE;
  }

  GBytes *key = checkers_position_search_key_new(position);
  if (key == NULL) {
    return FALSE;
  }

  if (g_hash_table_contains(ctx->seen_positions, key)) {
    g_bytes_unref(key);
    return FALSE;
  }

  g_hash_table_add(ctx->seen_positions, key);
  return TRUE;
}

static gboolean checkers_position_search_recursive(const Game *position,
                                                   guint ply,
                                                   CheckersMove *line,
                                                   CheckersPositionSearchCtx *ctx) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(ctx != NULL, FALSE);

  gboolean is_new_position = checkers_position_search_mark_seen(ctx, position);
  if (!is_new_position) {
    return TRUE;
  }

  if (ply >= ctx->options->min_ply) {
    ctx->stats->evaluated_positions++;
    if (ctx->predicate(position, line, ply, ctx->predicate_data)) {
      ctx->stats->matched_positions++;
      if (ctx->on_match != NULL) {
        ctx->on_match(position, line, ply, ctx->match_data);
      }
    }
  }

  if (ply >= ctx->options->max_ply || position->state.winner != CHECKERS_WINNER_NONE) {
    return TRUE;
  }

  MoveList moves = position->available_moves(position);
  for (size_t i = 0; i < moves.count; ++i) {
    if (ply >= CHECKERS_MAX_MOVE_LENGTH) {
      g_debug("Reached move history length limit while searching positions");
      break;
    }

    Game child = *position;
    if (game_apply_move(&child, &moves.moves[i]) != 0) {
      g_debug("Skipping invalid move while traversing positions");
      continue;
    }

    line[ply] = moves.moves[i];
    if (!checkers_position_search_recursive(&child, ply + 1, line, ctx)) {
      movelist_free(&moves);
      return FALSE;
    }
  }

  movelist_free(&moves);
  return TRUE;
}

gboolean checkers_position_search(const Game *root,
                                  const CheckersPositionSearchOptions *options,
                                  CheckersPositionPredicateFunc predicate,
                                  gpointer predicate_data,
                                  CheckersPositionMatchFunc on_match,
                                  gpointer match_data,
                                  CheckersPositionSearchStats *out_stats) {
  g_return_val_if_fail(root != NULL, FALSE);
  g_return_val_if_fail(options != NULL, FALSE);
  g_return_val_if_fail(predicate != NULL, FALSE);
  g_return_val_if_fail(out_stats != NULL, FALSE);
  g_return_val_if_fail(options->max_ply >= options->min_ply, FALSE);

  CheckersPositionSearchStats stats = {0};
  CheckersPositionSearchCtx ctx = {
      .options = options,
      .predicate = predicate,
      .predicate_data = predicate_data,
      .on_match = on_match,
      .match_data = match_data,
      .stats = &stats,
      .seen_positions = NULL,
  };

  if (options->deduplicate_positions) {
    ctx.seen_positions = g_hash_table_new_full(g_bytes_hash,
                                               g_bytes_equal,
                                               (GDestroyNotify)g_bytes_unref,
                                               NULL);
  }

  CheckersMove line[CHECKERS_MAX_MOVE_LENGTH] = {0};
  gboolean ok = checkers_position_search_recursive(root, 0, line, &ctx);
  if (ctx.seen_positions != NULL) {
    g_hash_table_destroy(ctx.seen_positions);
  }

  if (!ok) {
    g_debug("Position search failed");
    return FALSE;
  }

  *out_stats = stats;
  return TRUE;
}
