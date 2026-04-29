#ifndef BOOP_GAME_H
#define BOOP_GAME_H

#include "boop_types.h"

#include <glib.h>

void boop_position_init(BoopPosition *position);
void boop_position_clear(BoopPosition *position);
void boop_position_copy(BoopPosition *dest, const BoopPosition *src);
GameBackendOutcome boop_position_outcome(const BoopPosition *position);
guint boop_position_turn(const BoopPosition *position);
GameBackendMoveList boop_position_list_moves(const BoopPosition *position);
GameBackendMoveList boop_position_list_good_moves(const BoopPosition *position, guint max_count, guint depth_hint);
void boop_move_list_free(GameBackendMoveList *moves);
const BoopMove *boop_move_list_get(const GameBackendMoveList *moves, gsize index);
gboolean boop_moves_equal(const BoopMove *left, const BoopMove *right);
gboolean boop_position_apply_move(BoopPosition *position, const BoopMove *move);
gint boop_position_evaluate_static(const BoopPosition *position);
gint boop_position_terminal_score(GameBackendOutcome outcome, guint ply_depth);
guint64 boop_position_hash(const BoopPosition *position);
gboolean boop_move_format(const BoopMove *move, char *buffer, gsize size);
gboolean boop_move_parse(const char *notation, BoopMove *out_move);
gboolean boop_square_to_coord(guint square, guint *out_row, guint *out_col);
gboolean boop_coord_to_square(guint row, guint col, guint *out_square);
gboolean boop_move_get_path(const BoopMove *move, guint *out_length, guint *out_indices, gsize max_indices);

gboolean boop_move_builder_init(const BoopPosition *position, GameBackendMoveBuilder *out_builder);
void boop_move_builder_clear(GameBackendMoveBuilder *builder);
GameBackendMoveList boop_move_builder_list_candidates(const GameBackendMoveBuilder *builder);
gboolean boop_move_builder_step(GameBackendMoveBuilder *builder, const BoopMove *candidate);
gboolean boop_move_builder_is_complete(const GameBackendMoveBuilder *builder);
gboolean boop_move_builder_build_move(const GameBackendMoveBuilder *builder, BoopMove *out_move);

#endif
