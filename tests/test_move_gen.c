#define _GNU_SOURCE

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/game.h"
#include "../src/rulesets.h"

static void test_init_game_with_ruleset(Game *game, PlayerRuleset ruleset) {
  assert(game != NULL);

  const CheckersRules *rules = checkers_ruleset_get_rules(ruleset);
  assert(rules != NULL);
  game_init_with_rules(game, rules);
}

static void test_initial_setup_moves(void) {
  Game game;
  test_init_game_with_ruleset(&game, PLAYER_RULESET_AMERICAN);

  assert(game.state.turn == CHECKERS_COLOR_WHITE);
  assert(game.state.winner == CHECKERS_WINNER_NONE);
  assert(game.rules->board_size == 8);

  for (uint8_t i = 0; i < 12; ++i) {
    assert(board_get(&game.state.board, i) == CHECKERS_PIECE_BLACK_MAN);
  }
  for (uint8_t i = 12; i < 20; ++i) {
    assert(board_get(&game.state.board, i) == CHECKERS_PIECE_EMPTY);
  }
  for (uint8_t i = 20; i < 32; ++i) {
    assert(board_get(&game.state.board, i) == CHECKERS_PIECE_WHITE_MAN);
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

static void test_move_selection_helpers(void) {
  Game game;
  test_init_game_with_ruleset(&game, PLAYER_RULESET_AMERICAN);
  memset(game.state.board.data, 0, sizeof(game.state.board.data));
  game.state.turn = CHECKERS_COLOR_WHITE;

  int8_t white_index = board_index_from_coord(5, 0, game.rules->board_size);
  int8_t landing_index = board_index_from_coord(4, 1, game.rules->board_size);
  assert(white_index >= 0 && landing_index >= 0);

  board_set(&game.state.board, (uint8_t)white_index, CHECKERS_PIECE_WHITE_MAN);

  MoveList moves = game_list_available_moves(&game);
  assert(moves.count == 1);

  bool starts[CHECKERS_MAX_SQUARES];
  game_moves_collect_starts(&moves, starts);
  assert(starts[white_index]);
  assert(!starts[landing_index]);

  bool destinations[CHECKERS_MAX_SQUARES];
  uint8_t path[] = {(uint8_t)white_index};
  game_moves_collect_next_destinations(&moves, path, 1, destinations);
  assert(destinations[landing_index]);
  assert(!destinations[white_index]);

  movelist_free(&moves);
  game_destroy(&game);
}

static void test_black_simple_move_direction(void) {
  Game game;
  test_init_game_with_ruleset(&game, PLAYER_RULESET_AMERICAN);
  memset(game.state.board.data, 0, sizeof(game.state.board.data));
  game.state.turn = CHECKERS_COLOR_BLACK;

  int8_t black_index = board_index_from_coord(2, 3, game.rules->board_size);
  int8_t forward_left = board_index_from_coord(3, 2, game.rules->board_size);
  int8_t forward_right = board_index_from_coord(3, 4, game.rules->board_size);
  int8_t backward_left = board_index_from_coord(1, 2, game.rules->board_size);
  int8_t backward_right = board_index_from_coord(1, 4, game.rules->board_size);
  assert(black_index >= 0 && forward_left >= 0 && forward_right >= 0);
  assert(backward_left >= 0 && backward_right >= 0);

  board_set(&game.state.board, (uint8_t)black_index, CHECKERS_PIECE_BLACK_MAN);

  MoveList moves = game_list_available_moves(&game);
  assert(moves.count == 2);
  bool saw_forward_left = false;
  bool saw_forward_right = false;
  for (size_t i = 0; i < moves.count; ++i) {
    assert(moves.moves[i].captures == 0);
    assert(moves.moves[i].path[0] == (uint8_t)black_index);
    assert(moves.moves[i].path[1] != (uint8_t)backward_left);
    assert(moves.moves[i].path[1] != (uint8_t)backward_right);
    if (moves.moves[i].path[1] == (uint8_t)forward_left) {
      saw_forward_left = true;
    }
    if (moves.moves[i].path[1] == (uint8_t)forward_right) {
      saw_forward_right = true;
    }
  }
  assert(saw_forward_left);
  assert(saw_forward_right);
  movelist_free(&moves);

  game_destroy(&game);
}

static void test_forced_capture_and_removal_moves(void) {
  Game game;
  test_init_game_with_ruleset(&game, PLAYER_RULESET_AMERICAN);
  memset(game.state.board.data, 0, sizeof(game.state.board.data));
  game.state.turn = CHECKERS_COLOR_WHITE;
  game.state.winner = CHECKERS_WINNER_NONE;

  int8_t white_index = board_index_from_coord(5, 2, game.rules->board_size);
  int8_t black_index = board_index_from_coord(4, 3, game.rules->board_size);
  int8_t landing_index = board_index_from_coord(3, 4, game.rules->board_size);
  assert(white_index >= 0 && black_index >= 0 && landing_index >= 0);

  board_set(&game.state.board, (uint8_t)white_index, CHECKERS_PIECE_WHITE_MAN);
  board_set(&game.state.board, (uint8_t)black_index, CHECKERS_PIECE_BLACK_MAN);

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

  assert(board_get(&game.state.board, (uint8_t)white_index) == CHECKERS_PIECE_EMPTY);
  assert(board_get(&game.state.board, (uint8_t)black_index) == CHECKERS_PIECE_EMPTY);
  assert(board_get(&game.state.board, (uint8_t)landing_index) == CHECKERS_PIECE_WHITE_MAN);

  game_destroy(&game);
}

static void test_no_capture_over_own_piece(void) {
  Game game;
  test_init_game_with_ruleset(&game, PLAYER_RULESET_AMERICAN);
  memset(game.state.board.data, 0, sizeof(game.state.board.data));
  game.state.turn = CHECKERS_COLOR_WHITE;
  game.state.winner = CHECKERS_WINNER_NONE;

  uint8_t black_positions[] = {0, 3, 4, 7, 9, 12, 13, 23};
  uint8_t white_positions[] = {16, 20, 21, 22, 24, 27, 28, 30};
  for (size_t i = 0; i < sizeof(black_positions) / sizeof(black_positions[0]); ++i) {
    board_set(&game.state.board, black_positions[i], CHECKERS_PIECE_BLACK_MAN);
  }
  for (size_t i = 0; i < sizeof(white_positions) / sizeof(white_positions[0]); ++i) {
    board_set(&game.state.board, white_positions[i], CHECKERS_PIECE_WHITE_MAN);
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

static void test_men_backward_jump_rule(void) {
  Game game;
  test_init_game_with_ruleset(&game, PLAYER_RULESET_AMERICAN);
  memset(game.state.board.data, 0, sizeof(game.state.board.data));
  game.state.turn = CHECKERS_COLOR_WHITE;

  int8_t white_index = board_index_from_coord(2, 1, game.rules->board_size);
  int8_t black_index = board_index_from_coord(3, 2, game.rules->board_size);
  int8_t landing_index = board_index_from_coord(4, 3, game.rules->board_size);
  assert(white_index >= 0 && black_index >= 0 && landing_index >= 0);

  board_set(&game.state.board, (uint8_t)white_index, CHECKERS_PIECE_WHITE_MAN);
  board_set(&game.state.board, (uint8_t)black_index, CHECKERS_PIECE_BLACK_MAN);

  MoveList moves = game_list_available_moves(&game);
  for (size_t i = 0; i < moves.count; ++i) {
    assert(moves.moves[i].captures == 0);
  }
  movelist_free(&moves);
  game_destroy(&game);

  test_init_game_with_ruleset(&game, PLAYER_RULESET_RUSSIAN);
  memset(game.state.board.data, 0, sizeof(game.state.board.data));
  game.state.turn = CHECKERS_COLOR_WHITE;
  board_set(&game.state.board, (uint8_t)white_index, CHECKERS_PIECE_WHITE_MAN);
  board_set(&game.state.board, (uint8_t)black_index, CHECKERS_PIECE_BLACK_MAN);

  moves = game_list_available_moves(&game);
  assert(moves.count == 1);
  assert(moves.moves[0].captures == 1);
  assert(moves.moves[0].path[0] == (uint8_t)white_index);
  assert(moves.moves[0].path[1] == (uint8_t)landing_index);
  movelist_free(&moves);

  game_destroy(&game);
}

static void test_capture_mandatory_rule(void) {
  Game game;
  test_init_game_with_ruleset(&game, PLAYER_RULESET_AMERICAN);
  memset(game.state.board.data, 0, sizeof(game.state.board.data));
  game.state.turn = CHECKERS_COLOR_WHITE;

  int8_t white_index = board_index_from_coord(5, 2, game.rules->board_size);
  int8_t black_index = board_index_from_coord(4, 3, game.rules->board_size);
  int8_t landing_index = board_index_from_coord(3, 4, game.rules->board_size);
  assert(white_index >= 0 && black_index >= 0 && landing_index >= 0);

  board_set(&game.state.board, (uint8_t)white_index, CHECKERS_PIECE_WHITE_MAN);
  board_set(&game.state.board, (uint8_t)black_index, CHECKERS_PIECE_BLACK_MAN);

  MoveList moves = game_list_available_moves(&game);
  bool saw_capture = FALSE;
  bool saw_simple = FALSE;
  for (size_t i = 0; i < moves.count; ++i) {
    if (moves.moves[i].captures > 0) {
      saw_capture = TRUE;
    } else {
      saw_simple = TRUE;
    }
  }
  assert(saw_capture);
  assert(!saw_simple);
  movelist_free(&moves);

  game_destroy(&game);
}

static void test_longest_capture_rule(void) {
  Game game;
  test_init_game_with_ruleset(&game, PLAYER_RULESET_RUSSIAN);
  memset(game.state.board.data, 0, sizeof(game.state.board.data));
  game.state.turn = CHECKERS_COLOR_WHITE;

  int8_t multi_start = board_index_from_coord(5, 0, game.rules->board_size);
  int8_t single_start = board_index_from_coord(5, 4, game.rules->board_size);
  int8_t first_mid = board_index_from_coord(4, 1, game.rules->board_size);
  int8_t second_mid = board_index_from_coord(2, 3, game.rules->board_size);
  int8_t single_mid = board_index_from_coord(4, 5, game.rules->board_size);
  int8_t multi_end = board_index_from_coord(1, 4, game.rules->board_size);
  assert(multi_start >= 0 && single_start >= 0 && first_mid >= 0 && second_mid >= 0);
  assert(single_mid >= 0 && multi_end >= 0);

  board_set(&game.state.board, (uint8_t)multi_start, CHECKERS_PIECE_WHITE_MAN);
  board_set(&game.state.board, (uint8_t)single_start, CHECKERS_PIECE_WHITE_MAN);
  board_set(&game.state.board, (uint8_t)first_mid, CHECKERS_PIECE_BLACK_MAN);
  board_set(&game.state.board, (uint8_t)second_mid, CHECKERS_PIECE_BLACK_MAN);
  board_set(&game.state.board, (uint8_t)single_mid, CHECKERS_PIECE_BLACK_MAN);

  MoveList moves = game_list_available_moves(&game);
  assert(moves.count == 1);
  assert(moves.moves[0].captures == 2);
  assert(moves.moves[0].path[0] == (uint8_t)multi_start);
  assert(moves.moves[0].path[moves.moves[0].length - 1] == (uint8_t)multi_end);
  movelist_free(&moves);

  game_destroy(&game);
}

static void test_kings_can_fly(void) {
  Game game;
  test_init_game_with_ruleset(&game, PLAYER_RULESET_RUSSIAN);
  memset(game.state.board.data, 0, sizeof(game.state.board.data));
  game.state.turn = CHECKERS_COLOR_WHITE;

  int8_t king_index = board_index_from_coord(5, 0, game.rules->board_size);
  int8_t enemy_index = board_index_from_coord(3, 2, game.rules->board_size);
  int8_t landing_far = board_index_from_coord(1, 4, game.rules->board_size);
  assert(king_index >= 0 && enemy_index >= 0 && landing_far >= 0);

  board_set(&game.state.board, (uint8_t)king_index, CHECKERS_PIECE_WHITE_KING);
  board_set(&game.state.board, (uint8_t)enemy_index, CHECKERS_PIECE_BLACK_MAN);

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
  test_initial_setup_moves();
  test_move_selection_helpers();
  test_black_simple_move_direction();
  test_forced_capture_and_removal_moves();
  test_no_capture_over_own_piece();
  test_men_backward_jump_rule();
  test_capture_mandatory_rule();
  test_longest_capture_rule();
  test_kings_can_fly();

  printf("All tests passed.\n");
  return 0;
}
