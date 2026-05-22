#include "ai_search.h"
#include "games/homeworlds/homeworlds_backend.h"
#include "games/homeworlds/homeworlds_game.h"
#include "games/homeworlds/homeworlds_position_text.h"
#include "games/homeworlds/homeworlds_sgf_position.h"
#include "game_app_profile.h"
#include "sgf_io.h"
#include "sgf_move_props.h"

#include <stdio.h>

enum {
  HOMEWORLDS_PROFILE_DEFAULT_DEPTH = 1,
  HOMEWORLDS_PROFILE_TT_SIZE_MB = 256,
};

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

static gboolean homeworlds_profile_apply_replayed_move(HomeworldsPosition *position,
                                                       const SgfNode *node,
                                                       const char *path,
                                                       guint move_number,
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

  g_print("%u. %s\n", move_number + 1, notation);
  return TRUE;
}

static gboolean homeworlds_profile_replay_file_moves(HomeworldsPosition *position,
                                                     const char *path,
                                                     guint requested_moves,
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
  if (!sgf_io_load_file(path, &tree, &error)) {
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

  g_print("Replayed moves from %s (%u requested):\n", path, requested_moves);
  for (guint i = 1; i < nodes->len && applied_moves < requested_moves; ++i) {
    const SgfNode *node = g_ptr_array_index(nodes, i);
    g_return_val_if_fail(node != NULL, FALSE);

    gboolean had_move = FALSE;
    if (!homeworlds_profile_apply_replayed_move(position, node, path, applied_moves, &had_move)) {
      return FALSE;
    }
    if (had_move) {
      applied_moves++;
    }
  }

  *out_applied_moves = applied_moves;
  return TRUE;
}

static gboolean homeworlds_profile_print_ai_analysis(const HomeworldsPosition *position,
                                                     guint applied_moves,
                                                     gboolean replayed,
                                                     guint depth) {
  GameAiScoredMoveList moves = {0};
  GameAiSearchStats stats = {0};
  GameAiTranspositionTable *tt = NULL;

  g_return_val_if_fail(position != NULL, FALSE);

  tt = game_ai_tt_new(HOMEWORLDS_PROFILE_TT_SIZE_MB, homeworlds_game_backend.move_size);
  game_ai_search_stats_clear(&stats);
  if (!game_ai_search_analyze_moves_cancellable_with_tt(&homeworlds_game_backend,
                                                        position,
                                                        depth,
                                                        &moves,
                                                        NULL,
                                                        NULL,
                                                        NULL,
                                                        NULL,
                                                        tt,
                                                        &stats)) {
    if (tt != NULL) {
      game_ai_tt_free(tt);
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
  gint requested_moves = 2;
  gint analysis_depth = HOMEWORLDS_PROFILE_DEFAULT_DEPTH;
  gint seed = 1;
  g_autofree gchar *file_path = NULL;
  GOptionEntry options[] = {
    {
      .long_name = "moves",
      .short_name = 'n',
      .flags = 0,
      .arg = G_OPTION_ARG_INT,
      .arg_data = &requested_moves,
      .description = "Number of moves to apply before running AI analysis",
      .arg_description = "N",
    },
    {
      .long_name = "depth",
      .short_name = 'd',
      .flags = 0,
      .arg = G_OPTION_ARG_INT,
      .arg_data = &analysis_depth,
      .description = "AI search depth to run after reaching the requested move",
      .arg_description = "DEPTH",
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
      .description = "Replay the first moves from an existing Homeworlds SGF file",
      .arg_description = "PATH",
    },
    {0},
  };
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) error = NULL;
  GRand *random = NULL;
  g_autofree char *board = NULL;
  HomeworldsPosition position = {0};
  guint applied_moves = 0;

  context = g_option_context_new("- generate or replay a Homeworlds position and run AI analysis");
  g_option_context_add_main_entries(context, options, NULL);
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("%s\n", error->message);
    return 2;
  }
  if (requested_moves < 0) {
    g_printerr("--moves must be non-negative.\n");
    return 2;
  }
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

  homeworlds_position_init(&position);
  if (file_path != NULL) {
    if (!homeworlds_profile_replay_file_moves(&position,
                                              file_path,
                                              (guint)requested_moves,
                                              &applied_moves)) {
      return 1;
    }
  } else {
    random = g_rand_new_with_seed((guint32) seed);
    g_print("Generated moves (%d requested):\n", requested_moves);
    for (gint i = 0; i < requested_moves; ++i) {
      if (!homeworlds_profile_apply_random_good_move(&position, random, applied_moves)) {
        break;
      }
      applied_moves++;
    }
  }

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

  if (!homeworlds_profile_print_ai_analysis(&position,
                                            applied_moves,
                                            file_path != NULL,
                                            (guint)analysis_depth)) {
    if (random != NULL) {
      g_rand_free(random);
    }
    return 1;
  }
  if (random != NULL) {
    g_rand_free(random);
  }
  return 0;
}
