#ifndef HOMEWORLDS_MOVE_BUILDER_H
#define HOMEWORLDS_MOVE_BUILDER_H

#include "../../game_backend.h"
#include "homeworlds_types.h"

#include <glib.h>

typedef enum {
  HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR = 0,
  HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR,
  HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP,
  HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP,
  HOMEWORLDS_BUILDER_STAGE_SELECT_ACTION,
  HOMEWORLDS_BUILDER_STAGE_SELECT_TRADE_COLOR,
  HOMEWORLDS_BUILDER_STAGE_SELECT_ATTACK_TARGET,
  HOMEWORLDS_BUILDER_STAGE_SELECT_MOVE_TARGET,
  HOMEWORLDS_BUILDER_STAGE_COMPLETE,
} HomeworldsBuilderStage;

typedef struct {
  HomeworldsPosition working_position;
  HomeworldsMove move;
  guint8 stage;
  guint8 selected_system_index;
  HomeworldsPyramid selected_ship_pyramid;
  guint8 pending_action_kind;
  guint8 forced_action_color;
  guint8 pending_actions_remaining;
  guint8 initial_homeworld_has_ships[2];
} HomeworldsMoveBuilderState;

typedef struct {
  GHashTable *states;
} HomeworldsGenerationDedupe;

typedef struct {
  HomeworldsGenerationDedupe *sacrifice_dedupe;
} HomeworldsGenerationContext;

gboolean homeworlds_move_builder_init(const HomeworldsPosition *position, GameBackendMoveBuilder *out_builder);
void homeworlds_move_builder_clear(GameBackendMoveBuilder *builder);
GameBackendMoveList homeworlds_move_builder_list_candidates(const GameBackendMoveBuilder *builder);
gboolean homeworlds_move_builder_step(GameBackendMoveBuilder *builder, const HomeworldsMoveCandidate *candidate);
gboolean homeworlds_move_builder_apply_catastrophe(GameBackendMoveBuilder *builder,
                                                   guint system_index,
                                                   HomeworldsColor color);
gboolean homeworlds_move_builder_apply_catastrophe_step(GameBackendMoveBuilder *builder,
                                                        guint system_index,
                                                        HomeworldsColor color);
gboolean homeworlds_move_builder_is_complete(const GameBackendMoveBuilder *builder);
gboolean homeworlds_move_builder_build_move(const GameBackendMoveBuilder *builder, HomeworldsMove *out_move);
void homeworlds_generation_context_init(HomeworldsGenerationContext *context);
void homeworlds_generation_dedupe_init(HomeworldsGenerationDedupe *dedupe);
void homeworlds_generation_dedupe_clear(HomeworldsGenerationDedupe *dedupe);
gboolean homeworlds_generation_visit_state(const HomeworldsGenerationContext *context,
                                           const HomeworldsMoveBuilderState *state,
                                           gboolean *out_duplicate);
gboolean homeworlds_generation_prepare_child_context(const HomeworldsGenerationContext *parent_context,
                                                     const HomeworldsMoveBuilderState *parent_state,
                                                     const HomeworldsMoveBuilderState *child_state,
                                                     HomeworldsGenerationContext *child_context,
                                                     HomeworldsGenerationDedupe *child_dedupe,
                                                     gboolean *out_prune);

#endif
