#include "../src/create_puzzles_runner.h"
#include "../src/games/boop/boop_backend.h"
#include "../src/games/checkers/checkers_backend.h"
#include "../src/sgf_io.h"
#include "../src/sgf_tree.h"
#include "test_profile_utils.h"

#include <glib.h>
#include <stdio.h>

typedef struct {
  const GameBackend *expected_backend;
  const GameBackendVariant *expected_variant;
  guint moves_seen;
  guint stop_after;
} TestRunnerAnalyzeState;

static gboolean test_runner_count_move(const GGameCreatePuzzlesMoveContext *context,
                                       gpointer user_data,
                                       gboolean *out_stop,
                                       GError ** /*error*/) {
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(user_data != NULL, FALSE);
  g_return_val_if_fail(out_stop != NULL, FALSE);

  TestRunnerAnalyzeState *state = user_data;
  g_assert_true(context->backend == state->expected_backend);
  g_assert_true(context->variant == state->expected_variant);
  g_assert_nonnull(context->source_tree);
  g_assert_nonnull(context->main_line);
  g_assert_cmpuint(context->node_index, ==, state->moves_seen + 1);
  g_assert_cmpuint(context->move_number, ==, state->moves_seen + 1);
  g_assert_nonnull(context->position_before);
  g_assert_nonnull(context->played_move);
  g_assert_nonnull(context->position_after);

  state->moves_seen++;
  *out_stop = state->stop_after > 0 && state->moves_seen >= state->stop_after;
  return TRUE;
}

static void test_runner_generation_attempt_limit(void) {
  g_assert_cmpuint(ggame_create_puzzles_runner_generation_attempt_limit(1), ==, 20);
  g_assert_cmpuint(ggame_create_puzzles_runner_generation_attempt_limit(2), ==, 40);
  g_assert_cmpuint(ggame_create_puzzles_runner_generation_attempt_limit(G_MAXUINT / 20u + 1u), ==, G_MAXUINT);
}

static void test_runner_generates_and_analyzes_boop_self_play_tree(void) {
  GGameCreatePuzzlesRunnerConfig config = {
    .backend = &boop_game_backend,
    .variant = NULL,
    .self_play_depth = 0,
    .max_self_play_plies = 4,
  };
  guint plies = 0;
  GameBackendOutcome outcome = GAME_BACKEND_OUTCOME_ONGOING;
  g_autoptr(GError) error = NULL;
  g_autoptr(SgfTree) tree =
      ggame_create_puzzles_runner_generate_self_play_tree(&config, &plies, &outcome, &error);
  g_assert_no_error(error);
  g_assert_nonnull(tree);
  g_assert_cmpuint(plies, ==, 4);
  g_assert_cmpint(outcome, ==, GAME_BACKEND_OUTCOME_ONGOING);

  const SgfNode *root = sgf_tree_get_root(tree);
  g_assert_nonnull(root);
  g_assert_null(sgf_node_get_property_first(root, "RU"));
  g_assert_cmpstr(sgf_node_get_property_first(root, "GBKS"), ==, "8");
  g_assert_cmpstr(sgf_node_get_property_first(root, "GBCS"), ==, "0");
  g_assert_cmpstr(sgf_node_get_property_first(root, "GWKS"), ==, "8");
  g_assert_cmpstr(sgf_node_get_property_first(root, "GWCS"), ==, "0");
  g_assert_cmpstr(sgf_node_get_property_first(root, "PL"), ==, "B");

  g_autoptr(GPtrArray) main_line = sgf_tree_build_main_line(tree);
  g_assert_nonnull(main_line);
  g_assert_cmpuint(main_line->len, ==, plies + 1);

  TestRunnerAnalyzeState full_replay = {
    .expected_backend = &boop_game_backend,
    .expected_variant = NULL,
  };
  g_assert_true(ggame_create_puzzles_runner_analyze_tree(&config,
                                                         tree,
                                                         test_runner_count_move,
                                                         &full_replay,
                                                         &error));
  g_assert_no_error(error);
  g_assert_cmpuint(full_replay.moves_seen, ==, plies);

  TestRunnerAnalyzeState stopped_replay = {
    .expected_backend = &boop_game_backend,
    .expected_variant = NULL,
    .stop_after = 2,
  };
  g_assert_true(ggame_create_puzzles_runner_analyze_tree(&config,
                                                         tree,
                                                         test_runner_count_move,
                                                         &stopped_replay,
                                                         &error));
  g_assert_no_error(error);
  g_assert_cmpuint(stopped_replay.moves_seen, ==, 2);
}

static void test_runner_generates_and_analyzes_checkers_self_play_tree(void) {
  const GameBackendVariant *variant = checkers_game_backend.variant_by_short_name("international");
  g_assert_nonnull(variant);

  GGameCreatePuzzlesRunnerConfig config = {
    .backend = &checkers_game_backend,
    .variant = variant,
    .self_play_depth = 0,
    .max_self_play_plies = 2,
  };
  guint plies = 0;
  GameBackendOutcome outcome = GAME_BACKEND_OUTCOME_ONGOING;
  g_autoptr(GError) error = NULL;
  g_autoptr(SgfTree) tree =
      ggame_create_puzzles_runner_generate_self_play_tree(&config, &plies, &outcome, &error);
  g_assert_no_error(error);
  g_assert_nonnull(tree);
  g_assert_cmpuint(plies, ==, 2);
  g_assert_cmpint(outcome, ==, GAME_BACKEND_OUTCOME_ONGOING);

  const SgfNode *root = sgf_tree_get_root(tree);
  g_assert_nonnull(root);
  g_assert_cmpstr(sgf_node_get_property_first(root, "RU"), ==, "international");
  g_assert_nonnull(sgf_node_get_property_first(root, "PL"));

  g_autoptr(GPtrArray) main_line = sgf_tree_build_main_line(tree);
  g_assert_nonnull(main_line);
  g_assert_cmpuint(main_line->len, ==, plies + 1);

  TestRunnerAnalyzeState replay = {
    .expected_backend = &checkers_game_backend,
    .expected_variant = variant,
  };
  g_assert_true(ggame_create_puzzles_runner_analyze_tree(&config,
                                                         tree,
                                                         test_runner_count_move,
                                                         &replay,
                                                         &error));
  g_assert_no_error(error);
  g_assert_cmpuint(replay.moves_seen, ==, plies);
}

int main(int argc, char **argv) {
  ggame_test_init_profile(&argc, &argv, "boop");
  const GGameAppProfile *profile = ggame_active_app_profile();
  g_return_val_if_fail(profile != NULL, 1);

  test_runner_generation_attempt_limit();

  if (profile->kind == GGAME_APP_KIND_BOOP) {
    test_runner_generates_and_analyzes_boop_self_play_tree();
  } else if (profile->kind == GGAME_APP_KIND_CHECKERS) {
    test_runner_generates_and_analyzes_checkers_self_play_tree();
  } else {
    g_test_skip("Requires boop or checkers profile");
    printf("All tests passed.\n");
    return 0;
  }

  printf("All tests passed.\n");
  return 0;
}
