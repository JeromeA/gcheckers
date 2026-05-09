#ifndef GGAME_CREATE_PUZZLES_LAUNCHER_H
#define GGAME_CREATE_PUZZLES_LAUNCHER_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
  const char *profile_id;
  guint default_depth;
} GGameCreatePuzzlesLauncherConfig;

void ggame_create_puzzles_launcher_config_for_program_name(const char *program_name,
                                                           GGameCreatePuzzlesLauncherConfig *out_config);

G_END_DECLS

#endif
