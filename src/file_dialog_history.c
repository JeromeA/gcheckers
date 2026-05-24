#include "file_dialog_history.h"

#include "common_settings.h"

GSettings *ggame_file_dialog_history_create_settings(void) {
  return ggame_common_settings_create();
}

GFile *ggame_file_dialog_history_get_initial_folder(GSettings *settings, const char *key) {
  g_return_val_if_fail(G_IS_SETTINGS(settings), NULL);
  g_return_val_if_fail(key != NULL, NULL);

  g_autofree char *folder_uri = g_settings_get_string(settings, key);
  if (folder_uri == NULL || folder_uri[0] == '\0') {
    return NULL;
  }

  return g_file_new_for_uri(folder_uri);
}

gboolean ggame_file_dialog_history_remember_parent(GSettings *settings, const char *key, GFile *file) {
  g_return_val_if_fail(G_IS_SETTINGS(settings), FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(G_IS_FILE(file), FALSE);

  g_autoptr(GFile) folder = g_file_get_parent(file);
  if (folder == NULL) {
    return FALSE;
  }

  g_autofree char *folder_uri = g_file_get_uri(folder);
  if (folder_uri == NULL || folder_uri[0] == '\0') {
    return FALSE;
  }

  return g_settings_set_string(settings, key, folder_uri);
}
