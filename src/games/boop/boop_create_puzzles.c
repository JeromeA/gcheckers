#include "boop_create_puzzles.h"

#include "active_game_backend.h"
#include "ai_search.h"
#include "boop_backend.h"
#include "boop_game.h"
#include "boop_sgf_position.h"
#include "create_puzzles_progress.h"
#include "create_puzzles_runner.h"
#include "puzzle_catalog.h"
#include "sgf_io.h"
#include "sgf_move_props.h"
#include "sgf_tree.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <string.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GameAiTranspositionTable, game_ai_tt_free)

enum {
  BOOP_PUZZLE_ANALYSIS_TT_SIZE_MB = 64,
  BOOP_PUZZLE_MIN_LEGAL_MOVES = 2,
  BOOP_PUZZLE_SINGLE_MOVE_MARGIN = 50,
  BOOP_PUZZLE_MAX_SELF_PLAY_PLIES = 200,
};

typedef enum {
  BOOP_CREATE_PUZZLES_MODE_GENERATE = 0,
  BOOP_CREATE_PUZZLES_MODE_CHECK_EXISTING,
} BoopCreatePuzzlesMode;

typedef struct {
  BoopCreatePuzzlesMode mode;
  guint depth;
  gboolean save_games;
  gboolean dry_run;
  const char *arg;
} BoopCreatePuzzlesOptions;

typedef enum {
  BOOP_PUZZLE_ARG_INVALID = 0,
  BOOP_PUZZLE_ARG_COUNT,
  BOOP_PUZZLE_ARG_FILE,
} BoopPuzzleArgType;

typedef struct {
  BoopMove move;
  guint side;
} BoopPuzzleLineMove;

typedef struct {
  GameAiScoredMoveList moves;
  gint best_score;
  gint second_score;
  gint static_score;
  BoopMove best_move;
} BoopPuzzlePositionAnalysis;

typedef struct {
  GArray *line;
  gint start_static;
  gint final_static;
  BoopMove solution_move;
} BoopPuzzleValidatedCandidate;

typedef struct {
  guint games_processed;
  guint positions_analyzed;
  guint puzzles_generated;
  guint existing_puzzles_checked;
  guint existing_puzzles_removed;
  guint existing_puzzles_would_remove;
  guint rejected_too_few_moves;
  guint rejected_margin_too_small;
  guint rejected_duplicate;
  guint rejected_invalid_file;
  guint rejected_saved_line_mismatch;
} BoopPuzzleRunStats;

typedef struct {
  guint depth;
  GameAiTranspositionTable *tt;
  gboolean save_games;
  const char *output_dir;
  GHashTable *existing_solution_keys;
  guint *inout_index;
  guint max_total_emitted;
  guint *out_emitted;
} BoopPuzzleEmitContext;

static BoopPuzzleRunStats boop_puzzle_run_stats = {0};

#define boop_puzzle_log_progress ggame_create_puzzles_progress_log

static void boop_puzzle_print_final_report(void) {
  guint rejected_total = boop_puzzle_run_stats.rejected_too_few_moves +
                         boop_puzzle_run_stats.rejected_margin_too_small +
                         boop_puzzle_run_stats.rejected_duplicate +
                         boop_puzzle_run_stats.rejected_invalid_file +
                         boop_puzzle_run_stats.rejected_saved_line_mismatch;

  g_print("Report:\n");
  if (boop_puzzle_run_stats.games_processed > 0) {
    g_print("  games processed: %u\n", boop_puzzle_run_stats.games_processed);
  }
  g_print("  positions analyzed: %u\n", boop_puzzle_run_stats.positions_analyzed);
  if (boop_puzzle_run_stats.existing_puzzles_checked > 0) {
    g_print("  existing puzzles checked: %u\n", boop_puzzle_run_stats.existing_puzzles_checked);
    if (boop_puzzle_run_stats.existing_puzzles_removed > 0) {
      g_print("  existing puzzles removed: %u\n", boop_puzzle_run_stats.existing_puzzles_removed);
    }
    if (boop_puzzle_run_stats.existing_puzzles_would_remove > 0) {
      g_print("  existing puzzles that would be removed: %u\n",
              boop_puzzle_run_stats.existing_puzzles_would_remove);
    }
  }
  g_print("  positions rejected: %u\n", rejected_total);
  if (boop_puzzle_run_stats.rejected_too_few_moves > 0) {
    g_print("    %u: side to move has too few legal moves\n", boop_puzzle_run_stats.rejected_too_few_moves);
  }
  if (boop_puzzle_run_stats.rejected_margin_too_small > 0) {
    g_print("    %u: best move margin is too small\n", boop_puzzle_run_stats.rejected_margin_too_small);
  }
  if (boop_puzzle_run_stats.rejected_duplicate > 0) {
    g_print("    %u: solution matches an existing puzzle\n", boop_puzzle_run_stats.rejected_duplicate);
  }
  if (boop_puzzle_run_stats.rejected_invalid_file > 0) {
    g_print("    %u: failed to load puzzle file\n", boop_puzzle_run_stats.rejected_invalid_file);
  }
  if (boop_puzzle_run_stats.rejected_saved_line_mismatch > 0) {
    g_print("    %u: saved line no longer matches validation\n",
            boop_puzzle_run_stats.rejected_saved_line_mismatch);
  }
  g_print("  puzzles generated: %u\n", boop_puzzle_run_stats.puzzles_generated);
}

static gboolean boop_create_puzzles_parse_depth(const char *arg, guint *out_depth) {
  g_return_val_if_fail(arg != NULL, FALSE);
  g_return_val_if_fail(out_depth != NULL, FALSE);

  gchar *end = NULL;
  guint64 value = g_ascii_strtoull(arg, &end, 10);
  if (end == arg || end == NULL || *end != '\0' || value == 0 || value > G_MAXUINT) {
    return FALSE;
  }

  *out_depth = (guint)value;
  return TRUE;
}

static gboolean boop_create_puzzles_cli_parse(int argc,
                                              char **argv,
                                              guint default_depth,
                                              BoopCreatePuzzlesOptions *out_options,
                                              char **out_error_message) {
  g_return_val_if_fail(argc >= 0, FALSE);
  g_return_val_if_fail(argv != NULL, FALSE);
  g_return_val_if_fail(default_depth > 0, FALSE);
  g_return_val_if_fail(out_options != NULL, FALSE);

  if (out_error_message != NULL) {
    *out_error_message = NULL;
  }

  *out_options = (BoopCreatePuzzlesOptions){
    .mode = BOOP_CREATE_PUZZLES_MODE_GENERATE,
    .depth = default_depth,
    .save_games = FALSE,
    .dry_run = FALSE,
    .arg = NULL,
  };

  for (gint i = 1; i < argc; ++i) {
    if (g_strcmp0(argv[i], "--depth") == 0) {
      if (i + 1 >= argc || !boop_create_puzzles_parse_depth(argv[i + 1], &out_options->depth)) {
        if (out_error_message != NULL) {
          *out_error_message = g_strdup("Invalid --depth value");
        }
        return FALSE;
      }
      i++;
      continue;
    }

    if (g_strcmp0(argv[i], "--save-games") == 0) {
      out_options->save_games = TRUE;
      continue;
    }

    if (g_strcmp0(argv[i], "--check-existing") == 0) {
      out_options->mode = BOOP_CREATE_PUZZLES_MODE_CHECK_EXISTING;
      continue;
    }

    if (g_strcmp0(argv[i], "--dry-run") == 0) {
      out_options->dry_run = TRUE;
      continue;
    }

    if (g_strcmp0(argv[i], "--ruleset") == 0) {
      if (out_error_message != NULL) {
        *out_error_message = g_strdup("--ruleset is not supported for boop puzzles");
      }
      return FALSE;
    }

    if (argv[i][0] == '-') {
      if (out_error_message != NULL) {
        *out_error_message = g_strdup_printf("Unknown option: %s", argv[i]);
      }
      return FALSE;
    }

    if (out_options->arg != NULL) {
      if (out_error_message != NULL) {
        *out_error_message = g_strdup("Expected a single puzzle count or SGF file");
      }
      return FALSE;
    }

    out_options->arg = argv[i];
  }

  if (out_options->mode == BOOP_CREATE_PUZZLES_MODE_CHECK_EXISTING) {
    if (out_options->save_games) {
      if (out_error_message != NULL) {
        *out_error_message = g_strdup("--save-games is only valid when generating puzzles");
      }
      return FALSE;
    }
  } else if (out_options->dry_run) {
    if (out_error_message != NULL) {
      *out_error_message = g_strdup("--dry-run is only valid with --check-existing");
    }
    return FALSE;
  }

  if (out_options->arg == NULL) {
    if (out_options->mode == BOOP_CREATE_PUZZLES_MODE_CHECK_EXISTING) {
      return TRUE;
    }
    if (out_error_message != NULL) {
      *out_error_message = g_strdup("Missing puzzle count or SGF file");
    }
    return FALSE;
  }

  return TRUE;
}

static BoopPuzzleArgType boop_puzzle_parse_arg(const char *arg, guint *out_count) {
  g_return_val_if_fail(arg != NULL, BOOP_PUZZLE_ARG_INVALID);
  g_return_val_if_fail(out_count != NULL, BOOP_PUZZLE_ARG_INVALID);

  if (arg[0] == '\0') {
    return BOOP_PUZZLE_ARG_INVALID;
  }

  gboolean all_digits = TRUE;
  for (const char *p = arg; *p != '\0'; p++) {
    if (!g_ascii_isdigit(*p)) {
      all_digits = FALSE;
      break;
    }
  }

  if (!all_digits) {
    return BOOP_PUZZLE_ARG_FILE;
  }

  gchar *end = NULL;
  guint64 value = g_ascii_strtoull(arg, &end, 10);
  if (end == arg || end == NULL || *end != '\0' || value == 0 || value > G_MAXUINT) {
    return BOOP_PUZZLE_ARG_INVALID;
  }

  *out_count = (guint)value;
  return BOOP_PUZZLE_ARG_COUNT;
}

static char *boop_puzzle_build_output_dir(void) {
  return g_build_filename("puzzles", "boop", NULL);
}

static SgfColor boop_puzzle_sgf_color(guint side) {
  switch (side) {
    case 0:
      return SGF_COLOR_BLACK;
    case 1:
      return SGF_COLOR_WHITE;
    default:
      return SGF_COLOR_NONE;
  }
}

static gint boop_puzzle_score_gap_to_next_best(guint side, gint best_score, gint second_score) {
  g_return_val_if_fail(side <= 1, 0);

  return side == 0 ? best_score - second_score : second_score - best_score;
}

static void boop_puzzle_position_analysis_clear(BoopPuzzlePositionAnalysis *analysis) {
  g_return_if_fail(analysis != NULL);

  game_ai_scored_move_list_free(&analysis->moves);
  memset(analysis, 0, sizeof(*analysis));
}

static void boop_puzzle_validated_candidate_clear(BoopPuzzleValidatedCandidate *candidate) {
  g_return_if_fail(candidate != NULL);

  if (candidate->line != NULL) {
    g_array_unref(candidate->line);
  }
  memset(candidate, 0, sizeof(*candidate));
}

static gboolean boop_puzzle_analyze_position(const BoopPosition *position,
                                             guint depth,
                                             GameAiTranspositionTable *tt,
                                             BoopPuzzlePositionAnalysis *out_analysis) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(depth > 0, FALSE);
  g_return_val_if_fail(tt != NULL, FALSE);
  g_return_val_if_fail(out_analysis != NULL, FALSE);

  *out_analysis = (BoopPuzzlePositionAnalysis){0};
  GameAiSearchStats stats = {0};
  game_ai_search_stats_clear(&stats);
  if (!game_ai_search_analyze_moves_cancellable_with_tt(&boop_game_backend,
                                                        position,
                                                        depth,
                                                        &out_analysis->moves,
                                                        NULL,
                                                        NULL,
                                                        NULL,
                                                        NULL,
                                                        tt,
                                                        &stats)) {
    boop_puzzle_position_analysis_clear(out_analysis);
    return FALSE;
  }

  if (out_analysis->moves.count == 0) {
    boop_puzzle_position_analysis_clear(out_analysis);
    return FALSE;
  }

  const BoopMove *best_move = out_analysis->moves.moves[0].move;
  g_return_val_if_fail(best_move != NULL, FALSE);
  out_analysis->best_move = *best_move;
  out_analysis->best_score = out_analysis->moves.moves[0].score;
  out_analysis->second_score = out_analysis->moves.count > 1
                                   ? out_analysis->moves.moves[1].score
                                   : out_analysis->best_score;
  out_analysis->static_score = boop_position_evaluate_static(position);
  return TRUE;
}

static gboolean boop_puzzle_position_is_valid(const BoopPosition *position,
                                              const BoopPuzzlePositionAnalysis *analysis) {
  guint side = 0;
  gint gap = 0;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(analysis != NULL, FALSE);

  side = boop_position_turn(position);
  if (analysis->moves.count < BOOP_PUZZLE_MIN_LEGAL_MOVES) {
    boop_puzzle_run_stats.rejected_too_few_moves++;
    return FALSE;
  }

  gap = boop_puzzle_score_gap_to_next_best(side, analysis->best_score, analysis->second_score);
  if (gap < BOOP_PUZZLE_SINGLE_MOVE_MARGIN) {
    boop_puzzle_run_stats.rejected_margin_too_small++;
    return FALSE;
  }

  return TRUE;
}

static gboolean boop_puzzle_validate_position(const BoopPosition *position,
                                              guint depth,
                                              GameAiTranspositionTable *tt,
                                              BoopPuzzleValidatedCandidate *out_candidate) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(depth > 0, FALSE);
  g_return_val_if_fail(tt != NULL, FALSE);
  g_return_val_if_fail(out_candidate != NULL, FALSE);

  *out_candidate = (BoopPuzzleValidatedCandidate){0};
  boop_puzzle_run_stats.positions_analyzed++;

  BoopPuzzlePositionAnalysis analysis = {0};
  if (!boop_puzzle_analyze_position(position, depth, tt, &analysis)) {
    return FALSE;
  }
  if (!boop_puzzle_position_is_valid(position, &analysis)) {
    boop_puzzle_position_analysis_clear(&analysis);
    return FALSE;
  }

  BoopPosition final_position = {0};
  boop_position_copy(&final_position, position);
  if (!boop_position_apply_move(&final_position, &analysis.best_move)) {
    boop_puzzle_position_analysis_clear(&analysis);
    return FALSE;
  }

  GArray *line = g_array_new(FALSE, FALSE, sizeof(BoopPuzzleLineMove));
  g_return_val_if_fail(line != NULL, FALSE);
  BoopPuzzleLineMove step = {
    .move = analysis.best_move,
    .side = boop_position_turn(position),
  };
  g_array_append_val(line, step);

  gint final_static = boop_position_evaluate_static(&final_position);
  GameBackendOutcome outcome = boop_position_outcome(&final_position);
  if (outcome != GAME_BACKEND_OUTCOME_ONGOING) {
    final_static = boop_position_terminal_score(outcome, 1);
  }

  out_candidate->line = line;
  out_candidate->start_static = analysis.static_score;
  out_candidate->final_static = final_static;
  out_candidate->solution_move = analysis.best_move;
  boop_puzzle_position_analysis_clear(&analysis);
  return TRUE;
}

static char *boop_puzzle_build_line_solution_key(const GArray *line) {
  g_return_val_if_fail(line != NULL, NULL);

  GString *key = g_string_new(NULL);
  g_return_val_if_fail(key != NULL, NULL);

  for (guint i = 0; i < line->len; ++i) {
    const BoopPuzzleLineMove *step = &g_array_index(line, BoopPuzzleLineMove, i);
    char move_text[128] = {0};
    if (!boop_move_format(&step->move, move_text, sizeof(move_text))) {
      g_string_free(key, TRUE);
      return NULL;
    }
    g_string_append_printf(key, "%u:%s;", step->side, move_text);
  }

  return g_string_free(key, FALSE);
}

static SgfTree *boop_puzzle_build_line_tree(const BoopPosition *start_position,
                                            const GArray *line,
                                            const char *description) {
  g_return_val_if_fail(start_position != NULL, NULL);
  g_return_val_if_fail(line != NULL, NULL);
  g_return_val_if_fail(description != NULL, NULL);

  g_autoptr(SgfTree) tree = sgf_tree_new();
  SgfNode *root = (SgfNode *)sgf_tree_get_root(tree);
  g_return_val_if_fail(root != NULL, NULL);

  g_autoptr(GError) position_error = NULL;
  if (!boop_sgf_position_write_position_node(start_position, root, &position_error)) {
    g_debug("Failed to encode boop %s root: %s",
            description,
            position_error != NULL ? position_error->message : "unknown error");
    return NULL;
  }
  if (!sgf_io_tree_set_variant(tree, NULL)) {
    g_debug("Failed to encode boop %s variant metadata", description);
    return NULL;
  }

  for (guint i = 0; i < line->len; ++i) {
    const BoopPuzzleLineMove *step = &g_array_index(line, BoopPuzzleLineMove, i);
    SgfNode *node = (SgfNode *)sgf_tree_append_node(tree);
    g_return_val_if_fail(node != NULL, NULL);

    g_autoptr(GError) move_error = NULL;
    if (!sgf_move_props_set_move(node, boop_puzzle_sgf_color(step->side), &step->move, &move_error)) {
      g_debug("Failed to set boop %s SGF move: %s",
              description,
              move_error != NULL ? move_error->message : "unknown error");
      return NULL;
    }
  }

  return g_steal_pointer(&tree);
}

static gboolean boop_puzzle_save_sgf(const char *path,
                                     const BoopPosition *start_position,
                                     const GArray *line,
                                     gint start_static,
                                     gint final_static,
                                     guint solution_depth,
                                     const BoopMove *solution_move) {
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(start_position != NULL, FALSE);
  g_return_val_if_fail(line != NULL, FALSE);
  g_return_val_if_fail(line->len > 0, FALSE);
  g_return_val_if_fail(solution_depth > 0, FALSE);
  g_return_val_if_fail(solution_move != NULL, FALSE);

  g_autoptr(SgfTree) tree = boop_puzzle_build_line_tree(start_position, line, "puzzle");
  if (tree == NULL) {
    return FALSE;
  }
  SgfNode *root = (SgfNode *)sgf_tree_get_root(tree);
  g_return_val_if_fail(root != NULL, FALSE);

  char solution_text[128] = {0};
  if (!boop_move_format(solution_move, solution_text, sizeof(solution_text))) {
    g_debug("Failed to format boop puzzle solution move");
    return FALSE;
  }

  g_autofree char *comment = g_strdup_printf("solution_depth=%u start_static=%d final_static=%d solution=%s",
                                             solution_depth,
                                             start_static,
                                             final_static,
                                             solution_text);
  if (!sgf_node_add_property(root, "C", comment)) {
    g_debug("Failed to add boop puzzle comment");
    return FALSE;
  }

  g_autoptr(GError) save_error = NULL;
  if (!sgf_io_save_file(path, tree, &save_error)) {
    g_debug("Failed to save boop puzzle file %s: %s",
            path,
            save_error != NULL ? save_error->message : "unknown error");
    return FALSE;
  }
  return TRUE;
}

static gboolean boop_puzzle_emit_validated_candidate(const BoopPosition *start_position,
                                                     guint solution_depth,
                                                     const BoopPuzzleValidatedCandidate *candidate,
                                                     const GGameCreatePuzzlesMoveContext *move_context,
                                                     BoopPuzzleEmitContext *emit_context,
                                                     char **out_puzzle_path,
                                                     char **out_game_path) {
  g_return_val_if_fail(start_position != NULL, FALSE);
  g_return_val_if_fail(solution_depth > 0, FALSE);
  g_return_val_if_fail(candidate != NULL, FALSE);
  g_return_val_if_fail(candidate->line != NULL, FALSE);
  g_return_val_if_fail(emit_context != NULL, FALSE);
  g_return_val_if_fail(emit_context->output_dir != NULL, FALSE);
  g_return_val_if_fail(emit_context->existing_solution_keys != NULL, FALSE);
  g_return_val_if_fail(emit_context->inout_index != NULL, FALSE);
  g_return_val_if_fail(out_puzzle_path != NULL, FALSE);
  g_return_val_if_fail(out_game_path != NULL, FALSE);

  g_autofree char *solution_key = boop_puzzle_build_line_solution_key(candidate->line);
  g_return_val_if_fail(solution_key != NULL, FALSE);
  if (g_hash_table_contains(emit_context->existing_solution_keys, solution_key)) {
    boop_puzzle_run_stats.rejected_duplicate++;
    return FALSE;
  }

  guint index = *emit_context->inout_index;
  g_autofree char *puzzle_path = game_puzzle_catalog_build_indexed_path(emit_context->output_dir, "puzzle", index);
  g_return_val_if_fail(puzzle_path != NULL, FALSE);
  if (!boop_puzzle_save_sgf(puzzle_path,
                            start_position,
                            candidate->line,
                            candidate->start_static,
                            candidate->final_static,
                            solution_depth,
                            &candidate->solution_move)) {
    return FALSE;
  }

  g_autofree char *game_path = NULL;
  if (emit_context->save_games && move_context != NULL && move_context->source_tree != NULL) {
    game_path = game_puzzle_catalog_build_indexed_path(emit_context->output_dir, "game", index);
    g_return_val_if_fail(game_path != NULL, FALSE);
    g_autoptr(GError) save_error = NULL;
    if (!ggame_create_puzzles_runner_save_source_game(game_path, move_context->source_tree, &save_error)) {
      g_debug("Failed to save boop source game file %s: %s",
              game_path,
              save_error != NULL ? save_error->message : "unknown error");
      return FALSE;
    }
  }

  g_hash_table_add(emit_context->existing_solution_keys, g_steal_pointer(&solution_key));
  *out_puzzle_path = g_steal_pointer(&puzzle_path);
  *out_game_path = g_steal_pointer(&game_path);
  *emit_context->inout_index += 1;
  return TRUE;
}

static gboolean boop_puzzle_emit_candidate_if_valid(const BoopPosition *position,
                                                    const GGameCreatePuzzlesMoveContext *move_context,
                                                    BoopPuzzleEmitContext *emit_context) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(emit_context != NULL, FALSE);
  g_return_val_if_fail(emit_context->depth > 0, FALSE);
  g_return_val_if_fail(emit_context->tt != NULL, FALSE);
  g_return_val_if_fail(emit_context->out_emitted != NULL, FALSE);

  if (boop_position_outcome(position) != GAME_BACKEND_OUTCOME_ONGOING) {
    return TRUE;
  }

  BoopPuzzleValidatedCandidate validated = {0};
  if (!boop_puzzle_validate_position(position, emit_context->depth, emit_context->tt, &validated)) {
    return TRUE;
  }

  g_autofree char *puzzle_path = NULL;
  g_autofree char *game_path = NULL;
  if (!boop_puzzle_emit_validated_candidate(position,
                                            emit_context->depth,
                                            &validated,
                                            move_context,
                                            emit_context,
                                            &puzzle_path,
                                            &game_path)) {
    boop_puzzle_validated_candidate_clear(&validated);
    return TRUE;
  }

  (*emit_context->out_emitted)++;
  boop_puzzle_run_stats.puzzles_generated++;
  boop_puzzle_log_progress("  -> kept: start_static=%d final_static=%d line_plies=%u",
                           validated.start_static,
                           validated.final_static,
                           validated.line->len);
  if (game_path != NULL) {
    boop_puzzle_log_progress("  -> saved %s and %s", puzzle_path, game_path);
  } else {
    boop_puzzle_log_progress("  -> saved %s", puzzle_path);
  }
  boop_puzzle_validated_candidate_clear(&validated);
  return TRUE;
}

static gboolean boop_puzzle_load_position_and_line(const char *path,
                                                   BoopPosition *out_start_position,
                                                   GArray *out_line) {
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(out_start_position != NULL, FALSE);
  g_return_val_if_fail(out_line != NULL, FALSE);

  g_autoptr(SgfTree) tree = NULL;
  g_autoptr(GError) load_error = NULL;
  if (!sgf_io_load_file(path, &tree, &load_error)) {
    g_debug("Failed to load boop SGF file %s: %s",
            path,
            load_error != NULL ? load_error->message : "unknown error");
    return FALSE;
  }

  const GameBackendVariant *variant = NULL;
  g_autoptr(GError) variant_error = NULL;
  if (!sgf_io_tree_get_variant(tree, &variant, &variant_error) || variant != NULL) {
    g_debug("Invalid boop SGF variant metadata in %s: %s",
            path,
            variant_error != NULL ? variant_error->message : "unexpected variant");
    return FALSE;
  }

  const SgfNode *root = sgf_tree_get_root(tree);
  g_return_val_if_fail(root != NULL, FALSE);

  BoopPosition position = {0};
  boop_position_init(&position);
  g_autoptr(GError) setup_error = NULL;
  if (!boop_sgf_position_apply_setup_node(&position, root, &setup_error)) {
    g_debug("Failed to apply boop SGF root setup from %s: %s",
            path,
            setup_error != NULL ? setup_error->message : "unknown error");
    return FALSE;
  }
  boop_position_copy(out_start_position, &position);

  g_autoptr(GPtrArray) nodes = sgf_tree_build_main_line(tree);
  if (nodes == NULL || nodes->len == 0) {
    g_debug("Boop SGF has no main line: %s", path);
    return FALSE;
  }

  for (guint i = 1; i < nodes->len; ++i) {
    const SgfNode *node = g_ptr_array_index(nodes, i);
    g_return_val_if_fail(node != NULL, FALSE);

    guint side = boop_position_turn(&position);
    SgfColor color = SGF_COLOR_NONE;
    BoopMove move = {0};
    gboolean has_move = FALSE;
    g_autoptr(GError) move_error = NULL;
    if (!sgf_move_props_try_parse_node(node, &color, &move, &has_move, &move_error)) {
      g_debug("Failed to parse boop SGF move in %s: %s",
              path,
              move_error != NULL ? move_error->message : "unknown error");
      return FALSE;
    }
    if (!has_move) {
      continue;
    }
    if (color != boop_puzzle_sgf_color(side)) {
      g_debug("Boop SGF move color does not match side to move in %s", path);
      return FALSE;
    }

    BoopPuzzleLineMove step = {
      .move = move,
      .side = side,
    };
    g_array_append_val(out_line, step);

    if (!boop_position_apply_move(&position, &move)) {
      g_debug("Boop SGF move could not be replayed in %s", path);
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean boop_puzzle_lines_equal(const GArray *left, const GArray *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  if (left->len != right->len) {
    return FALSE;
  }

  for (guint i = 0; i < left->len; ++i) {
    const BoopPuzzleLineMove *a = &g_array_index(left, BoopPuzzleLineMove, i);
    const BoopPuzzleLineMove *b = &g_array_index(right, BoopPuzzleLineMove, i);
    if (a->side != b->side || !boop_moves_equal(&a->move, &b->move)) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean boop_puzzle_load_existing_solution_keys(const char *output_dir, GHashTable *out_keys) {
  g_return_val_if_fail(output_dir != NULL, FALSE);
  g_return_val_if_fail(out_keys != NULL, FALSE);

  if (!g_file_test(output_dir, G_FILE_TEST_IS_DIR)) {
    return TRUE;
  }

  g_autoptr(GError) dir_error = NULL;
  g_autoptr(GDir) dir = g_dir_open(output_dir, 0, &dir_error);
  if (dir == NULL) {
    g_debug("Failed to open boop puzzle output dir %s: %s",
            output_dir,
            dir_error != NULL ? dir_error->message : "unknown error");
    return FALSE;
  }

  guint loaded = 0;
  const char *name = NULL;
  while ((name = g_dir_read_name(dir)) != NULL) {
    if (!game_puzzle_catalog_name_is_puzzle_sgf(name)) {
      continue;
    }

    g_autofree char *path = g_build_filename(output_dir, name, NULL);
    BoopPosition start = {0};
    g_autoptr(GArray) line = g_array_new(FALSE, FALSE, sizeof(BoopPuzzleLineMove));
    if (!boop_puzzle_load_position_and_line(path, &start, line)) {
      g_debug("Failed to load existing boop puzzle line from %s", path);
      return FALSE;
    }

    g_autofree char *key = boop_puzzle_build_line_solution_key(line);
    if (key == NULL) {
      g_debug("Failed to build solution key for existing boop puzzle %s", path);
      return FALSE;
    }

    g_hash_table_add(out_keys, g_steal_pointer(&key));
    loaded++;
  }

  boop_puzzle_log_progress("Loaded %u existing puzzle solution keys", loaded);
  return TRUE;
}

static char *boop_puzzle_build_matching_game_path(const char *puzzle_path) {
  g_return_val_if_fail(puzzle_path != NULL, NULL);

  g_autofree char *dir = g_path_get_dirname(puzzle_path);
  g_autofree char *base = g_path_get_basename(puzzle_path);
  if (!game_puzzle_catalog_name_is_puzzle_sgf(base)) {
    return NULL;
  }

  g_autofree char *game_name = g_strdup_printf("game-%s", base + strlen("puzzle-"));
  return g_build_filename(dir, game_name, NULL);
}

static gboolean boop_puzzle_delete_file_if_present(const char *path) {
  g_return_val_if_fail(path != NULL, FALSE);

  if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
    return TRUE;
  }
  if (g_remove(path) != 0) {
    g_debug("Failed to delete %s: %s", path, g_strerror(errno));
    return FALSE;
  }
  return TRUE;
}

static gboolean boop_puzzle_check_existing_file(const char *path,
                                                guint depth,
                                                GameAiTranspositionTable *tt,
                                                gboolean dry_run) {
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(depth > 0, FALSE);
  g_return_val_if_fail(tt != NULL, FALSE);

  boop_puzzle_log_progress("Checking %s...", path);
  boop_puzzle_run_stats.existing_puzzles_checked++;

  BoopPosition start = {0};
  g_autoptr(GArray) saved_line = g_array_new(FALSE, FALSE, sizeof(BoopPuzzleLineMove));
  gboolean loaded = boop_puzzle_load_position_and_line(path, &start, saved_line) && saved_line->len > 0;
  BoopPuzzleValidatedCandidate validated = {0};
  gboolean valid = loaded && boop_puzzle_validate_position(&start, depth, tt, &validated);
  if (valid && !boop_puzzle_lines_equal(saved_line, validated.line)) {
    valid = FALSE;
    boop_puzzle_run_stats.rejected_saved_line_mismatch++;
  }

  if (valid) {
    boop_puzzle_validated_candidate_clear(&validated);
    boop_puzzle_log_progress("  -> kept");
    return TRUE;
  }
  boop_puzzle_validated_candidate_clear(&validated);
  if (!loaded) {
    boop_puzzle_run_stats.rejected_invalid_file++;
  }

  g_autofree char *game_path = boop_puzzle_build_matching_game_path(path);
  if (dry_run) {
    boop_puzzle_run_stats.existing_puzzles_would_remove++;
    boop_puzzle_log_progress("  -> invalid");
    if (game_path != NULL && g_file_test(game_path, G_FILE_TEST_EXISTS)) {
      boop_puzzle_log_progress("     would delete %s", game_path);
    }
    return TRUE;
  }

  boop_puzzle_log_progress("  -> invalid, deleting");
  if (!boop_puzzle_delete_file_if_present(path)) {
    return FALSE;
  }
  boop_puzzle_log_progress("     deleted %s", path);
  if (game_path != NULL && g_file_test(game_path, G_FILE_TEST_EXISTS)) {
    if (!boop_puzzle_delete_file_if_present(game_path)) {
      return FALSE;
    }
    boop_puzzle_log_progress("     deleted %s", game_path);
  }
  boop_puzzle_run_stats.existing_puzzles_removed++;
  return TRUE;
}

static gboolean boop_puzzle_check_existing_dir(const char *dir_path,
                                               guint depth,
                                               GameAiTranspositionTable *tt,
                                               gboolean dry_run) {
  g_return_val_if_fail(dir_path != NULL, FALSE);
  g_return_val_if_fail(depth > 0, FALSE);
  g_return_val_if_fail(tt != NULL, FALSE);

  if (!g_file_test(dir_path, G_FILE_TEST_IS_DIR)) {
    g_debug("Boop puzzle directory does not exist: %s", dir_path);
    return TRUE;
  }

  g_autoptr(GDir) dir = g_dir_open(dir_path, 0, NULL);
  if (dir == NULL) {
    g_debug("Failed to open boop puzzle directory %s", dir_path);
    return FALSE;
  }

  const char *name = NULL;
  while ((name = g_dir_read_name(dir)) != NULL) {
    if (!game_puzzle_catalog_name_is_puzzle_sgf(name)) {
      continue;
    }

    g_autofree char *path = g_build_filename(dir_path, name, NULL);
    if (!boop_puzzle_check_existing_file(path, depth, tt, dry_run)) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean boop_puzzle_consider_runner_move(const GGameCreatePuzzlesMoveContext *move_context,
                                                 gpointer user_data,
                                                 gboolean *out_stop,
                                                 GError ** /*error*/) {
  g_return_val_if_fail(move_context != NULL, FALSE);
  g_return_val_if_fail(user_data != NULL, FALSE);
  g_return_val_if_fail(out_stop != NULL, FALSE);

  BoopPuzzleEmitContext *emit_context = user_data;
  if (*emit_context->out_emitted >= emit_context->max_total_emitted) {
    *out_stop = TRUE;
    return TRUE;
  }

  const BoopPosition *position = move_context->position_before;
  if (!boop_puzzle_emit_candidate_if_valid(position, move_context, emit_context)) {
    return FALSE;
  }

  *out_stop = *emit_context->out_emitted >= emit_context->max_total_emitted;
  return TRUE;
}

static gboolean boop_puzzle_emit_from_tree(SgfTree *tree,
                                           guint depth,
                                           GameAiTranspositionTable *tt,
                                           gboolean save_game_sgf,
                                           const char *output_dir,
                                           GHashTable *existing_solution_keys,
                                           guint *inout_index,
                                           guint max_total_emitted,
                                           guint *out_emitted) {
  g_return_val_if_fail(SGF_IS_TREE(tree), FALSE);
  g_return_val_if_fail(depth > 0, FALSE);
  g_return_val_if_fail(tt != NULL, FALSE);
  g_return_val_if_fail(output_dir != NULL, FALSE);
  g_return_val_if_fail(existing_solution_keys != NULL, FALSE);
  g_return_val_if_fail(inout_index != NULL, FALSE);
  g_return_val_if_fail(out_emitted != NULL, FALSE);

  GGameCreatePuzzlesRunnerConfig runner_config = {
    .backend = &boop_game_backend,
    .variant = NULL,
    .self_play_depth = 0,
    .max_self_play_plies = BOOP_PUZZLE_MAX_SELF_PLAY_PLIES,
  };
  BoopPuzzleEmitContext emit_context = {
    .depth = depth,
    .tt = tt,
    .save_games = save_game_sgf,
    .output_dir = output_dir,
    .existing_solution_keys = existing_solution_keys,
    .inout_index = inout_index,
    .max_total_emitted = max_total_emitted,
    .out_emitted = out_emitted,
  };

  g_autoptr(GError) runner_error = NULL;
  if (!ggame_create_puzzles_runner_analyze_tree(&runner_config,
                                                tree,
                                                boop_puzzle_consider_runner_move,
                                                &emit_context,
                                                &runner_error)) {
    g_debug("Boop source game analysis failed: %s",
            runner_error != NULL ? runner_error->message : "unknown error");
    return FALSE;
  }
  return TRUE;
}

static gboolean boop_puzzle_emit_from_file(const char *path,
                                           guint depth,
                                           GameAiTranspositionTable *tt,
                                           gboolean save_game_sgf,
                                           const char *output_dir,
                                           GHashTable *existing_solution_keys,
                                           guint *inout_index,
                                           guint *out_emitted) {
  g_return_val_if_fail(path != NULL, FALSE);

  g_autoptr(SgfTree) tree = NULL;
  g_autoptr(GError) load_error = NULL;
  if (!sgf_io_load_file(path, &tree, &load_error)) {
    g_debug("Failed to load boop source game file %s: %s",
            path,
            load_error != NULL ? load_error->message : "unknown error");
    return FALSE;
  }

  return boop_puzzle_emit_from_tree(tree,
                                    depth,
                                    tt,
                                    save_game_sgf,
                                    output_dir,
                                    existing_solution_keys,
                                    inout_index,
                                    G_MAXUINT,
                                    out_emitted);
}

static void boop_create_puzzles_print_usage(const char *program_name) {
  g_printerr("Usage: %s [--depth N] [--save-games] <puzzle-count|sgf-file>\n", program_name);
  g_printerr("   or: %s [--depth N] --check-existing [--dry-run] [puzzle-dir]\n", program_name);
}

int boop_create_puzzles_main(int argc, char **argv, guint default_depth) {
  BoopCreatePuzzlesOptions options = {0};
  g_autofree char *parse_error = NULL;

  g_return_val_if_fail(GGAME_ACTIVE_GAME_BACKEND == &boop_game_backend, 1);

  if (!boop_create_puzzles_cli_parse(argc, argv, default_depth, &options, &parse_error)) {
    g_printerr("%s\n", parse_error != NULL ? parse_error : "Invalid arguments");
    boop_create_puzzles_print_usage(argv[0]);
    return 1;
  }

  g_autofree char *output_dir = boop_puzzle_build_output_dir();
  if (output_dir == NULL) {
    g_printerr("Missing boop output directory\n");
    return 1;
  }

  g_autoptr(GameAiTranspositionTable) tt =
      game_ai_tt_new(BOOP_PUZZLE_ANALYSIS_TT_SIZE_MB, boop_game_backend.move_size);
  if (tt == NULL) {
    g_printerr("Failed to allocate shared analysis TT\n");
    return 1;
  }

  if (options.mode == BOOP_CREATE_PUZZLES_MODE_CHECK_EXISTING) {
    const char *dir_path = options.arg != NULL ? options.arg : output_dir;
    if (!boop_puzzle_check_existing_dir(dir_path, options.depth, tt, options.dry_run)) {
      g_printerr("Failed to check existing boop puzzles in %s\n", dir_path);
      return 1;
    }
    boop_puzzle_print_final_report();
    return 0;
  }

  guint wanted = 0;
  BoopPuzzleArgType arg_type = boop_puzzle_parse_arg(options.arg, &wanted);
  if (arg_type == BOOP_PUZZLE_ARG_INVALID) {
    g_printerr("Invalid argument: %s\n", options.arg);
    return 1;
  }

  if (g_mkdir_with_parents(output_dir, 0755) != 0) {
    g_printerr("Failed to create output dir %s: %s\n", output_dir, g_strerror(errno));
    return 1;
  }

  guint next_index = 0;
  g_autoptr(GError) index_error = NULL;
  if (!game_puzzle_catalog_find_next_index(output_dir, &next_index, &index_error)) {
    g_printerr("Failed to compute next puzzle index: %s\n",
               index_error != NULL ? index_error->message : "unknown error");
    return 1;
  }

  g_autoptr(GHashTable) existing_solution_keys = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  if (!boop_puzzle_load_existing_solution_keys(output_dir, existing_solution_keys)) {
    g_printerr("Failed to load existing boop puzzles from %s\n", output_dir);
    return 1;
  }

  guint emitted = 0;
  if (arg_type == BOOP_PUZZLE_ARG_FILE) {
    if (!boop_puzzle_emit_from_file(options.arg,
                                    options.depth,
                                    tt,
                                    options.save_games,
                                    output_dir,
                                    existing_solution_keys,
                                    &next_index,
                                    &emitted)) {
      g_printerr("Failed to extract boop puzzles from file\n");
      return 1;
    }
    g_print("file=%s\n", options.arg);
    boop_puzzle_print_final_report();
    return 0;
  }

  guint attempts = 0;
  guint max_attempts = ggame_create_puzzles_runner_generation_attempt_limit(wanted);
  while (emitted < wanted && attempts < max_attempts) {
    GGameCreatePuzzlesRunnerConfig runner_config = {
      .backend = &boop_game_backend,
      .variant = NULL,
      .self_play_depth = 0,
      .max_self_play_plies = BOOP_PUZZLE_MAX_SELF_PLAY_PLIES,
    };
    g_autoptr(GError) runner_error = NULL;
    g_autoptr(SgfTree) source_tree =
        ggame_create_puzzles_runner_generate_self_play_tree(&runner_config, NULL, NULL, &runner_error);
    if (source_tree == NULL) {
      g_printerr("Boop self-play generation failed: %s\n",
                 runner_error != NULL ? runner_error->message : "unknown error");
      return 1;
    }
    boop_puzzle_run_stats.games_processed++;

    if (!boop_puzzle_emit_from_tree(source_tree,
                                    options.depth,
                                    tt,
                                    options.save_games,
                                    output_dir,
                                    existing_solution_keys,
                                    &next_index,
                                    wanted,
                                    &emitted)) {
      g_printerr("Boop self-play puzzle extraction failed\n");
      return 1;
    }
    attempts++;
  }

  boop_puzzle_print_final_report();
  if (emitted < wanted) {
    g_printerr("Only generated %u of %u requested boop puzzles\n", emitted, wanted);
    return 1;
  }

  return 0;
}
