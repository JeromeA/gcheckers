#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#ifndef HOMEWORLDS_EVAL_EXPERIMENT_PATH
#error "HOMEWORLDS_EVAL_EXPERIMENT_PATH must be defined"
#endif

static gchar **test_homeworlds_eval_experiment_base_env(void) {
  gchar **envp = g_get_environ();

  return g_environ_unsetenv(envp, "GCHECKERS_HOMEWORLDS_EVAL_PROGRESS");
}

static void test_homeworlds_eval_experiment_runs_selected_variable(void) {
  gchar *argv[] = {
    (gchar *)HOMEWORLDS_EVAL_EXPERIMENT_PATH,
    (gchar *)"--variable",
    (gchar *)"ship1",
    (gchar *)"--values",
    (gchar *)"5,10",
    (gchar *)"--games",
    (gchar *)"2",
    (gchar *)"--max-plies",
    (gchar *)"2",
    (gchar *)"--seed",
    (gchar *)"1",
    NULL,
  };
  gchar **envp = test_homeworlds_eval_experiment_base_env();
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  g_autoptr(GError) error = NULL;
  gint wait_status = 0;

  g_assert_true(g_spawn_sync(NULL, argv, envp, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_strfreev(envp);
  g_assert_no_error(error);
  g_assert_true(g_spawn_check_wait_status(wait_status, &error));
  g_assert_no_error(error);
  g_assert_cmpstr(stderr_text, ==, "");
  g_assert_nonnull(strstr(stdout_text, "variable=ship1 depth=1 games=2 max-plies=2 seed=1\n"));
  g_assert_nonnull(strstr(stdout_text, "value,candidate_wins,baseline_wins,win_ratio,draws,timeouts\n"));
  g_assert_nonnull(strstr(stdout_text, "5,0,0,,0,2\n"));
  g_assert_nonnull(strstr(stdout_text, "10,0,0,,0,2\n"));
}

static void test_homeworlds_eval_experiment_reports_win_ratio(void) {
  gchar *argv[] = {
    (gchar *)HOMEWORLDS_EVAL_EXPERIMENT_PATH,
    (gchar *)"--variable",
    (gchar *)"ship1",
    (gchar *)"--values",
    (gchar *)"5",
    (gchar *)"--games",
    (gchar *)"2",
    (gchar *)"--max-plies",
    (gchar *)"50",
    (gchar *)"--seed",
    (gchar *)"1",
    NULL,
  };
  gchar **envp = test_homeworlds_eval_experiment_base_env();
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  g_autoptr(GError) error = NULL;
  gint wait_status = 0;

  g_assert_true(g_spawn_sync(NULL, argv, envp, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_strfreev(envp);
  g_assert_no_error(error);
  g_assert_true(g_spawn_check_wait_status(wait_status, &error));
  g_assert_no_error(error);
  g_assert_cmpstr(stderr_text, ==, "");
  g_assert_nonnull(strstr(stdout_text, "value,candidate_wins,baseline_wins,win_ratio,draws,timeouts\n"));
  g_assert_nonnull(strstr(stdout_text, "5,0,0,,0,2\n"));
}

static void test_homeworlds_eval_experiment_rejects_unknown_variable(void) {
  gchar *argv[] = {
    (gchar *)HOMEWORLDS_EVAL_EXPERIMENT_PATH,
    (gchar *)"--variable",
    (gchar *)"empty-homeworld",
    (gchar *)"--values",
    (gchar *)"80",
    NULL,
  };
  gchar **envp = test_homeworlds_eval_experiment_base_env();
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  g_autoptr(GError) error = NULL;
  gint wait_status = 0;

  g_assert_true(g_spawn_sync(NULL, argv, envp, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_strfreev(envp);
  g_assert_no_error(error);
  g_assert_false(g_spawn_check_wait_status(wait_status, NULL));
  g_assert_cmpstr(stdout_text, ==, "");
  g_assert_nonnull(strstr(stderr_text, "Unknown --variable 'empty-homeworld'."));
}

static void test_homeworlds_eval_experiment_rejects_removed_homeworld_ship_variables(void) {
  gchar *argv[] = {
    (gchar *)HOMEWORLDS_EVAL_EXPERIMENT_PATH,
    (gchar *)"--variable",
    (gchar *)"homeworld-ship3",
    (gchar *)"--values",
    (gchar *)"25",
    (gchar *)"--games",
    (gchar *)"1",
    (gchar *)"--max-plies",
    (gchar *)"1",
    (gchar *)"--seed",
    (gchar *)"1",
    NULL,
  };
  gchar **envp = test_homeworlds_eval_experiment_base_env();
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  g_autoptr(GError) error = NULL;
  gint wait_status = 0;

  g_assert_true(g_spawn_sync(NULL, argv, envp, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_strfreev(envp);
  g_assert_no_error(error);
  g_assert_false(g_spawn_check_wait_status(wait_status, NULL));
  g_assert_cmpstr(stdout_text, ==, "");
  g_assert_nonnull(strstr(stderr_text, "Unknown --variable 'homeworld-ship3'."));
}

static void test_homeworlds_eval_experiment_rejects_removed_ship_aliases(void) {
  gchar *argv[] = {
    (gchar *)HOMEWORLDS_EVAL_EXPERIMENT_PATH,
    (gchar *)"--variable",
    (gchar *)"large-ship",
    (gchar *)"--values",
    (gchar *)"30",
    NULL,
  };
  gchar **envp = test_homeworlds_eval_experiment_base_env();
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  g_autoptr(GError) error = NULL;
  gint wait_status = 0;

  g_assert_true(g_spawn_sync(NULL, argv, envp, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_strfreev(envp);
  g_assert_no_error(error);
  g_assert_false(g_spawn_check_wait_status(wait_status, NULL));
  g_assert_cmpstr(stdout_text, ==, "");
  g_assert_nonnull(strstr(stderr_text, "Unknown --variable 'large-ship'."));
}

static void test_homeworlds_eval_experiment_traces_move_counts(void) {
  gchar *argv[] = {
    (gchar *)HOMEWORLDS_EVAL_EXPERIMENT_PATH,
    (gchar *)"--variable",
    (gchar *)"ship1",
    (gchar *)"--values",
    (gchar *)"5",
    (gchar *)"--games",
    (gchar *)"1",
    (gchar *)"--max-plies",
    (gchar *)"1",
    (gchar *)"--seed",
    (gchar *)"1",
    (gchar *)"--trace-move-counts",
    NULL,
  };
  gchar **envp = test_homeworlds_eval_experiment_base_env();
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  g_autoptr(GError) error = NULL;
  gint wait_status = 0;

  g_assert_true(g_spawn_sync(NULL, argv, envp, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_strfreev(envp);
  g_assert_no_error(error);
  g_assert_true(g_spawn_check_wait_status(wait_status, &error));
  g_assert_no_error(error);
  g_assert_nonnull(strstr(stdout_text, "value,candidate_wins,baseline_wins,win_ratio,draws,timeouts\n"));
  g_assert_nonnull(strstr(stderr_text,
                          "move-count,value,game,seed,candidate_side,ply,side,depth_hint,"
                          "generated_leaves,scored_moves,kept_moves,"
                          "pruning_checked_branches,pruning_window_cutoff_branches,pruning_pruned_branches,"
                          "ordering_candidate_lists,ordering_reordered_candidate_lists,"
                          "ordering_reordered_candidates,ordering_single_step_passes,"
                          "ordering_single_step_moves\n"));
  g_assert_nonnull(strstr(stderr_text, "move-count,5,0,1,0,0,0,1,72,0,72,0,0,0,0,0,0,0,0\n"));
}

static void test_homeworlds_eval_experiment_shows_rewriting_progress(void) {
  gchar *argv[] = {
    (gchar *)HOMEWORLDS_EVAL_EXPERIMENT_PATH,
    (gchar *)"--variable",
    (gchar *)"ship1",
    (gchar *)"--values",
    (gchar *)"5",
    (gchar *)"--games",
    (gchar *)"1",
    (gchar *)"--max-plies",
    (gchar *)"1",
    (gchar *)"--seed",
    (gchar *)"1",
    NULL,
  };
  gchar **envp = test_homeworlds_eval_experiment_base_env();
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  g_autoptr(GError) error = NULL;
  gint wait_status = 0;

  envp = g_environ_setenv(envp, "GCHECKERS_HOMEWORLDS_EVAL_PROGRESS", "always", TRUE);
  g_assert_true(g_spawn_sync(NULL, argv, envp, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_strfreev(envp);
  g_assert_no_error(error);
  g_assert_true(g_spawn_check_wait_status(wait_status, &error));
  g_assert_no_error(error);
  g_assert_nonnull(strstr(stdout_text, "value,candidate_wins,baseline_wins,win_ratio,draws,timeouts\n"));
  g_assert_nonnull(strstr(stderr_text,
                          "\r\033[2Khomeworlds_eval_experiment: value=5 game=1/1 ply=1/1"));
  g_assert_true(g_str_has_suffix(stderr_text, "\r\033[2K"));
  g_assert_null(strchr(stderr_text, '\n'));
}

static void test_homeworlds_eval_experiment_writes_big_move_report(void) {
  g_autoptr(GError) error = NULL;
  g_autofree gchar *tmp_dir = g_dir_make_tmp("homeworlds-big-move-report-XXXXXX", &error);
  g_autofree gchar *tool_path = g_canonicalize_filename(HOMEWORLDS_EVAL_EXPERIMENT_PATH, NULL);
  g_autofree gchar *report_path = NULL;
  g_autofree gchar *report_text = NULL;
  gsize report_len = 0;
  gchar *argv[] = {
    tool_path,
    (gchar *)"--variable",
    (gchar *)"ship1",
    (gchar *)"--values",
    (gchar *)"5",
    (gchar *)"--games",
    (gchar *)"1",
    (gchar *)"--max-plies",
    (gchar *)"1",
    (gchar *)"--seed",
    (gchar *)"1",
    NULL,
  };
  gchar **envp = test_homeworlds_eval_experiment_base_env();
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  gint wait_status = 0;

  g_assert_no_error(error);
  g_assert_nonnull(tmp_dir);

  envp = g_environ_setenv(envp, "GCHECKERS_HOMEWORLDS_BIG_MOVE_REPORT_THRESHOLD", "0", TRUE);
  envp = g_environ_setenv(envp, "GCHECKERS_HOMEWORLDS_BIG_MOVE_REPORT_MIN_TOTAL_MOVES", "0", TRUE);
  g_assert_true(g_spawn_sync(tmp_dir, argv, envp, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_strfreev(envp);
  g_assert_no_error(error);
  g_assert_true(g_spawn_check_wait_status(wait_status, &error));
  g_assert_no_error(error);
  g_assert_nonnull(strstr(stdout_text, "value,candidate_wins,baseline_wins,win_ratio,draws,timeouts\n"));
  g_assert_cmpstr(stderr_text, ==, "");

  report_path = g_build_filename(tmp_dir, "big_move_report_001.txt", NULL);
  g_assert_true(g_file_get_contents(report_path, &report_text, &report_len, &error));
  g_assert_no_error(error);
  g_assert_cmpuint(report_len, >, 0);
  g_assert_nonnull(strstr(report_text, "good_moves_generated: "));
  g_assert_nonnull(strstr(report_text, "good_moves_pruning_checked_branches: "));
  g_assert_nonnull(strstr(report_text, "good_moves_pruning_window_cutoff_branches: "));
  g_assert_nonnull(strstr(report_text, "good_moves_pruning_pruned_branches: "));
  g_assert_nonnull(strstr(report_text, "good_moves_ordering_candidate_lists: "));
  g_assert_nonnull(strstr(report_text, "good_moves_ordering_reordered_candidate_lists: "));
  g_assert_nonnull(strstr(report_text, "good_moves_ordering_reordered_candidates: "));
  g_assert_nonnull(strstr(report_text, "good_moves_ordering_single_step_passes: "));
  g_assert_nonnull(strstr(report_text, "good_moves_ordering_single_step_moves: "));
  g_assert_nonnull(strstr(report_text, "\nmoves:\n<none>\n\nposition:\nNo systems.\n"));
  g_assert_nonnull(strstr(report_text, "position:\nNo systems.\n"));
  g_assert_nonnull(strstr(report_text, "\nall_moves:\n"));
  g_assert_nonnull(strstr(report_text, "all_moves_streamed: "));
  g_assert_null(strstr(report_text, "good_moves():"));
  g_assert_null(strstr(report_text, "good_moves_pruning_mode:"));
  g_assert_null(strstr(report_text, "good_moves_pruning_would_prune_branches:"));
  g_assert_null(strstr(report_text, "good_moves_pruning_verified_leaves:"));
  g_assert_null(strstr(report_text, "good_moves_pruning_verification_failures:"));
  g_assert_null(strstr(report_text, "good_moves_ordering:"));
  g_assert_cmpint(g_remove(report_path), ==, 0);
  g_assert_cmpint(g_rmdir(tmp_dir), ==, 0);
}

static void test_homeworlds_eval_experiment_big_move_report_includes_played_moves(void) {
  g_autoptr(GError) error = NULL;
  g_autofree gchar *tmp_dir = g_dir_make_tmp("homeworlds-big-move-report-XXXXXX", &error);
  g_autofree gchar *tool_path = g_canonicalize_filename(HOMEWORLDS_EVAL_EXPERIMENT_PATH, NULL);
  g_autofree gchar *first_report_path = NULL;
  g_autofree gchar *second_report_path = NULL;
  g_autofree gchar *report_text = NULL;
  gchar *argv[] = {
    tool_path,
    (gchar *)"--variable",
    (gchar *)"ship1",
    (gchar *)"--values",
    (gchar *)"5",
    (gchar *)"--games",
    (gchar *)"1",
    (gchar *)"--max-plies",
    (gchar *)"2",
    (gchar *)"--seed",
    (gchar *)"1",
    NULL,
  };
  gchar **envp = test_homeworlds_eval_experiment_base_env();
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  gint wait_status = 0;

  g_assert_no_error(error);
  g_assert_nonnull(tmp_dir);

  envp = g_environ_setenv(envp, "GCHECKERS_HOMEWORLDS_BIG_MOVE_REPORT_THRESHOLD", "0", TRUE);
  envp = g_environ_setenv(envp, "GCHECKERS_HOMEWORLDS_BIG_MOVE_REPORT_MIN_TOTAL_MOVES", "0", TRUE);
  g_assert_true(g_spawn_sync(tmp_dir, argv, envp, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_strfreev(envp);
  g_assert_no_error(error);
  g_assert_true(g_spawn_check_wait_status(wait_status, &error));
  g_assert_no_error(error);
  g_assert_cmpstr(stderr_text, ==, "");

  first_report_path = g_build_filename(tmp_dir, "big_move_report_001.txt", NULL);
  second_report_path = g_build_filename(tmp_dir, "big_move_report_002.txt", NULL);
  g_assert_true(g_file_test(first_report_path, G_FILE_TEST_EXISTS));
  g_assert_true(g_file_get_contents(second_report_path, &report_text, NULL, &error));
  g_assert_no_error(error);
  g_assert_nonnull(strstr(report_text, "\nmoves:\n1. "));
  g_assert_nonnull(strstr(report_text, "\n\nposition:\n"));
  g_assert_cmpint(g_remove(first_report_path), ==, 0);
  g_assert_cmpint(g_remove(second_report_path), ==, 0);
  g_assert_cmpint(g_rmdir(tmp_dir), ==, 0);
}

static void test_homeworlds_eval_experiment_discards_small_big_move_report(void) {
  g_autoptr(GError) error = NULL;
  g_autofree gchar *tmp_dir = g_dir_make_tmp("homeworlds-big-move-report-XXXXXX", &error);
  g_autofree gchar *tool_path = g_canonicalize_filename(HOMEWORLDS_EVAL_EXPERIMENT_PATH, NULL);
  g_autofree gchar *report_path = NULL;
  gchar *argv[] = {
    tool_path,
    (gchar *)"--variable",
    (gchar *)"ship1",
    (gchar *)"--values",
    (gchar *)"5",
    (gchar *)"--games",
    (gchar *)"1",
    (gchar *)"--max-plies",
    (gchar *)"1",
    (gchar *)"--seed",
    (gchar *)"1",
    NULL,
  };
  gchar **envp = test_homeworlds_eval_experiment_base_env();
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  gint wait_status = 0;

  g_assert_no_error(error);
  g_assert_nonnull(tmp_dir);

  envp = g_environ_setenv(envp, "GCHECKERS_HOMEWORLDS_BIG_MOVE_REPORT_THRESHOLD", "0", TRUE);
  g_assert_true(g_spawn_sync(tmp_dir, argv, envp, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_strfreev(envp);
  g_assert_no_error(error);
  g_assert_true(g_spawn_check_wait_status(wait_status, &error));
  g_assert_no_error(error);
  g_assert_nonnull(strstr(stdout_text, "value,candidate_wins,baseline_wins,win_ratio,draws,timeouts\n"));
  g_assert_cmpstr(stderr_text, ==, "");

  report_path = g_build_filename(tmp_dir, "big_move_report_001.txt", NULL);
  g_assert_false(g_file_test(report_path, G_FILE_TEST_EXISTS));
  g_assert_cmpint(g_rmdir(tmp_dir), ==, 0);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/homeworlds-eval-experiment/runs-selected-variable",
                  test_homeworlds_eval_experiment_runs_selected_variable);
  g_test_add_func("/homeworlds-eval-experiment/reports-win-ratio",
                  test_homeworlds_eval_experiment_reports_win_ratio);
  g_test_add_func("/homeworlds-eval-experiment/rejects-unknown-variable",
                  test_homeworlds_eval_experiment_rejects_unknown_variable);
  g_test_add_func("/homeworlds-eval-experiment/rejects-removed-homeworld-ship-variables",
                  test_homeworlds_eval_experiment_rejects_removed_homeworld_ship_variables);
  g_test_add_func("/homeworlds-eval-experiment/rejects-removed-ship-aliases",
                  test_homeworlds_eval_experiment_rejects_removed_ship_aliases);
  g_test_add_func("/homeworlds-eval-experiment/traces-move-counts",
                  test_homeworlds_eval_experiment_traces_move_counts);
  g_test_add_func("/homeworlds-eval-experiment/shows-rewriting-progress",
                  test_homeworlds_eval_experiment_shows_rewriting_progress);
  g_test_add_func("/homeworlds-eval-experiment/writes-big-move-report",
                  test_homeworlds_eval_experiment_writes_big_move_report);
  g_test_add_func("/homeworlds-eval-experiment/big-move-report-includes-played-moves",
                  test_homeworlds_eval_experiment_big_move_report_includes_played_moves);
  g_test_add_func("/homeworlds-eval-experiment/discards-small-big-move-report",
                  test_homeworlds_eval_experiment_discards_small_big_move_report);
  return g_test_run();
}
