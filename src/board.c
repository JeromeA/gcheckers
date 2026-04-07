#include "board.h"

#include <glib.h>
#include <string.h>

static bool board_size_valid(uint8_t board_size) {
  return board_size == 8 || board_size == 10;
}

void board_init(CheckersBoard *board, uint8_t board_size) {
  g_return_if_fail(board != NULL);
  g_return_if_fail(board_size_valid(board_size));

  memset(board, 0, sizeof(*board));
  board->board_size = board_size;
}

void board_reset(CheckersBoard *board, uint8_t board_size) {
  g_return_if_fail(board != NULL);
  g_return_if_fail(board_size_valid(board_size));

  memset(board->data, 0, sizeof(board->data));
  board->board_size = board_size;

  uint8_t squares = board_playable_squares(board_size);
  uint8_t row_pieces = (uint8_t)(board_size / 2);
  uint8_t rows = (uint8_t)(row_pieces - 1);
  uint8_t total_pieces = (uint8_t)(rows * row_pieces);

  for (uint8_t i = 0; i < total_pieces; ++i) {
    board_set(board, i, CHECKERS_PIECE_BLACK_MAN);
  }
  for (uint8_t i = (uint8_t)(squares - total_pieces); i < squares; ++i) {
    board_set(board, i, CHECKERS_PIECE_WHITE_MAN);
  }
}

uint8_t board_get_raw(const CheckersBoard *board, uint8_t index) {
  g_return_val_if_fail(board != NULL, 0);

  uint8_t packed = board->data[index / 2];
  if (index % 2 == 0) {
    return packed & 0x0F;
  }
  return (packed >> 4) & 0x0F;
}

void board_set_raw(CheckersBoard *board, uint8_t index, uint8_t value) {
  g_return_if_fail(board != NULL);

  uint8_t *packed = &board->data[index / 2];
  if (index % 2 == 0) {
    *packed = (uint8_t)((*packed & 0xF0) | (value & 0x0F));
  } else {
    *packed = (uint8_t)((*packed & 0x0F) | ((value & 0x0F) << 4));
  }
}

CheckersPiece board_get(const CheckersBoard *board, uint8_t index) {
  g_return_val_if_fail(board != NULL, CHECKERS_PIECE_EMPTY);

  return (CheckersPiece)board_get_raw(board, index);
}

void board_set(CheckersBoard *board, uint8_t index, CheckersPiece value) {
  g_return_if_fail(board != NULL);

  board_set_raw(board, index, value);
}

int8_t board_index_from_coord(int row, int col, uint8_t board_size) {
  if (row < 0 || row >= board_size || col < 0 || col >= board_size) {
    return -1;
  }
  if ((row + col) % 2 == 0) {
    return -1;
  }
  int per_row = board_size / 2;
  return (int8_t)(row * per_row + col / 2);
}

void board_coord_from_index(uint8_t index, int *row, int *col, uint8_t board_size) {
  g_return_if_fail(row != NULL);
  g_return_if_fail(col != NULL);

  int per_row = board_size / 2;
  *row = index / per_row;
  int base_col = (index % per_row) * 2;
  *col = base_col + ((*row + 1) % 2);
}

void board_coord_transform_for_bottom_color(int *row, int *col, uint8_t board_size, CheckersColor bottom_color) {
  g_return_if_fail(row != NULL);
  g_return_if_fail(col != NULL);
  g_return_if_fail(board_size_valid(board_size));
  g_return_if_fail(bottom_color == CHECKERS_COLOR_WHITE || bottom_color == CHECKERS_COLOR_BLACK);

  if (bottom_color == CHECKERS_COLOR_WHITE) {
    return;
  }

  *row = (int)board_size - 1 - *row;
  *col = (int)board_size - 1 - *col;
}

uint8_t board_playable_squares(uint8_t board_size) {
  g_return_val_if_fail(board_size_valid(board_size), 0);
  return (uint8_t)((board_size / 2) * board_size);
}

size_t board_byte_count(uint8_t board_size) {
  uint8_t squares = board_playable_squares(board_size);
  return (size_t)((squares + 1) / 2);
}

CheckersColor board_piece_color(CheckersPiece piece) {
  if (piece == CHECKERS_PIECE_WHITE_MAN || piece == CHECKERS_PIECE_WHITE_KING) {
    return CHECKERS_COLOR_WHITE;
  }
  return CHECKERS_COLOR_BLACK;
}

bool board_is_opponent(CheckersPiece piece, CheckersColor player) {
  if (piece == CHECKERS_PIECE_EMPTY) {
    return false;
  }
  return board_piece_color(piece) != player;
}
