#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "checkers_constants.h"

typedef enum {
  CHECKERS_COLOR_WHITE = 0,
  CHECKERS_COLOR_BLACK = 1
} CheckersColor;

typedef enum {
  CHECKERS_PIECE_EMPTY = 0,
  CHECKERS_PIECE_WHITE_MAN,
  CHECKERS_PIECE_WHITE_KING,
  CHECKERS_PIECE_BLACK_MAN,
  CHECKERS_PIECE_BLACK_KING
} CheckersPiece;

typedef struct {
  uint8_t data[CHECKERS_MAX_BOARD_BYTES];
  uint8_t board_size;
} CheckersBoard;

void board_init(CheckersBoard *board, uint8_t board_size);
void board_reset(CheckersBoard *board, uint8_t board_size);
uint8_t board_get_raw(const CheckersBoard *board, uint8_t index);
void board_set_raw(CheckersBoard *board, uint8_t index, uint8_t value);
CheckersPiece board_get(const CheckersBoard *board, uint8_t index);
void board_set(CheckersBoard *board, uint8_t index, CheckersPiece value);

int8_t board_index_from_coord(int row, int col, uint8_t board_size);
void board_coord_from_index(uint8_t index, int *row, int *col, uint8_t board_size);
uint8_t board_playable_squares(uint8_t board_size);
size_t board_byte_count(uint8_t board_size);

CheckersColor board_piece_color(CheckersPiece piece);
bool board_is_opponent(CheckersPiece piece, CheckersColor player);

#endif
