#include <glib.h>
#include <string.h>

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

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/homeworlds-profile-moves/prints-report", test_homeworlds_profile_moves_prints_report);
  g_test_add_func("/homeworlds-profile-moves/rejects-negative-move-count",
                  test_homeworlds_profile_moves_rejects_negative_move_count);
  return g_test_run();
}
