#include "game.h"

#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static void print_move(FILE *out, const CheckersMove *move) {
  g_return_if_fail(out != NULL);
  g_return_if_fail(move != NULL);

  char buffer[128];
  if (!game_format_move_notation(move, buffer, sizeof(buffer))) {
    g_debug("Failed to format move notation\n");
    fputs("?", out);
    return;
  }
  fputs(buffer, out);
}

static void list_moves(const MoveList *moves) {
  g_return_if_fail(moves != NULL);

  for (size_t i = 0; i < moves->count; ++i) {
    printf("%zu) ", i + 1);
    print_move(stdout, &moves->moves[i]);
    fputc('\n', stdout);
  }
}

static void set_winner_for_no_moves(Game *game) {
  g_return_if_fail(game != NULL);

  g_debug("No available moves for current player\n");
  game->state.winner =
      game->state.turn == CHECKERS_COLOR_WHITE ? CHECKERS_WINNER_BLACK : CHECKERS_WINNER_WHITE;
}

static int prompt_move_index(size_t move_count) {
  char buffer[64];

  for (;;) {
    printf("Choose a move (1-%zu): ", move_count);
    if (!fgets(buffer, sizeof(buffer), stdin)) {
      g_debug("Failed to read move selection\n");
      return -1;
    }

    char *endptr = NULL;
    long selection = strtol(buffer, &endptr, 10);
    if (endptr == buffer) {
      puts("Please enter a number.");
      continue;
    }
    if (selection < 1 || selection > (long)move_count) {
      puts("Selection out of range.");
      continue;
    }
    return (int)(selection - 1);
  }
}

static bool play_turn_human(Game *game) {
  g_return_val_if_fail(game != NULL, false);

  MoveList moves = game->available_moves(game);
  if (moves.count == 0) {
    set_winner_for_no_moves(game);
    return false;
  }

  puts("Available moves:");
  list_moves(&moves);

  int selection = prompt_move_index(moves.count);
  if (selection < 0) {
    movelist_free(&moves);
    return false;
  }

  if (game_apply_move(game, &moves.moves[selection]) != 0) {
    g_debug("Failed to apply selected move\n");
    movelist_free(&moves);
    return false;
  }

  movelist_free(&moves);
  return true;
}

static bool play_turn_ai(Game *game) {
  g_return_val_if_fail(game != NULL, false);

  MoveList moves = game->available_moves(game);
  if (moves.count == 0) {
    set_winner_for_no_moves(game);
    return false;
  }

  size_t choice = (size_t)(rand() % (int)moves.count);
  printf("AI plays: ");
  print_move(stdout, &moves.moves[choice]);
  fputc('\n', stdout);

  if (game_apply_move(game, &moves.moves[choice]) != 0) {
    g_debug("Failed to apply AI move\n");
    movelist_free(&moves);
    return false;
  }

  movelist_free(&moves);
  return true;
}

int main(int /*argc*/, char **/*argv*/) {
  Game game;
  game_init(&game);
  srand(1);

  while (game.state.winner == CHECKERS_WINNER_NONE) {
    game.print_state(&game, stdout);
    if (game.state.turn == CHECKERS_COLOR_WHITE) {
      if (!play_turn_human(&game)) {
        break;
      }
    } else {
      if (!play_turn_ai(&game)) {
        break;
      }
    }
  }

  puts("Game over.");
  game.print_state(&game, stdout);
  game_destroy(&game);
  return 0;
}
