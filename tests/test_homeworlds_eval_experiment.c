#include <glib.h>
#include <string.h>

#ifndef HOMEWORLDS_EVAL_EXPERIMENT_PATH
#error "HOMEWORLDS_EVAL_EXPERIMENT_PATH must be defined"
#endif

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
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  g_autoptr(GError) error = NULL;
  gint wait_status = 0;

  g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_assert_no_error(error);
  g_assert_true(g_spawn_check_wait_status(wait_status, &error));
  g_assert_no_error(error);
  g_assert_cmpstr(stderr_text, ==, "");
  g_assert_nonnull(strstr(stdout_text, "variable=ship1 depth=1 games=2 max-plies=2 seed=1\n"));
  g_assert_nonnull(strstr(stdout_text, "value,candidate_wins,baseline_wins,draws,timeouts\n"));
  g_assert_nonnull(strstr(stdout_text, "5,"));
  g_assert_nonnull(strstr(stdout_text, "10,"));
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
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  g_autoptr(GError) error = NULL;
  gint wait_status = 0;

  g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_assert_no_error(error);
  g_assert_false(g_spawn_check_wait_status(wait_status, NULL));
  g_assert_cmpstr(stdout_text, ==, "");
  g_assert_nonnull(strstr(stderr_text, "Unknown --variable 'empty-homeworld'."));
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
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  g_autoptr(GError) error = NULL;
  gint wait_status = 0;

  g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_assert_no_error(error);
  g_assert_true(g_spawn_check_wait_status(wait_status, &error));
  g_assert_no_error(error);
  g_assert_nonnull(strstr(stdout_text, "value,candidate_wins,baseline_wins,draws,timeouts\n"));
  g_assert_nonnull(strstr(stderr_text,
                          "move-count,value,game,seed,candidate_side,ply,side,depth_hint,"
                          "generated_leaves,scored_moves,kept_moves\n"));
  g_assert_nonnull(strstr(stderr_text, "move-count,5,0,1,0,0,0,1,"));
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/homeworlds-eval-experiment/runs-selected-variable",
                  test_homeworlds_eval_experiment_runs_selected_variable);
  g_test_add_func("/homeworlds-eval-experiment/rejects-unknown-variable",
                  test_homeworlds_eval_experiment_rejects_unknown_variable);
  g_test_add_func("/homeworlds-eval-experiment/traces-move-counts",
                  test_homeworlds_eval_experiment_traces_move_counts);
  return g_test_run();
}
