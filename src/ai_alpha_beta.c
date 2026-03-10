#include "ai_alpha_beta.h"

#include "ai_zobrist.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  CheckersAiCancelFunc should_cancel;
  gpointer cancel_user_data;
  CheckersAiProgressFunc on_progress;
  gpointer progress_user_data;
  CheckersAiSearchStats *stats;
  CheckersAiTranspositionTable *tt;
} CheckersAiSearchContext;

static void checkers_ai_search_count_node(CheckersAiSearchContext *ctx) {
  /*
   * Hot-path assumption reminder:
   * The following checks are enforced at API boundaries by callers.
   * Keep the commented checks below as reminders if call paths change.
   */
  /* g_return_if_fail(ctx != NULL); */
  /* g_return_if_fail(ctx->stats != NULL); */

  ctx->stats->nodes++;
  if (ctx->on_progress != NULL) {
    ctx->on_progress(ctx->stats, ctx->progress_user_data);
  }
}

static gboolean checkers_ai_moves_match(const CheckersMove *left, const CheckersMove *right) {
  /*
   * Hot-path assumption reminder:
   * The following checks are enforced at API boundaries by callers.
   * Keep the commented checks below as reminders if call paths change.
   */
  /* g_return_val_if_fail(left != NULL, FALSE); */
  /* g_return_val_if_fail(right != NULL, FALSE); */

  if (left->length != right->length || left->captures != right->captures) {
    return FALSE;
  }
  if (left->length == 0) {
    return TRUE;
  }

  return memcmp(left->path, right->path, left->length * sizeof(left->path[0])) == 0;
}

static gint checkers_ai_material_score(const Game *game) {
  /*
   * Hot-path assumption reminder:
   * The following checks are enforced at API boundaries by callers.
   * Keep the commented checks below as reminders if call paths change.
   */
  /* g_return_val_if_fail(game != NULL, 0); */

  const CheckersBoard *board = &game->state.board;
  uint8_t squares = board_playable_squares(board->board_size);
  gint score = 0;
  for (uint8_t i = 0; i < squares; ++i) {
    CheckersPiece piece = board_get(board, i);
    gint value = 0;
    switch (piece) {
      case CHECKERS_PIECE_WHITE_MAN:
      case CHECKERS_PIECE_BLACK_MAN:
        value = 100;
        break;
      case CHECKERS_PIECE_WHITE_KING:
      case CHECKERS_PIECE_BLACK_KING:
        value = 200;
        break;
      case CHECKERS_PIECE_EMPTY:
      default:
        value = 0;
        break;
    }

    if (value == 0) {
      continue;
    }

    CheckersColor color = board_piece_color(piece);
    score += color == CHECKERS_COLOR_WHITE ? value : -value;
  }

  return score;
}

static gint checkers_ai_terminal_score(const Game *game, guint ply_depth) {
  /*
   * Hot-path assumption reminder:
   * The following checks are enforced at API boundaries by callers.
   * Keep the commented checks below as reminders if call paths change.
   */
  /* g_return_val_if_fail(game != NULL, 0); */

  if (game->state.winner == CHECKERS_WINNER_NONE) {
    return checkers_ai_material_score(game);
  }

  gint win_score = 3000 - (gint)ply_depth;
  switch (game->state.winner) {
    case CHECKERS_WINNER_WHITE:
      return win_score;
    case CHECKERS_WINNER_BLACK:
      return -win_score;
    case CHECKERS_WINNER_DRAW:
      return 0;
    case CHECKERS_WINNER_NONE:
    default:
      return checkers_ai_material_score(game);
  }
}

static void checkers_ai_tt_store_result(CheckersAiSearchContext *ctx,
                                        guint64 key,
                                        guint depth_remaining,
                                        gint score,
                                        CheckersAiTtBound bound,
                                        const CheckersMove *best_move) {
  /*
   * Hot-path assumption reminder:
   * The following checks are enforced at API boundaries by callers.
   * Keep the commented checks below as reminders if call paths change.
   */
  /* g_return_if_fail(ctx != NULL); */

  if (ctx->tt == NULL) {
    return;
  }

  checkers_ai_tt_store(ctx->tt, key, depth_remaining, score, bound, best_move);
}

static gboolean checkers_ai_tt_probe(CheckersAiSearchContext *ctx,
                                     guint64 key,
                                     guint depth_remaining,
                                     gint *alpha,
                                     gint *beta,
                                     CheckersAiTtEntry *out_entry,
                                     gboolean *out_has_entry,
                                     gboolean *out_cutoff,
                                     gint *out_cutoff_score) {
  /*
   * Hot-path assumption reminder:
   * The following checks are enforced at API boundaries by callers.
   * Keep the commented checks below as reminders if call paths change.
   */
  /* g_return_val_if_fail(ctx != NULL, FALSE); */
  /* g_return_val_if_fail(alpha != NULL, FALSE); */
  /* g_return_val_if_fail(beta != NULL, FALSE); */
  /* g_return_val_if_fail(out_entry != NULL, FALSE); */
  /* g_return_val_if_fail(out_has_entry != NULL, FALSE); */
  /* g_return_val_if_fail(out_cutoff != NULL, FALSE); */
  /* g_return_val_if_fail(out_cutoff_score != NULL, FALSE); */

  *out_has_entry = FALSE;
  *out_cutoff = FALSE;
  *out_cutoff_score = 0;

  if (ctx->tt == NULL) {
    return TRUE;
  }

  if (ctx->stats != NULL) {
    ctx->stats->tt_probes++;
  }

  if (!checkers_ai_tt_lookup(ctx->tt, key, out_entry)) {
    return TRUE;
  }

  *out_has_entry = TRUE;
  if (ctx->stats != NULL) {
    ctx->stats->tt_hits++;
  }

  if (out_entry->depth < depth_remaining) {
    return TRUE;
  }

  switch ((CheckersAiTtBound)out_entry->bound) {
    case CHECKERS_AI_TT_BOUND_EXACT:
      *out_cutoff = TRUE;
      *out_cutoff_score = out_entry->score;
      return TRUE;
    case CHECKERS_AI_TT_BOUND_LOWER:
      if (out_entry->score > *alpha) {
        *alpha = out_entry->score;
      }
      break;
    case CHECKERS_AI_TT_BOUND_UPPER:
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

static gint checkers_ai_alpha_beta_search(Game *game,
                                          guint depth_remaining,
                                          guint ply_depth,
                                          gint alpha,
                                          gint beta,
                                          CheckersAiSearchContext *ctx,
                                          gboolean *cancelled) {
  /*
   * Hot-path assumption reminder:
   * The following checks are enforced at API boundaries by callers.
   * Keep the commented checks below as reminders if call paths change.
   */
  /* g_return_val_if_fail(game != NULL, 0); */
  /* g_return_val_if_fail(ctx != NULL, 0); */
  /* g_return_val_if_fail(cancelled != NULL, 0); */

  checkers_ai_search_count_node(ctx);

  if (ctx->should_cancel != NULL && ctx->should_cancel(ctx->cancel_user_data)) {
    *cancelled = TRUE;
    return 0;
  }

  guint64 key = checkers_ai_zobrist_key(game);
  gint original_alpha = alpha;
  gint original_beta = beta;

  CheckersAiTtEntry tt_entry = {0};
  gboolean has_tt_entry = FALSE;
  gboolean tt_cutoff = FALSE;
  gint tt_cutoff_score = 0;
  if (!checkers_ai_tt_probe(ctx,
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
    if (ctx->stats != NULL) {
      ctx->stats->tt_cutoffs++;
    }
    return tt_cutoff_score;
  }

  if (depth_remaining == 0 || game->state.winner != CHECKERS_WINNER_NONE) {
    gint score = checkers_ai_terminal_score(game, ply_depth);
    checkers_ai_tt_store_result(ctx, key, depth_remaining, score, CHECKERS_AI_TT_BOUND_EXACT, NULL);
    return score;
  }

  MoveList moves = game->available_moves(game);
  if (moves.count == 0) {
    CheckersWinner winner = game->state.turn == CHECKERS_COLOR_WHITE ? CHECKERS_WINNER_BLACK
                                                                     : CHECKERS_WINNER_WHITE;
    game->state.winner = winner;
    gint score = checkers_ai_terminal_score(game, ply_depth);
    game->state.winner = CHECKERS_WINNER_NONE;
    movelist_free(&moves);
    checkers_ai_tt_store_result(ctx, key, depth_remaining, score, CHECKERS_AI_TT_BOUND_EXACT, NULL);
    return score;
  }

  if (has_tt_entry && tt_entry.best_move.length >= 2) {
    for (size_t i = 0; i < moves.count; ++i) {
      if (checkers_ai_moves_match(&moves.moves[i], &tt_entry.best_move)) {
        if (i != 0) {
          CheckersMove swap = moves.moves[0];
          moves.moves[0] = moves.moves[i];
          moves.moves[i] = swap;
        }
        break;
      }
    }
  }

  gboolean maximizing = game->state.turn == CHECKERS_COLOR_WHITE;
  gint best = maximizing ? INT_MIN : INT_MAX;
  CheckersMove best_move = {0};
  gboolean have_best_move = FALSE;

  for (size_t i = 0; i < moves.count; ++i) {
    if (ctx->should_cancel != NULL && ctx->should_cancel(ctx->cancel_user_data)) {
      *cancelled = TRUE;
      break;
    }

    Game child = *game;
    if (game_apply_move(&child, &moves.moves[i]) != 0) {
      g_debug("Skipping invalid move while searching");
      continue;
    }

    gint score = checkers_ai_alpha_beta_search(&child,
                                               depth_remaining - 1,
                                               ply_depth + 1,
                                               alpha,
                                               beta,
                                               ctx,
                                               cancelled);
    if (*cancelled) {
      break;
    }

    if (maximizing) {
      if (score > best) {
        best = score;
        best_move = moves.moves[i];
        have_best_move = TRUE;
      }
      if (best > alpha) {
        alpha = best;
      }
    } else {
      if (score < best) {
        best = score;
        best_move = moves.moves[i];
        have_best_move = TRUE;
      }
      if (best < beta) {
        beta = best;
      }
    }

    if (beta <= alpha) {
      break;
    }
  }

  movelist_free(&moves);
  if (*cancelled) {
    return 0;
  }

  if (best == INT_MIN || best == INT_MAX) {
    best = checkers_ai_terminal_score(game, ply_depth);
    have_best_move = FALSE;
  }

  CheckersAiTtBound bound = CHECKERS_AI_TT_BOUND_EXACT;
  if (best <= original_alpha) {
    bound = CHECKERS_AI_TT_BOUND_UPPER;
  } else if (best >= original_beta) {
    bound = CHECKERS_AI_TT_BOUND_LOWER;
  }
  checkers_ai_tt_store_result(ctx, key, depth_remaining, best, bound, have_best_move ? &best_move : NULL);

  return best;
}

static int checkers_ai_scored_move_compare_desc(const void *left, const void *right) {
  const CheckersScoredMove *a = left;
  const CheckersScoredMove *b = right;
  if (a->score < b->score) {
    return 1;
  }
  if (a->score > b->score) {
    return -1;
  }
  return 0;
}

static int checkers_ai_scored_move_compare_asc(const void *left, const void *right) {
  const CheckersScoredMove *a = left;
  const CheckersScoredMove *b = right;
  if (a->score < b->score) {
    return -1;
  }
  if (a->score > b->score) {
    return 1;
  }
  return 0;
}

void checkers_ai_search_stats_clear(CheckersAiSearchStats *stats) {
  g_return_if_fail(stats != NULL);

  memset(stats, 0, sizeof(*stats));
}

void checkers_ai_search_stats_add(CheckersAiSearchStats *dest, const CheckersAiSearchStats *src) {
  g_return_if_fail(dest != NULL);
  g_return_if_fail(src != NULL);

  dest->nodes += src->nodes;
  dest->tt_probes += src->tt_probes;
  dest->tt_hits += src->tt_hits;
  dest->tt_cutoffs += src->tt_cutoffs;
}

void checkers_scored_move_list_free(CheckersScoredMoveList *list) {
  g_return_if_fail(list != NULL);

  g_free(list->moves);
  list->moves = NULL;
  list->count = 0;
}

gboolean checkers_ai_alpha_beta_analyze_moves_cancellable(const Game *game,
                                                          guint max_depth,
                                                          CheckersScoredMoveList *out_moves,
                                                          CheckersAiCancelFunc should_cancel,
                                                          gpointer user_data) {
  CheckersAiSearchStats stats = {0};
  checkers_ai_search_stats_clear(&stats);
  return checkers_ai_alpha_beta_analyze_moves_cancellable_with_tt(game,
                                                                   max_depth,
                                                                   out_moves,
                                                                   should_cancel,
                                                                   user_data,
                                                                   NULL,
                                                                   NULL,
                                                                   NULL,
                                                                   &stats);
}

gboolean checkers_ai_alpha_beta_analyze_moves_cancellable_with_tt(const Game *game,
                                                                  guint max_depth,
                                                                  CheckersScoredMoveList *out_moves,
                                                                  CheckersAiCancelFunc should_cancel,
                                                                  gpointer user_data,
                                                                  CheckersAiProgressFunc on_progress,
                                                                  gpointer progress_user_data,
                                                                  CheckersAiTranspositionTable *tt,
                                                                  CheckersAiSearchStats *out_stats) {
  g_return_val_if_fail(game != NULL, FALSE);
  g_return_val_if_fail(game->available_moves != NULL, FALSE);
  g_return_val_if_fail(max_depth > 0, FALSE);
  g_return_val_if_fail(out_moves != NULL, FALSE);
  g_return_val_if_fail(out_stats != NULL, FALSE);

  out_moves->moves = NULL;
  out_moves->count = 0;

  MoveList moves = game->available_moves(game);
  if (moves.count == 0) {
    movelist_free(&moves);
    g_debug("No available moves for alpha-beta analysis");
    return FALSE;
  }

  if (tt != NULL) {
    checkers_ai_tt_new_generation(tt);
  }

  CheckersScoredMove *scored_moves = g_new0(CheckersScoredMove, moves.count);
  size_t write = 0;
  gboolean cancelled = FALSE;

  CheckersAiSearchContext ctx = {
      .should_cancel = should_cancel,
      .cancel_user_data = user_data,
      .on_progress = on_progress,
      .progress_user_data = progress_user_data,
      .stats = out_stats,
      .tt = tt,
  };

  for (size_t i = 0; i < moves.count; ++i) {
    if (should_cancel != NULL && should_cancel(user_data)) {
      cancelled = TRUE;
      break;
    }

    Game child = *game;
    if (game_apply_move(&child, &moves.moves[i]) != 0) {
      g_debug("Skipping invalid root move while analyzing");
      continue;
    }

    gint score = checkers_ai_alpha_beta_search(&child,
                                               max_depth - 1,
                                               1,
                                               INT_MIN,
                                               INT_MAX,
                                               &ctx,
                                               &cancelled);
    if (cancelled) {
      break;
    }

    scored_moves[write].move = moves.moves[i];
    scored_moves[write].score = score;
    write++;
  }

  movelist_free(&moves);
  if (cancelled) {
    g_free(scored_moves);
    return FALSE;
  }

  if (write == 0) {
    g_free(scored_moves);
    g_debug("Alpha-beta analysis found no valid root moves");
    return FALSE;
  }

  if (game->state.turn == CHECKERS_COLOR_WHITE) {
    qsort(scored_moves, write, sizeof(scored_moves[0]), checkers_ai_scored_move_compare_desc);
  } else {
    qsort(scored_moves, write, sizeof(scored_moves[0]), checkers_ai_scored_move_compare_asc);
  }
  out_moves->moves = scored_moves;
  out_moves->count = write;
  return TRUE;
}

gboolean checkers_ai_alpha_beta_analyze_moves(const Game *game, guint max_depth, CheckersScoredMoveList *out_moves) {
  return checkers_ai_alpha_beta_analyze_moves_cancellable(game, max_depth, out_moves, NULL, NULL);
}

gboolean checkers_ai_alpha_beta_evaluate_position(const Game *game, guint max_depth, gint *out_score) {
  g_return_val_if_fail(game != NULL, FALSE);
  g_return_val_if_fail(game->available_moves != NULL, FALSE);
  g_return_val_if_fail(max_depth > 0, FALSE);
  g_return_val_if_fail(out_score != NULL, FALSE);

  Game root = *game;
  gboolean cancelled = FALSE;
  CheckersAiSearchStats stats = {0};
  checkers_ai_search_stats_clear(&stats);
  CheckersAiSearchContext ctx = {
      .should_cancel = NULL,
      .cancel_user_data = NULL,
      .on_progress = NULL,
      .progress_user_data = NULL,
      .stats = &stats,
      .tt = NULL,
  };

  gint score = checkers_ai_alpha_beta_search(&root, max_depth, 0, INT_MIN, INT_MAX, &ctx, &cancelled);
  if (cancelled) {
    g_debug("Unexpected cancellation while evaluating position");
    return FALSE;
  }

  *out_score = score;
  return TRUE;
}

gboolean checkers_ai_alpha_beta_choose_move(const Game *game, guint max_depth, CheckersMove *out_move) {
  g_return_val_if_fail(game != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);
  g_return_val_if_fail(max_depth > 0, FALSE);

  CheckersScoredMoveList scored_moves = {0};
  if (!checkers_ai_alpha_beta_analyze_moves(game, max_depth, &scored_moves)) {
    return FALSE;
  }

  gint best_score = scored_moves.moves[0].score;
  size_t best_count = 1;
  while (best_count < scored_moves.count && scored_moves.moves[best_count].score == best_score) {
    best_count++;
  }

  guint selected_index = g_random_int_range(0, (gint)best_count);
  *out_move = scored_moves.moves[selected_index].move;
  checkers_scored_move_list_free(&scored_moves);
  return TRUE;
}
