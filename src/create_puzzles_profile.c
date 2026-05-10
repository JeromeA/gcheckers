#include "create_puzzles_profile.h"

#include "games/boop/boop_create_puzzles.h"
#include "games/checkers/checkers_create_puzzles.h"

#include <string.h>

static const GGameCreatePuzzlesProfile create_puzzles_profiles[] = {
  {
    .profile_id = "checkers",
    .run = checkers_create_puzzles_main,
  },
  {
    .profile_id = "boop",
    .run = boop_create_puzzles_main,
  },
};

const GGameCreatePuzzlesProfile *ggame_create_puzzles_profile_lookup(const char *profile_id) {
  g_return_val_if_fail(profile_id != NULL, NULL);

  for (guint i = 0; i < G_N_ELEMENTS(create_puzzles_profiles); ++i) {
    if (g_strcmp0(create_puzzles_profiles[i].profile_id, profile_id) == 0) {
      return &create_puzzles_profiles[i];
    }
  }

  return NULL;
}
