#include "sgf_autosave.h"

#include "app_paths.h"
#include "sgf_io.h"

#include <errno.h>
#include <glib/gstdio.h>

#define SGF_AUTOSAVE_SUBDIR "autosave"
#define SGF_AUTOSAVE_MAX_SUFFIX 99

char *sgf_autosave_format_current_timestamp(void) {
  g_autoptr(GDateTime) now = g_date_time_new_now_local();
  if (now == NULL) {
    return NULL;
  }

  return g_date_time_format(now, "%Y%m%d%H%M%S");
}

char *sgf_autosave_build_available_path(const char *game_id,
                                        const char *game_started_at,
                                        const char *move_at,
                                        GError **error) {
  g_return_val_if_fail(game_id != NULL, NULL);
  g_return_val_if_fail(game_id[0] != '\0', NULL);
  g_return_val_if_fail(game_started_at != NULL, NULL);
  g_return_val_if_fail(game_started_at[0] != '\0', NULL);
  g_return_val_if_fail(move_at != NULL, NULL);
  g_return_val_if_fail(move_at[0] != '\0', NULL);

  g_autofree char *autosave_root =
      ggame_app_paths_get_user_state_subdir(SGF_AUTOSAVE_ENV, SGF_AUTOSAVE_SUBDIR, error);
  if (autosave_root == NULL) {
    return NULL;
  }

  g_autofree char *game_dir = g_build_filename(autosave_root, game_id, NULL);
  if (g_mkdir_with_parents(game_dir, 0755) != 0) {
    int saved_errno = errno;
    g_set_error(error,
                G_FILE_ERROR,
                g_file_error_from_errno(saved_errno),
                "Failed to create SGF autosave directory %s: %s",
                game_dir,
                g_strerror(saved_errno));
    return NULL;
  }

  for (guint suffix = 0; suffix <= SGF_AUTOSAVE_MAX_SUFFIX; ++suffix) {
    g_autofree char *filename = g_strdup_printf("%s-%s-%02u", game_started_at, move_at, suffix);
    g_autofree char *path = g_build_filename(game_dir, filename, NULL);

    if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
      return g_steal_pointer(&path);
    }
  }

  g_set_error(error,
              G_FILE_ERROR,
              G_FILE_ERROR_EXIST,
              "No SGF autosave filename is available for %s-%s in %s",
              game_started_at,
              move_at,
              game_dir);
  return NULL;
}

gboolean sgf_autosave_save_tree(const char *game_id,
                                const char *game_started_at,
                                SgfTree *tree,
                                GError **error) {
  g_return_val_if_fail(SGF_IS_TREE(tree), FALSE);

  g_autofree char *move_at = sgf_autosave_format_current_timestamp();
  if (move_at == NULL) {
    g_set_error_literal(error,
                        G_FILE_ERROR,
                        G_FILE_ERROR_FAILED,
                        "Failed to format SGF autosave timestamp");
    return FALSE;
  }

  g_autofree char *path = sgf_autosave_build_available_path(game_id, game_started_at, move_at, error);
  if (path == NULL) {
    return FALSE;
  }

  return sgf_io_save_file(path, tree, error);
}
