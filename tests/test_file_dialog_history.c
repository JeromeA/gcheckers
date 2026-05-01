#include "../src/file_dialog_history.h"
#include "test_profile_utils.h"

#include <glib.h>

static const char *ggame_file_dialog_history_key = "sgf-last-folder";

static void test_file_dialog_history_round_trip(void) {
  g_setenv("GSETTINGS_BACKEND", "memory", TRUE);

  g_autoptr(GSettings) settings = ggame_file_dialog_history_create_settings();
  g_assert_nonnull(settings);

  g_autoptr(GFile) initial_folder =
      ggame_file_dialog_history_get_initial_folder(settings, ggame_file_dialog_history_key);
  g_assert_null(initial_folder);

  g_autoptr(GFile) file = g_file_new_for_path("/tmp/gcheckers-puzzles/example.sgf");
  g_assert_true(ggame_file_dialog_history_remember_parent(settings, ggame_file_dialog_history_key, file));

  g_autoptr(GFile) remembered_folder =
      ggame_file_dialog_history_get_initial_folder(settings, ggame_file_dialog_history_key);
  g_assert_nonnull(remembered_folder);

  g_autofree char *remembered_path = g_file_get_path(remembered_folder);
  g_assert_cmpstr(remembered_path, ==, "/tmp/gcheckers-puzzles");
}

int main(int argc, char **argv) {
  ggame_test_init_profile(&argc, &argv, "checkers");
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/file-dialog-history/round-trip", test_file_dialog_history_round_trip);
  return g_test_run();
}
