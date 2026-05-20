#include "games/homeworlds/homeworlds_backend.h"
#include "games/homeworlds/homeworlds_game.h"
#include "games/homeworlds/homeworlds_move_report.h"

#include <stdio.h>

static gboolean homeworlds_profile_apply_random_good_move(HomeworldsPosition *position,
                                                          GRand *random,
                                                          guint move_number) {
  GameBackendMoveList moves = {0};
  const HomeworldsMove *move = NULL;
  HomeworldsMove selected = {0};
  guint selected_index = 0;
  char notation[128] = {0};

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(random != NULL, FALSE);

  if (position->phase == HOMEWORLDS_PHASE_FINISHED) {
    g_printerr("Game finished after %u moves.\n", move_number);
    return FALSE;
  }

  moves = homeworlds_game_backend.list_good_moves(position, 0);
  if (moves.count == 0) {
    g_printerr("No good moves after %u moves.\n", move_number);
    return FALSE;
  }
  if (moves.count > G_MAXINT32) {
    g_printerr("Too many candidate moves after %u moves.\n", move_number);
    homeworlds_game_backend.move_list_free(&moves);
    return FALSE;
  }

  selected_index = (guint) g_rand_int_range(random, 0, (gint32) moves.count);
  move = homeworlds_game_backend.move_list_get(&moves, selected_index);
  if (move == NULL) {
    g_printerr("Failed to read generated move %u.\n", move_number + 1);
    homeworlds_game_backend.move_list_free(&moves);
    return FALSE;
  }
  selected = *move;
  homeworlds_game_backend.move_list_free(&moves);

  if (!homeworlds_move_format(&selected, notation, sizeof(notation))) {
    g_printerr("Failed to format generated move %u.\n", move_number + 1);
    return FALSE;
  }
  if (!homeworlds_position_apply_move(position, &selected)) {
    g_printerr("Generated illegal move %u: %s\n", move_number + 1, notation);
    return FALSE;
  }

  g_print("%u. %s\n", move_number + 1, notation);
  return TRUE;
}

int main(int argc, char **argv) {
  gint requested_moves = 2;
  gint seed = 1;
  GOptionEntry options[] = {
    {
      .long_name = "moves",
      .short_name = 'n',
      .flags = 0,
      .arg = G_OPTION_ARG_INT,
      .arg_data = &requested_moves,
      .description = "Number of random good moves to apply before printing the report",
      .arg_description = "N",
    },
    {
      .long_name = "seed",
      .short_name = 's',
      .flags = 0,
      .arg = G_OPTION_ARG_INT,
      .arg_data = &seed,
      .description = "Random seed",
      .arg_description = "SEED",
    },
    {0},
  };
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) error = NULL;
  GRand *random = NULL;
  g_autofree char *report = NULL;
  HomeworldsPosition position = {0};
  guint applied_moves = 0;

  context = g_option_context_new("- generate a Homeworlds position and print the move report");
  g_option_context_add_main_entries(context, options, NULL);
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("%s\n", error->message);
    return 2;
  }
  if (requested_moves < 0) {
    g_printerr("--moves must be non-negative.\n");
    return 2;
  }
  if (seed < 0) {
    g_printerr("--seed must be non-negative.\n");
    return 2;
  }

  homeworlds_position_init(&position);
  random = g_rand_new_with_seed((guint32) seed);
  g_print("Generated moves (%d requested):\n", requested_moves);
  for (gint i = 0; i < requested_moves; ++i) {
    if (!homeworlds_profile_apply_random_good_move(&position, random, applied_moves)) {
      break;
    }
    applied_moves++;
  }

  report = homeworlds_move_report_format(&position);
  if (report == NULL) {
    g_printerr("Failed to format move report.\n");
    g_rand_free(random);
    return 1;
  }

  g_print("\nMove report after %u generated moves:\n%s", applied_moves, report);
  if (!g_str_has_suffix(report, "\n")) {
    g_print("\n");
  }
  g_rand_free(random);
  return 0;
}
