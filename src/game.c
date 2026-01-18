#include "game.h"

#include <glib.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static bool rules_valid(const CheckersRules *rules) {
  if (!rules) {
    return false;
  }
  return rules->board_size == 8 || rules->board_size == 10;
}

CheckersRules game_rules_american_checkers(void) {
  CheckersRules rules = {
      .board_size = 8,
      .men_can_jump_backwards = false,
      .capture_mandatory = true,
      .longest_capture_mandatory = false,
      .kings_can_fly = false};
  return rules;
}

CheckersRules game_rules_international_draughts(void) {
  CheckersRules rules = {
      .board_size = 10,
      .men_can_jump_backwards = true,
      .capture_mandatory = true,
      .longest_capture_mandatory = true,
      .kings_can_fly = true};
  return rules;
}

static void ensure_capacity(Game *game) {
  if (!game) {
    g_debug("ensure_capacity received null game\n");
    g_return_if_fail(game != NULL);
  }

  if (game->history_size < game->history_capacity) {
    return;
  }
  size_t new_capacity = game->history_capacity == 0 ? 16 : game->history_capacity * 2;
  CheckersMove *expanded = realloc(game->history, sizeof(CheckersMove) * new_capacity);
  if (!expanded) {
    g_debug("Failed to expand history buffer\n");
    return;
  }
  game->history = expanded;
  game->history_capacity = new_capacity;
}

void game_init(Game *game) {
  CheckersRules rules = game_rules_american_checkers();
  game_init_with_rules(game, &rules);
}

void game_init_with_rules(Game *game, const CheckersRules *rules) {
  if (!game || !rules) {
    g_debug("game_init_with_rules received invalid arguments\n");
    g_return_if_fail(game != NULL);
    g_return_if_fail(rules != NULL);
  }
  if (!rules_valid(rules)) {
    g_debug("game_init_with_rules received invalid rules\n");
    g_return_if_fail(rules_valid(rules));
  }

  memset(game, 0, sizeof(*game));
  game->rules = *rules;
  board_reset(&game->state.board, game->rules.board_size);
  game->state.turn = CHECKERS_COLOR_WHITE;
  game->state.winner = CHECKERS_WINNER_NONE;
  game->print_state = game_print_state;
  game->available_moves = game_list_available_moves;
}

void game_destroy(Game *game) {
  if (!game) {
    g_debug("game_destroy received null game\n");
    g_return_if_fail(game != NULL);
  }

  free(game->history);
  game->history = NULL;
  game->history_size = 0;
  game->history_capacity = 0;
}

static bool promote_needed(CheckersPiece piece, int row, uint8_t board_size) {
  if (piece == CHECKERS_PIECE_WHITE_MAN && row == 0) {
    return true;
  }
  if (piece == CHECKERS_PIECE_BLACK_MAN && row == board_size - 1) {
    return true;
  }
  return false;
}

static void remove_captured(Game *game, const CheckersMove *move) {
  if (!game || !move) {
    g_debug("remove_captured received invalid arguments\n");
    g_return_if_fail(game != NULL);
    g_return_if_fail(move != NULL);
  }
  if (move->captures == 0) {
    return;
  }

  GameState *state = &game->state;
  for (uint8_t i = 1; i < move->length; ++i) {
    uint8_t from = move->path[i - 1];
    uint8_t to = move->path[i];
    int from_row = 0;
    int from_col = 0;
    int to_row = 0;
    int to_col = 0;
    board_coord_from_index(from, &from_row, &from_col, game->rules.board_size);
    board_coord_from_index(to, &to_row, &to_col, game->rules.board_size);

    int dr = (to_row > from_row) ? 1 : -1;
    int dc = (to_col > from_col) ? 1 : -1;
    for (int r = from_row + dr, c = from_col + dc; r != to_row && c != to_col; r += dr, c += dc) {
      int8_t mid_index = board_index_from_coord(r, c, game->rules.board_size);
      if (mid_index < 0) {
        continue;
      }
      CheckersPiece mid_piece = board_get(&state->board, (uint8_t)mid_index);
      if (mid_piece != CHECKERS_PIECE_EMPTY) {
        board_set(&state->board, (uint8_t)mid_index, CHECKERS_PIECE_EMPTY);
        break;
      }
    }
  }
}

static void update_winner(Game *game) {
  if (!game) {
    g_debug("update_winner received null game\n");
    g_return_if_fail(game != NULL);
  }

  MoveList moves = game_list_available_moves(game);
  if (moves.count == 0) {
    game->state.winner =
        game->state.turn == CHECKERS_COLOR_WHITE ? CHECKERS_WINNER_BLACK : CHECKERS_WINNER_WHITE;
  }
  movelist_free(&moves);
}

int game_apply_move(Game *game, const CheckersMove *move) {
  if (!game || !move || move->length < 2) {
    g_debug("game_apply_move received invalid arguments\n");
    g_return_val_if_fail(game != NULL, -1);
    g_return_val_if_fail(move != NULL, -1);
    g_return_val_if_fail(move->length >= 2, -1);
  }
  if (game->state.winner != CHECKERS_WINNER_NONE) {
    g_debug("game_apply_move called after game ended\n");
    return -1;
  }
  CheckersPiece piece = board_get(&game->state.board, move->path[0]);
  if (piece == CHECKERS_PIECE_EMPTY || board_piece_color(piece) != game->state.turn) {
    g_debug("game_apply_move called with wrong player piece\n");
    return -1;
  }

  GameState *state = &game->state;
  board_set(&state->board, move->path[0], CHECKERS_PIECE_EMPTY);
  uint8_t destination = move->path[move->length - 1];
  int dest_row = 0;
  int dest_col = 0;
  board_coord_from_index(destination, &dest_row, &dest_col, game->rules.board_size);
  if (promote_needed(piece, dest_row, game->rules.board_size)) {
    piece = board_piece_color(piece) == CHECKERS_COLOR_WHITE ? CHECKERS_PIECE_WHITE_KING
                                                             : CHECKERS_PIECE_BLACK_KING;
  }
  board_set(&state->board, destination, piece);
  remove_captured(game, move);

  ensure_capacity(game);
  if (game->history_size < game->history_capacity) {
    game->history[game->history_size++] = *move;
  }

  game->state.turn = game->state.turn == CHECKERS_COLOR_WHITE ? CHECKERS_COLOR_BLACK
                                                              : CHECKERS_COLOR_WHITE;
  update_winner(game);
  return 0;
}
