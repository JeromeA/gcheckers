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

void homeworlds_position_finish_turn(HomeworldsPosition *position) {
  g_return_if_fail(position != NULL);

  if (position->phase != HOMEWORLDS_PHASE_PLAY) {
    return;
  }

  position->turn = position->turn == 0 ? 1 : 0;
  if (homeworlds_system_ship_count_for_side(&position->systems[position->turn], position->turn) == 0) {
    position->phase = HOMEWORLDS_PHASE_FINISHED;
  }
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
  if (homeworlds_pyramid_size(move->setup_ship) != HOMEWORLDS_SIZE_LARGE) {
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

static gboolean homeworlds_position_apply_construct(HomeworldsPosition *position, const HomeworldsTurnStep *step) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->system_index >= HOMEWORLDS_SYSTEM_SLOT_COUNT || step->ship_owner != position->turn ||
      step->ship_slot >= HOMEWORLDS_SHIP_SLOT_COUNT) {
    return FALSE;
  }

  HomeworldsSystem *system = &position->systems[step->system_index];
  HomeworldsPyramid source = system->ships[step->ship_owner][step->ship_slot];
  HomeworldsPyramid built = 0;

  if (!homeworlds_pyramid_is_valid(source)) {
    return FALSE;
  }
  if (!homeworlds_system_has_access_to_color(system, position->turn, HOMEWORLDS_COLOR_GREEN)) {
    return FALSE;
  }
  if (!homeworlds_system_find_smallest_bank_ship(position, homeworlds_pyramid_color(source), &built)) {
    return FALSE;
  }
  if (!homeworlds_bank_take(position, built)) {
    return FALSE;
  }

  return homeworlds_system_add_ship(system, position->turn, built);
}

static gboolean homeworlds_position_apply_trade(HomeworldsPosition *position, const HomeworldsTurnStep *step) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->system_index >= HOMEWORLDS_SYSTEM_SLOT_COUNT || step->ship_owner != position->turn ||
      step->ship_slot >= HOMEWORLDS_SHIP_SLOT_COUNT || step->target_color > HOMEWORLDS_COLOR_BLUE) {
    return FALSE;
  }

  HomeworldsSystem *system = &position->systems[step->system_index];
  HomeworldsPyramid source = system->ships[step->ship_owner][step->ship_slot];

  if (!homeworlds_pyramid_is_valid(source)) {
    return FALSE;
  }
  if (!homeworlds_system_has_access_to_color(system, position->turn, HOMEWORLDS_COLOR_BLUE)) {
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
  system->ships[step->ship_owner][step->ship_slot] = traded;
  return TRUE;
}

static gboolean homeworlds_position_apply_attack(HomeworldsPosition *position, const HomeworldsTurnStep *step) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->system_index >= HOMEWORLDS_SYSTEM_SLOT_COUNT || step->ship_owner != position->turn ||
      step->ship_slot >= HOMEWORLDS_SHIP_SLOT_COUNT || step->target_ship_owner == position->turn ||
      step->target_ship_owner >= 2 || step->target_ship_slot >= HOMEWORLDS_SHIP_SLOT_COUNT) {
    return FALSE;
  }

  HomeworldsSystem *system = &position->systems[step->system_index];
  HomeworldsPyramid attacker = system->ships[step->ship_owner][step->ship_slot];
  HomeworldsPyramid target = system->ships[step->target_ship_owner][step->target_ship_slot];

  if (!homeworlds_pyramid_is_valid(attacker) || !homeworlds_pyramid_is_valid(target)) {
    return FALSE;
  }
  if (!homeworlds_system_has_access_to_color(system, position->turn, HOMEWORLDS_COLOR_RED)) {
    return FALSE;
  }
  if (homeworlds_pyramid_size(attacker) < homeworlds_pyramid_size(target)) {
    return FALSE;
  }

  system->ships[step->target_ship_owner][step->target_ship_slot] = 0;
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

gboolean homeworlds_position_apply_turn_step(HomeworldsPosition *position, const HomeworldsTurnStep *step) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  switch ((HomeworldsStepKind) step->kind) {
    case HOMEWORLDS_STEP_PASS:
      return TRUE;
    case HOMEWORLDS_STEP_CONSTRUCT:
      return homeworlds_position_apply_construct(position, step);
    case HOMEWORLDS_STEP_TRADE:
      return homeworlds_position_apply_trade(position, step);
    case HOMEWORLDS_STEP_ATTACK:
      return homeworlds_position_apply_attack(position, step);
    case HOMEWORLDS_STEP_MOVE: {
      if (step->system_index >= HOMEWORLDS_SYSTEM_SLOT_COUNT ||
          step->target_system_index >= HOMEWORLDS_SYSTEM_SLOT_COUNT || step->ship_owner != position->turn ||
          step->ship_slot >= HOMEWORLDS_SHIP_SLOT_COUNT) {
        return FALSE;
      }

      HomeworldsSystem *from_system = &position->systems[step->system_index];
      HomeworldsSystem *to_system = &position->systems[step->target_system_index];

      if (!homeworlds_system_has_access_to_color(from_system, position->turn, HOMEWORLDS_COLOR_YELLOW)) {
        return FALSE;
      }
      if (!homeworlds_system_is_connected(from_system, to_system)) {
        return FALSE;
      }

      return homeworlds_position_move_ship_between_systems(position,
                                                           step->system_index,
                                                           step->ship_slot,
                                                           step->target_system_index);
    }
    case HOMEWORLDS_STEP_DISCOVER: {
      if (step->system_index >= HOMEWORLDS_SYSTEM_SLOT_COUNT || step->ship_owner != position->turn ||
          step->ship_slot >= HOMEWORLDS_SHIP_SLOT_COUNT || !homeworlds_pyramid_is_valid(step->pyramid)) {
        return FALSE;
      }

      guint target_system_index = 0;
      if (!homeworlds_position_find_empty_system(position, &target_system_index)) {
        return FALSE;
      }
      if (!homeworlds_system_has_access_to_color(&position->systems[step->system_index],
                                                 position->turn,
                                                 HOMEWORLDS_COLOR_YELLOW)) {
        return FALSE;
      }
      if (!homeworlds_bank_take(position, step->pyramid)) {
        return FALSE;
      }

      HomeworldsSystem *target_system = &position->systems[target_system_index];
      if (!homeworlds_system_add_star(target_system, step->pyramid)) {
        homeworlds_bank_put(position, step->pyramid);
        return FALSE;
      }
      if (!homeworlds_system_is_connected(&position->systems[step->system_index], target_system)) {
        target_system->stars[0] = 0;
        homeworlds_bank_put(position, step->pyramid);
        return FALSE;
      }
      if (!homeworlds_position_move_ship_between_systems(position,
                                                         step->system_index,
                                                         step->ship_slot,
                                                         target_system_index)) {
        target_system->stars[0] = 0;
        homeworlds_bank_put(position, step->pyramid);
        return FALSE;
      }

      return TRUE;
    }
    case HOMEWORLDS_STEP_SACRIFICE: {
      if (step->system_index >= HOMEWORLDS_SYSTEM_SLOT_COUNT || step->ship_owner != position->turn ||
          step->ship_slot >= HOMEWORLDS_SHIP_SLOT_COUNT) {
        return FALSE;
      }

      HomeworldsPyramid ship = 0;
      if (!homeworlds_system_remove_ship(&position->systems[step->system_index], position->turn, step->ship_slot, &ship)) {
        return FALSE;
      }

      homeworlds_bank_put(position, ship);
      homeworlds_system_cleanup_orphaned_stars(position, step->system_index);
      return TRUE;
    }
    case HOMEWORLDS_STEP_CATASTROPHE:
      if (step->system_index >= HOMEWORLDS_SYSTEM_SLOT_COUNT || step->target_color > HOMEWORLDS_COLOR_BLUE) {
        return FALSE;
      }
      return homeworlds_position_apply_catastrophe(position, step->system_index, (HomeworldsColor) step->target_color);
    case HOMEWORLDS_STEP_NONE:
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

  if (move->acting_side != position->turn) {
    return FALSE;
  }

  if (move->kind == HOMEWORLDS_MOVE_KIND_SETUP) {
    return homeworlds_position_apply_setup_move(position, move);
  }
  if (position->phase != HOMEWORLDS_PHASE_PLAY || move->kind != HOMEWORLDS_MOVE_KIND_TURN || move->step_count == 0) {
    return FALSE;
  }

  guint pending_sacrifice_actions = 0;
  HomeworldsColor sacrifice_color = HOMEWORLDS_COLOR_RED;

  for (guint i = 0; i < move->step_count; ++i) {
    const HomeworldsTurnStep *step = &move->steps[i];

    if (step->kind == HOMEWORLDS_STEP_SACRIFICE) {
      if (i != 0 || pending_sacrifice_actions != 0) {
        return FALSE;
      }

      HomeworldsPyramid ship = position->systems[step->system_index].ships[position->turn][step->ship_slot];
      if (!homeworlds_pyramid_is_valid(ship)) {
        return FALSE;
      }

      pending_sacrifice_actions = homeworlds_pyramid_size(ship);
      sacrifice_color = homeworlds_pyramid_color(ship);
    } else if (pending_sacrifice_actions == 0 && i > 0) {
      return FALSE;
    } else if (pending_sacrifice_actions > 0) {
      HomeworldsColor action_color = HOMEWORLDS_COLOR_RED;

      switch ((HomeworldsStepKind) step->kind) {
        case HOMEWORLDS_STEP_CONSTRUCT:
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
    }

    if (!homeworlds_position_apply_turn_step(position, step)) {
      return FALSE;
    }
  }

  if (pending_sacrifice_actions > 0) {
    return FALSE;
  }

  homeworlds_position_finish_turn(position);
  return TRUE;
}

gboolean homeworlds_position_apply_catastrophe(HomeworldsPosition *position, guint system_index, HomeworldsColor color) {
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

  score += (gint) homeworlds_system_ship_count_for_side(&position->systems[0], 0) * 15;
  score -= (gint) homeworlds_system_ship_count_for_side(&position->systems[1], 1) * 15;
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

gboolean homeworlds_move_format(const HomeworldsMove *move, char *buffer, gsize size) {
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(size > 0, FALSE);

  if (move->kind == HOMEWORLDS_MOVE_KIND_SETUP) {
    return g_snprintf(buffer,
                      size,
                      "setup %u+%u ship %u",
                      (guint) move->setup_stars[0],
                      (guint) move->setup_stars[1],
                      (guint) move->setup_ship) < (gint) size;
  }

  if (move->step_count == 0) {
    return g_snprintf(buffer, size, "empty") < (gint) size;
  }

  const HomeworldsTurnStep *first = &move->steps[0];
  switch ((HomeworldsStepKind) first->kind) {
    case HOMEWORLDS_STEP_PASS:
      return g_snprintf(buffer, size, "pass") < (gint) size;
    case HOMEWORLDS_STEP_CONSTRUCT:
      return g_snprintf(buffer, size, "construct S%u", (guint) first->system_index) < (gint) size;
    case HOMEWORLDS_STEP_TRADE:
      return g_snprintf(buffer,
                        size,
                        "trade S%u C%u",
                        (guint) first->system_index,
                        (guint) first->target_color) < (gint) size;
    case HOMEWORLDS_STEP_ATTACK:
      return g_snprintf(buffer, size, "attack S%u", (guint) first->system_index) < (gint) size;
    case HOMEWORLDS_STEP_MOVE:
      return g_snprintf(buffer,
                        size,
                        "move S%u->S%u",
                        (guint) first->system_index,
                        (guint) first->target_system_index) < (gint) size;
    case HOMEWORLDS_STEP_DISCOVER:
      return g_snprintf(buffer,
                        size,
                        "discover S%u P%u",
                        (guint) first->system_index,
                        (guint) first->pyramid) < (gint) size;
    case HOMEWORLDS_STEP_SACRIFICE:
      return g_snprintf(buffer, size, "sacrifice S%u", (guint) first->system_index) < (gint) size;
    case HOMEWORLDS_STEP_CATASTROPHE:
      return g_snprintf(buffer,
                        size,
                        "catastrophe S%u C%u",
                        (guint) first->system_index,
                        (guint) first->target_color) < (gint) size;
    case HOMEWORLDS_STEP_NONE:
    default:
      return g_snprintf(buffer, size, "unknown") < (gint) size;
  }
}
