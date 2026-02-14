#include "gcheckers_sgf_controller.h"

struct _GCheckersSgfController {
  GObject parent_instance;
  SgfTree *sgf_tree;
  SgfView *sgf_view;
};

G_DEFINE_TYPE(GCheckersSgfController, gcheckers_sgf_controller, G_TYPE_OBJECT)

static void gcheckers_sgf_controller_on_node_selected(SgfView * /*view*/,
                                                      const SgfNode *node,
                                                      gpointer user_data) {
  GCheckersSgfController *self = GCHECKERS_SGF_CONTROLLER(user_data);

  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self));
  g_return_if_fail(node != NULL);

  if (!sgf_tree_set_current(self->sgf_tree, node)) {
    g_debug("Failed to select SGF node\n");
  }
}

static void gcheckers_sgf_controller_dispose(GObject *object) {
  GCheckersSgfController *self = GCHECKERS_SGF_CONTROLLER(object);

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

}

GCheckersSgfController *gcheckers_sgf_controller_new(void) {
  return g_object_new(GCHECKERS_TYPE_SGF_CONTROLLER, NULL);
}

const SgfNode *gcheckers_sgf_controller_append_synthetic_move(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), NULL);
  g_return_val_if_fail(self->sgf_tree != NULL, NULL);

  const SgfNode *current = sgf_tree_get_current(self->sgf_tree);
  guint move_number = current ? sgf_node_get_move_number(current) + 1 : 1;
  SgfColor color = (move_number % 2 == 1) ? SGF_COLOR_BLACK : SGF_COLOR_WHITE;
  guint8 payload_value = (guint8)move_number;
  GBytes *payload = g_bytes_new(&payload_value, sizeof(payload_value));

  const SgfNode *node = sgf_tree_append_move(self->sgf_tree, color, payload);
  g_bytes_unref(payload);

  if (!node) {
    g_debug("Failed to append synthetic SGF move\n");
    return NULL;
  }

  g_debug("Appended synthetic SGF move %u\n", sgf_node_get_move_number(node));
  sgf_view_refresh(self->sgf_view);
  return node;
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

