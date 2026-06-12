#include "homeworlds_game.h"

#include "homeworlds_move_builder.h"

#include <string.h>

typedef struct {
  HomeworldsMove *moves;
  gsize count;
  gsize capacity;
} HomeworldsMoveListBuilder;

typedef gboolean (*HomeworldsMoveCollectorFunc)(gconstpointer move, gpointer user_data);

typedef struct {
  guint system_index;
  HomeworldsColor color;
} HomeworldsCatastropheChoice;

static const HomeworldsEvalWeights homeworlds_default_eval_weights = {
  .ship_values = {
    [HOMEWORLDS_SIZE_SMALL] = 10,
    [HOMEWORLDS_SIZE_MEDIUM] = 20,
    [HOMEWORLDS_SIZE_LARGE] = 30,
  },
  .homeworld_ship_values = {
    [HOMEWORLDS_SIZE_SMALL] = 0,
    [HOMEWORLDS_SIZE_MEDIUM] = 10,
    [HOMEWORLDS_SIZE_LARGE] = 10,
  },
  .empty_homeworld_value = 80,
  .single_star_homeworld_penalty = -60,
  .buildable_color_value = 30,
};

static HomeworldsEvalWeights homeworlds_active_eval_weights = {
  .ship_values = {
    [HOMEWORLDS_SIZE_SMALL] = 10,
    [HOMEWORLDS_SIZE_MEDIUM] = 20,
    [HOMEWORLDS_SIZE_LARGE] = 30,
  },
  .homeworld_ship_values = {
    [HOMEWORLDS_SIZE_SMALL] = 0,
    [HOMEWORLDS_SIZE_MEDIUM] = 10,
    [HOMEWORLDS_SIZE_LARGE] = 10,
  },
  .empty_homeworld_value = 80,
  .single_star_homeworld_penalty = -60,
  .buildable_color_value = 30,
};

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

static void homeworlds_system_count_pyramid(guint8 counts[HOMEWORLDS_COLOR_BLUE + 1],
                                            HomeworldsPyramid pyramid,
                                            gint delta) {
  g_return_if_fail(counts != NULL);
  g_return_if_fail(homeworlds_pyramid_is_valid(pyramid));
  g_return_if_fail(delta == 1 || delta == -1);

  HomeworldsColor color = homeworlds_pyramid_color(pyramid);
  if (delta < 0) {
    g_return_if_fail(counts[color] > 0);
    counts[color]--;
  } else {
    counts[color]++;
  }
}

static void homeworlds_system_set_star_slot(HomeworldsSystem *system, guint slot, HomeworldsPyramid pyramid) {
  g_return_if_fail(system != NULL);
  g_return_if_fail(slot < HOMEWORLDS_STAR_SLOT_COUNT);
  g_return_if_fail(pyramid == 0 || homeworlds_pyramid_is_valid(pyramid));

  HomeworldsPyramid old_pyramid = system->stars[slot];
  if (homeworlds_pyramid_is_valid(old_pyramid)) {
    homeworlds_system_count_pyramid(system->star_color_counts, old_pyramid, -1);
  }
  system->stars[slot] = pyramid;
  if (homeworlds_pyramid_is_valid(pyramid)) {
    homeworlds_system_count_pyramid(system->star_color_counts, pyramid, 1);
  }
}

static void homeworlds_system_set_ship_slot(HomeworldsSystem *system,
                                            guint side,
                                            guint slot,
                                            HomeworldsPyramid pyramid) {
  g_return_if_fail(system != NULL);
  g_return_if_fail(side < 2);
  g_return_if_fail(slot < HOMEWORLDS_SHIP_SLOT_COUNT);
  g_return_if_fail(pyramid == 0 || homeworlds_pyramid_is_valid(pyramid));

  HomeworldsPyramid old_pyramid = system->ships[side][slot];
  if (pyramid == 0) {
    g_return_if_fail(homeworlds_pyramid_is_valid(old_pyramid));

    homeworlds_system_count_pyramid(system->ship_color_counts[side], old_pyramid, -1);
    for (guint next_slot = slot + 1; next_slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++next_slot) {
      HomeworldsPyramid next_pyramid = system->ships[side][next_slot];

      system->ships[side][next_slot - 1] = next_pyramid;
      if (!homeworlds_pyramid_is_valid(next_pyramid)) {
        return;
      }
    }
    system->ships[side][HOMEWORLDS_SHIP_SLOT_COUNT - 1] = 0;
    return;
  }

  if (homeworlds_pyramid_is_valid(old_pyramid)) {
    homeworlds_system_count_pyramid(system->ship_color_counts[side], old_pyramid, -1);
  }
  system->ships[side][slot] = pyramid;
  if (homeworlds_pyramid_is_valid(pyramid)) {
    homeworlds_system_count_pyramid(system->ship_color_counts[side], pyramid, 1);
  }
}

static gboolean homeworlds_system_add_star(HomeworldsSystem *system, HomeworldsPyramid pyramid) {
  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), FALSE);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    if (system->stars[i] != 0) {
      continue;
    }

    homeworlds_system_set_star_slot(system, i, pyramid);
    return TRUE;
  }

  return FALSE;
}

static gboolean homeworlds_system_add_ship(HomeworldsSystem *system, guint side, HomeworldsPyramid pyramid) {
  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), FALSE);

  guint ship_count = homeworlds_system_ship_count_for_side(system, side);
  if (ship_count >= HOMEWORLDS_SHIP_SLOT_COUNT) {
    return FALSE;
  }
  if (system->ships[side][ship_count] != 0) {
    g_debug("Homeworlds ship slots are not compact for side %u", side);
    return FALSE;
  }

  homeworlds_system_set_ship_slot(system, side, ship_count, pyramid);
  return TRUE;
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

  homeworlds_system_set_ship_slot(system, side, ship_slot, 0);
  if (out_pyramid != NULL) {
    *out_pyramid = pyramid;
  }
  return TRUE;
}

static void homeworlds_system_return_all_ships_to_bank(HomeworldsPosition *position, guint system_index) {
  g_return_if_fail(position != NULL);
  g_return_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT);

  HomeworldsSystem *system = &position->systems[system_index];
  for (guint side = 0; side < 2; ++side) {
    while (homeworlds_pyramid_is_valid(system->ships[side][0])) {
      HomeworldsPyramid pyramid = system->ships[side][0];

      homeworlds_system_set_ship_slot(system, side, 0, 0);
      homeworlds_bank_put(position, pyramid);
    }
  }
}

static void homeworlds_system_cleanup_orphaned_stars(HomeworldsPosition *position, guint system_index) {
  g_return_if_fail(position != NULL);
  g_return_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT);

  HomeworldsSystem *system = &position->systems[system_index];
  if (homeworlds_system_has_any_ships(system)) {
    return;
  }

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    HomeworldsPyramid star = system->stars[i];
    if (star == 0) {
      continue;
    }

    homeworlds_system_set_star_slot(system, i, 0);
    homeworlds_bank_put(position, star);
  }
}

static gboolean homeworlds_system_find_ship_slot(const HomeworldsSystem *system,
                                                 guint side,
                                                 HomeworldsPyramid ship,
                                                 guint *out_ship_slot) {
  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(ship), FALSE);
  g_return_val_if_fail(out_ship_slot != NULL, FALSE);

  *out_ship_slot = HOMEWORLDS_INVALID_INDEX;

  for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    HomeworldsPyramid candidate = system->ships[side][slot];

    if (!homeworlds_pyramid_is_valid(candidate)) {
      break;
    }
    if (candidate != ship) {
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
  if (homeworlds_system_is_empty(&position->systems[system_index])) {
    return FALSE;
  }

  out_ref->kind = HOMEWORLDS_SYSTEM_REF_SYSTEM;
  out_ref->system_index = (guint8)system_index;
  return TRUE;
}

gboolean homeworlds_position_resolve_system_ref(const HomeworldsPosition *position,
                                                const HomeworldsSystemRef *ref,
                                                guint *out_system_index) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(ref != NULL, FALSE);
  g_return_val_if_fail(out_system_index != NULL, FALSE);

  *out_system_index = HOMEWORLDS_INVALID_INDEX;

  switch ((HomeworldsSystemRefKind)ref->kind) {
    case HOMEWORLDS_SYSTEM_REF_HOMEWORLD:
      if (ref->homeworld_side >= 2) {
        return FALSE;
      }
      *out_system_index = ref->homeworld_side;
      return TRUE;
    case HOMEWORLDS_SYSTEM_REF_SYSTEM:
      if (ref->system_index < 2 ||
          ref->system_index >= HOMEWORLDS_SYSTEM_SLOT_COUNT ||
          homeworlds_pyramid_is_valid(ref->star) ||
          homeworlds_system_is_empty(&position->systems[ref->system_index])) {
        return FALSE;
      }
      *out_system_index = ref->system_index;
      return TRUE;
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

  *out_system_index = HOMEWORLDS_INVALID_INDEX;
  *out_ship_slot = HOMEWORLDS_INVALID_INDEX;
  *out_ship = 0;

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

  if (!homeworlds_system_has_ships_for_side(&position->systems[acting_side], acting_side)) {
    position->phase = HOMEWORLDS_PHASE_FINISHED;
    position->turn = acting_side;
    return;
  }

  if (!homeworlds_system_has_ships_for_side(&position->systems[opponent], opponent)) {
    position->phase = HOMEWORLDS_PHASE_FINISHED;
    position->turn = opponent;
    return;
  }

  position->turn = opponent;
}

static gboolean homeworlds_position_homeworld_ship_loss_ends_turn(const HomeworldsPosition *position,
                                                                  const guint initial_homeworld_has_ships[2]) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(initial_homeworld_has_ships != NULL, FALSE);

  for (guint side = 0; side < 2; ++side) {
    if (!initial_homeworld_has_ships[side]) {
      continue;
    }
    if (!homeworlds_system_has_ships_for_side(&position->systems[side], side)) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean homeworlds_position_apply_setup_move(HomeworldsPosition *position, const HomeworldsMove *move) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  if (position->phase != HOMEWORLDS_PHASE_SETUP || move->kind != HOMEWORLDS_MOVE_KIND_SETUP) {
    return FALSE;
  }

  const guint side = position->turn;
  HomeworldsSystem *homeworld = &position->systems[side];

  if (homeworlds_system_has_star(homeworld) || homeworlds_system_has_any_ships(homeworld)) {
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
  has_source_color_ship = system->ship_color_counts[position->turn][step->target_color] > 0;
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
  homeworlds_system_set_ship_slot(system, position->turn, ship_slot, traded);
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
  homeworlds_system_set_ship_slot(system, opponent, target_slot, 0);
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

  if (step->kind == HOMEWORLDS_STEP_MOVE &&
      homeworlds_position_resolve_system_ref(position, &step->target_system, &target_system_index)) {
    HomeworldsSystem *to_system = &position->systems[target_system_index];

    if (target_system_index == from_system_index || !homeworlds_system_is_connected(from_system, to_system)) {
      return FALSE;
    }

    return homeworlds_position_move_ship_between_systems(position,
                                                         from_system_index,
                                                         ship_slot,
                                                         target_system_index);
  }

  if (step->kind != HOMEWORLDS_STEP_DISCOVER ||
      step->target_system.kind != HOMEWORLDS_SYSTEM_REF_SYSTEM ||
      step->target_system.system_index < 2 ||
      step->target_system.system_index >= HOMEWORLDS_SYSTEM_SLOT_COUNT ||
      !homeworlds_pyramid_is_valid(step->target_system.star)) {
    return FALSE;
  }
  target_system_index = step->target_system.system_index;
  if (!homeworlds_system_is_empty(&position->systems[target_system_index])) {
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
    homeworlds_system_set_star_slot(target_system, 0, 0);
    homeworlds_bank_put(position, step->target_system.star);
    return FALSE;
  }
  if (!homeworlds_position_move_ship_between_systems(position,
                                                     from_system_index,
                                                     ship_slot,
                                                     target_system_index)) {
    homeworlds_system_set_star_slot(target_system, 0, 0);
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

static gboolean homeworlds_position_apply_turn_step_in_place(HomeworldsPosition *position,
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

static gboolean homeworlds_position_apply_turn_step_with_access(HomeworldsPosition *position,
                                                                const HomeworldsTurnStep *step,
                                                                gboolean require_access) {
  HomeworldsPosition working = {0};

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  working = *position;
  if (!homeworlds_position_apply_turn_step_in_place(&working, step, require_access)) {
    return FALSE;
  }

  *position = working;
  return TRUE;
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

  HomeworldsPosition working = *position;

  if (move->kind == HOMEWORLDS_MOVE_KIND_SETUP) {
    if (!homeworlds_position_apply_setup_move(&working, move)) {
      return FALSE;
    }

    *position = working;
    return TRUE;
  }
  if (working.phase != HOMEWORLDS_PHASE_PLAY ||
      move->kind != HOMEWORLDS_MOVE_KIND_TURN ||
      move->step_count == 0 ||
      move->step_count > HOMEWORLDS_MAX_MOVE_STEPS) {
    return FALSE;
  }

  guint pending_sacrifice_actions = 0;
  guint initial_homeworld_has_ships[2] = {
    homeworlds_system_has_ships_for_side(&working.systems[0], 0),
    homeworlds_system_has_ships_for_side(&working.systems[1], 1),
  };
  HomeworldsColor sacrifice_color = HOMEWORLDS_COLOR_RED;
  gboolean primary_action_done = FALSE;

  for (guint i = 0; i < move->step_count; ++i) {
    const HomeworldsTurnStep *step = &move->steps[i];
    gboolean require_access = TRUE;

    if (step->kind == HOMEWORLDS_STEP_CATASTROPHE) {
      if (!homeworlds_position_apply_turn_step(&working, step)) {
        return FALSE;
      }
      if (homeworlds_position_homeworld_ship_loss_ends_turn(&working, initial_homeworld_has_ships)) {
        if (i + 1 < move->step_count) {
          return FALSE;
        }
        homeworlds_position_finish_turn(&working);
        *position = working;
        return TRUE;
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
      if (!homeworlds_position_resolve_ship_ref(&working,
                                                &step->actor,
                                                working.turn,
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

    if (!homeworlds_position_apply_turn_step_with_access(&working, step, require_access)) {
      return FALSE;
    }
    if (homeworlds_position_homeworld_ship_loss_ends_turn(&working, initial_homeworld_has_ships)) {
      if (i + 1 < move->step_count) {
        return FALSE;
      }
      homeworlds_position_finish_turn(&working);
      *position = working;
      return TRUE;
    }
  }

  if (pending_sacrifice_actions > 0) {
    return FALSE;
  }

  if (primary_action_done) {
    homeworlds_position_finish_turn(&working);
  }
  *position = working;
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

    homeworlds_system_set_star_slot(system, i, 0);
    destroyed_star = TRUE;
    homeworlds_bank_put(position, star);
  }

  for (guint side = 0; side < 2; ++side) {
    for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT;) {
      HomeworldsPyramid ship = system->ships[side][slot];
      if (!homeworlds_pyramid_is_valid(ship)) {
        break;
      }
      if (homeworlds_pyramid_color(ship) != color) {
        slot++;
        continue;
      }

      homeworlds_system_set_ship_slot(system, side, slot, 0);
      homeworlds_bank_put(position, ship);
    }
  }

  if (destroyed_star && !homeworlds_system_has_star(system)) {
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
  for (HomeworldsColor color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; color++) {
    count += system->ship_color_counts[side][color];
  }
  return count;
}

static void homeworlds_system_compact_ship_slots(HomeworldsSystem *system, guint side) {
  guint write_slot = 0;

  g_return_if_fail(system != NULL);
  g_return_if_fail(side < 2);

  for (guint read_slot = 0; read_slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++read_slot) {
    HomeworldsPyramid ship = system->ships[side][read_slot];
    if (!homeworlds_pyramid_is_valid(ship)) {
      continue;
    }

    system->ships[side][write_slot++] = ship;
  }
  while (write_slot < HOMEWORLDS_SHIP_SLOT_COUNT) {
    system->ships[side][write_slot++] = 0;
  }
}

void homeworlds_system_rebuild_color_counts(HomeworldsSystem *system) {
  g_return_if_fail(system != NULL);

  memset(system->star_color_counts, 0, sizeof(system->star_color_counts));
  memset(system->ship_color_counts, 0, sizeof(system->ship_color_counts));
  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    if (homeworlds_pyramid_is_valid(system->stars[i])) {
      system->star_color_counts[homeworlds_pyramid_color(system->stars[i])]++;
    }
  }
  for (guint side = 0; side < 2; ++side) {
    homeworlds_system_compact_ship_slots(system, side);
    for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
      HomeworldsPyramid ship = system->ships[side][slot];
      if (!homeworlds_pyramid_is_valid(ship)) {
        break;
      }

      system->ship_color_counts[side][homeworlds_pyramid_color(ship)]++;
    }
  }
}

void homeworlds_position_rebuild_color_counts(HomeworldsPosition *position) {
  g_return_if_fail(position != NULL);

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    homeworlds_system_rebuild_color_counts(&position->systems[system_index]);
  }
}

gboolean homeworlds_system_has_access_to_color(const HomeworldsSystem *system, guint side, HomeworldsColor color) {
  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, FALSE);

  return homeworlds_system_accessible_color_count(system, side, color) > 0;
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

  *out_system_index = HOMEWORLDS_INVALID_INDEX;

  for (guint i = 2; i < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++i) {
    if (!homeworlds_system_is_empty(&position->systems[i])) {
      continue;
    }

    *out_system_index = i;
    return TRUE;
  }

  return FALSE;
}

const HomeworldsEvalWeights *homeworlds_eval_weights_default(void) {
  return &homeworlds_default_eval_weights;
}

void homeworlds_eval_weights_reset_active(void) {
  homeworlds_active_eval_weights = homeworlds_default_eval_weights;
}

void homeworlds_eval_weights_set_active(const HomeworldsEvalWeights *weights) {
  g_return_if_fail(weights != NULL);

  homeworlds_active_eval_weights = *weights;
}

static gint homeworlds_eval_ship_value(const HomeworldsEvalWeights *weights, HomeworldsPyramid ship) {
  g_return_val_if_fail(weights != NULL, 0);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(ship), 0);

  HomeworldsSize size = homeworlds_pyramid_size(ship);
  g_return_val_if_fail(size >= HOMEWORLDS_SIZE_SMALL && size <= HOMEWORLDS_SIZE_LARGE, 0);

  return weights->ship_values[size];
}

static gint homeworlds_system_largest_ship_value_for_side(const HomeworldsSystem *system,
                                                          guint side,
                                                          const HomeworldsEvalWeights *weights) {
  HomeworldsSize largest_size = 0;

  g_return_val_if_fail(system != NULL, 0);
  g_return_val_if_fail(side < 2, 0);
  g_return_val_if_fail(weights != NULL, 0);

  for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    HomeworldsPyramid ship = system->ships[side][slot];
    if (!homeworlds_pyramid_is_valid(ship)) {
      break;
    }

    largest_size = MAX(largest_size, homeworlds_pyramid_size(ship));
  }

  if (largest_size == 0) {
    return 0;
  }
  return weights->homeworld_ship_values[largest_size];
}

static guint homeworlds_system_star_count(const HomeworldsSystem *system) {
  g_return_val_if_fail(system != NULL, 0);

  guint count = 0;
  for (guint slot = 0; slot < HOMEWORLDS_STAR_SLOT_COUNT; ++slot) {
    count += homeworlds_pyramid_is_valid(system->stars[slot]);
  }
  return count;
}

static gint homeworlds_homeworld_static_value_for_side(const HomeworldsSystem *homeworld,
                                                       guint side,
                                                       const HomeworldsEvalWeights *weights) {
  g_return_val_if_fail(homeworld != NULL, 0);
  g_return_val_if_fail(side < 2, 0);
  g_return_val_if_fail(weights != NULL, 0);

  guint star_count = homeworlds_system_star_count(homeworld);
  if (star_count == 0 && !homeworlds_system_has_any_ships(homeworld)) {
    return weights->empty_homeworld_value;
  }

  gint value = homeworlds_system_largest_ship_value_for_side(homeworld, side, weights);
  if (star_count == 1) {
    value += weights->single_star_homeworld_penalty;
  }
  return value;
}

static guint homeworlds_position_buildable_color_count_for_side(const HomeworldsPosition *position, guint side) {
  gboolean buildable[HOMEWORLDS_COLOR_BLUE + 1] = {0};

  g_return_val_if_fail(position != NULL, 0);
  g_return_val_if_fail(side < 2, 0);

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &position->systems[system_index];
    if (homeworlds_system_accessible_color_count(system, side, HOMEWORLDS_COLOR_GREEN) == 0) {
      continue;
    }

    for (HomeworldsColor color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; color++) {
      if (system->ship_color_counts[side][color] > 0) {
        buildable[color] = TRUE;
      }
    }
  }

  guint count = 0;
  for (HomeworldsColor color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; color++) {
    count += buildable[color];
  }
  return count;
}

static gint homeworlds_position_evaluate_static_for_side(const HomeworldsPosition *position,
                                                         guint side,
                                                         const HomeworldsEvalWeights *weights) {
  g_return_val_if_fail(position != NULL, 0);
  g_return_val_if_fail(side < 2, 0);
  g_return_val_if_fail(weights != NULL, 0);

  gint score = 0;

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &position->systems[system_index];

    for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
      HomeworldsPyramid ship = system->ships[side][slot];
      if (!homeworlds_pyramid_is_valid(ship)) {
        break;
      }

      score += homeworlds_eval_ship_value(weights, ship);
    }
  }

  const HomeworldsSystem *homeworld = &position->systems[side];
  score += homeworlds_homeworld_static_value_for_side(homeworld, side, weights);
  score += (gint)homeworlds_position_buildable_color_count_for_side(position, side) *
           weights->buildable_color_value;
  return score;
}

gint homeworlds_position_evaluate_static_with_weights(const HomeworldsPosition *position,
                                                       const HomeworldsEvalWeights *weights) {
  g_return_val_if_fail(position != NULL, 0);
  g_return_val_if_fail(weights != NULL, 0);

  return homeworlds_position_evaluate_static_for_side(position, 0, weights) -
         homeworlds_position_evaluate_static_for_side(position, 1, weights);
}

gint homeworlds_position_evaluate_static(const HomeworldsPosition *position) {
  return homeworlds_position_evaluate_static_with_weights(position, &homeworlds_active_eval_weights);
}

gint homeworlds_position_terminal_score(GameBackendOutcome outcome, guint ply_depth) {
  switch (outcome) {
    case GAME_BACKEND_OUTCOME_SIDE_0_WIN:
      return 1000 - (gint) ply_depth;
    case GAME_BACKEND_OUTCOME_SIDE_1_WIN:
      return -1000 + (gint) ply_depth;
    case GAME_BACKEND_OUTCOME_DRAW:
      return 0;
    case GAME_BACKEND_OUTCOME_ONGOING:
    default:
      return 0;
  }
}

static gboolean homeworlds_position_count_pyramid(HomeworldsPyramid pyramid, guint counts[13]) {
  g_return_val_if_fail(counts != NULL, FALSE);

  if (homeworlds_pyramid_is_unused(pyramid)) {
    return TRUE;
  }
  if (!homeworlds_pyramid_is_valid(pyramid)) {
    return FALSE;
  }

  counts[pyramid]++;
  return TRUE;
}

static gboolean homeworlds_position_pyramid_counts_equal(const guint left[13], const guint right[13]) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  for (guint i = 0; i < 13; ++i) {
    if (left[i] != right[i]) {
      return FALSE;
    }
  }
  return TRUE;
}

static gboolean homeworlds_position_bank_contents_equal(const HomeworldsPosition *left,
                                                        const HomeworldsPosition *right) {
  guint left_counts[13] = {0};
  guint right_counts[13] = {0};

  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  for (guint i = 0; i < HOMEWORLDS_BANK_SLOT_COUNT; ++i) {
    if (!homeworlds_position_count_pyramid(left->bank[i], left_counts) ||
        !homeworlds_position_count_pyramid(right->bank[i], right_counts)) {
      return FALSE;
    }
  }
  return homeworlds_position_pyramid_counts_equal(left_counts, right_counts);
}

static gboolean homeworlds_position_systems_equal(const HomeworldsSystem *left, const HomeworldsSystem *right) {
  guint left_stars[13] = {0};
  guint right_stars[13] = {0};

  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    if (!homeworlds_position_count_pyramid(left->stars[i], left_stars) ||
        !homeworlds_position_count_pyramid(right->stars[i], right_stars)) {
      return FALSE;
    }
  }
  if (!homeworlds_position_pyramid_counts_equal(left_stars, right_stars)) {
    return FALSE;
  }

  for (guint side = 0; side < 2; ++side) {
    guint left_ships[13] = {0};
    guint right_ships[13] = {0};

    for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
      HomeworldsPyramid left_ship = left->ships[side][slot];
      HomeworldsPyramid right_ship = right->ships[side][slot];

      if (!homeworlds_pyramid_is_valid(left_ship) && !homeworlds_pyramid_is_valid(right_ship)) {
        if (left_ship != 0 || right_ship != 0) {
          return FALSE;
        }
        break;
      }
      if (!homeworlds_position_count_pyramid(left_ship, left_ships) ||
          !homeworlds_position_count_pyramid(right_ship, right_ships)) {
        return FALSE;
      }
    }
    if (!homeworlds_position_pyramid_counts_equal(left_ships, right_ships)) {
      return FALSE;
    }
  }
  return TRUE;
}

gboolean homeworlds_positions_equal(const HomeworldsPosition *left, const HomeworldsPosition *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  if (left->phase != right->phase || left->turn != right->turn) {
    return FALSE;
  }
  if (!homeworlds_position_bank_contents_equal(left, right)) {
    return FALSE;
  }
  for (guint i = 0; i < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++i) {
    if (!homeworlds_position_systems_equal(&left->systems[i], &right->systems[i])) {
      return FALSE;
    }
  }
  return TRUE;
}

static void homeworlds_position_hash_byte(guint64 *hash, guint8 byte) {
  g_return_if_fail(hash != NULL);

  *hash ^= byte;
  *hash *= 1099511628211ULL;
}

static void homeworlds_position_hash_pyramid_counts(guint64 *hash, const guint counts[13]) {
  g_return_if_fail(hash != NULL);
  g_return_if_fail(counts != NULL);

  for (guint i = 1; i < 13; ++i) {
    homeworlds_position_hash_byte(hash, (guint8) counts[i]);
  }
}

static void homeworlds_position_hash_bank(guint64 *hash, const HomeworldsPosition *position) {
  guint counts[13] = {0};

  g_return_if_fail(hash != NULL);
  g_return_if_fail(position != NULL);

  for (guint i = 0; i < HOMEWORLDS_BANK_SLOT_COUNT; ++i) {
    if (!homeworlds_position_count_pyramid(position->bank[i], counts)) {
      homeworlds_position_hash_byte(hash, position->bank[i]);
    }
  }
  homeworlds_position_hash_pyramid_counts(hash, counts);
}

static void homeworlds_position_hash_system(guint64 *hash, const HomeworldsSystem *system) {
  guint star_counts[13] = {0};

  g_return_if_fail(hash != NULL);
  g_return_if_fail(system != NULL);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    if (!homeworlds_position_count_pyramid(system->stars[i], star_counts)) {
      homeworlds_position_hash_byte(hash, system->stars[i]);
    }
  }
  homeworlds_position_hash_pyramid_counts(hash, star_counts);

  for (guint side = 0; side < 2; ++side) {
    guint ship_counts[13] = {0};

    for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
      HomeworldsPyramid ship = system->ships[side][slot];

      if (!homeworlds_pyramid_is_valid(ship)) {
        if (ship != 0) {
          homeworlds_position_hash_byte(hash, ship);
        }
        break;
      }
      if (!homeworlds_position_count_pyramid(ship, ship_counts)) {
        homeworlds_position_hash_byte(hash, ship);
      }
    }
    homeworlds_position_hash_pyramid_counts(hash, ship_counts);
  }
}

guint64 homeworlds_position_hash(const HomeworldsPosition *position) {
  guint64 hash = 1469598103934665603ULL;

  g_return_val_if_fail(position != NULL, 0);

  homeworlds_position_hash_byte(&hash, position->phase);
  homeworlds_position_hash_byte(&hash, position->turn);
  homeworlds_position_hash_bank(&hash, position);
  for (guint i = 0; i < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++i) {
    homeworlds_position_hash_system(&hash, &position->systems[i]);
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
    case HOMEWORLDS_SYSTEM_REF_SYSTEM:
      if (ref->system_index < 2 || ref->system_index >= HOMEWORLDS_SYSTEM_SLOT_COUNT) {
        return FALSE;
      }
      g_string_append_printf(text, "S%u", (guint)ref->system_index - 2);
      return TRUE;
    case HOMEWORLDS_SYSTEM_REF_NONE:
    default:
      return FALSE;
  }
}

static gboolean homeworlds_move_parse_system_number(const char **cursor, guint *out_system_index) {
  guint64 value = 0;

  g_return_val_if_fail(cursor != NULL, FALSE);
  g_return_val_if_fail(*cursor != NULL, FALSE);
  g_return_val_if_fail(out_system_index != NULL, FALSE);

  if (!g_ascii_isdigit(**cursor)) {
    return FALSE;
  }

  while (g_ascii_isdigit(**cursor)) {
    value = (value * 10) + (guint64)(**cursor - '0');
    if (value > HOMEWORLDS_SYSTEM_SLOT_COUNT - 3) {
      return FALSE;
    }
    (*cursor)++;
  }

  *out_system_index = (guint)value + 2;
  return TRUE;
}

static gboolean homeworlds_move_parse_system_ref(const char **cursor, HomeworldsSystemRef *out_ref) {
  guint system_index = HOMEWORLDS_INVALID_INDEX;

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

  if ((*cursor)[0] != 'S') {
    return FALSE;
  }
  (*cursor)++;

  if (!homeworlds_move_parse_system_number(cursor, &system_index)) {
    memset(out_ref, 0, sizeof(*out_ref));
    return FALSE;
  }

  out_ref->kind = HOMEWORLDS_SYSTEM_REF_SYSTEM;
  out_ref->system_index = (guint8)system_index;
  return TRUE;
}

static gboolean homeworlds_move_system_ref_is_discovery_target(const HomeworldsSystemRef *ref) {
  g_return_val_if_fail(ref != NULL, FALSE);

  return ref->kind == HOMEWORLDS_SYSTEM_REF_SYSTEM &&
         ref->system_index >= 2 &&
         ref->system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT &&
         homeworlds_pyramid_is_valid(ref->star);
}

static void homeworlds_move_skip_turn_step_separators(const char **cursor) {
  g_return_if_fail(cursor != NULL);
  g_return_if_fail(*cursor != NULL);

  while (g_ascii_isspace(**cursor)) {
    (*cursor)++;
  }
}

static gboolean homeworlds_move_at_separator(char character) {
  return character == '\0' || g_ascii_isspace(character);
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
      if (!homeworlds_move_append_pyramid(text, step->actor.ship, FALSE)) {
        return FALSE;
      }
      g_string_append_c(text, '>');
      return homeworlds_move_append_system_ref(text, &step->target_system);
    case HOMEWORLDS_STEP_DISCOVER:
      if (!homeworlds_move_append_pyramid(text, step->actor.ship, FALSE)) {
        return FALSE;
      }
      if (!homeworlds_move_system_ref_is_discovery_target(&step->target_system)) {
        return FALSE;
      }
      g_string_append_c(text, '>');
      if (!homeworlds_move_append_system_ref(text, &step->target_system)) {
        return FALSE;
      }
      g_string_append_c(text, '(');
      if (!homeworlds_move_append_pyramid(text, step->target_system.star, TRUE)) {
        return FALSE;
      }
      g_string_append_c(text, ')');
      return TRUE;
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

static gboolean homeworlds_system_refs_equal(const HomeworldsSystemRef *left, const HomeworldsSystemRef *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  if (left->kind != right->kind) {
    return FALSE;
  }

  switch ((HomeworldsSystemRefKind)left->kind) {
    case HOMEWORLDS_SYSTEM_REF_HOMEWORLD:
      return left->homeworld_side == right->homeworld_side;
    case HOMEWORLDS_SYSTEM_REF_SYSTEM:
      return left->system_index == right->system_index && left->star == right->star;
    case HOMEWORLDS_SYSTEM_REF_NONE:
    default:
      return TRUE;
  }
}

static gboolean homeworlds_ship_refs_equal(const HomeworldsShipRef *left, const HomeworldsShipRef *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  return left->ship == right->ship && homeworlds_system_refs_equal(&left->system, &right->system);
}

static gboolean homeworlds_turn_steps_equal(const HomeworldsTurnStep *left, const HomeworldsTurnStep *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  if (left->kind != right->kind) {
    return FALSE;
  }

  switch ((HomeworldsStepKind)left->kind) {
    case HOMEWORLDS_STEP_PASS:
      return TRUE;
    case HOMEWORLDS_STEP_CATASTROPHE:
      return left->target_color == right->target_color &&
             homeworlds_system_refs_equal(&left->target_system, &right->target_system);
    case HOMEWORLDS_STEP_BUILD:
      return left->target_color == right->target_color &&
             homeworlds_system_refs_equal(&left->actor.system, &right->actor.system);
    case HOMEWORLDS_STEP_TRADE:
      return left->target_color == right->target_color && homeworlds_ship_refs_equal(&left->actor, &right->actor);
    case HOMEWORLDS_STEP_ATTACK:
      return left->target_ship.ship == right->target_ship.ship &&
             homeworlds_ship_refs_equal(&left->actor, &right->actor);
    case HOMEWORLDS_STEP_MOVE:
    case HOMEWORLDS_STEP_DISCOVER:
      return homeworlds_ship_refs_equal(&left->actor, &right->actor) &&
             homeworlds_system_refs_equal(&left->target_system, &right->target_system);
    case HOMEWORLDS_STEP_SACRIFICE:
      return homeworlds_ship_refs_equal(&left->actor, &right->actor);
    case HOMEWORLDS_STEP_NONE:
    default:
      return TRUE;
  }
}

static void homeworlds_move_hash_byte(guint64 *hash, guint8 byte) {
  g_return_if_fail(hash != NULL);

  *hash ^= byte;
  *hash *= 1099511628211ULL;
}

static void homeworlds_move_hash_system_ref(guint64 *hash, const HomeworldsSystemRef *ref) {
  g_return_if_fail(hash != NULL);
  g_return_if_fail(ref != NULL);

  homeworlds_move_hash_byte(hash, ref->kind);
  switch ((HomeworldsSystemRefKind)ref->kind) {
    case HOMEWORLDS_SYSTEM_REF_HOMEWORLD:
      homeworlds_move_hash_byte(hash, ref->homeworld_side);
      break;
    case HOMEWORLDS_SYSTEM_REF_SYSTEM:
      homeworlds_move_hash_byte(hash, ref->system_index);
      homeworlds_move_hash_byte(hash, ref->star);
      break;
    case HOMEWORLDS_SYSTEM_REF_NONE:
    default:
      break;
  }
}

static void homeworlds_move_hash_ship_ref(guint64 *hash, const HomeworldsShipRef *ref) {
  g_return_if_fail(hash != NULL);
  g_return_if_fail(ref != NULL);

  homeworlds_move_hash_system_ref(hash, &ref->system);
  homeworlds_move_hash_byte(hash, ref->ship);
}

static void homeworlds_move_hash_turn_step(guint64 *hash, const HomeworldsTurnStep *step) {
  g_return_if_fail(hash != NULL);
  g_return_if_fail(step != NULL);

  homeworlds_move_hash_byte(hash, step->kind);
  switch ((HomeworldsStepKind)step->kind) {
    case HOMEWORLDS_STEP_PASS:
      break;
    case HOMEWORLDS_STEP_CATASTROPHE:
      homeworlds_move_hash_system_ref(hash, &step->target_system);
      homeworlds_move_hash_byte(hash, step->target_color);
      break;
    case HOMEWORLDS_STEP_BUILD:
      homeworlds_move_hash_system_ref(hash, &step->actor.system);
      homeworlds_move_hash_byte(hash, step->target_color);
      break;
    case HOMEWORLDS_STEP_TRADE:
      homeworlds_move_hash_ship_ref(hash, &step->actor);
      homeworlds_move_hash_byte(hash, step->target_color);
      break;
    case HOMEWORLDS_STEP_ATTACK:
      homeworlds_move_hash_ship_ref(hash, &step->actor);
      homeworlds_move_hash_byte(hash, step->target_ship.ship);
      break;
    case HOMEWORLDS_STEP_MOVE:
    case HOMEWORLDS_STEP_DISCOVER:
      homeworlds_move_hash_ship_ref(hash, &step->actor);
      homeworlds_move_hash_system_ref(hash, &step->target_system);
      break;
    case HOMEWORLDS_STEP_SACRIFICE:
      homeworlds_move_hash_ship_ref(hash, &step->actor);
      break;
    case HOMEWORLDS_STEP_NONE:
    default:
      break;
  }
}

gboolean homeworlds_moves_equal(const HomeworldsMove *left, const HomeworldsMove *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  if (left->kind != right->kind) {
    return FALSE;
  }

  switch ((HomeworldsMoveKind)left->kind) {
    case HOMEWORLDS_MOVE_KIND_SETUP:
      return left->setup_stars[0] == right->setup_stars[0] &&
             left->setup_stars[1] == right->setup_stars[1] &&
             left->setup_ship == right->setup_ship;
    case HOMEWORLDS_MOVE_KIND_TURN:
      if (left->step_count != right->step_count || left->step_count > HOMEWORLDS_MAX_MOVE_STEPS) {
        return FALSE;
      }
      for (guint i = 0; i < left->step_count; ++i) {
        if (!homeworlds_turn_steps_equal(&left->steps[i], &right->steps[i])) {
          return FALSE;
        }
      }
      return TRUE;
    case HOMEWORLDS_MOVE_KIND_NONE:
    default:
      return TRUE;
  }
}

guint homeworlds_move_hash(gconstpointer value) {
  const HomeworldsMove *move = value;
  guint64 hash = 1469598103934665603ULL;

  g_return_val_if_fail(move != NULL, 0);

  homeworlds_move_hash_byte(&hash, move->kind);
  switch ((HomeworldsMoveKind)move->kind) {
    case HOMEWORLDS_MOVE_KIND_SETUP:
      for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
        homeworlds_move_hash_byte(&hash, move->setup_stars[i]);
      }
      homeworlds_move_hash_byte(&hash, move->setup_ship);
      break;
    case HOMEWORLDS_MOVE_KIND_TURN:
      homeworlds_move_hash_byte(&hash, move->step_count);
      for (guint i = 0; i < move->step_count && i < HOMEWORLDS_MAX_MOVE_STEPS; ++i) {
        homeworlds_move_hash_turn_step(&hash, &move->steps[i]);
      }
      break;
    case HOMEWORLDS_MOVE_KIND_NONE:
    default:
      break;
  }

  return (guint)(hash ^ (hash >> 32));
}

void homeworlds_move_list_free(GameBackendMoveList *moves) {
  g_return_if_fail(moves != NULL);

  g_clear_pointer(&moves->moves, g_free);
  moves->count = 0;
}

const HomeworldsMove *homeworlds_move_list_get(const GameBackendMoveList *moves, gsize index) {
  g_return_val_if_fail(moves != NULL, NULL);
  g_return_val_if_fail(index < moves->count, NULL);
  g_return_val_if_fail(moves->moves != NULL, NULL);

  return ((const HomeworldsMove *)moves->moves) + index;
}

static void homeworlds_move_list_builder_clear(HomeworldsMoveListBuilder *builder) {
  g_return_if_fail(builder != NULL);

  g_clear_pointer(&builder->moves, g_free);
  builder->count = 0;
  builder->capacity = 0;
}

static gboolean homeworlds_move_list_builder_append(HomeworldsMoveListBuilder *builder, const HomeworldsMove *move) {
  g_return_val_if_fail(builder != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  if (builder->count == builder->capacity) {
    gsize next_capacity = builder->capacity == 0 ? 16 : builder->capacity * 2;
    HomeworldsMove *next_moves = g_realloc_n(builder->moves, next_capacity, sizeof(*next_moves));
    g_return_val_if_fail(next_moves != NULL, FALSE);
    builder->moves = next_moves;
    builder->capacity = next_capacity;
  }

  builder->moves[builder->count++] = *move;
  return TRUE;
}

static gboolean homeworlds_move_list_builder_collect(gconstpointer move_data, gpointer user_data) {
  HomeworldsMoveListBuilder *builder = user_data;
  const HomeworldsMove *move = move_data;

  g_return_val_if_fail(builder != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  return homeworlds_move_list_builder_append(builder, move);
}

static guint homeworlds_collect_catastrophe_choices(const HomeworldsMoveBuilderState *state,
                                                    HomeworldsCatastropheChoice *out_choices,
                                                    guint max_choices) {
  guint count = 0;

  g_return_val_if_fail(state != NULL, 0);
  g_return_val_if_fail(out_choices != NULL || max_choices == 0, 0);

  if (state->working_position.phase != HOMEWORLDS_PHASE_PLAY ||
      state->move.step_count >= HOMEWORLDS_MAX_MOVE_STEPS ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP) {
    return 0;
  }

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &state->working_position.systems[system_index];

    for (guint color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
      if (homeworlds_system_color_count(system, (HomeworldsColor) color) < 4) {
        continue;
      }
      if (count < max_choices) {
        out_choices[count] = (HomeworldsCatastropheChoice){
          .system_index = system_index,
          .color = (HomeworldsColor)color,
        };
      }
      count++;
    }
  }

  return MIN(count, max_choices);
}

static gboolean homeworlds_apply_catastrophe_choice(HomeworldsMoveBuilderState *state,
                                                    const HomeworldsCatastropheChoice *choice) {
  GameBackendMoveBuilder builder = {0};

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(choice != NULL, FALSE);
  g_return_val_if_fail(choice->system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);

  builder.builder_state = state;
  builder.builder_state_size = sizeof(*state);
  return homeworlds_move_builder_apply_catastrophe_step(&builder, choice->system_index, choice->color);
}

static gboolean homeworlds_collect_all_moves_recursive(const HomeworldsMoveBuilderState *state,
                                                       const HomeworldsGenerationContext *context,
                                                       HomeworldsMoveCollectorFunc collect_func,
                                                       gpointer user_data) {
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  HomeworldsCatastropheChoice catastrophes[HOMEWORLDS_SYSTEM_SLOT_COUNT * 4] = {0};
  guint catastrophe_count = 0;
  gboolean duplicate = FALSE;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(collect_func != NULL, FALSE);

  if (!homeworlds_generation_visit_state(context, state, &duplicate)) {
    return FALSE;
  }
  if (duplicate) {
    return TRUE;
  }

  builder.builder_state = (gpointer)state;
  builder.builder_state_size = sizeof(*state);
  if (state->move.step_count >= HOMEWORLDS_MAX_MOVE_STEPS &&
      !homeworlds_move_builder_is_complete(&builder)) {
    return TRUE;
  }

  if (homeworlds_move_builder_is_complete(&builder)) {
    HomeworldsMove move = {0};

    if (!homeworlds_move_builder_build_move(&builder, &move) ||
        !collect_func(&move, user_data)) {
      return FALSE;
    }
  }

  catastrophe_count = homeworlds_collect_catastrophe_choices(state, catastrophes, G_N_ELEMENTS(catastrophes));
  for (guint i = 0; i < catastrophe_count; ++i) {
    HomeworldsMoveBuilderState child_state = *state;
    HomeworldsGenerationContext child_context = {0};
    HomeworldsGenerationDedupe child_dedupe = {0};
    gboolean prune_child = FALSE;

    if (!homeworlds_apply_catastrophe_choice(&child_state, &catastrophes[i])) {
      continue;
    }
    if (!homeworlds_generation_prepare_child_context(context,
                                                     state,
                                                     &child_state,
                                                     &child_context,
                                                     &child_dedupe,
                                                     &prune_child)) {
      return FALSE;
    }
    if (!prune_child &&
        !homeworlds_collect_all_moves_recursive(&child_state, &child_context, collect_func, user_data)) {
      homeworlds_generation_dedupe_clear(&child_dedupe);
      return FALSE;
    }
    homeworlds_generation_dedupe_clear(&child_dedupe);
  }

  if (homeworlds_move_builder_is_complete(&builder)) {
    return TRUE;
  }

  candidates = homeworlds_move_builder_list_candidates(&builder);
  for (guint pass = 0; pass < 2; ++pass) {
    gboolean want_pass = pass == 0;

    for (gsize i = 0; i < candidates.count; ++i) {
      const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *)candidates.moves)[i];
      HomeworldsMoveBuilderState child_state = *state;
      HomeworldsGenerationContext child_context = {0};
      HomeworldsGenerationDedupe child_dedupe = {0};
      GameBackendMoveBuilder child = {
        .builder_state = &child_state,
        .builder_state_size = sizeof(child_state),
      };
      gboolean is_pass = candidate->data.kind == HOMEWORLDS_CANDIDATE_ACTION &&
                         candidate->data.target_color == HOMEWORLDS_STEP_PASS;
      gboolean prune_child = FALSE;

      if (is_pass != want_pass) {
        continue;
      }

      if (!homeworlds_move_builder_step(&child, candidate)) {
        continue;
      }
      if (!homeworlds_generation_prepare_child_context(context,
                                                       state,
                                                       &child_state,
                                                       &child_context,
                                                       &child_dedupe,
                                                       &prune_child)) {
        g_free(candidates.moves);
        return FALSE;
      }
      if (!prune_child &&
          !homeworlds_collect_all_moves_recursive(&child_state, &child_context, collect_func, user_data)) {
        homeworlds_generation_dedupe_clear(&child_dedupe);
        g_free(candidates.moves);
        return FALSE;
      }
      homeworlds_generation_dedupe_clear(&child_dedupe);
    }
  }

  g_free(candidates.moves);
  return TRUE;
}

GameBackendMoveList homeworlds_position_list_all_moves(const HomeworldsPosition *position) {
  GameBackendMoveBuilder builder = {0};
  HomeworldsGenerationContext context = {0};
  HomeworldsMoveListBuilder move_list = {0};

  g_return_val_if_fail(position != NULL, (GameBackendMoveList){0});

  if (!homeworlds_move_builder_init(position, &builder)) {
    return (GameBackendMoveList){0};
  }
  homeworlds_generation_context_init(&context);
  if (!homeworlds_collect_all_moves_recursive(builder.builder_state,
                                             &context,
                                             homeworlds_move_list_builder_collect,
                                             &move_list)) {
    homeworlds_move_builder_clear(&builder);
    homeworlds_move_list_builder_clear(&move_list);
    return (GameBackendMoveList){0};
  }

  homeworlds_move_builder_clear(&builder);
  return (GameBackendMoveList){
    .moves = move_list.moves,
    .count = move_list.count,
  };
}

gboolean homeworlds_position_stream_all_moves(const HomeworldsPosition *position,
                                              GameBackendMoveStreamFunc stream_func,
                                              gpointer user_data) {
  GameBackendMoveBuilder builder = {0};
  HomeworldsGenerationContext context = {0};
  gboolean ok = FALSE;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(stream_func != NULL, FALSE);

  if (!homeworlds_move_builder_init(position, &builder)) {
    return FALSE;
  }
  homeworlds_generation_context_init(&context);
  ok = homeworlds_collect_all_moves_recursive(builder.builder_state, &context, stream_func, user_data);
  homeworlds_move_builder_clear(&builder);
  return ok;
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

  switch (**cursor) {
    case '=':
      (*cursor)++;
      if (!homeworlds_move_color_from_letter(**cursor, FALSE, &color)) {
        return FALSE;
      }
      out_step->kind = HOMEWORLDS_STEP_TRADE;
      out_step->target_color = color;
      (*cursor)++;
      return TRUE;
    case 'x':
      (*cursor)++;
      out_step->kind = HOMEWORLDS_STEP_ATTACK;
      out_step->target_ship.system = out_step->actor.system;
      return homeworlds_move_parse_pyramid(cursor, FALSE, &out_step->target_ship.ship);
    case '>':
      (*cursor)++;
      out_step->kind = HOMEWORLDS_STEP_MOVE;
      if (!homeworlds_move_parse_system_ref(cursor, &out_step->target_system)) {
        return FALSE;
      }
      if (**cursor != '(') {
        return TRUE;
      }
      if (out_step->target_system.kind != HOMEWORLDS_SYSTEM_REF_SYSTEM) {
        return FALSE;
      }
      (*cursor)++;
      if (!homeworlds_move_parse_pyramid(cursor, TRUE, &out_step->target_system.star) ||
          **cursor != ')') {
        return FALSE;
      }
      (*cursor)++;
      if (!homeworlds_move_system_ref_is_discovery_target(&out_step->target_system)) {
        return FALSE;
      }
      out_step->kind = HOMEWORLDS_STEP_DISCOVER;
      return TRUE;
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

  HomeworldsMove parsed = {0};

  if (notation[0] == '\0') {
    return FALSE;
  }
  if (g_ascii_isupper(notation[0]) && notation[0] != 'H') {
    if (homeworlds_move_parse_setup(notation, &parsed)) {
      *out_move = parsed;
      return TRUE;
    }
  }

  memset(&parsed, 0, sizeof(parsed));
  if (!homeworlds_move_parse_turn(notation, &parsed)) {
    return FALSE;
  }

  *out_move = parsed;
  return TRUE;
}
