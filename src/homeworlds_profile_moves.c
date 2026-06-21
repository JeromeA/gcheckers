#include "ai_search.h"
#include "games/homeworlds/homeworlds_backend.h"
#include "games/homeworlds/homeworlds_game.h"
#include "games/homeworlds/homeworlds_move_report.h"
#include "games/homeworlds/homeworlds_position_text.h"
#include "games/homeworlds/homeworlds_sgf_position.h"
#include "game_app_profile.h"
#include "game_text_io.h"
#include "sgf_io.h"
#include "sgf_move_props.h"

#include <errno.h>
#include <stdio.h>

#ifdef G_OS_UNIX
#include <unistd.h>
#endif

enum {
  HOMEWORLDS_PROFILE_DEFAULT_DEPTH = 1,
  HOMEWORLDS_PROFILE_PROGRESS_INTERVAL_USEC = 250 * 1000,
  HOMEWORLDS_PROFILE_TT_SIZE_MB = 256,
};

#define HOMEWORLDS_PROFILE_PROGRESS_ENV "GCHECKERS_HOMEWORLDS_PROFILE_PROGRESS"

typedef struct {
  const GameAiSearchStats *stats;
  guint64 node_limit;
  gint64 next_report_time_us;
  gboolean enabled;
  gboolean visible;
  gboolean node_limit_reached;
} HomeworldsProfileAnalysisProgress;

static gint homeworlds_profile_requested_moves = 2;
static gboolean homeworlds_profile_requested_moves_set = FALSE;
static guint64 homeworlds_profile_node_limit = 0;

static gboolean homeworlds_profile_parse_moves_option(const gchar * /*option_name*/,
                                                      const gchar *value,
                                                      gpointer /*data*/,
                                                      GError **error) {
  guint64 parsed_value = 0;
  char *end_ptr = NULL;

  if (value == NULL || *value == '\0') {
    g_set_error_literal(error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE, "--moves must be non-negative.");
    return FALSE;
  }
  for (const char *cursor = value; *cursor != '\0'; ++cursor) {
    if (!g_ascii_isdigit(*cursor)) {
      g_set_error_literal(error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE, "--moves must be non-negative.");
      return FALSE;
    }
  }

  parsed_value = g_ascii_strtoull(value, &end_ptr, 10);
  if (end_ptr == value || end_ptr == NULL || *end_ptr != '\0' || parsed_value > G_MAXINT) {
    g_set_error_literal(error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE, "--moves must be non-negative.");
    return FALSE;
  }

  homeworlds_profile_requested_moves = (gint)parsed_value;
  homeworlds_profile_requested_moves_set = TRUE;
  return TRUE;
}

static gboolean homeworlds_profile_parse_node_limit_option(const gchar * /*option_name*/,
                                                           const gchar *value,
                                                           gpointer /*data*/,
                                                           GError **error) {
  guint64 parsed_value = 0;
  char *end_ptr = NULL;

  if (value == NULL || *value == '\0') {
    g_set_error_literal(error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE, "--node-limit must be positive.");
    return FALSE;
  }
  for (const char *cursor = value; *cursor != '\0'; ++cursor) {
    if (!g_ascii_isdigit(*cursor)) {
      g_set_error_literal(error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE, "--node-limit must be positive.");
      return FALSE;
    }
  }

  errno = 0;
  parsed_value = g_ascii_strtoull(value, &end_ptr, 10);
  if (errno != 0 || end_ptr == value || end_ptr == NULL || *end_ptr != '\0' || parsed_value == 0) {
    g_set_error_literal(error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE, "--node-limit must be positive.");
    return FALSE;
  }

  homeworlds_profile_node_limit = parsed_value;
  return TRUE;
}

static gboolean homeworlds_profile_stderr_is_terminal(void) {
#ifdef G_OS_UNIX
  return isatty(fileno(stderr));
#else
  return FALSE;
#endif
}

static gboolean homeworlds_profile_progress_setting_is_true(const char *text) {
  g_return_val_if_fail(text != NULL, FALSE);

  return g_ascii_strcasecmp(text, "1") == 0 ||
      g_ascii_strcasecmp(text, "true") == 0 ||
      g_ascii_strcasecmp(text, "yes") == 0 ||
      g_ascii_strcasecmp(text, "always") == 0;
}

static gboolean homeworlds_profile_progress_setting_is_false(const char *text) {
  g_return_val_if_fail(text != NULL, FALSE);

  return g_ascii_strcasecmp(text, "0") == 0 ||
      g_ascii_strcasecmp(text, "false") == 0 ||
      g_ascii_strcasecmp(text, "no") == 0 ||
      g_ascii_strcasecmp(text, "never") == 0;
}

static gboolean homeworlds_profile_progress_is_enabled(void) {
  const char *progress_text = g_getenv(HOMEWORLDS_PROFILE_PROGRESS_ENV);

  if (progress_text == NULL || progress_text[0] == '\0' ||
      g_ascii_strcasecmp(progress_text, "auto") == 0) {
    return homeworlds_profile_stderr_is_terminal();
  }
  if (homeworlds_profile_progress_setting_is_true(progress_text)) {
    return TRUE;
  }
  if (homeworlds_profile_progress_setting_is_false(progress_text)) {
    return FALSE;
  }

  g_debug("Ignoring invalid %s value", HOMEWORLDS_PROFILE_PROGRESS_ENV);
  return homeworlds_profile_stderr_is_terminal();
}

static HomeworldsProfileAnalysisProgress homeworlds_profile_analysis_progress_new(const GameAiSearchStats *stats,
                                                                                  guint64 node_limit) {
  HomeworldsProfileAnalysisProgress progress = {
    .stats = stats,
    .node_limit = node_limit,
    .next_report_time_us = 0,
    .enabled = homeworlds_profile_progress_is_enabled(),
    .visible = FALSE,
    .node_limit_reached = FALSE,
  };

  return progress;
}

static void homeworlds_profile_analysis_progress_update(const GameAiSearchStats *stats, gpointer user_data) {
  HomeworldsProfileAnalysisProgress *progress = user_data;
  gint64 now_us = 0;

  g_return_if_fail(stats != NULL);
  g_return_if_fail(progress != NULL);

  if (!progress->enabled) {
    return;
  }

  now_us = g_get_monotonic_time();
  if (progress->visible && now_us < progress->next_report_time_us) {
    return;
  }

  g_printerr("\r\033[2Khomeworlds_profile_moves: nodes=%" G_GUINT64_FORMAT, stats->nodes);
  fflush(stderr);
  progress->visible = TRUE;
  progress->next_report_time_us = now_us + HOMEWORLDS_PROFILE_PROGRESS_INTERVAL_USEC;
}

static void homeworlds_profile_analysis_progress_clear(HomeworldsProfileAnalysisProgress *progress) {
  g_return_if_fail(progress != NULL);

  if (!progress->enabled || !progress->visible) {
    return;
  }

  g_printerr("\r\033[2K");
  fflush(stderr);
  progress->visible = FALSE;
}

static gboolean homeworlds_profile_analysis_should_cancel(gpointer user_data) {
  HomeworldsProfileAnalysisProgress *progress = user_data;

  g_return_val_if_fail(progress != NULL, FALSE);

  if (progress->node_limit == 0 || progress->stats == NULL || progress->stats->nodes < progress->node_limit) {
    return FALSE;
  }

  progress->node_limit_reached = TRUE;
  return TRUE;
}

static gboolean homeworlds_profile_apply_random_good_move(HomeworldsPosition *position,
                                                          GRand *random,
                                                          guint move_number,
                                                          gboolean print_move,
                                                          GArray *played_moves) {
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
    homeworlds_game_backend.move_list_free(&moves);
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

  if (played_moves != NULL) {
    g_array_append_val(played_moves, selected);
  }
  if (print_move) {
    g_print("%u. %s\n", move_number + 1, notation);
  }
  return TRUE;
}

static gboolean homeworlds_profile_apply_replayed_move(HomeworldsPosition *position,
                                                       const SgfNode *node,
                                                       const char *path,
                                                       guint move_number,
                                                       gboolean print_move,
                                                       GArray *played_moves,
                                                       gboolean *out_had_move) {
  SgfColor color = SGF_COLOR_NONE;
  SgfColor expected_color = SGF_COLOR_NONE;
  HomeworldsMove move = {0};
  gboolean has_move = FALSE;
  guint side = 0;
  char notation[128] = {0};
  g_autoptr(GError) error = NULL;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(out_had_move != NULL, FALSE);

  *out_had_move = FALSE;
  if (!sgf_move_props_try_parse_node(node, &color, &move, &has_move, &error)) {
    g_printerr("Failed to parse move %u from %s: %s\n",
               move_number + 1,
               path,
               error != NULL ? error->message : "unknown error");
    return FALSE;
  }
  if (!has_move) {
    return TRUE;
  }
  *out_had_move = TRUE;

  side = homeworlds_position_turn(position);
  expected_color = homeworlds_game_backend.sgf_color_for_side(side);
  if (color != expected_color) {
    g_printerr("Move %u in %s does not match the side to move.\n", move_number + 1, path);
    return FALSE;
  }

  if (!homeworlds_move_format(&move, notation, sizeof(notation))) {
    g_printerr("Failed to format move %u from %s.\n", move_number + 1, path);
    return FALSE;
  }
  if (!homeworlds_position_apply_move(position, &move)) {
    g_printerr("Illegal move %u in %s: %s\n", move_number + 1, path, notation);
    return FALSE;
  }

  if (played_moves != NULL) {
    g_array_append_val(played_moves, move);
  }
  if (print_move) {
    g_print("%u. %s\n", move_number + 1, notation);
  }
  return TRUE;
}

static gboolean homeworlds_profile_replay_file_moves(HomeworldsPosition *position,
                                                     const char *path,
                                                     gboolean replay_all_moves,
                                                     guint requested_moves,
                                                     gboolean print_moves,
                                                     GArray *played_moves,
                                                     guint *out_applied_moves) {
  g_autoptr(SgfTree) tree = NULL;
  g_autoptr(GPtrArray) nodes = NULL;
  g_autoptr(GError) error = NULL;
  const GameBackendVariant *variant = NULL;
  const SgfNode *root = NULL;
  guint applied_moves = 0;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(out_applied_moves != NULL, FALSE);

  *out_applied_moves = 0;
  if (ggame_text_game_io_backend_supports_path(&homeworlds_game_backend, path)) {
    if (!ggame_text_game_io_load_file(&homeworlds_game_backend, NULL, path, &tree, &error)) {
      g_printerr("Failed to load %s: %s\n", path, error != NULL ? error->message : "unknown error");
      return FALSE;
    }
  } else if (!sgf_io_load_file(path, &tree, &error)) {
    g_printerr("Failed to load %s: %s\n", path, error != NULL ? error->message : "unknown error");
    return FALSE;
  }

  if (!sgf_io_tree_get_variant(tree, &variant, &error) || variant != NULL) {
    g_printerr("Invalid Homeworlds SGF metadata in %s: %s\n",
               path,
               error != NULL ? error->message : "unexpected variant");
    return FALSE;
  }

  root = sgf_tree_get_root(tree);
  g_return_val_if_fail(root != NULL, FALSE);
  if (!homeworlds_sgf_position_apply_setup_node(position, root, &error)) {
    g_printerr("Failed to apply Homeworlds setup from %s: %s\n",
               path,
               error != NULL ? error->message : "unknown error");
    return FALSE;
  }

  nodes = sgf_tree_build_main_line(tree);
  if (nodes == NULL || nodes->len == 0) {
    g_printerr("No main line in %s.\n", path);
    return FALSE;
  }

  if (print_moves) {
    if (replay_all_moves) {
      g_print("Replayed moves from %s (all moves requested):\n", path);
    } else {
      g_print("Replayed moves from %s (%u requested):\n", path, requested_moves);
    }
  }
  for (guint i = 1; i < nodes->len && (replay_all_moves || applied_moves < requested_moves); ++i) {
    const SgfNode *node = g_ptr_array_index(nodes, i);
    g_return_val_if_fail(node != NULL, FALSE);

    gboolean had_move = FALSE;
    if (!homeworlds_profile_apply_replayed_move(position,
                                                node,
                                                path,
                                                applied_moves,
                                                print_moves,
                                                played_moves,
                                                &had_move)) {
      return FALSE;
    }
    if (had_move) {
      applied_moves++;
    }
  }

  *out_applied_moves = applied_moves;
  return TRUE;
}

static gboolean homeworlds_profile_print_move_report(const HomeworldsPosition *position,
                                                     const GArray *played_moves,
                                                     guint applied_moves,
                                                     gboolean replayed) {
  gsize all_move_count = 0;

  g_return_val_if_fail(position != NULL, FALSE);

  g_print("Move report after %u %s moves:\n",
          applied_moves,
          replayed ? "replayed" : "generated");
  if (!homeworlds_move_report_write(stdout, position, played_moves, &all_move_count)) {
    g_printerr("Failed to write move report.\n");
    return FALSE;
  }
  return TRUE;
}

static gboolean homeworlds_profile_print_ai_analysis(const HomeworldsPosition *position,
                                                     guint applied_moves,
                                                     gboolean replayed,
                                                     guint depth,
                                                     guint64 node_limit) {
  GameAiScoredMoveList moves = {0};
  GameAiSearchStats stats = {0};
  HomeworldsProfileAnalysisProgress progress = {0};
  GameAiTranspositionTable *tt = NULL;
  gboolean success = FALSE;

  g_return_val_if_fail(position != NULL, FALSE);

  tt = game_ai_tt_new(HOMEWORLDS_PROFILE_TT_SIZE_MB, homeworlds_game_backend.move_size);
  game_ai_search_stats_clear(&stats);
  progress = homeworlds_profile_analysis_progress_new(&stats, node_limit);
  success = game_ai_search_analyze_moves_cancellable_with_tt(&homeworlds_game_backend,
                                                             position,
                                                             depth,
                                                             &moves,
                                                             node_limit != 0
                                                                 ? homeworlds_profile_analysis_should_cancel
                                                                 : NULL,
                                                             &progress,
                                                             progress.enabled
                                                                 ? homeworlds_profile_analysis_progress_update
                                                                 : NULL,
                                                             &progress,
                                                             tt,
                                                             &stats);
  homeworlds_profile_analysis_progress_clear(&progress);
  if (!success) {
    if (tt != NULL) {
      game_ai_tt_free(tt);
    }
    if (progress.node_limit_reached) {
      g_print("\nAI analysis stopped after %" G_GUINT64_FORMAT " nodes (limit %" G_GUINT64_FORMAT
              ") at depth %u.\n",
              stats.nodes,
              node_limit,
              depth);
      g_print("TT probes: %" G_GUINT64_FORMAT "\n", stats.tt_probes);
      g_print("TT hits: %" G_GUINT64_FORMAT "\n", stats.tt_hits);
      g_print("TT cutoffs: %" G_GUINT64_FORMAT "\n", stats.tt_cutoffs);
      return TRUE;
    }
    g_printerr("Failed to analyze moves at depth %u.\n", depth);
    return FALSE;
  }

  g_print("\nAI analysis after %u %s moves at depth %u:\n",
          applied_moves,
          replayed ? "replayed" : "generated",
          depth);
  g_print("Nodes: %" G_GUINT64_FORMAT "\n", stats.nodes);
  g_print("TT probes: %" G_GUINT64_FORMAT "\n", stats.tt_probes);
  g_print("TT hits: %" G_GUINT64_FORMAT "\n", stats.tt_hits);
  g_print("TT cutoffs: %" G_GUINT64_FORMAT "\n", stats.tt_cutoffs);
  g_print("Moves: %" G_GSIZE_FORMAT "\n\n", moves.count);

  for (gsize i = 0; i < moves.count; ++i) {
    char notation[128] = {0};
    const HomeworldsMove *move = moves.moves[i].move;

    if (move == NULL || !homeworlds_move_format(move, notation, sizeof(notation))) {
      game_ai_scored_move_list_free(&moves);
      if (tt != NULL) {
        game_ai_tt_free(tt);
      }
      g_printerr("Failed to format analyzed move %" G_GSIZE_FORMAT ".\n", i + 1);
      return FALSE;
    }

    g_print("%" G_GSIZE_FORMAT ". %s score=%d nodes=%" G_GUINT64_FORMAT "\n",
            i + 1,
            notation,
            moves.moves[i].score,
            moves.moves[i].nodes);
  }

  game_ai_scored_move_list_free(&moves);
  if (tt != NULL) {
    game_ai_tt_free(tt);
  }
  return TRUE;
}

int main(int argc, char **argv) {
  gint analysis_depth = HOMEWORLDS_PROFILE_DEFAULT_DEPTH;
  gint seed = 1;
  gboolean show_ai_report = FALSE;
  gboolean show_move_report = FALSE;
  g_autofree gchar *file_path = NULL;
  GOptionEntry options[] = {
    {
      .long_name = "moves",
      .short_name = 'n',
      .flags = 0,
      .arg = G_OPTION_ARG_CALLBACK,
      .arg_data = homeworlds_profile_parse_moves_option,
      .description = "Number of moves to apply before reporting; omit with --file to replay the full main line",
      .arg_description = "N",
    },
    {
      .long_name = "depth",
      .short_name = 'd',
      .flags = 0,
      .arg = G_OPTION_ARG_INT,
      .arg_data = &analysis_depth,
      .description = "AI search depth to run when --ai-report is set",
      .arg_description = "DEPTH",
    },
    {
      .long_name = "node-limit",
      .short_name = 0,
      .flags = 0,
      .arg = G_OPTION_ARG_CALLBACK,
      .arg_data = homeworlds_profile_parse_node_limit_option,
      .description = "Stop AI analysis after N searched nodes",
      .arg_description = "N",
    },
    {
      .long_name = "seed",
      .short_name = 's',
      .flags = 0,
      .arg = G_OPTION_ARG_INT,
      .arg_data = &seed,
      .description = "Random seed when --file is not set",
      .arg_description = "SEED",
    },
    {
      .long_name = "file",
      .short_name = 'f',
      .flags = 0,
      .arg = G_OPTION_ARG_FILENAME,
      .arg_data = &file_path,
      .description = "Replay moves from an existing Homeworlds SGF or text file",
      .arg_description = "PATH",
    },
    {
      .long_name = "ai-report",
      .short_name = 0,
      .flags = 0,
      .arg = G_OPTION_ARG_NONE,
      .arg_data = &show_ai_report,
      .description = "Print AI analysis after reaching the requested position",
      .arg_description = NULL,
    },
    {
      .long_name = "move-report",
      .short_name = 0,
      .flags = 0,
      .arg = G_OPTION_ARG_NONE,
      .arg_data = &show_move_report,
      .description = "Print the Homeworlds move report after reaching the requested position",
      .arg_description = NULL,
    },
    {0},
  };
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) error = NULL;
  GRand *random = NULL;
  g_autofree char *board = NULL;
  g_autoptr(GArray) played_moves = NULL;
  HomeworldsPosition position = {0};
  guint applied_moves = 0;
  gint requested_moves = homeworlds_profile_requested_moves;

  context = g_option_context_new("- generate or replay a Homeworlds position and print reports");
  g_option_context_add_main_entries(context, options, NULL);
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("%s\n", error->message);
    return 2;
  }
  requested_moves = homeworlds_profile_requested_moves;
  if (analysis_depth < 0) {
    g_printerr("--depth must be non-negative.\n");
    return 2;
  }
  if (seed < 0) {
    g_printerr("--seed must be non-negative.\n");
    return 2;
  }
  if (file_path != NULL && file_path[0] == '\0') {
    g_printerr("--file must not be empty.\n");
    return 2;
  }
  if (!ggame_app_profile_set_active_by_id("homeworlds")) {
    g_printerr("Failed to activate the Homeworlds app profile.\n");
    return 1;
  }

  played_moves = g_array_new(FALSE, FALSE, sizeof(HomeworldsMove));
  if (played_moves == NULL) {
    g_printerr("Failed to allocate played move list.\n");
    return 1;
  }

  homeworlds_position_init(&position);
  if (file_path != NULL) {
    if (!homeworlds_profile_replay_file_moves(&position,
                                              file_path,
                                              !homeworlds_profile_requested_moves_set,
                                              (guint)requested_moves,
                                              !show_move_report,
                                              played_moves,
                                              &applied_moves)) {
      return 1;
    }
  } else {
    random = g_rand_new_with_seed((guint32) seed);
    if (!show_move_report) {
      g_print("Generated moves (%d requested):\n", requested_moves);
    }
    for (gint i = 0; i < requested_moves; ++i) {
      if (!homeworlds_profile_apply_random_good_move(&position,
                                                     random,
                                                     applied_moves,
                                                     !show_move_report,
                                                     played_moves)) {
        break;
      }
      applied_moves++;
    }
  }

  if (show_move_report) {
    if (!homeworlds_profile_print_move_report(&position, played_moves, applied_moves, file_path != NULL)) {
      if (random != NULL) {
        g_rand_free(random);
      }
      return 1;
    }
  } else {
    board = homeworlds_position_format_ascii(&position);
    if (board == NULL) {
      g_printerr("Failed to format current position.\n");
      if (random != NULL) {
        g_rand_free(random);
      }
      return 1;
    }

    g_print("\nCurrent position after %u %s moves:\n%s",
            applied_moves,
            file_path != NULL ? "replayed" : "generated",
            board);
    if (!g_str_has_suffix(board, "\n")) {
      g_print("\n");
    }
  }

  if (show_ai_report) {
    if (!homeworlds_profile_print_ai_analysis(&position,
                                              applied_moves,
                                              file_path != NULL,
                                              (guint)analysis_depth,
                                              homeworlds_profile_node_limit)) {
      if (random != NULL) {
        g_rand_free(random);
      }
      return 1;
    }
  }
  if (random != NULL) {
    g_rand_free(random);
  }
  return 0;
}
