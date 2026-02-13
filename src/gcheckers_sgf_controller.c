#include "gcheckers_sgf_controller.h"

#include <string.h>

struct _GCheckersSgfController {
  GObject parent_instance;
  BoardView *board_view;
  GCheckersModel *model;
  SgfTree *sgf_tree;
  SgfView *sgf_view;
  gulong state_handler_id;
  gboolean is_replaying;
  guint last_history_size;
};

G_DEFINE_TYPE(GCheckersSgfController, gcheckers_sgf_controller, G_TYPE_OBJECT)

static SgfColor gcheckers_sgf_controller_color_from_turn(CheckersColor color) {
  switch (color) {
    case CHECKERS_COLOR_BLACK:
      return SGF_COLOR_BLACK;
    case CHECKERS_COLOR_WHITE:
      return SGF_COLOR_WHITE;
    default:
      return SGF_COLOR_NONE;
  }
}

static void gcheckers_sgf_controller_disconnect_model(GCheckersSgfController *self) {
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self));

  if (self->model && self->state_handler_id != 0) {
    g_signal_handler_disconnect(self->model, self->state_handler_id);
    self->state_handler_id = 0;
  }
  g_clear_object(&self->model);
}

static void gcheckers_sgf_controller_append_move(GCheckersSgfController *self) {
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));
  g_return_if_fail(self->sgf_tree != NULL);

  if (self->is_replaying) {
    return;
  }

  guint history_size = gcheckers_model_get_history_size(self->model);
  if (history_size <= self->last_history_size) {
    self->last_history_size = history_size;
    return;
  }

  const CheckersMove *last_move = gcheckers_model_peek_last_move(self->model);
  if (!last_move) {
    self->last_history_size = history_size;
    return;
  }

  const GameState *state = gcheckers_model_peek_state(self->model);
  if (!state) {
    g_debug("Failed to fetch game state for SGF update\n");
    return;
  }

  CheckersColor mover = state->turn == CHECKERS_COLOR_WHITE ? CHECKERS_COLOR_BLACK : CHECKERS_COLOR_WHITE;
  SgfColor sgf_color = gcheckers_sgf_controller_color_from_turn(mover);
  GBytes *payload = g_bytes_new(last_move, sizeof(*last_move));
  const SgfNode *node = sgf_tree_append_move(self->sgf_tree, sgf_color, payload);
  g_bytes_unref(payload);

  if (!node) {
    g_debug("Failed to append SGF move\n");
    self->last_history_size = history_size;
    return;
  }

  sgf_view_refresh(self->sgf_view);
  self->last_history_size = history_size;
}

static GPtrArray *gcheckers_sgf_controller_build_node_path(const SgfNode *node) {
  g_return_val_if_fail(node != NULL, NULL);

  GPtrArray *path = g_ptr_array_new();
  const SgfNode *cursor = node;
  while (cursor) {
    g_ptr_array_add(path, (gpointer)cursor);
    cursor = sgf_node_get_parent(cursor);
  }

  for (guint i = 0; i < path->len / 2; ++i) {
    gpointer tmp = g_ptr_array_index(path, i);
    g_ptr_array_index(path, i) = g_ptr_array_index(path, path->len - 1 - i);
    g_ptr_array_index(path, path->len - 1 - i) = tmp;
  }

  return path;
}

static void gcheckers_sgf_controller_replay_to_node(GCheckersSgfController *self, const SgfNode *node) {
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));
  g_return_if_fail(node != NULL);

  self->is_replaying = TRUE;
  gcheckers_model_reset(self->model);
  board_view_clear_selection(self->board_view);

  GPtrArray *path = gcheckers_sgf_controller_build_node_path(node);
  if (!path) {
    g_debug("Failed to build SGF node path for replay\n");
    self->last_history_size = gcheckers_model_get_history_size(self->model);
    self->is_replaying = FALSE;
    return;
  }

  for (guint i = 0; i < path->len; ++i) {
    const SgfNode *step = g_ptr_array_index(path, i);
    if (sgf_node_get_move_number(step) == 0) {
      continue;
    }

    GBytes *payload = sgf_node_get_payload(step);
    if (!payload) {
      g_debug("Missing payload for SGF node %u\n", sgf_node_get_move_number(step));
      continue;
    }

    gsize size = 0;
    const void *data = g_bytes_get_data(payload, &size);
    if (size != sizeof(CheckersMove)) {
      g_debug("Unexpected SGF payload size %zu\n", size);
      g_bytes_unref(payload);
      continue;
    }

    CheckersMove move;
    memcpy(&move, data, sizeof(move));
    if (!gcheckers_model_apply_move(self->model, &move)) {
      g_debug("Failed to replay SGF move %u\n", sgf_node_get_move_number(step));
    }
    g_bytes_unref(payload);
  }

  g_ptr_array_unref(path);
  self->last_history_size = gcheckers_model_get_history_size(self->model);
  self->is_replaying = FALSE;
}

static void gcheckers_sgf_controller_on_node_selected(SgfView * /*view*/,
                                                      const SgfNode *node,
                                                      gpointer user_data) {
  GCheckersSgfController *self = GCHECKERS_SGF_CONTROLLER(user_data);

  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self));
  g_return_if_fail(node != NULL);

  if (!sgf_tree_set_current(self->sgf_tree, node)) {
    g_debug("Failed to select SGF node\n");
    return;
  }

  gcheckers_sgf_controller_replay_to_node(self, node);
}

static void gcheckers_sgf_controller_on_model_state_changed(GCheckersModel *model,
                                                            gpointer user_data) {
  GCheckersSgfController *self = GCHECKERS_SGF_CONTROLLER(user_data);

  g_return_if_fail(GCHECKERS_IS_MODEL(model));
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self));

  gcheckers_sgf_controller_append_move(self);
}

static void gcheckers_sgf_controller_dispose(GObject *object) {
  GCheckersSgfController *self = GCHECKERS_SGF_CONTROLLER(object);

  gcheckers_sgf_controller_disconnect_model(self);
  g_clear_object(&self->board_view);
  g_clear_object(&self->sgf_view);
  g_clear_object(&self->sgf_tree);

  G_OBJECT_CLASS(gcheckers_sgf_controller_parent_class)->dispose(object);
}

static void gcheckers_sgf_controller_class_init(GCheckersSgfControllerClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = gcheckers_sgf_controller_dispose;

}

static void gcheckers_sgf_controller_init(GCheckersSgfController *self) {
  self->sgf_tree = sgf_tree_new();
  self->sgf_view = sgf_view_new();
  sgf_view_set_tree(self->sgf_view, self->sgf_tree);
  g_signal_connect(self->sgf_view,
                   "node-selected",
                   G_CALLBACK(gcheckers_sgf_controller_on_node_selected),
                   self);

  self->is_replaying = FALSE;
  self->last_history_size = 0;
}

GCheckersSgfController *gcheckers_sgf_controller_new(BoardView *board_view) {
  g_return_val_if_fail(BOARD_IS_VIEW(board_view), NULL);

  GCheckersSgfController *self = g_object_new(GCHECKERS_TYPE_SGF_CONTROLLER, NULL);
  self->board_view = g_object_ref(board_view);
  return self;
}

void gcheckers_sgf_controller_set_model(GCheckersSgfController *self, GCheckersModel *model) {
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(model));

  gcheckers_sgf_controller_disconnect_model(self);

  self->model = g_object_ref(model);
  self->state_handler_id = g_signal_connect(self->model,
                                            "state-changed",
                                            G_CALLBACK(gcheckers_sgf_controller_on_model_state_changed),
                                            self);
  gcheckers_sgf_controller_reset(self);
}

void gcheckers_sgf_controller_reset(GCheckersSgfController *self) {
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self));

  sgf_tree_reset(self->sgf_tree);
  sgf_view_set_tree(self->sgf_view, self->sgf_tree);
  self->last_history_size = self->model ? gcheckers_model_get_history_size(self->model) : 0;
  self->is_replaying = FALSE;
}

GtkWidget *gcheckers_sgf_controller_get_widget(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), NULL);

  GtkWidget *widget = sgf_view_get_widget(self->sgf_view);
  if (!widget) {
    g_debug("Missing SGF view widget\n");
    return NULL;
  }

  return widget;
}

gboolean gcheckers_sgf_controller_is_replaying(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);

  return self->is_replaying;
}

