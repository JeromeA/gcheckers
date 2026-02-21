#include "gcheckers_sgf_controller.h"

#include <string.h>

struct _GCheckersSgfController {
  GObject parent_instance;
  BoardView *board_view;
  GCheckersModel *model;
  SgfTree *sgf_tree;
  SgfView *sgf_view;
  gboolean is_replaying;
};

G_DEFINE_TYPE(GCheckersSgfController, gcheckers_sgf_controller, G_TYPE_OBJECT)

enum {
  SIGNAL_ANALYSIS_REQUESTED,
  SIGNAL_LAST
};

static guint controller_signals[SIGNAL_LAST] = {0};

static gboolean gcheckers_sgf_controller_moves_equal(const CheckersMove *left, const CheckersMove *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  if (left->length != right->length || left->captures != right->captures) {
    return FALSE;
  }

  if (left->length == 0) {
    return TRUE;
  }

  return memcmp(left->path, right->path, left->length * sizeof(left->path[0])) == 0;
}

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

  g_clear_object(&self->model);
}

static gboolean gcheckers_sgf_controller_extract_payload_move(const SgfNode *node, CheckersMove *move) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  GBytes *payload = sgf_node_get_payload(node);
  if (!payload) {
    g_debug("Missing payload for SGF node %u", sgf_node_get_move_number(node));
    return FALSE;
  }

  gsize size = 0;
  const void *data = g_bytes_get_data(payload, &size);
  if (size != sizeof(*move)) {
    g_debug("Unexpected SGF payload size %zu", size);
    g_bytes_unref(payload);
    return FALSE;
  }

  memcpy(move, data, sizeof(*move));
  g_bytes_unref(payload);
  return TRUE;
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

static gboolean gcheckers_sgf_controller_replay_to_node(GCheckersSgfController *self, const SgfNode *node) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self->model), FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  self->is_replaying = TRUE;
  gcheckers_model_reset(self->model);
  board_view_clear_selection(self->board_view);

  GPtrArray *path = gcheckers_sgf_controller_build_node_path(node);
  if (!path) {
    g_debug("Failed to build SGF node path for replay");
    self->is_replaying = FALSE;
    return FALSE;
  }

  gboolean success = TRUE;
  for (guint i = 0; i < path->len; ++i) {
    const SgfNode *step = g_ptr_array_index(path, i);
    if (sgf_node_get_move_number(step) == 0) {
      continue;
    }

    CheckersMove move;
    if (!gcheckers_sgf_controller_extract_payload_move(step, &move)) {
      success = FALSE;
      break;
    }

    if (!gcheckers_model_apply_move(self->model, &move)) {
      g_debug("Failed to replay SGF move %u", sgf_node_get_move_number(step));
      success = FALSE;
      break;
    }
  }

  g_ptr_array_unref(path);
  self->is_replaying = FALSE;
  return success;
}

static gboolean gcheckers_sgf_controller_sync_model_for_transition(GCheckersSgfController *self,
                                                                   const SgfNode *previous,
                                                                   const SgfNode *target) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self->model), FALSE);
  g_return_val_if_fail(target != NULL, FALSE);

  if (previous == target) {
    return TRUE;
  }

  if (previous && sgf_node_get_parent(target) == previous) {
    CheckersMove move;
    if (!gcheckers_sgf_controller_extract_payload_move(target, &move)) {
      return FALSE;
    }

    if (!gcheckers_model_apply_move(self->model, &move)) {
      g_debug("Failed to apply SGF transition move %u", sgf_node_get_move_number(target));
      return FALSE;
    }

    return TRUE;
  }

  return gcheckers_sgf_controller_replay_to_node(self, target);
}

static gboolean gcheckers_sgf_controller_move_current(GCheckersSgfController *self, const SgfNode *node) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  const SgfNode *previous = sgf_tree_get_current(self->sgf_tree);
  if (!sgf_tree_set_current(self->sgf_tree, node)) {
    g_debug("Failed to select SGF node");
    return FALSE;
  }

  if (!self->model) {
    return TRUE;
  }

  return gcheckers_sgf_controller_sync_model_for_transition(self, previous, node);
}

static gboolean gcheckers_sgf_controller_navigate_to(GCheckersSgfController *self, const SgfNode *node) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  if (!gcheckers_sgf_controller_move_current(self, node)) {
    return FALSE;
  }

  g_signal_emit(self, controller_signals[SIGNAL_ANALYSIS_REQUESTED], 0, node);
  sgf_view_refresh(self->sgf_view);
  return TRUE;
}

static void gcheckers_sgf_controller_on_node_selected(SgfView * /*view*/,
                                                      const SgfNode *node,
                                                      gpointer user_data) {
  GCheckersSgfController *self = GCHECKERS_SGF_CONTROLLER(user_data);

  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self));
  g_return_if_fail(node != NULL);

  if (!gcheckers_sgf_controller_move_current(self, node)) {
    return;
  }

  g_signal_emit(self, controller_signals[SIGNAL_ANALYSIS_REQUESTED], 0, node);
  sgf_view_refresh(self->sgf_view);
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

  controller_signals[SIGNAL_ANALYSIS_REQUESTED] = g_signal_new("analysis-requested",
                                                               G_TYPE_FROM_CLASS(klass),
                                                               G_SIGNAL_RUN_LAST,
                                                               0,
                                                               NULL,
                                                               NULL,
                                                               NULL,
                                                               G_TYPE_NONE,
                                                               1,
                                                               G_TYPE_POINTER);
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

  if (self->model == model) {
    return;
  }

  gcheckers_sgf_controller_disconnect_model(self);
  self->model = g_object_ref(model);
}

void gcheckers_sgf_controller_new_game(GCheckersSgfController *self) {
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self));

  sgf_tree_reset(self->sgf_tree);
  sgf_view_set_tree(self->sgf_view, self->sgf_tree);
  board_view_clear_selection(self->board_view);
  self->is_replaying = FALSE;
}

gboolean gcheckers_sgf_controller_apply_move(GCheckersSgfController *self, const CheckersMove *move) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self->model), FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  MoveList moves = gcheckers_model_list_moves(self->model);
  gboolean found = FALSE;
  for (guint i = 0; i < moves.count; ++i) {
    if (gcheckers_sgf_controller_moves_equal(&moves.moves[i], move)) {
      found = TRUE;
      break;
    }
  }

  if (!found) {
    g_debug("Attempted to apply move not present in current model move list");
    movelist_free(&moves);
    return FALSE;
  }

  const GameState *state = gcheckers_model_peek_state(self->model);
  if (!state) {
    g_debug("Failed to fetch game state for SGF append");
    movelist_free(&moves);
    return FALSE;
  }

  const SgfNode *previous = sgf_tree_get_current(self->sgf_tree);
  GBytes *payload = g_bytes_new(move, sizeof(*move));
  const SgfNode *node =
      sgf_tree_append_move(self->sgf_tree, gcheckers_sgf_controller_color_from_turn(state->turn), payload);
  g_bytes_unref(payload);
  movelist_free(&moves);

  if (!node) {
    g_debug("Failed to append SGF move");
    return FALSE;
  }

  if (!sgf_tree_set_current(self->sgf_tree, node)) {
    g_debug("Failed to move SGF current to appended node");
    return FALSE;
  }

  if (!gcheckers_sgf_controller_sync_model_for_transition(self, previous, node)) {
    return FALSE;
  }

  sgf_view_refresh(self->sgf_view);
  return TRUE;
}

gboolean gcheckers_sgf_controller_step_ai_move(GCheckersSgfController *self, guint depth, CheckersMove *out_move) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self->model), FALSE);
  g_return_val_if_fail(depth > 0, FALSE);

  CheckersMove move = {0};
  if (!gcheckers_model_choose_best_move(self->model, depth, &move)) {
    g_debug("Failed to choose alpha-beta SGF move from model");
    return FALSE;
  }

  gboolean applied = gcheckers_sgf_controller_apply_move(self, &move);
  if (applied && out_move) {
    *out_move = move;
  }

  return applied;
}

gboolean gcheckers_sgf_controller_rewind_to_start(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);

  const SgfNode *root = sgf_tree_get_root(self->sgf_tree);
  if (!root) {
    g_debug("Missing SGF root node");
    return FALSE;
  }

  if (sgf_tree_get_current(self->sgf_tree) == root) {
    return FALSE;
  }

  return gcheckers_sgf_controller_navigate_to(self, root);
}

gboolean gcheckers_sgf_controller_step_backward(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);

  const SgfNode *current = sgf_tree_get_current(self->sgf_tree);
  if (!current) {
    g_debug("Missing SGF current node");
    return FALSE;
  }

  const SgfNode *target = sgf_node_get_parent(current);
  if (!target) {
    return FALSE;
  }

  return gcheckers_sgf_controller_navigate_to(self, target);
}

gboolean gcheckers_sgf_controller_step_forward(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);

  const SgfNode *current = sgf_tree_get_current(self->sgf_tree);
  if (!current) {
    g_debug("Missing SGF current node");
    return FALSE;
  }

  const GPtrArray *children = sgf_node_get_children(current);
  if (!children || children->len == 0) {
    return FALSE;
  }

  const SgfNode *target = g_ptr_array_index((GPtrArray *)children, 0);
  g_return_val_if_fail(target != NULL, FALSE);
  return gcheckers_sgf_controller_navigate_to(self, target);
}

gboolean gcheckers_sgf_controller_step_forward_to_branch(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);

  const SgfNode *cursor = sgf_tree_get_current(self->sgf_tree);
  if (!cursor) {
    g_debug("Missing SGF current node");
    return FALSE;
  }

  gboolean moved = FALSE;
  while (TRUE) {
    const GPtrArray *children = sgf_node_get_children(cursor);
    if (!children || children->len == 0 || children->len >= 2) {
      break;
    }
    cursor = g_ptr_array_index((GPtrArray *)children, 0);
    g_return_val_if_fail(cursor != NULL, FALSE);
    moved = TRUE;
  }

  if (!moved) {
    return FALSE;
  }

  return gcheckers_sgf_controller_navigate_to(self, cursor);
}

gboolean gcheckers_sgf_controller_step_forward_to_end(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);

  const SgfNode *cursor = sgf_tree_get_current(self->sgf_tree);
  if (!cursor) {
    g_debug("Missing SGF current node");
    return FALSE;
  }

  gboolean moved = FALSE;
  while (TRUE) {
    const GPtrArray *children = sgf_node_get_children(cursor);
    if (!children || children->len == 0) {
      break;
    }
    cursor = g_ptr_array_index((GPtrArray *)children, 0);
    g_return_val_if_fail(cursor != NULL, FALSE);
    moved = TRUE;
  }

  if (!moved) {
    return FALSE;
  }

  return gcheckers_sgf_controller_navigate_to(self, cursor);
}

GtkWidget *gcheckers_sgf_controller_get_widget(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), NULL);

  GtkWidget *widget = sgf_view_get_widget(self->sgf_view);
  if (!widget) {
    g_debug("Missing SGF view widget");
    return NULL;
  }

  return widget;
}

SgfTree *gcheckers_sgf_controller_get_tree(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), NULL);

  if (!self->sgf_tree) {
    g_debug("Missing SGF tree");
    return NULL;
  }

  return self->sgf_tree;
}

SgfView *gcheckers_sgf_controller_get_view(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), NULL);

  if (!self->sgf_view) {
    g_debug("Missing SGF view");
    return NULL;
  }

  return self->sgf_view;
}

gboolean gcheckers_sgf_controller_is_replaying(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);

  return self->is_replaying;
}

void gcheckers_sgf_controller_force_layout_resync(GCheckersSgfController *self) {
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self));

  if (!self->sgf_view) {
    g_debug("Missing SGF view for layout resync");
    return;
  }

  sgf_view_force_layout_sync(self->sgf_view);
}
