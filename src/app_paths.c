#include "app_paths.h"

#include <glib.h>
#include <glib/gstdio.h>

static gboolean gcheckers_app_paths_is_directory(const char *path) {
  g_return_val_if_fail(path != NULL, FALSE);

  return g_file_test(path, G_FILE_TEST_IS_DIR);
}

char *gcheckers_app_paths_find_data_subdir(const char *env_name, const char *subdir_name) {
  g_return_val_if_fail(env_name != NULL, NULL);
  g_return_val_if_fail(subdir_name != NULL, NULL);

  const char *override = g_getenv(env_name);
  if (override != NULL && *override != '\0') {
    return g_strdup(override);
  }

  g_autofree char *user_dir = g_build_filename(g_get_user_data_dir(), "gcheckers", subdir_name, NULL);
  if (gcheckers_app_paths_is_directory(user_dir)) {
    return g_steal_pointer(&user_dir);
  }

  const char * const *system_dirs = g_get_system_data_dirs();
  if (system_dirs != NULL) {
    for (guint i = 0; system_dirs[i] != NULL; i++) {
      g_autofree char *system_dir = g_build_filename(system_dirs[i], "gcheckers", subdir_name, NULL);
      if (gcheckers_app_paths_is_directory(system_dir)) {
        return g_steal_pointer(&system_dir);
      }
      if (i == 0) {
        user_dir = g_steal_pointer(&system_dir);
      }
    }
  }

  g_autofree char *local_dir = g_build_filename(subdir_name, NULL);
  if (gcheckers_app_paths_is_directory(local_dir)) {
    return g_steal_pointer(&local_dir);
  }

  if (user_dir != NULL) {
    return g_steal_pointer(&user_dir);
  }

  return g_steal_pointer(&local_dir);
}

char *gcheckers_app_paths_get_user_state_subdir(const char *env_name,
                                                const char *subdir_name,
                                                GError **error) {
  g_return_val_if_fail(subdir_name != NULL, NULL);

  const char *override = NULL;
  if (env_name != NULL) {
    override = g_getenv(env_name);
  }

  g_autofree char *path = NULL;
  if (override != NULL && *override != '\0') {
    path = g_strdup(override);
  } else {
    path = g_build_filename(g_get_user_data_dir(), "gcheckers", subdir_name, NULL);
  }

  if (g_mkdir_with_parents(path, 0755) != 0) {
    int saved_errno = errno;
    g_set_error(error,
                G_FILE_ERROR,
                g_file_error_from_errno(saved_errno),
                "Failed to create state directory %s: %s",
                path,
                g_strerror(saved_errno));
    return NULL;
  }

  return g_steal_pointer(&path);
}
