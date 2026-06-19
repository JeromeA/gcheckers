#ifndef HOMEWORLDS_GAME_H
#define HOMEWORLDS_GAME_H

#include "../../game_backend.h"
#include "homeworlds_types.h"

#include <glib.h>

G_BEGIN_DECLS

typedef struct {
  gint ship_values[HOMEWORLDS_SIZE_LARGE + 1];
  gint empty_homeworld_value;
  gint single_star_homeworld_penalty;
  gint buildable_color_value;
} HomeworldsEvalWeights;

void homeworlds_position_init(HomeworldsPosition *position);
void homeworlds_position_clear(HomeworldsPosition *position);
void homeworlds_position_copy(HomeworldsPosition *dest, const HomeworldsPosition *src);
GameBackendOutcome homeworlds_position_outcome(const HomeworldsPosition *position);
guint homeworlds_position_turn(const HomeworldsPosition *position);

gboolean homeworlds_moves_equal(const HomeworldsMove *left, const HomeworldsMove *right);
guint homeworlds_move_hash(gconstpointer value);
gboolean homeworlds_positions_equal(const HomeworldsPosition *left, const HomeworldsPosition *right);
GameBackendMoveList homeworlds_position_list_all_moves(const HomeworldsPosition *position);
gboolean homeworlds_position_stream_all_moves(const HomeworldsPosition *position,
                                              GameBackendMoveStreamFunc stream_func,
                                              gpointer user_data);
void homeworlds_move_list_free(GameBackendMoveList *moves);
const HomeworldsMove *homeworlds_move_list_get(const GameBackendMoveList *moves, gsize index);
gboolean homeworlds_position_apply_move(HomeworldsPosition *position, const HomeworldsMove *move);
gboolean homeworlds_position_apply_catastrophe(HomeworldsPosition *position, guint system_index, HomeworldsColor color);
gboolean homeworlds_position_apply_turn_step(HomeworldsPosition *position, const HomeworldsTurnStep *step);
gboolean homeworlds_position_apply_forced_action_step(HomeworldsPosition *position, const HomeworldsTurnStep *step);
void homeworlds_position_finish_turn(HomeworldsPosition *position);
gboolean homeworlds_position_system_ref_for_index(const HomeworldsPosition *position,
                                                  guint system_index,
                                                  HomeworldsSystemRef *out_ref);
gboolean homeworlds_position_resolve_system_ref(const HomeworldsPosition *position,
                                                const HomeworldsSystemRef *ref,
                                                guint *out_system_index);

void homeworlds_system_rebuild_color_counts(HomeworldsSystem *system);
void homeworlds_position_rebuild_color_counts(HomeworldsPosition *position);
gboolean homeworlds_system_is_connected(const HomeworldsSystem *left, const HomeworldsSystem *right);
static inline guint homeworlds_system_color_count(const HomeworldsSystem *system, HomeworldsColor color) {
  g_return_val_if_fail(system != NULL, 0);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, 0);

  return (guint)system->star_color_counts[color] +
         (guint)system->ship_color_counts[0][color] +
         (guint)system->ship_color_counts[1][color];
}

static inline guint homeworlds_system_accessible_color_count(const HomeworldsSystem *system,
                                                             guint side,
                                                             HomeworldsColor color) {
  g_return_val_if_fail(system != NULL, 0);
  g_return_val_if_fail(side < 2, 0);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, 0);

  return (guint)system->star_color_counts[color] + (guint)system->ship_color_counts[side][color];
}

guint homeworlds_system_ship_count_for_side(const HomeworldsSystem *system, guint side);
gboolean homeworlds_system_has_access_to_color(const HomeworldsSystem *system, guint side, HomeworldsColor color);
gboolean homeworlds_system_find_smallest_bank_ship(const HomeworldsPosition *position,
                                                   HomeworldsColor color,
                                                   HomeworldsPyramid *out_pyramid);
gboolean homeworlds_position_find_empty_system(const HomeworldsPosition *position, guint *out_system_index);

const HomeworldsEvalWeights *homeworlds_eval_weights_default(void);
void homeworlds_eval_weights_reset_active(void);
void homeworlds_eval_weights_set_active(const HomeworldsEvalWeights *weights);
gint homeworlds_position_evaluate_static_with_weights(const HomeworldsPosition *position,
                                                       const HomeworldsEvalWeights *weights);
gint homeworlds_position_evaluate_static(const HomeworldsPosition *position);
gint homeworlds_position_terminal_score(GameBackendOutcome outcome, guint ply_depth);
guint64 homeworlds_position_hash(const HomeworldsPosition *position);
gboolean homeworlds_move_format(const HomeworldsMove *move, char *buffer, gsize size);
gboolean homeworlds_move_parse(const char *notation, HomeworldsMove *out_move);

G_END_DECLS

#endif
