#include <assert.h>
#include <string.h>

#include "../src/games/homeworlds/homeworlds_game.h"
#include "../src/games/homeworlds/homeworlds_position_text.h"

static gboolean test_bank_remove(HomeworldsPosition *position, HomeworldsPyramid pyramid) {
  assert(position != NULL);
  assert(homeworlds_pyramid_is_valid(pyramid));

  for (guint i = 0; i < HOMEWORLDS_BANK_SLOT_COUNT; ++i) {
    if (position->bank[i] != pyramid) {
      continue;
    }

    position->bank[i] = 0;
    return TRUE;
  }

  return FALSE;
}

static gboolean test_system_add_ship(HomeworldsPosition *position,
                                     guint system_index,
                                     guint side,
                                     HomeworldsPyramid pyramid) {
  assert(position != NULL);
  assert(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT);
  assert(side < 2);
  assert(homeworlds_pyramid_is_valid(pyramid));

  for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    if (position->systems[system_index].ships[side][slot] != 0) {
      continue;
    }

    position->systems[system_index].ships[side][slot] = pyramid;
    return TRUE;
  }

  return FALSE;
}

static gboolean test_system_add_star(HomeworldsPosition *position, guint system_index, HomeworldsPyramid pyramid) {
  assert(position != NULL);
  assert(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT);
  assert(homeworlds_pyramid_is_valid(pyramid));

  for (guint slot = 0; slot < HOMEWORLDS_STAR_SLOT_COUNT; ++slot) {
    if (position->systems[system_index].stars[slot] != 0) {
      continue;
    }

    position->systems[system_index].stars[slot] = pyramid;
    return TRUE;
  }

  return FALSE;
}

static HomeworldsSystemRef test_homeworld_ref(guint side) {
  assert(side < 2);

  return (HomeworldsSystemRef){
    .kind = HOMEWORLDS_SYSTEM_REF_HOMEWORLD,
    .homeworld_side = side,
  };
}

static HomeworldsSystemRef test_star_ref(HomeworldsColor color, HomeworldsSize size, guint duplicate_index) {
  return (HomeworldsSystemRef){
    .kind = HOMEWORLDS_SYSTEM_REF_STAR,
    .duplicate_index = (guint8)duplicate_index,
    .star = homeworlds_pyramid_make(color, size),
  };
}

static HomeworldsShipRef test_ship_ref(HomeworldsSystemRef system, HomeworldsColor color, HomeworldsSize size) {
  return (HomeworldsShipRef){
    .system = system,
    .ship = homeworlds_pyramid_make(color, size),
  };
}

static HomeworldsMove test_setup_move(guint /*side*/,
                                      HomeworldsPyramid first_star,
                                      HomeworldsPyramid second_star,
                                      HomeworldsPyramid ship) {
  HomeworldsMove move = {0};

  move.kind = HOMEWORLDS_MOVE_KIND_SETUP;
  move.setup_stars[0] = first_star;
  move.setup_stars[1] = second_star;
  move.setup_ship = ship;
  return move;
}

static HomeworldsMove test_single_step_move(guint /*side*/, HomeworldsTurnStep step) {
  HomeworldsMove move = {0};

  move.kind = HOMEWORLDS_MOVE_KIND_TURN;
  move.step_count = 1;
  move.steps[0] = step;
  return move;
}

static void test_prepare_basic_position(HomeworldsPosition *position) {
  HomeworldsMove p0 = test_setup_move(0,
                                      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
                                      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
                                      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE));
  HomeworldsMove p1 = test_setup_move(1,
                                      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
                                      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
                                      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE));

  homeworlds_position_init(position);
  assert(homeworlds_position_apply_move(position, &p0));
  assert(homeworlds_position_apply_move(position, &p1));
  assert(position->phase == HOMEWORLDS_PHASE_PLAY);
  assert(position->turn == 0);
}

static void test_setup_and_loss_detection(void) {
  HomeworldsPosition position = {0};

  test_prepare_basic_position(&position);
  assert(homeworlds_system_ship_count_for_side(&position.systems[0], 0) == 1);
  assert(homeworlds_system_ship_count_for_side(&position.systems[1], 1) == 1);
  assert(homeworlds_position_outcome(&position) == GAME_BACKEND_OUTCOME_ONGOING);

  position.systems[1].ships[1][0] = 0;
  HomeworldsMove pass = test_single_step_move(0, (HomeworldsTurnStep){.kind = HOMEWORLDS_STEP_PASS});
  assert(homeworlds_position_apply_move(&position, &pass));
  assert(homeworlds_position_outcome(&position) == GAME_BACKEND_OUTCOME_SIDE_0_WIN);
}

static void test_setup_accepts_any_bank_pyramids(void) {
  HomeworldsPosition position = {0};
  HomeworldsPyramid large_star = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE);
  HomeworldsPyramid small_ship = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL);
  HomeworldsMove move = test_setup_move(0, large_star, large_star, small_ship);

  homeworlds_position_init(&position);
  assert(homeworlds_position_apply_move(&position, &move));
  assert(position.turn == 1);
  assert(position.systems[0].stars[0] == large_star);
  assert(position.systems[0].stars[1] == large_star);
  assert(position.systems[0].ships[0][0] == small_ship);
}

static void test_build_uses_smallest_available_ship(void) {
  HomeworldsPosition position = {0};
  HomeworldsMove move = {0};

  test_prepare_basic_position(&position);
  move = test_single_step_move(0,
                               (HomeworldsTurnStep){
                                   .kind = HOMEWORLDS_STEP_BUILD,
                                   .actor = {
                                     .system = test_homeworld_ref(0),
                                   },
                                   .target_color = HOMEWORLDS_COLOR_GREEN,
                               });
  assert(homeworlds_position_apply_move(&position, &move));
  assert(homeworlds_system_ship_count_for_side(&position.systems[0], 0) == 2);
  assert(position.systems[0].ships[0][1] == homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL));
}

static void test_invalid_multi_step_move_leaves_position_unchanged(void) {
  HomeworldsPosition position = {0};
  HomeworldsMove move = {0};

  test_prepare_basic_position(&position);
  HomeworldsPosition before = position;

  move.kind = HOMEWORLDS_MOVE_KIND_TURN;
  move.step_count = 2;
  move.steps[0] = (HomeworldsTurnStep){
    .kind = HOMEWORLDS_STEP_BUILD,
    .actor = {
      .system = test_homeworld_ref(0),
    },
    .target_color = HOMEWORLDS_COLOR_GREEN,
  };
  move.steps[1] = (HomeworldsTurnStep){
    .kind = HOMEWORLDS_STEP_BUILD,
    .actor = {
      .system = test_homeworld_ref(0),
    },
    .target_color = HOMEWORLDS_COLOR_GREEN,
  };

  assert(!homeworlds_position_apply_move(&position, &move));
  assert(memcmp(&position, &before, sizeof(position)) == 0);
}

static void test_failed_turn_step_leaves_position_unchanged(void) {
  HomeworldsPosition position = {0};
  HomeworldsTurnStep step = {
    .kind = HOMEWORLDS_STEP_BUILD,
    .actor = {
      .system = test_homeworld_ref(0),
    },
    .target_color = HOMEWORLDS_COLOR_GREEN,
  };

  test_prepare_basic_position(&position);
  for (guint slot = 1; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    position.systems[0].ships[0][slot] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL);
  }

  HomeworldsPosition before = position;
  assert(!homeworlds_position_apply_turn_step(&position, &step));
  assert(memcmp(&position, &before, sizeof(position)) == 0);
}

static void test_smallest_bank_ship_failure_clears_output(void) {
  HomeworldsPosition position = {0};
  HomeworldsPyramid found = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL);

  homeworlds_position_init(&position);
  for (guint i = 0; i < HOMEWORLDS_BANK_SLOT_COUNT; ++i) {
    if (homeworlds_pyramid_color(position.bank[i]) == HOMEWORLDS_COLOR_BLUE) {
      position.bank[i] = 0;
    }
  }

  assert(!homeworlds_system_find_smallest_bank_ship(&position, HOMEWORLDS_COLOR_BLUE, &found));
  assert(found == 0);
}

static void test_trade_preserves_size(void) {
  HomeworldsPosition position = {0};
  HomeworldsMove move = {0};

  test_prepare_basic_position(&position);
  move = test_single_step_move(0,
                               (HomeworldsTurnStep){
                                   .kind = HOMEWORLDS_STEP_TRADE,
                                   .actor = test_ship_ref(test_homeworld_ref(0),
                                                          HOMEWORLDS_COLOR_GREEN,
                                                          HOMEWORLDS_SIZE_LARGE),
                                   .target_color = HOMEWORLDS_COLOR_BLUE,
                               });
  assert(homeworlds_position_apply_move(&position, &move));
  assert(position.systems[0].ships[0][0] == homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE));
}

static void test_attack_requires_size_and_changes_owner(void) {
  HomeworldsPosition position = {0};
  HomeworldsMove move = {0};

  test_prepare_basic_position(&position);
  position.systems[1].ships[1][0] = 0;
  position.systems[0].ships[1][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL);

  move = test_single_step_move(0,
                               (HomeworldsTurnStep){
                                   .kind = HOMEWORLDS_STEP_ATTACK,
                                   .actor = test_ship_ref(test_homeworld_ref(0),
                                                          HOMEWORLDS_COLOR_GREEN,
                                                          HOMEWORLDS_SIZE_LARGE),
                                   .target_ship = test_ship_ref(test_homeworld_ref(0),
                                                                HOMEWORLDS_COLOR_YELLOW,
                                                                HOMEWORLDS_SIZE_SMALL),
                               });
  assert(homeworlds_position_apply_move(&position, &move));
  assert(homeworlds_system_ship_count_for_side(&position.systems[0], 1) == 0);
  assert(homeworlds_system_ship_count_for_side(&position.systems[0], 0) == 2);
}

static void test_move_and_discover_follow_connectivity(void) {
  HomeworldsPosition move_position = {0};
  HomeworldsPosition discover_position = {0};
  HomeworldsMove move = {0};

  test_prepare_basic_position(&move_position);
  move_position.systems[0].ships[0][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE);
  assert(test_system_add_star(&move_position,
                              2,
                              homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE)));
  assert(test_bank_remove(&move_position, homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE)));

  move = test_single_step_move(0,
                               (HomeworldsTurnStep){
                                   .kind = HOMEWORLDS_STEP_MOVE,
                                   .actor = test_ship_ref(test_homeworld_ref(0),
                                                          HOMEWORLDS_COLOR_YELLOW,
                                                          HOMEWORLDS_SIZE_LARGE),
                                   .target_system = test_star_ref(HOMEWORLDS_COLOR_GREEN,
                                                                  HOMEWORLDS_SIZE_LARGE,
                                                                  0),
                               });
  assert(homeworlds_position_apply_move(&move_position, &move));
  assert(homeworlds_system_ship_count_for_side(&move_position.systems[0], 0) == 0);
  assert(homeworlds_system_ship_count_for_side(&move_position.systems[2], 0) == 1);

  test_prepare_basic_position(&discover_position);
  discover_position.systems[0].ships[0][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE);
  move = test_single_step_move(0,
                               (HomeworldsTurnStep){
                                   .kind = HOMEWORLDS_STEP_MOVE,
                                   .actor = test_ship_ref(test_homeworld_ref(0),
                                                          HOMEWORLDS_COLOR_YELLOW,
                                                          HOMEWORLDS_SIZE_LARGE),
                                   .target_system = test_star_ref(HOMEWORLDS_COLOR_GREEN,
                                                                  HOMEWORLDS_SIZE_LARGE,
                                                                  0),
                               });
  assert(homeworlds_position_apply_move(&discover_position, &move));
  assert(homeworlds_system_ship_count_for_side(&discover_position.systems[2], 0) == 1);
  assert(discover_position.systems[2].stars[0] == homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN,
                                                                           HOMEWORLDS_SIZE_LARGE));
}

static void test_empty_system_lookup_failure_sets_invalid_index(void) {
  HomeworldsPosition position = {0};
  guint system_index = 2;

  homeworlds_position_init(&position);
  for (guint i = 2; i < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++i) {
    assert(test_system_add_star(&position, i, homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL)));
  }

  assert(!homeworlds_position_find_empty_system(&position, &system_index));
  assert(system_index == HOMEWORLDS_INVALID_INDEX);
}

static void test_system_ref_resolution_failure_sets_invalid_index(void) {
  HomeworldsPosition position = {0};
  HomeworldsSystemRef ref = test_star_ref(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE, 0);
  guint system_index = 0;

  homeworlds_position_init(&position);
  assert(!homeworlds_position_resolve_system_ref(&position, &ref, &system_index));
  assert(system_index == HOMEWORLDS_INVALID_INDEX);
}

static void test_sacrifice_grants_multiple_actions(void) {
  HomeworldsPosition position = {0};
  HomeworldsMove move = {0};

  test_prepare_basic_position(&position);
  position.systems[0].ships[0][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM);
  assert(test_system_add_ship(&position, 0, 0, homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE)));
  assert(test_bank_remove(&position, homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE)));

  move.kind = HOMEWORLDS_MOVE_KIND_TURN;
  move.step_count = 3;
  move.steps[0] = (HomeworldsTurnStep){
    .kind = HOMEWORLDS_STEP_SACRIFICE,
    .actor = test_ship_ref(test_homeworld_ref(0), HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
  };
  move.steps[1] = (HomeworldsTurnStep){
    .kind = HOMEWORLDS_STEP_TRADE,
    .actor = test_ship_ref(test_homeworld_ref(0), HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
    .target_color = HOMEWORLDS_COLOR_GREEN,
  };
  move.steps[2] = (HomeworldsTurnStep){
    .kind = HOMEWORLDS_STEP_TRADE,
    .actor = test_ship_ref(test_homeworld_ref(0), HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
    .target_color = HOMEWORLDS_COLOR_YELLOW,
  };

  assert(homeworlds_position_apply_move(&position, &move));
  assert(position.systems[0].ships[0][1] == homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE));
}

static void test_sacrifice_actions_ignore_local_color_access(void) {
  HomeworldsPosition position = {0};
  HomeworldsMove move = {0};
  HomeworldsPyramid blue_small = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL);
  HomeworldsPyramid red_small = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL);
  HomeworldsPyramid red_medium = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM);

  test_prepare_basic_position(&position);
  assert(test_system_add_ship(&position, 0, 0, blue_small));
  assert(test_bank_remove(&position, blue_small));
  assert(test_system_add_star(&position, 2, red_medium));
  assert(test_bank_remove(&position, red_medium));
  assert(test_system_add_ship(&position, 2, 0, red_small));
  assert(test_bank_remove(&position, red_small));

  move.kind = HOMEWORLDS_MOVE_KIND_TURN;
  move.step_count = 4;
  move.steps[0] = (HomeworldsTurnStep){
    .kind = HOMEWORLDS_STEP_SACRIFICE,
    .actor = test_ship_ref(test_homeworld_ref(0), HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
  };
  move.steps[1] = (HomeworldsTurnStep){
    .kind = HOMEWORLDS_STEP_BUILD,
    .actor = {
      .system = test_star_ref(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM, 0),
    },
    .target_color = HOMEWORLDS_COLOR_RED,
  };
  move.steps[2] = (HomeworldsTurnStep){
    .kind = HOMEWORLDS_STEP_PASS,
  };
  move.steps[3] = (HomeworldsTurnStep){
    .kind = HOMEWORLDS_STEP_PASS,
  };

  assert(homeworlds_position_apply_move(&position, &move));
  assert(homeworlds_system_ship_count_for_side(&position.systems[2], 0) == 2);
  assert(position.turn == 1);
}

static void test_catastrophe_removes_matching_color_and_collapses_star_system(void) {
  HomeworldsPosition position = {0};

  homeworlds_position_init(&position);
  position.phase = HOMEWORLDS_PHASE_PLAY;
  assert(test_system_add_star(&position, 2, homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL)));
  assert(test_system_add_star(&position, 2, homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM)));
  assert(test_bank_remove(&position, homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL)));
  assert(test_bank_remove(&position, homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM)));
  assert(test_system_add_ship(&position, 2, 0, homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL)));
  assert(test_system_add_ship(&position, 2, 0, homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM)));
  assert(test_system_add_ship(&position, 2, 1, homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE)));
  assert(test_bank_remove(&position, homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL)));
  assert(test_bank_remove(&position, homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM)));
  assert(test_bank_remove(&position, homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE)));

  assert(homeworlds_system_color_count(&position.systems[2], HOMEWORLDS_COLOR_BLUE) == 4);
  assert(homeworlds_position_apply_catastrophe(&position, 2, HOMEWORLDS_COLOR_BLUE));
  assert(position.systems[2].stars[0] == 0);
  assert(position.systems[2].ships[0][0] == 0);
  assert(position.systems[2].ships[1][0] == 0);
}

static void test_ship_catastrophe_returns_orphaned_stars_to_bank(void) {
  HomeworldsPosition position = {0};

  homeworlds_position_init(&position);
  position.phase = HOMEWORLDS_PHASE_PLAY;

  assert(test_system_add_star(&position, 2, homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL)));
  assert(test_bank_remove(&position, homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL)));
  assert(test_system_add_ship(&position, 2, 0, homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL)));
  assert(test_system_add_ship(&position, 2, 0, homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM)));
  assert(test_system_add_ship(&position, 2, 1, homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE)));
  assert(test_system_add_ship(&position, 2, 1, homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL)));

  assert(homeworlds_system_color_count(&position.systems[2], HOMEWORLDS_COLOR_BLUE) == 4);
  assert(homeworlds_position_apply_catastrophe(&position, 2, HOMEWORLDS_COLOR_BLUE));
  assert(homeworlds_system_is_empty(&position.systems[2]));
}

static void test_symbolic_catastrophe_move_does_not_finish_turn(void) {
  HomeworldsPosition position = {0};
  HomeworldsMove move = {
    .kind = HOMEWORLDS_MOVE_KIND_TURN,
    .step_count = 1,
    .steps = {
      {
        .kind = HOMEWORLDS_STEP_CATASTROPHE,
        .target_system = test_star_ref(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL, 0),
        .target_color = HOMEWORLDS_COLOR_BLUE,
      },
    },
  };

  homeworlds_position_init(&position);
  position.phase = HOMEWORLDS_PHASE_PLAY;
  position.turn = 0;
  assert(test_system_add_star(&position, 2, homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL)));
  assert(test_system_add_ship(&position, 2, 0, homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL)));
  assert(test_system_add_ship(&position, 2, 0, homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM)));
  assert(test_system_add_ship(&position, 2, 1, homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE)));

  assert(homeworlds_system_color_count(&position.systems[2], HOMEWORLDS_COLOR_BLUE) == 4);
  assert(homeworlds_position_apply_move(&position, &move));
  assert(position.phase == HOMEWORLDS_PHASE_PLAY);
  assert(position.turn == 0);
  assert(homeworlds_system_is_empty(&position.systems[2]));
}

static void test_static_evaluation_counts_largest_homeworld_ship_twice(void) {
  HomeworldsPosition position = {0};

  homeworlds_position_init(&position);
  position.phase = HOMEWORLDS_PHASE_PLAY;

  assert(test_system_add_ship(&position, 0, 0, homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL)));
  assert(test_system_add_ship(&position, 0, 0, homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE)));
  assert(test_system_add_ship(&position, 1, 1, homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL)));
  assert(test_system_add_ship(&position, 1, 1, homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW,
                                                                       HOMEWORLDS_SIZE_MEDIUM)));

  assert(homeworlds_position_evaluate_static(&position) == 20);
}

static void test_position_ascii_formats_systems_by_reachability(void) {
  HomeworldsPosition position = {0};
  char *text = NULL;

  homeworlds_position_init(&position);
  position.phase = HOMEWORLDS_PHASE_PLAY;
  position.turn = 0;
  memset(position.systems, 0, sizeof(position.systems));

  position.systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position.systems[2] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  position.systems[3] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
    },
  };
  position.systems[4] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM),
      },
    },
  };
  position.systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };

  text = homeworlds_position_format_ascii(&position);
  assert(text != NULL);
  assert(strcmp(text,
                "b3 Y1B2 -\n"
                "- R3 r1\n"
                "\n"
                "- R2Y3 -\n"
                "- R1 y2\n"
                "- G2B3 g3\n") == 0);
  g_free(text);
}

static void test_position_ascii_formats_empty_position(void) {
  HomeworldsPosition position = {0};
  char *text = NULL;

  homeworlds_position_init(&position);
  text = homeworlds_position_format_ascii(&position);
  assert(text != NULL);
  assert(strcmp(text, "No systems.\n") == 0);
  g_free(text);
}

static void test_move_parse_failure_leaves_output_unchanged(void) {
  HomeworldsMove move = {
    .kind = HOMEWORLDS_MOVE_KIND_SETUP,
    .setup_stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
    },
    .setup_ship = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
  };
  HomeworldsMove before = move;

  assert(!homeworlds_move_parse("H1g+ @", &move));
  assert(memcmp(&move, &before, sizeof(move)) == 0);
}

static void test_move_parse_accepts_legacy_step_spacing(void) {
  HomeworldsMove move = {0};
  char notation[128] = {0};

  assert(homeworlds_move_parse("H2 g3-/B1 g+/H2 g+/B1 g+", &move));
  assert(move.kind == HOMEWORLDS_MOVE_KIND_TURN);
  assert(move.step_count == 4);
  assert(homeworlds_move_format(&move, notation, sizeof(notation)));
  assert(strcmp(notation, "H2g3- B1g+ H2g+ B1g+") == 0);
}

static void test_move_equality_uses_structural_notation_identity(void) {
  HomeworldsMove build = {
    .kind = HOMEWORLDS_MOVE_KIND_TURN,
    .step_count = 1,
    .steps = {
      {
        .kind = HOMEWORLDS_STEP_BUILD,
        .actor = {
          .system = test_homeworld_ref(0),
          .ship = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
        },
        .target_ship = {
          .system = test_star_ref(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL, 0),
          .ship = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
        },
        .target_color = HOMEWORLDS_COLOR_GREEN,
      },
    },
  };
  HomeworldsMove same_build = build;
  HomeworldsMove different_build = build;

  same_build.steps[0].actor.ship = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE);
  same_build.steps[0].target_ship.ship = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE);
  different_build.steps[0].target_color = HOMEWORLDS_COLOR_BLUE;

  assert(homeworlds_moves_equal(&build, &same_build));
  assert(!homeworlds_moves_equal(&build, &different_build));
}

static void test_list_all_moves_deduplicates_symbolic_moves(void) {
  HomeworldsPosition position = {0};
  GameBackendMoveList moves = {0};
  HomeworldsPyramid green_small = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL);
  guint green_build_count = 0;

  test_prepare_basic_position(&position);
  assert(test_bank_remove(&position, green_small));
  assert(test_system_add_ship(&position, 0, 0, green_small));

  moves = homeworlds_position_list_all_moves(&position);
  assert(moves.count > 0);
  for (gsize i = 0; i < moves.count; ++i) {
    const HomeworldsMove *move = homeworlds_move_list_get(&moves, i);
    char notation[128] = {0};

    assert(move != NULL);
    assert(homeworlds_move_format(move, notation, sizeof(notation)));
    if (strcmp(notation, "H1g+") == 0) {
      green_build_count++;
    }

    for (gsize j = i + 1; j < moves.count; ++j) {
      const HomeworldsMove *other = homeworlds_move_list_get(&moves, j);

      assert(other != NULL);
      assert(!homeworlds_moves_equal(move, other));
    }
  }
  assert(green_build_count == 1);
  homeworlds_move_list_free(&moves);
}

int main(void) {
  test_setup_and_loss_detection();
  test_setup_accepts_any_bank_pyramids();
  test_build_uses_smallest_available_ship();
  test_invalid_multi_step_move_leaves_position_unchanged();
  test_failed_turn_step_leaves_position_unchanged();
  test_smallest_bank_ship_failure_clears_output();
  test_trade_preserves_size();
  test_attack_requires_size_and_changes_owner();
  test_move_and_discover_follow_connectivity();
  test_empty_system_lookup_failure_sets_invalid_index();
  test_system_ref_resolution_failure_sets_invalid_index();
  test_sacrifice_grants_multiple_actions();
  test_sacrifice_actions_ignore_local_color_access();
  test_catastrophe_removes_matching_color_and_collapses_star_system();
  test_ship_catastrophe_returns_orphaned_stars_to_bank();
  test_symbolic_catastrophe_move_does_not_finish_turn();
  test_static_evaluation_counts_largest_homeworld_ship_twice();
  test_position_ascii_formats_systems_by_reachability();
  test_position_ascii_formats_empty_position();
  test_move_parse_failure_leaves_output_unchanged();
  test_move_parse_accepts_legacy_step_spacing();
  test_move_equality_uses_structural_notation_identity();
  test_list_all_moves_deduplicates_symbolic_moves();
  return 0;
}
