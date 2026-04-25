#include "puzzle_catalog.h"

#include "app_paths.h"

#include <glib/gstdio.h>

static gboolean game_puzzle_catalog_parse_basename(const char *basename, guint *out_number) {
  g_return_val_if_fail(basename != NULL, FALSE);
  g_return_val_if_fail(out_number != NULL, FALSE);

  if (!g_str_has_prefix(basename, "puzzle-") || !g_str_has_suffix(basename, ".sgf")) {
    return FALSE;
  }

  gsize prefix_len = strlen("puzzle-");
  gsize suffix_len = strlen(".sgf");
  gsize length = strlen(basename);
  if (length <= prefix_len + suffix_len) {
    return FALSE;
  }

  g_autofree char *number_text = g_strndup(basename + prefix_len, length - prefix_len - suffix_len);
  if (number_text[0] == '\0') {
    return FALSE;
  }

  for (const char *p = number_text; *p != '\0'; p++) {
    if (!g_ascii_isdigit(*p)) {
      return FALSE;
    }
  }

  guint64 parsed = g_ascii_strtoull(number_text, NULL, 10);
  if (parsed > G_MAXUINT) {
    return FALSE;
  }

  *out_number = (guint) parsed;
  return TRUE;
}

static gint game_puzzle_catalog_entry_compare(gconstpointer left, gconstpointer right) {
  const GamePuzzleCatalogEntry *entry_left = *(const GamePuzzleCatalogEntry *const *) left;
  const GamePuzzleCatalogEntry *entry_right = *(const GamePuzzleCatalogEntry *const *) right;

  g_return_val_if_fail(entry_left != NULL, 0);
  g_return_val_if_fail(entry_right != NULL, 0);

  if (entry_left->puzzle_number < entry_right->puzzle_number) {
    return -1;
  }
  if (entry_left->puzzle_number > entry_right->puzzle_number) {
    return 1;
  }

  return g_strcmp0(entry_left->basename, entry_right->basename);
}

void game_puzzle_catalog_entry_free(GamePuzzleCatalogEntry *entry) {
  if (entry == NULL) {
    return;
  }

  g_clear_pointer(&entry->basename, g_free);
  g_clear_pointer(&entry->path, g_free);
  g_clear_pointer(&entry->puzzle_id, g_free);
  g_free(entry);
}

GPtrArray *game_puzzle_catalog_load_variant(const GameBackend *backend,
                                            const GameBackendVariant *variant,
                                            GError **error) {
  g_return_val_if_fail(backend != NULL, NULL);
  g_return_val_if_fail(backend->id != NULL, NULL);
  g_return_val_if_fail(variant != NULL, NULL);
  g_return_val_if_fail(variant->short_name != NULL, NULL);

  g_autofree char *puzzles_root = ggame_app_paths_find_data_subdir("GCHECKERS_PUZZLES_DIR", "puzzles");
  if (puzzles_root == NULL) {
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "Failed to resolve puzzle root directory");
    return NULL;
  }

  g_autofree char *variant_dir = g_build_filename(puzzles_root, backend->id, variant->short_name, NULL);
  GPtrArray *entries = g_ptr_array_new_with_free_func((GDestroyNotify) game_puzzle_catalog_entry_free);
  if (!g_file_test(variant_dir, G_FILE_TEST_IS_DIR)) {
    return entries;
  }

  g_autoptr(GDir) dir = g_dir_open(variant_dir, 0, error);
  if (dir == NULL) {
    g_ptr_array_unref(entries);
    return NULL;
  }

  for (const char *name = g_dir_read_name(dir); name != NULL; name = g_dir_read_name(dir)) {
    guint puzzle_number = 0;
    if (!game_puzzle_catalog_parse_basename(name, &puzzle_number)) {
      continue;
    }

    GamePuzzleCatalogEntry *entry = g_new0(GamePuzzleCatalogEntry, 1);
    entry->puzzle_number = puzzle_number;
    entry->basename = g_strdup(name);
    entry->path = g_build_filename(variant_dir, name, NULL);
    entry->puzzle_id = g_strdup_printf("%s/%s/%s", backend->id, variant->short_name, name);
    g_ptr_array_add(entries, entry);
  }

  g_ptr_array_sort(entries, game_puzzle_catalog_entry_compare);
  return entries;
}
