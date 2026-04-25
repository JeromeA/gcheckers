#ifndef GGAME_APP_PATHS_H
#define GGAME_APP_PATHS_H

#include <glib.h>

char *ggame_app_paths_find_data_subdir(const char *env_name, const char *subdir_name);
char *ggame_app_paths_get_user_state_subdir(const char *env_name,
                                                const char *subdir_name,
                                                GError **error);

#endif
