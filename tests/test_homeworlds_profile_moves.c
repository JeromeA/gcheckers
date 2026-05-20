#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

#ifndef HOMEWORLDS_PROFILE_MOVES_PATH
#error "HOMEWORLDS_PROFILE_MOVES_PATH must be defined"
#endif

static void test_homeworlds_profile_moves_prints_report(void) {
  gchar *argv[] = {
    (gchar *)HOMEWORLDS_PROFILE_MOVES_PATH,
    (gchar *)"--moves",
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
  g_assert_nonnull(strstr(stdout_text, "Generated moves (2 requested):"));
  g_assert_nonnull(strstr(stdout_text, "Current position after 2 generated moves:"));
  g_assert_nonnull(strstr(stdout_text, "b3 R2G1 -\n\n- B1R3 g3"));
  g_assert_nonnull(strstr(stdout_text, "Move report after 2 generated moves:"));
  g_assert_nonnull(strstr(stdout_text, "good_moves()"));
  g_assert_nonnull(strstr(stdout_text, "all possible moves minus good_moves()"));
  g_assert_true(strstr(stdout_text, "Generated moves (2 requested):") <
                strstr(stdout_text, "Current position after 2 generated moves:"));
  g_assert_true(strstr(stdout_text, "Current position after 2 generated moves:") <
                strstr(stdout_text, "Move report after 2 generated moves:"));
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

static void test_homeworlds_profile_moves_replays_file(void) {
  const char *content =
      "(;AP[gcheckers]CA[UTF-8]FF[4]GM[40];B[B1R3g3];W[R2G1b3];B[H1 r+]"
      ";W[H2 g3-/B1 g+/H2 g+/B1 g+])";
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
  g_assert_nonnull(strstr(stdout_text, "b3 R2G1 -\n\n- B1R3 g3"));
  g_assert_nonnull(strstr(stdout_text, "Move report after 2 replayed moves:"));
  g_assert_true(strstr(stdout_text, "Replayed moves from ") <
                strstr(stdout_text, "Current position after 2 replayed moves:"));
  g_assert_true(strstr(stdout_text, "Current position after 2 replayed moves:") <
                strstr(stdout_text, "Move report after 2 replayed moves:"));
  g_assert_cmpint(g_remove(path), ==, 0);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/homeworlds-profile-moves/prints-report", test_homeworlds_profile_moves_prints_report);
  g_test_add_func("/homeworlds-profile-moves/rejects-negative-move-count",
                  test_homeworlds_profile_moves_rejects_negative_move_count);
  g_test_add_func("/homeworlds-profile-moves/replays-file", test_homeworlds_profile_moves_replays_file);
  return g_test_run();
}
