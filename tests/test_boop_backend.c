#include <assert.h>
#include <string.h>

#include "../src/games/boop/boop_backend.h"
#include "../src/games/boop/boop_game.h"

int main(void) {
  const GameBackend *backend = &boop_game_backend;
  gpointer position = NULL;
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  const BoopMove *candidate = NULL;
  BoopMove move = {0};
  char notation[8] = {0};

  assert(strcmp(backend->id, "boop") == 0);
  assert(strcmp(backend->display_name, "Boop") == 0);
  assert(!backend->supports_move_list);
  assert(backend->supports_move_builder);
  assert(!backend->supports_ai_search);

  position = g_malloc0(backend->position_size);
  assert(position != NULL);
  backend->position_init(position, NULL);
  assert(backend->position_turn(position) == 0);
  assert(backend->move_builder_init(position, &builder));
  candidates = backend->move_builder_list_candidates(&builder);
  assert(candidates.count == 1);
  candidate = backend->move_list_get(&candidates, 0);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  assert(backend->move_builder_is_complete(&builder));
  assert(backend->move_builder_build_move(&builder, &move));
  assert(backend->format_move(&move, notation, sizeof(notation)));
  assert(notation[0] != '\0');
  backend->move_list_free(&candidates);
  backend->move_builder_clear(&builder);
  backend->position_clear(position);
  g_free(position);

  return 0;
}
