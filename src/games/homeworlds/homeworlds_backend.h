#ifndef HOMEWORLDS_BACKEND_H
#define HOMEWORLDS_BACKEND_H

#include "game_backend.h"
#include "homeworlds_types.h"

typedef struct {
  guint depth_hint;
  guint side;
  gsize generated_leaves;
  gsize scored_moves;
  gsize kept_moves;
} HomeworldsGoodMoveTrace;

typedef void (*HomeworldsGoodMoveTraceFunc)(const HomeworldsGoodMoveTrace *trace, gpointer user_data);

extern const GameBackend homeworlds_game_backend;

void homeworlds_backend_set_good_move_trace(HomeworldsGoodMoveTraceFunc trace_func, gpointer user_data);

#endif
