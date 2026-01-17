#include "game.h"

#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t board_get(const GameState *state, uint8_t index) {
  g_return_val_if_fail(state != NULL, 0);

  uint8_t packed = state->board[index / 2];
  if (index % 2 == 0) {
    return packed & 0x0F;
  }
  return (packed >> 4) & 0x0F;
}

static void board_set(GameState *state, uint8_t index, uint8_t value) {
  g_return_if_fail(state != NULL);

  uint8_t *packed = &state->board[index / 2];
  if (index % 2 == 0) {
    *packed = (uint8_t)((*packed & 0xF0) | (value & 0x0F));
  } else {
    *packed = (uint8_t)((*packed & 0x0F) | ((value & 0x0F) << 4));
  }
}

static CheckersColor piece_color(CheckersPiece piece) {
  if (piece == CHECKERS_PIECE_WHITE_MAN || piece == CHECKERS_PIECE_WHITE_KING) {
    return CHECKERS_COLOR_WHITE;
  }
  return CHECKERS_COLOR_BLACK;
}

static bool is_opponent(CheckersPiece piece, CheckersColor player) {
  if (piece == CHECKERS_PIECE_EMPTY) {
    return false;
  }
  return piece_color(piece) != player;
}

static void reset_board(GameState *state) {
  g_return_if_fail(state != NULL);

  memset(state->board, 0, sizeof(state->board));
  for (uint8_t i = 0; i < 12; ++i) {
    board_set(state, i, CHECKERS_PIECE_BLACK_MAN);
  }
  for (uint8_t i = 20; i < 32; ++i) {
    board_set(state, i, CHECKERS_PIECE_WHITE_MAN);
  }
}

static int8_t index_from_coord(int row, int col) {
  if (row < 0 || row >= 8 || col < 0 || col >= 8) {
    return -1;
  }
  if ((row + col) % 2 == 0) {
    return -1;
  }
  return (int8_t)(row * 4 + col / 2);
}

static void coord_from_index(uint8_t index, int *row, int *col) {
  if (!row || !col) {
    g_debug("coord_from_index received null pointers\n");
    g_return_if_fail(row != NULL);
    g_return_if_fail(col != NULL);
  }

  *row = index / 4;
  int base_col = (index % 4) * 2;
  *col = base_col + ((*row + 1) % 2);
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

static bool is_forward(CheckersColor color, int delta_row) {
  if (color == CHECKERS_COLOR_WHITE) {
    return delta_row == -1;
  }
  return delta_row == 1;
}

static void generate_simple_moves(const Game *game, uint8_t index, MoveList *moves) {
  if (!game || !moves) {
    g_debug("generate_simple_moves received invalid arguments\n");
    g_return_if_fail(game != NULL);
    g_return_if_fail(moves != NULL);
  }

  CheckersPiece piece = (CheckersPiece)board_get(&game->state, index);
  if (piece == CHECKERS_PIECE_EMPTY) {
    return;
  }

  int row = 0;
  int col = 0;
  coord_from_index(index, &row, &col);

  int directions[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  for (size_t i = 0; i < 4; ++i) {
    int dr = directions[i][0];
    int dc = directions[i][1];
    if (piece == CHECKERS_PIECE_WHITE_MAN || piece == CHECKERS_PIECE_BLACK_MAN) {
      if (!is_forward(piece_color(piece), dr)) {
        continue;
      }
    }
    int nr = row + dr;
    int nc = col + dc;
    int8_t target_index = index_from_coord(nr, nc);
    if (target_index < 0) {
      continue;
    }
    if (board_get(&game->state, (uint8_t)target_index) == CHECKERS_PIECE_EMPTY) {
      CheckersMove move = {.length = 2};
      move.path[0] = index;
      move.path[1] = (uint8_t)target_index;
      append_move(moves, &move);
    }
  }
}

static void dfs_jumps(const Game *game, uint8_t index, CheckersMove *partial, MoveList *moves,
                      bool *visited) {
  if (!game || !partial || !moves || !visited) {
    g_debug("dfs_jumps received invalid arguments\n");
    g_return_if_fail(game != NULL);
    g_return_if_fail(partial != NULL);
    g_return_if_fail(moves != NULL);
    g_return_if_fail(visited != NULL);
  }

  bool extended = false;
  CheckersPiece piece = (CheckersPiece)board_get(&game->state, index);
  int row = 0;
  int col = 0;
  coord_from_index(index, &row, &col);
  int directions[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  for (size_t i = 0; i < 4; ++i) {
    int dr = directions[i][0];
    int dc = directions[i][1];
    if (piece == CHECKERS_PIECE_WHITE_MAN || piece == CHECKERS_PIECE_BLACK_MAN) {
      if (!is_forward(piece_color(piece), dr)) {
        continue;
      }
    }
    int mid_r = row + dr;
    int mid_c = col + dc;
    int land_r = row + dr * 2;
    int land_c = col + dc * 2;
    int8_t mid_index = index_from_coord(mid_r, mid_c);
    int8_t land_index = index_from_coord(land_r, land_c);
    if (mid_index < 0 || land_index < 0) {
      continue;
    }
    CheckersPiece middle_piece = (CheckersPiece)board_get(&game->state, (uint8_t)mid_index);
    if (!is_opponent(middle_piece, piece_color(piece))) {
      continue;
    }
    if (board_get(&game->state, (uint8_t)land_index) != CHECKERS_PIECE_EMPTY) {
      continue;
    }
    if (visited[land_index]) {
      continue;
    }
    extended = true;
    visited[land_index] = true;
    partial->path[partial->length++] = (uint8_t)land_index;
    dfs_jumps(game, (uint8_t)land_index, partial, moves, visited);
    partial->length -= 1;
    visited[land_index] = false;
  }

  if (!extended && partial->length > 1) {
    append_move(moves, partial);
  }
}

static void generate_jump_moves(const Game *game, uint8_t index, MoveList *moves) {
  if (!game || !moves) {
    g_debug("generate_jump_moves received invalid arguments\n");
    g_return_if_fail(game != NULL);
    g_return_if_fail(moves != NULL);
  }

  CheckersPiece piece = (CheckersPiece)board_get(&game->state, index);
  if (piece == CHECKERS_PIECE_EMPTY) {
    return;
  }
  bool visited[32] = {false};
  CheckersMove move = {.length = 1};
  move.path[0] = index;
  visited[index] = true;
  dfs_jumps(game, index, &move, moves, visited);
}

static bool find_jump_available(const MoveList *list) {
  if (!list) {
    g_debug("find_jump_available received null list\n");
    g_return_val_if_fail(list != NULL, false);
  }

  for (size_t i = 0; i < list->count; ++i) {
    if (list->moves[i].length >= 3) {
      return true;
    }
  }
  return false;
}

static void filter_forced_captures(MoveList *moves) {
  if (!moves) {
    g_debug("filter_forced_captures received null moves\n");
    g_return_if_fail(moves != NULL);
  }

  bool has_jump = find_jump_available(moves);
  if (!has_jump) {
    return;
  }
  size_t write = 0;
  for (size_t i = 0; i < moves->count; ++i) {
    if (moves->moves[i].length >= 3) {
      moves->moves[write++] = moves->moves[i];
    }
  }
  moves->count = write;
}

static void ensure_capacity(Game *game) {
  if (!game) {
    g_debug("ensure_capacity received null game\n");
    g_return_if_fail(game != NULL);
  }

  if (game->history_size < game->history_capacity) {
    return;
  }
  size_t new_capacity = game->history_capacity == 0 ? 16 : game->history_capacity * 2;
  CheckersMove *expanded = realloc(game->history, sizeof(CheckersMove) * new_capacity);
  if (!expanded) {
    g_debug("Failed to expand history buffer\n");
    return;
  }
  game->history = expanded;
  game->history_capacity = new_capacity;
}

void game_init(Game *game) {
  if (!game) {
    g_debug("game_init received null game\n");
    g_return_if_fail(game != NULL);
  }

  memset(game, 0, sizeof(*game));
  reset_board(&game->state);
  game->state.turn = CHECKERS_COLOR_WHITE;
  game->state.winner = CHECKERS_WINNER_NONE;
  game->print_state = game_print_state;
  game->available_moves = game_list_available_moves;
}

void game_destroy(Game *game) {
  if (!game) {
    g_debug("game_destroy received null game\n");
    g_return_if_fail(game != NULL);
  }

  free(game->history);
  game->history = NULL;
  game->history_size = 0;
  game->history_capacity = 0;
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
  for (uint8_t i = 0; i < 32; ++i) {
    CheckersPiece piece = (CheckersPiece)board_get(&game->state, i);
    if (piece == CHECKERS_PIECE_EMPTY) {
      continue;
    }
    if (piece_color(piece) != game->state.turn) {
      continue;
    }
    generate_jump_moves(game, i, &moves);
  }
  if (moves.count == 0) {
    for (uint8_t i = 0; i < 32; ++i) {
      CheckersPiece piece = (CheckersPiece)board_get(&game->state, i);
      if (piece == CHECKERS_PIECE_EMPTY) {
        continue;
      }
      if (piece_color(piece) != game->state.turn) {
        continue;
      }
      generate_simple_moves(game, i, &moves);
    }
  }
  filter_forced_captures(&moves);
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

static void print_board_piece(FILE *out, CheckersPiece piece) {
  if (!out) {
    g_debug("print_board_piece received null output\n");
    g_return_if_fail(out != NULL);
  }

  char symbol = '.';
  switch (piece) {
    case CHECKERS_PIECE_WHITE_MAN:
      symbol = 'w';
      break;
    case CHECKERS_PIECE_WHITE_KING:
      symbol = 'W';
      break;
    case CHECKERS_PIECE_BLACK_MAN:
      symbol = 'b';
      break;
    case CHECKERS_PIECE_BLACK_KING:
      symbol = 'B';
      break;
    default:
      symbol = '.';
  }
  fputc(symbol, out);
}

void game_print_state(const Game *game, FILE *out) {
  if (!game) {
    g_debug("game_print_state received null game\n");
    g_return_if_fail(game != NULL);
  }
  if (!out) {
    out = stdout;
  }

  fprintf(out, "Turn: %s\n", game->state.turn == CHECKERS_COLOR_WHITE ? "White" : "Black");
  fprintf(out, "Winner: ");
  switch (game->state.winner) {
    case CHECKERS_WINNER_WHITE:
      fprintf(out, "White\n");
      break;
    case CHECKERS_WINNER_BLACK:
      fprintf(out, "Black\n");
      break;
    case CHECKERS_WINNER_DRAW:
      fprintf(out, "Draw\n");
      break;
    default:
      fprintf(out, "None\n");
  }

  for (int row = 0; row < 8; ++row) {
    for (int col = 0; col < 8; ++col) {
      if ((row + col) % 2 == 0) {
        fputc(' ', out);
        continue;
      }
      int8_t idx = index_from_coord(row, col);
      if (idx < 0) {
        fputc(' ', out);
        continue;
      }
      CheckersPiece piece = (CheckersPiece)board_get(&game->state, (uint8_t)idx);
      print_board_piece(out, piece);
      fputc(' ', out);
    }
    fputc('\n', out);
  }
}

static bool promote_needed(CheckersPiece piece, int row) {
  if (piece == CHECKERS_PIECE_WHITE_MAN && row == 0) {
    return true;
  }
  if (piece == CHECKERS_PIECE_BLACK_MAN && row == 7) {
    return true;
  }
  return false;
}

static void remove_captured(GameState *state, const CheckersMove *move) {
  if (!state || !move) {
    g_debug("remove_captured received invalid arguments\n");
    g_return_if_fail(state != NULL);
    g_return_if_fail(move != NULL);
  }

  for (uint8_t i = 1; i < move->length; ++i) {
    uint8_t from = move->path[i - 1];
    uint8_t to = move->path[i];
    int from_row = 0;
    int from_col = 0;
    int to_row = 0;
    int to_col = 0;
    coord_from_index(from, &from_row, &from_col);
    coord_from_index(to, &to_row, &to_col);
    if (abs(from_row - to_row) == 2) {
      int mid_row = (from_row + to_row) / 2;
      int mid_col = (from_col + to_col) / 2;
      int8_t mid_index = index_from_coord(mid_row, mid_col);
      if (mid_index >= 0) {
        board_set(state, (uint8_t)mid_index, CHECKERS_PIECE_EMPTY);
      }
    }
  }
}

static void update_winner(Game *game) {
  if (!game) {
    g_debug("update_winner received null game\n");
    g_return_if_fail(game != NULL);
  }

  MoveList moves = game_list_available_moves(game);
  if (moves.count == 0) {
    game->state.winner =
        game->state.turn == CHECKERS_COLOR_WHITE ? CHECKERS_WINNER_BLACK : CHECKERS_WINNER_WHITE;
  }
  movelist_free(&moves);
}

int game_apply_move(Game *game, const CheckersMove *move) {
  if (!game || !move || move->length < 2) {
    g_debug("game_apply_move received invalid arguments\n");
    g_return_val_if_fail(game != NULL, -1);
    g_return_val_if_fail(move != NULL, -1);
    g_return_val_if_fail(move->length >= 2, -1);
  }
  if (game->state.winner != CHECKERS_WINNER_NONE) {
    g_debug("game_apply_move called after game ended\n");
    return -1;
  }
  CheckersPiece piece = (CheckersPiece)board_get(&game->state, move->path[0]);
  if (piece == CHECKERS_PIECE_EMPTY || piece_color(piece) != game->state.turn) {
    g_debug("game_apply_move called with wrong player piece\n");
    return -1;
  }

  GameState *state = &game->state;
  board_set(state, move->path[0], CHECKERS_PIECE_EMPTY);
  uint8_t destination = move->path[move->length - 1];
  int dest_row = 0;
  int dest_col = 0;
  coord_from_index(destination, &dest_row, &dest_col);
  if (promote_needed(piece, dest_row)) {
    piece = piece_color(piece) == CHECKERS_COLOR_WHITE ? CHECKERS_PIECE_WHITE_KING
                                                       : CHECKERS_PIECE_BLACK_KING;
  }
  board_set(state, destination, piece);
  remove_captured(state, move);

  ensure_capacity(game);
  if (game->history_size < game->history_capacity) {
    game->history[game->history_size++] = *move;
  }

  game->state.turn = game->state.turn == CHECKERS_COLOR_WHITE ? CHECKERS_COLOR_BLACK
                                                              : CHECKERS_COLOR_WHITE;
  update_winner(game);
  return 0;
}
