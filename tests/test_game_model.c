#include <assert.h>
#include <string.h>

#include <glib-object.h>

#include "../src/active_game_backend.h"
#include "../src/game_model.h"

typedef struct {
  guint calls;
} StateChangedProbe;

typedef struct {
  gint total;
} TestBuilderOnlyPosition;

typedef struct {
  gint total;
} TestBuilderOnlyState;

static const char *test_game_model_builder_side_label(guint side) {
  return side == 0 ? "Side 0" : "Side 1";
}

static const char *test_game_model_builder_outcome_banner_text(GameBackendOutcome outcome) {
  switch (outcome) {
    case GAME_BACKEND_OUTCOME_SIDE_0_WIN:
      return "Side 0 wins";
    case GAME_BACKEND_OUTCOME_SIDE_1_WIN:
      return "Side 1 wins";
    case GAME_BACKEND_OUTCOME_DRAW:
      return "Draw";
    case GAME_BACKEND_OUTCOME_ONGOING:
    default:
      return "Ongoing";
  }
}

static void test_game_model_builder_position_init(gpointer position, const GameBackendVariant * /*variant_or_null*/) {
  TestBuilderOnlyPosition *builder_position = position;

  g_return_if_fail(builder_position != NULL);

  builder_position->total = 0;
}

static void test_game_model_builder_position_clear(gpointer /*position*/) {
}

static void test_game_model_builder_position_copy(gpointer dest, gconstpointer src) {
  TestBuilderOnlyPosition *dest_position = dest;
  const TestBuilderOnlyPosition *src_position = src;

  g_return_if_fail(dest_position != NULL);
  g_return_if_fail(src_position != NULL);

  *dest_position = *src_position;
}

static GameBackendOutcome test_game_model_builder_position_outcome(gconstpointer /*position*/) {
  return GAME_BACKEND_OUTCOME_ONGOING;
}

static guint test_game_model_builder_position_turn(gconstpointer /*position*/) {
  return 0;
}

static void test_game_model_builder_move_list_free(GameBackendMoveList *moves) {
  g_return_if_fail(moves != NULL);

  g_clear_pointer(&moves->moves, g_free);
  moves->count = 0;
}

static const void *test_game_model_builder_move_list_get(const GameBackendMoveList *moves, gsize index) {
  const gint *values = NULL;

  g_return_val_if_fail(moves != NULL, NULL);
  g_return_val_if_fail(index < moves->count, NULL);

  values = moves->moves;
  g_return_val_if_fail(values != NULL, NULL);
  return &values[index];
}

static gboolean test_game_model_builder_moves_equal(gconstpointer left, gconstpointer right) {
  const gint *left_value = left;
  const gint *right_value = right;

  g_return_val_if_fail(left_value != NULL, FALSE);
  g_return_val_if_fail(right_value != NULL, FALSE);

  return *left_value == *right_value;
}

static gboolean test_game_model_builder_init(gconstpointer position, GameBackendMoveBuilder *out_builder) {
  const TestBuilderOnlyPosition *builder_position = position;
  TestBuilderOnlyState *state = NULL;

  g_return_val_if_fail(builder_position != NULL, FALSE);
  g_return_val_if_fail(out_builder != NULL, FALSE);

  state = g_new0(TestBuilderOnlyState, 1);
  g_return_val_if_fail(state != NULL, FALSE);

  state->total = builder_position->total;
  out_builder->builder_state = state;
  out_builder->builder_state_size = sizeof(*state);
  return TRUE;
}

static void test_game_model_builder_clear(GameBackendMoveBuilder *builder) {
  g_return_if_fail(builder != NULL);

  g_clear_pointer(&builder->builder_state, g_free);
  builder->builder_state_size = 0;
}

static GameBackendMoveList test_game_model_builder_list_candidates(const GameBackendMoveBuilder *builder) {
  const TestBuilderOnlyState *state = NULL;
  gint *candidate = NULL;

  g_return_val_if_fail(builder != NULL, (GameBackendMoveList){0});

  state = builder->builder_state;
  g_return_val_if_fail(state != NULL, (GameBackendMoveList){0});
  if (state->total != 0) {
    return (GameBackendMoveList){0};
  }

  candidate = g_new(gint, 1);
  g_return_val_if_fail(candidate != NULL, (GameBackendMoveList){0});
  candidate[0] = 1;
  return (GameBackendMoveList){
    .moves = candidate,
    .count = 1,
  };
}

static gboolean test_game_model_builder_step(GameBackendMoveBuilder *builder, gconstpointer candidate) {
  TestBuilderOnlyState *state = NULL;
  const gint *value = candidate;

  g_return_val_if_fail(builder != NULL, FALSE);
  g_return_val_if_fail(value != NULL, FALSE);

  state = builder->builder_state;
  g_return_val_if_fail(state != NULL, FALSE);
  if (state->total != 0 || *value != 1) {
    return FALSE;
  }

  state->total = *value;
  return TRUE;
}

static gboolean test_game_model_builder_is_complete(const GameBackendMoveBuilder *builder) {
  const TestBuilderOnlyState *state = NULL;

  g_return_val_if_fail(builder != NULL, FALSE);

  state = builder->builder_state;
  g_return_val_if_fail(state != NULL, FALSE);
  return state->total == 1;
}

static gboolean test_game_model_builder_build_move(const GameBackendMoveBuilder *builder, gpointer out_move) {
  const TestBuilderOnlyState *state = NULL;
  gint *move = out_move;

  g_return_val_if_fail(builder != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  state = builder->builder_state;
  g_return_val_if_fail(state != NULL, FALSE);
  if (state->total != 1) {
    return FALSE;
  }

  *move = 1;
  return TRUE;
}

static gboolean test_game_model_builder_apply_move(gpointer position, gconstpointer move) {
  TestBuilderOnlyPosition *builder_position = position;
  const gint *value = move;

  g_return_val_if_fail(builder_position != NULL, FALSE);
  g_return_val_if_fail(value != NULL, FALSE);
  if (builder_position->total != 0 || *value != 1) {
    return FALSE;
  }

  builder_position->total = *value;
  return TRUE;
}

static gboolean test_game_model_builder_format_move(gconstpointer move, char *buffer, gsize size) {
  const gint *value = move;

  g_return_val_if_fail(value != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(size > 0, FALSE);

  return g_snprintf(buffer, size, "step-%d", *value) < (gint)size;
}

static const GameBackend test_game_model_builder_backend = {
  .id = "builder-only",
  .display_name = "Builder Only",
  .variant_count = 0,
  .position_size = sizeof(TestBuilderOnlyPosition),
  .move_size = sizeof(gint),
  .supports_move_list = FALSE,
  .supports_move_builder = TRUE,
  .supports_ai_search = FALSE,
  .side_label = test_game_model_builder_side_label,
  .outcome_banner_text = test_game_model_builder_outcome_banner_text,
  .position_init = test_game_model_builder_position_init,
  .position_clear = test_game_model_builder_position_clear,
  .position_copy = test_game_model_builder_position_copy,
  .position_outcome = test_game_model_builder_position_outcome,
  .position_turn = test_game_model_builder_position_turn,
  .move_list_free = test_game_model_builder_move_list_free,
  .move_list_get = test_game_model_builder_move_list_get,
  .moves_equal = test_game_model_builder_moves_equal,
  .move_builder_init = test_game_model_builder_init,
  .move_builder_clear = test_game_model_builder_clear,
  .move_builder_list_candidates = test_game_model_builder_list_candidates,
  .move_builder_step = test_game_model_builder_step,
  .move_builder_is_complete = test_game_model_builder_is_complete,
  .move_builder_build_move = test_game_model_builder_build_move,
  .apply_move = test_game_model_builder_apply_move,
  .format_move = test_game_model_builder_format_move,
};

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

static void test_game_model_builder_only_backend(void) {
  GGameModel *model = ggame_model_new(&test_game_model_builder_backend);
  StateChangedProbe probe = {0};
  gint move = 1;
  char *status = NULL;
  gulong handler_id = 0;

  assert(model != NULL);
  assert(ggame_model_peek_backend(model) == &test_game_model_builder_backend);

  handler_id = g_signal_connect(model, "state-changed", G_CALLBACK(test_game_model_state_changed_cb), &probe);
  assert(handler_id > 0);

  assert(ggame_model_list_moves(model).count == 0);
  assert(ggame_model_apply_move(model, &move));
  assert(probe.calls == 1);

  status = ggame_model_format_status(model);
  assert(status != NULL);
  assert(strstr(status, "Game: Builder Only") != NULL);
  assert(strstr(status, "Moves available: unavailable") != NULL);
  g_free(status);

  g_object_unref(model);
}

int main(void) {
  test_game_model_init_and_apply_move();
  test_game_model_reset_and_status();
  test_game_model_builder_only_backend();

  return 0;
}
