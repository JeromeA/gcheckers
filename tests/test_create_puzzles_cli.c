#include "../src/create_puzzles_cli.h"
#include "../src/rulesets.h"

#include <glib.h>

static void test_create_puzzles_cli_defaults(void) {
  char *argv[] = {
      (char *)"create_puzzles",
      (char *)"--ruleset",
      (char *)"american",
      (char *)"12",
  };
  CheckersCreatePuzzlesCliOptions options = {0};
  g_autofree char *error = NULL;

  g_assert_true(checkers_create_puzzles_cli_parse(G_N_ELEMENTS(argv), argv, 8, &options, &error));
  g_assert_no_error((GError *)NULL);
  g_assert_null(error);
  g_assert_cmpint(options.mode, ==, CHECKERS_CREATE_PUZZLES_MODE_GENERATE);
  g_assert_cmpuint(options.depth, ==, 8);
  g_assert_false(options.try_forced_mistakes);
  g_assert_false(options.save_games);
  g_assert_false(options.dry_run);
  g_assert_true(options.has_ruleset);
  g_assert_cmpint(options.ruleset, ==, PLAYER_RULESET_AMERICAN);
  g_assert_cmpstr(options.arg, ==, "12");
}

static void test_create_puzzles_cli_synthetic_candidates_flag(void) {
  char *argv[] = {
      (char *)"create_puzzles",
      (char *)"--ruleset",
      (char *)"international",
      (char *)"--synthetic-candidates",
      (char *)"input.sgf",
  };
  CheckersCreatePuzzlesCliOptions options = {0};
  g_autofree char *error = NULL;

  g_assert_true(checkers_create_puzzles_cli_parse(G_N_ELEMENTS(argv), argv, 8, &options, &error));
  g_assert_null(error);
  g_assert_cmpint(options.mode, ==, CHECKERS_CREATE_PUZZLES_MODE_GENERATE);
  g_assert_cmpuint(options.depth, ==, 8);
  g_assert_cmpint(options.ruleset, ==, PLAYER_RULESET_INTERNATIONAL);
  g_assert_true(options.try_forced_mistakes);
  g_assert_false(options.save_games);
  g_assert_cmpstr(options.arg, ==, "input.sgf");
}

static void test_create_puzzles_cli_parses_depth_and_synthetic_candidates(void) {
  char *argv[] = {
      (char *)"create_puzzles",
      (char *)"--ruleset",
      (char *)"russian",
      (char *)"--depth",
      (char *)"10",
      (char *)"--synthetic-candidates",
      (char *)"3",
  };
  CheckersCreatePuzzlesCliOptions options = {0};
  g_autofree char *error = NULL;

  g_assert_true(checkers_create_puzzles_cli_parse(G_N_ELEMENTS(argv), argv, 8, &options, &error));
  g_assert_null(error);
  g_assert_cmpint(options.mode, ==, CHECKERS_CREATE_PUZZLES_MODE_GENERATE);
  g_assert_cmpuint(options.depth, ==, 10);
  g_assert_cmpint(options.ruleset, ==, PLAYER_RULESET_RUSSIAN);
  g_assert_true(options.try_forced_mistakes);
  g_assert_false(options.save_games);
  g_assert_cmpstr(options.arg, ==, "3");
}

static void test_create_puzzles_cli_save_games_flag(void) {
  char *argv[] = {
      (char *)"create_puzzles",
      (char *)"--ruleset",
      (char *)"american",
      (char *)"--save-games",
      (char *)"2",
  };
  CheckersCreatePuzzlesCliOptions options = {0};
  g_autofree char *error = NULL;

  g_assert_true(checkers_create_puzzles_cli_parse(G_N_ELEMENTS(argv), argv, 8, &options, &error));
  g_assert_null(error);
  g_assert_cmpint(options.mode, ==, CHECKERS_CREATE_PUZZLES_MODE_GENERATE);
  g_assert_true(options.save_games);
  g_assert_cmpint(options.ruleset, ==, PLAYER_RULESET_AMERICAN);
  g_assert_cmpstr(options.arg, ==, "2");
}

static void test_create_puzzles_cli_check_existing_mode(void) {
  char *argv[] = {
      (char *)"create_puzzles",
      (char *)"--ruleset",
      (char *)"international",
      (char *)"--check-existing",
      (char *)"--dry-run",
      (char *)"puzzles-alt",
  };
  CheckersCreatePuzzlesCliOptions options = {0};
  g_autofree char *error = NULL;

  g_assert_true(checkers_create_puzzles_cli_parse(G_N_ELEMENTS(argv), argv, 8, &options, &error));
  g_assert_null(error);
  g_assert_cmpint(options.mode, ==, CHECKERS_CREATE_PUZZLES_MODE_CHECK_EXISTING);
  g_assert_cmpuint(options.depth, ==, 8);
  g_assert_false(options.try_forced_mistakes);
  g_assert_false(options.save_games);
  g_assert_true(options.dry_run);
  g_assert_cmpint(options.ruleset, ==, PLAYER_RULESET_INTERNATIONAL);
  g_assert_cmpstr(options.arg, ==, "puzzles-alt");

  char *argv_default_dir[] = {
      (char *)"create_puzzles",
      (char *)"--ruleset",
      (char *)"russian",
      (char *)"--check-existing",
  };
  g_assert_true(checkers_create_puzzles_cli_parse(G_N_ELEMENTS(argv_default_dir),
                                                  argv_default_dir,
                                                  8,
                                                  &options,
                                                  &error));
  g_assert_null(error);
  g_assert_cmpint(options.mode, ==, CHECKERS_CREATE_PUZZLES_MODE_CHECK_EXISTING);
  g_assert_false(options.save_games);
  g_assert_cmpint(options.ruleset, ==, PLAYER_RULESET_RUSSIAN);
  g_assert_null(options.arg);
}

static void test_create_puzzles_cli_ruleset_short_names(void) {
  g_assert_cmpstr(checkers_ruleset_short_name(PLAYER_RULESET_AMERICAN), ==, "american");
  g_assert_cmpstr(checkers_ruleset_short_name(PLAYER_RULESET_INTERNATIONAL), ==, "international");
  g_assert_cmpstr(checkers_ruleset_short_name(PLAYER_RULESET_RUSSIAN), ==, "russian");

  PlayerRuleset ruleset = PLAYER_RULESET_AMERICAN;
  g_assert_true(checkers_ruleset_find_by_short_name("american", &ruleset));
  g_assert_cmpint(ruleset, ==, PLAYER_RULESET_AMERICAN);
  g_assert_true(checkers_ruleset_find_by_short_name("international", &ruleset));
  g_assert_cmpint(ruleset, ==, PLAYER_RULESET_INTERNATIONAL);
  g_assert_true(checkers_ruleset_find_by_short_name("russian", &ruleset));
  g_assert_cmpint(ruleset, ==, PLAYER_RULESET_RUSSIAN);
  g_assert_false(checkers_ruleset_find_by_short_name("unknown", &ruleset));
}

static void test_create_puzzles_cli_rejects_invalid_input(void) {
  char *missing_arg_argv[] = {
      (char *)"create_puzzles",
      (char *)"--ruleset",
      (char *)"american",
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

  char *missing_ruleset_argv[] = {
      (char *)"create_puzzles",
      (char *)"12",
  };
  g_clear_pointer(&error, g_free);
  g_assert_false(checkers_create_puzzles_cli_parse(G_N_ELEMENTS(missing_ruleset_argv),
                                                   missing_ruleset_argv,
                                                   8,
                                                   &options,
                                                   &error));
  g_assert_cmpstr(error, ==, "Missing --ruleset <short-name>");

  char *bad_ruleset_argv[] = {
      (char *)"create_puzzles",
      (char *)"--ruleset",
      (char *)"bad",
      (char *)"12",
  };
  g_clear_pointer(&error, g_free);
  g_assert_false(checkers_create_puzzles_cli_parse(G_N_ELEMENTS(bad_ruleset_argv),
                                                   bad_ruleset_argv,
                                                   8,
                                                   &options,
                                                   &error));
  g_assert_cmpstr(error, ==, "Invalid --ruleset value");

  char *unknown_option_argv[] = {
      (char *)"create_puzzles",
      (char *)"--ruleset",
      (char *)"american",
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

  char *bad_dry_run_argv[] = {
      (char *)"create_puzzles",
      (char *)"--ruleset",
      (char *)"american",
      (char *)"--dry-run",
      (char *)"12",
  };
  g_clear_pointer(&error, g_free);
  g_assert_false(checkers_create_puzzles_cli_parse(G_N_ELEMENTS(bad_dry_run_argv),
                                                   bad_dry_run_argv,
                                                   8,
                                                   &options,
                                                   &error));
  g_assert_cmpstr(error, ==, "--dry-run is only valid with --check-existing");

  char *bad_synthetic_check_argv[] = {
      (char *)"create_puzzles",
      (char *)"--ruleset",
      (char *)"american",
      (char *)"--check-existing",
      (char *)"--synthetic-candidates",
  };
  g_clear_pointer(&error, g_free);
  g_assert_false(checkers_create_puzzles_cli_parse(G_N_ELEMENTS(bad_synthetic_check_argv),
                                                   bad_synthetic_check_argv,
                                                   8,
                                                   &options,
                                                   &error));
  g_assert_cmpstr(error, ==, "--synthetic-candidates is only valid when generating puzzles");

  char *bad_save_games_check_argv[] = {
      (char *)"create_puzzles",
      (char *)"--ruleset",
      (char *)"american",
      (char *)"--check-existing",
      (char *)"--save-games",
  };
  g_clear_pointer(&error, g_free);
  g_assert_false(checkers_create_puzzles_cli_parse(G_N_ELEMENTS(bad_save_games_check_argv),
                                                   bad_save_games_check_argv,
                                                   8,
                                                   &options,
                                                   &error));
  g_assert_cmpstr(error, ==, "--save-games is only valid when generating puzzles");
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/create-puzzles-cli/defaults", test_create_puzzles_cli_defaults);
  g_test_add_func("/create-puzzles-cli/synthetic-candidates",
                  test_create_puzzles_cli_synthetic_candidates_flag);
  g_test_add_func("/create-puzzles-cli/depth-and-synthetic-candidates",
                  test_create_puzzles_cli_parses_depth_and_synthetic_candidates);
  g_test_add_func("/create-puzzles-cli/save-games", test_create_puzzles_cli_save_games_flag);
  g_test_add_func("/create-puzzles-cli/check-existing", test_create_puzzles_cli_check_existing_mode);
  g_test_add_func("/create-puzzles-cli/ruleset-short-names", test_create_puzzles_cli_ruleset_short_names);
  g_test_add_func("/create-puzzles-cli/rejects-invalid-input", test_create_puzzles_cli_rejects_invalid_input);

  return g_test_run();
}
