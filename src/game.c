#include "game.h"

#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool rules_valid(const CheckersRules *rules) {
  if (!rules) {
    return false;
  }
  return rules->board_size == 8 || rules->board_size == 10;
}

static bool is_forward(CheckersColor color, int delta_row) {
  if (color == CHECKERS_COLOR_WHITE) {
    return delta_row == -1;
  }
  return delta_row == 1;
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

bool game_format_move_notation(const CheckersMove *move, char *buffer, size_t size) {
  if (!move || !buffer || size == 0) {
    g_debug("game_format_move_notation received invalid arguments\n");
    g_return_val_if_fail(move != NULL, false);
    g_return_val_if_fail(buffer != NULL, false);
    g_return_val_if_fail(size > 0, false);
  }
  if (move->length < 2) {
    g_debug("game_format_move_notation received too-short move\n");
    g_return_val_if_fail(move->length >= 2, false);
  }

  size_t offset = 0;
  buffer[0] = '\0';
  for (uint8_t i = 0; i < move->length; ++i) {
    if (move->path[i] >= CHECKERS_MAX_SQUARES) {
      g_debug("game_format_move_notation received out-of-range index\n");
      g_return_val_if_fail(move->path[i] < CHECKERS_MAX_SQUARES, false);
    }
    int square = (int)move->path[i] + 1;
    int written = g_snprintf(buffer + offset, size - offset, "%d", square);
    if (written < 0 || (size_t)written >= size - offset) {
      g_debug("game_format_move_notation buffer too small\n");
      return false;
    }
    offset += (size_t)written;

    if (i + 1 < move->length) {
      char separator = move->captures > 0 ? 'x' : '-';
      if (offset + 1 >= size) {
        g_debug("game_format_move_notation buffer too small for separator\n");
        return false;
      }
      buffer[offset++] = separator;
      buffer[offset] = '\0';
    }
  }
  return true;
}

static void append_move(MoveList *list, const CheckersMove *move) {
  if (!list || !move) {
    g_debug("append_move received invalid arguments\n");
    g_return_if_fail(list != NULL);
    g_return_if_fail(move != NULL);
  }

  CheckersMove *expanded = realloc(list->moves, sizeof(CheckersMove) * (list->count + 1));
  if (!expanded) {
    g_debug("Failed to expand move list\n");
    return;
  }
  list->moves = expanded;
  list->moves[list->count] = *move;
  list->count += 1;
}

static void generate_simple_moves(const Game *game, uint8_t index, MoveList *moves) {
  if (!game || !moves) {
    g_debug("generate_simple_moves received invalid arguments\n");
    g_return_if_fail(game != NULL);
    g_return_if_fail(moves != NULL);
  }

  CheckersPiece piece = board_get(&game->state.board, index);
  if (piece == CHECKERS_PIECE_EMPTY) {
    return;
  }

  int row = 0;
  int col = 0;
  board_coord_from_index(index, &row, &col, game->rules.board_size);

  int directions[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  bool is_king = piece == CHECKERS_PIECE_WHITE_KING || piece == CHECKERS_PIECE_BLACK_KING;

  for (size_t i = 0; i < 4; ++i) {
    int dr = directions[i][0];
    int dc = directions[i][1];
    if (!is_king && !is_forward(board_piece_color(piece), dr)) {
      continue;
    }

    int step = 1;
    for (;;) {
      int nr = row + dr * step;
      int nc = col + dc * step;
      int8_t target_index = board_index_from_coord(nr, nc, game->rules.board_size);
      if (target_index < 0) {
        break;
      }
      if (board_get(&game->state.board, (uint8_t)target_index) != CHECKERS_PIECE_EMPTY) {
        break;
      }

      CheckersMove move = {.length = 2, .captures = 0};
      move.path[0] = index;
      move.path[1] = (uint8_t)target_index;
      append_move(moves, &move);

      if (!is_king || !game->rules.kings_can_fly) {
        break;
      }
      step += 1;
    }
  }
}

static void dfs_jumps(const Game *game,
                      uint8_t index,
                      CheckersPiece piece,
                      CheckersMove *partial,
                      MoveList *moves,
                      CheckersBoard *board) {
  if (!game || !partial || !moves || !board) {
    g_debug("dfs_jumps received invalid arguments\n");
    g_return_if_fail(game != NULL);
    g_return_if_fail(partial != NULL);
    g_return_if_fail(moves != NULL);
    g_return_if_fail(board != NULL);
  }

  bool extended = false;
  int row = 0;
  int col = 0;
  board_coord_from_index(index, &row, &col, game->rules.board_size);
  int directions[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  bool is_king = piece == CHECKERS_PIECE_WHITE_KING || piece == CHECKERS_PIECE_BLACK_KING;

  for (size_t i = 0; i < 4; ++i) {
    int dr = directions[i][0];
    int dc = directions[i][1];
    if (!is_king && !game->rules.men_can_jump_backwards && !is_forward(board_piece_color(piece), dr)) {
      continue;
    }

    if (!is_king || !game->rules.kings_can_fly) {
      int mid_r = row + dr;
      int mid_c = col + dc;
      int land_r = row + dr * 2;
      int land_c = col + dc * 2;
      int8_t mid_index = board_index_from_coord(mid_r, mid_c, game->rules.board_size);
      int8_t land_index = board_index_from_coord(land_r, land_c, game->rules.board_size);
      if (mid_index < 0 || land_index < 0) {
        continue;
      }
      CheckersPiece middle_piece = (CheckersPiece)board_get_raw(board, (uint8_t)mid_index);
      if (!board_is_opponent(middle_piece, board_piece_color(piece))) {
        continue;
      }
      if (board_get_raw(board, (uint8_t)land_index) != CHECKERS_PIECE_EMPTY) {
        continue;
      }
      if (partial->length + 1 >= CHECKERS_MAX_MOVE_LENGTH) {
        g_debug("dfs_jumps reached maximum move length\n");
        continue;
      }

      extended = true;
      CheckersBoard next_board = *board;
      board_set_raw(&next_board, index, CHECKERS_PIECE_EMPTY);
      board_set_raw(&next_board, (uint8_t)mid_index, CHECKERS_PIECE_EMPTY);
      board_set_raw(&next_board, (uint8_t)land_index, piece);

      partial->path[partial->length++] = (uint8_t)land_index;
      partial->captures += 1;
      dfs_jumps(game, (uint8_t)land_index, piece, partial, moves, &next_board);
      partial->length -= 1;
      partial->captures -= 1;
      continue;
    }

    bool found_opponent = false;
    uint8_t opponent_index = 0;
    for (int step = 1;; ++step) {
      int scan_r = row + dr * step;
      int scan_c = col + dc * step;
      int8_t scan_index = board_index_from_coord(scan_r, scan_c, game->rules.board_size);
      if (scan_index < 0) {
        break;
      }
      CheckersPiece scan_piece = (CheckersPiece)board_get_raw(board, (uint8_t)scan_index);
      if (scan_piece == CHECKERS_PIECE_EMPTY) {
        if (!found_opponent) {
          continue;
        }
        if (partial->length + 1 >= CHECKERS_MAX_MOVE_LENGTH) {
          g_debug("dfs_jumps reached maximum move length\n");
          continue;
        }
        extended = true;
        CheckersBoard next_board = *board;
        board_set_raw(&next_board, index, CHECKERS_PIECE_EMPTY);
        board_set_raw(&next_board, opponent_index, CHECKERS_PIECE_EMPTY);
        board_set_raw(&next_board, (uint8_t)scan_index, piece);

        partial->path[partial->length++] = (uint8_t)scan_index;
        partial->captures += 1;
        dfs_jumps(game, (uint8_t)scan_index, piece, partial, moves, &next_board);
        partial->length -= 1;
        partial->captures -= 1;
        continue;
      }

      if (board_is_opponent(scan_piece, board_piece_color(piece)) && !found_opponent) {
        found_opponent = true;
        opponent_index = (uint8_t)scan_index;
        continue;
      }
      break;
    }
  }

  if (!extended && partial->captures > 0) {
    append_move(moves, partial);
  }
}

static void generate_jump_moves(const Game *game, uint8_t index, MoveList *moves) {
  if (!game || !moves) {
    g_debug("generate_jump_moves received invalid arguments\n");
    g_return_if_fail(game != NULL);
    g_return_if_fail(moves != NULL);
  }

  CheckersPiece piece = board_get(&game->state.board, index);
  if (piece == CHECKERS_PIECE_EMPTY) {
    return;
  }

  CheckersMove move = {.length = 1, .captures = 0};
  move.path[0] = index;
  CheckersBoard board_copy = game->state.board;
  dfs_jumps(game, index, piece, &move, moves, &board_copy);
}

static void filter_longest_captures(MoveList *moves) {
  if (!moves) {
    g_debug("filter_longest_captures received null moves\n");
    g_return_if_fail(moves != NULL);
  }

  uint8_t max_captures = 0;
  for (size_t i = 0; i < moves->count; ++i) {
    if (moves->moves[i].captures > max_captures) {
      max_captures = moves->moves[i].captures;
    }
  }
  if (max_captures == 0) {
    return;
  }

  size_t write = 0;
  for (size_t i = 0; i < moves->count; ++i) {
    if (moves->moves[i].captures == max_captures) {
      moves->moves[write++] = moves->moves[i];
    }
  }
  moves->count = write;
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

MoveList game_list_available_moves(const Game *game) {
  MoveList moves = {.moves = NULL, .count = 0};
  if (!game) {
    g_debug("game_list_available_moves received null game\n");
    g_return_val_if_fail(game != NULL, moves);
  }
  if (game->state.winner != CHECKERS_WINNER_NONE) {
    return moves;
  }

  uint8_t squares = board_playable_squares(game->rules.board_size);
  for (uint8_t i = 0; i < squares; ++i) {
    CheckersPiece piece = board_get(&game->state.board, i);
    if (piece == CHECKERS_PIECE_EMPTY) {
      continue;
    }
    if (board_piece_color(piece) != game->state.turn) {
      continue;
    }
    generate_jump_moves(game, i, &moves);
  }

  if (moves.count > 0 && game->rules.capture_mandatory) {
    if (game->rules.longest_capture_mandatory) {
      filter_longest_captures(&moves);
    }
    return moves;
  }

  for (uint8_t i = 0; i < squares; ++i) {
    CheckersPiece piece = board_get(&game->state.board, i);
    if (piece == CHECKERS_PIECE_EMPTY) {
      continue;
    }
    if (board_piece_color(piece) != game->state.turn) {
      continue;
    }
    generate_simple_moves(game, i, &moves);
  }

  return moves;
}

void movelist_free(MoveList *list) {
  if (!list) {
    g_debug("movelist_free received null list\n");
    g_return_if_fail(list != NULL);
  }

  free(list->moves);
  list->moves = NULL;
  list->count = 0;
}

static void format_subscript_number(char *buffer, size_t size, int number, int *digit_count) {
  if (!buffer || !digit_count) {
    g_debug("format_subscript_number received invalid arguments\n");
    g_return_if_fail(buffer != NULL);
    g_return_if_fail(digit_count != NULL);
  }
  if (size == 0) {
    g_debug("format_subscript_number received zero buffer size\n");
    g_return_if_fail(size > 0);
  }

  static const char *subscripts[] = {"₀", "₁", "₂", "₃", "₄", "₅", "₆", "₇", "₈", "₉"};

  buffer[0] = '\0';
  if (number >= 10) {
    g_strlcat(buffer, subscripts[number / 10], size);
    g_strlcat(buffer, subscripts[number % 10], size);
    *digit_count = 2;
  } else {
    g_strlcat(buffer, subscripts[number], size);
    *digit_count = 1;
  }
}

static int symbol_display_width(const char *symbol) {
  if (!symbol) {
    return 0;
  }

  gunichar ch = g_utf8_get_char(symbol);
  if (ch >= 0x26C0 && ch <= 0x26C3) {
    return 1;
  }
  return g_unichar_iswide(ch) ? 2 : 1;
}

enum { BOARD_SQUARE_BUFFER_SIZE = 16 };

static void append_padding(char *buffer, size_t size, int count) {
  if (!buffer) {
    g_debug("append_padding received null buffer\n");
    g_return_if_fail(buffer != NULL);
  }
  if (size == 0) {
    g_debug("append_padding received zero buffer size\n");
    g_return_if_fail(size > 0);
  }
  if (count < 0) {
    g_debug("append_padding received negative count %d\n", count);
    g_return_if_fail(count >= 0);
  }

  for (int i = 0; i < count; ++i) {
    g_strlcat(buffer, " ", size);
  }
}

static void format_board_square(char *top,
                                size_t top_size,
                                char *bottom,
                                size_t bottom_size,
                                CheckersPiece piece,
                                int square,
                                bool playable,
                                int max_square) {
  if (!top || !bottom) {
    g_debug("format_board_square received null buffers\n");
    g_return_if_fail(top != NULL);
    g_return_if_fail(bottom != NULL);
  }
  if (top_size < BOARD_SQUARE_BUFFER_SIZE || bottom_size < BOARD_SQUARE_BUFFER_SIZE) {
    g_debug("format_board_square received insufficient buffer sizes\n");
    g_return_if_fail(top_size >= BOARD_SQUARE_BUFFER_SIZE);
    g_return_if_fail(bottom_size >= BOARD_SQUARE_BUFFER_SIZE);
  }

  if (!playable) {
    g_strlcpy(top, "\x1b[7m    \x1b[0m", top_size);
    g_strlcpy(bottom, "\x1b[7m    \x1b[0m", bottom_size);
    return;
  }

  if (square < 1 || square > max_square) {
    g_debug("format_board_square received invalid square %d\n", square);
    g_return_if_fail(square >= 1 && square <= max_square);
    g_strlcpy(top, "    ", top_size);
    g_strlcpy(bottom, "    ", bottom_size);
    return;
  }

  const char *symbol = " ";
  switch (piece) {
    case CHECKERS_PIECE_WHITE_MAN:
      symbol = "⛀";
      break;
    case CHECKERS_PIECE_WHITE_KING:
      symbol = "⛁";
      break;
    case CHECKERS_PIECE_BLACK_MAN:
      symbol = "⛂";
      break;
    case CHECKERS_PIECE_BLACK_KING:
      symbol = "⛃";
      break;
    case CHECKERS_PIECE_EMPTY:
      symbol = " ";
      break;
    default:
      g_debug("format_board_square received unknown piece %d\n", piece);
      symbol = "·";
      break;
  }

  top[0] = '\0';
  g_strlcat(top, " ", top_size);
  g_strlcat(top, symbol, top_size);
  append_padding(top, top_size, 4 - symbol_display_width(symbol) - 1);

  char subscript[8];
  int digit_count = 0;
  format_subscript_number(subscript, sizeof(subscript), square, &digit_count);

  bottom[0] = '\0';
  g_strlcat(bottom, " ", bottom_size);
  g_strlcat(bottom, subscript, bottom_size);
  append_padding(bottom, bottom_size, 4 - digit_count - 1);
}

void game_print_state(const Game *game, FILE *out) {
  if (!game) {
    g_debug("game_print_state received null game\n");
    g_return_if_fail(game != NULL);
  }
  if (!out) {
    out = stdout;
  }

  fprintf(out, "Turn: %s\n", game->state.turn == CHECKERS_COLOR_WHITE ? "White" : "Black");
  fprintf(out, "Winner: ");
  switch (game->state.winner) {
    case CHECKERS_WINNER_WHITE:
      fprintf(out, "White\n");
      break;
    case CHECKERS_WINNER_BLACK:
      fprintf(out, "Black\n");
      break;
    case CHECKERS_WINNER_DRAW:
      fprintf(out, "Draw\n");
      break;
    default:
      fprintf(out, "None\n");
  }

  int max_square = board_playable_squares(game->rules.board_size);
  for (int row = 0; row < game->rules.board_size; ++row) {
    for (int col = 0; col < game->rules.board_size; ++col) {
      bool playable = (row + col) % 2 != 0;
      int8_t idx = playable ? board_index_from_coord(row, col, game->rules.board_size) : -1;
      CheckersPiece piece = CHECKERS_PIECE_EMPTY;
      if (idx >= 0) {
        piece = board_get(&game->state.board, (uint8_t)idx);
      }

      char top[BOARD_SQUARE_BUFFER_SIZE];
      char bottom[BOARD_SQUARE_BUFFER_SIZE];
      format_board_square(top, sizeof(top), bottom, sizeof(bottom), piece, idx + 1, playable, max_square);
      fputs(top, out);
    }
    fputc('\n', out);
    for (int col = 0; col < game->rules.board_size; ++col) {
      bool playable = (row + col) % 2 != 0;
      int8_t idx = playable ? board_index_from_coord(row, col, game->rules.board_size) : -1;
      CheckersPiece piece = CHECKERS_PIECE_EMPTY;
      if (idx >= 0) {
        piece = board_get(&game->state.board, (uint8_t)idx);
      }

      char top[BOARD_SQUARE_BUFFER_SIZE];
      char bottom[BOARD_SQUARE_BUFFER_SIZE];
      format_board_square(top, sizeof(top), bottom, sizeof(bottom), piece, idx + 1, playable, max_square);
      fputs(bottom, out);
    }
    fputc('\n', out);
  }
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
