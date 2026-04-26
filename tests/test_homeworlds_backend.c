#include <assert.h>
#include <string.h>

#include "../src/games/homeworlds/homeworlds_backend.h"
#include "../src/games/homeworlds/homeworlds_game.h"
#include "../src/games/homeworlds/homeworlds_move_builder.h"

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

static void test_backend_metadata(void) {
  const GameBackend *backend = &homeworlds_game_backend;

  assert(strcmp(backend->id, "homeworlds") == 0);
  assert(strcmp(backend->display_name, "Homeworlds") == 0);
  assert(!backend->supports_move_list);
  assert(backend->supports_move_builder);
  assert(backend->supports_ai_search);
  assert(backend->list_good_moves != NULL);
  assert(strcmp(backend->side_label(0), "Player 1") == 0);
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
  assert(strstr(first_text, "attack") != NULL);
  assert(strcmp(first_text, second_text) != 0);
  backend->move_list_free(&good_moves);
}

int main(void) {
  test_backend_metadata();
  test_backend_move_builder_completes_setup();
  test_backend_move_builder_completes_turn();
  test_backend_good_moves_are_subset_and_ordered();
  return 0;
}
