#include "sgf_view_selection_controller.h"

struct _SgfViewSelectionController {
  GObject parent_instance;
};

G_DEFINE_TYPE(SgfViewSelectionController, sgf_view_selection_controller, G_TYPE_OBJECT)

static const SgfNode *sgf_view_selection_controller_get_first_child(const SgfNode *node) {
  const GPtrArray *children = sgf_node_get_children(node);
  if (!children || children->len == 0) {
    return NULL;
  }
  return g_ptr_array_index(children, 0);
}

static const SgfNode *sgf_view_selection_controller_get_sibling(const SgfNode *node, int offset) {
  const SgfNode *parent = sgf_node_get_parent(node);
  if (!parent) {
    return NULL;
  }

  const GPtrArray *children = sgf_node_get_children(parent);
  if (!children || children->len == 0) {
    return NULL;
  }

  for (guint i = 0; i < children->len; ++i) {
    if (g_ptr_array_index(children, i) == node) {
      int target = (int)i + offset;
      if (target < 0 || target >= (int)children->len) {
        return NULL;
      }
      return g_ptr_array_index(children, (guint)target);
    }
  }

  return NULL;
}

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
  (void)self;
}

SgfViewSelectionController *sgf_view_selection_controller_new(void) {
  return g_object_new(SGF_TYPE_VIEW_SELECTION_CONTROLLER, NULL);
}

gboolean sgf_view_selection_controller_apply_style(SgfViewSelectionController *self,
                                                   const SgfNode *previous,
                                                   const SgfNode *current,
                                                   GHashTable *node_widgets) {
  g_return_val_if_fail(SGF_IS_VIEW_SELECTION_CONTROLLER(self), FALSE);

  return sgf_view_selection_controller_update_style(previous, current, node_widgets);
}

const SgfNode *sgf_view_selection_controller_next(SgfViewSelectionController *self,
                                                  const SgfNode *current,
                                                  SgfViewNavigation navigation) {
  g_return_val_if_fail(SGF_IS_VIEW_SELECTION_CONTROLLER(self), NULL);
  if (!current) {
    return NULL;
  }

  if (navigation == SGF_VIEW_NAVIGATE_PARENT) {
    return sgf_node_get_parent(current);
  }

  if (navigation == SGF_VIEW_NAVIGATE_CHILD) {
    return sgf_view_selection_controller_get_first_child(current);
  }

  if (navigation == SGF_VIEW_NAVIGATE_PREVIOUS_SIBLING) {
    return sgf_view_selection_controller_get_sibling(current, -1);
  }

  if (navigation == SGF_VIEW_NAVIGATE_NEXT_SIBLING) {
    return sgf_view_selection_controller_get_sibling(current, 1);
  }

  return NULL;
}
