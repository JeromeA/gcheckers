#include "board_selection_controller.h"

#include <string.h>

struct _BoardSelectionController {
  GObject parent_instance;
  GGameModel *model;
  BoardSelectionControllerMoveHandler move_handler;
  gpointer move_handler_data;
  BoardSelectionControllerCandidatePreference candidate_preference;
  gpointer candidate_preference_data;
  BoardSelectionControllerCompletionConfirmation completion_confirmation;
  gpointer completion_confirmation_data;
  GameBackendMoveBuilder builder;
  gulong model_state_changed_handler_id;
  gboolean builder_active;
  gboolean completion_pending;
  guint selected_path[128];
  guint selected_length;
};

G_DEFINE_TYPE(BoardSelectionController, board_selection_controller, G_TYPE_OBJECT)

typedef enum {
  BOARD_SELECTION_CLICK_FAILED = 0,
  BOARD_SELECTION_CLICK_NO_MATCH,
  BOARD_SELECTION_CLICK_HANDLED,
} BoardSelectionClickResult;

static void board_selection_controller_print_move(const char *label,
                                                  const GameBackend *backend,
                                                  gconstpointer move) {
  g_return_if_fail(label != NULL);
  g_return_if_fail(backend != NULL);
  g_return_if_fail(move != NULL);

  char buffer[128];
  if (!backend->format_move(move, buffer, sizeof(buffer))) {
    g_debug("Failed to format move notation");
    return;
  }
}

static void board_selection_controller_apply_player_move(BoardSelectionController *self, gconstpointer move) {
  g_return_if_fail(BOARD_IS_SELECTION_CONTROLLER(self));
  g_return_if_fail(move != NULL);
  g_return_if_fail(self->move_handler != NULL);
  g_return_if_fail(self->model != NULL);

  if (!self->move_handler(move, self->move_handler_data)) {
    g_debug("Failed to apply player move");
    return;
  }

  board_selection_controller_print_move("Player", ggame_model_peek_backend(self->model), move);
  board_selection_controller_clear(self);
}

static gboolean board_selection_controller_build_current_move(BoardSelectionController *self,
                                                              const GameBackend *backend,
                                                              gpointer *out_move) {
  gpointer move = NULL;

  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), FALSE);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);
  g_return_val_if_fail(backend->move_builder_build_move != NULL, FALSE);

  move = g_malloc0(backend->move_size);
  if (move == NULL || !backend->move_builder_build_move(&self->builder, move)) {
    g_debug("Failed to build completed board move");
    g_free(move);
    return FALSE;
  }

  *out_move = move;
  return TRUE;
}

static void board_selection_controller_clear_builder(BoardSelectionController *self) {
  const GameBackend *backend = NULL;

  g_return_if_fail(BOARD_IS_SELECTION_CONTROLLER(self));

  if (!self->builder_active) {
    return;
  }

  backend = ggame_model_peek_backend(self->model);
  if (backend != NULL && backend->move_builder_clear != NULL) {
    backend->move_builder_clear(&self->builder);
  }
  memset(&self->builder, 0, sizeof(self->builder));
  self->builder_active = FALSE;
  self->completion_pending = FALSE;
}

static void board_selection_controller_disconnect_model(BoardSelectionController *self) {
  g_return_if_fail(BOARD_IS_SELECTION_CONTROLLER(self));

  if (self->model != NULL && self->model_state_changed_handler_id != 0) {
    g_signal_handler_disconnect(self->model, self->model_state_changed_handler_id);
  }
  self->model_state_changed_handler_id = 0;
}

static void board_selection_controller_on_model_state_changed(GGameModel *model, gpointer user_data) {
  BoardSelectionController *self = BOARD_SELECTION_CONTROLLER(user_data);

  g_return_if_fail(BOARD_IS_SELECTION_CONTROLLER(self));
  g_return_if_fail(GGAME_IS_MODEL(model));

  if (self->model != model) {
    g_debug("Ignoring state change from a stale board selection model");
    return;
  }

  board_selection_controller_clear(self);
}

static gboolean board_selection_controller_ensure_builder(BoardSelectionController *self, const GameBackend *backend) {
  gconstpointer position = NULL;

  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), FALSE);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(backend->supports_move_builder, FALSE);
  g_return_val_if_fail(backend->move_builder_init != NULL, FALSE);

  if (self->builder_active) {
    return TRUE;
  }

  position = ggame_model_peek_position(self->model);
  if (position == NULL) {
    g_debug("Missing position while initializing board move builder");
    return FALSE;
  }

  if (!backend->move_builder_init(position, &self->builder)) {
    g_debug("Failed to initialize board move builder");
    return FALSE;
  }

  self->builder_active = TRUE;
  return TRUE;
}

static gboolean board_selection_controller_candidate_get_path(const GameBackend *backend,
                                                              gconstpointer candidate,
                                                              guint *out_length,
                                                              guint *out_indices,
                                                              gsize max_indices) {
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(candidate != NULL, FALSE);
  g_return_val_if_fail(out_length != NULL, FALSE);
  g_return_val_if_fail(backend->square_grid_move_get_path != NULL, FALSE);

  return backend->square_grid_move_get_path(candidate, out_length, out_indices, max_indices);
}

static gboolean board_selection_controller_candidate_matches_click(BoardSelectionController *self,
                                                                   const GameBackend *backend,
                                                                   gconstpointer candidate,
                                                                   guint index) {
  guint path[G_N_ELEMENTS(self->selected_path)] = {0};
  guint length = 0;

  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), FALSE);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(candidate != NULL, FALSE);

  if (!board_selection_controller_candidate_get_path(backend, candidate, &length, path, G_N_ELEMENTS(path))) {
    return FALSE;
  }
  if (length != self->selected_length + 1 || path[self->selected_length] != index) {
    return FALSE;
  }

  for (guint i = 0; i < self->selected_length; ++i) {
    if (path[i] != self->selected_path[i]) {
      return FALSE;
    }
  }

  return TRUE;
}

static gconstpointer board_selection_controller_find_clicked_candidate(BoardSelectionController *self,
                                                                       const GameBackend *backend,
                                                                       const GameBackendMoveList *candidates,
                                                                       guint index) {
  const void *fallback = NULL;

  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), NULL);
  g_return_val_if_fail(backend != NULL, NULL);
  g_return_val_if_fail(candidates != NULL, NULL);

  for (gsize i = 0; i < candidates->count; ++i) {
    const void *candidate = backend->move_list_get(candidates, i);
    if (candidate == NULL || !board_selection_controller_candidate_matches_click(self, backend, candidate, index)) {
      continue;
    }

    if (fallback == NULL) {
      fallback = candidate;
    }
    if (self->candidate_preference == NULL ||
        self->candidate_preference(candidate, self->candidate_preference_data)) {
      return candidate;
    }
  }

  return fallback;
}

static gboolean board_selection_controller_sync_path_from_candidate(BoardSelectionController *self,
                                                                    const GameBackend *backend,
                                                                    gconstpointer candidate) {
  guint length = 0;

  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), FALSE);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(candidate != NULL, FALSE);

  if (!board_selection_controller_candidate_get_path(backend,
                                                     candidate,
                                                     &length,
                                                     self->selected_path,
                                                     G_N_ELEMENTS(self->selected_path))) {
    return FALSE;
  }

  self->selected_length = length;
  return TRUE;
}

static gboolean board_selection_controller_sync_path_from_builder(BoardSelectionController *self,
                                                                  const GameBackend *backend) {
  guint length = 0;
  guint path[G_N_ELEMENTS(self->selected_path)] = {0};

  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), FALSE);
  g_return_val_if_fail(backend != NULL, FALSE);

  if (backend->move_builder_get_selection_path == NULL) {
    return TRUE;
  }

  if (!backend->move_builder_get_selection_path(&self->builder, &length, path, G_N_ELEMENTS(path))) {
    return FALSE;
  }

  memcpy(self->selected_path, path, length * sizeof(path[0]));
  self->selected_length = length;
  return TRUE;
}

static gboolean board_selection_controller_reset_builder_selection(BoardSelectionController *self,
                                                                   const GameBackend *backend) {
  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), FALSE);
  g_return_val_if_fail(backend != NULL, FALSE);

  if (backend->move_builder_reset_selection == NULL || !self->builder_active) {
    return FALSE;
  }

  if (!backend->move_builder_reset_selection(&self->builder)) {
    return FALSE;
  }

  self->completion_pending = FALSE;
  self->selected_length = 0;
  return board_selection_controller_sync_path_from_builder(self, backend);
}

static BoardSelectionClickResult board_selection_controller_try_step_click(BoardSelectionController *self,
                                                                           const GameBackend *backend,
                                                                           guint index) {
  GameBackendMoveList candidates = {0};
  const void *match = NULL;

  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), BOARD_SELECTION_CLICK_FAILED);
  g_return_val_if_fail(backend != NULL, BOARD_SELECTION_CLICK_FAILED);

  candidates = backend->move_builder_list_candidates(&self->builder);
  if (candidates.count == 0) {
    backend->move_list_free(&candidates);
    g_debug("No available candidates while handling board click");
    return BOARD_SELECTION_CLICK_FAILED;
  }

  match = board_selection_controller_find_clicked_candidate(self, backend, &candidates, index);
  if (match == NULL) {
    backend->move_list_free(&candidates);
    return BOARD_SELECTION_CLICK_NO_MATCH;
  }

  if (!board_selection_controller_sync_path_from_candidate(self, backend, match) ||
      !backend->move_builder_step(&self->builder, match)) {
    g_debug("Failed to advance board move builder");
    backend->move_list_free(&candidates);
    return BOARD_SELECTION_CLICK_FAILED;
  }
  if (!board_selection_controller_sync_path_from_builder(self, backend)) {
    g_debug("Failed to sync board move builder selection path");
    backend->move_list_free(&candidates);
    return BOARD_SELECTION_CLICK_FAILED;
  }

  if (backend->move_builder_is_complete(&self->builder)) {
    gpointer move = NULL;
    if (!board_selection_controller_build_current_move(self, backend, &move)) {
      backend->move_list_free(&candidates);
      return BOARD_SELECTION_CLICK_FAILED;
    }

    backend->move_list_free(&candidates);
    if (self->completion_confirmation != NULL &&
        self->completion_confirmation(&self->builder, move, self->completion_confirmation_data)) {
      self->completion_pending = TRUE;
      g_free(move);
      return BOARD_SELECTION_CLICK_HANDLED;
    }

    board_selection_controller_apply_player_move(self, move);
    g_free(move);
    return BOARD_SELECTION_CLICK_HANDLED;
  }

  backend->move_list_free(&candidates);
  return BOARD_SELECTION_CLICK_HANDLED;
}

static void board_selection_controller_dispose(GObject *object) {
  BoardSelectionController *self = BOARD_SELECTION_CONTROLLER(object);

  board_selection_controller_disconnect_model(self);
  board_selection_controller_clear_builder(self);
  g_clear_object(&self->model);

  G_OBJECT_CLASS(board_selection_controller_parent_class)->dispose(object);
}

static void board_selection_controller_class_init(BoardSelectionControllerClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = board_selection_controller_dispose;
}

static void board_selection_controller_init(BoardSelectionController *self) {
  self->selected_length = 0;
  self->builder_active = FALSE;
  self->completion_pending = FALSE;
  self->model_state_changed_handler_id = 0;
  self->move_handler = NULL;
  self->move_handler_data = NULL;
  self->candidate_preference = NULL;
  self->candidate_preference_data = NULL;
  self->completion_confirmation = NULL;
  self->completion_confirmation_data = NULL;
}

BoardSelectionController *board_selection_controller_new(void) {
  return g_object_new(BOARD_TYPE_SELECTION_CONTROLLER, NULL);
}

void board_selection_controller_set_model(BoardSelectionController *self, GGameModel *model) {
  g_return_if_fail(BOARD_IS_SELECTION_CONTROLLER(self));
  g_return_if_fail(GGAME_IS_MODEL(model));

  board_selection_controller_disconnect_model(self);
  board_selection_controller_clear_builder(self);
  g_set_object(&self->model, model);
  self->model_state_changed_handler_id = g_signal_connect(self->model,
                                                          "state-changed",
                                                          G_CALLBACK(board_selection_controller_on_model_state_changed),
                                                          self);
  board_selection_controller_clear(self);
}

void board_selection_controller_set_move_handler(BoardSelectionController *self,
                                                 BoardSelectionControllerMoveHandler handler,
                                                 gpointer user_data) {
  g_return_if_fail(BOARD_IS_SELECTION_CONTROLLER(self));

  self->move_handler = handler;
  self->move_handler_data = user_data;
}

void board_selection_controller_set_candidate_preference(BoardSelectionController *self,
                                                         BoardSelectionControllerCandidatePreference preference,
                                                         gpointer user_data) {
  g_return_if_fail(BOARD_IS_SELECTION_CONTROLLER(self));

  self->candidate_preference = preference;
  self->candidate_preference_data = user_data;
}

void board_selection_controller_set_completion_confirmation(BoardSelectionController *self,
                                                            BoardSelectionControllerCompletionConfirmation confirmation,
                                                            gpointer user_data) {
  g_return_if_fail(BOARD_IS_SELECTION_CONTROLLER(self));

  self->completion_confirmation = confirmation;
  self->completion_confirmation_data = user_data;
}

void board_selection_controller_clear(BoardSelectionController *self) {
  g_return_if_fail(BOARD_IS_SELECTION_CONTROLLER(self));

  board_selection_controller_clear_builder(self);
  self->selected_length = 0;
}

const guint *board_selection_controller_peek_path(BoardSelectionController *self, guint *length) {
  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), NULL);
  g_return_val_if_fail(length != NULL, NULL);

  *length = self->selected_length;
  return self->selected_path;
}

const GameBackendMoveBuilder *board_selection_controller_peek_builder(BoardSelectionController *self) {
  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), NULL);

  if (!self->builder_active) {
    return NULL;
  }

  return &self->builder;
}

gboolean board_selection_controller_completion_pending(BoardSelectionController *self) {
  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), FALSE);

  return self->completion_pending;
}

gboolean board_selection_controller_confirm(BoardSelectionController *self) {
  const GameBackend *backend = NULL;
  gpointer move = NULL;

  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), FALSE);
  g_return_val_if_fail(GGAME_IS_MODEL(self->model), FALSE);
  g_return_val_if_fail(self->move_handler != NULL, FALSE);

  if (!self->completion_pending) {
    return FALSE;
  }

  backend = ggame_model_peek_backend(self->model);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(backend->move_builder_is_complete != NULL, FALSE);
  if (!self->builder_active || !backend->move_builder_is_complete(&self->builder) ||
      !board_selection_controller_build_current_move(self, backend, &move)) {
    return FALSE;
  }

  board_selection_controller_apply_player_move(self, move);
  g_free(move);
  return TRUE;
}

gboolean board_selection_controller_contains(BoardSelectionController *self, guint index) {
  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), FALSE);

  for (guint i = 0; i < self->selected_length; ++i) {
    if (self->selected_path[i] == index) {
      return TRUE;
    }
  }

  return FALSE;
}

gboolean board_selection_controller_collect_highlights(BoardSelectionController *self,
                                                       gboolean *out_selectable,
                                                       gboolean *out_destinations,
                                                       gsize out_count) {
  const GameBackend *backend = NULL;
  GameBackendMoveList candidates = {0};
  gboolean has_candidates = FALSE;

  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), FALSE);
  g_return_val_if_fail(GGAME_IS_MODEL(self->model), FALSE);
  g_return_val_if_fail(out_selectable != NULL, FALSE);
  g_return_val_if_fail(out_destinations != NULL, FALSE);

  memset(out_selectable, 0, sizeof(*out_selectable) * out_count);
  memset(out_destinations, 0, sizeof(*out_destinations) * out_count);

  backend = ggame_model_peek_backend(self->model);
  g_return_val_if_fail(backend != NULL, FALSE);
  if (!backend->supports_move_builder || backend->move_builder_list_candidates == NULL) {
    return FALSE;
  }
  if (!board_selection_controller_ensure_builder(self, backend)) {
    return FALSE;
  }

  candidates = backend->move_builder_list_candidates(&self->builder);
  for (gsize i = 0; i < candidates.count; ++i) {
    const void *candidate = backend->move_list_get(&candidates, i);
    guint path[G_N_ELEMENTS(self->selected_path)] = {0};
    guint length = 0;

    if (candidate == NULL ||
        !board_selection_controller_candidate_get_path(backend, candidate, &length, path, G_N_ELEMENTS(path)) ||
        length == 0 || length != self->selected_length + 1) {
      continue;
    }

    gboolean prefix_matches = TRUE;
    for (guint path_index = 0; path_index < self->selected_length; ++path_index) {
      if (path[path_index] != self->selected_path[path_index]) {
        prefix_matches = FALSE;
        break;
      }
    }
    if (!prefix_matches || path[self->selected_length] >= out_count) {
      continue;
    }

    if (self->selected_length == 0) {
      out_selectable[path[0]] = TRUE;
    } else {
      out_destinations[path[self->selected_length]] = TRUE;
    }
    has_candidates = TRUE;
  }

  backend->move_list_free(&candidates);
  return has_candidates;
}

gboolean board_selection_controller_handle_click(BoardSelectionController *self, guint index) {
  const GameBackend *backend = NULL;
  BoardSelectionClickResult result = BOARD_SELECTION_CLICK_FAILED;

  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), FALSE);
  g_return_val_if_fail(GGAME_IS_MODEL(self->model), FALSE);
  g_return_val_if_fail(self->move_handler != NULL, FALSE);

  backend = ggame_model_peek_backend(self->model);
  g_return_val_if_fail(backend != NULL, FALSE);
  if (!backend->supports_move_builder) {
    g_debug("Board selection controller requires move-builder backends");
    return FALSE;
  }
  g_return_val_if_fail(backend->move_builder_list_candidates != NULL, FALSE);
  g_return_val_if_fail(backend->move_builder_step != NULL, FALSE);
  g_return_val_if_fail(backend->move_builder_is_complete != NULL, FALSE);
  g_return_val_if_fail(backend->move_builder_build_move != NULL, FALSE);

  if (!board_selection_controller_ensure_builder(self, backend)) {
    return FALSE;
  }

  if (self->completion_pending) {
    if (!board_selection_controller_reset_builder_selection(self, backend)) {
      return TRUE;
    }

    result = board_selection_controller_try_step_click(self, backend, index);
    return result != BOARD_SELECTION_CLICK_FAILED;
  }

  result = board_selection_controller_try_step_click(self, backend, index);
  if (result == BOARD_SELECTION_CLICK_HANDLED) {
    return TRUE;
  }
  if (result == BOARD_SELECTION_CLICK_FAILED || self->selected_length == 0) {
    return FALSE;
  }

  if (board_selection_controller_reset_builder_selection(self, backend)) {
    result = board_selection_controller_try_step_click(self, backend, index);
    return result != BOARD_SELECTION_CLICK_FAILED;
  }

  board_selection_controller_clear(self);
  if (!board_selection_controller_ensure_builder(self, backend)) {
    return TRUE;
  }

  result = board_selection_controller_try_step_click(self, backend, index);
  return result != BOARD_SELECTION_CLICK_FAILED;
}
