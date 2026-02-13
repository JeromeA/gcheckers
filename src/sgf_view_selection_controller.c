#include "sgf_view_selection_controller.h"

struct _SgfViewSelectionController {
  GObject parent_instance;
  const SgfNode *selected;
};

G_DEFINE_TYPE(SgfViewSelectionController, sgf_view_selection_controller, G_TYPE_OBJECT)

static gboolean sgf_view_selection_controller_update_style(const SgfNode *previous,
                                                           const SgfNode *current,
                                                           GHashTable *node_widgets) {
  if (!node_widgets) {
    g_debug("Missing node widgets when updating selection style");
    return FALSE;
  }

  gboolean handled = TRUE;

  if (previous) {
    GtkWidget *widget = g_hash_table_lookup(node_widgets, (gpointer)previous);
    if (widget) {
      gtk_widget_remove_css_class(widget, "sgf-disc-selected");
    } else {
      handled = FALSE;
    }
  }

  if (current) {
    GtkWidget *widget = g_hash_table_lookup(node_widgets, (gpointer)current);
    if (widget) {
      gtk_widget_add_css_class(widget, "sgf-disc-selected");
    } else {
      handled = FALSE;
    }
  }

  return handled;
}

static void sgf_view_selection_controller_class_init(SgfViewSelectionControllerClass *klass) {
  (void)klass;
}

static void sgf_view_selection_controller_init(SgfViewSelectionController *self) {
  self->selected = NULL;
}

SgfViewSelectionController *sgf_view_selection_controller_new(void) {
  return g_object_new(SGF_TYPE_VIEW_SELECTION_CONTROLLER, NULL);
}

const SgfNode *sgf_view_selection_controller_get_selected(SgfViewSelectionController *self) {
  g_return_val_if_fail(SGF_IS_VIEW_SELECTION_CONTROLLER(self), NULL);

  return self->selected;
}

void sgf_view_selection_controller_set_selected_raw(SgfViewSelectionController *self, const SgfNode *node) {
  g_return_if_fail(SGF_IS_VIEW_SELECTION_CONTROLLER(self));

  self->selected = node;
}

gboolean sgf_view_selection_controller_set_selected(SgfViewSelectionController *self,
                                                    const SgfNode *node,
                                                    GHashTable *node_widgets) {
  g_return_val_if_fail(SGF_IS_VIEW_SELECTION_CONTROLLER(self), FALSE);

  const SgfNode *previous = self->selected;
  self->selected = node;

  return sgf_view_selection_controller_update_style(previous, node, node_widgets);
}

