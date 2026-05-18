#include <assert.h>
#include <string.h>

#include "../src/games/homeworlds/homeworlds_backend.h"
#include "../src/games/homeworlds/homeworlds_game.h"
#include "../src/games/homeworlds/homeworlds_move_builder.h"
#include "../src/sgf_tree.h"

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

static gboolean test_setup_includes_green(const HomeworldsMove *move) {
  assert(move != NULL);
  assert(move->kind == HOMEWORLDS_MOVE_KIND_SETUP);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    if (homeworlds_pyramid_color(move->setup_stars[i]) == HOMEWORLDS_COLOR_GREEN) {
      return TRUE;
    }
  }

  return homeworlds_pyramid_color(move->setup_ship) == HOMEWORLDS_COLOR_GREEN;
}

static void test_assert_good_setup_policy(const HomeworldsMove *move, gboolean require_green) {
  gboolean seen_colors[4] = {FALSE};

  assert(move != NULL);
  assert(move->kind == HOMEWORLDS_MOVE_KIND_SETUP);
  assert(homeworlds_pyramid_size(move->setup_ship) == HOMEWORLDS_SIZE_LARGE);
  assert(!require_green || test_setup_includes_green(move));
  assert(test_setup_star_size_mask(move) != 0);
  assert((test_setup_star_size_mask(move) & (test_setup_star_size_mask(move) - 1)) != 0);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    HomeworldsColor color = homeworlds_pyramid_color(move->setup_stars[i]);
    assert(!seen_colors[color]);
    seen_colors[color] = TRUE;
  }

  HomeworldsColor ship_color = homeworlds_pyramid_color(move->setup_ship);
  assert(!seen_colors[ship_color]);
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
  char notation[128] = {0};
  HomeworldsMove parsed = {0};

  test_backend_move_roundtrips(&setup);
  test_backend_move_roundtrips(&turn);
  assert(homeworlds_move_format(&setup, notation, sizeof(notation)));
  assert(strcmp(notation, "R1B2g3") == 0);
  assert(homeworlds_move_format(&turn, notation, sizeof(notation)));
  assert(strcmp(notation, "H1 g3-/H1 r3=g") == 0);
  assert(homeworlds_move_parse("Y2B1g3", &parsed));
  assert(homeworlds_move_format(&parsed, notation, sizeof(notation)));
  assert(strcmp(notation, "Y2B1g3") == 0);
  assert(homeworlds_move_parse("G3 y2>G2 G3 y!", &parsed));
  assert(homeworlds_move_format(&parsed, notation, sizeof(notation)));
  assert(strcmp(notation, "G3 y2>G2 G3 y!") == 0);
  assert(homeworlds_move_parse("pass", &parsed));
  assert(homeworlds_move_format(&parsed, notation, sizeof(notation)));
  assert(strcmp(notation, "pass") == 0);
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

  test_prepare_position(&position);
  position.systems[1].ships[1][0] = 0;
  position.systems[0].ships[1][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL);

  good_moves = backend->list_good_moves(&position, 2);
  assert(good_moves.count >= 2);
  assert(backend->format_move(backend->move_list_get(&good_moves, 0), first_text, sizeof(first_text)));
  assert(backend->format_move(backend->move_list_get(&good_moves, 1), second_text, sizeof(second_text)));
  assert(first_text[0] == 'H' || strchr("RYGB", first_text[0]) != NULL);
  assert(strcmp(first_text, second_text) != 0);
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_follow_setup_policy_without_truncation(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
  GameBackendMoveList player_two_good_moves = {0};
  const HomeworldsMove *player_one_move = NULL;
  guint player_one_star_sizes = 0;

  homeworlds_position_init(&position);
  good_moves = backend->list_good_moves(&position, 0);
  assert(good_moves.count > 16);
  for (gsize i = 0; i < good_moves.count; ++i) {
    test_assert_good_setup_policy(backend->move_list_get(&good_moves, i), TRUE);
  }

  player_one_move = backend->move_list_get(&good_moves, 0);
  assert(player_one_move != NULL);
  player_one_star_sizes = test_setup_star_size_mask(player_one_move);
  assert(homeworlds_position_apply_move(&position, player_one_move));
  assert(position.turn == 1);

  player_two_good_moves = backend->list_good_moves(&position, 0);
  assert(player_two_good_moves.count > 0);
  for (gsize i = 0; i < player_two_good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&player_two_good_moves, i);

    test_assert_good_setup_policy(move, FALSE);
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
  good_moves = backend->list_good_moves(&position, 0);
  assert(good_moves.count > 0);
  for (gsize i = 0; i < good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&good_moves, i);

    assert(move != NULL);
    assert(move->kind == HOMEWORLDS_MOVE_KIND_TURN);
    assert(move->step_count == 1);
    assert(move->steps[0].kind == HOMEWORLDS_STEP_BUILD);
  }
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_never_pass(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};

  test_prepare_position(&position);

  good_moves = backend->list_good_moves(&position, 0);
  assert(good_moves.count > 0);
  for (gsize i = 0; i < good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&good_moves, i);

    assert(move != NULL);
    assert(move->kind == HOMEWORLDS_MOVE_KIND_TURN);
    assert(move->step_count > 0);
    for (guint step = 0; step < move->step_count; ++step) {
      assert(move->steps[step].kind != HOMEWORLDS_STEP_PASS);
    }
  }
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_return_empty_when_only_pass_is_available(void) {
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

  good_moves = backend->list_good_moves(&position, 0);
  assert(good_moves.count == 0);
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_skip_attack_without_targets(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};

  test_prepare_position(&position);
  good_moves = backend->list_good_moves(&position, 0);
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

  good_moves = backend->list_good_moves(&position, 0);
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

  good_moves = backend->list_good_moves(&position, 0);
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

static void test_backend_good_moves_start_with_profitable_catastrophe(void) {
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

  good_moves = backend->list_good_moves(&position, 0);
  assert(good_moves.count > 0);
  for (gsize i = 0; i < good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&good_moves, i);
    guint system_index = HOMEWORLDS_INVALID_INDEX;

    assert(move != NULL);
    assert(move->kind == HOMEWORLDS_MOVE_KIND_TURN);
    assert(move->step_count > 0);
    assert(move->steps[0].kind == HOMEWORLDS_STEP_CATASTROPHE);
    assert(move->steps[0].target_color == HOMEWORLDS_COLOR_RED);
    assert(homeworlds_position_resolve_system_ref(&position, &move->steps[0].target_system, &system_index));
    assert(system_index == 2);
  }
  backend->move_list_free(&good_moves);
}

static void test_backend_good_moves_trigger_new_profitable_catastrophe_immediately(void) {
  const GameBackend *backend = &homeworlds_game_backend;
  HomeworldsPosition position = {0};
  GameBackendMoveList good_moves = {0};
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

  good_moves = backend->list_good_moves(&position, 0);
  assert(good_moves.count > 0);
  for (gsize i = 0; i < good_moves.count; ++i) {
    const HomeworldsMove *move = backend->move_list_get(&good_moves, i);
    guint system_index = HOMEWORLDS_INVALID_INDEX;

    assert(move != NULL);
    if (move->step_count == 0 ||
        move->steps[0].kind != HOMEWORLDS_STEP_BUILD ||
        move->steps[0].actor.system.kind != HOMEWORLDS_SYSTEM_REF_HOMEWORLD ||
        move->steps[0].actor.system.homeworld_side != 0 ||
        move->steps[0].actor.ship != homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE)) {
      continue;
    }

    saw_build = TRUE;
    assert(move->step_count >= 2);
    assert(move->steps[1].kind == HOMEWORLDS_STEP_CATASTROPHE);
    assert(move->steps[1].target_color == HOMEWORLDS_COLOR_GREEN);
    assert(homeworlds_position_resolve_system_ref(&position, &move->steps[1].target_system, &system_index));
    assert(system_index == 0);
  }
  assert(saw_build);
  backend->move_list_free(&good_moves);
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
        .target_system = test_star_ref(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE, 0),
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
    .step_count = 1,
    .steps = {
      {
        .kind = HOMEWORLDS_STEP_ATTACK,
        .actor = test_ship_ref(test_homeworld_ref(1), HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE),
        .target_ship = test_ship_ref(test_homeworld_ref(1), HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
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
  test_backend_move_builder_deduplicates_discovery_stars();
  test_backend_good_moves_are_subset_and_ordered();
  test_backend_good_moves_follow_setup_policy_without_truncation();
  test_backend_good_moves_first_turn_always_builds();
  test_backend_good_moves_never_pass();
  test_backend_good_moves_return_empty_when_only_pass_is_available();
  test_backend_good_moves_skip_attack_without_targets();
  test_backend_good_moves_keep_last_homeworld_ship();
  test_backend_good_moves_skip_unsafe_build_catastrophe();
  test_backend_good_moves_start_with_profitable_catastrophe();
  test_backend_good_moves_trigger_new_profitable_catastrophe_immediately();
  test_backend_moving_last_ship_out_of_homeworld_loses_immediately();
  test_backend_destroying_opponent_homeworld_wins_immediately();
  return 0;
}
