#include "ai_alpha_beta.h"

#include "ai_search.h"
#include "games/checkers/checkers_backend.h"

#include <string.h>

static void checkers_ai_copy_stats_from_generic(CheckersAiSearchStats *dest, const GameAiSearchStats *src) {
  g_return_if_fail(dest != NULL);
  g_return_if_fail(src != NULL);

  dest->nodes = src->nodes;
  dest->tt_probes = src->tt_probes;
  dest->tt_hits = src->tt_hits;
  dest->tt_cutoffs = src->tt_cutoffs;
}

static void checkers_ai_copy_stats_to_generic(GameAiSearchStats *dest, const CheckersAiSearchStats *src) {
  g_return_if_fail(dest != NULL);
  g_return_if_fail(src != NULL);

  dest->nodes = src->nodes;
  dest->tt_probes = src->tt_probes;
  dest->tt_hits = src->tt_hits;
  dest->tt_cutoffs = src->tt_cutoffs;
}

typedef struct {
  CheckersAiCancelFunc cancel;
  gpointer user_data;
} CheckersAiCancelBridge;

static gboolean checkers_ai_cancel_trampoline(gpointer user_data) {
  CheckersAiCancelBridge *bridge = user_data;

  g_return_val_if_fail(bridge != NULL, FALSE);

  if (bridge->cancel == NULL) {
    return FALSE;
  }

  return bridge->cancel(bridge->user_data);
}

typedef struct {
  CheckersAiProgressFunc on_progress;
  gpointer user_data;
} CheckersAiProgressBridge;

static void checkers_ai_progress_trampoline(const GameAiSearchStats *stats, gpointer user_data) {
  CheckersAiProgressBridge *bridge = user_data;
  CheckersAiSearchStats wrapped = {0};

  g_return_if_fail(stats != NULL);
  g_return_if_fail(bridge != NULL);

  checkers_ai_copy_stats_from_generic(&wrapped, stats);
  bridge->on_progress(&wrapped, bridge->user_data);
}

static gboolean checkers_ai_copy_scored_moves(CheckersScoredMoveList *dest, const GameAiScoredMoveList *src) {
  g_return_val_if_fail(dest != NULL, FALSE);
  g_return_val_if_fail(src != NULL, FALSE);

  dest->moves = g_new0(CheckersScoredMove, src->count);
  dest->count = src->count;

  for (gsize i = 0; i < src->count; ++i) {
    if (src->moves[i].move == NULL) {
      g_debug("Generic search returned a scored move without move bytes");
      checkers_scored_move_list_free(dest);
      return FALSE;
    }

    memcpy(&dest->moves[i].move, src->moves[i].move, sizeof(dest->moves[i].move));
    dest->moves[i].score = src->moves[i].score;
    dest->moves[i].nodes = src->moves[i].nodes;
  }

  return TRUE;
}

static gint checkers_ai_static_material_only(const Game *game) {
  const CheckersBoard *board = &game->state.board;
  guint8 squares = board_playable_squares(board->board_size);
  gint score = 0;

  g_return_val_if_fail(game != NULL, 0);

  for (guint8 i = 0; i < squares; ++i) {
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

    score += board_piece_color(piece) == CHECKERS_COLOR_WHITE ? value : -value;
  }

  return score;
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

  g_clear_pointer(&list->moves, g_free);
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
  GameAiScoredMoveList generic_moves = {0};
  GameAiSearchStats generic_stats = {0};
  gboolean ok = FALSE;
  CheckersAiCancelBridge cancel_bridge = {
      .cancel = should_cancel,
      .user_data = user_data,
  };
  CheckersAiProgressBridge progress_bridge = {
      .on_progress = on_progress,
      .user_data = progress_user_data,
  };

  g_return_val_if_fail(game != NULL, FALSE);
  g_return_val_if_fail(out_moves != NULL, FALSE);
  g_return_val_if_fail(out_stats != NULL, FALSE);

  out_moves->moves = NULL;
  out_moves->count = 0;

  checkers_ai_copy_stats_to_generic(&generic_stats, out_stats);
  ok = game_ai_search_analyze_moves_cancellable_with_tt(&checkers_game_backend,
                                                        game,
                                                        max_depth,
                                                        &generic_moves,
                                                        should_cancel != NULL ? checkers_ai_cancel_trampoline : NULL,
                                                        &cancel_bridge,
                                                        on_progress != NULL ? checkers_ai_progress_trampoline : NULL,
                                                        on_progress != NULL ? &progress_bridge : NULL,
                                                        tt != NULL ? checkers_ai_tt_peek_generic(tt) : NULL,
                                                        &generic_stats);
  checkers_ai_copy_stats_from_generic(out_stats, &generic_stats);
  if (!ok) {
    return FALSE;
  }

  ok = checkers_ai_copy_scored_moves(out_moves, &generic_moves);
  game_ai_scored_move_list_free(&generic_moves);
  return ok;
}

gboolean checkers_ai_alpha_beta_analyze_moves(const Game *game, guint max_depth, CheckersScoredMoveList *out_moves) {
  return checkers_ai_alpha_beta_analyze_moves_cancellable(game, max_depth, out_moves, NULL, NULL);
}

gboolean checkers_ai_alpha_beta_evaluate_position(const Game *game, guint max_depth, gint *out_score) {
  return game_ai_search_evaluate_position(&checkers_game_backend, game, max_depth, out_score);
}

gboolean checkers_ai_evaluate_static_material(const Game *game, gint *out_score) {
  g_return_val_if_fail(game != NULL, FALSE);
  g_return_val_if_fail(out_score != NULL, FALSE);

  *out_score = checkers_ai_static_material_only(game);
  return TRUE;
}

gboolean checkers_ai_alpha_beta_choose_move(const Game *game, guint max_depth, CheckersMove *out_move) {
  return game_ai_search_choose_move(&checkers_game_backend, game, max_depth, out_move);
}
