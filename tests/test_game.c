#include <assert.h>
#include <stdint.h>
#include <stdio.h>
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

static int8_t index_from_coord(int row, int col) {
    if (row < 0 || row >= 8 || col < 0 || col >= 8) {
        return -1;
    }
    if ((row + col) % 2 == 0) {
        return -1;
    }
    return (int8_t)(row * 4 + col / 2);
}

static void test_initial_setup(void) {
    Game game;
    game_init(&game);

    assert(game.state.turn == CHECKERS_COLOR_WHITE);
    assert(game.state.winner == CHECKERS_WINNER_NONE);

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
    }
    movelist_free(&moves);

    game_destroy(&game);
}

static void test_apply_simple_move(void) {
    Game game;
    game_init(&game);

    CheckersMove move = {.path = {21, 17}, .length = 2};
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

    int8_t white_index = index_from_coord(5, 2);
    int8_t black_index = index_from_coord(4, 3);
    int8_t landing_index = index_from_coord(3, 4);
    assert(white_index >= 0 && black_index >= 0 && landing_index >= 0);

    board_set(&game.state, (uint8_t)white_index, CHECKERS_PIECE_WHITE_MAN);
    board_set(&game.state, (uint8_t)black_index, CHECKERS_PIECE_BLACK_MAN);

    MoveList moves = game_list_available_moves(&game);
    assert(moves.count == 1);
    assert(moves.moves[0].length == 2);
    assert(moves.moves[0].path[0] == (uint8_t)white_index);
    assert(moves.moves[0].path[1] == (uint8_t)landing_index);
    movelist_free(&moves);

    CheckersMove capture = {.path = {(uint8_t)white_index, (uint8_t)landing_index}, .length = 2};
    int rc = game_apply_move(&game, &capture);
    assert(rc == 0);

    assert(board_get(&game.state, (uint8_t)white_index) == CHECKERS_PIECE_EMPTY);
    assert(board_get(&game.state, (uint8_t)black_index) == CHECKERS_PIECE_EMPTY);
    assert(board_get(&game.state, (uint8_t)landing_index) == CHECKERS_PIECE_WHITE_MAN);

    game_destroy(&game);
}

int main(void) {
    test_initial_setup();
    test_apply_simple_move();
    test_forced_capture_and_removal();

    printf("All tests passed.\n");
    return 0;
}

