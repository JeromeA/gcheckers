#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/games/checkers/board.h"

static void test_board_init_and_reset(void) {
  CheckersBoard board;
  board_init(&board, 8);

  assert(board.board_size == 8);
  for (size_t i = 0; i < sizeof(board.data); ++i) {
    assert(board.data[i] == 0);
  }

  board_reset(&board, 8);

  assert(board.board_size == 8);
  for (uint8_t i = 0; i < 12; ++i) {
    assert(board_get(&board, i) == CHECKERS_PIECE_BLACK_MAN);
  }
  for (uint8_t i = 12; i < 20; ++i) {
    assert(board_get(&board, i) == CHECKERS_PIECE_EMPTY);
  }
  for (uint8_t i = 20; i < 32; ++i) {
    assert(board_get(&board, i) == CHECKERS_PIECE_WHITE_MAN);
  }
}

static void test_board_get_set(void) {
  CheckersBoard board;
  board_init(&board, 8);

  board_set(&board, 0, CHECKERS_PIECE_WHITE_KING);
  board_set(&board, 1, CHECKERS_PIECE_BLACK_MAN);
  board_set(&board, 2, CHECKERS_PIECE_WHITE_MAN);

  assert(board_get(&board, 0) == CHECKERS_PIECE_WHITE_KING);
  assert(board_get(&board, 1) == CHECKERS_PIECE_BLACK_MAN);
  assert(board_get(&board, 2) == CHECKERS_PIECE_WHITE_MAN);

  board_set(&board, 1, CHECKERS_PIECE_EMPTY);
  assert(board_get(&board, 1) == CHECKERS_PIECE_EMPTY);
}

static void test_board_coord_roundtrip(void) {
  uint8_t board_size = 8;
  int row = 2;
  int col = 1;
  int8_t index = board_index_from_coord(row, col, board_size);
  assert(index >= 0);

  int out_row = 0;
  int out_col = 0;
  board_coord_from_index((uint8_t)index, &out_row, &out_col, board_size);
  assert(out_row == row);
  assert(out_col == col);
}

static void test_board_helpers(void) {
  assert(board_playable_squares(8) == 32);
  assert(board_byte_count(8) == 16);

  CheckersPiece white = CHECKERS_PIECE_WHITE_MAN;
  CheckersPiece black = CHECKERS_PIECE_BLACK_KING;
  assert(board_piece_color(white) == CHECKERS_COLOR_WHITE);
  assert(board_piece_color(black) == CHECKERS_COLOR_BLACK);
  assert(board_is_opponent(black, CHECKERS_COLOR_WHITE));
  assert(!board_is_opponent(CHECKERS_PIECE_EMPTY, CHECKERS_COLOR_BLACK));
}

static void test_board_coord_transform_for_bottom_color(void) {
  int row = 2;
  int col = 1;

  board_coord_transform_for_bottom_color(&row, &col, 8, CHECKERS_COLOR_WHITE);
  assert(row == 2);
  assert(col == 1);

  board_coord_transform_for_bottom_color(&row, &col, 8, CHECKERS_COLOR_BLACK);
  assert(row == 5);
  assert(col == 6);
}

int main(void) {
  test_board_init_and_reset();
  test_board_get_set();
  test_board_coord_roundtrip();
  test_board_helpers();
  test_board_coord_transform_for_bottom_color();

  puts("Board tests passed.");
  return 0;
}
