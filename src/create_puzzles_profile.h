#ifndef GGAME_CREATE_PUZZLES_PROFILE_H
#define GGAME_CREATE_PUZZLES_PROFILE_H

#include <glib.h>

G_BEGIN_DECLS

typedef int (*GGameCreatePuzzlesProfileRunFunc)(int argc, char **argv, guint default_depth);

typedef struct {
  const char *profile_id;
  GGameCreatePuzzlesProfileRunFunc run;
} GGameCreatePuzzlesProfile;

const GGameCreatePuzzlesProfile *ggame_create_puzzles_profile_lookup(const char *profile_id);

G_END_DECLS

#endif
