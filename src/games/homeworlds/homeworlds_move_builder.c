#include "homeworlds_move_builder.h"

#include "homeworlds_game.h"

#include <string.h>

typedef struct {
  HomeworldsMoveCandidate *items;
  gsize count;
  gsize capacity;
} HomeworldsCandidateBuffer;

static gboolean homeworlds_candidate_buffer_append(HomeworldsCandidateBuffer *buffer,
                                                   const HomeworldsMoveCandidate *candidate) {
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(candidate != NULL, FALSE);

  if (buffer->count == buffer->capacity) {
    gsize next_capacity = buffer->capacity == 0 ? 8 : buffer->capacity * 2;
    HomeworldsMoveCandidate *next_items = g_realloc_n(buffer->items, next_capacity, sizeof(*next_items));
    g_return_val_if_fail(next_items != NULL, FALSE);
    buffer->items = next_items;
    buffer->capacity = next_capacity;
  }

  buffer->items[buffer->count++] = *candidate;
  return TRUE;
}

static GameBackendMoveList homeworlds_candidate_buffer_finish(HomeworldsCandidateBuffer *buffer) {
  g_return_val_if_fail(buffer != NULL, (GameBackendMoveList){0});

  return (GameBackendMoveList){
    .moves = buffer->items,
    .count = buffer->count,
  };
}

static HomeworldsMoveBuilderState *homeworlds_builder_state(GameBackendMoveBuilder *builder) {
  g_return_val_if_fail(builder != NULL, NULL);

  return builder->builder_state;
}

static const HomeworldsMoveBuilderState *homeworlds_builder_state_const(const GameBackendMoveBuilder *builder) {
  g_return_val_if_fail(builder != NULL, NULL);

  return builder->builder_state;
}

static gboolean homeworlds_builder_find_selected_ship_slot(const HomeworldsMoveBuilderState *state,
                                                           guint *out_ship_slot) {
  const HomeworldsSystem *system = NULL;
  const guint side = state->working_position.turn;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(out_ship_slot != NULL, FALSE);
  g_return_val_if_fail(state->selected_system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(state->selected_ship_pyramid), FALSE);

  system = &state->working_position.systems[state->selected_system_index];
  for (guint ship_slot = 0; ship_slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++ship_slot) {
    if (system->ships[side][ship_slot] != state->selected_ship_pyramid) {
      continue;
    }

    *out_ship_slot = ship_slot;
    return TRUE;
  }

  return FALSE;
}

static gboolean homeworlds_builder_selected_ship_ref(const HomeworldsMoveBuilderState *state,
                                                     HomeworldsShipRef *out_ref) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(out_ref != NULL, FALSE);
  g_return_val_if_fail(state->selected_system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(state->selected_ship_pyramid), FALSE);

  memset(out_ref, 0, sizeof(*out_ref));
  if (!homeworlds_position_system_ref_for_index(&state->working_position,
                                                state->selected_system_index,
                                                &out_ref->system)) {
    return FALSE;
  }

  out_ref->ship = state->selected_ship_pyramid;
  return TRUE;
}

static gboolean homeworlds_builder_system_contains_star(const HomeworldsSystem *system, HomeworldsPyramid star) {
  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(star), FALSE);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    if (system->stars[i] == star) {
      return TRUE;
    }
  }

  return FALSE;
}

static HomeworldsSystemRef homeworlds_builder_discovery_ref(const HomeworldsPosition *position,
                                                            HomeworldsPyramid star) {
  HomeworldsSystemRef ref = {
    .kind = HOMEWORLDS_SYSTEM_REF_STAR,
    .star = star,
  };

  g_return_val_if_fail(position != NULL, ref);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(star), ref);

  for (guint system_index = 2; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    if (!homeworlds_builder_system_contains_star(&position->systems[system_index], star)) {
      continue;
    }

    ref.duplicate_index++;
  }

  return ref;
}

static gboolean homeworlds_builder_append_step(HomeworldsMoveBuilderState *state, const HomeworldsTurnStep *step) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (state->move.step_count >= HOMEWORLDS_MAX_MOVE_STEPS) {
    g_debug("Homeworlds move exceeded maximum step count");
    return FALSE;
  }

  state->move.steps[state->move.step_count++] = *step;
  return TRUE;
}

static gboolean homeworlds_builder_consume_pending_action(HomeworldsMoveBuilderState *state) {
  g_return_val_if_fail(state != NULL, FALSE);

  if (state->pending_actions_remaining > 0) {
    state->pending_actions_remaining--;
  }
  return TRUE;
}

static gboolean homeworlds_builder_finish_or_continue(HomeworldsMoveBuilderState *state) {
  g_return_val_if_fail(state != NULL, FALSE);

  if (state->pending_actions_remaining > 0) {
    state->stage = HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP;
  } else {
    state->stage = HOMEWORLDS_BUILDER_STAGE_COMPLETE;
  }
  state->selected_system_index = HOMEWORLDS_INVALID_INDEX;
  state->selected_ship_pyramid = 0;
  state->pending_action_kind = HOMEWORLDS_STEP_NONE;
  return TRUE;
}

static gboolean homeworlds_builder_commit_action(HomeworldsMoveBuilderState *state, const HomeworldsTurnStep *step) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (!homeworlds_builder_append_step(state, step)) {
    return FALSE;
  }
  if (!homeworlds_position_apply_turn_step(&state->working_position, step)) {
    state->move.step_count--;
    return FALSE;
  }
  return homeworlds_builder_finish_or_continue(state);
}

static gboolean homeworlds_builder_apply_step_in_place(HomeworldsMoveBuilderState *state,
                                                       const HomeworldsTurnStep *step) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  return homeworlds_position_apply_turn_step(&state->working_position, step);
}

static gboolean homeworlds_builder_apply_prefix_step(HomeworldsMoveBuilderState *state,
                                                     const HomeworldsTurnStep *step) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (!homeworlds_builder_append_step(state, step)) {
    return FALSE;
  }
  if (!homeworlds_builder_apply_step_in_place(state, step)) {
    state->move.step_count--;
    return FALSE;
  }
  return TRUE;
}

static guint homeworlds_builder_bank_count(const HomeworldsPosition *position, HomeworldsPyramid pyramid) {
  guint count = 0;

  g_return_val_if_fail(position != NULL, 0);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), 0);

  for (guint i = 0; i < HOMEWORLDS_BANK_SLOT_COUNT; ++i) {
    if (position->bank[i] == pyramid) {
      count++;
    }
  }

  return count;
}

static guint homeworlds_builder_setup_reserved_count(const HomeworldsMoveBuilderState *state,
                                                     HomeworldsPyramid pyramid) {
  guint count = 0;

  g_return_val_if_fail(state != NULL, 0);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), 0);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    if (state->move.setup_stars[i] == pyramid) {
      count++;
    }
  }

  return count;
}

static gboolean homeworlds_builder_setup_pyramid_is_available(const HomeworldsMoveBuilderState *state,
                                                              HomeworldsPyramid pyramid) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), FALSE);

  return homeworlds_builder_bank_count(&state->working_position, pyramid) >
         homeworlds_builder_setup_reserved_count(state, pyramid);
}

static GameBackendMoveList homeworlds_builder_list_setup_stars(const HomeworldsMoveBuilderState *state) {
  HomeworldsCandidateBuffer buffer = {0};
  gboolean seen_pyramids[13] = {FALSE};

  g_return_val_if_fail(state != NULL, (GameBackendMoveList){0});

  for (guint i = 0; i < HOMEWORLDS_BANK_SLOT_COUNT; ++i) {
    HomeworldsPyramid pyramid = state->working_position.bank[i];
    if (!homeworlds_pyramid_is_valid(pyramid)) {
      continue;
    }
    if (seen_pyramids[pyramid]) {
      continue;
    }
    seen_pyramids[pyramid] = TRUE;
    if (!homeworlds_builder_setup_pyramid_is_available(state, pyramid)) {
      continue;
    }

    HomeworldsMoveCandidate candidate = {
      .data.kind = HOMEWORLDS_CANDIDATE_SETUP_STAR,
      .data.pyramid = pyramid,
    };
    if (!homeworlds_candidate_buffer_append(&buffer, &candidate)) {
      g_free(buffer.items);
      return (GameBackendMoveList){0};
    }
  }

  return homeworlds_candidate_buffer_finish(&buffer);
}

static GameBackendMoveList homeworlds_builder_list_setup_ships(const HomeworldsMoveBuilderState *state) {
  HomeworldsCandidateBuffer buffer = {0};
  gboolean seen_pyramids[13] = {FALSE};

  g_return_val_if_fail(state != NULL, (GameBackendMoveList){0});

  for (guint i = 0; i < HOMEWORLDS_BANK_SLOT_COUNT; ++i) {
    HomeworldsPyramid pyramid = state->working_position.bank[i];
    if (!homeworlds_pyramid_is_valid(pyramid)) {
      continue;
    }
    if (seen_pyramids[pyramid]) {
      continue;
    }
    seen_pyramids[pyramid] = TRUE;
    if (!homeworlds_builder_setup_pyramid_is_available(state, pyramid)) {
      continue;
    }

    HomeworldsMoveCandidate candidate = {
      .data.kind = HOMEWORLDS_CANDIDATE_SETUP_SHIP,
      .data.pyramid = pyramid,
    };
    if (!homeworlds_candidate_buffer_append(&buffer, &candidate)) {
      g_free(buffer.items);
      return (GameBackendMoveList){0};
    }
  }

  return homeworlds_candidate_buffer_finish(&buffer);
}

static GameBackendMoveList homeworlds_builder_list_selectable_ships(const HomeworldsMoveBuilderState *state) {
  HomeworldsCandidateBuffer buffer = {0};
  const guint side = state->working_position.turn;

  g_return_val_if_fail(state != NULL, (GameBackendMoveList){0});

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &state->working_position.systems[system_index];
    gboolean seen_pyramids[13] = {FALSE};

    for (guint ship_slot = 0; ship_slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++ship_slot) {
      HomeworldsPyramid pyramid = system->ships[side][ship_slot];

      if (!homeworlds_pyramid_is_valid(pyramid)) {
        continue;
      }
      if (seen_pyramids[pyramid]) {
        continue;
      }
      seen_pyramids[pyramid] = TRUE;

      HomeworldsMoveCandidate candidate = {
        .data.kind = HOMEWORLDS_CANDIDATE_SELECT_SHIP,
        .data.system_index = system_index,
        .data.ship_owner = side,
        .data.pyramid = pyramid,
      };
      if (!homeworlds_candidate_buffer_append(&buffer, &candidate)) {
        g_free(buffer.items);
        return (GameBackendMoveList){0};
      }
    }
  }

  HomeworldsMoveCandidate pass_candidate = {
    .data.kind = HOMEWORLDS_CANDIDATE_ACTION,
    .data.target_color = HOMEWORLDS_STEP_PASS,
  };
  if (!homeworlds_candidate_buffer_append(&buffer, &pass_candidate)) {
    g_free(buffer.items);
    return (GameBackendMoveList){0};
  }

  return homeworlds_candidate_buffer_finish(&buffer);
}

static GameBackendMoveList homeworlds_builder_list_actions(const HomeworldsMoveBuilderState *state) {
  HomeworldsCandidateBuffer buffer = {0};
  const HomeworldsSystem *system = NULL;
  HomeworldsPyramid ship = 0;
  const guint side = state->working_position.turn;
  guint selected_ship_slot = 0;

  g_return_val_if_fail(state != NULL, (GameBackendMoveList){0});
  g_return_val_if_fail(state->selected_system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, (GameBackendMoveList){0});
  g_return_val_if_fail(homeworlds_builder_find_selected_ship_slot(state, &selected_ship_slot),
                       (GameBackendMoveList){0});

  system = &state->working_position.systems[state->selected_system_index];
  ship = system->ships[side][selected_ship_slot];
  g_return_val_if_fail(homeworlds_pyramid_is_valid(ship), (GameBackendMoveList){0});

  if (state->pending_actions_remaining > 0) {
    HomeworldsColor forced_color = (HomeworldsColor) state->forced_action_color;

    if (forced_color == HOMEWORLDS_COLOR_GREEN &&
        homeworlds_system_has_access_to_color(system, side, HOMEWORLDS_COLOR_GREEN)) {
      HomeworldsMoveCandidate candidate = {
        .data.kind = HOMEWORLDS_CANDIDATE_ACTION,
        .data.target_color = HOMEWORLDS_STEP_BUILD,
      };
      homeworlds_candidate_buffer_append(&buffer, &candidate);
    } else if (forced_color == HOMEWORLDS_COLOR_BLUE &&
               homeworlds_system_has_access_to_color(system, side, HOMEWORLDS_COLOR_BLUE)) {
      HomeworldsMoveCandidate candidate = {
        .data.kind = HOMEWORLDS_CANDIDATE_ACTION,
        .data.target_color = HOMEWORLDS_STEP_TRADE,
      };
      homeworlds_candidate_buffer_append(&buffer, &candidate);
    } else if (forced_color == HOMEWORLDS_COLOR_RED &&
               homeworlds_system_has_access_to_color(system, side, HOMEWORLDS_COLOR_RED)) {
      HomeworldsMoveCandidate candidate = {
        .data.kind = HOMEWORLDS_CANDIDATE_ACTION,
        .data.target_color = HOMEWORLDS_STEP_ATTACK,
      };
      homeworlds_candidate_buffer_append(&buffer, &candidate);
    } else if (forced_color == HOMEWORLDS_COLOR_YELLOW &&
               homeworlds_system_has_access_to_color(system, side, HOMEWORLDS_COLOR_YELLOW)) {
      HomeworldsMoveCandidate candidate = {
        .data.kind = HOMEWORLDS_CANDIDATE_ACTION,
        .data.target_color = HOMEWORLDS_STEP_MOVE,
      };
      homeworlds_candidate_buffer_append(&buffer, &candidate);
    }

    return homeworlds_candidate_buffer_finish(&buffer);
  }

  if (homeworlds_system_has_access_to_color(system, side, HOMEWORLDS_COLOR_RED)) {
    HomeworldsMoveCandidate attack = {
      .data.kind = HOMEWORLDS_CANDIDATE_ACTION,
      .data.target_color = HOMEWORLDS_STEP_ATTACK,
    };
    homeworlds_candidate_buffer_append(&buffer, &attack);
  }
  if (homeworlds_system_has_access_to_color(system, side, HOMEWORLDS_COLOR_YELLOW)) {
    HomeworldsMoveCandidate move = {
      .data.kind = HOMEWORLDS_CANDIDATE_ACTION,
      .data.target_color = HOMEWORLDS_STEP_MOVE,
    };
    homeworlds_candidate_buffer_append(&buffer, &move);
  }
  if (homeworlds_system_has_access_to_color(system, side, HOMEWORLDS_COLOR_GREEN)) {
    HomeworldsMoveCandidate build = {
      .data.kind = HOMEWORLDS_CANDIDATE_ACTION,
      .data.target_color = HOMEWORLDS_STEP_BUILD,
    };
    homeworlds_candidate_buffer_append(&buffer, &build);
  }
  if (homeworlds_system_has_access_to_color(system, side, HOMEWORLDS_COLOR_BLUE)) {
    HomeworldsMoveCandidate trade = {
      .data.kind = HOMEWORLDS_CANDIDATE_ACTION,
      .data.target_color = HOMEWORLDS_STEP_TRADE,
    };
    homeworlds_candidate_buffer_append(&buffer, &trade);
  }

  HomeworldsMoveCandidate sacrifice = {
    .data.kind = HOMEWORLDS_CANDIDATE_ACTION,
    .data.target_color = HOMEWORLDS_STEP_SACRIFICE,
  };
  homeworlds_candidate_buffer_append(&buffer, &sacrifice);
  return homeworlds_candidate_buffer_finish(&buffer);
}

static GameBackendMoveList homeworlds_builder_list_trade_colors(const HomeworldsMoveBuilderState *state) {
  HomeworldsCandidateBuffer buffer = {0};
  const HomeworldsSystem *system = NULL;
  HomeworldsPyramid ship = 0;
  guint selected_ship_slot = 0;

  g_return_val_if_fail(state != NULL, (GameBackendMoveList){0});
  g_return_val_if_fail(state->selected_system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, (GameBackendMoveList){0});
  g_return_val_if_fail(homeworlds_builder_find_selected_ship_slot(state, &selected_ship_slot),
                       (GameBackendMoveList){0});

  system = &state->working_position.systems[state->selected_system_index];
  ship = system->ships[state->working_position.turn][selected_ship_slot];
  g_return_val_if_fail(homeworlds_pyramid_is_valid(ship), (GameBackendMoveList){0});

  for (guint color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
    HomeworldsPyramid target = homeworlds_pyramid_make((HomeworldsColor) color, homeworlds_pyramid_size(ship));
    if (color == homeworlds_pyramid_color(ship)) {
      continue;
    }

    for (guint bank_slot = 0; bank_slot < HOMEWORLDS_BANK_SLOT_COUNT; ++bank_slot) {
      if (state->working_position.bank[bank_slot] != target) {
        continue;
      }

      HomeworldsMoveCandidate candidate = {
        .data.kind = HOMEWORLDS_CANDIDATE_TRADE_COLOR,
        .data.target_color = color,
      };
      homeworlds_candidate_buffer_append(&buffer, &candidate);
      break;
    }
  }

  return homeworlds_candidate_buffer_finish(&buffer);
}

static GameBackendMoveList homeworlds_builder_list_attack_targets(const HomeworldsMoveBuilderState *state) {
  HomeworldsCandidateBuffer buffer = {0};
  const HomeworldsSystem *system = NULL;
  HomeworldsPyramid attacker = 0;
  guint selected_ship_slot = 0;
  gboolean seen_pyramids[13] = {FALSE};

  g_return_val_if_fail(state != NULL, (GameBackendMoveList){0});
  g_return_val_if_fail(homeworlds_builder_find_selected_ship_slot(state, &selected_ship_slot),
                       (GameBackendMoveList){0});

  system = &state->working_position.systems[state->selected_system_index];
  attacker = system->ships[state->working_position.turn][selected_ship_slot];
  g_return_val_if_fail(homeworlds_pyramid_is_valid(attacker), (GameBackendMoveList){0});

  for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    HomeworldsPyramid target = system->ships[state->working_position.turn == 0 ? 1 : 0][slot];
    if (!homeworlds_pyramid_is_valid(target)) {
      continue;
    }
    if (seen_pyramids[target]) {
      continue;
    }
    if (homeworlds_pyramid_size(attacker) < homeworlds_pyramid_size(target)) {
      continue;
    }
    seen_pyramids[target] = TRUE;

    HomeworldsMoveCandidate candidate = {
      .data.kind = HOMEWORLDS_CANDIDATE_ATTACK_TARGET,
      .data.target_ship_owner = state->working_position.turn == 0 ? 1 : 0,
      .data.target_ship_slot = slot,
      .data.pyramid = target,
    };
    homeworlds_candidate_buffer_append(&buffer, &candidate);
  }

  return homeworlds_candidate_buffer_finish(&buffer);
}

static GameBackendMoveList homeworlds_builder_list_move_targets(const HomeworldsMoveBuilderState *state) {
  HomeworldsCandidateBuffer buffer = {0};
  const HomeworldsSystem *from_system = NULL;
  gboolean seen_discovery_stars[13] = {FALSE};

  g_return_val_if_fail(state != NULL, (GameBackendMoveList){0});
  from_system = &state->working_position.systems[state->selected_system_index];

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    if (system_index == state->selected_system_index ||
        homeworlds_system_is_empty(&state->working_position.systems[system_index])) {
      continue;
    }
    if (!homeworlds_system_is_connected(from_system, &state->working_position.systems[system_index])) {
      continue;
    }

    HomeworldsMoveCandidate candidate = {
      .data.kind = HOMEWORLDS_CANDIDATE_MOVE_TARGET,
      .data.target_system_index = system_index,
    };
    homeworlds_candidate_buffer_append(&buffer, &candidate);
  }

  for (guint bank_slot = 0; bank_slot < HOMEWORLDS_BANK_SLOT_COUNT; ++bank_slot) {
    HomeworldsPyramid star = state->working_position.bank[bank_slot];
    if (!homeworlds_pyramid_is_valid(star)) {
      continue;
    }
    if (seen_discovery_stars[star]) {
      continue;
    }

    HomeworldsSystem temporary = {0};
    temporary.stars[0] = star;
    if (!homeworlds_system_is_connected(from_system, &temporary)) {
      continue;
    }

    HomeworldsMoveCandidate candidate = {
      .data.kind = HOMEWORLDS_CANDIDATE_MOVE_TARGET,
      .data.target_system_index = HOMEWORLDS_INVALID_INDEX,
      .data.pyramid = star,
    };
    homeworlds_candidate_buffer_append(&buffer, &candidate);
    seen_discovery_stars[star] = TRUE;
  }

  return homeworlds_candidate_buffer_finish(&buffer);
}

gboolean homeworlds_move_builder_init(const HomeworldsPosition *position, GameBackendMoveBuilder *out_builder) {
  HomeworldsMoveBuilderState *state = NULL;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(out_builder != NULL, FALSE);

  state = g_new0(HomeworldsMoveBuilderState, 1);
  g_return_val_if_fail(state != NULL, FALSE);

  state->working_position = *position;
  state->move.kind = position->phase == HOMEWORLDS_PHASE_SETUP ? HOMEWORLDS_MOVE_KIND_SETUP : HOMEWORLDS_MOVE_KIND_TURN;
  state->stage = position->phase == HOMEWORLDS_PHASE_SETUP ? HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR
                                                           : HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP;
  state->selected_system_index = HOMEWORLDS_INVALID_INDEX;
  state->selected_ship_pyramid = 0;
  state->forced_action_color = HOMEWORLDS_INVALID_COLOR;
  out_builder->builder_state = state;
  out_builder->builder_state_size = sizeof(*state);
  return TRUE;
}

void homeworlds_move_builder_clear(GameBackendMoveBuilder *builder) {
  g_return_if_fail(builder != NULL);

  g_clear_pointer(&builder->builder_state, g_free);
  builder->builder_state_size = 0;
}

gboolean homeworlds_move_builder_apply_catastrophe(GameBackendMoveBuilder *builder,
                                                   guint system_index,
                                                   HomeworldsColor color) {
  HomeworldsMoveBuilderState *state = homeworlds_builder_state(builder);
  HomeworldsTurnStep step = {
    .kind = HOMEWORLDS_STEP_CATASTROPHE,
    .target_color = color,
  };

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, FALSE);

  if (state->working_position.phase != HOMEWORLDS_PHASE_PLAY ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_COMPLETE) {
    return FALSE;
  }
  if (!homeworlds_position_system_ref_for_index(&state->working_position, system_index, &step.target_system) ||
      !homeworlds_builder_apply_prefix_step(state, &step)) {
    return FALSE;
  }

  state->stage = HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP;
  state->selected_system_index = HOMEWORLDS_INVALID_INDEX;
  state->selected_ship_pyramid = 0;
  state->pending_action_kind = HOMEWORLDS_STEP_NONE;
  return TRUE;
}

GameBackendMoveList homeworlds_move_builder_list_candidates(const GameBackendMoveBuilder *builder) {
  const HomeworldsMoveBuilderState *state = homeworlds_builder_state_const(builder);

  g_return_val_if_fail(state != NULL, (GameBackendMoveList){0});

  switch ((HomeworldsBuilderStage) state->stage) {
    case HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR:
      return homeworlds_builder_list_setup_stars(state);
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR:
      return homeworlds_builder_list_setup_stars(state);
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP:
      return homeworlds_builder_list_setup_ships(state);
    case HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP:
      return homeworlds_builder_list_selectable_ships(state);
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ACTION:
      return homeworlds_builder_list_actions(state);
    case HOMEWORLDS_BUILDER_STAGE_SELECT_TRADE_COLOR:
      return homeworlds_builder_list_trade_colors(state);
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ATTACK_TARGET:
      return homeworlds_builder_list_attack_targets(state);
    case HOMEWORLDS_BUILDER_STAGE_SELECT_MOVE_TARGET:
      return homeworlds_builder_list_move_targets(state);
    case HOMEWORLDS_BUILDER_STAGE_COMPLETE:
    default:
      return (GameBackendMoveList){0};
  }
}

gboolean homeworlds_move_builder_step(GameBackendMoveBuilder *builder, const HomeworldsMoveCandidate *candidate) {
  HomeworldsMoveBuilderState *state = homeworlds_builder_state(builder);

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(candidate != NULL, FALSE);

  switch ((HomeworldsBuilderStage) state->stage) {
    case HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR:
      if (candidate->data.kind != HOMEWORLDS_CANDIDATE_SETUP_STAR ||
          !homeworlds_pyramid_is_valid(candidate->data.pyramid) ||
          !homeworlds_builder_setup_pyramid_is_available(state, candidate->data.pyramid)) {
        return FALSE;
      }
      state->move.setup_stars[0] = candidate->data.pyramid;
      state->stage = HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR;
      return TRUE;
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR:
      if (candidate->data.kind != HOMEWORLDS_CANDIDATE_SETUP_STAR ||
          !homeworlds_pyramid_is_valid(candidate->data.pyramid) ||
          !homeworlds_builder_setup_pyramid_is_available(state, candidate->data.pyramid)) {
        return FALSE;
      }
      state->move.setup_stars[1] = candidate->data.pyramid;
      state->stage = HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP;
      return TRUE;
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP:
      if (candidate->data.kind != HOMEWORLDS_CANDIDATE_SETUP_SHIP ||
          !homeworlds_pyramid_is_valid(candidate->data.pyramid) ||
          !homeworlds_builder_setup_pyramid_is_available(state, candidate->data.pyramid)) {
        return FALSE;
      }
      state->move.setup_ship = candidate->data.pyramid;
      state->stage = HOMEWORLDS_BUILDER_STAGE_COMPLETE;
      return TRUE;
    case HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP:
      if (candidate->data.kind == HOMEWORLDS_CANDIDATE_ACTION &&
          candidate->data.target_color == HOMEWORLDS_STEP_PASS) {
        HomeworldsTurnStep step = {.kind = HOMEWORLDS_STEP_PASS};
        return homeworlds_builder_commit_action(state, &step);
      }
      if (candidate->data.kind != HOMEWORLDS_CANDIDATE_SELECT_SHIP ||
          candidate->data.system_index >= HOMEWORLDS_SYSTEM_SLOT_COUNT ||
          candidate->data.ship_owner != state->working_position.turn ||
          !homeworlds_pyramid_is_valid(candidate->data.pyramid)) {
        return FALSE;
      }
      {
        gboolean found = FALSE;
        for (guint ship_slot = 0; ship_slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++ship_slot) {
          if (state->working_position.systems[candidate->data.system_index]
                  .ships[state->working_position.turn][ship_slot] != candidate->data.pyramid) {
            continue;
          }

          found = TRUE;
          break;
        }
        if (!found) {
          return FALSE;
        }
      }
      state->selected_system_index = candidate->data.system_index;
      state->selected_ship_pyramid = candidate->data.pyramid;
      state->stage = HOMEWORLDS_BUILDER_STAGE_SELECT_ACTION;
      return TRUE;
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ACTION: {
      guint selected_ship_slot = 0;
      if (!homeworlds_builder_find_selected_ship_slot(state, &selected_ship_slot)) {
        return FALSE;
      }

      HomeworldsTurnStep step = {
        .kind = candidate->data.target_color,
      };
      if (!homeworlds_builder_selected_ship_ref(state, &step.actor)) {
        return FALSE;
      }

      if (candidate->data.kind != HOMEWORLDS_CANDIDATE_ACTION) {
        return FALSE;
      }
      if (candidate->data.target_color == HOMEWORLDS_STEP_BUILD) {
        homeworlds_builder_consume_pending_action(state);
        return homeworlds_builder_commit_action(state, &step);
      }
      if (candidate->data.target_color == HOMEWORLDS_STEP_SACRIFICE) {
        HomeworldsPyramid ship = state->working_position.systems[state->selected_system_index]
                                     .ships[state->working_position.turn][selected_ship_slot];
        if (!homeworlds_pyramid_is_valid(ship)) {
          return FALSE;
        }

        state->pending_actions_remaining = homeworlds_pyramid_size(ship);
        state->forced_action_color = homeworlds_pyramid_color(ship);
        if (!homeworlds_builder_apply_prefix_step(state, &step)) {
          return FALSE;
        }
        state->stage = HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP;
        return TRUE;
      }
      if (candidate->data.target_color == HOMEWORLDS_STEP_TRADE) {
        state->pending_action_kind = HOMEWORLDS_STEP_TRADE;
        state->stage = HOMEWORLDS_BUILDER_STAGE_SELECT_TRADE_COLOR;
        return TRUE;
      }
      if (candidate->data.target_color == HOMEWORLDS_STEP_ATTACK) {
        state->pending_action_kind = HOMEWORLDS_STEP_ATTACK;
        state->stage = HOMEWORLDS_BUILDER_STAGE_SELECT_ATTACK_TARGET;
        return TRUE;
      }
      if (candidate->data.target_color == HOMEWORLDS_STEP_MOVE) {
        state->pending_action_kind = HOMEWORLDS_STEP_MOVE;
        state->stage = HOMEWORLDS_BUILDER_STAGE_SELECT_MOVE_TARGET;
        return TRUE;
      }
      return FALSE;
    }
    case HOMEWORLDS_BUILDER_STAGE_SELECT_TRADE_COLOR: {
      guint selected_ship_slot = 0;
      if (!homeworlds_builder_find_selected_ship_slot(state, &selected_ship_slot)) {
        return FALSE;
      }

      HomeworldsTurnStep step = {
        .kind = HOMEWORLDS_STEP_TRADE,
        .target_color = candidate->data.target_color,
      };
      if (!homeworlds_builder_selected_ship_ref(state, &step.actor)) {
        return FALSE;
      }

      if (candidate->data.kind != HOMEWORLDS_CANDIDATE_TRADE_COLOR) {
        return FALSE;
      }
      homeworlds_builder_consume_pending_action(state);
      return homeworlds_builder_commit_action(state, &step);
    }
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ATTACK_TARGET: {
      guint selected_ship_slot = 0;
      if (!homeworlds_builder_find_selected_ship_slot(state, &selected_ship_slot)) {
        return FALSE;
      }

      HomeworldsTurnStep step = {
        .kind = HOMEWORLDS_STEP_ATTACK,
      };
      if (!homeworlds_builder_selected_ship_ref(state, &step.actor) ||
          candidate->data.target_ship_owner >= 2 ||
          candidate->data.target_ship_slot >= HOMEWORLDS_SHIP_SLOT_COUNT) {
        return FALSE;
      }
      step.target_ship.system = step.actor.system;
      step.target_ship.ship = state->working_position.systems[state->selected_system_index]
                                  .ships[candidate->data.target_ship_owner][candidate->data.target_ship_slot];
      if (!homeworlds_pyramid_is_valid(step.target_ship.ship)) {
        return FALSE;
      }

      if (candidate->data.kind != HOMEWORLDS_CANDIDATE_ATTACK_TARGET) {
        return FALSE;
      }
      homeworlds_builder_consume_pending_action(state);
      return homeworlds_builder_commit_action(state, &step);
    }
    case HOMEWORLDS_BUILDER_STAGE_SELECT_MOVE_TARGET: {
      guint selected_ship_slot = 0;
      if (!homeworlds_builder_find_selected_ship_slot(state, &selected_ship_slot)) {
        return FALSE;
      }

      HomeworldsTurnStep step = {
        .kind = HOMEWORLDS_STEP_MOVE,
      };
      if (!homeworlds_builder_selected_ship_ref(state, &step.actor)) {
        return FALSE;
      }
      if (candidate->data.target_system_index == HOMEWORLDS_INVALID_INDEX) {
        if (!homeworlds_pyramid_is_valid(candidate->data.pyramid)) {
          return FALSE;
        }
        step.target_system = homeworlds_builder_discovery_ref(&state->working_position, candidate->data.pyramid);
      } else if (!homeworlds_position_system_ref_for_index(&state->working_position,
                                                           candidate->data.target_system_index,
                                                           &step.target_system)) {
        return FALSE;
      }

      if (candidate->data.kind != HOMEWORLDS_CANDIDATE_MOVE_TARGET) {
        return FALSE;
      }
      homeworlds_builder_consume_pending_action(state);
      return homeworlds_builder_commit_action(state, &step);
    }
    case HOMEWORLDS_BUILDER_STAGE_COMPLETE:
    default:
      return FALSE;
  }
}

gboolean homeworlds_move_builder_is_complete(const GameBackendMoveBuilder *builder) {
  const HomeworldsMoveBuilderState *state = homeworlds_builder_state_const(builder);

  g_return_val_if_fail(state != NULL, FALSE);

  return state->stage == HOMEWORLDS_BUILDER_STAGE_COMPLETE;
}

gboolean homeworlds_move_builder_build_move(const GameBackendMoveBuilder *builder, HomeworldsMove *out_move) {
  const HomeworldsMoveBuilderState *state = homeworlds_builder_state_const(builder);

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  if (state->stage != HOMEWORLDS_BUILDER_STAGE_COMPLETE) {
    return FALSE;
  }

  *out_move = state->move;
  return TRUE;
}
