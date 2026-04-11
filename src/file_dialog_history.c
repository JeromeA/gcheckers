#include "file_dialog_history.h"

static const char *gcheckers_settings_schema_id = "io.github.JeromeA.gcheckers";

GSettings *gcheckers_file_dialog_history_create_settings(void) {
  GSettingsSchemaSource *default_source = g_settings_schema_source_get_default();
  GSettingsSchema *schema = NULL;
  if (default_source != NULL) {
    schema = g_settings_schema_source_lookup(default_source, gcheckers_settings_schema_id, TRUE);
  }

  g_autoptr(GSettingsSchemaSource) local_source = NULL;
  if (schema == NULL) {
    g_autoptr(GError) error = NULL;
    local_source =
        g_settings_schema_source_new_from_directory("data/schemas", default_source, FALSE, &error);
    if (!local_source) {
      g_debug("Unable to load local GSettings schemas: %s", error != NULL ? error->message : "unknown error");
      return NULL;
    }

    schema = g_settings_schema_source_lookup(local_source, gcheckers_settings_schema_id, FALSE);
  }

  if (schema == NULL) {
    g_debug("Missing GSettings schema %s", gcheckers_settings_schema_id);
    return NULL;
  }

  GSettings *settings = g_settings_new_full(schema, NULL, NULL);
  g_settings_schema_unref(schema);
  return settings;
}

GFile *gcheckers_file_dialog_history_get_initial_folder(GSettings *settings, const char *key) {
  g_return_val_if_fail(G_IS_SETTINGS(settings), NULL);
  g_return_val_if_fail(key != NULL, NULL);

  g_autofree char *folder_uri = g_settings_get_string(settings, key);
  if (folder_uri == NULL || folder_uri[0] == '\0') {
    return NULL;
  }

  return g_file_new_for_uri(folder_uri);
}

gboolean gcheckers_file_dialog_history_remember_parent(GSettings *settings, const char *key, GFile *file) {
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
