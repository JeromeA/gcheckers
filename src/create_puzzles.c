#include "create_puzzles_launcher.h"
#include "create_puzzles_profile.h"
#include "game_app_profile.h"

#include <glib.h>
#include <stdio.h>

int main(int argc, char **argv) {
  GGameCreatePuzzlesLauncherConfig launcher = {0};

  ggame_create_puzzles_launcher_config_for_program_name(argv[0], &launcher);

  if (!ggame_app_profile_set_active_by_id(launcher.profile_id)) {
    g_printerr("Failed to activate %s profile\n", launcher.profile_id);
    return 1;
  }

  const GGameCreatePuzzlesProfile *profile = ggame_create_puzzles_profile_lookup(launcher.profile_id);
  if (profile == NULL || profile->run == NULL) {
    g_printerr("Unsupported create_puzzles profile: %s\n", launcher.profile_id);
    return 1;
  }

  return profile->run(argc, argv, launcher.default_depth);
}
