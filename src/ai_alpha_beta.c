#include "ai_alpha_beta.h"

#include <limits.h>
#include <stdlib.h>

static gint checkers_ai_material_score(const Game *game, CheckersColor perspective) {
  g_return_val_if_fail(game != NULL, 0);

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
        value = 175;
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
    score += color == perspective ? value : -value;
  }

  return score;
}

static gint checkers_ai_terminal_score(const Game *game, CheckersColor perspective, guint ply_depth) {
  g_return_val_if_fail(game != NULL, 0);

  if (game->state.winner == CHECKERS_WINNER_NONE) {
    return checkers_ai_material_score(game, perspective);
  }

  gint win_score = 100000 - (gint)ply_depth;
  switch (game->state.winner) {
    case CHECKERS_WINNER_WHITE:
      return perspective == CHECKERS_COLOR_WHITE ? win_score : -win_score;
    case CHECKERS_WINNER_BLACK:
      return perspective == CHECKERS_COLOR_BLACK ? win_score : -win_score;
    case CHECKERS_WINNER_DRAW:
      return 0;
    case CHECKERS_WINNER_NONE:
    default:
      return checkers_ai_material_score(game, perspective);
  }
}

static gint checkers_ai_alpha_beta_search(Game *game,
                                          guint depth_remaining,
                                          guint ply_depth,
                                          gint alpha,
                                          gint beta,
                                          CheckersColor perspective,
                                          CheckersAiCancelFunc should_cancel,
                                          gpointer user_data,
                                          gboolean *cancelled) {
  g_return_val_if_fail(game != NULL, 0);
  g_return_val_if_fail(cancelled != NULL, 0);

  if (should_cancel && should_cancel(user_data)) {
    *cancelled = TRUE;
    return 0;
  }

  if (depth_remaining == 0 || game->state.winner != CHECKERS_WINNER_NONE) {
    return checkers_ai_terminal_score(game, perspective, ply_depth);
  }

  MoveList moves = game->available_moves(game);
  if (moves.count == 0) {
    CheckersWinner winner = game->state.turn == CHECKERS_COLOR_WHITE ? CHECKERS_WINNER_BLACK
                                                                     : CHECKERS_WINNER_WHITE;
    game->state.winner = winner;
    gint score = checkers_ai_terminal_score(game, perspective, ply_depth);
    game->state.winner = CHECKERS_WINNER_NONE;
    movelist_free(&moves);
    return score;
  }

  gboolean maximizing = game->state.turn == perspective;
  gint best = maximizing ? INT_MIN : INT_MAX;

  for (size_t i = 0; i < moves.count; ++i) {
    if (should_cancel && should_cancel(user_data)) {
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
                                               perspective,
                                               should_cancel,
                                               user_data,
                                               cancelled);
    if (*cancelled) {
      break;
    }

    if (maximizing) {
      if (score > best) {
        best = score;
      }
      if (best > alpha) {
        alpha = best;
      }
    } else {
      if (score < best) {
        best = score;
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
    return checkers_ai_terminal_score(game, perspective, ply_depth);
  }
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
  g_return_val_if_fail(game != NULL, FALSE);
  g_return_val_if_fail(max_depth > 0, FALSE);
  g_return_val_if_fail(out_moves != NULL, FALSE);

  out_moves->moves = NULL;
  out_moves->count = 0;

  MoveList moves = game->available_moves(game);
  if (moves.count == 0) {
    movelist_free(&moves);
    g_debug("No available moves for alpha-beta analysis");
    return FALSE;
  }

  CheckersScoredMove *scored_moves = g_new0(CheckersScoredMove, moves.count);
  size_t write = 0;
  CheckersColor perspective = game->state.turn;
  gboolean cancelled = FALSE;

  for (size_t i = 0; i < moves.count; ++i) {
    if (should_cancel && should_cancel(user_data)) {
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
                                               perspective,
                                               should_cancel,
                                               user_data,
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

  qsort(scored_moves, write, sizeof(scored_moves[0]), checkers_ai_scored_move_compare_desc);
  out_moves->moves = scored_moves;
  out_moves->count = write;
  return TRUE;
}

gboolean checkers_ai_alpha_beta_analyze_moves(const Game *game, guint max_depth, CheckersScoredMoveList *out_moves) {
  return checkers_ai_alpha_beta_analyze_moves_cancellable(game, max_depth, out_moves, NULL, NULL);
}

gboolean checkers_ai_alpha_beta_evaluate_position(const Game *game, guint max_depth, gint *out_score) {
  g_return_val_if_fail(game != NULL, FALSE);
  g_return_val_if_fail(max_depth > 0, FALSE);
  g_return_val_if_fail(out_score != NULL, FALSE);

  Game root = *game;
  gboolean cancelled = FALSE;
  gint score = checkers_ai_alpha_beta_search(&root,
                                             max_depth,
                                             0,
                                             INT_MIN,
                                             INT_MAX,
                                             game->state.turn,
                                             NULL,
                                             NULL,
                                             &cancelled);
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
