#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/games/homeworlds/homeworlds_backend.h"
#include "../src/games/homeworlds/homeworlds_game.h"
#include "../src/games/homeworlds/homeworlds_move_builder.h"
#include "../src/sgf_tree.h"

typedef struct {
  HomeworldsGoodMoveTrace trace;
  char *goal_report;
  gboolean called;
} TestGoodMoveTraceCapture;

static HomeworldsSystemRef test_homeworld_ref(guint side) {
  assert(side < 2);

  return (HomeworldsSystemRef){
    .kind = HOMEWORLDS_SYSTEM_REF_HOMEWORLD,
    .homeworld_side = side,
  };
}

static HomeworldsSystemRef test_system_ref(guint system_index) {
  assert(system_index >= 2);
  assert(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT);

  return (HomeworldsSystemRef){
    .kind = HOMEWORLDS_SYSTEM_REF_SYSTEM,
    .system_index = (guint8)system_index,
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

static guint test_setup_star_size_mask(const HomeworldsMove *move) {
  guint mask = 0;

  assert(move != NULL);
  assert(move->kind == HOMEWORLDS_MOVE_KIND_SETUP);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    assert(homeworlds_pyramid_is_valid(move->setup_stars[i]));
    mask |= 1u << (homeworlds_pyramid_size(move->setup_stars[i]) - 1);
  }

  return mask;
}

static void test_assert_good_setup_policy(const HomeworldsMove *move) {
  gboolean seen_colors[4] = {FALSE};

  assert(move != NULL);
  assert(move->kind == HOMEWORLDS_MOVE_KIND_SETUP);
  assert(homeworlds_pyramid_size(move->setup_ship) == HOMEWORLDS_SIZE_LARGE);
  assert(test_setup_star_size_mask(move) != 0);
  assert((test_setup_star_size_mask(move) & (test_setup_star_size_mask(move) - 1)) != 0);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    HomeworldsColor color = homeworlds_pyramid_color(move->setup_stars[i]);
    assert(!seen_colors[color]);
    seen_colors[color] = TRUE;
  }

  HomeworldsColor ship_color = homeworlds_pyramid_color(move->setup_ship);
  assert(!seen_colors[ship_color]);
  seen_colors[ship_color] = TRUE;
  assert(seen_colors[HOMEWORLDS_COLOR_GREEN]);
  assert(seen_colors[HOMEWORLDS_COLOR_BLUE]);
  assert(seen_colors[HOMEWORLDS_COLOR_RED] || seen_colors[HOMEWORLDS_COLOR_YELLOW]);
}

static gboolean test_step_is_catastrophe_at(const HomeworldsPosition *position,
                                            const HomeworldsTurnStep *step,
                                            guint system_index,
                                            HomeworldsColor color) {
  guint step_system_index = HOMEWORLDS_INVALID_INDEX;

  assert(position != NULL);
  assert(step != NULL);
  assert(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT);
  assert(color <= HOMEWORLDS_COLOR_BLUE);

  return step->kind == HOMEWORLDS_STEP_CATASTROPHE &&
         step->target_color == color &&
         homeworlds_position_resolve_system_ref(position, &step->target_system, &step_system_index) &&
         step_system_index == system_index;
}

static gboolean test_move_has_catastrophe_at(const HomeworldsPosition *position,
                                             const HomeworldsMove *move,
                                             guint system_index,
                                             HomeworldsColor color) {
  assert(position != NULL);
  assert(move != NULL);

  if (move->kind != HOMEWORLDS_MOVE_KIND_TURN) {
    return FALSE;
  }

  for (guint i = 0; i < move->step_count; ++i) {
    if (test_step_is_catastrophe_at(position, &move->steps[i], system_index, color)) {
      return TRUE;
    }
  }

  return FALSE;
}

static void test_prepare_position(HomeworldsPosition *position) {
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
}

static void test_apply_notation(HomeworldsPosition *position, const char *notation) {
  HomeworldsMove move = {0};

  assert(position != NULL);
  assert(notation != NULL);
  assert(homeworlds_move_parse(notation, &move));
  assert(homeworlds_position_apply_move(position, &move));
}

static void test_prepare_static_prune_position(HomeworldsPosition *position, guint move_count) {
  static const char *moves[] = {
    "Y3B1g3",
    "R2B1g3",
    "H1g+",
    "H2g+",
    "H1g1>S0(B2)",
    "H2g3=b",
    "S0g+",
    "H2g+",
    "H1g+",
    "H2g1=r",
    "S0g1=y",
    "H2r+",
    "S0g1>S1(B3)",
    "H2r1=y",
    "S1g+",
    "H2b+",
    "H1g3- H1g+ S1g+ H1g+",
    "H2g+",
    "H1g3- H1g+ S0y+ S0y+",
    "H2g3- H2y+ H2g+ H2g+",
  };

  assert(position != NULL);
  assert(move_count <= G_N_ELEMENTS(moves));

  homeworlds_position_init(position);
  for (guint i = 0; i < move_count; ++i) {
    test_apply_notation(position, moves[i]);
  }
}

static void test_prepare_unfavorable_trade_catastrophe_position(HomeworldsPosition *position) {
  static const char *moves[] = {
    "B1Y2g3",
    "Y3B2g3",
    "H1g+",
    "H2g+",
    "H1g1>S0(B3)",
    "H2g3=y",
    "H1g+",
    "H2g+",
    "S0g+",
    "H2g2=r",
    "H1g1>S1(B3)",
    "H2g+",
    "H1g+",
    "H2g+",
    "S1g+",
    "H2g3- H2r+ H2r+ H2g+",
    "S0g2=y",
    "H2r2=b",
    "H1g3- S0g+ H1g+ S0y+",
    "H2g3- H2r+ H2g+ H2b+",
    "S0y2- S0g1>S2(B1) S2g1>H2 H2g!",
    "H2b2=g",
    "H1g2=r",
    "H2b+",
    "H1r+",
    "H2g+",
    "H1g+",
    "H2g+",
    "S0g+",
    "H2g2- H2y+ H2g+",
    "S1g1=y",
  };

  assert(position != NULL);

  homeworlds_position_init(position);
  for (guint i = 0; i < G_N_ELEMENTS(moves); ++i) {
    test_apply_notation(position, moves[i]);
  }
  assert(position->phase == HOMEWORLDS_PHASE_PLAY);
  assert(position->turn == 1);
}

static void test_prepare_homeworld_orphaning_yellow_catastrophe_position(HomeworldsPosition *position) {
  assert(position != NULL);

  homeworlds_position_init(position);
  position->phase = HOMEWORLDS_PHASE_PLAY;
  position->turn = 0;
  memset(position->systems, 0, sizeof(position->systems));
  position->systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  position->systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position->systems[2] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM),
      },
    },
  };
  position->systems[3] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  homeworlds_position_rebuild_color_counts(position);
}

static void test_prepare_two_goal_yellow_catastrophe_position(HomeworldsPosition *position) {
  assert(position != NULL);

  homeworlds_position_init(position);
  position->phase = HOMEWORLDS_PHASE_PLAY;
  position->turn = 0;
  memset(position->bank, 0, sizeof(position->bank));
  memset(position->systems, 0, sizeof(position->systems));
  position->systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  position->systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position->systems[2] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position->systems[3] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  homeworlds_position_rebuild_color_counts(position);
}

static void test_prepare_large_yellow_goal_report_position(HomeworldsPosition *position) {
  assert(position != NULL);

  homeworlds_position_init(position);
  position->phase = HOMEWORLDS_PHASE_PLAY;
  position->turn = 0;
  memset(position->bank, 0, sizeof(position->bank));
  memset(position->systems, 0, sizeof(position->systems));
  position->systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position->systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
      },
    },
  };
  position->systems[2] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position->systems[3] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM),
      },
    },
  };
  position->systems[4] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position->systems[5] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
      },
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
      },
    },
  };
  position->systems[6] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  position->systems[7] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position->systems[8] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  homeworlds_position_rebuild_color_counts(position);
}

static void test_prepare_buildability_goal_position(HomeworldsPosition *position) {
  HomeworldsPyramid bank[] = {
    homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
    homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
    homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
    homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
    homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE),
    homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE),
    homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM),
    homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
    homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
    homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
    homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
    homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
    homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
    homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
    homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
    homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
  };

  assert(position != NULL);

  homeworlds_position_init(position);
  position->phase = HOMEWORLDS_PHASE_PLAY;
  position->turn = 1;
  memset(position->bank, 0, sizeof(position->bank));
  memset(position->systems, 0, sizeof(position->systems));
  memcpy(position->bank, bank, sizeof(bank));
  position->systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM),
      },
    },
  };
  position->systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM),
      },
    },
  };
  position->systems[2] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM),
      },
    },
  };
  position->systems[3] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position->systems[4] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  homeworlds_position_rebuild_color_counts(position);
}

static void test_prepare_existing_catastrophe_quality_goal_position(HomeworldsPosition *position) {
  assert(position != NULL);

  homeworlds_position_init(position);
  position->phase = HOMEWORLDS_PHASE_PLAY;
  position->turn = 0;
  memset(position->bank, 0, sizeof(position->bank));
  memset(position->systems, 0, sizeof(position->systems));
  position->systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  position->systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position->systems[2] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      },
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  homeworlds_position_rebuild_color_counts(position);
}

static gint test_score_after_move(const HomeworldsPosition *position, const HomeworldsMove *move) {
  HomeworldsPosition child = {0};
  GameBackendOutcome outcome = GAME_BACKEND_OUTCOME_ONGOING;
  gint score = 0;

  assert(position != NULL);
  assert(move != NULL);

  homeworlds_position_copy(&child, position);
  assert(homeworlds_position_apply_move(&child, move));
  outcome = homeworlds_position_outcome(&child);
  score = outcome == GAME_BACKEND_OUTCOME_ONGOING
      ? homeworlds_position_evaluate_static(&child)
      : homeworlds_position_terminal_score(outcome, 1);
  homeworlds_position_clear(&child);
  return score;
}

static void test_remove_all_from_bank(HomeworldsPosition *position, HomeworldsPyramid pyramid) {
  assert(position != NULL);
  assert(homeworlds_pyramid_is_valid(pyramid));

  for (guint i = 0; i < HOMEWORLDS_BANK_SLOT_COUNT; ++i) {
    if (position->bank[i] == pyramid) {
      position->bank[i] = 0;
    }
  }
}

static void test_take_one_from_bank(HomeworldsPosition *position, HomeworldsPyramid pyramid) {
  assert(position != NULL);
  assert(homeworlds_pyramid_is_valid(pyramid));

  for (guint i = 0; i < HOMEWORLDS_BANK_SLOT_COUNT; ++i) {
    if (position->bank[i] != pyramid) {
      continue;
    }

    position->bank[i] = 0;
    return;
  }

  assert(FALSE);
}

static void test_add_to_bank(HomeworldsPosition *position, HomeworldsPyramid pyramid) {
  assert(position != NULL);
  assert(homeworlds_pyramid_is_valid(pyramid));

  for (guint i = 0; i < HOMEWORLDS_BANK_SLOT_COUNT; ++i) {
    if (position->bank[i] != 0) {
      continue;
    }

    position->bank[i] = pyramid;
    return;
  }

  assert(FALSE);
}

static void test_prepare_player_two_yellow_sacrifice_position(HomeworldsPosition *position) {
  const char *prefix[] = {
    "Y2G3r3",
    "R2G1b3",
    "H1r+",
    "H2b+",
    "H1r1>S0(B1)",
    "H2b+",
    "H1r+",
    "H2b3=y",
    "H1r3>S1(Y1)",
  };

  assert(position != NULL);

  homeworlds_position_init(position);
  for (guint i = 0; i < G_N_ELEMENTS(prefix); ++i) {
    test_apply_notation(position, prefix[i]);
  }
  assert(position->phase == HOMEWORLDS_PHASE_PLAY);
  assert(position->turn == 1);
}

static void test_prepare_green_sacrifice_build_order_position(HomeworldsPosition *position) {
  assert(position != NULL);

  homeworlds_position_init(position);
  position->phase = HOMEWORLDS_PHASE_PLAY;
  position->turn = 0;
  memset(position->systems, 0, sizeof(position->systems));
  position->systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  position->systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position->systems[2] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  homeworlds_position_rebuild_color_counts(position);
}

static void test_prepare_green_sacrifice_ambiguous_build_order_position(HomeworldsPosition *position) {
  const char *prefix[] = {
    "B1Y3g3",
    "G2Y3b3",
    "H1g+",
    "H2b+",
    "H1g+",
    "H2b+",
    "H1g1>S0(B2)",
    "H2b1=y",
    "S0g+",
    "H2y+",
  };

  assert(position != NULL);

  homeworlds_position_init(position);
  for (guint i = 0; i < G_N_ELEMENTS(prefix); ++i) {
    test_apply_notation(position, prefix[i]);
  }
  assert(position->phase == HOMEWORLDS_PHASE_PLAY);
  assert(position->turn == 0);
}

static void test_prepare_green_medium_rebuild_position(HomeworldsPosition *position) {
  HomeworldsPyramid green_small = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL);

  assert(position != NULL);

  homeworlds_position_init(position);
  position->phase = HOMEWORLDS_PHASE_PLAY;
  position->turn = 0;
  memset(position->systems, 0, sizeof(position->systems));
  position->systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM),
        green_small,
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  position->systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position->systems[2] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [0] = {
        green_small,
      },
    },
  };
  homeworlds_position_rebuild_color_counts(position);
}

static void test_prepare_sacrifice_catastrophe_order_position(HomeworldsPosition *position) {
  assert(position != NULL);

  homeworlds_position_init(position);
  position->phase = HOMEWORLDS_PHASE_PLAY;
  position->turn = 0;
  memset(position->systems, 0, sizeof(position->systems));
  position->systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  position->systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position->systems[2] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  homeworlds_position_rebuild_color_counts(position);
  assert(homeworlds_system_color_count(&position->systems[2], HOMEWORLDS_COLOR_RED) == 4);
}

static void test_prepare_terminal_homeworld_catastrophe_position(HomeworldsPosition *position) {
  assert(position != NULL);

  homeworlds_position_init(position);
  position->phase = HOMEWORLDS_PHASE_PLAY;
  position->turn = 0;
  memset(position->systems, 0, sizeof(position->systems));
  position->systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  position->systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      },
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
      },
    },
  };
  homeworlds_position_rebuild_color_counts(position);
  assert(homeworlds_system_color_count(&position->systems[1], HOMEWORLDS_COLOR_RED) == 4);
}

static void test_prepare_yellow_sacrifice_route_position(HomeworldsPosition *position,
                                                         gboolean direct_target_is_reachable) {
  assert(position != NULL);

  homeworlds_position_init(position);
  position->phase = HOMEWORLDS_PHASE_PLAY;
  position->turn = 0;
  memset(position->systems, 0, sizeof(position->systems));
  position->systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      direct_target_is_reachable ? 0 : homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  position->systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position->systems[2] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE,
                              direct_target_is_reachable ? HOMEWORLDS_SIZE_MEDIUM : HOMEWORLDS_SIZE_LARGE),
    },
  };
  position->systems[3] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN,
                              direct_target_is_reachable ? HOMEWORLDS_SIZE_LARGE : HOMEWORLDS_SIZE_MEDIUM),
    },
  };
  homeworlds_position_rebuild_color_counts(position);
}

static gboolean test_good_moves_contains_notation(const GameBackend *backend,
                                                  const GameBackendMoveList *good_moves,
                                                  const char *expected_notation) {
  char notation[128] = {0};

  assert(backend != NULL);
  assert(good_moves != NULL);
  assert(expected_notation != NULL);

  for (gsize i = 0; i < good_moves->count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(good_moves, i);

    assert(move != NULL);
    assert(backend->format_move(move, notation, sizeof(notation)));
    if (strcmp(notation, expected_notation) == 0) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean test_good_moves_contains_notation_prefix(const GameBackend *backend,
                                                         const GameBackendMoveList *good_moves,
                                                         const char *expected_prefix) {
  char notation[128] = {0};

  assert(backend != NULL);
  assert(good_moves != NULL);
  assert(expected_prefix != NULL);

  for (gsize i = 0; i < good_moves->count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(good_moves, i);

    assert(move != NULL);
    assert(backend->format_move(move, notation, sizeof(notation)));
    if (g_str_has_prefix(notation, expected_prefix)) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean test_good_moves_contains_equivalent_notation(const GameBackend *backend,
                                                             const HomeworldsPosition *position,
                                                             const GameBackendMoveList *good_moves,
                                                             const char *expected_notation) {
  HomeworldsMove expected = {0};

  assert(backend != NULL);
  assert(position != NULL);
  assert(good_moves != NULL);
  assert(expected_notation != NULL);
  assert(backend->moves_equivalent != NULL);
  assert(homeworlds_move_parse(expected_notation, &expected));

  for (gsize i = 0; i < good_moves->count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(good_moves, i);

    assert(move != NULL);
    if (backend->moves_equivalent(position, move, &expected)) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean test_move_has_step_kind(const HomeworldsMove *move, HomeworldsStepKind kind) {
  assert(move != NULL);

  if (move->kind != HOMEWORLDS_MOVE_KIND_TURN) {
    return FALSE;
  }

  for (guint i = 0; i < move->step_count; ++i) {
    if (move->steps[i].kind == kind) {
      return TRUE;
    }
  }
  return FALSE;
}

static void test_assert_move_notation_is_legal(const HomeworldsPosition *position, const char *notation) {
  HomeworldsPosition copy = {0};
  HomeworldsMove move = {0};

  assert(position != NULL);
  assert(notation != NULL);

  copy = *position;
  assert(homeworlds_move_parse(notation, &move));
  assert(homeworlds_position_apply_move(&copy, &move));
}

static void test_assert_move_notations_reach_different_positions(const HomeworldsPosition *position,
                                                                 const char *left_notation,
                                                                 const char *right_notation) {
  HomeworldsPosition left = {0};
  HomeworldsPosition right = {0};
  HomeworldsMove left_move = {0};
  HomeworldsMove right_move = {0};

  assert(position != NULL);
  assert(left_notation != NULL);
  assert(right_notation != NULL);

  left = *position;
  right = *position;
  assert(homeworlds_move_parse(left_notation, &left_move));
  assert(homeworlds_move_parse(right_notation, &right_move));
  assert(homeworlds_position_apply_move(&left, &left_move));
  assert(homeworlds_position_apply_move(&right, &right_move));
  assert(memcmp(&left, &right, sizeof(left)) != 0);
}

static void test_assert_move_notations_reach_same_position(const HomeworldsPosition *position,
                                                           const char *left_notation,
                                                           const char *right_notation) {
  HomeworldsPosition left = {0};
  HomeworldsPosition right = {0};
  HomeworldsMove left_move = {0};
  HomeworldsMove right_move = {0};

  assert(position != NULL);
  assert(left_notation != NULL);
  assert(right_notation != NULL);

  left = *position;
  right = *position;
  assert(homeworlds_move_parse(left_notation, &left_move));
  assert(homeworlds_move_parse(right_notation, &right_move));
  assert(homeworlds_position_apply_move(&left, &left_move));
  assert(homeworlds_position_apply_move(&right, &right_move));
  assert(homeworlds_positions_equal(&left, &right));
}

static const HomeworldsMoveCandidate *test_find_setup_candidate(const GameBackend *backend,
                                                                const GameBackendMoveList *candidates,
                                                                HomeworldsCandidateKind kind,
                                                                HomeworldsPyramid pyramid) {
  g_return_val_if_fail(backend != NULL, NULL);
  g_return_val_if_fail(candidates != NULL, NULL);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), NULL);

  for (gsize i = 0; i < candidates->count; ++i) {
    const HomeworldsMoveCandidate *candidate = backend->move_list_get(candidates, i);

    if (candidate->data.kind == kind && candidate->data.pyramid == pyramid) {
      return candidate;
    }
  }

  return NULL;
}

static const HomeworldsMoveCandidate *test_find_ship_candidate(const GameBackend *backend,
                                                               const GameBackendMoveList *candidates,
                                                               guint system_index,
                                                               HomeworldsPyramid pyramid) {
  g_return_val_if_fail(backend != NULL, NULL);
  g_return_val_if_fail(candidates != NULL, NULL);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), NULL);

  for (gsize i = 0; i < candidates->count; ++i) {
    const HomeworldsMoveCandidate *candidate = backend->move_list_get(candidates, i);

    if (candidate->data.kind == HOMEWORLDS_CANDIDATE_SELECT_SHIP &&
        candidate->data.system_index == system_index &&
        candidate->data.pyramid == pyramid) {
      return candidate;
    }
  }

  return NULL;
}

static const HomeworldsMoveCandidate *test_find_action_candidate(const GameBackend *backend,
                                                                 const GameBackendMoveList *candidates,
                                                                 HomeworldsStepKind action) {
  g_return_val_if_fail(backend != NULL, NULL);
  g_return_val_if_fail(candidates != NULL, NULL);

  for (gsize i = 0; i < candidates->count; ++i) {
    const HomeworldsMoveCandidate *candidate = backend->move_list_get(candidates, i);

    if (candidate->data.kind == HOMEWORLDS_CANDIDATE_ACTION && candidate->data.target_color == action) {
      return candidate;
    }
  }

  return NULL;
}

static void test_capture_good_move_trace(const HomeworldsGoodMoveTrace *trace, gpointer user_data) {
  TestGoodMoveTraceCapture *capture = user_data;

  assert(trace != NULL);
  assert(capture != NULL);

  capture->trace = *trace;
  g_free(capture->goal_report);
  capture->goal_report = g_strdup(trace->goal_report);
  capture->trace.moves = NULL;
  capture->trace.move_count = 0;
  capture->trace.goal_report = capture->goal_report;
  capture->called = TRUE;
}

static gboolean test_goal_report_find_goal_bounds(const char *report,
                                                  const char *goal_label,
                                                  gint *out_min,
                                                  gint *out_max) {
  char needle[128] = {0};
  const char *match = NULL;

  assert(report != NULL);
  assert(goal_label != NULL);
  assert(out_min != NULL);
  assert(out_max != NULL);

  g_snprintf(needle, sizeof(needle), "goal=[%s]", goal_label);
  match = report;
  while ((match = strstr(match, needle)) != NULL) {
    const char *line = match;
    const char *bounds = NULL;

    while (line > report && line[-1] != '\n') {
      line--;
    }
    bounds = strstr(line, "bounds=[");
    if (bounds != NULL && bounds < match && sscanf(bounds, "bounds=[%d,%d]", out_min, out_max) == 2) {
      return TRUE;
    }
    match += strlen(needle);
  }
  return FALSE;
}

static gboolean test_goal_report_find_selected_goal_rejects(const char *report,
                                                            const char *goal_label,
                                                            gsize *out_reject_count) {
  g_auto(GStrv) lines = NULL;
  g_autofree gchar *needle = NULL;
  gboolean found = FALSE;

  assert(report != NULL);
  assert(goal_label != NULL);
  assert(out_reject_count != NULL);

  *out_reject_count = 0;
  needle = g_strdup_printf("goal=[%s]", goal_label);
  assert(needle != NULL);
  lines = g_strsplit(report, "\n", -1);
  for (guint i = 0; lines[i] != NULL; ++i) {
    gsize id = 0;
    char result_prefix[64] = {0};
    gboolean found_result = FALSE;

    if (!g_str_has_prefix(lines[i], "select #") ||
        strstr(lines[i], needle) == NULL ||
        sscanf(lines[i], "select #%zu", &id) != 1) {
      continue;
    }

    g_snprintf(result_prefix, sizeof(result_prefix), "explore-result #%zu", id);
    for (guint j = i + 1; lines[j] != NULL && !g_str_has_prefix(lines[j], "select #"); ++j) {
      const char *rejects = NULL;

      if (!g_str_has_prefix(lines[j], result_prefix)) {
        continue;
      }
      rejects = strstr(lines[j], "goal_filter_reject+=");
      if (rejects != NULL) {
        gsize reject_count = 0;

        if (sscanf(rejects, "goal_filter_reject+=%zu", &reject_count) != 1) {
          return FALSE;
        }
        *out_reject_count += reject_count;
      }
      found = TRUE;
      found_result = TRUE;
      break;
    }
    if (!found_result) {
      return FALSE;
    }
  }
  return found;
}

static const HomeworldsMoveCandidate *test_find_trade_color_candidate(const GameBackend *backend,
                                                                      const GameBackendMoveList *candidates,
                                                                      HomeworldsColor color) {
  g_return_val_if_fail(backend != NULL, NULL);
  g_return_val_if_fail(candidates != NULL, NULL);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, NULL);

  for (gsize i = 0; i < candidates->count; ++i) {
    const HomeworldsMoveCandidate *candidate = backend->move_list_get(candidates, i);

    if (candidate->data.kind == HOMEWORLDS_CANDIDATE_TRADE_COLOR && candidate->data.target_color == color) {
      return candidate;
    }
  }

  return NULL;
}

static void test_backend_metadata(void) {
  const GameBackend *backend = &homeworlds_game_backend;

  assert(strcmp(backend->id, "homeworlds") == 0);
  assert(strcmp(backend->display_name, "Homeworlds") == 0);
  assert(!backend->supports_move_list);
  assert(backend->supports_move_builder);
  assert(backend->supports_ai_search);
  assert(backend->supports_ascii_game_io);
  assert(strcmp(backend->ascii_game_file_description, "Homeworlds text game files") == 0);
  assert(strcmp(backend->ascii_game_file_extension, "txt") == 0);
  assert(backend->list_good_moves != NULL);
  assert(backend->parse_move != NULL);
  assert(backend->moves_equivalent != NULL);
  assert(backend->sgf_color_for_side != NULL);
  assert(backend->sgf_apply_setup_node != NULL);
  assert(backend->sgf_write_position_node != NULL);
  assert(backend->sgf_color_for_side(0) == SGF_COLOR_BLACK);
  assert(backend->sgf_color_for_side(1) == SGF_COLOR_WHITE);
  assert(strcmp(backend->side_label(0), "Player 1") == 0);
}

static void test_backend_move_roundtrips(const HomeworldsMove *move) {
  const GameBackend *backend = &homeworlds_game_backend;
  char notation[128] = {0};
  HomeworldsMove parsed = {0};

  assert(move != NULL);
  assert(backend->format_move(move, notation, sizeof(notation)));
  assert(notation[0] != '\0');
  assert(backend->parse_move(notation, &parsed));
  assert(backend->moves_equal(move, &parsed));
}

static void test_backend_move_equality_uses_symbolic_build_identity(void) {
  const GameBackend *backend = &homeworlds_game_backend;
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
          .system = test_system_ref(2),
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

  assert(backend->moves_equal(&build, &same_build));
  assert(!backend->moves_equal(&build, &different_build));
}

static void test_backend_move_equivalence_uses_resulting_position(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  HomeworldsMove canonical = {0};
  HomeworldsMove redundant = {0};
  const char *canonical_move = "H1b2- H1r1=g H1y1=r";
  const char *redundant_move = "H1b2- H1y1=r H1r1=g";

  test_prepare_position(&position);
  position.systems[0].ships[0][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM);
  position.systems[0].ships[0][1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL);
  position.systems[0].ships[0][2] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL);
  for (guint slot = 3; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    position.systems[0].ships[0][slot] = 0;
  }
  homeworlds_system_rebuild_color_counts(&position.systems[0]);

  assert(homeworlds_move_parse(canonical_move, &canonical));
  assert(homeworlds_move_parse(redundant_move, &redundant));
  assert(!backend->moves_equal(&canonical, &redundant));
  assert(backend->moves_equivalent(&position, &canonical, &redundant));
}

static void test_backend_move_codec_roundtrips_setup_and_turn(void) {
  HomeworldsMove setup = test_setup_move(0,
                                         homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
                                         homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
                                         homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE));
  HomeworldsMove turn = {
    .kind = HOMEWORLDS_MOVE_KIND_TURN,
    .step_count = 2,
    .steps = {
      {
        .kind = HOMEWORLDS_STEP_SACRIFICE,
        .actor = test_ship_ref(test_homeworld_ref(0), HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
      },
      {
        .kind = HOMEWORLDS_STEP_TRADE,
        .actor = test_ship_ref(test_homeworld_ref(0), HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
        .target_color = HOMEWORLDS_COLOR_GREEN,
      },
    },
  };
  HomeworldsMove build = {
    .kind = HOMEWORLDS_MOVE_KIND_TURN,
    .step_count = 1,
    .steps = {
      {
        .kind = HOMEWORLDS_STEP_BUILD,
        .actor = {
          .system = test_homeworld_ref(0),
        },
        .target_color = HOMEWORLDS_COLOR_GREEN,
      },
    },
  };
  char notation[128] = {0};
  HomeworldsMove parsed = {0};

  test_backend_move_roundtrips(&setup);
  test_backend_move_roundtrips(&turn);
  test_backend_move_roundtrips(&build);
  assert(homeworlds_move_format(&setup, notation, sizeof(notation)));
  assert(strcmp(notation, "R1B2g3") == 0);
  assert(homeworlds_move_format(&turn, notation, sizeof(notation)));
  assert(strcmp(notation, "H1g3- H1r3=g") == 0);
  assert(homeworlds_move_format(&build, notation, sizeof(notation)));
  assert(strcmp(notation, "H1g+") == 0);
  assert(homeworlds_move_parse("H1g+", &parsed));
  assert(parsed.steps[0].kind == HOMEWORLDS_STEP_BUILD);
  assert(parsed.steps[0].actor.ship == 0);
  assert(parsed.steps[0].target_color == HOMEWORLDS_COLOR_GREEN);
  assert(homeworlds_move_format(&parsed, notation, sizeof(notation)));
  assert(strcmp(notation, "H1g+") == 0);
  assert(homeworlds_move_parse("Y2B1g3", &parsed));
  assert(homeworlds_move_format(&parsed, notation, sizeof(notation)));
  assert(strcmp(notation, "Y2B1g3") == 0);
  assert(homeworlds_move_parse("S1y2>S2(G2) S1y!", &parsed));
  assert(parsed.steps[0].kind == HOMEWORLDS_STEP_DISCOVER);
  assert(parsed.steps[0].target_system.kind == HOMEWORLDS_SYSTEM_REF_SYSTEM);
  assert(parsed.steps[0].target_system.system_index == 4);
  assert(homeworlds_move_format(&parsed, notation, sizeof(notation)));
  assert(strcmp(notation, "S1y2>S2(G2) S1y!") == 0);
  assert(homeworlds_move_parse("pass", &parsed));
  assert(homeworlds_move_format(&parsed, notation, sizeof(notation)));
  assert(strcmp(notation, "pass") == 0);
  assert(!homeworlds_move_parse("H1 g+", &parsed));
  assert(!homeworlds_move_parse("H1g3-/H1r3=g", &parsed));
  assert(!homeworlds_move_parse("S1y2>H1(G2)", &parsed));
}

static void test_backend_sgf_snapshot_roundtrips_position(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition saved = {0};
  HomeworldsPosition loaded = {0};
  g_autoptr(SgfTree) tree = NULL;
  SgfNode *root = NULL;

  test_prepare_position(&saved);
  saved.systems[2].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL);
  saved.systems[2].ships[0][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM);
  homeworlds_system_rebuild_color_counts(&saved.systems[2]);
  test_take_one_from_bank(&saved, saved.systems[2].stars[0]);
  test_take_one_from_bank(&saved, saved.systems[2].ships[0][0]);

  tree = sgf_tree_new();
  assert(SGF_IS_TREE(tree));
  root = (SgfNode *)sgf_tree_get_root(tree);
  assert(root != NULL);

  assert(backend->sgf_write_position_node(&saved, root, NULL));
  homeworlds_position_init(&loaded);
  assert(backend->sgf_apply_setup_node(&loaded, root, NULL));
  assert(memcmp(&saved, &loaded, sizeof(saved)) == 0);
}

static void test_backend_sgf_snapshot_rejects_duplicate_systems(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition saved = {0};
  HomeworldsPosition loaded = {0};
  g_autoptr(SgfTree) tree = NULL;
  SgfNode *root = NULL;
  const GPtrArray *systems = NULL;
  GError *error = NULL;

  test_prepare_position(&saved);
  tree = sgf_tree_new();
  assert(SGF_IS_TREE(tree));
  root = (SgfNode *)sgf_tree_get_root(tree);
  assert(root != NULL);

  assert(backend->sgf_write_position_node(&saved, root, NULL));
  systems = sgf_node_get_property_values(root, "GHS");
  assert(systems != NULL);
  assert(systems->len > 0);
  assert(sgf_node_add_property(root, "GHS", g_ptr_array_index((GPtrArray *)systems, 0)));

  homeworlds_position_init(&loaded);
  assert(!backend->sgf_apply_setup_node(&loaded, root, &error));
  assert(error != NULL);
  g_clear_error(&error);
}

static void test_backend_sgf_snapshot_rejects_ship_without_star(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition saved = {0};
  HomeworldsPosition loaded = {0};
  g_autoptr(SgfTree) tree = NULL;
  SgfNode *root = NULL;
  GError *error = NULL;

  test_prepare_position(&saved);
  tree = sgf_tree_new();
  assert(SGF_IS_TREE(tree));
  root = (SgfNode *)sgf_tree_get_root(tree);
  assert(root != NULL);

  assert(backend->sgf_write_position_node(&saved, root, NULL));
  assert(sgf_node_add_property(root,
                               "GHS",
                               "2|0,0|7,0,0,0,0,0,0,0,0,0,0,0,0,0|0,0,0,0,0,0,0,0,0,0,0,0,0,0"));

  homeworlds_position_init(&loaded);
  assert(!backend->sgf_apply_setup_node(&loaded, root, &error));
  assert(error != NULL);
  g_clear_error(&error);
}

static void test_backend_sgf_snapshot_rejects_star_without_ship(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition saved = {0};
  HomeworldsPosition loaded = {0};
  g_autoptr(SgfTree) tree = NULL;
  SgfNode *root = NULL;
  GError *error = NULL;

  test_prepare_position(&saved);
  saved.systems[2].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM);
  homeworlds_system_rebuild_color_counts(&saved.systems[2]);
  test_take_one_from_bank(&saved, saved.systems[2].stars[0]);

  tree = sgf_tree_new();
  assert(SGF_IS_TREE(tree));
  root = (SgfNode *)sgf_tree_get_root(tree);
  assert(root != NULL);

  assert(backend->sgf_write_position_node(&saved, root, NULL));

  homeworlds_position_init(&loaded);
  assert(!backend->sgf_apply_setup_node(&loaded, root, &error));
  assert(error != NULL);
  g_clear_error(&error);
}

static void test_backend_sgf_snapshot_rejects_loose_numbers(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition saved = {0};
  HomeworldsPosition loaded = {0};
  g_autoptr(SgfTree) tree = NULL;
  SgfNode *root = NULL;
  GError *error = NULL;

  test_prepare_position(&saved);
  tree = sgf_tree_new();
  assert(SGF_IS_TREE(tree));
  root = (SgfNode *)sgf_tree_get_root(tree);
  assert(root != NULL);

  assert(backend->sgf_write_position_node(&saved, root, NULL));
  assert(sgf_node_clear_property(root, "GHT"));
  assert(sgf_node_add_property(root, "GHT", " 0"));

  homeworlds_position_init(&loaded);
  assert(!backend->sgf_apply_setup_node(&loaded, root, &error));
  assert(error != NULL);
  g_clear_error(&error);
}

static void test_backend_sgf_snapshot_rejects_overfull_supply(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition saved = {0};
  HomeworldsPosition loaded = {0};
  g_autoptr(SgfTree) tree = NULL;
  SgfNode *root = NULL;
  GError *error = NULL;

  test_prepare_position(&saved);
  tree = sgf_tree_new();
  assert(SGF_IS_TREE(tree));
  root = (SgfNode *)sgf_tree_get_root(tree);
  assert(root != NULL);

  assert(backend->sgf_write_position_node(&saved, root, NULL));
  assert(sgf_node_clear_property(root, "GHB"));
  assert(sgf_node_add_property(root,
                               "GHB",
                               "1,2,3,4,5,6,7,8,9,10,11,12,"
                               "1,2,3,4,5,6,7,8,9,10,11,12,"
                               "1,2,3,4,5,6,7,8,9,10,11,12"));

  homeworlds_position_init(&loaded);
  assert(!backend->sgf_apply_setup_node(&loaded, root, &error));
  assert(error != NULL);
  g_clear_error(&error);
}

static void test_backend_move_builder_completes_setup(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  HomeworldsMove move = {0};

  homeworlds_position_init(&position);
  assert(backend->move_builder_init(&position, &builder));

  candidates = backend->move_builder_list_candidates(&builder);
  assert(candidates.count > 0);
  assert(backend->move_builder_step(&builder, backend->move_list_get(&candidates, 0)));
  backend->move_list_free(&candidates);

  candidates = backend->move_builder_list_candidates(&builder);
  assert(candidates.count > 0);
  assert(backend->move_builder_step(&builder, backend->move_list_get(&candidates, 0)));
  backend->move_list_free(&candidates);

  candidates = backend->move_builder_list_candidates(&builder);
  assert(candidates.count > 0);
  assert(backend->move_builder_step(&builder, backend->move_list_get(&candidates, 0)));
  backend->move_list_free(&candidates);

  assert(backend->move_builder_is_complete(&builder));
  assert(backend->move_builder_build_move(&builder, &move));
  assert(move.kind == HOMEWORLDS_MOVE_KIND_SETUP);
  backend->move_builder_clear(&builder);
}

static void test_backend_move_builder_setup_accepts_all_pyramid_sizes(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  HomeworldsPyramid repeated_large_star = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE);
  HomeworldsPyramid small_ship = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL);
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  HomeworldsMove move = {0};
  const HomeworldsMoveCandidate *candidate = NULL;

  homeworlds_position_init(&position);
  assert(backend->move_builder_init(&position, &builder));

  candidates = backend->move_builder_list_candidates(&builder);
  assert(candidates.count == 12);
  candidate = test_find_setup_candidate(backend,
                                        &candidates,
                                        HOMEWORLDS_CANDIDATE_SETUP_STAR,
                                        repeated_large_star);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  candidates = backend->move_builder_list_candidates(&builder);
  assert(candidates.count == 12);
  candidate = test_find_setup_candidate(backend,
                                        &candidates,
                                        HOMEWORLDS_CANDIDATE_SETUP_STAR,
                                        repeated_large_star);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  candidates = backend->move_builder_list_candidates(&builder);
  assert(candidates.count == 12);
  candidate = test_find_setup_candidate(backend, &candidates, HOMEWORLDS_CANDIDATE_SETUP_SHIP, small_ship);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  assert(backend->move_builder_is_complete(&builder));
  assert(backend->move_builder_build_move(&builder, &move));
  assert(move.setup_stars[0] == repeated_large_star);
  assert(move.setup_stars[1] == repeated_large_star);
  assert(move.setup_ship == small_ship);
  backend->move_builder_clear(&builder);
}

static void test_backend_move_builder_completes_turn(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  const HomeworldsMoveCandidate *selected_ship = NULL;
  const HomeworldsMoveCandidate *build = NULL;
  HomeworldsMove move = {0};

  test_prepare_position(&position);
  assert(backend->move_builder_init(&position, &builder));

  candidates = backend->move_builder_list_candidates(&builder);
  for (gsize i = 0; i < candidates.count; ++i) {
    const HomeworldsMoveCandidate *candidate = backend->move_list_get(&candidates, i);
    assert(candidate != NULL);
    if (candidate->data.kind != HOMEWORLDS_CANDIDATE_SELECT_SHIP || candidate->data.system_index != 0) {
      continue;
    }

    selected_ship = candidate;
    break;
  }
  assert(selected_ship != NULL);
  assert(backend->move_builder_step(&builder, selected_ship));
  backend->move_list_free(&candidates);

  candidates = backend->move_builder_list_candidates(&builder);
  for (gsize i = 0; i < candidates.count; ++i) {
    const HomeworldsMoveCandidate *candidate = backend->move_list_get(&candidates, i);
    assert(candidate != NULL);
    if (candidate->data.kind != HOMEWORLDS_CANDIDATE_ACTION ||
        candidate->data.target_color != HOMEWORLDS_STEP_BUILD) {
      continue;
    }

    build = candidate;
    break;
  }
  assert(build != NULL);
  assert(backend->move_builder_step(&builder, build));
  backend->move_list_free(&candidates);

  assert(backend->move_builder_is_complete(&builder));
  assert(backend->move_builder_build_move(&builder, &move));
  assert(move.step_count == 1);
  assert(move.steps[0].kind == HOMEWORLDS_STEP_BUILD);
  assert(move.steps[0].actor.system.kind == HOMEWORLDS_SYSTEM_REF_HOMEWORLD);
  assert(move.steps[0].actor.system.homeworld_side == 0);
  assert(move.steps[0].actor.ship == 0);
  assert(move.steps[0].target_color == HOMEWORLDS_COLOR_GREEN);
  backend->move_builder_clear(&builder);
}

static void test_backend_move_builder_sacrifice_build_skips_action_choice(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  HomeworldsMoveBuilderState *state = NULL;
  const HomeworldsMoveCandidate *candidate = NULL;
  HomeworldsPyramid green_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE);
  HomeworldsPyramid blue_small = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL);
  HomeworldsPyramid red_small = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL);

  test_prepare_position(&position);
  position.systems[0].ships[0][1] = blue_small;
  position.systems[2].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM);
  position.systems[2].ships[0][0] = red_small;
  homeworlds_position_rebuild_color_counts(&position);

  assert(backend->move_builder_init(&position, &builder));
  state = builder.builder_state;
  assert(state != NULL);

  candidates = backend->move_builder_list_candidates(&builder);
  candidate = test_find_ship_candidate(backend, &candidates, 0, green_large);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  candidates = backend->move_builder_list_candidates(&builder);
  candidate = test_find_action_candidate(backend, &candidates, HOMEWORLDS_STEP_SACRIFICE);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  assert(state->stage == HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP);
  assert(state->pending_actions_remaining == 3);
  assert(state->move.step_count == 1);
  assert(state->move.steps[0].kind == HOMEWORLDS_STEP_SACRIFICE);

  candidates = backend->move_builder_list_candidates(&builder);
  candidate = test_find_ship_candidate(backend, &candidates, 2, red_small);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  assert(state->stage == HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP);
  assert(state->pending_actions_remaining == 2);
  assert(state->move.step_count == 2);
  assert(state->move.steps[1].kind == HOMEWORLDS_STEP_BUILD);
  assert(state->move.steps[1].actor.ship == 0);
  assert(state->move.steps[1].target_color == HOMEWORLDS_COLOR_RED);
  assert(homeworlds_system_ship_count_for_side(&state->working_position.systems[2], 0) == 2);

  candidates = backend->move_builder_list_candidates(&builder);
  candidate = test_find_action_candidate(backend, &candidates, HOMEWORLDS_STEP_PASS);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  assert(state->stage == HOMEWORLDS_BUILDER_STAGE_COMPLETE);
  assert(state->pending_actions_remaining == 0);
  assert(state->move.step_count == 4);
  assert(state->move.steps[2].kind == HOMEWORLDS_STEP_PASS);
  assert(state->move.steps[3].kind == HOMEWORLDS_STEP_PASS);
  backend->move_builder_clear(&builder);
}

static void test_backend_move_builder_sacrifice_pass_fills_remaining_actions(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  HomeworldsPosition applied = {0};
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  HomeworldsMoveBuilderState *state = NULL;
  const HomeworldsMoveCandidate *candidate = NULL;
  HomeworldsMove move = {0};
  HomeworldsPyramid green_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE);
  char notation[128] = {0};

  test_prepare_position(&position);
  position.systems[0].ships[0][1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL);
  homeworlds_system_rebuild_color_counts(&position.systems[0]);
  applied = position;

  assert(backend->move_builder_init(&position, &builder));
  state = builder.builder_state;
  assert(state != NULL);

  candidates = backend->move_builder_list_candidates(&builder);
  candidate = test_find_ship_candidate(backend, &candidates, 0, green_large);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  candidates = backend->move_builder_list_candidates(&builder);
  candidate = test_find_action_candidate(backend, &candidates, HOMEWORLDS_STEP_SACRIFICE);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  assert(state->stage == HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP);
  assert(state->pending_actions_remaining == 3);
  assert(state->move.step_count == 1);

  candidates = backend->move_builder_list_candidates(&builder);
  candidate = test_find_action_candidate(backend, &candidates, HOMEWORLDS_STEP_PASS);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  assert(state->stage == HOMEWORLDS_BUILDER_STAGE_COMPLETE);
  assert(state->pending_actions_remaining == 0);
  assert(backend->move_builder_is_complete(&builder));
  assert(backend->move_builder_build_move(&builder, &move));
  assert(move.step_count == 4);
  assert(move.steps[0].kind == HOMEWORLDS_STEP_SACRIFICE);
  assert(move.steps[1].kind == HOMEWORLDS_STEP_PASS);
  assert(move.steps[2].kind == HOMEWORLDS_STEP_PASS);
  assert(move.steps[3].kind == HOMEWORLDS_STEP_PASS);
  assert(homeworlds_move_format(&move, notation, sizeof(notation)));
  assert(strcmp(notation, "H1g3- pass pass pass") == 0);
  assert(homeworlds_position_apply_move(&applied, &move));

  backend->move_builder_clear(&builder);
}

static void test_backend_move_builder_terminal_homeworld_catastrophe_completes_move(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  HomeworldsPosition applied = {0};
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  HomeworldsMoveBuilderState *state = NULL;
  const HomeworldsMoveCandidate *candidate = NULL;
  HomeworldsMove move = {0};
  HomeworldsPyramid red_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE);

  test_prepare_terminal_homeworld_catastrophe_position(&position);
  applied = position;

  assert(backend->move_builder_init(&position, &builder));
  state = builder.builder_state;
  assert(state != NULL);

  candidates = backend->move_builder_list_candidates(&builder);
  candidate = test_find_ship_candidate(backend, &candidates, 0, red_large);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  candidates = backend->move_builder_list_candidates(&builder);
  candidate = test_find_action_candidate(backend, &candidates, HOMEWORLDS_STEP_SACRIFICE);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  assert(state->stage == HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP);
  assert(state->pending_actions_remaining == 3);
  assert(homeworlds_move_builder_apply_catastrophe(&builder, 1, HOMEWORLDS_COLOR_RED));
  assert(state->stage == HOMEWORLDS_BUILDER_STAGE_COMPLETE);
  assert(state->pending_actions_remaining == 0);
  assert(backend->move_builder_is_complete(&builder));

  candidates = backend->move_builder_list_candidates(&builder);
  assert(candidates.count == 0);
  backend->move_list_free(&candidates);

  assert(backend->move_builder_build_move(&builder, &move));
  assert(move.step_count == 2);
  assert(move.steps[0].kind == HOMEWORLDS_STEP_SACRIFICE);
  assert(move.steps[1].kind == HOMEWORLDS_STEP_CATASTROPHE);
  assert(homeworlds_position_apply_move(&applied, &move));
  assert(applied.phase == HOMEWORLDS_PHASE_FINISHED);
  assert(homeworlds_position_outcome(&applied) == GAME_BACKEND_OUTCOME_SIDE_0_WIN);

  backend->move_builder_clear(&builder);
}

static void test_backend_move_builder_blue_sacrifice_can_retrade_result(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  const HomeworldsMoveCandidate *candidate = NULL;
  HomeworldsMoveBuilderState *state = NULL;
  HomeworldsPyramid blue_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE);
  HomeworldsPyramid red_medium = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM);
  HomeworldsPyramid green_medium = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM);
  HomeworldsPyramid green_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE);

  test_prepare_position(&position);
  position.systems[0].ships[0][1] = blue_large;
  position.systems[0].ships[0][2] = red_medium;
  homeworlds_system_rebuild_color_counts(&position.systems[0]);

  assert(backend->move_builder_init(&position, &builder));
  state = builder.builder_state;
  assert(state != NULL);

  candidates = backend->move_builder_list_candidates(&builder);
  candidate = test_find_ship_candidate(backend, &candidates, 0, blue_large);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  candidates = backend->move_builder_list_candidates(&builder);
  candidate = test_find_action_candidate(backend, &candidates, HOMEWORLDS_STEP_SACRIFICE);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  assert(state->pending_actions_remaining == 3);
  assert(state->forced_action_color == HOMEWORLDS_COLOR_BLUE);

  candidates = backend->move_builder_list_candidates(&builder);
  candidate = test_find_ship_candidate(backend, &candidates, 0, red_medium);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  candidates = backend->move_builder_list_candidates(&builder);
  candidate = test_find_trade_color_candidate(backend, &candidates, HOMEWORLDS_COLOR_GREEN);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  assert(state->stage == HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP);
  assert(state->pending_actions_remaining == 2);
  assert(state->move.step_count == 2);
  assert(state->move.steps[1].kind == HOMEWORLDS_STEP_TRADE);
  assert(state->move.steps[1].actor.ship == red_medium);
  assert(state->move.steps[1].target_color == HOMEWORLDS_COLOR_GREEN);

  candidates = backend->move_builder_list_candidates(&builder);
  candidate = test_find_ship_candidate(backend, &candidates, 0, green_medium);
  assert(candidate != NULL);
  assert(test_find_ship_candidate(backend, &candidates, 0, green_large) != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  assert(state->stage == HOMEWORLDS_BUILDER_STAGE_SELECT_TRADE_COLOR);

  backend->move_builder_clear(&builder);
}

static void test_backend_move_builder_deduplicates_discovery_stars(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  HomeworldsPyramid yellow_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE);
  HomeworldsPyramid blue_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE);
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  const HomeworldsMoveCandidate *selected_ship = NULL;
  const HomeworldsMoveCandidate *move_action = NULL;
  guint blue_large_discoveries = 0;
  gboolean seen_discovery_stars[13] = {FALSE};

  test_prepare_position(&position);
  position.systems[0].ships[0][0] = yellow_large;
  homeworlds_system_rebuild_color_counts(&position.systems[0]);
  assert(backend->move_builder_init(&position, &builder));

  candidates = backend->move_builder_list_candidates(&builder);
  for (gsize i = 0; i < candidates.count; ++i) {
    const HomeworldsMoveCandidate *candidate = backend->move_list_get(&candidates, i);

    assert(candidate != NULL);
    if (candidate->data.kind == HOMEWORLDS_CANDIDATE_SELECT_SHIP &&
        candidate->data.system_index == 0 &&
        candidate->data.pyramid == yellow_large) {
      selected_ship = candidate;
      break;
    }
  }
  assert(selected_ship != NULL);
  assert(backend->move_builder_step(&builder, selected_ship));
  backend->move_list_free(&candidates);

  candidates = backend->move_builder_list_candidates(&builder);
  for (gsize i = 0; i < candidates.count; ++i) {
    const HomeworldsMoveCandidate *candidate = backend->move_list_get(&candidates, i);

    assert(candidate != NULL);
    if (candidate->data.kind == HOMEWORLDS_CANDIDATE_ACTION &&
        candidate->data.target_color == HOMEWORLDS_STEP_MOVE) {
      move_action = candidate;
      break;
    }
  }
  assert(move_action != NULL);
  assert(backend->move_builder_step(&builder, move_action));
  backend->move_list_free(&candidates);

  candidates = backend->move_builder_list_candidates(&builder);
  for (gsize i = 0; i < candidates.count; ++i) {
    const HomeworldsMoveCandidate *candidate = backend->move_list_get(&candidates, i);

    assert(candidate != NULL);
    if (candidate->data.kind != HOMEWORLDS_CANDIDATE_MOVE_TARGET ||
        candidate->data.target_system_index != HOMEWORLDS_INVALID_INDEX) {
      continue;
    }

    assert(homeworlds_pyramid_is_valid(candidate->data.pyramid));
    assert(!seen_discovery_stars[candidate->data.pyramid]);
    seen_discovery_stars[candidate->data.pyramid] = TRUE;
    blue_large_discoveries += candidate->data.pyramid == blue_large;
  }
  assert(blue_large_discoveries == 1);
  backend->move_list_free(&candidates);
  backend->move_builder_clear(&builder);
}

static void test_backend_good_moves_are_subset_and_ordered(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  char first_text[64] = {0};
  char second_text[64] = {0};
  gint first_score = 0;

  test_prepare_position(&position);
  position.systems[1].ships[1][0] = 0;
  position.systems[0].ships[1][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL);
  homeworlds_position_rebuild_color_counts(&position);

  good_moves = backend->list_good_moves(&position, 2, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  assert(backend->format_move(backend->move_list_get(&good_moves, 0), first_text, sizeof(first_text)));
  first_score = test_score_after_move(&position, backend->move_list_get(&good_moves, 0));
  if (good_moves.count == 1) {
    assert(first_score == homeworlds_position_terminal_score(GAME_BACKEND_OUTCOME_SIDE_0_WIN, 1));
    backend->move_list_free(&good_moves);
    return;
  }
  assert(backend->format_move(backend->move_list_get(&good_moves, 1), second_text, sizeof(second_text)));
  assert(first_text[0] == 'H' || strchr("RYGB", first_text[0]) != NULL);
  assert(strcmp(first_text, second_text) != 0);
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_static_prunes_candidates(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  gint best_score = 0;

  test_prepare_static_prune_position(&position, 20);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  assert(good_moves.count <= 512);

  for (gsize i = 0; i < good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&good_moves, i);
    gint score = test_score_after_move(&position, move);

    if (i == 0) {
      best_score = score;
    } else {
      assert(score <= best_score);
    }
    assert(score >= best_score - 50);
  }

  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_zero_window_keeps_exact_best_ties(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList default_moves = {0};
  GameBackendMoveList zero_window_moves = {0};
  gint best_score = 0;
  gsize best_count = 0;

  homeworlds_position_init(&position);
  test_apply_notation(&position, "Y1G3b3");
  test_apply_notation(&position, "Y2B3g3");
  test_apply_notation(&position, "H1b+");
  test_apply_notation(&position, "H2g+");

  default_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  zero_window_moves = backend->list_good_moves(&position, 0, 0);
  assert(default_moves.count > 0);
  assert(zero_window_moves.count > 0);

  best_score = test_score_after_move(&position, backend->move_list_get(&default_moves, 0));
  for (gsize i = 0; i < default_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&default_moves, i);
    gint score = test_score_after_move(&position, move);

    if (score == best_score) {
      best_count++;
    }
  }

  assert(best_count > 1);
  assert(zero_window_moves.count == best_count);
  for (gsize i = 0; i < zero_window_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&zero_window_moves, i);

    assert(test_score_after_move(&position, move) == best_score);
  }

  backend->move_list_free(&default_moves);
  backend->move_list_free(&zero_window_moves);
  homeworlds_position_clear(&position);
}

static void test_backend_good_moves_static_prunes_player_two_candidates(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  gint best_score = 0;

  test_prepare_static_prune_position(&position, 19);
  assert(position.turn == 1);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count <= 512);

  for (gsize i = 0; i < good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&good_moves, i);
    gint score = test_score_after_move(&position, move);

    if (i == 0) {
      best_score = score;
    } else {
      assert(score >= best_score);
    }
    assert(score <= best_score + 50);
  }

  backend->move_list_free(&good_moves);
}

static void test_backend_good_move_trace_records_pruning(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  TestGoodMoveTraceCapture capture = {0};

  test_prepare_homeworld_orphaning_yellow_catastrophe_position(&position);
  homeworlds_backend_set_good_move_trace(test_capture_good_move_trace, &capture);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(capture.called);
  assert(capture.trace.pruning_checked_branches > 0);
  assert(capture.trace.pruning_window_cutoff_branches <= capture.trace.pruning_checked_branches);
  assert(capture.trace.pruning_pruned_branches <= capture.trace.pruning_checked_branches);
  assert(capture.trace.ordering_single_step_passes > 0);
  assert(capture.trace.ordering_single_step_moves > 0);
  assert(capture.trace.goal_branches_created > 0);
  assert(capture.trace.goal_branches_selected > 0);
  assert(capture.trace.goal_branches_split > 0);
  assert(capture.trace.goal_branches_direct > 0);
  assert(capture.trace.goal_branches_exhausted <= capture.trace.goal_branches_direct);
  assert(capture.goal_report != NULL);
  assert(strstr(capture.goal_report, "direct-result #0 single-steps") != NULL);
  assert(strstr(capture.goal_report, " single-step bounds=") == NULL);
  assert(strstr(capture.goal_report, "explore-result #") != NULL);
  assert(strstr(capture.goal_report, "score-split #") == NULL);
  assert(strstr(capture.goal_report, "interval_reject+=") == NULL);

  backend->move_list_free(&good_moves);
  homeworlds_backend_set_good_move_trace(NULL, NULL);
  g_free(capture.goal_report);
}

static void test_backend_good_move_trace_bounds_empty_green_sacrifice_builds(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  TestGoodMoveTraceCapture capture = {0};

  test_prepare_position(&position);
  memset(position.bank, 0, sizeof(position.bank));
  position.systems[0].ships[0][1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL);
  homeworlds_position_rebuild_color_counts(&position);
  homeworlds_backend_set_good_move_trace(test_capture_good_move_trace, &capture);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(capture.called);
  assert(capture.goal_report != NULL);
  assert(strstr(capture.goal_report, "leaves<=1 prefix=[H1g3-]") != NULL);
  assert(strstr(capture.goal_report, "leaves<=0 prefix=[H1g3-]") == NULL);

  backend->move_list_free(&good_moves);
  homeworlds_backend_set_good_move_trace(NULL, NULL);
  g_free(capture.goal_report);
}

static void test_backend_good_move_trace_bounds_red_and_blue_sacrifices(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  TestGoodMoveTraceCapture capture = {0};

  homeworlds_position_init(&position);
  position.phase = HOMEWORLDS_PHASE_PLAY;
  position.turn = 0;
  memset(position.bank, 0, sizeof(position.bank));
  memset(position.systems, 0, sizeof(position.systems));
  position.systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position.systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  homeworlds_position_rebuild_color_counts(&position);
  homeworlds_backend_set_good_move_trace(test_capture_good_move_trace, &capture);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(capture.called);
  assert(capture.goal_report != NULL);
  assert(strstr(capture.goal_report, "leaves<=1 prefix=[H1r3-]") != NULL);
  assert(strstr(capture.goal_report, "leaves<=2 prefix=[H1b3-]") != NULL);
  assert(strstr(capture.goal_report, "leaves<=0 prefix=[H1r3-]") == NULL);
  assert(strstr(capture.goal_report, "leaves<=0 prefix=[H1b3-]") == NULL);

  backend->move_list_free(&good_moves);
  homeworlds_backend_set_good_move_trace(NULL, NULL);
  g_free(capture.goal_report);
}

static void test_backend_good_move_trace_bounds_yellow_sacrifices(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  TestGoodMoveTraceCapture capture = {0};

  homeworlds_position_init(&position);
  position.phase = HOMEWORLDS_PHASE_PLAY;
  position.turn = 0;
  memset(position.systems, 0, sizeof(position.systems));
  position.systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  position.systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  homeworlds_position_rebuild_color_counts(&position);
  homeworlds_backend_set_good_move_trace(test_capture_good_move_trace, &capture);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(capture.called);
  assert(capture.goal_report != NULL);
  assert(strstr(capture.goal_report, "yellow-sacrifice") != NULL);
  assert(strstr(capture.goal_report, "prefix=[H1y2-]") != NULL);
  assert(strstr(capture.goal_report, "leaves<=unknown prefix=[H1y2-]") == NULL);
  assert(strstr(capture.goal_report, "yellow-sacrifice bounds=[-2147483648,") == NULL);
  assert(strstr(capture.goal_report, "parent_" "delta") == NULL);

  backend->move_list_free(&good_moves);
  homeworlds_backend_set_good_move_trace(NULL, NULL);
  g_free(capture.goal_report);
}

static void test_backend_good_move_trace_partitions_yellow_sacrifice_goals(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  TestGoodMoveTraceCapture capture = {0};
  gint both_min = 0;
  gint both_max = 0;
  gint red_min = 0;
  gint red_max = 0;
  gint yellow_min = 0;
  gint yellow_max = 0;
  gint none_min = 0;
  gint none_max = 0;

  test_prepare_two_goal_yellow_catastrophe_position(&position);
  homeworlds_backend_set_good_move_trace(test_capture_good_move_trace, &capture);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(capture.called);
  assert(capture.goal_report != NULL);
  assert(strstr(capture.goal_report, "goal=[S0y!+S1r!]") != NULL);
  assert(strstr(capture.goal_report, "goal=[S1r!]") != NULL);
  assert(strstr(capture.goal_report, "goal=[S0y!]") != NULL);
  assert(strstr(capture.goal_report, "goal=[no scheduled catastrophe]") != NULL);
  assert(strstr(capture.goal_report, "inside_interval+=") == NULL);
  assert(strstr(capture.goal_report, "score-split #") == NULL);
  assert(test_goal_report_find_goal_bounds(capture.goal_report, "S0y!+S1r!", &both_min, &both_max));
  assert(test_goal_report_find_goal_bounds(capture.goal_report, "S1r!", &red_min, &red_max));
  assert(test_goal_report_find_goal_bounds(capture.goal_report, "S0y!", &yellow_min, &yellow_max));
  assert(test_goal_report_find_goal_bounds(capture.goal_report,
                                           "no scheduled catastrophe",
                                           &none_min,
                                           &none_max));
  assert(both_min <= both_max);
  assert(red_min <= red_max);
  assert(yellow_min <= yellow_max);
  assert(none_min <= none_max);
  assert(both_min > none_min);
  assert(red_min > none_min);
  assert(yellow_min > none_min);

  backend->move_list_free(&good_moves);
  homeworlds_backend_set_good_move_trace(NULL, NULL);
  g_free(capture.goal_report);
}

static void test_backend_yellow_proof_subtracts_required_material(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  const HomeworldsMoveCandidate *candidate = NULL;
  const HomeworldsMoveBuilderState *state = NULL;
  HomeworldsGoodMoveProofStatus status = {0};

  test_prepare_two_goal_yellow_catastrophe_position(&position);
  assert(backend->move_builder_init(&position, &builder));

  candidates = backend->move_builder_list_candidates(&builder);
  candidate = test_find_ship_candidate(backend,
                                       &candidates,
                                       0,
                                       homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE));
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  candidates = backend->move_builder_list_candidates(&builder);
  candidate = test_find_action_candidate(backend, &candidates, HOMEWORLDS_STEP_SACRIFICE);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  state = builder.builder_state;
  assert(state != NULL);
  assert(homeworlds_backend_describe_yellow_sacrifice_proof(state, 0, 0, &status));
  assert(status.result == HOMEWORLDS_GOOD_MOVE_PROOF_KEEP);
  assert(status.pending_actions_remaining == 3);
  assert(status.catastrophe_gain == 80);
  assert(status.bound == status.current_score + status.buildable_gain + (gint)status.catastrophe_gain);

  backend->move_builder_clear(&builder);
}

static void test_backend_yellow_proof_counts_catastrophe_buildability_gain(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  HomeworldsMove move = {0};
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  const HomeworldsMoveCandidate *candidate = NULL;
  const HomeworldsMoveBuilderState *state = NULL;
  HomeworldsGoodMoveProofStatus status = {0};

  test_prepare_buildability_goal_position(&position);
  assert(homeworlds_move_parse("H2y2- S0g1>H1 S2g1>H1 H1g!", &move));
  assert(test_score_after_move(&position, &move) == -190);
  assert(backend->move_builder_init(&position, &builder));

  candidates = backend->move_builder_list_candidates(&builder);
  candidate = test_find_ship_candidate(backend,
                                       &candidates,
                                       1,
                                       homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM));
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  candidates = backend->move_builder_list_candidates(&builder);
  candidate = test_find_action_candidate(backend, &candidates, HOMEWORLDS_STEP_SACRIFICE);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  backend->move_list_free(&candidates);

  state = builder.builder_state;
  assert(state != NULL);
  assert(homeworlds_backend_describe_yellow_sacrifice_proof(state, 1, -90, &status));
  assert(status.result == HOMEWORLDS_GOOD_MOVE_PROOF_KEEP);
  assert(status.pending_actions_remaining == 2);
  assert(status.current_score == -90);
  assert(status.buildable_gain == 60);
  assert(status.catastrophe_gain == 100);
  assert(status.bound == -250);

  backend->move_builder_clear(&builder);
  homeworlds_position_clear(&position);
}

static void test_backend_good_move_trace_constrains_required_yellow_goals(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  TestGoodMoveTraceCapture capture = {0};
  gsize goal_rejects = 0;

  test_prepare_buildability_goal_position(&position);
  homeworlds_backend_set_good_move_trace(test_capture_good_move_trace, &capture);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(capture.called);
  assert(capture.goal_report != NULL);
  assert(test_goal_report_find_selected_goal_rejects(capture.goal_report, "H1g!", &goal_rejects));
  assert(goal_rejects == 0);

  backend->move_list_free(&good_moves);
  homeworlds_backend_set_good_move_trace(NULL, NULL);
  g_free(capture.goal_report);
  homeworlds_position_clear(&position);
}

static void test_backend_good_moves_constrain_required_yellow_goal_quality(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  const char *quality_prefix = "H1y3- S0r1>H1 S0r!";

  test_prepare_existing_catastrophe_quality_goal_position(&position);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  assert(test_good_moves_contains_notation_prefix(backend, &good_moves, quality_prefix));

  backend->move_list_free(&good_moves);
  homeworlds_position_clear(&position);
}

static void test_backend_good_moves_keep_buffer_when_goal_leaf_is_unreplayable(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  TestGoodMoveTraceCapture capture = {0};

  test_prepare_large_yellow_goal_report_position(&position);
  homeworlds_backend_set_good_move_trace(test_capture_good_move_trace, &capture);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(capture.called);
  assert(capture.trace.kept_moves == good_moves.count);
  assert(good_moves.count > 0);

  backend->move_list_free(&good_moves);
  homeworlds_backend_set_good_move_trace(NULL, NULL);
  g_free(capture.goal_report);
}

static void test_backend_good_move_trace_terminal_yellow_goal_is_absorbing(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  TestGoodMoveTraceCapture capture = {0};
  gint win_min = 0;
  gint win_max = 0;
  gint red_min = 0;
  gint red_max = 0;
  gint none_min = 0;
  gint none_max = 0;

  test_prepare_large_yellow_goal_report_position(&position);
  homeworlds_backend_set_good_move_trace(test_capture_good_move_trace, &capture);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(capture.called);
  assert(capture.goal_report != NULL);
  assert(good_moves.count == 1);
  assert(test_score_after_move(&position, backend->move_list_get(&good_moves, 0)) ==
         homeworlds_position_terminal_score(GAME_BACKEND_OUTCOME_SIDE_0_WIN, 1));
  assert(strstr(capture.goal_report, "goal=[H2y!+S3r!]") == NULL);
  assert(test_goal_report_find_goal_bounds(capture.goal_report, "H2y!", &win_min, &win_max));
  assert(test_goal_report_find_goal_bounds(capture.goal_report, "S3r!", &red_min, &red_max));
  assert(test_goal_report_find_goal_bounds(capture.goal_report,
                                           "no scheduled catastrophe",
                                           &none_min,
                                           &none_max));
  assert(win_min == homeworlds_position_terminal_score(GAME_BACKEND_OUTCOME_SIDE_0_WIN, 1));
  assert(win_max == win_min);
  assert(red_min == red_max);
  assert(none_min == none_max);
  assert(red_max < win_min);
  assert(none_max < red_min);

  backend->move_list_free(&good_moves);
  homeworlds_backend_set_good_move_trace(NULL, NULL);
  g_free(capture.goal_report);
}

static void test_backend_good_moves_follow_setup_policy_without_truncation(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  GameBackendMoveList player_two_good_moves = {0};
  const HomeworldsMove *player_one_move = NULL;
  guint player_one_star_sizes = 0;

  homeworlds_position_init(&position);
  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 16);
  for (gsize i = 0; i < good_moves.count; ++i) {
    test_assert_good_setup_policy(backend->move_list_get(&good_moves, i));
  }

  player_one_move = backend->move_list_get(&good_moves, 0);
  assert(player_one_move != NULL);
  player_one_star_sizes = test_setup_star_size_mask(player_one_move);
  assert(homeworlds_position_apply_move(&position, player_one_move));
  assert(position.turn == 1);

  player_two_good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(player_two_good_moves.count > 0);
  for (gsize i = 0; i < player_two_good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&player_two_good_moves, i);

    test_assert_good_setup_policy(move);
    assert(test_setup_star_size_mask(move) != player_one_star_sizes);
  }
  backend->move_list_free(&player_two_good_moves);
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_first_turn_always_builds(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};

  test_prepare_position(&position);
  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  for (gsize i = 0; i < good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&good_moves, i);

    assert(move != NULL);
    assert(move->kind == HOMEWORLDS_MOVE_KIND_TURN);
    assert(move->step_count == 1);
    assert(move->steps[0].kind == HOMEWORLDS_STEP_BUILD);
    assert(move->steps[0].actor.ship == 0);
    assert(move->steps[0].target_color <= HOMEWORLDS_COLOR_BLUE);
  }
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_use_symbolic_build_notation(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  guint green_homeworld_builds = 0;

  test_prepare_position(&position);
  position.systems[0].ships[0][1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL);
  homeworlds_system_rebuild_color_counts(&position.systems[0]);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  for (gsize i = 0; i < good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&good_moves, i);
    char notation[128] = {0};

    assert(move != NULL);
    assert(homeworlds_move_format(move, notation, sizeof(notation)));
    assert(strstr(notation, "g1+") == NULL);
    assert(strstr(notation, "g3+") == NULL);
    if (strcmp(notation, "H1g+") == 0) {
      green_homeworld_builds++;
    }
  }
  assert(green_homeworld_builds > 0);
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_skip_pass_when_other_moves_remain(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};

  test_prepare_position(&position);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  for (gsize i = 0; i < good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&good_moves, i);

    assert(move != NULL);
    assert(move->kind == HOMEWORLDS_MOVE_KIND_TURN);
    assert(move->step_count > 0);
    for (guint step = 0; step < move->step_count; ++step) {
      if (move->steps[step].kind == HOMEWORLDS_STEP_PASS) {
        assert(test_move_has_step_kind(move, HOMEWORLDS_STEP_SACRIFICE));
      }
    }
  }
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_keep_sacrifice_pass_completion(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  HomeworldsEvalWeights neutral_weights = {0};
  const char *pass_sacrifice = "H1g3- pass pass pass";

  test_prepare_position(&position);
  position.systems[0].ships[0][1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL);
  homeworlds_system_rebuild_color_counts(&position.systems[0]);
  test_assert_move_notation_is_legal(&position, pass_sacrifice);

  homeworlds_eval_weights_set_active(&neutral_weights);
  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  homeworlds_eval_weights_reset_active();
  assert(good_moves.count > 0);
  assert(test_good_moves_contains_notation(backend, &good_moves, pass_sacrifice));
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_keep_pass_when_only_move_remains(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {
    .phase = HOMEWORLDS_PHASE_PLAY,
    .turn = 0,
    .systems = {
      [0] = {
        .stars = {
          homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
          homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
        },
        .ships = {
          [0] = {
            homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
          },
        },
      },
      [1] = {
        .stars = {
          homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
          homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
        },
        .ships = {
          [1] = {
            homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
          },
        },
      },
    },
  };
  GameBackendMoveList good_moves = {0};
  const HomeworldsMove *move = NULL;
  char notation[128] = {0};

  homeworlds_position_rebuild_color_counts(&position);
  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count == 1);
  move = backend->move_list_get(&good_moves, 0);
  assert(move != NULL);
  assert(move->kind == HOMEWORLDS_MOVE_KIND_TURN);
  assert(move->step_count == 1);
  assert(move->steps[0].kind == HOMEWORLDS_STEP_PASS);
  assert(backend->format_move(move, notation, sizeof(notation)));
  assert(strcmp(notation, "pass") == 0);
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_skip_attack_without_targets(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};

  test_prepare_position(&position);
  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  for (gsize i = 0; i < good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&good_moves, i);

    assert(move != NULL);
    for (guint step = 0; step < move->step_count; ++step) {
      assert(move->steps[step].kind != HOMEWORLDS_STEP_ATTACK);
    }
  }
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_keep_last_homeworld_ship(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};

  homeworlds_position_init(&position);
  position.phase = HOMEWORLDS_PHASE_PLAY;
  position.turn = 0;
  memset(position.systems, 0, sizeof(position.systems));
  position.systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position.systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position.systems[2] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  homeworlds_position_rebuild_color_counts(&position);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  for (gsize i = 0; i < good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&good_moves, i);

    assert(move != NULL);
    for (guint step_index = 0; step_index < move->step_count; ++step_index) {
      const HomeworldsTurnStep *step = &move->steps[step_index];

      if (step->actor.system.kind != HOMEWORLDS_SYSTEM_REF_HOMEWORLD ||
          step->actor.system.homeworld_side != 0 ||
          step->actor.ship != homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE)) {
        continue;
      }
      assert(step->kind != HOMEWORLDS_STEP_SACRIFICE);
      assert(step->kind != HOMEWORLDS_STEP_MOVE);
      assert(step->kind != HOMEWORLDS_STEP_DISCOVER);
    }
  }
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_keep_last_homeworld_ship_after_yellow_sacrifice(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};

  test_prepare_player_two_yellow_sacrifice_position(&position);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  for (gsize i = 0; i < good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&good_moves, i);
    char notation[128] = {0};

    assert(move != NULL);
    assert(backend->format_move(move, notation, sizeof(notation)));
    assert(strcmp(notation, "H2y3- H2b1>S2(R3) H2b1>S3(Y3) S3b1>S4(Y2)") != 0);
  }
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_skip_redundant_yellow_sacrifice_hops(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  const char *bad_moves[] = {
    "H2y3- H2b1>S2(B3) S2b1>H2 H2b1>S2(B3)",
    "H2y3- H2b1>S2(B3) S2b1>S0 S0b1>S2(Y3)",
  };
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};

  test_prepare_player_two_yellow_sacrifice_position(&position);
  for (guint i = 0; i < G_N_ELEMENTS(bad_moves); ++i) {
    test_assert_move_notation_is_legal(&position, bad_moves[i]);
  }

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  for (guint i = 0; i < G_N_ELEMENTS(bad_moves); ++i) {
    assert(!test_good_moves_contains_notation(backend, &good_moves, bad_moves[i]));
  }
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_skip_redundant_yellow_sacrifice_intermediate(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  const char *direct_move = "H1y2- H1b1>S1 pass";
  const char *redundant_move = "H1y2- H1b1>S0 S0b1>S1";

  test_prepare_yellow_sacrifice_route_position(&position, TRUE);
  test_assert_move_notation_is_legal(&position, direct_move);
  test_assert_move_notation_is_legal(&position, redundant_move);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  assert(!test_good_moves_contains_notation(backend, &good_moves, redundant_move));
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_keep_useful_yellow_sacrifice_intermediate(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList all_moves = {0};
  const char *needed_move = "H1y2- H1b1>S0 S0b1>S1";

  test_prepare_yellow_sacrifice_route_position(&position, FALSE);
  test_assert_move_notation_is_legal(&position, needed_move);

  all_moves = homeworlds_position_list_all_moves(&position);
  assert(all_moves.count > 0);
  assert(test_good_moves_contains_notation(backend, &all_moves, needed_move));
  homeworlds_move_list_free(&all_moves);
}

static void test_backend_good_moves_order_commutative_blue_sacrifice_trades(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  HomeworldsEvalWeights neutral_weights = {0};
  const char *canonical_move = "H1b2- H1r1=g H1y1=r";
  const char *redundant_move = "H1b2- H1y1=r H1r1=g";

  test_prepare_position(&position);
  position.systems[0].ships[0][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM);
  position.systems[0].ships[0][1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL);
  position.systems[0].ships[0][2] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL);
  for (guint slot = 3; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    position.systems[0].ships[0][slot] = 0;
  }
  homeworlds_system_rebuild_color_counts(&position.systems[0]);

  test_assert_move_notation_is_legal(&position, canonical_move);
  test_assert_move_notation_is_legal(&position, redundant_move);

  /* Keep this canonicalization check independent of the default static prune window. */
  homeworlds_eval_weights_set_active(&neutral_weights);
  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  homeworlds_eval_weights_reset_active();
  assert(good_moves.count > 0);
  assert(test_good_moves_contains_notation(backend, &good_moves, canonical_move));
  assert(!test_good_moves_contains_notation(backend, &good_moves, redundant_move));
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_prune_blue_sacrifice_trade_permutation(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList all_moves = {0};
  GameBackendMoveList good_moves = {0};
  HomeworldsEvalWeights neutral_weights = {0};
  HomeworldsPyramid yellow_small = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL);
  const char *kept_move = "H1b2- H1y1=g H1g1=r";
  const char *pruned_move = "H1b2- H1y1=g H1r1=y";

  test_prepare_position(&position);
  position.systems[0].ships[0][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM);
  position.systems[0].ships[0][1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL);
  position.systems[0].ships[0][2] = yellow_small;
  for (guint slot = 3; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    position.systems[0].ships[0][slot] = 0;
  }
  homeworlds_system_rebuild_color_counts(&position.systems[0]);
  test_remove_all_from_bank(&position, yellow_small);

  test_assert_move_notation_is_legal(&position, kept_move);
  test_assert_move_notation_is_legal(&position, pruned_move);

  all_moves = homeworlds_position_list_all_moves(&position);
  assert(all_moves.count > 0);
  assert(!test_good_moves_contains_notation(backend, &all_moves, pruned_move));
  homeworlds_move_list_free(&all_moves);

  /* Keep this dependency check independent of the default static prune window. */
  homeworlds_eval_weights_set_active(&neutral_weights);
  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  homeworlds_eval_weights_reset_active();
  assert(good_moves.count > 0);
  assert(!test_good_moves_contains_notation(backend, &good_moves, pruned_move));
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_order_commutative_green_sacrifice_builds(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  const char *canonical_move = "H1g3- H1g+ H1g+ S0g+";
  const char *redundant_move = "H1g3- H1g+ S0g+ H1g+";

  test_prepare_green_sacrifice_build_order_position(&position);
  test_assert_move_notation_is_legal(&position, canonical_move);
  test_assert_move_notation_is_legal(&position, redundant_move);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  assert(test_good_moves_contains_notation(backend, &good_moves, canonical_move));
  assert(!test_good_moves_contains_notation(backend, &good_moves, redundant_move));
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_keep_bank_dependent_green_sacrifice_builds(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  const char *first_move = "H1g3- H1g+ H1g+ S0g+";
  const char *second_move = "H1g3- H1g+ S0g+ H1g+";

  test_prepare_green_sacrifice_ambiguous_build_order_position(&position);
  test_assert_move_notation_is_legal(&position, first_move);
  test_assert_move_notation_is_legal(&position, second_move);
  test_assert_move_notations_reach_different_positions(&position, first_move, second_move);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  assert(test_good_moves_contains_notation(backend, &good_moves, first_move));
  assert(test_good_moves_contains_notation(backend, &good_moves, second_move));
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_keep_dependent_green_sacrifice_builds(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  const char *dependent_move = "H1g3- H1g+ S0g+ H1g+";

  test_prepare_green_sacrifice_build_order_position(&position);
  test_remove_all_from_bank(&position, homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL));
  test_remove_all_from_bank(&position, homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM));
  test_remove_all_from_bank(&position, homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE));
  test_add_to_bank(&position, homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL));
  test_add_to_bank(&position, homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM));

  test_assert_move_notation_is_legal(&position, dependent_move);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  assert(test_good_moves_contains_notation(backend, &good_moves, dependent_move));
  backend->move_list_free(&good_moves);
}

static void test_backend_moves_canonicalize_catastrophe_before_sacrifice(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList all_moves = {0};
  GameBackendMoveList good_moves = {0};
  HomeworldsEvalWeights material_weights = {
    .ship_values = {
      [HOMEWORLDS_SIZE_SMALL] = 10,
      [HOMEWORLDS_SIZE_MEDIUM] = 20,
      [HOMEWORLDS_SIZE_LARGE] = 30,
    },
  };
  const char *canonical_move = "H1g2- S0r! H1g+ H1g+";
  const char *redundant_move = "S0r! H1g2- H1g+ H1g+";

  test_prepare_sacrifice_catastrophe_order_position(&position);
  test_assert_move_notation_is_legal(&position, canonical_move);
  test_assert_move_notation_is_legal(&position, redundant_move);
  test_assert_move_notations_reach_same_position(&position, canonical_move, redundant_move);

  all_moves = homeworlds_position_list_all_moves(&position);
  assert(all_moves.count > 0);
  assert(test_good_moves_contains_notation(backend, &all_moves, canonical_move));
  assert(!test_good_moves_contains_notation(backend, &all_moves, redundant_move));
  homeworlds_move_list_free(&all_moves);

  homeworlds_eval_weights_set_active(&material_weights);
  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  homeworlds_eval_weights_reset_active();
  assert(good_moves.count > 0);
  assert(test_good_moves_contains_notation(backend, &good_moves, canonical_move));
  assert(!test_good_moves_contains_notation(backend, &good_moves, redundant_move));
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_keep_green_medium_rebuild_representative(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  const char *direct_move = "H1r+";
  const char *direct_green_move = "S0g+";
  const char *ordered_redundant_move = "H1g2- H1r+ H1g+";
  const char *kept_sacrifice_move = "H1g2- H1g+ H1r+";
  const char *redundant_green_move = "H1g2- H1g+ S0g+";

  test_prepare_green_medium_rebuild_position(&position);
  test_assert_move_notation_is_legal(&position, direct_move);
  test_assert_move_notation_is_legal(&position, direct_green_move);
  test_assert_move_notation_is_legal(&position, ordered_redundant_move);
  test_assert_move_notation_is_legal(&position, kept_sacrifice_move);
  test_assert_move_notation_is_legal(&position, redundant_green_move);
  test_assert_move_notations_reach_different_positions(&position, direct_move, ordered_redundant_move);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  assert(test_good_moves_contains_notation(backend, &good_moves, direct_move));
  assert(test_good_moves_contains_notation(backend, &good_moves, direct_green_move));
  assert(test_good_moves_contains_notation(backend, &good_moves, kept_sacrifice_move));
  assert(!test_good_moves_contains_notation(backend, &good_moves, ordered_redundant_move));
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_skip_unsafe_build_catastrophe(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};

  homeworlds_position_init(&position);
  position.phase = HOMEWORLDS_PHASE_PLAY;
  position.turn = 0;
  memset(position.systems, 0, sizeof(position.systems));
  position.systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
      },
    },
  };
  position.systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position.systems[2] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  homeworlds_position_rebuild_color_counts(&position);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  for (gsize i = 0; i < good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&good_moves, i);

    assert(move != NULL);
    for (guint step_index = 0; step_index < move->step_count; ++step_index) {
      const HomeworldsTurnStep *step = &move->steps[step_index];

      assert(step->kind != HOMEWORLDS_STEP_BUILD ||
             step->actor.system.kind != HOMEWORLDS_SYSTEM_REF_HOMEWORLD ||
             step->actor.system.homeworld_side != 0);
    }
  }
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_skip_unfavorable_move_catastrophe(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  gboolean saw_safe_move = FALSE;

  homeworlds_position_init(&position);
  position.phase = HOMEWORLDS_PHASE_PLAY;
  position.turn = 0;
  memset(position.systems, 0, sizeof(position.systems));
  position.systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  position.systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position.systems[2] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
      },
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  position.systems[3] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  homeworlds_position_rebuild_color_counts(&position);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  for (gsize i = 0; i < good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&good_moves, i);

    assert(move != NULL);
    for (guint step_index = 0; step_index < move->step_count; ++step_index) {
      const HomeworldsTurnStep *step = &move->steps[step_index];

      if ((step->kind != HOMEWORLDS_STEP_MOVE && step->kind != HOMEWORLDS_STEP_DISCOVER) ||
          step->actor.system.kind != HOMEWORLDS_SYSTEM_REF_HOMEWORLD ||
          step->actor.system.homeworld_side != 0 ||
          step->actor.ship != homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM)) {
        continue;
      }
      assert(step->target_system.kind != HOMEWORLDS_SYSTEM_REF_SYSTEM ||
             step->target_system.system_index != 2 ||
             homeworlds_pyramid_is_valid(step->target_system.star));
      saw_safe_move = TRUE;
    }
  }
  assert(saw_safe_move);
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_keep_homeworld_orphaning_yellow_catastrophe(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  const char *winning_move = "H1y3- H1y1>H2 S0y2>H2 S1y3>H2 H2y!";

  test_prepare_homeworld_orphaning_yellow_catastrophe_position(&position);
  test_assert_move_notation_is_legal(&position, winning_move);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count == 1);
  assert(test_score_after_move(&position, backend->move_list_get(&good_moves, 0)) ==
         homeworlds_position_terminal_score(GAME_BACKEND_OUTCOME_SIDE_0_WIN, 1));
  assert(test_good_moves_contains_equivalent_notation(backend, &position, &good_moves, winning_move));
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_skip_unfavorable_trade_catastrophe(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  const char *unsafe_trade = "H2g2=y";

  test_prepare_unfavorable_trade_catastrophe_position(&position);
  test_assert_move_notation_is_legal(&position, unsafe_trade);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  assert(!test_good_moves_contains_notation(backend, &good_moves, unsafe_trade));
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_skip_redundant_small_sacrifice(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};

  homeworlds_position_init(&position);
  position.phase = HOMEWORLDS_PHASE_PLAY;
  position.turn = 0;
  memset(position.systems, 0, sizeof(position.systems));
  position.systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position.systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  homeworlds_position_rebuild_color_counts(&position);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  for (gsize i = 0; i < good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&good_moves, i);

    assert(move != NULL);
    assert(move->step_count == 0 ||
           move->steps[0].kind != HOMEWORLDS_STEP_SACRIFICE ||
           move->steps[0].actor.system.kind != HOMEWORLDS_SYSTEM_REF_HOMEWORLD ||
           move->steps[0].actor.system.homeworld_side != 0 ||
           move->steps[0].actor.ship != homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL));
  }
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_skip_green_sacrifice_unfavorable_build(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  HomeworldsPyramid green_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE);

  homeworlds_position_init(&position);
  position.phase = HOMEWORLDS_PHASE_PLAY;
  position.turn = 0;
  memset(position.systems, 0, sizeof(position.systems));
  position.systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        green_large,
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
      },
    },
  };
  position.systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  homeworlds_position_rebuild_color_counts(&position);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  for (gsize i = 0; i < good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&good_moves, i);

    assert(move != NULL);
    assert(move->step_count == 0 ||
           move->steps[0].kind != HOMEWORLDS_STEP_SACRIFICE ||
           move->steps[0].actor.system.kind != HOMEWORLDS_SYSTEM_REF_HOMEWORLD ||
           move->steps[0].actor.system.homeworld_side != 0 ||
           move->steps[0].actor.ship != green_large);
  }
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_count_immediate_catastrophe_buildability_gain(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  HomeworldsMove catastrophe = {0};
  GameBackendMoveList good_moves = {0};
  gint before_score = 0;

  homeworlds_position_init(&position);
  position.phase = HOMEWORLDS_PHASE_PLAY;
  position.turn = 0;
  memset(position.systems, 0, sizeof(position.systems));
  position.systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position.systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position.systems[2] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
      },
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
      },
    },
  };
  homeworlds_position_rebuild_color_counts(&position);

  assert(homeworlds_move_parse("S0r!", &catastrophe));
  before_score = homeworlds_position_evaluate_static(&position);
  assert(test_score_after_move(&position, &catastrophe) == before_score + 30);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  for (gsize i = 0; i < good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&good_moves, i);

    assert(move != NULL);
    assert(test_move_has_catastrophe_at(&position, move, 2, HOMEWORLDS_COLOR_RED));
  }
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_trigger_initial_profitable_catastrophe_anywhere(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  gboolean saw_late_catastrophe = FALSE;

  homeworlds_position_init(&position);
  position.phase = HOMEWORLDS_PHASE_PLAY;
  position.turn = 0;
  memset(position.systems, 0, sizeof(position.systems));
  position.systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position.systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  position.systems[2] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      },
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };
  homeworlds_position_rebuild_color_counts(&position);

  good_moves = backend->list_good_moves(&position, 0, GAME_BACKEND_DEFAULT_GOOD_MOVE_SCORE_WINDOW);
  assert(good_moves.count > 0);
  for (gsize i = 0; i < good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&good_moves, i);

    assert(move != NULL);
    assert(move->kind == HOMEWORLDS_MOVE_KIND_TURN);
    assert(move->step_count > 0);
    assert(test_move_has_catastrophe_at(&position, move, 2, HOMEWORLDS_COLOR_RED));
    saw_late_catastrophe = saw_late_catastrophe ||
                            !test_step_is_catastrophe_at(&position,
                                                         &move->steps[0],
                                                         2,
                                                         HOMEWORLDS_COLOR_RED);
  }
  assert(saw_late_catastrophe);
  backend->move_list_free(&good_moves);
}

static void test_backend_all_moves_trigger_new_profitable_catastrophe_immediately(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList all_moves = {0};
  gboolean saw_build = FALSE;

  homeworlds_position_init(&position);
  position.phase = HOMEWORLDS_PHASE_PLAY;
  position.turn = 0;
  memset(position.systems, 0, sizeof(position.systems));
  position.systems[0] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
    },
    .ships = {
      [0] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
      },
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM),
      },
    },
  };
  position.systems[1] = (HomeworldsSystem){
    .stars = {
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE),
    },
    .ships = {
      [1] = {
        homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
      },
    },
  };
  homeworlds_position_rebuild_color_counts(&position);

  all_moves = homeworlds_position_list_all_moves(&position);
  assert(all_moves.count > 0);
  for (gsize i = 0; i < all_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&all_moves, i);
    guint system_index = HOMEWORLDS_INVALID_INDEX;

    assert(move != NULL);
    if (move->step_count == 0 ||
        move->steps[0].kind != HOMEWORLDS_STEP_BUILD ||
        move->steps[0].actor.system.kind != HOMEWORLDS_SYSTEM_REF_HOMEWORLD ||
        move->steps[0].actor.system.homeworld_side != 0 ||
        move->steps[0].target_color != HOMEWORLDS_COLOR_GREEN) {
      continue;
    }

    if (move->step_count < 2 ||
        move->steps[1].kind != HOMEWORLDS_STEP_CATASTROPHE ||
        move->steps[1].target_color != HOMEWORLDS_COLOR_GREEN) {
      continue;
    }

    saw_build = TRUE;
    assert(homeworlds_position_resolve_system_ref(&position, &move->steps[1].target_system, &system_index));
    assert(system_index == 0);
  }
  assert(saw_build);
  homeworlds_move_list_free(&all_moves);
}

static void test_backend_moving_last_ship_out_of_homeworld_loses_immediately(void) {
  HomeworldsPosition position = {
    .phase = HOMEWORLDS_PHASE_PLAY,
    .turn = 0,
    .systems = {
      [0] = {
        .stars = {
          homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
          homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
        },
        .ships = {
          [0] = {
            homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
          },
        },
      },
      [1] = {
        .stars = {
          homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
          homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
        },
        .ships = {
          [1] = {
            homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
          },
        },
      },
      [2] = {
        .stars = {
          homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE),
        },
      },
    },
  };
  HomeworldsMove move = {
    .kind = HOMEWORLDS_MOVE_KIND_TURN,
    .step_count = 1,
    .steps = {
      {
        .kind = HOMEWORLDS_STEP_MOVE,
        .actor = test_ship_ref(test_homeworld_ref(0), HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
        .target_system = test_system_ref(2),
      },
    },
  };

  homeworlds_position_rebuild_color_counts(&position);
  assert(homeworlds_position_apply_move(&position, &move));
  assert(position.phase == HOMEWORLDS_PHASE_FINISHED);
  assert(position.turn == 0);
  assert(homeworlds_position_outcome(&position) == GAME_BACKEND_OUTCOME_SIDE_1_WIN);
}

static void test_backend_destroying_opponent_homeworld_wins_immediately(void) {
  HomeworldsPosition position = {
    .phase = HOMEWORLDS_PHASE_PLAY,
    .turn = 0,
    .systems = {
      [0] = {
        .stars = {
          homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL),
        },
        .ships = {
          [0] = {
            homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL),
          },
        },
      },
      [1] = {
        .stars = {
          homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM),
        },
        .ships = {
          [0] = {
            homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
          },
          [1] = {
            homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
          },
        },
      },
    },
  };
  HomeworldsMove move = {
    .kind = HOMEWORLDS_MOVE_KIND_TURN,
    .step_count = 1,
    .steps = {
      {
        .kind = HOMEWORLDS_STEP_ATTACK,
        .actor = test_ship_ref(test_homeworld_ref(1), HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
        .target_ship = test_ship_ref(test_homeworld_ref(1), HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
      },
    },
  };

  homeworlds_position_rebuild_color_counts(&position);
  assert(homeworlds_position_apply_move(&position, &move));
  assert(position.phase == HOMEWORLDS_PHASE_FINISHED);
  assert(position.turn == 1);
  assert(homeworlds_position_outcome(&position) == GAME_BACKEND_OUTCOME_SIDE_0_WIN);
}

int main(void) {
  test_backend_metadata();
  test_backend_move_equality_uses_symbolic_build_identity();
  test_backend_move_equivalence_uses_resulting_position();
  test_backend_move_codec_roundtrips_setup_and_turn();
  test_backend_sgf_snapshot_roundtrips_position();
  test_backend_sgf_snapshot_rejects_duplicate_systems();
  test_backend_sgf_snapshot_rejects_ship_without_star();
  test_backend_sgf_snapshot_rejects_star_without_ship();
  test_backend_sgf_snapshot_rejects_loose_numbers();
  test_backend_sgf_snapshot_rejects_overfull_supply();
  test_backend_move_builder_completes_setup();
  test_backend_move_builder_setup_accepts_all_pyramid_sizes();
  test_backend_move_builder_completes_turn();
  test_backend_move_builder_sacrifice_build_skips_action_choice();
  test_backend_move_builder_sacrifice_pass_fills_remaining_actions();
  test_backend_move_builder_terminal_homeworld_catastrophe_completes_move();
  test_backend_move_builder_blue_sacrifice_can_retrade_result();
  test_backend_move_builder_deduplicates_discovery_stars();
  test_backend_good_moves_are_subset_and_ordered();
  test_backend_good_moves_static_prunes_candidates();
  test_backend_good_moves_zero_window_keeps_exact_best_ties();
  test_backend_good_moves_static_prunes_player_two_candidates();
  test_backend_good_move_trace_records_pruning();
  test_backend_good_move_trace_bounds_empty_green_sacrifice_builds();
  test_backend_good_move_trace_bounds_red_and_blue_sacrifices();
  test_backend_good_move_trace_bounds_yellow_sacrifices();
  test_backend_good_move_trace_partitions_yellow_sacrifice_goals();
  test_backend_yellow_proof_subtracts_required_material();
  test_backend_yellow_proof_counts_catastrophe_buildability_gain();
  test_backend_good_move_trace_constrains_required_yellow_goals();
  test_backend_good_moves_constrain_required_yellow_goal_quality();
  test_backend_good_moves_keep_buffer_when_goal_leaf_is_unreplayable();
  test_backend_good_move_trace_terminal_yellow_goal_is_absorbing();
  test_backend_good_moves_follow_setup_policy_without_truncation();
  test_backend_good_moves_first_turn_always_builds();
  test_backend_good_moves_use_symbolic_build_notation();
  test_backend_good_moves_skip_pass_when_other_moves_remain();
  test_backend_good_moves_keep_sacrifice_pass_completion();
  test_backend_good_moves_keep_pass_when_only_move_remains();
  test_backend_good_moves_skip_attack_without_targets();
  test_backend_good_moves_keep_last_homeworld_ship();
  test_backend_good_moves_keep_last_homeworld_ship_after_yellow_sacrifice();
  test_backend_good_moves_skip_redundant_yellow_sacrifice_hops();
  test_backend_good_moves_skip_redundant_yellow_sacrifice_intermediate();
  test_backend_good_moves_keep_useful_yellow_sacrifice_intermediate();
  test_backend_good_moves_order_commutative_blue_sacrifice_trades();
  test_backend_good_moves_prune_blue_sacrifice_trade_permutation();
  test_backend_good_moves_order_commutative_green_sacrifice_builds();
  test_backend_good_moves_keep_bank_dependent_green_sacrifice_builds();
  test_backend_good_moves_keep_dependent_green_sacrifice_builds();
  test_backend_moves_canonicalize_catastrophe_before_sacrifice();
  test_backend_good_moves_keep_green_medium_rebuild_representative();
  test_backend_good_moves_skip_unsafe_build_catastrophe();
  test_backend_good_moves_skip_unfavorable_move_catastrophe();
  test_backend_good_moves_keep_homeworld_orphaning_yellow_catastrophe();
  test_backend_good_moves_skip_unfavorable_trade_catastrophe();
  test_backend_good_moves_skip_redundant_small_sacrifice();
  test_backend_good_moves_skip_green_sacrifice_unfavorable_build();
  test_backend_good_moves_count_immediate_catastrophe_buildability_gain();
  test_backend_good_moves_trigger_initial_profitable_catastrophe_anywhere();
  test_backend_all_moves_trigger_new_profitable_catastrophe_immediately();
  test_backend_moving_last_ship_out_of_homeworld_loses_immediately();
  test_backend_destroying_opponent_homeworld_wins_immediately();
  return 0;
}
