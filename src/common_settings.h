#ifndef GGAME_COMMON_SETTINGS_H
#define GGAME_COMMON_SETTINGS_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define GGAME_COMMON_SETTINGS_SCHEMA_ID "io.github.jeromea.ggame"
#define GGAME_COMMON_SETTINGS_KEY_SGF_LAST_FOLDER "sgf-last-folder"
#define GGAME_COMMON_SETTINGS_KEY_IMPORT_REMEMBER "import-remember"
#define GGAME_COMMON_SETTINGS_KEY_IMPORT_EMAIL "import-email"
#define GGAME_COMMON_SETTINGS_KEY_IMPORT_PASSWORD "import-password"
#define GGAME_COMMON_SETTINGS_KEY_WINDOW_WIDTH "window-width"
#define GGAME_COMMON_SETTINGS_KEY_WINDOW_HEIGHT "window-height"
#define GGAME_COMMON_SETTINGS_KEY_WINDOW_LAYOUT_SAVED "window-layout-saved"
#define GGAME_COMMON_SETTINGS_KEY_WINDOW_BOARD_PANEL_WIDTH "window-board-panel-width"
#define GGAME_COMMON_SETTINGS_KEY_WINDOW_NAVIGATION_PANEL_WIDTH "window-navigation-panel-width"
#define GGAME_COMMON_SETTINGS_KEY_WINDOW_ANALYSIS_PANEL_WIDTH "window-analysis-panel-width"
#define GGAME_COMMON_SETTINGS_KEY_WINDOW_SHOW_NAVIGATION_DRAWER "window-show-navigation-drawer"
#define GGAME_COMMON_SETTINGS_KEY_WINDOW_SHOW_ANALYSIS_DRAWER "window-show-analysis-drawer"
#define GGAME_COMMON_SETTINGS_KEY_WINDOW_SHOW_MOVE_REPORT "window-show-move-report"

GSettings *ggame_common_settings_create(void);

G_END_DECLS

#endif
