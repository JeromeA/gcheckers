#ifndef GGAME_APP_SETTINGS_H
#define GGAME_APP_SETTINGS_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define GCHECKERS_APP_SETTINGS_SCHEMA_ID "io.github.jeromea.gcheckers"
#define GCHECKERS_APP_SETTINGS_KEY_SEND_PUZZLE_USAGE "send-puzzle-usage-data"
#define GCHECKERS_APP_SETTINGS_KEY_SEND_APPLICATION_USAGE "send-application-usage-data"
#define GCHECKERS_APP_SETTINGS_KEY_PRIVACY_SETTINGS_SHOWN "privacy-settings-shown"

GSettings *ggame_app_settings_create(void);
gboolean ggame_app_settings_get_privacy_settings_shown(GSettings *settings);
gboolean ggame_app_settings_mark_privacy_settings_shown(GSettings *settings);

G_END_DECLS

#endif
