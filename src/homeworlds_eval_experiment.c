#include "ai_search.h"
#include "games/homeworlds/homeworlds_backend.h"
#include "games/homeworlds/homeworlds_game.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

enum {
  HOMEWORLDS_EXPERIMENT_DEPTH = 1,
  HOMEWORLDS_EXPERIMENT_DEFAULT_GAMES = 100,
  HOMEWORLDS_EXPERIMENT_DEFAULT_MAX_PLIES = 300,
};

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
} HomeworldsExperimentMoveTraceContext;

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

static void homeworlds_experiment_trace_move_generation(const HomeworldsGoodMoveTrace *trace, gpointer user_data) {
  const HomeworldsExperimentMoveTraceContext *context = user_data;

  g_return_if_fail(trace != NULL);
  g_return_if_fail(context != NULL);

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

static gboolean homeworlds_experiment_choose_move(const HomeworldsPosition *position,
                                                  const HomeworldsEvalWeights *weights,
                                                  GRand *random,
                                                  HomeworldsMove *out_move,
                                                  const HomeworldsExperimentMoveTraceContext *trace_context) {
  GameAiScoredMoveList moves = {0};
  gint best_score = 0;
  gsize best_count = 0;
  gsize selected_index = 0;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(weights != NULL, FALSE);
  g_return_val_if_fail(random != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  if (trace_context != NULL) {
    homeworlds_backend_set_good_move_trace(homeworlds_experiment_trace_move_generation, (gpointer)trace_context);
  }
  homeworlds_eval_weights_set_active(weights);
  gboolean found = game_ai_search_analyze_moves(&homeworlds_game_backend,
                                                position,
                                                HOMEWORLDS_EXPERIMENT_DEPTH,
                                                &moves);
  homeworlds_eval_weights_reset_active();
  if (trace_context != NULL) {
    homeworlds_backend_set_good_move_trace(NULL, NULL);
  }
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
  GameBackendOutcome outcome = GAME_BACKEND_OUTCOME_ONGOING;

  g_return_val_if_fail(baseline != NULL, GAME_BACKEND_OUTCOME_ONGOING);
  g_return_val_if_fail(candidate != NULL, GAME_BACKEND_OUTCOME_ONGOING);
  g_return_val_if_fail(candidate_side < 2, GAME_BACKEND_OUTCOME_ONGOING);
  g_return_val_if_fail(max_plies > 0, GAME_BACKEND_OUTCOME_ONGOING);

  random = g_rand_new_with_seed(seed);
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
                                           trace_move_counts ? &trace_context : NULL)) {
      outcome = homeworlds_experiment_no_move_outcome(&position);
      break;
    }
    if (!homeworlds_position_apply_move(&position, &move)) {
      g_debug("Homeworlds experiment generated an invalid move");
      break;
    }
  }

  if (outcome == GAME_BACKEND_OUTCOME_ONGOING) {
    outcome = homeworlds_position_outcome(&position);
  }

  homeworlds_position_clear(&position);
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
      .description = "One value to vary: ship1, ship2, ship3, single-star, buildable-color",
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
  g_print("value,candidate_wins,baseline_wins,draws,timeouts\n");
  if (trace_move_counts_option) {
    g_printerr("move-count,value,game,seed,candidate_side,ply,side,depth_hint,"
               "generated_leaves,scored_moves,kept_moves\n");
  }

  for (guint i = 0; i < values->len; ++i) {
    gint value = g_array_index(values, gint, i);
    HomeworldsEvalWeights candidate = baseline;
    HomeworldsExperimentStats stats = {0};

    homeworlds_experiment_apply_variable(&candidate, variable, value);
    stats = homeworlds_experiment_run_value(&baseline,
                                            &candidate,
                                            value,
                                            (guint)games_option,
                                            (guint)max_plies_option,
                                            (guint32)seed_option,
                                            trace_move_counts_option);
    g_print("%d,%u,%u,%u,%u\n",
            value,
            stats.candidate_wins,
            stats.baseline_wins,
            stats.draws,
            stats.timeouts);
  }

  homeworlds_eval_weights_reset_active();
  g_array_free(values, TRUE);
  return 0;
}
