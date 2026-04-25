#include "../src/app_paths.h"

#include <glib.h>
#include <glib/gstdio.h>

static void ensure_directory(const char *path) {
  int rc = g_mkdir_with_parents(path, 0755);
  g_assert_cmpint(rc, ==, 0);
}

static void test_app_paths_env_override_wins(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root = g_dir_make_tmp("gcheckers-app-paths-env-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_nonnull(root);

  g_autofree char *override = g_build_filename(root, "custom-puzzles", NULL);
  g_setenv("GCHECKERS_TEST_OVERRIDE", override, TRUE);

  g_autofree char *resolved =
      ggame_app_paths_find_data_subdir("GCHECKERS_TEST_OVERRIDE", "puzzles");
  g_assert_cmpstr(resolved, ==, override);

  g_autofree char *variant_dir = g_build_filename(resolved, "american", NULL);
  g_autofree char *expected_variant_dir = g_build_filename(override, "american", NULL);
  g_assert_cmpstr(variant_dir, ==, expected_variant_dir);
}

static void test_app_paths_system_data_dir_is_used(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root = g_dir_make_tmp("gcheckers-app-paths-system-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_nonnull(root);

  g_autofree char *system_data = g_build_filename(root, "system-data", NULL);
  g_autofree char *puzzles_dir = g_build_filename(system_data, "gcheckers", "puzzles", NULL);
  ensure_directory(puzzles_dir);

  g_unsetenv("GCHECKERS_TEST_OVERRIDE");
  g_setenv("XDG_DATA_HOME", root, TRUE);
  g_setenv("XDG_DATA_DIRS", system_data, TRUE);

  g_autofree char *resolved =
      ggame_app_paths_find_data_subdir("GCHECKERS_TEST_OVERRIDE", "puzzles");
  g_assert_cmpstr(resolved, ==, puzzles_dir);

  g_autofree char *variant_dir = g_build_filename(resolved, "international", NULL);
  g_autofree char *expected_variant_dir = g_build_filename(puzzles_dir, "international", NULL);
  g_assert_cmpstr(variant_dir, ==, expected_variant_dir);
}

static void test_app_paths_user_state_dir_is_created(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root = g_dir_make_tmp("gcheckers-app-paths-state-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_nonnull(root);

  g_autofree char *override = g_build_filename(root, "puzzle-progress", NULL);
  g_setenv("GCHECKERS_PUZZLE_PROGRESS_DIR", override, TRUE);

  g_autofree char *resolved = ggame_app_paths_get_user_state_subdir("GCHECKERS_PUZZLE_PROGRESS_DIR",
                                                                        "ignored",
                                                                        &error);
  g_assert_no_error(error);
  g_assert_cmpstr(resolved, ==, override);
  g_assert_true(g_file_test(resolved, G_FILE_TEST_IS_DIR));
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/app-paths/env-override-wins", test_app_paths_env_override_wins);
  g_test_add_func("/app-paths/system-data-dir-is-used", test_app_paths_system_data_dir_is_used);
  g_test_add_func("/app-paths/user-state-dir-is-created", test_app_paths_user_state_dir_is_created);
  return g_test_run();
}
