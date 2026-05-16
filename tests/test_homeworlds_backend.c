#include <assert.h>
#include <string.h>

#include "../src/games/homeworlds/homeworlds_backend.h"
#include "../src/games/homeworlds/homeworlds_game.h"
#include "../src/games/homeworlds/homeworlds_move_builder.h"
#include "../src/games/homeworlds/homeworlds_random_ai.h"
#include "../src/sgf_tree.h"

static HomeworldsMove test_setup_move(guint side,
                                      HomeworldsPyramid first_star,
                                      HomeworldsPyramid second_star,
                                      HomeworldsPyramid ship) {
  HomeworldsMove move = {0};

  move.kind = HOMEWORLDS_MOVE_KIND_SETUP;
  move.acting_side = side;
  move.setup_stars[0] = first_star;
  move.setup_stars[1] = second_star;
  move.setup_ship = ship;
  return move;
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

static void test_backend_metadata(void) {
  const GameBackend *backend = &homeworlds_game_backend;

  assert(strcmp(backend->id, "homeworlds") == 0);
  assert(strcmp(backend->display_name, "Homeworlds") == 0);
  assert(!backend->supports_move_list);
  assert(backend->supports_move_builder);
  assert(backend->supports_ai_search);
  assert(backend->list_good_moves != NULL);
  assert(backend->parse_move != NULL);
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

static void test_backend_move_codec_roundtrips_setup_and_turn(void) {
  HomeworldsMove setup = test_setup_move(0,
                                         homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
                                         homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
                                         homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE));
  HomeworldsMove turn = {
    .kind = HOMEWORLDS_MOVE_KIND_TURN,
    .acting_side = 0,
    .step_count = 2,
    .steps = {
      {
        .kind = HOMEWORLDS_STEP_SACRIFICE,
        .system_index = 0,
        .ship_owner = 0,
        .ship_slot = 0,
      },
      {
        .kind = HOMEWORLDS_STEP_TRADE,
        .system_index = 0,
        .ship_owner = 0,
        .ship_slot = 1,
        .target_color = HOMEWORLDS_COLOR_GREEN,
      },
    },
  };

  test_backend_move_roundtrips(&setup);
  test_backend_move_roundtrips(&turn);
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
  saved.bank[0] = 0;

  tree = sgf_tree_new();
  assert(SGF_IS_TREE(tree));
  root = (SgfNode *)sgf_tree_get_root(tree);
  assert(root != NULL);

  assert(backend->sgf_write_position_node(&saved, root, NULL));
  homeworlds_position_init(&loaded);
  assert(backend->sgf_apply_setup_node(&loaded, root, NULL));
  assert(memcmp(&saved, &loaded, sizeof(saved)) == 0);
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
  const HomeworldsMoveCandidate *construct = NULL;
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
        candidate->data.target_color != HOMEWORLDS_STEP_CONSTRUCT) {
      continue;
    }

    construct = candidate;
    break;
  }
  assert(construct != NULL);
  assert(backend->move_builder_step(&builder, construct));
  backend->move_list_free(&candidates);

  assert(backend->move_builder_is_complete(&builder));
  assert(backend->move_builder_build_move(&builder, &move));
  assert(move.step_count == 1);
  assert(move.steps[0].kind == HOMEWORLDS_STEP_CONSTRUCT);
  backend->move_builder_clear(&builder);
}

static void test_backend_good_moves_are_subset_and_ordered(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  char first_text[64] = {0};
  char second_text[64] = {0};

  test_prepare_position(&position);
  position.systems[1].ships[1][0] = 0;
  position.systems[0].ships[1][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL);

  good_moves = backend->list_good_moves(&position, 2, 2);
  assert(good_moves.count == 2);
  assert(backend->format_move(backend->move_list_get(&good_moves, 0), first_text, sizeof(first_text)));
  assert(backend->format_move(backend->move_list_get(&good_moves, 1), second_text, sizeof(second_text)));
  assert(first_text[0] == 'T');
  assert(strcmp(first_text, second_text) != 0);
  backend->move_list_free(&good_moves);
}

static void test_backend_random_ai_builds_setup_move(void) {
  HomeworldsPosition position = {0};
  HomeworldsMove move = {0};
  GRand *rand = g_rand_new_with_seed(11);

  homeworlds_position_init(&position);
  assert(homeworlds_random_ai_build_move(&position, rand, &move));
  assert(move.kind == HOMEWORLDS_MOVE_KIND_SETUP);
  assert(homeworlds_position_apply_move(&position, &move));
  assert(position.turn == 1);

  g_rand_free(rand);
}

static void test_backend_random_ai_never_passes(void) {
  HomeworldsPosition position = {0};

  test_prepare_position(&position);
  for (guint seed = 0; seed < 64; ++seed) {
    HomeworldsPosition copy = position;
    HomeworldsMove move = {0};
    GRand *rand = g_rand_new_with_seed(seed);

    assert(homeworlds_random_ai_build_move(&copy, rand, &move));
    assert(move.kind == HOMEWORLDS_MOVE_KIND_TURN);
    assert(move.step_count > 0);
    for (guint step = 0; step < move.step_count; ++step) {
      assert(move.steps[step].kind != HOMEWORLDS_STEP_PASS);
    }
    assert(homeworlds_position_apply_move(&copy, &move));
    g_rand_free(rand);
  }
}

static void test_backend_random_ai_skips_attack_without_targets(void) {
  HomeworldsPosition position = {0};

  test_prepare_position(&position);
  for (guint seed = 0; seed < 64; ++seed) {
    HomeworldsPosition copy = position;
    HomeworldsMove move = {0};
    GRand *rand = g_rand_new_with_seed(seed);

    assert(homeworlds_random_ai_build_move(&copy, rand, &move));
    for (guint step = 0; step < move.step_count; ++step) {
      assert(move.steps[step].kind != HOMEWORLDS_STEP_ATTACK);
    }
    assert(homeworlds_position_apply_move(&copy, &move));
    g_rand_free(rand);
  }
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
    .acting_side = 0,
    .step_count = 1,
    .steps = {
      {
        .kind = HOMEWORLDS_STEP_MOVE,
        .system_index = 0,
        .ship_owner = 0,
        .ship_slot = 0,
        .target_system_index = 2,
      },
    },
  };

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
    .acting_side = 0,
    .step_count = 1,
    .steps = {
      {
        .kind = HOMEWORLDS_STEP_ATTACK,
        .system_index = 1,
        .ship_owner = 0,
        .ship_slot = 0,
        .target_ship_owner = 1,
        .target_ship_slot = 0,
      },
    },
  };

  assert(homeworlds_position_apply_move(&position, &move));
  assert(position.phase == HOMEWORLDS_PHASE_FINISHED);
  assert(position.turn == 1);
  assert(homeworlds_position_outcome(&position) == GAME_BACKEND_OUTCOME_SIDE_0_WIN);
}

int main(void) {
  test_backend_metadata();
  test_backend_move_codec_roundtrips_setup_and_turn();
  test_backend_sgf_snapshot_roundtrips_position();
  test_backend_move_builder_completes_setup();
  test_backend_move_builder_setup_accepts_all_pyramid_sizes();
  test_backend_move_builder_completes_turn();
  test_backend_good_moves_are_subset_and_ordered();
  test_backend_random_ai_builds_setup_move();
  test_backend_random_ai_never_passes();
  test_backend_random_ai_skips_attack_without_targets();
  test_backend_moving_last_ship_out_of_homeworld_loses_immediately();
  test_backend_destroying_opponent_homeworld_wins_immediately();
  return 0;
}
