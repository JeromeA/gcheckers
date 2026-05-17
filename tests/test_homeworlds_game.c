#include <assert.h>
#include <string.h>

#include "../src/games/homeworlds/homeworlds_game.h"

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
                                   .actor = test_ship_ref(test_homeworld_ref(0),
                                                          HOMEWORLDS_COLOR_GREEN,
                                                          HOMEWORLDS_SIZE_LARGE),
                               });
  assert(homeworlds_position_apply_move(&position, &move));
  assert(homeworlds_system_ship_count_for_side(&position.systems[0], 0) == 2);
  assert(position.systems[0].ships[0][1] == homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL));
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

int main(void) {
  test_setup_and_loss_detection();
  test_setup_accepts_any_bank_pyramids();
  test_build_uses_smallest_available_ship();
  test_trade_preserves_size();
  test_attack_requires_size_and_changes_owner();
  test_move_and_discover_follow_connectivity();
  test_sacrifice_grants_multiple_actions();
  test_catastrophe_removes_matching_color_and_collapses_star_system();
  test_symbolic_catastrophe_move_does_not_finish_turn();
  test_static_evaluation_counts_largest_homeworld_ship_twice();
  return 0;
}
