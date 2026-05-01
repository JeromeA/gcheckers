#ifndef GGAME_TEST_PROFILE_UTILS_H
#define GGAME_TEST_PROFILE_UTILS_H

#include "../src/game_app_profile.h"

#include <glib.h>

G_BEGIN_DECLS

void ggame_test_init_profile(int *argc, char ***argv, const char *default_profile_id);
gboolean ggame_test_require_profile_kind(GGameAppKind kind);
const char *ggame_test_profile_kind_name(GGameAppKind kind);

G_END_DECLS

#endif
