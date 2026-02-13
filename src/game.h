#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stddef.h>

#include "board.h"
#include "checkers_constants.h"

typedef enum {
  CHECKERS_WINNER_NONE = 0,
  CHECKERS_WINNER_WHITE,
  CHECKERS_WINNER_BLACK,
  CHECKERS_WINNER_DRAW
} CheckersWinner;

typedef struct {
  uint8_t path[CHECKERS_MAX_MOVE_LENGTH];
  uint8_t length;
  uint8_t captures;
} CheckersMove;

typedef struct {
  CheckersMove *moves;
  size_t count;
} MoveList;

typedef struct {
  uint8_t board_size;
  bool men_can_jump_backwards;
  bool capture_mandatory;
  bool longest_capture_mandatory;
  bool kings_can_fly;
} CheckersRules;

typedef struct {
  CheckersBoard board;
  CheckersColor turn;
  CheckersWinner winner;
} GameState;

typedef struct Game Game;

struct Game {
  GameState state;
  CheckersRules rules;
  CheckersMove *history;
  size_t history_size;
  size_t history_capacity;
};

void game_init(Game *game);
void game_init_with_rules(Game *game, const CheckersRules *rules);
void game_destroy(Game *game);

CheckersRules game_rules_american_checkers(void);

MoveList game_list_available_moves(const Game *game);
void movelist_free(MoveList *list);
void game_moves_collect_starts(const MoveList *moves, bool starts[CHECKERS_MAX_SQUARES]);
void game_moves_collect_next_destinations(const MoveList *moves,
                                          const uint8_t *path,
                                          uint8_t length,
                                          bool destinations[CHECKERS_MAX_SQUARES]);

const char *game_winner_label(CheckersWinner winner);

int game_apply_move(Game *game, const CheckersMove *move);

#endif
