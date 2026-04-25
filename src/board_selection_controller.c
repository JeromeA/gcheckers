#include "board_selection_controller.h"

#include <string.h>

struct _BoardSelectionController {
  GObject parent_instance;
  GGameModel *model;
  BoardSelectionControllerMoveHandler move_handler;
  gpointer move_handler_data;
  guint selected_path[128];
  guint selected_length;
};

G_DEFINE_TYPE(BoardSelectionController, board_selection_controller, G_TYPE_OBJECT)

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

static gboolean board_selection_controller_move_has_prefix(const GameBackend *backend,
                                                           gconstpointer move,
                                                           const guint *path,
                                                           guint length) {
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(backend->square_grid_move_has_prefix != NULL, FALSE);

  return backend->square_grid_move_has_prefix(move, path, length);
}

static gboolean board_selection_controller_moves_have_prefix(const GameBackend *backend,
                                                             const GameBackendMoveList *moves,
                                                             const guint *path,
                                                             guint length) {
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(moves != NULL, FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  for (gsize i = 0; i < moves->count; ++i) {
    const void *move = backend->move_list_get(moves, i);
    if (move != NULL && board_selection_controller_move_has_prefix(backend, move, path, length)) {
      return TRUE;
    }
  }

  return FALSE;
}

static gconstpointer board_selection_controller_find_exact_move(const GameBackend *backend,
                                                                const GameBackendMoveList *moves,
                                                                const guint *path,
                                                                guint length) {
  g_return_val_if_fail(backend != NULL, NULL);
  g_return_val_if_fail(moves != NULL, NULL);
  g_return_val_if_fail(path != NULL, NULL);

  for (gsize i = 0; i < moves->count; ++i) {
    const void *move = backend->move_list_get(moves, i);
    guint move_length = 0;

    if (move == NULL || !backend->square_grid_move_get_path(move, &move_length, NULL, 0) || move_length != length) {
      continue;
    }
    if (board_selection_controller_move_has_prefix(backend, move, path, length)) {
      return move;
    }
  }

  return NULL;
}

static gboolean board_selection_controller_moves_start_with(const GameBackend *backend,
                                                            const GameBackendMoveList *moves,
                                                            guint index) {
  guint path = index;

  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(moves != NULL, FALSE);

  return board_selection_controller_moves_have_prefix(backend, moves, &path, 1);
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

static void board_selection_controller_dispose(GObject *object) {
  BoardSelectionController *self = BOARD_SELECTION_CONTROLLER(object);

  g_clear_object(&self->model);

  G_OBJECT_CLASS(board_selection_controller_parent_class)->dispose(object);
}

static void board_selection_controller_class_init(BoardSelectionControllerClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = board_selection_controller_dispose;
}

static void board_selection_controller_init(BoardSelectionController *self) {
  self->selected_length = 0;
  self->move_handler = NULL;
  self->move_handler_data = NULL;
}

BoardSelectionController *board_selection_controller_new(void) {
  return g_object_new(BOARD_TYPE_SELECTION_CONTROLLER, NULL);
}

void board_selection_controller_set_model(BoardSelectionController *self, GGameModel *model) {
  g_return_if_fail(BOARD_IS_SELECTION_CONTROLLER(self));
  g_return_if_fail(GGAME_IS_MODEL(model));

  g_set_object(&self->model, model);
  board_selection_controller_clear(self);
}

void board_selection_controller_set_move_handler(BoardSelectionController *self,
                                                 BoardSelectionControllerMoveHandler handler,
                                                 gpointer user_data) {
  g_return_if_fail(BOARD_IS_SELECTION_CONTROLLER(self));

  self->move_handler = handler;
  self->move_handler_data = user_data;
}

void board_selection_controller_clear(BoardSelectionController *self) {
  g_return_if_fail(BOARD_IS_SELECTION_CONTROLLER(self));

  self->selected_length = 0;
}

const guint *board_selection_controller_peek_path(BoardSelectionController *self, guint *length) {
  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), NULL);
  g_return_val_if_fail(length != NULL, NULL);

  *length = self->selected_length;
  return self->selected_path;
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

gboolean board_selection_controller_handle_click(BoardSelectionController *self, guint index) {
  const GameBackend *backend = NULL;
  GameBackendMoveList moves = {0};

  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), FALSE);
  g_return_val_if_fail(GGAME_IS_MODEL(self->model), FALSE);
  g_return_val_if_fail(self->move_handler != NULL, FALSE);

  backend = ggame_model_peek_backend(self->model);
  g_return_val_if_fail(backend != NULL, FALSE);
  if (!backend->supports_move_list) {
    g_debug("Board selection controller requires list-move backends");
    return FALSE;
  }

  moves = ggame_model_list_moves(self->model);
  if (moves.count == 0) {
    backend->move_list_free(&moves);
    g_debug("No available moves while handling board click");
    return FALSE;
  }

  if (self->selected_length == 0) {
    if (!board_selection_controller_moves_start_with(backend, &moves, index)) {
      backend->move_list_free(&moves);
      return FALSE;
    }

    self->selected_path[0] = index;
    self->selected_length = 1;
    backend->move_list_free(&moves);
    return TRUE;
  }

  if (self->selected_length >= G_N_ELEMENTS(self->selected_path)) {
    g_debug("Selection path length exceeded max move length");
    backend->move_list_free(&moves);
    return FALSE;
  }

  guint candidate[G_N_ELEMENTS(self->selected_path)];
  memcpy(candidate, self->selected_path, self->selected_length * sizeof(candidate[0]));
  candidate[self->selected_length] = index;
  guint candidate_length = self->selected_length + 1;

  if (!board_selection_controller_moves_have_prefix(backend, &moves, candidate, candidate_length)) {
    if (board_selection_controller_moves_start_with(backend, &moves, index)) {
      self->selected_path[0] = index;
      self->selected_length = 1;
      backend->move_list_free(&moves);
      return TRUE;
    }

    g_debug("Selected path is not in move list");
    backend->move_list_free(&moves);
    return FALSE;
  }

  memcpy(self->selected_path, candidate, candidate_length * sizeof(candidate[0]));
  self->selected_length = candidate_length;

  gconstpointer match = board_selection_controller_find_exact_move(backend, &moves, candidate, candidate_length);
  if (match != NULL) {
    board_selection_controller_apply_player_move(self, match);
  }

  backend->move_list_free(&moves);
  return TRUE;
}
