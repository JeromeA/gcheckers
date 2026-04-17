#ifndef BOARD_GEOMETRY_H
#define BOARD_GEOMETRY_H

#include <stdint.h>

#include "checkers_constants.h"

typedef enum {
  CHECKERS_DIRECTION_UP_LEFT = 0,
  CHECKERS_DIRECTION_UP_RIGHT,
  CHECKERS_DIRECTION_DOWN_LEFT,
  CHECKERS_DIRECTION_DOWN_RIGHT,
  CHECKERS_DIRECTION_COUNT
} CheckersDirection;

enum {
  CHECKERS_DIRECTION_SENTINEL = -1
};

typedef struct {
  uint8_t board_size;
  uint8_t squares;
  int8_t rays[CHECKERS_MAX_SQUARES][CHECKERS_DIRECTION_COUNT][CHECKERS_MAX_DIRECTION_STEPS + 1];
} CheckersBoardGeometry;

const CheckersBoardGeometry *checkers_board_geometry_get(uint8_t board_size);

#endif
