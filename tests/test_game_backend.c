#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/active_game_backend.h"

static void test_backend_metadata(void) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  assert(backend != NULL);

#if defined(GGAME_GAME_CHECKERS)
  assert(strcmp(backend->id, "checkers") == 0);
  assert(strcmp(backend->display_name, "Checkers") == 0);
  assert(backend->variant_count == 3);
  assert(backend->supports_move_list);
  assert(!backend->supports_move_builder);
  assert(backend->supports_ai_search);
  assert(backend->list_good_moves != NULL);

  const GameBackendVariant *american = backend->variant_at(0);
  assert(american != NULL);
  assert(strcmp(american->short_name, "american") == 0);

  const GameBackendVariant *russian = backend->variant_by_short_name("russian");
  assert(russian != NULL);
  assert(strcmp(russian->name, "Russian (8x8)") == 0);
#elif defined(GGAME_GAME_HOMEWORLDS)
  assert(strcmp(backend->id, "homeworlds") == 0);
  assert(strcmp(backend->display_name, "Homeworlds") == 0);
  assert(backend->variant_count == 0);
  assert(!backend->supports_move_list);
  assert(backend->supports_move_builder);
  assert(backend->supports_ai_search);
  assert(backend->list_good_moves != NULL);
  assert(strcmp(backend->side_label(0), "Player 1") == 0);
  assert(strcmp(backend->side_label(1), "Player 2") == 0);
#else
#error "Add metadata expectations for the selected backend."
#endif
}

static void test_backend_position_and_move_flow(void) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  assert(backend != NULL);

#if defined(GGAME_GAME_CHECKERS)
  assert(backend->supports_move_list);

  gpointer position = g_malloc0(backend->position_size);
  assert(position != NULL);

  const GameBackendVariant *american = backend->variant_by_short_name("american");
  assert(american != NULL);
  backend->position_init(position, american);

  GameBackendMoveList moves = backend->list_moves(position);
  assert(moves.count > 0);

  const void *first_move = backend->move_list_get(&moves, 0);
  assert(first_move != NULL);

  char notation[32] = {0};
  assert(backend->format_move(first_move, notation, sizeof(notation)));
  assert(notation[0] != '\0');

  gpointer move_copy = g_malloc0(backend->move_size);
  assert(move_copy != NULL);
  memcpy(move_copy, first_move, backend->move_size);
  assert(backend->moves_equal(first_move, move_copy));
  assert(backend->apply_move(position, move_copy));

  backend->move_list_free(&moves);
  backend->position_clear(position);
  g_free(move_copy);
  g_free(position);
#elif defined(GGAME_GAME_HOMEWORLDS)
  assert(!backend->supports_move_list);
  assert(backend->supports_move_builder);
  assert(backend->move_builder_init != NULL);
  assert(backend->move_builder_clear != NULL);
  assert(backend->move_builder_list_candidates != NULL);
  assert(backend->move_builder_step != NULL);
  assert(backend->move_builder_is_complete != NULL);
  assert(backend->move_builder_build_move != NULL);

  gpointer position = g_malloc0(backend->position_size);
  assert(position != NULL);
  backend->position_init(position, NULL);
  assert(backend->position_outcome(position) == GAME_BACKEND_OUTCOME_ONGOING);
  assert(backend->position_turn(position) == 0);

  GameBackendMoveBuilder builder = {0};
  assert(backend->move_builder_init(position, &builder));
  GameBackendMoveList candidates = backend->move_builder_list_candidates(&builder);
  assert(candidates.count > 0);
  backend->move_list_free(&candidates);
  assert(!backend->move_builder_is_complete(&builder));
  backend->move_builder_clear(&builder);

  GameBackendMoveList good_moves = backend->list_good_moves(position, 4, 1);
  assert(good_moves.count > 0);
  backend->move_list_free(&good_moves);
  backend->position_clear(position);
  g_free(position);
#else
#error "Add move-flow expectations for the selected backend."
#endif
}

static void test_backend_move_path_length_only_query(void) {
#if !defined(GGAME_GAME_CHECKERS)
  return;
#else
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  assert(backend != NULL);
  assert(backend->square_grid_move_get_path != NULL);

  gpointer position = g_malloc0(backend->position_size);
  assert(position != NULL);

  const GameBackendVariant *american = backend->variant_by_short_name("american");
  assert(american != NULL);
  backend->position_init(position, american);

  GameBackendMoveList moves = backend->list_moves(position);
  assert(moves.count > 0);

  gboolean found_simple_move = FALSE;
  for (gsize i = 0; i < moves.count; ++i) {
    const void *move = backend->move_list_get(&moves, i);
    assert(move != NULL);

    guint move_length = 0;
    assert(backend->square_grid_move_get_path(move, &move_length, NULL, 0));
    if (move_length != 2) {
      continue;
    }

    guint path[2] = {0};
    assert(backend->square_grid_move_get_path(move, &move_length, path, G_N_ELEMENTS(path)));
    assert(move_length == 2);
    assert(path[0] != path[1]);
    found_simple_move = TRUE;
    break;
  }

  assert(found_simple_move);

  backend->move_list_free(&moves);
  backend->position_clear(position);
  g_free(position);
#endif
}

int main(void) {
  test_backend_metadata();
  test_backend_position_and_move_flow();
  test_backend_move_path_length_only_query();

  printf("All tests passed.\n");
  return 0;
}
