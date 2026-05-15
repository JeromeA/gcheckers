#ifndef HOMEWORLDS_RANDOM_AI_H
#define HOMEWORLDS_RANDOM_AI_H

#include "homeworlds_types.h"

#include <glib.h>

G_BEGIN_DECLS

gboolean homeworlds_random_ai_build_move(const HomeworldsPosition *position, GRand *rand, HomeworldsMove *out_move);

G_END_DECLS

#endif
