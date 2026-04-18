#include "app_settings.h"

GSettings *gcheckers_app_settings_create(void) {
  GSettingsSchemaSource *default_source = g_settings_schema_source_get_default();
  GSettingsSchema *schema = NULL;
  if (default_source != NULL) {
    schema = g_settings_schema_source_lookup(default_source, GCHECKERS_APP_SETTINGS_SCHEMA_ID, TRUE);
  }

  g_autoptr(GSettingsSchemaSource) local_source = NULL;
  if (schema == NULL) {
    g_autoptr(GError) error = NULL;
    local_source =
        g_settings_schema_source_new_from_directory("data/schemas", default_source, FALSE, &error);
    if (local_source == NULL) {
      g_debug("Unable to load local GSettings schemas: %s", error != NULL ? error->message : "unknown error");
      return NULL;
    }

    schema = g_settings_schema_source_lookup(local_source, GCHECKERS_APP_SETTINGS_SCHEMA_ID, FALSE);
  }

  if (schema == NULL) {
    g_debug("Missing GSettings schema %s", GCHECKERS_APP_SETTINGS_SCHEMA_ID);
    return NULL;
  }

  GSettings *settings = g_settings_new_full(schema, NULL, NULL);
  g_settings_schema_unref(schema);
  return settings;
}

gboolean gcheckers_app_settings_get_privacy_settings_shown(GSettings *settings) {
  g_return_val_if_fail(G_IS_SETTINGS(settings), FALSE);

  return g_settings_get_boolean(settings, GCHECKERS_APP_SETTINGS_KEY_PRIVACY_SETTINGS_SHOWN);
}

gboolean gcheckers_app_settings_mark_privacy_settings_shown(GSettings *settings) {
  g_return_val_if_fail(G_IS_SETTINGS(settings), FALSE);

  return g_settings_set_boolean(settings, GCHECKERS_APP_SETTINGS_KEY_PRIVACY_SETTINGS_SHOWN, TRUE);
}
