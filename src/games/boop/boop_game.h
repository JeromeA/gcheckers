#ifndef BOOP_GAME_H
#define BOOP_GAME_H

#include "../../game_backend.h"

#include <glib.h>

typedef struct {
  guint8 turn;
  guint8 outcome;
} BoopPosition;

typedef struct {
  guint8 square;
} BoopMove;

typedef struct {
  guint8 square;
} BoopMoveBuilderState;

void boop_position_init(BoopPosition *position);
void boop_position_clear(BoopPosition *position);
void boop_position_copy(BoopPosition *dest, const BoopPosition *src);
GameBackendOutcome boop_position_outcome(const BoopPosition *position);
guint boop_position_turn(const BoopPosition *position);
gboolean boop_position_apply_move(BoopPosition *position, const BoopMove *move);
gboolean boop_move_format(const BoopMove *move, char *buffer, gsize size);

gboolean boop_move_builder_init(const BoopPosition *position, GameBackendMoveBuilder *out_builder);
void boop_move_builder_clear(GameBackendMoveBuilder *builder);
GameBackendMoveList boop_move_builder_list_candidates(const GameBackendMoveBuilder *builder);
gboolean boop_move_builder_step(GameBackendMoveBuilder *builder, const BoopMove *candidate);
gboolean boop_move_builder_is_complete(const GameBackendMoveBuilder *builder);
gboolean boop_move_builder_build_move(const GameBackendMoveBuilder *builder, BoopMove *out_move);

#endif
