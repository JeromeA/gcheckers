#include "game_app_profile.h"

#if defined(GGAME_GAME_CHECKERS)
#include "games/checkers/checkers_backend.h"

static const GGameAppProfile active_app_profile = {
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
#elif defined(GGAME_GAME_HOMEWORLDS)
#include "games/homeworlds/homeworlds_backend.h"

static const GGameAppProfile active_app_profile = {
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
  .layout =
      {
          .default_board_panel_width = 500,
          .default_navigation_panel_width = 300,
          .default_analysis_panel_width = 300,
          .show_navigation_drawer_by_default = TRUE,
          .show_analysis_drawer_by_default = FALSE,
      },
};
#elif defined(GGAME_GAME_BOOP)
#include "games/boop/boop_backend.h"
#include "games/boop/boop_controls.h"

static const GGameAppProfile active_app_profile = {
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
          .supports_save_position = FALSE,
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
#else
#error "No game application profile selected. Define GGAME_GAME_CHECKERS, GGAME_GAME_HOMEWORLDS, GGAME_GAME_BOOP, or another game define."
#endif

const GGameAppProfile *ggame_active_app_profile(void) {
  return &active_app_profile;
}

gboolean ggame_app_profile_supports_puzzle_catalog(const GGameAppProfile *profile) {
  g_return_val_if_fail(profile != NULL, FALSE);

  return profile->features.supports_puzzles &&
         profile->backend != NULL &&
         profile->backend->id != NULL &&
         profile->backend->variant_count > 0 &&
         profile->backend->variant_at != NULL;
}
