#include "board_geometry.h"

#include <glib.h>
#include <string.h>

#include "board.h"

static const int direction_row_delta[CHECKERS_DIRECTION_COUNT] = {-1, -1, 1, 1};
static const int direction_col_delta[CHECKERS_DIRECTION_COUNT] = {-1, 1, -1, 1};

static void checkers_board_geometry_build(CheckersBoardGeometry *geometry, uint8_t board_size) {
  g_return_if_fail(geometry != NULL);

  memset(geometry, 0, sizeof(*geometry));
  geometry->board_size = board_size;
  geometry->squares = board_playable_squares(board_size);
  memset(geometry->rays, CHECKERS_DIRECTION_SENTINEL, sizeof(geometry->rays));

  for (uint8_t index = 0; index < geometry->squares; ++index) {
    int row = 0;
    int col = 0;
    board_coord_from_index(index, &row, &col, board_size);

    for (uint8_t direction = 0; direction < CHECKERS_DIRECTION_COUNT; ++direction) {
      int scan_row = row + direction_row_delta[direction];
      int scan_col = col + direction_col_delta[direction];
      uint8_t write = 0;

      while (write < CHECKERS_MAX_DIRECTION_STEPS) {
        int8_t next_index = board_index_from_coord(scan_row, scan_col, board_size);
        if (next_index < 0) {
          break;
        }
        geometry->rays[index][direction][write++] = next_index;
        scan_row += direction_row_delta[direction];
        scan_col += direction_col_delta[direction];
      }
    }
  }
}

static const CheckersBoardGeometry *checkers_board_geometry_get_8(void) {
  static CheckersBoardGeometry geometry;
  static gsize initialized = 0;

  if (g_once_init_enter(&initialized)) {
    checkers_board_geometry_build(&geometry, 8);
    g_once_init_leave(&initialized, 1);
  }

  return &geometry;
}

static const CheckersBoardGeometry *checkers_board_geometry_get_10(void) {
  static CheckersBoardGeometry geometry;
  static gsize initialized = 0;

  if (g_once_init_enter(&initialized)) {
    checkers_board_geometry_build(&geometry, 10);
    g_once_init_leave(&initialized, 1);
  }

  return &geometry;
}

const CheckersBoardGeometry *checkers_board_geometry_get(uint8_t board_size) {
  switch (board_size) {
    case 8:
      return checkers_board_geometry_get_8();
    case 10:
      return checkers_board_geometry_get_10();
    default:
      g_debug("Unsupported board size for geometry: %u\n", board_size);
      return NULL;
  }
}
