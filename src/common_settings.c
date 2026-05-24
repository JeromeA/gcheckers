#include "common_settings.h"

#include "game_app_profile.h"

static char *ggame_common_settings_profile_path_component(const char *profile_id) {
  GString *component = g_string_new(NULL);

  if (profile_id == NULL || profile_id[0] == '\0') {
    g_string_append(component, "default");
  } else {
    for (const char *p = profile_id; *p != '\0'; ++p) {
      char c = *p;
      g_string_append_c(component, g_ascii_isalnum(c) || c == '-' || c == '_' ? c : '-');
    }
  }

  return g_string_free(component, FALSE);
}

GSettings *ggame_common_settings_create(void) {
  const GGameAppProfile *profile = ggame_active_app_profile();
  GSettingsSchemaSource *default_source = g_settings_schema_source_get_default();
  GSettingsSchema *schema = NULL;

  if (default_source != NULL) {
    schema = g_settings_schema_source_lookup(default_source, GGAME_COMMON_SETTINGS_SCHEMA_ID, TRUE);
  }

  g_autoptr(GSettingsSchemaSource) local_source = NULL;
  if (schema == NULL) {
    g_autoptr(GError) error = NULL;
    local_source = g_settings_schema_source_new_from_directory("data/schemas", default_source, FALSE, &error);
    if (local_source == NULL) {
      g_debug("Unable to load local GSettings schemas: %s", error != NULL ? error->message : "unknown error");
      return NULL;
    }

    schema = g_settings_schema_source_lookup(local_source, GGAME_COMMON_SETTINGS_SCHEMA_ID, FALSE);
  }

  if (schema == NULL) {
    g_debug("Missing GSettings schema %s", GGAME_COMMON_SETTINGS_SCHEMA_ID);
    return NULL;
  }

  g_autofree char *component =
      ggame_common_settings_profile_path_component(profile != NULL ? profile->id : NULL);
  g_autofree char *path = g_strdup_printf("/io/github/jeromea/ggame/%s/", component);
  GSettings *settings = g_settings_new_full(schema, NULL, path);
  g_settings_schema_unref(schema);
  return settings;
}
