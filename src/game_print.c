#include "game.h"

#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum { BOARD_SQUARE_BUFFER_SIZE = 16 };

bool game_format_move_notation(const CheckersMove *move, char *buffer, size_t size) {
  g_return_val_if_fail(move != NULL, false);
  g_return_val_if_fail(buffer != NULL, false);
  g_return_val_if_fail(size > 0, false);
  g_return_val_if_fail(move->length >= 2, false);

  size_t offset = 0;
  buffer[0] = '\0';
  for (uint8_t i = 0; i < move->length; ++i) {
    g_return_val_if_fail(move->path[i] < CHECKERS_MAX_SQUARES, false);
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

static void format_subscript_number(char *buffer, size_t size, int number, int *digit_count) {
  g_return_if_fail(buffer != NULL);
  g_return_if_fail(digit_count != NULL);
  g_return_if_fail(size > 0);

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
  g_return_val_if_fail(symbol != NULL, 0);

  gunichar ch = g_utf8_get_char(symbol);
  if (ch >= 0x26C0 && ch <= 0x26C3) {
    return 1;
  }
  return g_unichar_iswide(ch) ? 2 : 1;
}

static void append_padding(char *buffer, size_t size, int count) {
  g_return_if_fail(buffer != NULL);
  g_return_if_fail(size > 0);
  g_return_if_fail(count >= 0);

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
  g_return_if_fail(top != NULL);
  g_return_if_fail(bottom != NULL);
  g_return_if_fail(top_size >= BOARD_SQUARE_BUFFER_SIZE);
  g_return_if_fail(bottom_size >= BOARD_SQUARE_BUFFER_SIZE);

  if (!playable) {
    g_strlcpy(top, "\x1b[7m    \x1b[0m", top_size);
    g_strlcpy(bottom, "\x1b[7m    \x1b[0m", bottom_size);
    return;
  }

  g_return_if_fail(square >= 1 && square <= max_square);

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
  g_return_if_fail(game != NULL);
  if (!out) {
    out = stdout;
  }

  fprintf(out, "Turn: %s\n", game->state.turn == CHECKERS_COLOR_WHITE ? "White" : "Black");
  fprintf(out, "Winner: %s\n", game_winner_label(game->state.winner));

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
      format_board_square(top, sizeof(top), bottom, sizeof(bottom), piece, idx + 1, playable,
                          max_square);
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
      format_board_square(top, sizeof(top), bottom, sizeof(bottom), piece, idx + 1, playable,
                          max_square);
      fputs(bottom, out);
    }
    fputc('\n', out);
  }
}

const char *game_winner_label(CheckersWinner winner) {
  switch (winner) {
    case CHECKERS_WINNER_WHITE:
      return "White";
    case CHECKERS_WINNER_BLACK:
      return "Black";
    case CHECKERS_WINNER_DRAW:
      return "Draw";
    case CHECKERS_WINNER_NONE:
      return "None";
    default:
      g_debug("game_winner_label received unknown winner %d\n", winner);
      return "None";
  }
}
