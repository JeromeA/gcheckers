#include "ai_search.h"
#include "games/homeworlds/homeworlds_backend.h"
#include "games/homeworlds/homeworlds_game.h"
#include "games/homeworlds/homeworlds_position_text.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  HOMEWORLDS_EXPERIMENT_DEPTH = 1,
  HOMEWORLDS_EXPERIMENT_DEFAULT_GAMES = 100,
  HOMEWORLDS_EXPERIMENT_DEFAULT_MAX_PLIES = 300,
  HOMEWORLDS_EXPERIMENT_BIG_MOVE_REPORT_THRESHOLD = 7000000,
  HOMEWORLDS_EXPERIMENT_BIG_MOVE_REPORT_MIN_TOTAL_MOVES = 90000000,
};

static const char *HOMEWORLDS_EXPERIMENT_BIG_MOVE_REPORT_THRESHOLD_ENV =
    "GCHECKERS_HOMEWORLDS_BIG_MOVE_REPORT_THRESHOLD";
static const char *HOMEWORLDS_EXPERIMENT_BIG_MOVE_REPORT_MIN_TOTAL_MOVES_ENV =
    "GCHECKERS_HOMEWORLDS_BIG_MOVE_REPORT_MIN_TOTAL_MOVES";

typedef enum {
  HOMEWORLDS_EXPERIMENT_VARIABLE_NONE = 0,
  HOMEWORLDS_EXPERIMENT_VARIABLE_SHIP_1,
  HOMEWORLDS_EXPERIMENT_VARIABLE_SHIP_2,
  HOMEWORLDS_EXPERIMENT_VARIABLE_SHIP_3,
  HOMEWORLDS_EXPERIMENT_VARIABLE_HOMEWORLD_SHIP_1,
  HOMEWORLDS_EXPERIMENT_VARIABLE_HOMEWORLD_SHIP_2,
  HOMEWORLDS_EXPERIMENT_VARIABLE_HOMEWORLD_SHIP_3,
  HOMEWORLDS_EXPERIMENT_VARIABLE_SINGLE_STAR,
  HOMEWORLDS_EXPERIMENT_VARIABLE_BUILDABLE_COLOR,
} HomeworldsExperimentVariable;

typedef struct {
  guint candidate_wins;
  guint baseline_wins;
  guint draws;
  guint timeouts;
} HomeworldsExperimentStats;

typedef struct {
  gint value;
  guint game;
  guint seed;
  guint candidate_side;
  guint ply;
  const GArray *played_moves;
  gboolean trace_move_counts;
} HomeworldsExperimentMoveTraceContext;

typedef struct {
  FILE *file;
  gsize count;
} HomeworldsExperimentMoveDumpContext;

static guint homeworlds_experiment_big_move_report_counter = 1;

static gboolean homeworlds_experiment_parse_int(const char *text, gint *out_value) {
  char *end = NULL;
  gint64 value = 0;

  g_return_val_if_fail(text != NULL, FALSE);
  g_return_val_if_fail(out_value != NULL, FALSE);

  errno = 0;
  value = g_ascii_strtoll(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || value < G_MININT || value > G_MAXINT) {
    return FALSE;
  }

  *out_value = (gint)value;
  return TRUE;
}

static gboolean homeworlds_experiment_parse_gsize(const char *text, gsize *out_value) {
  guint64 value = 0;
  char *end = NULL;

  g_return_val_if_fail(text != NULL, FALSE);
  g_return_val_if_fail(out_value != NULL, FALSE);

  if (*text == '\0') {
    return FALSE;
  }

  errno = 0;
  value = g_ascii_strtoull(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || value > G_MAXSIZE) {
    return FALSE;
  }

  *out_value = (gsize)value;
  return TRUE;
}

static gsize homeworlds_experiment_big_move_report_threshold(void) {
  const char *threshold_text = g_getenv(HOMEWORLDS_EXPERIMENT_BIG_MOVE_REPORT_THRESHOLD_ENV);
  gsize threshold = HOMEWORLDS_EXPERIMENT_BIG_MOVE_REPORT_THRESHOLD;

  if (threshold_text == NULL) {
    return threshold;
  }
  if (!homeworlds_experiment_parse_gsize(threshold_text, &threshold)) {
    g_debug("Ignoring invalid %s value", HOMEWORLDS_EXPERIMENT_BIG_MOVE_REPORT_THRESHOLD_ENV);
    return HOMEWORLDS_EXPERIMENT_BIG_MOVE_REPORT_THRESHOLD;
  }
  return threshold;
}

static gsize homeworlds_experiment_big_move_report_min_total_moves(void) {
  const char *min_total_moves_text = g_getenv(HOMEWORLDS_EXPERIMENT_BIG_MOVE_REPORT_MIN_TOTAL_MOVES_ENV);
  gsize min_total_moves = HOMEWORLDS_EXPERIMENT_BIG_MOVE_REPORT_MIN_TOTAL_MOVES;

  if (min_total_moves_text == NULL) {
    return min_total_moves;
  }
  if (!homeworlds_experiment_parse_gsize(min_total_moves_text, &min_total_moves)) {
    g_debug("Ignoring invalid %s value", HOMEWORLDS_EXPERIMENT_BIG_MOVE_REPORT_MIN_TOTAL_MOVES_ENV);
    return HOMEWORLDS_EXPERIMENT_BIG_MOVE_REPORT_MIN_TOTAL_MOVES;
  }
  return min_total_moves;
}

static char *homeworlds_experiment_next_big_move_report_path(void) {
  while (homeworlds_experiment_big_move_report_counter < G_MAXUINT) {
    g_autofree char *path =
        g_strdup_printf("big_move_report_%03u.txt", homeworlds_experiment_big_move_report_counter++);

    if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
      return g_steal_pointer(&path);
    }
  }

  g_debug("Homeworlds big move report counter exhausted");
  return NULL;
}

static gboolean homeworlds_experiment_dump_streamed_move(gconstpointer move_data, gpointer user_data) {
  const HomeworldsMove *move = move_data;
  HomeworldsExperimentMoveDumpContext *context = user_data;
  char notation[128] = {0};

  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(context->file != NULL, FALSE);

  context->count++;
  if (!homeworlds_move_format(move, notation, sizeof(notation))) {
    g_debug("Failed to format streamed Homeworlds move for big move report");
    return fprintf(context->file, "%" G_GSIZE_FORMAT ". <unformattable move>\n", context->count) >= 0;
  }

  return fprintf(context->file, "%" G_GSIZE_FORMAT ". %s\n", context->count, notation) >= 0;
}

static void homeworlds_experiment_write_played_moves(FILE *file, const GArray *played_moves) {
  g_return_if_fail(file != NULL);

  fprintf(file, "moves:\n");
  if (played_moves == NULL || played_moves->len == 0) {
    fprintf(file, "<none>\n\n");
    return;
  }

  for (guint i = 0; i < played_moves->len; ++i) {
    const HomeworldsMove *move = &g_array_index(played_moves, HomeworldsMove, i);
    char notation[128] = {0};

    if (!homeworlds_move_format(move, notation, sizeof(notation))) {
      fprintf(file, "%u. <unformattable move>\n", i + 1);
      continue;
    }

    fprintf(file, "%u. %s\n", i + 1, notation);
  }
  fprintf(file, "\n");
}

static GArray *homeworlds_experiment_parse_values(const char *text) {
  GArray *values = NULL;
  g_auto(GStrv) parts = NULL;

  g_return_val_if_fail(text != NULL, NULL);

  values = g_array_new(FALSE, FALSE, sizeof(gint));
  parts = g_strsplit(text, ",", -1);
  for (guint i = 0; parts[i] != NULL; ++i) {
    char *part = g_strstrip(parts[i]);
    gint value = 0;

    if (part[0] == '\0' || !homeworlds_experiment_parse_int(part, &value)) {
      g_array_free(values, TRUE);
      return NULL;
    }

    g_array_append_val(values, value);
  }

  if (values->len == 0) {
    g_array_free(values, TRUE);
    return NULL;
  }
  return values;
}

static HomeworldsExperimentVariable homeworlds_experiment_parse_variable(const char *text) {
  g_return_val_if_fail(text != NULL, HOMEWORLDS_EXPERIMENT_VARIABLE_NONE);

  if (g_strcmp0(text, "ship1") == 0) {
    return HOMEWORLDS_EXPERIMENT_VARIABLE_SHIP_1;
  }
  if (g_strcmp0(text, "ship2") == 0) {
    return HOMEWORLDS_EXPERIMENT_VARIABLE_SHIP_2;
  }
  if (g_strcmp0(text, "ship3") == 0) {
    return HOMEWORLDS_EXPERIMENT_VARIABLE_SHIP_3;
  }
  if (g_strcmp0(text, "homeworld-ship1") == 0) {
    return HOMEWORLDS_EXPERIMENT_VARIABLE_HOMEWORLD_SHIP_1;
  }
  if (g_strcmp0(text, "homeworld-ship2") == 0) {
    return HOMEWORLDS_EXPERIMENT_VARIABLE_HOMEWORLD_SHIP_2;
  }
  if (g_strcmp0(text, "homeworld-ship3") == 0) {
    return HOMEWORLDS_EXPERIMENT_VARIABLE_HOMEWORLD_SHIP_3;
  }
  if (g_strcmp0(text, "single-star") == 0) {
    return HOMEWORLDS_EXPERIMENT_VARIABLE_SINGLE_STAR;
  }
  if (g_strcmp0(text, "buildable-color") == 0) {
    return HOMEWORLDS_EXPERIMENT_VARIABLE_BUILDABLE_COLOR;
  }
  return HOMEWORLDS_EXPERIMENT_VARIABLE_NONE;
}

static const char *homeworlds_experiment_variable_name(HomeworldsExperimentVariable variable) {
  switch (variable) {
    case HOMEWORLDS_EXPERIMENT_VARIABLE_SHIP_1:
      return "ship1";
    case HOMEWORLDS_EXPERIMENT_VARIABLE_SHIP_2:
      return "ship2";
    case HOMEWORLDS_EXPERIMENT_VARIABLE_SHIP_3:
      return "ship3";
    case HOMEWORLDS_EXPERIMENT_VARIABLE_HOMEWORLD_SHIP_1:
      return "homeworld-ship1";
    case HOMEWORLDS_EXPERIMENT_VARIABLE_HOMEWORLD_SHIP_2:
      return "homeworld-ship2";
    case HOMEWORLDS_EXPERIMENT_VARIABLE_HOMEWORLD_SHIP_3:
      return "homeworld-ship3";
    case HOMEWORLDS_EXPERIMENT_VARIABLE_SINGLE_STAR:
      return "single-star";
    case HOMEWORLDS_EXPERIMENT_VARIABLE_BUILDABLE_COLOR:
      return "buildable-color";
    case HOMEWORLDS_EXPERIMENT_VARIABLE_NONE:
    default:
      return "unknown";
  }
}

static void homeworlds_experiment_apply_variable(HomeworldsEvalWeights *weights,
                                                 HomeworldsExperimentVariable variable,
                                                 gint value) {
  g_return_if_fail(weights != NULL);

  switch (variable) {
    case HOMEWORLDS_EXPERIMENT_VARIABLE_SHIP_1:
      weights->ship_values[HOMEWORLDS_SIZE_SMALL] = value;
      break;
    case HOMEWORLDS_EXPERIMENT_VARIABLE_SHIP_2:
      weights->ship_values[HOMEWORLDS_SIZE_MEDIUM] = value;
      break;
    case HOMEWORLDS_EXPERIMENT_VARIABLE_SHIP_3:
      weights->ship_values[HOMEWORLDS_SIZE_LARGE] = value;
      break;
    case HOMEWORLDS_EXPERIMENT_VARIABLE_HOMEWORLD_SHIP_1:
      weights->homeworld_ship_values[HOMEWORLDS_SIZE_SMALL] = value;
      break;
    case HOMEWORLDS_EXPERIMENT_VARIABLE_HOMEWORLD_SHIP_2:
      weights->homeworld_ship_values[HOMEWORLDS_SIZE_MEDIUM] = value;
      break;
    case HOMEWORLDS_EXPERIMENT_VARIABLE_HOMEWORLD_SHIP_3:
      weights->homeworld_ship_values[HOMEWORLDS_SIZE_LARGE] = value;
      break;
    case HOMEWORLDS_EXPERIMENT_VARIABLE_SINGLE_STAR:
      weights->single_star_homeworld_penalty = value;
      break;
    case HOMEWORLDS_EXPERIMENT_VARIABLE_BUILDABLE_COLOR:
      weights->buildable_color_value = value;
      break;
    case HOMEWORLDS_EXPERIMENT_VARIABLE_NONE:
    default:
      g_return_if_reached();
  }
}

static GameBackendOutcome homeworlds_experiment_no_move_outcome(const HomeworldsPosition *position) {
  g_return_val_if_fail(position != NULL, GAME_BACKEND_OUTCOME_ONGOING);

  return homeworlds_position_turn(position) == 0
      ? GAME_BACKEND_OUTCOME_SIDE_1_WIN
      : GAME_BACKEND_OUTCOME_SIDE_0_WIN;
}

static void homeworlds_experiment_write_big_move_report(const HomeworldsGoodMoveTrace *trace,
                                                        const HomeworldsExperimentMoveTraceContext *context) {
  g_autofree char *path = NULL;
  g_autofree char *ascii = NULL;
  FILE *file = NULL;
  HomeworldsExperimentMoveDumpContext dump_context = {0};
  gboolean streamed = FALSE;
  gsize min_total_moves = 0;

  g_return_if_fail(trace != NULL);
  g_return_if_fail(trace->position != NULL);
  g_return_if_fail(context != NULL);

  path = homeworlds_experiment_next_big_move_report_path();
  if (path == NULL) {
    return;
  }

  file = fopen(path, "w");
  if (file == NULL) {
    g_printerr("Failed to open %s for Homeworlds big move report.\n", path);
    return;
  }

  ascii = homeworlds_position_format_ascii(trace->position);
  if (ascii == NULL) {
    g_debug("Failed to format Homeworlds position for big move report");
    fclose(file);
    return;
  }

  fprintf(file, "value: %d\n", context->value);
  fprintf(file, "game: %u\n", context->game);
  fprintf(file, "seed: %u\n", context->seed);
  fprintf(file, "candidate_side: %u\n", context->candidate_side);
  fprintf(file, "ply: %u\n", context->ply);
  fprintf(file, "side: %u\n", trace->side);
  fprintf(file, "depth_hint: %u\n", trace->depth_hint);
  fprintf(file, "good_moves_generated: %" G_GSIZE_FORMAT "\n", trace->generated_leaves);
  fprintf(file, "good_moves_scored: %" G_GSIZE_FORMAT "\n", trace->scored_moves);
  fprintf(file, "good_moves_kept: %" G_GSIZE_FORMAT "\n\n", trace->kept_moves);
  homeworlds_experiment_write_played_moves(file, context->played_moves);
  fprintf(file, "position:\n%s", ascii);
  if (!g_str_has_suffix(ascii, "\n")) {
    fprintf(file, "\n");
  }
  fprintf(file, "\nall_moves:\n");

  dump_context.file = file;
  streamed = homeworlds_game_backend.stream_moves(trace->position,
                                                  homeworlds_experiment_dump_streamed_move,
                                                  &dump_context);
  fprintf(file, "\nall_moves_streamed: %" G_GSIZE_FORMAT "\n", dump_context.count);
  if (!streamed) {
    fprintf(file, "all_moves_stream_error: true\n");
  }

  if (fclose(file) != 0 || !streamed) {
    g_printerr("Failed to finish %s Homeworlds big move report.\n", path);
    return;
  }

  min_total_moves = homeworlds_experiment_big_move_report_min_total_moves();
  if (dump_context.count < min_total_moves) {
    if (g_remove(path) != 0) {
      g_printerr("Failed to delete %s Homeworlds big move report with only %" G_GSIZE_FORMAT " total moves.\n",
                 path,
                 dump_context.count);
      return;
    }

    return;
  }
}

static void homeworlds_experiment_trace_move_generation(const HomeworldsGoodMoveTrace *trace, gpointer user_data) {
  const HomeworldsExperimentMoveTraceContext *context = user_data;

  g_return_if_fail(trace != NULL);
  g_return_if_fail(context != NULL);

  if (context->trace_move_counts) {
    g_printerr("move-count,%d,%u,%u,%u,%u,%u,%u,%" G_GSIZE_FORMAT ",%" G_GSIZE_FORMAT ",%" G_GSIZE_FORMAT "\n",
               context->value,
               context->game,
               context->seed,
               context->candidate_side,
               context->ply,
               trace->side,
               trace->depth_hint,
               trace->generated_leaves,
               trace->scored_moves,
               trace->kept_moves);
  }

  if (trace->generated_leaves > homeworlds_experiment_big_move_report_threshold()) {
    homeworlds_experiment_write_big_move_report(trace, context);
  }
}

static gboolean homeworlds_experiment_choose_move(const HomeworldsPosition *position,
                                                  const HomeworldsEvalWeights *weights,
                                                  GRand *random,
                                                  HomeworldsMove *out_move,
                                                  const HomeworldsExperimentMoveTraceContext *trace_context) {
  GameAiScoredMoveList moves = {0};
  gint best_score = 0;
  gsize best_count = 0;
  gsize selected_index = 0;
  gboolean found = FALSE;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(weights != NULL, FALSE);
  g_return_val_if_fail(random != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  g_return_val_if_fail(trace_context != NULL, FALSE);

  homeworlds_backend_set_good_move_trace(homeworlds_experiment_trace_move_generation, (gpointer)trace_context);
  homeworlds_eval_weights_set_active(weights);
  found = game_ai_search_analyze_moves(&homeworlds_game_backend,
                                       position,
                                       HOMEWORLDS_EXPERIMENT_DEPTH,
                                       &moves);
  homeworlds_eval_weights_reset_active();
  homeworlds_backend_set_good_move_trace(NULL, NULL);
  if (!found) {
    return FALSE;
  }

  best_score = moves.moves[0].score;
  while (best_count < moves.count && moves.moves[best_count].score == best_score) {
    best_count++;
  }
  if (best_count == 0 || best_count > G_MAXINT32) {
    game_ai_scored_move_list_free(&moves);
    return FALSE;
  }

  selected_index = (gsize)g_rand_int_range(random, 0, (gint32)best_count);
  memcpy(out_move, moves.moves[selected_index].move, sizeof(*out_move));
  game_ai_scored_move_list_free(&moves);
  return TRUE;
}

static GameBackendOutcome homeworlds_experiment_play_game(const HomeworldsEvalWeights *baseline,
                                                          const HomeworldsEvalWeights *candidate,
                                                          gint value,
                                                          guint game,
                                                          guint candidate_side,
                                                          guint max_plies,
                                                          guint32 seed,
                                                          gboolean trace_move_counts) {
  HomeworldsPosition position = {0};
  const HomeworldsEvalWeights *side_weights[2] = {0};
  GRand *random = NULL;
  GArray *played_moves = NULL;
  GameBackendOutcome outcome = GAME_BACKEND_OUTCOME_ONGOING;

  g_return_val_if_fail(baseline != NULL, GAME_BACKEND_OUTCOME_ONGOING);
  g_return_val_if_fail(candidate != NULL, GAME_BACKEND_OUTCOME_ONGOING);
  g_return_val_if_fail(candidate_side < 2, GAME_BACKEND_OUTCOME_ONGOING);
  g_return_val_if_fail(max_plies > 0, GAME_BACKEND_OUTCOME_ONGOING);

  random = g_rand_new_with_seed(seed);
  played_moves = g_array_new(FALSE, FALSE, sizeof(HomeworldsMove));
  homeworlds_position_init(&position);
  side_weights[candidate_side] = candidate;
  side_weights[1 - candidate_side] = baseline;

  for (guint ply = 0; ply < max_plies; ++ply) {
    HomeworldsMove move = {0};
    HomeworldsExperimentMoveTraceContext trace_context = {
      .value = value,
      .game = game,
      .seed = seed,
      .candidate_side = candidate_side,
      .ply = ply,
      .played_moves = played_moves,
      .trace_move_counts = trace_move_counts,
    };
    guint side = 0;

    outcome = homeworlds_position_outcome(&position);
    if (outcome != GAME_BACKEND_OUTCOME_ONGOING) {
      break;
    }

    side = homeworlds_position_turn(&position);
    if (!homeworlds_experiment_choose_move(&position,
                                           side_weights[side],
                                           random,
                                           &move,
                                           &trace_context)) {
      outcome = homeworlds_experiment_no_move_outcome(&position);
      break;
    }
    if (!homeworlds_position_apply_move(&position, &move)) {
      g_debug("Homeworlds experiment generated an invalid move");
      break;
    }
    g_array_append_val(played_moves, move);
  }

  if (outcome == GAME_BACKEND_OUTCOME_ONGOING) {
    outcome = homeworlds_position_outcome(&position);
  }

  homeworlds_position_clear(&position);
  g_array_free(played_moves, TRUE);
  g_rand_free(random);
  return outcome;
}

static void homeworlds_experiment_record_outcome(HomeworldsExperimentStats *stats,
                                                 GameBackendOutcome outcome,
                                                 guint candidate_side) {
  g_return_if_fail(stats != NULL);
  g_return_if_fail(candidate_side < 2);

  switch (outcome) {
    case GAME_BACKEND_OUTCOME_SIDE_0_WIN:
      if (candidate_side == 0) {
        stats->candidate_wins++;
      } else {
        stats->baseline_wins++;
      }
      break;
    case GAME_BACKEND_OUTCOME_SIDE_1_WIN:
      if (candidate_side == 1) {
        stats->candidate_wins++;
      } else {
        stats->baseline_wins++;
      }
      break;
    case GAME_BACKEND_OUTCOME_DRAW:
      stats->draws++;
      break;
    case GAME_BACKEND_OUTCOME_ONGOING:
    default:
      stats->timeouts++;
      break;
  }
}

static guint homeworlds_experiment_candidate_side_for_game(guint game) {
  return game % 2;
}

static guint32 homeworlds_experiment_seed_for_game(guint32 seed, guint game) {
  return seed + (game / 2);
}

static char *homeworlds_experiment_win_ratio(const HomeworldsExperimentStats *stats) {
  guint decisive_games = 0;
  char buffer[G_ASCII_DTOSTR_BUF_SIZE] = {0};

  g_return_val_if_fail(stats != NULL, NULL);

  decisive_games = stats->candidate_wins + stats->baseline_wins;
  if (decisive_games == 0) {
    return g_strdup("");
  }

  g_ascii_formatd(buffer, sizeof(buffer), "%.6f", (double)stats->candidate_wins / (double)decisive_games);
  return g_strdup(buffer);
}

static HomeworldsExperimentStats homeworlds_experiment_run_value(const HomeworldsEvalWeights *baseline,
                                                                 const HomeworldsEvalWeights *candidate,
                                                                 gint value,
                                                                 guint games,
                                                                 guint max_plies,
                                                                 guint32 seed,
                                                                 gboolean trace_move_counts) {
  HomeworldsExperimentStats stats = {0};

  g_return_val_if_fail(baseline != NULL, stats);
  g_return_val_if_fail(candidate != NULL, stats);
  g_return_val_if_fail(games > 0, stats);

  for (guint game = 0; game < games; ++game) {
    guint candidate_side = homeworlds_experiment_candidate_side_for_game(game);
    guint32 game_seed = homeworlds_experiment_seed_for_game(seed, game);
    GameBackendOutcome outcome = homeworlds_experiment_play_game(baseline,
                                                                 candidate,
                                                                 value,
                                                                 game,
                                                                 candidate_side,
                                                                 max_plies,
                                                                 game_seed,
                                                                 trace_move_counts);

    homeworlds_experiment_record_outcome(&stats, outcome, candidate_side);
  }
  return stats;
}

int main(int argc, char **argv) {
  gint games_option = HOMEWORLDS_EXPERIMENT_DEFAULT_GAMES;
  gint max_plies_option = HOMEWORLDS_EXPERIMENT_DEFAULT_MAX_PLIES;
  gint seed_option = 1;
  gboolean trace_move_counts_option = FALSE;
  g_autofree gchar *variable_text = NULL;
  g_autofree gchar *values_text = NULL;
  GOptionEntry options[] = {
    {
      .long_name = "variable",
      .short_name = 'v',
      .flags = 0,
      .arg = G_OPTION_ARG_STRING,
      .arg_data = &variable_text,
      .description = "One value to vary: ship1, ship2, ship3, homeworld-ship1, homeworld-ship2, "
                     "homeworld-ship3, single-star, buildable-color",
      .arg_description = "NAME",
    },
    {
      .long_name = "values",
      .short_name = 0,
      .flags = 0,
      .arg = G_OPTION_ARG_STRING,
      .arg_data = &values_text,
      .description = "Comma-separated values for the selected variable",
      .arg_description = "N,N,...",
    },
    {
      .long_name = "games",
      .short_name = 'g',
      .flags = 0,
      .arg = G_OPTION_ARG_INT,
      .arg_data = &games_option,
      .description = "Games to play for each value",
      .arg_description = "N",
    },
    {
      .long_name = "max-plies",
      .short_name = 0,
      .flags = 0,
      .arg = G_OPTION_ARG_INT,
      .arg_data = &max_plies_option,
      .description = "Maximum plies before a game is counted as a timeout",
      .arg_description = "N",
    },
    {
      .long_name = "seed",
      .short_name = 's',
      .flags = 0,
      .arg = G_OPTION_ARG_INT,
      .arg_data = &seed_option,
      .description = "Seed for deterministic tie-breaking",
      .arg_description = "N",
    },
    {
      .long_name = "trace-move-counts",
      .short_name = 0,
      .flags = 0,
      .arg = G_OPTION_ARG_NONE,
      .arg_data = &trace_move_counts_option,
      .description = "Print complete-leaf, scored-move, and kept-move counts for each played ply to stderr",
      .arg_description = NULL,
    },
    {0},
  };
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) error = NULL;
  GArray *values = NULL;
  HomeworldsExperimentVariable variable = HOMEWORLDS_EXPERIMENT_VARIABLE_NONE;
  HomeworldsEvalWeights baseline = *homeworlds_eval_weights_default();

  context = g_option_context_new("- run depth-1 Homeworlds static-evaluation self-play experiments");
  g_option_context_add_main_entries(context, options, NULL);
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("%s\n", error->message);
    return 2;
  }
  if (variable_text == NULL || variable_text[0] == '\0') {
    g_printerr("--variable is required.\n");
    return 2;
  }
  variable = homeworlds_experiment_parse_variable(variable_text);
  if (variable == HOMEWORLDS_EXPERIMENT_VARIABLE_NONE) {
    g_printerr("Unknown --variable '%s'.\n", variable_text);
    return 2;
  }
  if (values_text == NULL || values_text[0] == '\0') {
    g_printerr("--values is required.\n");
    return 2;
  }
  values = homeworlds_experiment_parse_values(values_text);
  if (values == NULL) {
    g_printerr("--values must be a non-empty comma-separated list of integers.\n");
    return 2;
  }
  if (games_option <= 0) {
    g_printerr("--games must be positive.\n");
    g_array_free(values, TRUE);
    return 2;
  }
  if (max_plies_option <= 0) {
    g_printerr("--max-plies must be positive.\n");
    g_array_free(values, TRUE);
    return 2;
  }
  if (seed_option < 0) {
    g_printerr("--seed must be non-negative.\n");
    g_array_free(values, TRUE);
    return 2;
  }

  g_print("variable=%s depth=%u games=%u max-plies=%u seed=%u\n",
          homeworlds_experiment_variable_name(variable),
          HOMEWORLDS_EXPERIMENT_DEPTH,
          (guint)games_option,
          (guint)max_plies_option,
          (guint)seed_option);
  g_print("value,candidate_wins,baseline_wins,win_ratio,draws,timeouts\n");
  if (trace_move_counts_option) {
    g_printerr("move-count,value,game,seed,candidate_side,ply,side,depth_hint,"
               "generated_leaves,scored_moves,kept_moves\n");
  }

  for (guint i = 0; i < values->len; ++i) {
    gint value = g_array_index(values, gint, i);
    HomeworldsEvalWeights candidate = baseline;
    HomeworldsExperimentStats stats = {0};
    g_autofree char *win_ratio = NULL;

    homeworlds_experiment_apply_variable(&candidate, variable, value);
    stats = homeworlds_experiment_run_value(&baseline,
                                            &candidate,
                                            value,
                                            (guint)games_option,
                                            (guint)max_plies_option,
                                            (guint32)seed_option,
                                            trace_move_counts_option);
    win_ratio = homeworlds_experiment_win_ratio(&stats);
    g_print("%d,%u,%u,%s,%u,%u\n",
            value,
            stats.candidate_wins,
            stats.baseline_wins,
            win_ratio,
            stats.draws,
            stats.timeouts);
  }

  homeworlds_eval_weights_reset_active();
  g_array_free(values, TRUE);
  return 0;
}
