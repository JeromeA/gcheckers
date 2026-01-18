#include "game.h"

#include <glib.h>
#include <stdbool.h>
#include <stdlib.h>

static bool is_forward(CheckersColor color, int delta_row) {
  if (color == CHECKERS_COLOR_WHITE) {
    return delta_row == -1;
  }
  return delta_row == 1;
}

static void append_move(MoveList *list, const CheckersMove *move) {
  if (!list || !move) {
    g_debug("append_move received invalid arguments\n");
    g_return_if_fail(list != NULL);
    g_return_if_fail(move != NULL);
  }

  CheckersMove *expanded = realloc(list->moves, sizeof(CheckersMove) * (list->count + 1));
  if (!expanded) {
    g_debug("Failed to expand move list\n");
    return;
  }
  list->moves = expanded;
  list->moves[list->count] = *move;
  list->count += 1;
}

static void generate_simple_moves(const Game *game, uint8_t index, MoveList *moves) {
  if (!game || !moves) {
    g_debug("generate_simple_moves received invalid arguments\n");
    g_return_if_fail(game != NULL);
    g_return_if_fail(moves != NULL);
  }

  CheckersPiece piece = board_get(&game->state.board, index);
  if (piece == CHECKERS_PIECE_EMPTY) {
    return;
  }

  int row = 0;
  int col = 0;
  board_coord_from_index(index, &row, &col, game->rules.board_size);

  int directions[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  bool is_king = piece == CHECKERS_PIECE_WHITE_KING || piece == CHECKERS_PIECE_BLACK_KING;

  for (size_t i = 0; i < 4; ++i) {
    int dr = directions[i][0];
    int dc = directions[i][1];
    if (!is_king && !is_forward(board_piece_color(piece), dr)) {
      continue;
    }

    int step = 1;
    for (;;) {
      int nr = row + dr * step;
      int nc = col + dc * step;
      int8_t target_index = board_index_from_coord(nr, nc, game->rules.board_size);
      if (target_index < 0) {
        break;
      }
      if (board_get(&game->state.board, (uint8_t)target_index) != CHECKERS_PIECE_EMPTY) {
        break;
      }

      CheckersMove move = {.length = 2, .captures = 0};
      move.path[0] = index;
      move.path[1] = (uint8_t)target_index;
      append_move(moves, &move);

      if (!is_king || !game->rules.kings_can_fly) {
        break;
      }
      step += 1;
    }
  }
}

static void dfs_jumps(const Game *game,
                      uint8_t index,
                      CheckersPiece piece,
                      CheckersMove *partial,
                      MoveList *moves,
                      CheckersBoard *board) {
  if (!game || !partial || !moves || !board) {
    g_debug("dfs_jumps received invalid arguments\n");
    g_return_if_fail(game != NULL);
    g_return_if_fail(partial != NULL);
    g_return_if_fail(moves != NULL);
    g_return_if_fail(board != NULL);
  }

  bool extended = false;
  int row = 0;
  int col = 0;
  board_coord_from_index(index, &row, &col, game->rules.board_size);
  int directions[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  bool is_king = piece == CHECKERS_PIECE_WHITE_KING || piece == CHECKERS_PIECE_BLACK_KING;

  for (size_t i = 0; i < 4; ++i) {
    int dr = directions[i][0];
    int dc = directions[i][1];
    if (!is_king && !game->rules.men_can_jump_backwards &&
        !is_forward(board_piece_color(piece), dr)) {
      continue;
    }

    if (!is_king || !game->rules.kings_can_fly) {
      int mid_r = row + dr;
      int mid_c = col + dc;
      int land_r = row + dr * 2;
      int land_c = col + dc * 2;
      int8_t mid_index = board_index_from_coord(mid_r, mid_c, game->rules.board_size);
      int8_t land_index = board_index_from_coord(land_r, land_c, game->rules.board_size);
      if (mid_index < 0 || land_index < 0) {
        continue;
      }
      CheckersPiece middle_piece = (CheckersPiece)board_get_raw(board, (uint8_t)mid_index);
      if (!board_is_opponent(middle_piece, board_piece_color(piece))) {
        continue;
      }
      if (board_get_raw(board, (uint8_t)land_index) != CHECKERS_PIECE_EMPTY) {
        continue;
      }
      if (partial->length + 1 >= CHECKERS_MAX_MOVE_LENGTH) {
        g_debug("dfs_jumps reached maximum move length\n");
        continue;
      }

      extended = true;
      CheckersBoard next_board = *board;
      board_set_raw(&next_board, index, CHECKERS_PIECE_EMPTY);
      board_set_raw(&next_board, (uint8_t)mid_index, CHECKERS_PIECE_EMPTY);
      board_set_raw(&next_board, (uint8_t)land_index, piece);

      partial->path[partial->length++] = (uint8_t)land_index;
      partial->captures += 1;
      dfs_jumps(game, (uint8_t)land_index, piece, partial, moves, &next_board);
      partial->length -= 1;
      partial->captures -= 1;
      continue;
    }

    bool found_opponent = false;
    uint8_t opponent_index = 0;
    for (int step = 1;; ++step) {
      int scan_r = row + dr * step;
      int scan_c = col + dc * step;
      int8_t scan_index = board_index_from_coord(scan_r, scan_c, game->rules.board_size);
      if (scan_index < 0) {
        break;
      }
      CheckersPiece scan_piece = (CheckersPiece)board_get_raw(board, (uint8_t)scan_index);
      if (scan_piece == CHECKERS_PIECE_EMPTY) {
        if (!found_opponent) {
          continue;
        }
        if (partial->length + 1 >= CHECKERS_MAX_MOVE_LENGTH) {
          g_debug("dfs_jumps reached maximum move length\n");
          continue;
        }
        extended = true;
        CheckersBoard next_board = *board;
        board_set_raw(&next_board, index, CHECKERS_PIECE_EMPTY);
        board_set_raw(&next_board, opponent_index, CHECKERS_PIECE_EMPTY);
        board_set_raw(&next_board, (uint8_t)scan_index, piece);

        partial->path[partial->length++] = (uint8_t)scan_index;
        partial->captures += 1;
        dfs_jumps(game, (uint8_t)scan_index, piece, partial, moves, &next_board);
        partial->length -= 1;
        partial->captures -= 1;
        continue;
      }

      if (board_is_opponent(scan_piece, board_piece_color(piece)) && !found_opponent) {
        found_opponent = true;
        opponent_index = (uint8_t)scan_index;
        continue;
      }
      break;
    }
  }

  if (!extended && partial->captures > 0) {
    append_move(moves, partial);
  }
}

static void generate_jump_moves(const Game *game, uint8_t index, MoveList *moves) {
  if (!game || !moves) {
    g_debug("generate_jump_moves received invalid arguments\n");
    g_return_if_fail(game != NULL);
    g_return_if_fail(moves != NULL);
  }

  CheckersPiece piece = board_get(&game->state.board, index);
  if (piece == CHECKERS_PIECE_EMPTY) {
    return;
  }

  CheckersMove move = {.length = 1, .captures = 0};
  move.path[0] = index;
  CheckersBoard board_copy = game->state.board;
  dfs_jumps(game, index, piece, &move, moves, &board_copy);
}

static void filter_longest_captures(MoveList *moves) {
  if (!moves) {
    g_debug("filter_longest_captures received null moves\n");
    g_return_if_fail(moves != NULL);
  }

  uint8_t max_captures = 0;
  for (size_t i = 0; i < moves->count; ++i) {
    if (moves->moves[i].captures > max_captures) {
      max_captures = moves->moves[i].captures;
    }
  }
  if (max_captures == 0) {
    return;
  }

  size_t write = 0;
  for (size_t i = 0; i < moves->count; ++i) {
    if (moves->moves[i].captures == max_captures) {
      moves->moves[write++] = moves->moves[i];
    }
  }
  moves->count = write;
}

MoveList game_list_available_moves(const Game *game) {
  MoveList moves = {.moves = NULL, .count = 0};
  if (!game) {
    g_debug("game_list_available_moves received null game\n");
    g_return_val_if_fail(game != NULL, moves);
  }
  if (game->state.winner != CHECKERS_WINNER_NONE) {
    return moves;
  }

  uint8_t squares = board_playable_squares(game->rules.board_size);
  for (uint8_t i = 0; i < squares; ++i) {
    CheckersPiece piece = board_get(&game->state.board, i);
    if (piece == CHECKERS_PIECE_EMPTY) {
      continue;
    }
    if (board_piece_color(piece) != game->state.turn) {
      continue;
    }
    generate_jump_moves(game, i, &moves);
  }

  if (moves.count > 0 && game->rules.capture_mandatory) {
    if (game->rules.longest_capture_mandatory) {
      filter_longest_captures(&moves);
    }
    return moves;
  }

  for (uint8_t i = 0; i < squares; ++i) {
    CheckersPiece piece = board_get(&game->state.board, i);
    if (piece == CHECKERS_PIECE_EMPTY) {
      continue;
    }
    if (board_piece_color(piece) != game->state.turn) {
      continue;
    }
    generate_simple_moves(game, i, &moves);
  }

  return moves;
}

void movelist_free(MoveList *list) {
  if (!list) {
    g_debug("movelist_free received null list\n");
    g_return_if_fail(list != NULL);
  }

  free(list->moves);
  list->moves = NULL;
  list->count = 0;
}
