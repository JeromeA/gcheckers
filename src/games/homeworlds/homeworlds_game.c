#include "homeworlds_game.h"

#include <string.h>

static gboolean homeworlds_bank_take(HomeworldsPosition *position, HomeworldsPyramid pyramid) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), FALSE);

  for (guint i = 0; i < HOMEWORLDS_BANK_SLOT_COUNT; ++i) {
    if (position->bank[i] != pyramid) {
      continue;
    }

    position->bank[i] = 0;
    return TRUE;
  }

  return FALSE;
}

static gboolean homeworlds_bank_put(HomeworldsPosition *position, HomeworldsPyramid pyramid) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), FALSE);

  for (guint i = 0; i < HOMEWORLDS_BANK_SLOT_COUNT; ++i) {
    if (position->bank[i] != 0) {
      continue;
    }

    position->bank[i] = pyramid;
    return TRUE;
  }

  g_debug("No free bank slot available");
  return FALSE;
}

static gboolean homeworlds_system_add_star(HomeworldsSystem *system, HomeworldsPyramid pyramid) {
  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), FALSE);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    if (system->stars[i] != 0) {
      continue;
    }

    system->stars[i] = pyramid;
    return TRUE;
  }

  return FALSE;
}

static gboolean homeworlds_system_add_ship(HomeworldsSystem *system, guint side, HomeworldsPyramid pyramid) {
  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), FALSE);

  for (guint i = 0; i < HOMEWORLDS_SHIP_SLOT_COUNT; ++i) {
    if (system->ships[side][i] != 0) {
      continue;
    }

    system->ships[side][i] = pyramid;
    return TRUE;
  }

  return FALSE;
}

static gboolean homeworlds_system_remove_ship(HomeworldsSystem *system,
                                              guint side,
                                              guint ship_slot,
                                              HomeworldsPyramid *out_pyramid) {
  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(ship_slot < HOMEWORLDS_SHIP_SLOT_COUNT, FALSE);

  HomeworldsPyramid pyramid = system->ships[side][ship_slot];
  if (pyramid == 0) {
    return FALSE;
  }

  system->ships[side][ship_slot] = 0;
  if (out_pyramid != NULL) {
    *out_pyramid = pyramid;
  }
  return TRUE;
}

static guint homeworlds_system_ship_count_total(const HomeworldsSystem *system) {
  g_return_val_if_fail(system != NULL, 0);

  guint count = 0;
  for (guint side = 0; side < 2; ++side) {
    count += homeworlds_system_ship_count_for_side(system, side);
  }
  return count;
}

static void homeworlds_system_return_all_ships_to_bank(HomeworldsPosition *position, guint system_index) {
  g_return_if_fail(position != NULL);
  g_return_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT);

  HomeworldsSystem *system = &position->systems[system_index];
  for (guint side = 0; side < 2; ++side) {
    for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
      HomeworldsPyramid pyramid = system->ships[side][slot];
      if (pyramid == 0) {
        continue;
      }

      system->ships[side][slot] = 0;
      homeworlds_bank_put(position, pyramid);
    }
  }
}

static void homeworlds_system_cleanup_orphaned_stars(HomeworldsPosition *position, guint system_index) {
  g_return_if_fail(position != NULL);
  g_return_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT);

  HomeworldsSystem *system = &position->systems[system_index];
  if (homeworlds_system_ship_count_total(system) != 0) {
    return;
  }

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    HomeworldsPyramid star = system->stars[i];
    if (star == 0) {
      continue;
    }

    system->stars[i] = 0;
    homeworlds_bank_put(position, star);
  }
}

static gboolean homeworlds_system_contains_star(const HomeworldsSystem *system, HomeworldsPyramid star) {
  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(star), FALSE);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    if (system->stars[i] == star) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean homeworlds_system_find_ship_slot(const HomeworldsSystem *system,
                                                 guint side,
                                                 HomeworldsPyramid ship,
                                                 guint *out_ship_slot) {
  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(ship), FALSE);
  g_return_val_if_fail(out_ship_slot != NULL, FALSE);

  for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    if (system->ships[side][slot] != ship) {
      continue;
    }

    *out_ship_slot = slot;
    return TRUE;
  }

  return FALSE;
}

gboolean homeworlds_position_system_ref_for_index(const HomeworldsPosition *position,
                                                  guint system_index,
                                                  HomeworldsSystemRef *out_ref) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);
  g_return_val_if_fail(out_ref != NULL, FALSE);

  memset(out_ref, 0, sizeof(*out_ref));
  if (system_index < 2) {
    out_ref->kind = HOMEWORLDS_SYSTEM_REF_HOMEWORLD;
    out_ref->homeworld_side = (guint8)system_index;
    return TRUE;
  }

  const HomeworldsSystem *system = &position->systems[system_index];
  HomeworldsPyramid star = 0;
  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    if (!homeworlds_pyramid_is_valid(system->stars[i])) {
      continue;
    }

    star = system->stars[i];
    break;
  }
  if (!homeworlds_pyramid_is_valid(star)) {
    return FALSE;
  }

  guint duplicate_index = 0;
  for (guint i = 2; i < system_index; ++i) {
    if (homeworlds_system_contains_star(&position->systems[i], star)) {
      duplicate_index++;
    }
  }

  out_ref->kind = HOMEWORLDS_SYSTEM_REF_STAR;
  out_ref->star = star;
  out_ref->duplicate_index = (guint8)duplicate_index;
  return TRUE;
}

gboolean homeworlds_position_resolve_system_ref(const HomeworldsPosition *position,
                                                const HomeworldsSystemRef *ref,
                                                guint *out_system_index) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(ref != NULL, FALSE);
  g_return_val_if_fail(out_system_index != NULL, FALSE);

  switch ((HomeworldsSystemRefKind)ref->kind) {
    case HOMEWORLDS_SYSTEM_REF_HOMEWORLD:
      if (ref->homeworld_side >= 2) {
        return FALSE;
      }
      *out_system_index = ref->homeworld_side;
      return TRUE;
    case HOMEWORLDS_SYSTEM_REF_STAR: {
      if (!homeworlds_pyramid_is_valid(ref->star)) {
        return FALSE;
      }

      guint duplicate_index = 0;
      for (guint i = 2; i < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++i) {
        if (!homeworlds_system_contains_star(&position->systems[i], ref->star)) {
          continue;
        }
        if (duplicate_index == ref->duplicate_index) {
          *out_system_index = i;
          return TRUE;
        }
        duplicate_index++;
      }
      return FALSE;
    }
    case HOMEWORLDS_SYSTEM_REF_NONE:
    default:
      return FALSE;
  }
}

static gboolean homeworlds_position_resolve_ship_ref(const HomeworldsPosition *position,
                                                     const HomeworldsShipRef *ref,
                                                     guint side,
                                                     guint *out_system_index,
                                                     guint *out_ship_slot,
                                                     HomeworldsPyramid *out_ship) {
  guint system_index = 0;
  guint ship_slot = 0;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(ref != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(out_system_index != NULL, FALSE);
  g_return_val_if_fail(out_ship_slot != NULL, FALSE);
  g_return_val_if_fail(out_ship != NULL, FALSE);

  if (!homeworlds_pyramid_is_valid(ref->ship) ||
      !homeworlds_position_resolve_system_ref(position, &ref->system, &system_index)) {
    return FALSE;
  }
  if (!homeworlds_system_find_ship_slot(&position->systems[system_index], side, ref->ship, &ship_slot)) {
    return FALSE;
  }

  *out_system_index = system_index;
  *out_ship_slot = ship_slot;
  *out_ship = ref->ship;
  return TRUE;
}

void homeworlds_position_finish_turn(HomeworldsPosition *position) {
  g_return_if_fail(position != NULL);

  if (position->phase != HOMEWORLDS_PHASE_PLAY) {
    return;
  }

  guint acting_side = position->turn;
  guint opponent = acting_side == 0 ? 1 : 0;

  if (homeworlds_system_ship_count_for_side(&position->systems[acting_side], acting_side) == 0) {
    position->phase = HOMEWORLDS_PHASE_FINISHED;
    position->turn = acting_side;
    return;
  }

  if (homeworlds_system_ship_count_for_side(&position->systems[opponent], opponent) == 0) {
    position->phase = HOMEWORLDS_PHASE_FINISHED;
    position->turn = opponent;
    return;
  }

  position->turn = opponent;
}

static gboolean homeworlds_position_apply_setup_move(HomeworldsPosition *position, const HomeworldsMove *move) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  if (position->phase != HOMEWORLDS_PHASE_SETUP || move->kind != HOMEWORLDS_MOVE_KIND_SETUP) {
    return FALSE;
  }

  const guint side = position->turn;
  HomeworldsSystem *homeworld = &position->systems[side];

  if (homeworlds_system_has_star(homeworld) || homeworlds_system_ship_count_total(homeworld) != 0) {
    return FALSE;
  }

  if (!homeworlds_pyramid_is_valid(move->setup_stars[0]) ||
      !homeworlds_pyramid_is_valid(move->setup_stars[1]) ||
      !homeworlds_pyramid_is_valid(move->setup_ship)) {
    return FALSE;
  }

  if (!homeworlds_bank_take(position, move->setup_stars[0]) ||
      !homeworlds_bank_take(position, move->setup_stars[1]) ||
      !homeworlds_bank_take(position, move->setup_ship)) {
    return FALSE;
  }

  if (!homeworlds_system_add_star(homeworld, move->setup_stars[0]) ||
      !homeworlds_system_add_star(homeworld, move->setup_stars[1]) ||
      !homeworlds_system_add_ship(homeworld, side, move->setup_ship)) {
    return FALSE;
  }

  if (side == 0) {
    position->turn = 1;
  } else {
    position->turn = 0;
    position->phase = HOMEWORLDS_PHASE_PLAY;
  }

  return TRUE;
}

static gboolean homeworlds_position_apply_build(HomeworldsPosition *position,
                                                const HomeworldsTurnStep *step,
                                                gboolean require_access) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  guint system_index = 0;
  HomeworldsPyramid built = 0;
  HomeworldsSystem *system = NULL;
  gboolean has_source_color_ship = FALSE;

  if (step->target_color > HOMEWORLDS_COLOR_BLUE ||
      !homeworlds_position_resolve_system_ref(position, &step->actor.system, &system_index)) {
    return FALSE;
  }

  system = &position->systems[system_index];
  for (guint ship_slot = 0; ship_slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++ship_slot) {
    HomeworldsPyramid ship = system->ships[position->turn][ship_slot];

    if (homeworlds_pyramid_is_valid(ship) &&
        homeworlds_pyramid_color(ship) == (HomeworldsColor) step->target_color) {
      has_source_color_ship = TRUE;
      break;
    }
  }
  if (!has_source_color_ship) {
    return FALSE;
  }
  if (require_access && !homeworlds_system_has_access_to_color(system, position->turn, HOMEWORLDS_COLOR_GREEN)) {
    return FALSE;
  }
  if (!homeworlds_system_find_smallest_bank_ship(position, (HomeworldsColor) step->target_color, &built)) {
    return FALSE;
  }
  if (!homeworlds_bank_take(position, built)) {
    return FALSE;
  }

  return homeworlds_system_add_ship(system, position->turn, built);
}

static gboolean homeworlds_position_apply_trade(HomeworldsPosition *position,
                                                const HomeworldsTurnStep *step,
                                                gboolean require_access) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->target_color > HOMEWORLDS_COLOR_BLUE) {
    return FALSE;
  }

  guint system_index = 0;
  guint ship_slot = 0;
  HomeworldsPyramid source = 0;
  if (!homeworlds_position_resolve_ship_ref(position,
                                            &step->actor,
                                            position->turn,
                                            &system_index,
                                            &ship_slot,
                                            &source)) {
    return FALSE;
  }

  HomeworldsSystem *system = &position->systems[system_index];
  if (require_access && !homeworlds_system_has_access_to_color(system, position->turn, HOMEWORLDS_COLOR_BLUE)) {
    return FALSE;
  }
  if (homeworlds_pyramid_color(source) == step->target_color) {
    return FALSE;
  }

  HomeworldsPyramid traded = homeworlds_pyramid_make((HomeworldsColor) step->target_color,
                                                     homeworlds_pyramid_size(source));
  if (!homeworlds_bank_take(position, traded)) {
    return FALSE;
  }

  homeworlds_bank_put(position, source);
  system->ships[position->turn][ship_slot] = traded;
  return TRUE;
}

static gboolean homeworlds_position_apply_attack(HomeworldsPosition *position,
                                                 const HomeworldsTurnStep *step,
                                                 gboolean require_access) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  guint system_index = 0;
  guint ship_slot = 0;
  guint target_slot = 0;
  guint opponent = position->turn == 0 ? 1 : 0;
  HomeworldsPyramid attacker = 0;
  HomeworldsPyramid target = 0;
  HomeworldsSystem *system = NULL;

  if (!homeworlds_position_resolve_ship_ref(position,
                                            &step->actor,
                                            position->turn,
                                            &system_index,
                                            &ship_slot,
                                            &attacker) ||
      !homeworlds_system_find_ship_slot(&position->systems[system_index], opponent, step->target_ship.ship,
                                        &target_slot)) {
    return FALSE;
  }

  system = &position->systems[system_index];
  target = position->systems[system_index].ships[opponent][target_slot];
  if (require_access && !homeworlds_system_has_access_to_color(system, position->turn, HOMEWORLDS_COLOR_RED)) {
    return FALSE;
  }
  if (homeworlds_pyramid_size(attacker) < homeworlds_pyramid_size(target)) {
    return FALSE;
  }

  (void)ship_slot;
  system->ships[opponent][target_slot] = 0;
  return homeworlds_system_add_ship(system, position->turn, target);
}

static gboolean homeworlds_position_move_ship_between_systems(HomeworldsPosition *position,
                                                              guint from_system_index,
                                                              guint ship_slot,
                                                              guint to_system_index) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(from_system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);
  g_return_val_if_fail(to_system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);
  g_return_val_if_fail(ship_slot < HOMEWORLDS_SHIP_SLOT_COUNT, FALSE);

  HomeworldsSystem *from_system = &position->systems[from_system_index];
  HomeworldsSystem *to_system = &position->systems[to_system_index];
  HomeworldsPyramid ship = 0;

  if (!homeworlds_system_remove_ship(from_system, position->turn, ship_slot, &ship)) {
    return FALSE;
  }
  if (!homeworlds_system_add_ship(to_system, position->turn, ship)) {
    homeworlds_system_add_ship(from_system, position->turn, ship);
    return FALSE;
  }

  homeworlds_system_cleanup_orphaned_stars(position, from_system_index);
  return TRUE;
}

static gboolean homeworlds_position_apply_move_or_discover(HomeworldsPosition *position,
                                                           const HomeworldsTurnStep *step,
                                                           gboolean require_access) {
  guint from_system_index = 0;
  guint ship_slot = 0;
  guint target_system_index = 0;
  HomeworldsPyramid ship = 0;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (!homeworlds_position_resolve_ship_ref(position,
                                            &step->actor,
                                            position->turn,
                                            &from_system_index,
                                            &ship_slot,
                                            &ship)) {
    return FALSE;
  }

  HomeworldsSystem *from_system = &position->systems[from_system_index];
  if (require_access && !homeworlds_system_has_access_to_color(from_system, position->turn, HOMEWORLDS_COLOR_YELLOW)) {
    return FALSE;
  }

  if (homeworlds_position_resolve_system_ref(position, &step->target_system, &target_system_index)) {
    HomeworldsSystem *to_system = &position->systems[target_system_index];

    if (target_system_index == from_system_index || !homeworlds_system_is_connected(from_system, to_system)) {
      return FALSE;
    }

    return homeworlds_position_move_ship_between_systems(position,
                                                         from_system_index,
                                                         ship_slot,
                                                         target_system_index);
  }

  if (step->target_system.kind != HOMEWORLDS_SYSTEM_REF_STAR ||
      !homeworlds_pyramid_is_valid(step->target_system.star)) {
    return FALSE;
  }
  if (!homeworlds_position_find_empty_system(position, &target_system_index)) {
    return FALSE;
  }
  if (!homeworlds_bank_take(position, step->target_system.star)) {
    return FALSE;
  }

  HomeworldsSystem *target_system = &position->systems[target_system_index];
  if (!homeworlds_system_add_star(target_system, step->target_system.star)) {
    homeworlds_bank_put(position, step->target_system.star);
    return FALSE;
  }
  if (!homeworlds_system_is_connected(from_system, target_system)) {
    target_system->stars[0] = 0;
    homeworlds_bank_put(position, step->target_system.star);
    return FALSE;
  }
  if (!homeworlds_position_move_ship_between_systems(position,
                                                     from_system_index,
                                                     ship_slot,
                                                     target_system_index)) {
    target_system->stars[0] = 0;
    homeworlds_bank_put(position, step->target_system.star);
    return FALSE;
  }

  (void)ship;
  return TRUE;
}

static gboolean homeworlds_position_apply_sacrifice(HomeworldsPosition *position, const HomeworldsTurnStep *step) {
  guint system_index = 0;
  guint ship_slot = 0;
  HomeworldsPyramid ship = 0;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (!homeworlds_position_resolve_ship_ref(position,
                                            &step->actor,
                                            position->turn,
                                            &system_index,
                                            &ship_slot,
                                            &ship)) {
    return FALSE;
  }
  if (!homeworlds_system_remove_ship(&position->systems[system_index], position->turn, ship_slot, &ship)) {
    return FALSE;
  }

  homeworlds_bank_put(position, ship);
  homeworlds_system_cleanup_orphaned_stars(position, system_index);
  return TRUE;
}

static gboolean homeworlds_position_apply_turn_step_with_access(HomeworldsPosition *position,
                                                                const HomeworldsTurnStep *step,
                                                                gboolean require_access) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  switch ((HomeworldsStepKind) step->kind) {
    case HOMEWORLDS_STEP_PASS:
      return TRUE;
    case HOMEWORLDS_STEP_BUILD:
      return homeworlds_position_apply_build(position, step, require_access);
    case HOMEWORLDS_STEP_TRADE:
      return homeworlds_position_apply_trade(position, step, require_access);
    case HOMEWORLDS_STEP_ATTACK:
      return homeworlds_position_apply_attack(position, step, require_access);
    case HOMEWORLDS_STEP_MOVE:
    case HOMEWORLDS_STEP_DISCOVER:
      return homeworlds_position_apply_move_or_discover(position, step, require_access);
    case HOMEWORLDS_STEP_SACRIFICE:
      return homeworlds_position_apply_sacrifice(position, step);
    case HOMEWORLDS_STEP_CATASTROPHE: {
      guint system_index = 0;
      if (step->target_color > HOMEWORLDS_COLOR_BLUE ||
          !homeworlds_position_resolve_system_ref(position, &step->target_system, &system_index)) {
        return FALSE;
      }
      return homeworlds_position_apply_catastrophe(position, system_index, (HomeworldsColor) step->target_color);
    }
    case HOMEWORLDS_STEP_NONE:
    default:
      return FALSE;
  }
}

gboolean homeworlds_position_apply_turn_step(HomeworldsPosition *position, const HomeworldsTurnStep *step) {
  return homeworlds_position_apply_turn_step_with_access(position, step, TRUE);
}

gboolean homeworlds_position_apply_forced_action_step(HomeworldsPosition *position, const HomeworldsTurnStep *step) {
  g_return_val_if_fail(step != NULL, FALSE);

  switch ((HomeworldsStepKind) step->kind) {
    case HOMEWORLDS_STEP_BUILD:
    case HOMEWORLDS_STEP_TRADE:
    case HOMEWORLDS_STEP_ATTACK:
    case HOMEWORLDS_STEP_MOVE:
    case HOMEWORLDS_STEP_DISCOVER:
      return homeworlds_position_apply_turn_step_with_access(position, step, FALSE);
    case HOMEWORLDS_STEP_NONE:
    case HOMEWORLDS_STEP_PASS:
    case HOMEWORLDS_STEP_SACRIFICE:
    case HOMEWORLDS_STEP_CATASTROPHE:
    default:
      return FALSE;
  }
}

void homeworlds_position_init(HomeworldsPosition *position) {
  g_return_if_fail(position != NULL);

  memset(position, 0, sizeof(*position));
  for (guint i = 0; i < HOMEWORLDS_BANK_SLOT_COUNT; ++i) {
    position->bank[i] = (HomeworldsPyramid) ((i % 12) + 1);
  }
  position->phase = HOMEWORLDS_PHASE_SETUP;
  position->turn = 0;
}

void homeworlds_position_clear(HomeworldsPosition *position) {
  g_return_if_fail(position != NULL);

  memset(position, 0, sizeof(*position));
}

void homeworlds_position_copy(HomeworldsPosition *dest, const HomeworldsPosition *src) {
  g_return_if_fail(dest != NULL);
  g_return_if_fail(src != NULL);

  *dest = *src;
}

GameBackendOutcome homeworlds_position_outcome(const HomeworldsPosition *position) {
  g_return_val_if_fail(position != NULL, GAME_BACKEND_OUTCOME_ONGOING);

  if (position->phase != HOMEWORLDS_PHASE_FINISHED) {
    return GAME_BACKEND_OUTCOME_ONGOING;
  }

  return position->turn == 0 ? GAME_BACKEND_OUTCOME_SIDE_1_WIN : GAME_BACKEND_OUTCOME_SIDE_0_WIN;
}

guint homeworlds_position_turn(const HomeworldsPosition *position) {
  g_return_val_if_fail(position != NULL, 0);

  return position->turn;
}

gboolean homeworlds_position_apply_move(HomeworldsPosition *position, const HomeworldsMove *move) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  if (move->kind == HOMEWORLDS_MOVE_KIND_SETUP) {
    return homeworlds_position_apply_setup_move(position, move);
  }
  if (position->phase != HOMEWORLDS_PHASE_PLAY || move->kind != HOMEWORLDS_MOVE_KIND_TURN || move->step_count == 0) {
    return FALSE;
  }

  guint pending_sacrifice_actions = 0;
  HomeworldsColor sacrifice_color = HOMEWORLDS_COLOR_RED;
  gboolean primary_action_done = FALSE;

  for (guint i = 0; i < move->step_count; ++i) {
    const HomeworldsTurnStep *step = &move->steps[i];
    gboolean require_access = TRUE;

    if (step->kind == HOMEWORLDS_STEP_CATASTROPHE) {
      if (!homeworlds_position_apply_turn_step(position, step)) {
        return FALSE;
      }
      continue;
    }

    if (step->kind == HOMEWORLDS_STEP_PASS) {
      if (pending_sacrifice_actions > 0) {
        pending_sacrifice_actions--;
      } else if (primary_action_done) {
        return FALSE;
      } else {
        primary_action_done = TRUE;
      }
    } else if (step->kind == HOMEWORLDS_STEP_SACRIFICE) {
      if (primary_action_done || pending_sacrifice_actions != 0) {
        return FALSE;
      }

      guint system_index = 0;
      guint ship_slot = 0;
      HomeworldsPyramid ship = 0;
      if (!homeworlds_position_resolve_ship_ref(position,
                                                &step->actor,
                                                position->turn,
                                                &system_index,
                                                &ship_slot,
                                                &ship)) {
        return FALSE;
      }

      (void)system_index;
      (void)ship_slot;
      pending_sacrifice_actions = homeworlds_pyramid_size(ship);
      sacrifice_color = homeworlds_pyramid_color(ship);
      primary_action_done = TRUE;
    } else if (pending_sacrifice_actions == 0) {
      if (primary_action_done) {
        return FALSE;
      }
      primary_action_done = TRUE;
    } else {
      HomeworldsColor action_color = HOMEWORLDS_COLOR_RED;

      switch ((HomeworldsStepKind) step->kind) {
        case HOMEWORLDS_STEP_BUILD:
          action_color = HOMEWORLDS_COLOR_GREEN;
          break;
        case HOMEWORLDS_STEP_TRADE:
          action_color = HOMEWORLDS_COLOR_BLUE;
          break;
        case HOMEWORLDS_STEP_ATTACK:
          action_color = HOMEWORLDS_COLOR_RED;
          break;
        case HOMEWORLDS_STEP_MOVE:
        case HOMEWORLDS_STEP_DISCOVER:
          action_color = HOMEWORLDS_COLOR_YELLOW;
          break;
        default:
          return FALSE;
      }

      if (action_color != sacrifice_color) {
        return FALSE;
      }
      pending_sacrifice_actions--;
      require_access = FALSE;
    }

    if (!homeworlds_position_apply_turn_step_with_access(position, step, require_access)) {
      return FALSE;
    }
  }

  if (pending_sacrifice_actions > 0) {
    return FALSE;
  }

  if (primary_action_done) {
    homeworlds_position_finish_turn(position);
  }
  return TRUE;
}

gboolean homeworlds_position_apply_catastrophe(HomeworldsPosition *position,
                                               guint system_index,
                                               HomeworldsColor color) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, FALSE);

  HomeworldsSystem *system = &position->systems[system_index];
  if (homeworlds_system_color_count(system, color) < 4) {
    return FALSE;
  }

  gboolean destroyed_star = FALSE;
  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    HomeworldsPyramid star = system->stars[i];
    if (!homeworlds_pyramid_is_valid(star) || homeworlds_pyramid_color(star) != color) {
      continue;
    }

    system->stars[i] = 0;
    destroyed_star = TRUE;
    homeworlds_bank_put(position, star);
  }

  for (guint side = 0; side < 2; ++side) {
    for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
      HomeworldsPyramid ship = system->ships[side][slot];
      if (!homeworlds_pyramid_is_valid(ship) || homeworlds_pyramid_color(ship) != color) {
        continue;
      }

      system->ships[side][slot] = 0;
      homeworlds_bank_put(position, ship);
    }
  }

  if (destroyed_star) {
    homeworlds_system_return_all_ships_to_bank(position, system_index);
  }
  homeworlds_system_cleanup_orphaned_stars(position, system_index);

  return TRUE;
}

gboolean homeworlds_system_is_connected(const HomeworldsSystem *left, const HomeworldsSystem *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    HomeworldsPyramid left_star = left->stars[i];
    if (!homeworlds_pyramid_is_valid(left_star)) {
      continue;
    }

    for (guint j = 0; j < HOMEWORLDS_STAR_SLOT_COUNT; ++j) {
      HomeworldsPyramid right_star = right->stars[j];
      if (!homeworlds_pyramid_is_valid(right_star)) {
        continue;
      }

      if (homeworlds_pyramid_size(left_star) == homeworlds_pyramid_size(right_star)) {
        return FALSE;
      }
    }
  }

  return TRUE;
}

guint homeworlds_system_ship_count_for_side(const HomeworldsSystem *system, guint side) {
  g_return_val_if_fail(system != NULL, 0);
  g_return_val_if_fail(side < 2, 0);

  guint count = 0;
  for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    count += system->ships[side][slot] != 0;
  }
  return count;
}

guint homeworlds_system_color_count(const HomeworldsSystem *system, HomeworldsColor color) {
  g_return_val_if_fail(system != NULL, 0);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, 0);

  guint count = 0;
  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    if (homeworlds_pyramid_is_valid(system->stars[i]) && homeworlds_pyramid_color(system->stars[i]) == color) {
      count++;
    }
  }
  for (guint side = 0; side < 2; ++side) {
    for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
      if (homeworlds_pyramid_is_valid(system->ships[side][slot]) &&
          homeworlds_pyramid_color(system->ships[side][slot]) == color) {
        count++;
      }
    }
  }
  return count;
}

gboolean homeworlds_system_has_access_to_color(const HomeworldsSystem *system, guint side, HomeworldsColor color) {
  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, FALSE);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    if (homeworlds_pyramid_is_valid(system->stars[i]) && homeworlds_pyramid_color(system->stars[i]) == color) {
      return TRUE;
    }
  }
  for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    if (homeworlds_pyramid_is_valid(system->ships[side][slot]) &&
        homeworlds_pyramid_color(system->ships[side][slot]) == color) {
      return TRUE;
    }
  }
  return FALSE;
}

gboolean homeworlds_system_find_smallest_bank_ship(const HomeworldsPosition *position,
                                                   HomeworldsColor color,
                                                   HomeworldsPyramid *out_pyramid) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, FALSE);
  g_return_val_if_fail(out_pyramid != NULL, FALSE);

  *out_pyramid = 0;

  for (HomeworldsSize size = HOMEWORLDS_SIZE_SMALL; size <= HOMEWORLDS_SIZE_LARGE; size++) {
    HomeworldsPyramid pyramid = homeworlds_pyramid_make(color, size);
    for (guint slot = 0; slot < HOMEWORLDS_BANK_SLOT_COUNT; ++slot) {
      if (position->bank[slot] != pyramid) {
        continue;
      }

      *out_pyramid = pyramid;
      return TRUE;
    }
  }

  return FALSE;
}

gboolean homeworlds_position_find_empty_system(const HomeworldsPosition *position, guint *out_system_index) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(out_system_index != NULL, FALSE);

  for (guint i = 2; i < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++i) {
    if (!homeworlds_system_is_empty(&position->systems[i])) {
      continue;
    }

    *out_system_index = i;
    return TRUE;
  }

  return FALSE;
}

static gint homeworlds_system_largest_ship_value_for_side(const HomeworldsSystem *system, guint side) {
  gint best_value = 0;

  g_return_val_if_fail(system != NULL, 0);
  g_return_val_if_fail(side < 2, 0);

  for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    HomeworldsPyramid ship = system->ships[side][slot];
    if (!homeworlds_pyramid_is_valid(ship)) {
      continue;
    }

    best_value = MAX(best_value, (gint) homeworlds_pyramid_size(ship) * 10);
  }

  return best_value;
}

gint homeworlds_position_evaluate_static(const HomeworldsPosition *position) {
  g_return_val_if_fail(position != NULL, 0);

  gint score = 0;

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &position->systems[system_index];

    for (guint side = 0; side < 2; ++side) {
      gint side_sign = side == 0 ? 1 : -1;
      for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
        HomeworldsPyramid ship = system->ships[side][slot];
        if (!homeworlds_pyramid_is_valid(ship)) {
          continue;
        }

        score += side_sign * ((gint) homeworlds_pyramid_size(ship) * 10);
      }
    }
  }

  score += homeworlds_system_largest_ship_value_for_side(&position->systems[0], 0);
  score -= homeworlds_system_largest_ship_value_for_side(&position->systems[1], 1);
  return score;
}

gint homeworlds_position_terminal_score(GameBackendOutcome outcome, guint ply_depth) {
  switch (outcome) {
    case GAME_BACKEND_OUTCOME_SIDE_0_WIN:
      return 3000 - (gint) ply_depth;
    case GAME_BACKEND_OUTCOME_SIDE_1_WIN:
      return -3000 + (gint) ply_depth;
    case GAME_BACKEND_OUTCOME_DRAW:
      return 0;
    case GAME_BACKEND_OUTCOME_ONGOING:
    default:
      return 0;
  }
}

guint64 homeworlds_position_hash(const HomeworldsPosition *position) {
  g_return_val_if_fail(position != NULL, 0);

  const guint8 *bytes = (const guint8 *) position;
  guint64 hash = 1469598103934665603ULL;

  for (gsize i = 0; i < sizeof(*position); ++i) {
    hash ^= bytes[i];
    hash *= 1099511628211ULL;
  }

  return hash;
}

static char homeworlds_move_color_letter(HomeworldsColor color, gboolean uppercase) {
  static const char *lowercase = "rygb";
  static const char *uppercase_letters = "RYGB";

  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, '\0');
  return uppercase ? uppercase_letters[color] : lowercase[color];
}

static gboolean homeworlds_move_color_from_letter(char letter, gboolean uppercase, HomeworldsColor *out_color) {
  const char *letters = uppercase ? "RYGB" : "rygb";
  const char *match = NULL;

  g_return_val_if_fail(out_color != NULL, FALSE);

  match = strchr(letters, letter);
  if (match == NULL) {
    return FALSE;
  }

  *out_color = (HomeworldsColor)(match - letters);
  return TRUE;
}

static gboolean homeworlds_move_append_pyramid(GString *text, HomeworldsPyramid pyramid, gboolean uppercase) {
  g_return_val_if_fail(text != NULL, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), FALSE);

  g_string_append_c(text, homeworlds_move_color_letter(homeworlds_pyramid_color(pyramid), uppercase));
  g_string_append_c(text, (char)('0' + homeworlds_pyramid_size(pyramid)));
  return TRUE;
}

static gboolean homeworlds_move_parse_pyramid(const char **cursor,
                                              gboolean uppercase,
                                              HomeworldsPyramid *out_pyramid) {
  HomeworldsColor color = HOMEWORLDS_COLOR_RED;
  HomeworldsSize size = HOMEWORLDS_SIZE_SMALL;

  g_return_val_if_fail(cursor != NULL, FALSE);
  g_return_val_if_fail(*cursor != NULL, FALSE);
  g_return_val_if_fail(out_pyramid != NULL, FALSE);

  if (!homeworlds_move_color_from_letter((*cursor)[0], uppercase, &color) ||
      (*cursor)[1] < '1' ||
      (*cursor)[1] > '3') {
    return FALSE;
  }

  size = (HomeworldsSize)((*cursor)[1] - '0');
  *out_pyramid = homeworlds_pyramid_make(color, size);
  *cursor += 2;
  return TRUE;
}

static gboolean homeworlds_move_append_system_ref(GString *text, const HomeworldsSystemRef *ref) {
  g_return_val_if_fail(text != NULL, FALSE);
  g_return_val_if_fail(ref != NULL, FALSE);

  switch ((HomeworldsSystemRefKind)ref->kind) {
    case HOMEWORLDS_SYSTEM_REF_HOMEWORLD:
      if (ref->homeworld_side >= 2) {
        return FALSE;
      }
      g_string_append_printf(text, "H%u", (guint)ref->homeworld_side + 1);
      return TRUE;
    case HOMEWORLDS_SYSTEM_REF_STAR:
      if (!homeworlds_move_append_pyramid(text, ref->star, TRUE)) {
        return FALSE;
      }
      for (guint i = 0; i < ref->duplicate_index; ++i) {
        g_string_append_c(text, '\'');
      }
      return TRUE;
    case HOMEWORLDS_SYSTEM_REF_NONE:
    default:
      return FALSE;
  }
}

static gboolean homeworlds_move_parse_system_ref(const char **cursor, HomeworldsSystemRef *out_ref) {
  g_return_val_if_fail(cursor != NULL, FALSE);
  g_return_val_if_fail(*cursor != NULL, FALSE);
  g_return_val_if_fail(out_ref != NULL, FALSE);

  memset(out_ref, 0, sizeof(*out_ref));
  if ((*cursor)[0] == 'H') {
    if ((*cursor)[1] != '1' && (*cursor)[1] != '2') {
      return FALSE;
    }

    out_ref->kind = HOMEWORLDS_SYSTEM_REF_HOMEWORLD;
    out_ref->homeworld_side = (guint8)((*cursor)[1] - '1');
    *cursor += 2;
    return TRUE;
  }

  out_ref->kind = HOMEWORLDS_SYSTEM_REF_STAR;
  if (!homeworlds_move_parse_pyramid(cursor, TRUE, &out_ref->star)) {
    memset(out_ref, 0, sizeof(*out_ref));
    return FALSE;
  }

  while (**cursor == '\'') {
    if (out_ref->duplicate_index == G_MAXUINT8) {
      return FALSE;
    }
    out_ref->duplicate_index++;
    (*cursor)++;
  }
  return TRUE;
}

static void homeworlds_move_skip_spaces(const char **cursor) {
  g_return_if_fail(cursor != NULL);
  g_return_if_fail(*cursor != NULL);

  while (g_ascii_isspace(**cursor)) {
    (*cursor)++;
  }
}

static void homeworlds_move_skip_turn_step_separators(const char **cursor) {
  g_return_if_fail(cursor != NULL);
  g_return_if_fail(*cursor != NULL);

  while (**cursor == '/' || g_ascii_isspace(**cursor)) {
    (*cursor)++;
  }
}

static gboolean homeworlds_move_at_separator(char character) {
  return character == '\0' || character == '/' || g_ascii_isspace(character);
}

static gboolean homeworlds_move_append_turn_step(GString *text, const HomeworldsTurnStep *step) {
  g_return_val_if_fail(text != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->kind == HOMEWORLDS_STEP_PASS) {
    g_string_append(text, "pass");
    return TRUE;
  }
  if (step->kind == HOMEWORLDS_STEP_CATASTROPHE) {
    if (step->target_color > HOMEWORLDS_COLOR_BLUE ||
        !homeworlds_move_append_system_ref(text, &step->target_system)) {
      return FALSE;
    }

    g_string_append_c(text, homeworlds_move_color_letter((HomeworldsColor)step->target_color, FALSE));
    g_string_append_c(text, '!');
    return TRUE;
  }

  if (!homeworlds_move_append_system_ref(text, &step->actor.system)) {
    return FALSE;
  }

  switch ((HomeworldsStepKind)step->kind) {
    case HOMEWORLDS_STEP_BUILD:
      if (step->target_color > HOMEWORLDS_COLOR_BLUE) {
        return FALSE;
      }
      g_string_append_c(text, homeworlds_move_color_letter((HomeworldsColor)step->target_color, FALSE));
      g_string_append_c(text, '+');
      return TRUE;
    case HOMEWORLDS_STEP_TRADE:
      if (!homeworlds_move_append_pyramid(text, step->actor.ship, FALSE)) {
        return FALSE;
      }
      if (step->target_color > HOMEWORLDS_COLOR_BLUE) {
        return FALSE;
      }
      g_string_append_c(text, '=');
      g_string_append_c(text, homeworlds_move_color_letter((HomeworldsColor)step->target_color, FALSE));
      return TRUE;
    case HOMEWORLDS_STEP_ATTACK:
      if (!homeworlds_move_append_pyramid(text, step->actor.ship, FALSE)) {
        return FALSE;
      }
      g_string_append_c(text, 'x');
      return homeworlds_move_append_pyramid(text, step->target_ship.ship, FALSE);
    case HOMEWORLDS_STEP_MOVE:
    case HOMEWORLDS_STEP_DISCOVER:
      if (!homeworlds_move_append_pyramid(text, step->actor.ship, FALSE)) {
        return FALSE;
      }
      g_string_append_c(text, '>');
      return homeworlds_move_append_system_ref(text, &step->target_system);
    case HOMEWORLDS_STEP_SACRIFICE:
      if (!homeworlds_move_append_pyramid(text, step->actor.ship, FALSE)) {
        return FALSE;
      }
      g_string_append_c(text, '-');
      return TRUE;
    case HOMEWORLDS_STEP_NONE:
    case HOMEWORLDS_STEP_PASS:
    case HOMEWORLDS_STEP_CATASTROPHE:
    default:
      return FALSE;
  }
}

gboolean homeworlds_move_format(const HomeworldsMove *move, char *buffer, gsize size) {
  GString *text = NULL;
  gboolean ok = FALSE;

  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(size > 0, FALSE);

  text = g_string_new(NULL);
  g_return_val_if_fail(text != NULL, FALSE);

  if (move->kind == HOMEWORLDS_MOVE_KIND_SETUP) {
    if (!homeworlds_move_append_pyramid(text, move->setup_stars[0], TRUE) ||
        !homeworlds_move_append_pyramid(text, move->setup_stars[1], TRUE) ||
        !homeworlds_move_append_pyramid(text, move->setup_ship, FALSE)) {
      g_string_free(text, TRUE);
      return FALSE;
    }
  } else if (move->kind == HOMEWORLDS_MOVE_KIND_TURN &&
             move->step_count > 0 &&
             move->step_count <= HOMEWORLDS_MAX_MOVE_STEPS) {
    for (guint i = 0; i < move->step_count; ++i) {
      if (i > 0) {
        g_string_append_c(text, ' ');
      }
      if (!homeworlds_move_append_turn_step(text, &move->steps[i])) {
        g_string_free(text, TRUE);
        return FALSE;
      }
    }
  } else {
    g_string_free(text, TRUE);
    return FALSE;
  }

  ok = text->len < size;
  if (ok) {
    memcpy(buffer, text->str, text->len + 1);
  }
  g_string_free(text, TRUE);
  return ok;
}

static gboolean homeworlds_move_parse_setup(const char *notation, HomeworldsMove *out_move) {
  const char *cursor = notation;

  g_return_val_if_fail(notation != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  if (!homeworlds_move_parse_pyramid(&cursor, TRUE, &out_move->setup_stars[0]) ||
      !homeworlds_move_parse_pyramid(&cursor, TRUE, &out_move->setup_stars[1]) ||
      !homeworlds_move_parse_pyramid(&cursor, FALSE, &out_move->setup_ship) ||
      *cursor != '\0') {
    return FALSE;
  }

  out_move->kind = HOMEWORLDS_MOVE_KIND_SETUP;
  return TRUE;
}

static gboolean homeworlds_move_append_parsed_step(HomeworldsMove *move, const HomeworldsTurnStep *step) {
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (move->step_count >= HOMEWORLDS_MAX_MOVE_STEPS) {
    return FALSE;
  }

  move->steps[move->step_count++] = *step;
  return TRUE;
}

static gboolean homeworlds_move_parse_pass(const char **cursor, HomeworldsTurnStep *out_step) {
  g_return_val_if_fail(cursor != NULL, FALSE);
  g_return_val_if_fail(*cursor != NULL, FALSE);
  g_return_val_if_fail(out_step != NULL, FALSE);

  if (!g_str_has_prefix(*cursor, "pass") || !homeworlds_move_at_separator((*cursor)[4])) {
    return FALSE;
  }

  memset(out_step, 0, sizeof(*out_step));
  out_step->kind = HOMEWORLDS_STEP_PASS;
  *cursor += 4;
  return TRUE;
}

static gboolean homeworlds_move_parse_turn_step(const char **cursor, HomeworldsTurnStep *out_step) {
  g_return_val_if_fail(cursor != NULL, FALSE);
  g_return_val_if_fail(*cursor != NULL, FALSE);
  g_return_val_if_fail(out_step != NULL, FALSE);

  if (homeworlds_move_parse_pass(cursor, out_step)) {
    return TRUE;
  }

  memset(out_step, 0, sizeof(*out_step));
  if (!homeworlds_move_parse_system_ref(cursor, &out_step->actor.system)) {
    return FALSE;
  }
  out_step->target_system = out_step->actor.system;
  homeworlds_move_skip_spaces(cursor);

  HomeworldsColor color = HOMEWORLDS_COLOR_RED;
  if (homeworlds_move_color_from_letter(**cursor, FALSE, &color) && (*cursor)[1] == '!') {
    out_step->kind = HOMEWORLDS_STEP_CATASTROPHE;
    out_step->target_system = out_step->actor.system;
    out_step->target_color = color;
    *cursor += 2;
    return TRUE;
  }
  if (homeworlds_move_color_from_letter(**cursor, FALSE, &color) && (*cursor)[1] == '+') {
    out_step->kind = HOMEWORLDS_STEP_BUILD;
    out_step->target_color = color;
    *cursor += 2;
    return TRUE;
  }

  if (!homeworlds_move_parse_pyramid(cursor, FALSE, &out_step->actor.ship)) {
    return FALSE;
  }
  homeworlds_move_skip_spaces(cursor);

  switch (**cursor) {
    case '=':
      (*cursor)++;
      homeworlds_move_skip_spaces(cursor);
      if (!homeworlds_move_color_from_letter(**cursor, FALSE, &color)) {
        return FALSE;
      }
      out_step->kind = HOMEWORLDS_STEP_TRADE;
      out_step->target_color = color;
      (*cursor)++;
      return TRUE;
    case 'x':
      (*cursor)++;
      homeworlds_move_skip_spaces(cursor);
      out_step->kind = HOMEWORLDS_STEP_ATTACK;
      out_step->target_ship.system = out_step->actor.system;
      return homeworlds_move_parse_pyramid(cursor, FALSE, &out_step->target_ship.ship);
    case '>':
      (*cursor)++;
      homeworlds_move_skip_spaces(cursor);
      out_step->kind = HOMEWORLDS_STEP_MOVE;
      return homeworlds_move_parse_system_ref(cursor, &out_step->target_system);
    case '-':
      out_step->kind = HOMEWORLDS_STEP_SACRIFICE;
      (*cursor)++;
      return TRUE;
    default:
      return FALSE;
  }
}

static gboolean homeworlds_move_parse_turn(const char *notation, HomeworldsMove *out_move) {
  const char *cursor = notation;

  g_return_val_if_fail(notation != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  out_move->kind = HOMEWORLDS_MOVE_KIND_TURN;
  while (*cursor != '\0') {
    HomeworldsTurnStep step = {0};

    homeworlds_move_skip_turn_step_separators(&cursor);
    if (*cursor == '\0') {
      break;
    }
    if (!homeworlds_move_parse_turn_step(&cursor, &step) ||
        !homeworlds_move_at_separator(*cursor) ||
        !homeworlds_move_append_parsed_step(out_move, &step)) {
      return FALSE;
    }

    homeworlds_move_skip_turn_step_separators(&cursor);
  }

  return out_move->step_count > 0;
}

gboolean homeworlds_move_parse(const char *notation, HomeworldsMove *out_move) {
  g_return_val_if_fail(notation != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  memset(out_move, 0, sizeof(*out_move));
  if (notation[0] == '\0') {
    return FALSE;
  }
  if (g_ascii_isupper(notation[0]) && notation[0] != 'H') {
    if (homeworlds_move_parse_setup(notation, out_move)) {
      return TRUE;
    }
    memset(out_move, 0, sizeof(*out_move));
  }

  return homeworlds_move_parse_turn(notation, out_move);
}
