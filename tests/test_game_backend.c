#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/active_game_backend.h"

static void test_backend_metadata(void) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  assert(backend != NULL);
  assert(strcmp(backend->id, "checkers") == 0);
  assert(strcmp(backend->display_name, "Checkers") == 0);
  assert(backend->variant_count == 3);

  const GameBackendVariant *american = backend->variant_at(0);
  assert(american != NULL);
  assert(strcmp(american->short_name, "american") == 0);

  const GameBackendVariant *russian = backend->variant_by_short_name("russian");
  assert(russian != NULL);
  assert(strcmp(russian->name, "Russian (8x8)") == 0);
}

static void test_backend_position_and_move_flow(void) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  assert(backend != NULL);

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
}

int main(void) {
  test_backend_metadata();
  test_backend_position_and_move_flow();

  printf("All tests passed.\n");
  return 0;
}
