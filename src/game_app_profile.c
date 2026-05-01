#include "game_app_profile.h"

#include "games/boop/boop_backend.h"
#include "games/boop/boop_controls.h"
#include "games/checkers/checkers_backend.h"
#include "games/homeworlds/homeworlds_app_window.h"
#include "games/homeworlds/homeworlds_backend.h"

static const GGameAppProfile checkers_app_profile = {
  .kind = GGAME_APP_KIND_CHECKERS,
  .id = "checkers",
  .app_id = "io.github.jeromea.gcheckers",
  .display_name = "Checkers",
  .window_title_name = "gcheckers",
  .settings_schema_id = "io.github.jeromea.gcheckers",
  .backend = &checkers_game_backend,
  .features =
      {
          .supports_shared_shell = TRUE,
          .supports_sgf_files = TRUE,
          .supports_ai_players = TRUE,
          .supports_analysis = TRUE,
          .supports_puzzles = TRUE,
          .supports_import = TRUE,
          .supports_settings = TRUE,
          .supports_save_position = TRUE,
          .supports_edit_mode = TRUE,
      },
  .layout =
      {
          .default_board_panel_width = 500,
          .default_navigation_panel_width = 300,
          .default_analysis_panel_width = 300,
          .show_navigation_drawer_by_default = TRUE,
          .show_analysis_drawer_by_default = TRUE,
      },
};

static const GGameAppProfile homeworlds_app_profile = {
  .kind = GGAME_APP_KIND_HOMEWORLDS,
  .id = "homeworlds",
  .app_id = "io.github.jeromea.ghomeworlds",
  .display_name = "Homeworlds",
  .window_title_name = "ghomeworlds",
  .settings_schema_id = NULL,
  .backend = &homeworlds_game_backend,
  .features =
      {
          .supports_shared_shell = FALSE,
          .supports_sgf_files = FALSE,
          .supports_ai_players = FALSE,
          .supports_analysis = FALSE,
          .supports_puzzles = FALSE,
          .supports_import = FALSE,
          .supports_settings = FALSE,
          .supports_save_position = FALSE,
          .supports_edit_mode = FALSE,
      },
  .ui =
      {
          .create_window = ghomeworlds_app_window_create,
      },
  .layout =
      {
          .default_board_panel_width = 500,
          .default_navigation_panel_width = 300,
          .default_analysis_panel_width = 300,
          .show_navigation_drawer_by_default = TRUE,
          .show_analysis_drawer_by_default = FALSE,
      },
};

static const GGameAppProfile boop_app_profile = {
  .kind = GGAME_APP_KIND_BOOP,
  .id = "boop",
  .app_id = "io.github.jeromea.gboop",
  .display_name = "Boop",
  .window_title_name = "gboop",
  .settings_schema_id = "io.github.jeromea.gboop",
  .backend = &boop_game_backend,
  .features =
      {
          .supports_shared_shell = TRUE,
          .supports_sgf_files = TRUE,
          .supports_ai_players = TRUE,
          .supports_analysis = TRUE,
          .supports_puzzles = FALSE,
          .supports_import = FALSE,
          .supports_settings = TRUE,
          .supports_save_position = TRUE,
          .supports_edit_mode = FALSE,
      },
  .ui =
      {
          .create_board_host = gboop_controls_create_board_host,
      },
  .layout =
      {
          .default_board_panel_width = 760,
          .default_navigation_panel_width = 300,
          .default_analysis_panel_width = 300,
          .show_navigation_drawer_by_default = TRUE,
          .show_analysis_drawer_by_default = FALSE,
      },
};

static const GGameAppProfile *const ggame_app_profiles[] = {
  &checkers_app_profile,
  &homeworlds_app_profile,
  &boop_app_profile,
};

static const GGameAppProfile *active_app_profile = &checkers_app_profile;

static gboolean ggame_app_profile_is_registered(const GGameAppProfile *profile) {
  g_return_val_if_fail(profile != NULL, FALSE);

  for (guint i = 0; i < G_N_ELEMENTS(ggame_app_profiles); ++i) {
    if (ggame_app_profiles[i] == profile) {
      return TRUE;
    }
  }

  return FALSE;
}

const GGameAppProfile *ggame_app_profile_get_by_kind(GGameAppKind kind) {
  for (guint i = 0; i < G_N_ELEMENTS(ggame_app_profiles); ++i) {
    if (ggame_app_profiles[i]->kind == kind) {
      return ggame_app_profiles[i];
    }
  }

  g_debug("Unknown app profile kind %d", (gint)kind);
  return NULL;
}

const GGameAppProfile *ggame_app_profile_lookup_by_id(const char *id) {
  g_return_val_if_fail(id != NULL, NULL);

  for (guint i = 0; i < G_N_ELEMENTS(ggame_app_profiles); ++i) {
    if (g_strcmp0(ggame_app_profiles[i]->id, id) == 0) {
      return ggame_app_profiles[i];
    }
  }

  g_debug("Unknown app profile id %s", id);
  return NULL;
}

const GGameAppProfile *ggame_app_profile_lookup_by_app_id(const char *app_id) {
  g_return_val_if_fail(app_id != NULL, NULL);

  for (guint i = 0; i < G_N_ELEMENTS(ggame_app_profiles); ++i) {
    if (g_strcmp0(ggame_app_profiles[i]->app_id, app_id) == 0) {
      return ggame_app_profiles[i];
    }
  }

  g_debug("Unknown application id %s", app_id);
  return NULL;
}

gboolean ggame_app_profile_set_active(const GGameAppProfile *profile) {
  g_return_val_if_fail(profile != NULL, FALSE);

  if (!ggame_app_profile_is_registered(profile)) {
    g_debug("Refusing to activate an unregistered app profile");
    return FALSE;
  }

  active_app_profile = profile;
  return TRUE;
}

gboolean ggame_app_profile_set_active_by_id(const char *id) {
  const GGameAppProfile *profile = ggame_app_profile_lookup_by_id(id);
  if (profile == NULL) {
    return FALSE;
  }

  return ggame_app_profile_set_active(profile);
}

const GGameAppProfile *ggame_active_app_profile(void) {
  return active_app_profile;
}

gboolean ggame_app_profile_supports_puzzle_catalog(const GGameAppProfile *profile) {
  g_return_val_if_fail(profile != NULL, FALSE);

  return profile->features.supports_puzzles &&
         profile->backend != NULL &&
         profile->backend->id != NULL &&
         profile->backend->variant_count > 0 &&
         profile->backend->variant_at != NULL;
}
