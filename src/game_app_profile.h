#ifndef GGAME_APP_PROFILE_H
#define GGAME_APP_PROFILE_H

#include "game_backend.h"

#include <glib.h>

G_BEGIN_DECLS

typedef struct _GtkWidget GtkWidget;
typedef struct _GtkApplication GtkApplication;
typedef struct _GtkWindow GtkWindow;
typedef struct _BoardView BoardView;
typedef struct _GGameModel GGameModel;

typedef enum {
  GGAME_APP_KIND_CHECKERS = 0,
  GGAME_APP_KIND_HOMEWORLDS,
  GGAME_APP_KIND_BOOP,
} GGameAppKind;

typedef struct {
  gboolean supports_shared_shell;
  gboolean supports_sgf_files;
  gboolean supports_ai_players;
  gboolean supports_analysis;
  gboolean supports_puzzles;
  gboolean supports_import;
  gboolean supports_settings;
  gboolean supports_save_position;
  gboolean supports_edit_mode;
} GGameAppFeatures;

typedef struct {
  GtkWindow *(*create_window)(GtkApplication *app);
  GtkWidget *(*create_board_host)(GGameModel *model, BoardView *board_view);
} GGameAppUiHooks;

typedef struct {
  gint default_board_panel_width;
  gint default_navigation_panel_width;
  gint default_analysis_panel_width;
  gboolean show_navigation_drawer_by_default;
  gboolean show_analysis_drawer_by_default;
} GGameAppLayout;

typedef struct {
  GGameAppKind kind;
  const char *id;
  const char *app_id;
  const char *display_name;
  const char *window_title_name;
  const char *settings_schema_id;
  const GameBackend *backend;
  GGameAppFeatures features;
  GGameAppUiHooks ui;
  GGameAppLayout layout;
} GGameAppProfile;

const GGameAppProfile *ggame_app_profile_get_by_kind(GGameAppKind kind);
const GGameAppProfile *ggame_app_profile_lookup_by_id(const char *id);
const GGameAppProfile *ggame_app_profile_lookup_by_app_id(const char *app_id);
gboolean ggame_app_profile_set_active(const GGameAppProfile *profile);
gboolean ggame_app_profile_set_active_by_id(const char *id);
const GGameAppProfile *ggame_active_app_profile(void);
gboolean ggame_app_profile_supports_puzzle_catalog(const GGameAppProfile *profile);

G_END_DECLS

#endif
