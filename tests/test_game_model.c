#include <assert.h>
#include <string.h>

#include <glib-object.h>

#include "../src/active_game_backend.h"
#include "../src/game_model.h"

typedef struct {
  guint calls;
} StateChangedProbe;

static void test_game_model_state_changed_cb(GGameModel * /*model*/, gpointer user_data) {
  StateChangedProbe *probe = user_data;

  g_return_if_fail(probe != NULL);

  probe->calls++;
}

static void test_game_model_init_and_apply_move(void) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  GGameModel *model = ggame_model_new(backend);
  StateChangedProbe probe = {0};
  gulong handler_id = 0;

  assert(model != NULL);
  assert(ggame_model_peek_backend(model) == backend);
  assert(ggame_model_peek_position(model) != NULL);

  const GameBackendVariant *variant = ggame_model_peek_variant(model);
  assert(variant != NULL);
  assert(strcmp(variant->short_name, "american") == 0);

  handler_id = g_signal_connect(model, "state-changed", G_CALLBACK(test_game_model_state_changed_cb), &probe);
  assert(handler_id > 0);

  GameBackendMoveList moves = ggame_model_list_moves(model);
  assert(moves.count > 0);

  const void *first_move = backend->move_list_get(&moves, 0);
  assert(first_move != NULL);

  gpointer move_copy = g_malloc0(backend->move_size);
  assert(move_copy != NULL);
  memcpy(move_copy, first_move, backend->move_size);

  assert(ggame_model_apply_move(model, move_copy));
  assert(probe.calls == 1);

  backend->move_list_free(&moves);
  g_free(move_copy);
  g_object_unref(model);
}

static void test_game_model_reset_and_status(void) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  GGameModel *model = ggame_model_new(backend);
  StateChangedProbe probe = {0};
  gulong handler_id = 0;

  assert(model != NULL);

  handler_id = g_signal_connect(model, "state-changed", G_CALLBACK(test_game_model_state_changed_cb), &probe);
  assert(handler_id > 0);

  const GameBackendVariant *russian = backend->variant_by_short_name("russian");
  assert(russian != NULL);

  ggame_model_reset(model, russian);
  assert(probe.calls == 1);

  const GameBackendVariant *variant = ggame_model_peek_variant(model);
  assert(variant == russian);

  char *status = ggame_model_format_status(model);
  assert(status != NULL);
  assert(strstr(status, "Game: Checkers") != NULL);
  assert(strstr(status, "Variant: Russian (8x8)") != NULL);
  g_free(status);

  GameBackendMoveList moves = ggame_model_list_moves(model);
  assert(moves.count > 0);
  backend->move_list_free(&moves);

  g_object_unref(model);
}

int main(void) {
  test_game_model_init_and_apply_move();
  test_game_model_reset_and_status();

  return 0;
}
