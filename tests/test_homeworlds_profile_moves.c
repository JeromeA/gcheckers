#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

#ifndef HOMEWORLDS_PROFILE_MOVES_PATH
#error "HOMEWORLDS_PROFILE_MOVES_PATH must be defined"
#endif

static void test_homeworlds_profile_moves_runs_ai_analysis_when_requested(void) {
  gchar *argv[] = {
    (gchar *)HOMEWORLDS_PROFILE_MOVES_PATH,
    (gchar *)"--moves",
    (gchar *)"2",
    (gchar *)"--depth",
    (gchar *)"0",
    (gchar *)"--seed",
    (gchar *)"1",
    (gchar *)"--ai-report",
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
  g_assert_nonnull(strstr(stdout_text, "Generated moves (2 requested):"));
  g_assert_nonnull(strstr(stdout_text, "Current position after 2 generated moves:"));
  g_assert_nonnull(strstr(stdout_text, "H2: g3 Y2B3 -\n\nH1: - Y1G3 b3"));
  g_assert_nonnull(strstr(stdout_text, "AI analysis after 2 generated moves at depth 0:"));
  g_assert_nonnull(strstr(stdout_text, "Nodes:"));
  g_assert_nonnull(strstr(stdout_text, "TT probes:"));
  g_assert_nonnull(strstr(stdout_text, "Moves:"));
  g_assert_nonnull(strstr(stdout_text, "score="));
  g_assert_null(strstr(stdout_text, "Move report after"));
  g_assert_null(strstr(stdout_text, "good_moves()"));
  g_assert_true(strstr(stdout_text, "Generated moves (2 requested):") <
                strstr(stdout_text, "Current position after 2 generated moves:"));
  g_assert_true(strstr(stdout_text, "Current position after 2 generated moves:") <
                strstr(stdout_text, "AI analysis after 2 generated moves at depth 0:"));
}

static void test_homeworlds_profile_moves_skips_ai_analysis_by_default(void) {
  gchar *argv[] = {
    (gchar *)HOMEWORLDS_PROFILE_MOVES_PATH,
    (gchar *)"--moves",
    (gchar *)"2",
    (gchar *)"--depth",
    (gchar *)"0",
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
  g_assert_nonnull(strstr(stdout_text, "Generated moves (2 requested):"));
  g_assert_nonnull(strstr(stdout_text, "Current position after 2 generated moves:"));
  g_assert_null(strstr(stdout_text, "AI analysis after"));
  g_assert_null(strstr(stdout_text, "Move report after"));
}

static void test_homeworlds_profile_moves_rejects_negative_move_count(void) {
  gchar *argv[] = {
    (gchar *)HOMEWORLDS_PROFILE_MOVES_PATH,
    (gchar *)"--moves",
    (gchar *)"-1",
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
  g_assert_false(g_spawn_check_wait_status(wait_status, NULL));
  g_assert_cmpstr(stdout_text, ==, "");
  g_assert_nonnull(strstr(stderr_text, "--moves must be non-negative."));
}

static void test_homeworlds_profile_moves_rejects_negative_depth(void) {
  gchar *argv[] = {
    (gchar *)HOMEWORLDS_PROFILE_MOVES_PATH,
    (gchar *)"--moves",
    (gchar *)"1",
    (gchar *)"--depth",
    (gchar *)"-1",
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
  g_assert_false(g_spawn_check_wait_status(wait_status, NULL));
  g_assert_cmpstr(stdout_text, ==, "");
  g_assert_nonnull(strstr(stderr_text, "--depth must be non-negative."));
}

static void test_homeworlds_profile_moves_rejects_zero_node_limit(void) {
  gchar *argv[] = {
    (gchar *)HOMEWORLDS_PROFILE_MOVES_PATH,
    (gchar *)"--moves",
    (gchar *)"1",
    (gchar *)"--depth",
    (gchar *)"1",
    (gchar *)"--node-limit",
    (gchar *)"0",
    (gchar *)"--seed",
    (gchar *)"1",
    (gchar *)"--ai-report",
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
  g_assert_nonnull(strstr(stderr_text, "--node-limit must be positive."));
}

static void test_homeworlds_profile_moves_stops_after_node_limit(void) {
  gchar *argv[] = {
    (gchar *)HOMEWORLDS_PROFILE_MOVES_PATH,
    (gchar *)"--moves",
    (gchar *)"2",
    (gchar *)"--depth",
    (gchar *)"2",
    (gchar *)"--node-limit",
    (gchar *)"1",
    (gchar *)"--seed",
    (gchar *)"1",
    (gchar *)"--ai-report",
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
  g_assert_nonnull(strstr(stdout_text, "Generated moves (2 requested):"));
  g_assert_nonnull(strstr(stdout_text, "AI analysis stopped after 1 nodes (limit 1) at depth 2."));
  g_assert_nonnull(strstr(stdout_text, "TT probes:"));
  g_assert_nonnull(strstr(stdout_text, "TT hits:"));
  g_assert_nonnull(strstr(stdout_text, "TT cutoffs:"));
  g_assert_null(strstr(stdout_text, "AI analysis after 2 generated moves at depth 2:"));
}

static void test_homeworlds_profile_moves_shows_forced_progress(void) {
  gchar *argv[] = {
    (gchar *)HOMEWORLDS_PROFILE_MOVES_PATH,
    (gchar *)"--moves",
    (gchar *)"2",
    (gchar *)"--depth",
    (gchar *)"0",
    (gchar *)"--seed",
    (gchar *)"1",
    (gchar *)"--ai-report",
    NULL,
  };
  gchar *envp[] = {
    (gchar *)"GCHECKERS_HOMEWORLDS_PROFILE_PROGRESS=always",
    NULL,
  };
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  g_autoptr(GError) error = NULL;
  gint wait_status = 0;

  g_assert_true(g_spawn_sync(NULL, argv, envp, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_assert_no_error(error);
  g_assert_true(g_spawn_check_wait_status(wait_status, &error));
  g_assert_no_error(error);
  g_assert_nonnull(strstr(stderr_text, "\r\033[2Khomeworlds_profile_moves: nodes="));
  g_assert_true(g_str_has_suffix(stderr_text, "\r\033[2K"));
  g_assert_nonnull(strstr(stdout_text, "AI analysis after 2 generated moves at depth 0:"));
}

static void test_homeworlds_profile_moves_replays_file(void) {
  const char *content =
      "(;AP[gcheckers]CA[UTF-8]FF[4]GM[40];B[B1R3g3];W[R2G1b3];B[H1r+];W[H2g+])";
  g_autofree gchar *path = NULL;
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  g_autoptr(GError) error = NULL;
  gint wait_status = 0;
  gint fd = g_file_open_tmp("homeworlds-profile-XXXXXX.sgf", &path, &error);

  g_assert_no_error(error);
  g_assert_cmpint(fd, >=, 0);
  g_assert_cmpint(close(fd), ==, 0);
  g_assert_true(g_file_set_contents(path, content, -1, &error));
  g_assert_no_error(error);

  gchar *argv[] = {
    (gchar *)HOMEWORLDS_PROFILE_MOVES_PATH,
    (gchar *)"--file",
    path,
    (gchar *)"--moves",
    (gchar *)"2",
    (gchar *)"--depth",
    (gchar *)"0",
    NULL,
  };

  g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_assert_no_error(error);
  g_assert_true(g_spawn_check_wait_status(wait_status, &error));
  g_assert_no_error(error);
  g_assert_cmpstr(stderr_text, ==, "");
  g_assert_nonnull(strstr(stdout_text, "Replayed moves from "));
  g_assert_nonnull(strstr(stdout_text, " (2 requested):"));
  g_assert_nonnull(strstr(stdout_text, "1. B1R3g3\n2. R2G1b3"));
  g_assert_nonnull(strstr(stdout_text, "Current position after 2 replayed moves:"));
  g_assert_nonnull(strstr(stdout_text, "H2: b3 R2G1 -\n\nH1: - B1R3 g3"));
  g_assert_null(strstr(stdout_text, "AI analysis after"));
  g_assert_null(strstr(stdout_text, "Move report after"));
  g_assert_true(strstr(stdout_text, "Replayed moves from ") <
                strstr(stdout_text, "Current position after 2 replayed moves:"));
  g_assert_cmpint(g_remove(path), ==, 0);
}

static void test_homeworlds_profile_moves_replays_text_file(void) {
  const char *content =
      "1. B1R3g3\n"
      "2. R2G1b3\n"
      "3. H1g+\n"
      "4. H2b+\n";
  g_autofree gchar *path = NULL;
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  g_autoptr(GError) error = NULL;
  gint wait_status = 0;
  gint fd = g_file_open_tmp("homeworlds-profile-XXXXXX.txt", &path, &error);

  g_assert_no_error(error);
  g_assert_cmpint(fd, >=, 0);
  g_assert_cmpint(close(fd), ==, 0);
  g_assert_true(g_file_set_contents(path, content, -1, &error));
  g_assert_no_error(error);

  gchar *argv[] = {
    (gchar *)HOMEWORLDS_PROFILE_MOVES_PATH,
    (gchar *)"--file",
    path,
    (gchar *)"--moves",
    (gchar *)"2",
    (gchar *)"--depth",
    (gchar *)"0",
    (gchar *)"--ai-report",
    (gchar *)"--move-report",
    NULL,
  };

  g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_assert_no_error(error);
  g_assert_true(g_spawn_check_wait_status(wait_status, &error));
  g_assert_no_error(error);
  g_assert_cmpstr(stderr_text, ==, "");
  g_assert_nonnull(strstr(stdout_text, "Move report after 2 replayed moves:"));
  g_assert_nonnull(strstr(stdout_text, "moves:\n1. B1R3g3\n2. R2G1b3\n"));
  g_assert_nonnull(strstr(stdout_text, "position:\nH2: b3 R2G1 -\n\nH1: - B1R3 g3"));
  g_assert_nonnull(strstr(stdout_text, "\nall_moves:\n"));
  g_assert_nonnull(strstr(stdout_text, "all_moves_streamed: "));
  g_assert_null(strstr(stdout_text, "good_moves()"));
  g_assert_nonnull(strstr(stdout_text, "AI analysis after 2 replayed moves at depth 0:"));
  g_assert_nonnull(strstr(stdout_text, "score="));
  g_assert_true(strstr(stdout_text, "Move report after 2 replayed moves:") <
                strstr(stdout_text, "\nall_moves:\n"));
  g_assert_true(strstr(stdout_text, "\nall_moves:\n") <
                strstr(stdout_text, "AI analysis after 2 replayed moves at depth 0:"));
  g_assert_true(strstr(stdout_text, "Move report after 2 replayed moves:") <
                strstr(stdout_text, "AI analysis after 2 replayed moves at depth 0:"));
  g_assert_cmpint(g_remove(path), ==, 0);
}

static void test_homeworlds_profile_moves_replays_full_text_file_when_moves_omitted(void) {
  const char *content =
      "1. B1R3g3\n"
      "2. R2G1b3\n"
      "3. H1g+\n"
      "4. H2b+\n";
  g_autofree gchar *path = NULL;
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  g_autoptr(GError) error = NULL;
  gint wait_status = 0;
  gint fd = g_file_open_tmp("homeworlds-profile-XXXXXX.txt", &path, &error);

  g_assert_no_error(error);
  g_assert_cmpint(fd, >=, 0);
  g_assert_cmpint(close(fd), ==, 0);
  g_assert_true(g_file_set_contents(path, content, -1, &error));
  g_assert_no_error(error);

  gchar *argv[] = {
    (gchar *)HOMEWORLDS_PROFILE_MOVES_PATH,
    (gchar *)"--file",
    path,
    (gchar *)"--depth",
    (gchar *)"0",
    NULL,
  };

  g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_assert_no_error(error);
  g_assert_true(g_spawn_check_wait_status(wait_status, &error));
  g_assert_no_error(error);
  g_assert_cmpstr(stderr_text, ==, "");
  g_assert_nonnull(strstr(stdout_text, "Replayed moves from "));
  g_assert_nonnull(strstr(stdout_text, " (all moves requested):"));
  g_assert_nonnull(strstr(stdout_text, "1. B1R3g3\n2. R2G1b3\n3. H1g+\n4. H2b+"));
  g_assert_nonnull(strstr(stdout_text, "Current position after 4 replayed moves:"));
  g_assert_null(strstr(stdout_text, "AI analysis after"));
  g_assert_null(strstr(stdout_text, "Move report after"));
  g_assert_cmpint(g_remove(path), ==, 0);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/homeworlds-profile-moves/runs-ai-analysis-when-requested",
                  test_homeworlds_profile_moves_runs_ai_analysis_when_requested);
  g_test_add_func("/homeworlds-profile-moves/skips-ai-analysis-by-default",
                  test_homeworlds_profile_moves_skips_ai_analysis_by_default);
  g_test_add_func("/homeworlds-profile-moves/rejects-negative-move-count",
                  test_homeworlds_profile_moves_rejects_negative_move_count);
  g_test_add_func("/homeworlds-profile-moves/rejects-negative-depth",
                  test_homeworlds_profile_moves_rejects_negative_depth);
  g_test_add_func("/homeworlds-profile-moves/rejects-zero-node-limit",
                  test_homeworlds_profile_moves_rejects_zero_node_limit);
  g_test_add_func("/homeworlds-profile-moves/stops-after-node-limit",
                  test_homeworlds_profile_moves_stops_after_node_limit);
  g_test_add_func("/homeworlds-profile-moves/shows-forced-progress",
                  test_homeworlds_profile_moves_shows_forced_progress);
  g_test_add_func("/homeworlds-profile-moves/replays-file", test_homeworlds_profile_moves_replays_file);
  g_test_add_func("/homeworlds-profile-moves/replays-text-file", test_homeworlds_profile_moves_replays_text_file);
  g_test_add_func("/homeworlds-profile-moves/replays-full-text-file-when-moves-omitted",
                  test_homeworlds_profile_moves_replays_full_text_file_when_moves_omitted);
  return g_test_run();
}
