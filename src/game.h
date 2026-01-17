#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
  CHECKERS_COLOR_WHITE = 0,
  CHECKERS_COLOR_BLACK = 1
} CheckersColor;

typedef enum {
  CHECKERS_WINNER_NONE = 0,
  CHECKERS_WINNER_WHITE,
  CHECKERS_WINNER_BLACK,
  CHECKERS_WINNER_DRAW
} CheckersWinner;

typedef enum {
  CHECKERS_PIECE_EMPTY = 0,
  CHECKERS_PIECE_WHITE_MAN,
  CHECKERS_PIECE_WHITE_KING,
  CHECKERS_PIECE_BLACK_MAN,
  CHECKERS_PIECE_BLACK_KING
} CheckersPiece;

typedef struct {
  uint8_t path[12];
  uint8_t length;
} CheckersMove;

typedef struct {
  CheckersMove *moves;
  size_t count;
} MoveList;

typedef struct {
  uint8_t board[16];
  CheckersColor turn;
  CheckersWinner winner;
} GameState;

typedef struct Game Game;

struct Game {
  GameState state;
  CheckersMove *history;
  size_t history_size;
  size_t history_capacity;

  void (*print_state)(const Game *game, FILE *out);
  MoveList (*available_moves)(const Game *game);
};

void game_init(Game *game);
void game_destroy(Game *game);

MoveList game_list_available_moves(const Game *game);
void movelist_free(MoveList *list);

void game_print_state(const Game *game, FILE *out);

int game_apply_move(Game *game, const CheckersMove *move);
bool game_format_move_notation(const CheckersMove *move, char *buffer, size_t size);

#endif
