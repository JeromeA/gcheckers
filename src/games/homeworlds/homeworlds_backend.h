#ifndef HOMEWORLDS_BACKEND_H
#define HOMEWORLDS_BACKEND_H

#include "game_backend.h"
#include "homeworlds_types.h"

extern const GameBackend homeworlds_game_backend;

GameBackendMoveList homeworlds_backend_list_good_moves_limited(const HomeworldsPosition *position,
                                                               gsize max_leaves,
                                                               gboolean *out_truncated);

#endif
