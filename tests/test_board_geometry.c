#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/games/checkers/board_geometry.h"

static void assert_ray_equals(const int8_t *ray, const int8_t *expected, size_t expected_length) {
  assert(ray != NULL);
  assert(expected != NULL);

  for (size_t i = 0; i < expected_length; ++i) {
    assert(ray[i] == expected[i]);
  }

  for (size_t i = expected_length; i < CHECKERS_MAX_DIRECTION_STEPS + 1; ++i) {
    assert(ray[i] == CHECKERS_DIRECTION_SENTINEL);
  }
}

static void test_8x8_rays(void) {
  const CheckersBoardGeometry *geometry = checkers_board_geometry_get(8);
  assert(geometry != NULL);
  assert(geometry->board_size == 8);
  assert(geometry->squares == 32);

  static const int8_t index_0_up_left[] = {-1};
  static const int8_t index_0_up_right[] = {-1};
  static const int8_t index_0_down_left[] = {4, -1};
  static const int8_t index_0_down_right[] = {5, 9, 14, 18, 23, 27, -1};

  assert_ray_equals(geometry->rays[0][CHECKERS_DIRECTION_UP_LEFT], index_0_up_left, 1);
  assert_ray_equals(geometry->rays[0][CHECKERS_DIRECTION_UP_RIGHT], index_0_up_right, 1);
  assert_ray_equals(geometry->rays[0][CHECKERS_DIRECTION_DOWN_LEFT], index_0_down_left, 2);
  assert_ray_equals(geometry->rays[0][CHECKERS_DIRECTION_DOWN_RIGHT], index_0_down_right, 7);

  static const int8_t index_13_up_left[] = {8, 4, -1};
  static const int8_t index_13_up_right[] = {9, 6, 2, -1};
  static const int8_t index_13_down_left[] = {16, 20, -1};
  static const int8_t index_13_down_right[] = {17, 22, 26, 31, -1};

  assert_ray_equals(geometry->rays[13][CHECKERS_DIRECTION_UP_LEFT], index_13_up_left, 3);
  assert_ray_equals(geometry->rays[13][CHECKERS_DIRECTION_UP_RIGHT], index_13_up_right, 4);
  assert_ray_equals(geometry->rays[13][CHECKERS_DIRECTION_DOWN_LEFT], index_13_down_left, 3);
  assert_ray_equals(geometry->rays[13][CHECKERS_DIRECTION_DOWN_RIGHT], index_13_down_right, 5);

  static const int8_t index_11_up_left[] = {7, 2, -1};
  static const int8_t index_11_up_right[] = {-1};
  static const int8_t index_11_down_left[] = {15, 18, 22, 25, 29, -1};
  static const int8_t index_11_down_right[] = {-1};

  assert_ray_equals(geometry->rays[11][CHECKERS_DIRECTION_UP_LEFT], index_11_up_left, 3);
  assert_ray_equals(geometry->rays[11][CHECKERS_DIRECTION_UP_RIGHT], index_11_up_right, 1);
  assert_ray_equals(geometry->rays[11][CHECKERS_DIRECTION_DOWN_LEFT], index_11_down_left, 6);
  assert_ray_equals(geometry->rays[11][CHECKERS_DIRECTION_DOWN_RIGHT], index_11_down_right, 1);
}

static void test_10x10_rays(void) {
  const CheckersBoardGeometry *geometry = checkers_board_geometry_get(10);
  assert(geometry != NULL);
  assert(geometry->board_size == 10);
  assert(geometry->squares == 50);

  static const int8_t index_0_up_left[] = {-1};
  static const int8_t index_0_up_right[] = {-1};
  static const int8_t index_0_down_left[] = {5, -1};
  static const int8_t index_0_down_right[] = {6, 11, 17, 22, 28, 33, 39, 44, -1};

  assert_ray_equals(geometry->rays[0][CHECKERS_DIRECTION_UP_LEFT], index_0_up_left, 1);
  assert_ray_equals(geometry->rays[0][CHECKERS_DIRECTION_UP_RIGHT], index_0_up_right, 1);
  assert_ray_equals(geometry->rays[0][CHECKERS_DIRECTION_DOWN_LEFT], index_0_down_left, 2);
  assert_ray_equals(geometry->rays[0][CHECKERS_DIRECTION_DOWN_RIGHT], index_0_down_right, 9);

  static const int8_t index_22_up_left[] = {17, 11, 6, 0, -1};
  static const int8_t index_22_up_right[] = {18, 13, 9, 4, -1};
  static const int8_t index_22_down_left[] = {27, 31, 36, 40, 45, -1};
  static const int8_t index_22_down_right[] = {28, 33, 39, 44, -1};

  assert_ray_equals(geometry->rays[22][CHECKERS_DIRECTION_UP_LEFT], index_22_up_left, 5);
  assert_ray_equals(geometry->rays[22][CHECKERS_DIRECTION_UP_RIGHT], index_22_up_right, 5);
  assert_ray_equals(geometry->rays[22][CHECKERS_DIRECTION_DOWN_LEFT], index_22_down_left, 6);
  assert_ray_equals(geometry->rays[22][CHECKERS_DIRECTION_DOWN_RIGHT], index_22_down_right, 5);

  static const int8_t index_14_up_left[] = {9, 3, -1};
  static const int8_t index_14_up_right[] = {-1};
  static const int8_t index_14_down_left[] = {19, 23, 28, 32, 37, 41, 46, -1};
  static const int8_t index_14_down_right[] = {-1};

  assert_ray_equals(geometry->rays[14][CHECKERS_DIRECTION_UP_LEFT], index_14_up_left, 3);
  assert_ray_equals(geometry->rays[14][CHECKERS_DIRECTION_UP_RIGHT], index_14_up_right, 1);
  assert_ray_equals(geometry->rays[14][CHECKERS_DIRECTION_DOWN_LEFT], index_14_down_left, 8);
  assert_ray_equals(geometry->rays[14][CHECKERS_DIRECTION_DOWN_RIGHT], index_14_down_right, 1);
}

int main(void) {
  test_8x8_rays();
  test_10x10_rays();
  puts("Board geometry tests passed.");
  return 0;
}
