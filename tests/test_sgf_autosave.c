#include "../src/sgf_autosave.h"

#include "../src/sgf_tree.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

static gboolean filename_has_autosave_shape(const char *name, const char *expected_start) {
  g_return_val_if_fail(name != NULL, FALSE);
  g_return_val_if_fail(expected_start != NULL, FALSE);

  if (strlen(name) != 32) {
    return FALSE;
  }
  if (strncmp(name, expected_start, 14) != 0 || name[14] != '-' || name[29] != '-') {
    return FALSE;
  }

  for (guint i = 0; i < 32; ++i) {
    if (i == 14 || i == 29) {
      continue;
    }
    if (!g_ascii_isdigit(name[i])) {
      return FALSE;
    }
  }

  return TRUE;
}

static GPtrArray *list_directory_names(const char *dir_path) {
  g_return_val_if_fail(dir_path != NULL, NULL);

  g_autoptr(GError) error = NULL;
  GDir *dir = g_dir_open(dir_path, 0, &error);
  g_assert_no_error(error);
  g_assert_nonnull(dir);

  GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
  const char *name = NULL;
  while ((name = g_dir_read_name(dir)) != NULL) {
    g_ptr_array_add(names, g_strdup(name));
  }
  g_dir_close(dir);

  return names;
}

static void test_sgf_autosave_build_available_path_uses_next_suffix(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root = g_dir_make_tmp("gcheckers-sgf-autosave-path-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_nonnull(root);
  g_setenv(SGF_AUTOSAVE_ENV, root, TRUE);

  g_autofree char *path_0 =
      sgf_autosave_build_available_path("checkers", "20260102030405", "20260102030406", &error);
  g_assert_no_error(error);
  g_assert_nonnull(path_0);

  g_autofree char *basename_0 = g_path_get_basename(path_0);
  g_assert_cmpstr(basename_0, ==, "20260102030405-20260102030406-00");
  g_assert_true(g_file_set_contents(path_0, "occupied", -1, &error));
  g_assert_no_error(error);

  g_autofree char *path_1 =
      sgf_autosave_build_available_path("checkers", "20260102030405", "20260102030406", &error);
  g_assert_no_error(error);
  g_assert_nonnull(path_1);

  g_autofree char *basename_1 = g_path_get_basename(path_1);
  g_assert_cmpstr(basename_1, ==, "20260102030405-20260102030406-01");
}

static void test_sgf_autosave_save_tree_writes_sgf_in_game_directory(void) {
  static const char *game_started_at = "20260102030405";
  g_autoptr(GError) error = NULL;
  g_autofree char *root = g_dir_make_tmp("gcheckers-sgf-autosave-save-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_nonnull(root);
  g_setenv(SGF_AUTOSAVE_ENV, root, TRUE);

  g_autoptr(SgfTree) tree = sgf_tree_new();
  g_assert_nonnull(tree);
  g_assert_true(sgf_autosave_save_tree("boop", game_started_at, tree, &error));
  g_assert_no_error(error);

  g_autofree char *game_dir = g_build_filename(root, "boop", NULL);
  g_assert_true(g_file_test(game_dir, G_FILE_TEST_IS_DIR));

  g_autoptr(GPtrArray) names = list_directory_names(game_dir);
  g_assert_cmpuint(names->len, ==, 1);

  const char *name = g_ptr_array_index(names, 0);
  g_assert_true(filename_has_autosave_shape(name, game_started_at));

  g_autofree char *path = g_build_filename(game_dir, name, NULL);
  g_autofree char *contents = NULL;
  gsize contents_len = 0;
  g_assert_true(g_file_get_contents(path, &contents, &contents_len, &error));
  g_assert_no_error(error);
  g_assert_true(contents_len > 0);
  g_assert_nonnull(strstr(contents, "AP[gcheckers]"));
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/sgf-autosave/build-available-path-uses-next-suffix",
                  test_sgf_autosave_build_available_path_uses_next_suffix);
  g_test_add_func("/sgf-autosave/save-tree-writes-sgf-in-game-directory",
                  test_sgf_autosave_save_tree_writes_sgf_in_game_directory);
  return g_test_run();
}
