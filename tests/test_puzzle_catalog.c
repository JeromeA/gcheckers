#include "../src/puzzle_catalog.h"

#include <glib.h>
#include <glib/gstdio.h>

static void test_puzzle_catalog_loads_sorted_variant_entries(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root = g_dir_make_tmp("gcheckers-puzzle-catalog-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_nonnull(root);

  g_autofree char *variant_dir = g_build_filename(root, "russian", NULL);
  g_assert_cmpint(g_mkdir_with_parents(variant_dir, 0755), ==, 0);

  g_autofree char *puzzle_seven = g_build_filename(variant_dir, "puzzle-0007.sgf", NULL);
  g_autofree char *puzzle_two = g_build_filename(variant_dir, "puzzle-0002.sgf", NULL);
  g_autofree char *noise = g_build_filename(variant_dir, "notes.txt", NULL);
  g_assert_true(g_file_set_contents(puzzle_seven, "(;FF[4]GM[40])", -1, &error));
  g_assert_no_error(error);
  g_assert_true(g_file_set_contents(puzzle_two, "(;FF[4]GM[40])", -1, &error));
  g_assert_no_error(error);
  g_assert_true(g_file_set_contents(noise, "ignore", -1, &error));
  g_assert_no_error(error);

  g_setenv("GCHECKERS_PUZZLES_DIR", root, TRUE);
  g_autoptr(GPtrArray) entries = checkers_puzzle_catalog_load_for_ruleset(PLAYER_RULESET_RUSSIAN, &error);
  g_assert_no_error(error);
  g_assert_nonnull(entries);
  g_assert_cmpuint(entries->len, ==, 2);

  CheckersPuzzleCatalogEntry *first = g_ptr_array_index(entries, 0);
  CheckersPuzzleCatalogEntry *second = g_ptr_array_index(entries, 1);
  g_assert_cmpuint(first->puzzle_number, ==, 2);
  g_assert_cmpstr(first->basename, ==, "puzzle-0002.sgf");
  g_assert_cmpstr(first->puzzle_id, ==, "russian/puzzle-0002.sgf");
  g_assert_cmpuint(second->puzzle_number, ==, 7);
  g_assert_cmpstr(second->basename, ==, "puzzle-0007.sgf");
  g_assert_cmpstr(second->puzzle_id, ==, "russian/puzzle-0007.sgf");

  g_unsetenv("GCHECKERS_PUZZLES_DIR");
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/puzzle-catalog/load-sorted-variant-entries",
                  test_puzzle_catalog_loads_sorted_variant_entries);
  return g_test_run();
}
