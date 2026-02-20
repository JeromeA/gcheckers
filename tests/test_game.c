#define _GNU_SOURCE

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/game.h"

static void test_apply_simple_move(void) {
  Game game;
  game_init(&game);

  CheckersMove move = {.path = {21, 17}, .length = 2, .captures = 0};
  int rc = game_apply_move(&game, &move);
  assert(rc == 0);

  assert(board_get(&game.state.board, 21) == CHECKERS_PIECE_EMPTY);
  assert(board_get(&game.state.board, 17) == CHECKERS_PIECE_WHITE_MAN);
  assert(game.state.turn == CHECKERS_COLOR_BLACK);

  game_destroy(&game);
}

static void test_presets_and_board_size(void) {
  Game game;
  CheckersRules rules = game_rules_international_draughts();
  game_init_with_rules(&game, &rules);

  assert(game.rules.board_size == 10);

  for (uint8_t i = 0; i < 20; ++i) {
    assert(board_get(&game.state.board, i) == CHECKERS_PIECE_BLACK_MAN);
  }
  for (uint8_t i = 20; i < 30; ++i) {
    assert(board_get(&game.state.board, i) == CHECKERS_PIECE_EMPTY);
  }
  for (uint8_t i = 30; i < 50; ++i) {
    assert(board_get(&game.state.board, i) == CHECKERS_PIECE_WHITE_MAN);
  }

  game_destroy(&game);
}

int main(void) {
  test_apply_simple_move();
  test_presets_and_board_size();

  printf("All tests passed.\n");
  return 0;
}
