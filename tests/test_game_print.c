#define _GNU_SOURCE

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/game.h"

static void test_move_notation(void) {
  CheckersMove simple = {.path = {0, 4}, .length = 2, .captures = 0};
  char buffer[32];
  bool ok = game_format_move_notation(&simple, buffer, sizeof(buffer));
  assert(ok);
  assert(strcmp(buffer, "1-5") == 0);

  int8_t start = board_index_from_coord(5, 0, 8);
  int8_t mid = board_index_from_coord(3, 2, 8);
  int8_t end = board_index_from_coord(1, 4, 8);
  assert(start >= 0 && mid >= 0 && end >= 0);

  CheckersMove jump = {.path = {(uint8_t)start, (uint8_t)mid, (uint8_t)end},
                       .length = 3,
                       .captures = 2};
  ok = game_format_move_notation(&jump, buffer, sizeof(buffer));
  assert(ok);
  assert(strcmp(buffer, "21x14x7") == 0);
}

static void test_print_state_format(void) {
  Game game;
  game_init(&game);

  int8_t white_king = board_index_from_coord(3, 0, game.rules.board_size);
  int8_t black_king = board_index_from_coord(3, 2, game.rules.board_size);
  assert(white_king >= 0 && black_king >= 0);
  board_set(&game.state.board, (uint8_t)white_king, CHECKERS_PIECE_WHITE_KING);
  board_set(&game.state.board, (uint8_t)black_king, CHECKERS_PIECE_BLACK_KING);

  char *buffer = NULL;
  size_t size = 0;
  FILE *stream = open_memstream(&buffer, &size);
  assert(stream != NULL);

  game_print_state(&game, stream);
  fclose(stream);
  assert(buffer != NULL);

  const char *line = strstr(buffer, "Winner: None\n");
  assert(line != NULL);
  line += strlen("Winner: None\n");

  const char *reverse = "\x1b[7m    \x1b[0m";
  char row0_top_expected[128];
  char row0_bottom_expected[128];
  char row3_top_expected[128];
  char row3_bottom_expected[128];

  snprintf(row0_top_expected,
           sizeof(row0_top_expected),
           "%s ⛂  %s ⛂  %s ⛂  %s ⛂  \n",
           reverse,
           reverse,
           reverse,
           reverse);
  snprintf(row0_bottom_expected,
           sizeof(row0_bottom_expected),
           "%s ₁  %s ₂  %s ₃  %s ₄  \n",
           reverse,
           reverse,
           reverse,
           reverse);
  snprintf(row3_top_expected,
           sizeof(row3_top_expected),
           " ⛁  %s ⛃  %s    %s    %s\n",
           reverse,
           reverse,
           reverse,
           reverse);
  snprintf(row3_bottom_expected,
           sizeof(row3_bottom_expected),
           " ₁₃ %s ₁₄ %s ₁₅ %s ₁₆ %s\n",
           reverse,
           reverse,
           reverse,
           reverse);

  assert(strncmp(line, row0_top_expected, strlen(row0_top_expected)) == 0);
  line = strchr(line, '\n');
  assert(line != NULL);
  line += 1;
  assert(strncmp(line, row0_bottom_expected, strlen(row0_bottom_expected)) == 0);

  for (int i = 0; i < 5; ++i) {
    line = strchr(line, '\n');
    assert(line != NULL);
    line += 1;
  }

  assert(strncmp(line, row3_top_expected, strlen(row3_top_expected)) == 0);
  line = strchr(line, '\n');
  assert(line != NULL);
  line += 1;
  assert(strncmp(line, row3_bottom_expected, strlen(row3_bottom_expected)) == 0);

  free(buffer);
  game_destroy(&game);
}

int main(void) {
  test_move_notation();
  test_print_state_format();

  printf("All tests passed.\n");
  return 0;
}
