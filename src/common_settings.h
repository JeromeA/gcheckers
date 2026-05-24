#ifndef GGAME_COMMON_SETTINGS_H
#define GGAME_COMMON_SETTINGS_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define GGAME_COMMON_SETTINGS_SCHEMA_ID "io.github.jeromea.ggame"
#define GGAME_COMMON_SETTINGS_KEY_SGF_LAST_FOLDER "sgf-last-folder"
#define GGAME_COMMON_SETTINGS_KEY_WINDOW_WIDTH "window-width"
#define GGAME_COMMON_SETTINGS_KEY_WINDOW_HEIGHT "window-height"

GSettings *ggame_common_settings_create(void);

G_END_DECLS

#endif
