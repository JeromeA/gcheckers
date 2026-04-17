#include "game.h"
#include "board_geometry.h"

#include <glib.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static void forward_direction_range(CheckersColor color, uint8_t *start, uint8_t *end) {
  g_return_if_fail(start != NULL);
  g_return_if_fail(end != NULL);

  if (color == CHECKERS_COLOR_WHITE) {
    *start = CHECKERS_DIRECTION_UP_LEFT;
    *end = CHECKERS_DIRECTION_DOWN_LEFT;
    return;
  }

  *start = CHECKERS_DIRECTION_DOWN_LEFT;
  *end = CHECKERS_DIRECTION_COUNT;
}

static void append_move(MoveList *list, const CheckersMove *move) {
  g_return_if_fail(list != NULL);
  g_return_if_fail(move != NULL);

  CheckersMove *expanded = realloc(list->moves, sizeof(CheckersMove) * (list->count + 1));
  if (!expanded) {
    g_debug("Failed to expand move list\n");
    return;
  }
  list->moves = expanded;
  list->moves[list->count] = *move;
  list->count += 1;
}

static void generate_simple_moves(const Game *game,
                                  const CheckersBoardGeometry *geometry,
                                  uint8_t index,
                                  MoveList *moves) {
  g_return_if_fail(game != NULL);
  g_return_if_fail(geometry != NULL);
  g_return_if_fail(moves != NULL);

  CheckersPiece piece = board_get(&game->state.board, index);
  if (piece == CHECKERS_PIECE_EMPTY) {
    return;
  }

  bool is_king = piece == CHECKERS_PIECE_WHITE_KING || piece == CHECKERS_PIECE_BLACK_KING;
  uint8_t start = CHECKERS_DIRECTION_UP_LEFT;
  uint8_t end = CHECKERS_DIRECTION_COUNT;
  if (!is_king) {
    forward_direction_range(board_piece_color(piece), &start, &end);
  }

  for (uint8_t direction = start; direction < end; ++direction) {
    const int8_t *ray = geometry->rays[index][direction];
    for (uint8_t step = 0; step < CHECKERS_MAX_DIRECTION_STEPS + 1; ++step) {
      int8_t target_index = ray[step];
      if (target_index == CHECKERS_DIRECTION_SENTINEL) {
        break;
      }
      if (board_get(&game->state.board, (uint8_t)target_index) != CHECKERS_PIECE_EMPTY) {
        break;
      }

      CheckersMove move = {.length = 2, .captures = 0};
      move.path[0] = index;
      move.path[1] = (uint8_t)target_index;
      append_move(moves, &move);

      if (!is_king || !game->rules->kings_can_fly) {
        break;
      }
    }
  }
}

static void dfs_jumps(const Game *game,
                      const CheckersBoardGeometry *geometry,
                      uint8_t index,
                      CheckersPiece piece,
                      CheckersMove *partial,
                      MoveList *moves,
                      CheckersBoard *board) {
  g_return_if_fail(game != NULL);
  g_return_if_fail(geometry != NULL);
  g_return_if_fail(partial != NULL);
  g_return_if_fail(moves != NULL);
  g_return_if_fail(board != NULL);

  bool extended = false;
  bool is_king = piece == CHECKERS_PIECE_WHITE_KING || piece == CHECKERS_PIECE_BLACK_KING;
  uint8_t start = CHECKERS_DIRECTION_UP_LEFT;
  uint8_t end = CHECKERS_DIRECTION_COUNT;
  if (!is_king && !game->rules->men_can_jump_backwards) {
    forward_direction_range(board_piece_color(piece), &start, &end);
  }

  for (uint8_t direction = start; direction < end; ++direction) {
    const int8_t *ray = geometry->rays[index][direction];

    if (!is_king || !game->rules->kings_can_fly) {
      int8_t mid_index = ray[0];
      int8_t land_index = ray[1];
      if (mid_index == CHECKERS_DIRECTION_SENTINEL || land_index == CHECKERS_DIRECTION_SENTINEL) {
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
      dfs_jumps(game, geometry, (uint8_t)land_index, piece, partial, moves, &next_board);
      partial->length -= 1;
      partial->captures -= 1;
      continue;
    }

    bool found_opponent = false;
    uint8_t opponent_index = 0;
    for (uint8_t step = 0; step < CHECKERS_MAX_DIRECTION_STEPS + 1; ++step) {
      int8_t scan_index = ray[step];
      if (scan_index == CHECKERS_DIRECTION_SENTINEL) {
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
        dfs_jumps(game, geometry, (uint8_t)scan_index, piece, partial, moves, &next_board);
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

static void generate_jump_moves(const Game *game,
                                const CheckersBoardGeometry *geometry,
                                uint8_t index,
                                MoveList *moves) {
  g_return_if_fail(game != NULL);
  g_return_if_fail(geometry != NULL);
  g_return_if_fail(moves != NULL);

  CheckersPiece piece = board_get(&game->state.board, index);
  if (piece == CHECKERS_PIECE_EMPTY) {
    return;
  }

  CheckersMove move = {.length = 1, .captures = 0};
  move.path[0] = index;
  CheckersBoard board_copy = game->state.board;
  dfs_jumps(game, geometry, index, piece, &move, moves, &board_copy);
}

static void filter_longest_captures(MoveList *moves) {
  g_return_if_fail(moves != NULL);

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
  g_return_val_if_fail(game != NULL, moves);
  if (game->state.winner != CHECKERS_WINNER_NONE) {
    return moves;
  }

  const CheckersBoardGeometry *geometry = checkers_board_geometry_get(game->rules->board_size);
  g_return_val_if_fail(geometry != NULL, moves);

  uint8_t squares = board_playable_squares(game->rules->board_size);
  for (uint8_t i = 0; i < squares; ++i) {
    CheckersPiece piece = board_get(&game->state.board, i);
    if (piece == CHECKERS_PIECE_EMPTY) {
      continue;
    }
    if (board_piece_color(piece) != game->state.turn) {
      continue;
    }
    generate_jump_moves(game, geometry, i, &moves);
  }

  if (moves.count > 0 && game->rules->capture_mandatory) {
    if (game->rules->longest_capture_mandatory) {
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
    generate_simple_moves(game, geometry, i, &moves);
  }

  return moves;
}

void movelist_free(MoveList *list) {
  g_return_if_fail(list != NULL);

  free(list->moves);
  list->moves = NULL;
  list->count = 0;
}

static bool move_has_prefix(const CheckersMove *move, const uint8_t *path, uint8_t length) {
  g_return_val_if_fail(move != NULL, false);
  g_return_val_if_fail(path != NULL, false);

  if (move->length < length) {
    return false;
  }
  if (length == 0) {
    return true;
  }
  return memcmp(move->path, path, length * sizeof(move->path[0])) == 0;
}

void game_moves_collect_starts(const MoveList *moves, bool starts[CHECKERS_MAX_SQUARES]) {
  g_return_if_fail(moves != NULL);
  g_return_if_fail(starts != NULL);

  memset(starts, 0, sizeof(bool) * CHECKERS_MAX_SQUARES);

  for (size_t i = 0; i < moves->count; ++i) {
    const CheckersMove *move = &moves->moves[i];
    if (move->length == 0) {
      continue;
    }
    starts[move->path[0]] = true;
  }
}

void game_moves_collect_next_destinations(const MoveList *moves,
                                          const uint8_t *path,
                                          uint8_t length,
                                          bool destinations[CHECKERS_MAX_SQUARES]) {
  g_return_if_fail(moves != NULL);
  g_return_if_fail(path != NULL);
  g_return_if_fail(destinations != NULL);
  g_return_if_fail(length > 0);

  memset(destinations, 0, sizeof(bool) * CHECKERS_MAX_SQUARES);

  for (size_t i = 0; i < moves->count; ++i) {
    const CheckersMove *move = &moves->moves[i];
    if (move->length <= length) {
      continue;
    }
    if (!move_has_prefix(move, path, length)) {
      continue;
    }
    destinations[move->path[length]] = true;
  }
}
