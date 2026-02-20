#include "ai_alpha_beta.h"

#include <limits.h>

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
                                          CheckersColor perspective) {
  g_return_val_if_fail(game != NULL, 0);

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
    Game child = *game;
    if (game_apply_move(&child, &moves.moves[i]) != 0) {
      g_debug("Skipping invalid move while searching");
      continue;
    }

    gint score =
        checkers_ai_alpha_beta_search(&child, depth_remaining - 1, ply_depth + 1, alpha, beta, perspective);
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
  if (best == INT_MIN || best == INT_MAX) {
    return checkers_ai_terminal_score(game, perspective, ply_depth);
  }
  return best;
}

gboolean checkers_ai_alpha_beta_choose_move(const Game *game, guint max_depth, CheckersMove *out_move) {
  g_return_val_if_fail(game != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);
  g_return_val_if_fail(max_depth > 0, FALSE);

  MoveList moves = game->available_moves(game);
  if (moves.count == 0) {
    movelist_free(&moves);
    g_debug("No available moves for alpha-beta AI");
    return FALSE;
  }

  CheckersColor perspective = game->state.turn;
  gint best_score = INT_MIN;
  gboolean found = FALSE;
  CheckersMove best_move = moves.moves[0];

  for (size_t i = 0; i < moves.count; ++i) {
    Game child = *game;
    if (game_apply_move(&child, &moves.moves[i]) != 0) {
      g_debug("Skipping invalid root move while searching");
      continue;
    }

    gint score =
        checkers_ai_alpha_beta_search(&child, max_depth - 1, 1, INT_MIN, INT_MAX, perspective);
    if (!found || score > best_score) {
      found = TRUE;
      best_score = score;
      best_move = moves.moves[i];
    }
  }

  movelist_free(&moves);
  if (!found) {
    g_debug("Alpha-beta failed to find a valid move");
    return FALSE;
  }

  *out_move = best_move;
  return TRUE;
}
