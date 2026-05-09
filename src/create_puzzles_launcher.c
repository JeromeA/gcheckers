#include "create_puzzles_launcher.h"

typedef struct {
  const char *program_name;
  const char *profile_id;
  guint default_depth;
} GGameCreatePuzzlesLauncherPreset;

static const GGameCreatePuzzlesLauncherPreset ggame_create_puzzles_launcher_presets[] = {
  {
    .program_name = "checkers_create_puzzles",
    .profile_id = "checkers",
    .default_depth = 8,
  },
  {
    .program_name = "boop_create_puzzles",
    .profile_id = "boop",
    .default_depth = 4,
  },
};

void ggame_create_puzzles_launcher_config_for_program_name(const char *program_name,
                                                           GGameCreatePuzzlesLauncherConfig *out_config) {
  g_return_if_fail(program_name != NULL);
  g_return_if_fail(out_config != NULL);

  g_autofree char *basename = g_path_get_basename(program_name);
  for (guint i = 0; i < G_N_ELEMENTS(ggame_create_puzzles_launcher_presets); ++i) {
    const GGameCreatePuzzlesLauncherPreset *preset = &ggame_create_puzzles_launcher_presets[i];

    if (g_strcmp0(basename, preset->program_name) == 0) {
      *out_config = (GGameCreatePuzzlesLauncherConfig) {
        .profile_id = preset->profile_id,
        .default_depth = preset->default_depth,
      };
      return;
    }
  }

  *out_config = (GGameCreatePuzzlesLauncherConfig) {
    .profile_id = "checkers",
    .default_depth = 8,
  };
}
