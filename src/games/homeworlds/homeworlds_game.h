#ifndef HOMEWORLDS_GAME_H
#define HOMEWORLDS_GAME_H

#include "../../game_backend.h"
#include "homeworlds_types.h"

#include <glib.h>

G_BEGIN_DECLS

void homeworlds_position_init(HomeworldsPosition *position);
void homeworlds_position_clear(HomeworldsPosition *position);
void homeworlds_position_copy(HomeworldsPosition *dest, const HomeworldsPosition *src);
GameBackendOutcome homeworlds_position_outcome(const HomeworldsPosition *position);
guint homeworlds_position_turn(const HomeworldsPosition *position);

gboolean homeworlds_position_apply_move(HomeworldsPosition *position, const HomeworldsMove *move);
gboolean homeworlds_position_apply_catastrophe(HomeworldsPosition *position, guint system_index, HomeworldsColor color);
gboolean homeworlds_position_apply_turn_step(HomeworldsPosition *position, const HomeworldsTurnStep *step);
void homeworlds_position_finish_turn(HomeworldsPosition *position);

gboolean homeworlds_system_is_connected(const HomeworldsSystem *left, const HomeworldsSystem *right);
guint homeworlds_system_ship_count_for_side(const HomeworldsSystem *system, guint side);
guint homeworlds_system_color_count(const HomeworldsSystem *system, HomeworldsColor color);
gboolean homeworlds_system_has_access_to_color(const HomeworldsSystem *system, guint side, HomeworldsColor color);
gboolean homeworlds_system_find_smallest_bank_ship(const HomeworldsPosition *position,
                                                   HomeworldsColor color,
                                                   HomeworldsPyramid *out_pyramid);
gboolean homeworlds_position_find_empty_system(const HomeworldsPosition *position, guint *out_system_index);

gint homeworlds_position_evaluate_static(const HomeworldsPosition *position);
gint homeworlds_position_terminal_score(GameBackendOutcome outcome, guint ply_depth);
guint64 homeworlds_position_hash(const HomeworldsPosition *position);
gboolean homeworlds_move_format(const HomeworldsMove *move, char *buffer, gsize size);

G_END_DECLS

#endif
