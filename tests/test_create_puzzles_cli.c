#include "../src/create_puzzles_cli.h"

#include <glib.h>

static void test_create_puzzles_cli_defaults(void) {
  char *argv[] = {
      (char *)"create_puzzles",
      (char *)"12",
  };
  CheckersCreatePuzzlesCliOptions options = {0};
  g_autofree char *error = NULL;

  g_assert_true(checkers_create_puzzles_cli_parse(G_N_ELEMENTS(argv), argv, 8, &options, &error));
  g_assert_no_error((GError *)NULL);
  g_assert_null(error);
  g_assert_cmpuint(options.depth, ==, 8);
  g_assert_false(options.try_forced_mistakes);
  g_assert_cmpstr(options.arg, ==, "12");
}

static void test_create_puzzles_cli_synthetic_candidates_flag(void) {
  char *argv[] = {
      (char *)"create_puzzles",
      (char *)"--synthetic-candidates",
      (char *)"input.sgf",
  };
  CheckersCreatePuzzlesCliOptions options = {0};
  g_autofree char *error = NULL;

  g_assert_true(checkers_create_puzzles_cli_parse(G_N_ELEMENTS(argv), argv, 8, &options, &error));
  g_assert_null(error);
  g_assert_cmpuint(options.depth, ==, 8);
  g_assert_true(options.try_forced_mistakes);
  g_assert_cmpstr(options.arg, ==, "input.sgf");
}

static void test_create_puzzles_cli_parses_depth_and_synthetic_candidates(void) {
  char *argv[] = {
      (char *)"create_puzzles",
      (char *)"--depth",
      (char *)"10",
      (char *)"--synthetic-candidates",
      (char *)"3",
  };
  CheckersCreatePuzzlesCliOptions options = {0};
  g_autofree char *error = NULL;

  g_assert_true(checkers_create_puzzles_cli_parse(G_N_ELEMENTS(argv), argv, 8, &options, &error));
  g_assert_null(error);
  g_assert_cmpuint(options.depth, ==, 10);
  g_assert_true(options.try_forced_mistakes);
  g_assert_cmpstr(options.arg, ==, "3");
}

static void test_create_puzzles_cli_rejects_invalid_input(void) {
  char *missing_arg_argv[] = {
      (char *)"create_puzzles",
      (char *)"--synthetic-candidates",
  };
  CheckersCreatePuzzlesCliOptions options = {0};
  g_autofree char *error = NULL;

  g_assert_false(checkers_create_puzzles_cli_parse(G_N_ELEMENTS(missing_arg_argv),
                                                   missing_arg_argv,
                                                   8,
                                                   &options,
                                                   &error));
  g_assert_cmpstr(error, ==, "Missing puzzle count or SGF file");

  char *unknown_option_argv[] = {
      (char *)"create_puzzles",
      (char *)"--bad",
      (char *)"12",
  };
  g_clear_pointer(&error, g_free);
  g_assert_false(checkers_create_puzzles_cli_parse(G_N_ELEMENTS(unknown_option_argv),
                                                   unknown_option_argv,
                                                   8,
                                                   &options,
                                                   &error));
  g_assert_cmpstr(error, ==, "Unknown option: --bad");
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/create-puzzles-cli/defaults", test_create_puzzles_cli_defaults);
  g_test_add_func("/create-puzzles-cli/synthetic-candidates",
                  test_create_puzzles_cli_synthetic_candidates_flag);
  g_test_add_func("/create-puzzles-cli/depth-and-synthetic-candidates",
                  test_create_puzzles_cli_parses_depth_and_synthetic_candidates);
  g_test_add_func("/create-puzzles-cli/rejects-invalid-input", test_create_puzzles_cli_rejects_invalid_input);

  return g_test_run();
}
