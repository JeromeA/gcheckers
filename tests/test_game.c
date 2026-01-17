#define _GNU_SOURCE

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/game.h"

static uint8_t board_get(const GameState *state, uint8_t index) {
  uint8_t packed = state->board[index / 2];
  if (index % 2 == 0) {
    return packed & 0x0F;
  }
  return (packed >> 4) & 0x0F;
}

static void board_set(GameState *state, uint8_t index, uint8_t value) {
  uint8_t *packed = &state->board[index / 2];
  if (index % 2 == 0) {
    *packed = (uint8_t)((*packed & 0xF0) | (value & 0x0F));
  } else {
    *packed = (uint8_t)((*packed & 0x0F) | ((value & 0x0F) << 4));
  }
}

static int8_t index_from_coord(int row, int col, uint8_t board_size) {
  if (row < 0 || row >= board_size || col < 0 || col >= board_size) {
    return -1;
  }
  if ((row + col) % 2 == 0) {
    return -1;
  }
  int per_row = board_size / 2;
  return (int8_t)(row * per_row + col / 2);
}

static void test_initial_setup(void) {
  Game game;
  game_init(&game);

  assert(game.state.turn == CHECKERS_COLOR_WHITE);
  assert(game.state.winner == CHECKERS_WINNER_NONE);
  assert(game.rules.board_size == 8);

  for (uint8_t i = 0; i < 12; ++i) {
    assert(board_get(&game.state, i) == CHECKERS_PIECE_BLACK_MAN);
  }
  for (uint8_t i = 12; i < 20; ++i) {
    assert(board_get(&game.state, i) == CHECKERS_PIECE_EMPTY);
  }
  for (uint8_t i = 20; i < 32; ++i) {
    assert(board_get(&game.state, i) == CHECKERS_PIECE_WHITE_MAN);
  }

  MoveList moves = game_list_available_moves(&game);
  assert(moves.count > 0);
  for (size_t i = 0; i < moves.count; ++i) {
    assert(moves.moves[i].length == 2);
    assert(moves.moves[i].captures == 0);
  }
  movelist_free(&moves);

  game_destroy(&game);
}

static void test_apply_simple_move(void) {
  Game game;
  game_init(&game);

  CheckersMove move = {.path = {21, 17}, .length = 2, .captures = 0};
  int rc = game_apply_move(&game, &move);
  assert(rc == 0);

  assert(board_get(&game.state, 21) == CHECKERS_PIECE_EMPTY);
  assert(board_get(&game.state, 17) == CHECKERS_PIECE_WHITE_MAN);
  assert(game.state.turn == CHECKERS_COLOR_BLACK);
  assert(game.history_size == 1);

  game_destroy(&game);
}

static void test_forced_capture_and_removal(void) {
  Game game;
  game_init(&game);
  memset(game.state.board, 0, sizeof(game.state.board));
  game.state.turn = CHECKERS_COLOR_WHITE;
  game.state.winner = CHECKERS_WINNER_NONE;

  int8_t white_index = index_from_coord(5, 2, game.rules.board_size);
  int8_t black_index = index_from_coord(4, 3, game.rules.board_size);
  int8_t landing_index = index_from_coord(3, 4, game.rules.board_size);
  assert(white_index >= 0 && black_index >= 0 && landing_index >= 0);

  board_set(&game.state, (uint8_t)white_index, CHECKERS_PIECE_WHITE_MAN);
  board_set(&game.state, (uint8_t)black_index, CHECKERS_PIECE_BLACK_MAN);

  MoveList moves = game_list_available_moves(&game);
  assert(moves.count == 1);
  assert(moves.moves[0].length == 2);
  assert(moves.moves[0].captures == 1);
  assert(moves.moves[0].path[0] == (uint8_t)white_index);
  assert(moves.moves[0].path[1] == (uint8_t)landing_index);
  movelist_free(&moves);

  CheckersMove capture = {
      .path = {(uint8_t)white_index, (uint8_t)landing_index}, .length = 2, .captures = 1};
  int rc = game_apply_move(&game, &capture);
  assert(rc == 0);

  assert(board_get(&game.state, (uint8_t)white_index) == CHECKERS_PIECE_EMPTY);
  assert(board_get(&game.state, (uint8_t)black_index) == CHECKERS_PIECE_EMPTY);
  assert(board_get(&game.state, (uint8_t)landing_index) == CHECKERS_PIECE_WHITE_MAN);

  game_destroy(&game);
}

static void test_move_notation(void) {
  CheckersMove simple = {.path = {0, 4}, .length = 2, .captures = 0};
  char buffer[32];
  bool ok = game_format_move_notation(&simple, buffer, sizeof(buffer));
  assert(ok);
  assert(strcmp(buffer, "1-5") == 0);

  int8_t start = index_from_coord(5, 0, 8);
  int8_t mid = index_from_coord(3, 2, 8);
  int8_t end = index_from_coord(1, 4, 8);
  assert(start >= 0 && mid >= 0 && end >= 0);

  CheckersMove jump = {.path = {(uint8_t)start, (uint8_t)mid, (uint8_t)end}, .length = 3, .captures = 2};
  ok = game_format_move_notation(&jump, buffer, sizeof(buffer));
  assert(ok);
  assert(strcmp(buffer, "21x14x7") == 0);
}

static void test_print_state_format(void) {
  Game game;
  game_init(&game);

  int8_t white_king = index_from_coord(3, 0, game.rules.board_size);
  int8_t black_king = index_from_coord(3, 2, game.rules.board_size);
  assert(white_king >= 0 && black_king >= 0);
  board_set(&game.state, (uint8_t)white_king, CHECKERS_PIECE_WHITE_KING);
  board_set(&game.state, (uint8_t)black_king, CHECKERS_PIECE_BLACK_KING);

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

  snprintf(row0_top_expected, sizeof(row0_top_expected), "%s ⛂  %s ⛂  %s ⛂  %s ⛂  \n", reverse, reverse,
           reverse, reverse);
  snprintf(row0_bottom_expected, sizeof(row0_bottom_expected), "%s ₁  %s ₂  %s ₃  %s ₄  \n", reverse, reverse,
           reverse, reverse);
  snprintf(row3_top_expected, sizeof(row3_top_expected), " ⛁  %s ⛃  %s    %s    %s\n", reverse, reverse, reverse,
           reverse);
  snprintf(row3_bottom_expected, sizeof(row3_bottom_expected), " ₁₃ %s ₁₄ %s ₁₅ %s ₁₆ %s\n", reverse, reverse,
           reverse, reverse);

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

static void test_no_capture_over_own_piece(void) {
  Game game;
  game_init(&game);
  memset(game.state.board, 0, sizeof(game.state.board));
  game.state.turn = CHECKERS_COLOR_WHITE;
  game.state.winner = CHECKERS_WINNER_NONE;

  uint8_t black_positions[] = {0, 3, 4, 7, 9, 12, 13, 23};
  uint8_t white_positions[] = {16, 20, 21, 22, 24, 27, 28, 30};
  for (size_t i = 0; i < sizeof(black_positions) / sizeof(black_positions[0]); ++i) {
    board_set(&game.state, black_positions[i], CHECKERS_PIECE_BLACK_MAN);
  }
  for (size_t i = 0; i < sizeof(white_positions) / sizeof(white_positions[0]); ++i) {
    board_set(&game.state, white_positions[i], CHECKERS_PIECE_WHITE_MAN);
  }

  MoveList moves = game_list_available_moves(&game);
  assert(moves.count == 1);
  assert(moves.moves[0].length == 2);
  assert(moves.moves[0].captures == 1);
  assert(moves.moves[0].path[0] == 27);
  assert(moves.moves[0].path[1] == 18);
  movelist_free(&moves);

  game_destroy(&game);
}

static void test_presets_and_board_size(void) {
  Game game;
  CheckersRules rules = game_rules_international_draughts();
  game_init_with_rules(&game, &rules);

  assert(game.rules.board_size == 10);

  for (uint8_t i = 0; i < 20; ++i) {
    assert(board_get(&game.state, i) == CHECKERS_PIECE_BLACK_MAN);
  }
  for (uint8_t i = 20; i < 30; ++i) {
    assert(board_get(&game.state, i) == CHECKERS_PIECE_EMPTY);
  }
  for (uint8_t i = 30; i < 50; ++i) {
    assert(board_get(&game.state, i) == CHECKERS_PIECE_WHITE_MAN);
  }

  game_destroy(&game);
}

static void test_men_backward_jump_rule(void) {
  CheckersRules rules = game_rules_american_checkers();
  rules.men_can_jump_backwards = false;

  Game game;
  game_init_with_rules(&game, &rules);
  memset(game.state.board, 0, sizeof(game.state.board));
  game.state.turn = CHECKERS_COLOR_WHITE;

  int8_t white_index = index_from_coord(2, 1, game.rules.board_size);
  int8_t black_index = index_from_coord(3, 2, game.rules.board_size);
  int8_t landing_index = index_from_coord(4, 3, game.rules.board_size);
  assert(white_index >= 0 && black_index >= 0 && landing_index >= 0);

  board_set(&game.state, (uint8_t)white_index, CHECKERS_PIECE_WHITE_MAN);
  board_set(&game.state, (uint8_t)black_index, CHECKERS_PIECE_BLACK_MAN);

  MoveList moves = game_list_available_moves(&game);
  for (size_t i = 0; i < moves.count; ++i) {
    assert(moves.moves[i].captures == 0);
  }
  movelist_free(&moves);
  game_destroy(&game);

  rules.men_can_jump_backwards = true;
  game_init_with_rules(&game, &rules);
  memset(game.state.board, 0, sizeof(game.state.board));
  game.state.turn = CHECKERS_COLOR_WHITE;
  board_set(&game.state, (uint8_t)white_index, CHECKERS_PIECE_WHITE_MAN);
  board_set(&game.state, (uint8_t)black_index, CHECKERS_PIECE_BLACK_MAN);

  moves = game_list_available_moves(&game);
  assert(moves.count == 1);
  assert(moves.moves[0].captures == 1);
  assert(moves.moves[0].path[0] == (uint8_t)white_index);
  assert(moves.moves[0].path[1] == (uint8_t)landing_index);
  movelist_free(&moves);

  game_destroy(&game);
}

static void test_capture_optional_rule(void) {
  CheckersRules rules = game_rules_american_checkers();
  rules.capture_mandatory = false;

  Game game;
  game_init_with_rules(&game, &rules);
  memset(game.state.board, 0, sizeof(game.state.board));
  game.state.turn = CHECKERS_COLOR_WHITE;

  int8_t white_index = index_from_coord(5, 2, game.rules.board_size);
  int8_t black_index = index_from_coord(4, 3, game.rules.board_size);
  int8_t landing_index = index_from_coord(3, 4, game.rules.board_size);
  assert(white_index >= 0 && black_index >= 0 && landing_index >= 0);

  board_set(&game.state, (uint8_t)white_index, CHECKERS_PIECE_WHITE_MAN);
  board_set(&game.state, (uint8_t)black_index, CHECKERS_PIECE_BLACK_MAN);

  MoveList moves = game_list_available_moves(&game);
  bool saw_capture = false;
  bool saw_simple = false;
  for (size_t i = 0; i < moves.count; ++i) {
    if (moves.moves[i].captures > 0) {
      saw_capture = true;
    } else {
      saw_simple = true;
    }
  }
  assert(saw_capture);
  assert(saw_simple);
  movelist_free(&moves);

  game_destroy(&game);
}

static void test_longest_capture_rule(void) {
  CheckersRules rules = game_rules_american_checkers();
  rules.longest_capture_mandatory = true;

  Game game;
  game_init_with_rules(&game, &rules);
  memset(game.state.board, 0, sizeof(game.state.board));
  game.state.turn = CHECKERS_COLOR_WHITE;

  int8_t multi_start = index_from_coord(5, 0, game.rules.board_size);
  int8_t single_start = index_from_coord(5, 4, game.rules.board_size);
  int8_t first_mid = index_from_coord(4, 1, game.rules.board_size);
  int8_t second_mid = index_from_coord(2, 3, game.rules.board_size);
  int8_t single_mid = index_from_coord(4, 5, game.rules.board_size);
  int8_t multi_end = index_from_coord(1, 4, game.rules.board_size);
  assert(multi_start >= 0 && single_start >= 0 && first_mid >= 0 && second_mid >= 0);
  assert(single_mid >= 0 && multi_end >= 0);

  board_set(&game.state, (uint8_t)multi_start, CHECKERS_PIECE_WHITE_MAN);
  board_set(&game.state, (uint8_t)single_start, CHECKERS_PIECE_WHITE_MAN);
  board_set(&game.state, (uint8_t)first_mid, CHECKERS_PIECE_BLACK_MAN);
  board_set(&game.state, (uint8_t)second_mid, CHECKERS_PIECE_BLACK_MAN);
  board_set(&game.state, (uint8_t)single_mid, CHECKERS_PIECE_BLACK_MAN);

  MoveList moves = game_list_available_moves(&game);
  assert(moves.count == 1);
  assert(moves.moves[0].captures == 2);
  assert(moves.moves[0].path[0] == (uint8_t)multi_start);
  assert(moves.moves[0].path[moves.moves[0].length - 1] == (uint8_t)multi_end);
  movelist_free(&moves);

  game_destroy(&game);
}

static void test_kings_can_fly(void) {
  CheckersRules rules = game_rules_international_draughts();
  rules.board_size = 8;

  Game game;
  game_init_with_rules(&game, &rules);
  memset(game.state.board, 0, sizeof(game.state.board));
  game.state.turn = CHECKERS_COLOR_WHITE;

  int8_t king_index = index_from_coord(5, 0, game.rules.board_size);
  int8_t enemy_index = index_from_coord(3, 2, game.rules.board_size);
  int8_t landing_far = index_from_coord(1, 4, game.rules.board_size);
  assert(king_index >= 0 && enemy_index >= 0 && landing_far >= 0);

  board_set(&game.state, (uint8_t)king_index, CHECKERS_PIECE_WHITE_KING);
  board_set(&game.state, (uint8_t)enemy_index, CHECKERS_PIECE_BLACK_MAN);

  MoveList moves = game_list_available_moves(&game);
  bool found_far = false;
  for (size_t i = 0; i < moves.count; ++i) {
    if (moves.moves[i].captures == 1 &&
        moves.moves[i].path[moves.moves[i].length - 1] == (uint8_t)landing_far) {
      found_far = true;
    }
  }
  assert(found_far);
  movelist_free(&moves);

  game_destroy(&game);
}

int main(void) {
  test_initial_setup();
  test_apply_simple_move();
  test_forced_capture_and_removal();
  test_move_notation();
  test_print_state_format();
  test_no_capture_over_own_piece();
  test_presets_and_board_size();
  test_men_backward_jump_rule();
  test_capture_optional_rule();
  test_longest_capture_rule();
  test_kings_can_fly();

  printf("All tests passed.\n");
  return 0;
}
