#include "homeworlds_random_ai.h"

#include "homeworlds_move_builder.h"

typedef gboolean (*HomeworldsRandomAiCandidateFilter)(const GameBackendMoveBuilder *builder,
                                                      const HomeworldsMoveCandidate *candidate);

static guint homeworlds_random_ai_int_range(GRand *rand, guint begin, guint end) {
  g_return_val_if_fail(begin < end, begin);

  if (rand != NULL) {
    return (guint) g_rand_int_range(rand, (gint32) begin, (gint32) end);
  }

  return (guint) g_random_int_range((gint32) begin, (gint32) end);
}

static void homeworlds_random_ai_move_list_free(GameBackendMoveList *moves) {
  g_return_if_fail(moves != NULL);

  g_clear_pointer(&moves->moves, g_free);
  moves->count = 0;
}

static const HomeworldsMoveBuilderState *homeworlds_random_ai_builder_state(const GameBackendMoveBuilder *builder) {
  g_return_val_if_fail(builder != NULL, NULL);
  g_return_val_if_fail(builder->builder_state != NULL, NULL);

  return builder->builder_state;
}

static gboolean homeworlds_random_ai_candidate_has_continuation(const GameBackendMoveBuilder *builder,
                                                                const HomeworldsMoveCandidate *candidate) {
  const HomeworldsMoveBuilderState *state = homeworlds_random_ai_builder_state(builder);
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(candidate != NULL, FALSE);

  HomeworldsMoveBuilderState state_copy = *state;
  GameBackendMoveBuilder scratch = {
    .builder_state = &state_copy,
    .builder_state_size = sizeof(state_copy),
  };

  if (!homeworlds_move_builder_step(&scratch, candidate)) {
    return FALSE;
  }
  if (homeworlds_move_builder_is_complete(&scratch)) {
    return TRUE;
  }

  GameBackendMoveList next = homeworlds_move_builder_list_candidates(&scratch);
  gboolean has_continuation = next.count > 0;
  if (state_copy.stage == HOMEWORLDS_BUILDER_STAGE_SELECT_ACTION) {
    has_continuation = FALSE;
    for (gsize i = 0; i < next.count; ++i) {
      const HomeworldsMoveCandidate *next_candidate = &((const HomeworldsMoveCandidate *) next.moves)[i];
      if (next_candidate->data.kind == HOMEWORLDS_CANDIDATE_ACTION &&
          homeworlds_random_ai_candidate_has_continuation(&scratch, next_candidate)) {
        has_continuation = TRUE;
        break;
      }
    }
  } else if (state_copy.stage == HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP && state_copy.pending_actions_remaining > 0) {
    has_continuation = FALSE;
    for (gsize i = 0; i < next.count; ++i) {
      const HomeworldsMoveCandidate *next_candidate = &((const HomeworldsMoveCandidate *) next.moves)[i];
      if (next_candidate->data.kind == HOMEWORLDS_CANDIDATE_SELECT_SHIP &&
          homeworlds_random_ai_candidate_has_continuation(&scratch, next_candidate)) {
        has_continuation = TRUE;
        break;
      }
    }
  }
  homeworlds_random_ai_move_list_free(&next);
  return has_continuation;
}

static gboolean homeworlds_random_ai_filter_select_ship(const GameBackendMoveBuilder *builder,
                                                        const HomeworldsMoveCandidate *candidate) {
  const HomeworldsMoveBuilderState *state = homeworlds_random_ai_builder_state(builder);

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(candidate != NULL, FALSE);

  if (candidate->data.kind == HOMEWORLDS_CANDIDATE_SELECT_SHIP) {
    return homeworlds_random_ai_candidate_has_continuation(builder, candidate);
  }
  if (state->pending_actions_remaining > 0) {
    return FALSE;
  }

  return candidate->data.kind == HOMEWORLDS_CANDIDATE_ACTION &&
         candidate->data.target_color == HOMEWORLDS_STEP_PASS;
}

static gboolean homeworlds_random_ai_filter_action(const GameBackendMoveBuilder *builder,
                                                   const HomeworldsMoveCandidate *candidate) {
  g_return_val_if_fail(candidate != NULL, FALSE);

  if (candidate->data.kind != HOMEWORLDS_CANDIDATE_ACTION) {
    return FALSE;
  }
  if (candidate->data.target_color == HOMEWORLDS_STEP_ATTACK &&
      !homeworlds_random_ai_candidate_has_continuation(builder, candidate)) {
    return FALSE;
  }

  return homeworlds_random_ai_candidate_has_continuation(builder, candidate);
}

static gboolean homeworlds_random_ai_filter_all(const GameBackendMoveBuilder * /*builder*/,
                                                const HomeworldsMoveCandidate *candidate) {
  return candidate != NULL;
}

static gboolean homeworlds_random_ai_choose_filtered(const GameBackendMoveBuilder *builder,
                                                     const GameBackendMoveList *candidates,
                                                     HomeworldsRandomAiCandidateFilter filter,
                                                     GRand *rand,
                                                     HomeworldsMoveCandidate *out_candidate) {
  guint *indices = NULL;
  guint count = 0;

  g_return_val_if_fail(builder != NULL, FALSE);
  g_return_val_if_fail(candidates != NULL, FALSE);
  g_return_val_if_fail(filter != NULL, FALSE);
  g_return_val_if_fail(out_candidate != NULL, FALSE);

  if (candidates->count == 0) {
    return FALSE;
  }

  indices = g_new0(guint, candidates->count);
  g_return_val_if_fail(indices != NULL, FALSE);

  for (gsize i = 0; i < candidates->count; ++i) {
    const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *) candidates->moves)[i];
    if (!filter(builder, candidate)) {
      continue;
    }

    indices[count++] = (guint) i;
  }

  if (count == 0) {
    g_free(indices);
    return FALSE;
  }

  guint selected = indices[homeworlds_random_ai_int_range(rand, 0, count)];
  *out_candidate = ((const HomeworldsMoveCandidate *) candidates->moves)[selected];
  g_free(indices);
  return TRUE;
}

static gboolean homeworlds_random_ai_choose_move_target(const GameBackendMoveBuilder * /*builder*/,
                                                        const GameBackendMoveList *candidates,
                                                        GRand *rand,
                                                        HomeworldsMoveCandidate *out_candidate) {
  guint *existing = NULL;
  guint *discovery = NULL;
  guint existing_count = 0;
  guint discovery_count = 0;

  g_return_val_if_fail(candidates != NULL, FALSE);
  g_return_val_if_fail(out_candidate != NULL, FALSE);

  if (candidates->count == 0) {
    return FALSE;
  }

  existing = g_new0(guint, candidates->count);
  discovery = g_new0(guint, candidates->count);
  if (existing == NULL || discovery == NULL) {
    g_free(existing);
    g_free(discovery);
    g_return_val_if_fail(existing != NULL && discovery != NULL, FALSE);
  }

  for (gsize i = 0; i < candidates->count; ++i) {
    const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *) candidates->moves)[i];
    if (candidate->data.kind != HOMEWORLDS_CANDIDATE_MOVE_TARGET) {
      continue;
    }

    if (candidate->data.target_system_index == HOMEWORLDS_INVALID_INDEX) {
      discovery[discovery_count++] = (guint) i;
    } else {
      existing[existing_count++] = (guint) i;
    }
  }

  gboolean found = FALSE;
  if (existing_count > 0 || discovery_count > 0) {
    guint selected_choice = homeworlds_random_ai_int_range(rand, 0, existing_count + 1);
    guint selected_index = 0;

    if (selected_choice < existing_count) {
      selected_index = existing[selected_choice];
      found = TRUE;
    } else if (discovery_count > 0) {
      selected_index = discovery[homeworlds_random_ai_int_range(rand, 0, discovery_count)];
      found = TRUE;
    } else if (existing_count > 0) {
      selected_index = existing[homeworlds_random_ai_int_range(rand, 0, existing_count)];
      found = TRUE;
    }

    if (found) {
      *out_candidate = ((const HomeworldsMoveCandidate *) candidates->moves)[selected_index];
    }
  }

  g_free(existing);
  g_free(discovery);
  return found;
}

static gboolean homeworlds_random_ai_choose_candidate(const GameBackendMoveBuilder *builder,
                                                      const GameBackendMoveList *candidates,
                                                      GRand *rand,
                                                      HomeworldsMoveCandidate *out_candidate) {
  const HomeworldsMoveBuilderState *state = homeworlds_random_ai_builder_state(builder);

  g_return_val_if_fail(state != NULL, FALSE);

  switch ((HomeworldsBuilderStage) state->stage) {
    case HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP:
      return homeworlds_random_ai_choose_filtered(builder,
                                                  candidates,
                                                  homeworlds_random_ai_filter_select_ship,
                                                  rand,
                                                  out_candidate);
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ACTION:
      return homeworlds_random_ai_choose_filtered(builder,
                                                  candidates,
                                                  homeworlds_random_ai_filter_action,
                                                  rand,
                                                  out_candidate);
    case HOMEWORLDS_BUILDER_STAGE_SELECT_MOVE_TARGET:
      return homeworlds_random_ai_choose_move_target(builder, candidates, rand, out_candidate);
    case HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR:
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR:
    case HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_TRADE_COLOR:
    case HOMEWORLDS_BUILDER_STAGE_SELECT_ATTACK_TARGET:
      return homeworlds_random_ai_choose_filtered(builder,
                                                  candidates,
                                                  homeworlds_random_ai_filter_all,
                                                  rand,
                                                  out_candidate);
    case HOMEWORLDS_BUILDER_STAGE_COMPLETE:
    default:
      return FALSE;
  }
}

gboolean homeworlds_random_ai_build_move(const HomeworldsPosition *position, GRand *rand, HomeworldsMove *out_move) {
  GameBackendMoveBuilder builder = {0};
  gboolean built = FALSE;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  if (!homeworlds_move_builder_init(position, &builder)) {
    return FALSE;
  }

  for (guint step = 0; step < 32 && !homeworlds_move_builder_is_complete(&builder); ++step) {
    GameBackendMoveList candidates = homeworlds_move_builder_list_candidates(&builder);
    HomeworldsMoveCandidate selected = {0};
    gboolean has_selected = homeworlds_random_ai_choose_candidate(&builder, &candidates, rand, &selected);
    homeworlds_random_ai_move_list_free(&candidates);

    if (!has_selected || !homeworlds_move_builder_step(&builder, &selected)) {
      homeworlds_move_builder_clear(&builder);
      return FALSE;
    }
  }

  built = homeworlds_move_builder_build_move(&builder, out_move);
  homeworlds_move_builder_clear(&builder);
  return built;
}
