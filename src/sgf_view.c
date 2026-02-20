#include "sgf_view.h"

#include "sgf_view_disc_factory.h"
#include "sgf_view_layout.h"
#include "sgf_view_link_renderer.h"
#include "sgf_view_scroller.h"
#include "sgf_view_selection_controller.h"
#include "widget_utils.h"

#include <gdk/gdkkeysyms.h>

struct _SgfView {
  GObject parent_instance;
  GtkWidget *root;
  GtkWidget *overlay;
  GtkWidget *lines_area;
  GtkWidget *tree_box;
  SgfTree *tree;
  GHashTable *node_widgets;
  SgfViewDiscFactory *disc_factory;
  SgfViewLayout *layout;
  SgfViewLinkRenderer *link_renderer;
  SgfViewSelectionController *selection;
  SgfViewScroller *scroller;
  const SgfNode *styled_selected;
};

G_DEFINE_TYPE(SgfView, sgf_view, G_TYPE_OBJECT)

enum { SIGNAL_NODE_SELECTED, SIGNAL_LAST };

static guint sgf_view_signals[SIGNAL_LAST] = {0};

static const int sgf_view_disc_size = 32;
static const int sgf_view_disc_border = 1;
static const int sgf_view_disc_spacing = 8;
static const int sgf_view_disc_stride = sgf_view_disc_size + (sgf_view_disc_border * 2);

static void sgf_view_rebuild(SgfView *self);
static gboolean sgf_view_apply_local_selection(SgfView *self, const SgfNode *node);

static void sgf_view_log_node_widgets_entry(gpointer key, gpointer value, gpointer user_data) {
  const SgfNode *node = key;
  GtkWidget *widget = value;
  guint *index = user_data;

  g_return_if_fail(index != NULL);

  if (!node) {
    g_debug("SGF rebuild node_widgets[%u]: node=NULL widget=%p", *index, widget);
  } else if (!widget) {
    g_debug("SGF rebuild node_widgets[%u]: node=%p move=%u widget=NULL",
            *index,
            node,
            sgf_node_get_move_number(node));
  } else {
    g_debug("SGF rebuild node_widgets[%u]: node=%p move=%u widget=%p type=%s",
            *index,
            node,
            sgf_node_get_move_number(node),
            widget,
            G_OBJECT_TYPE_NAME(widget));
  }

  (*index)++;
}

static void sgf_view_log_node_widgets_snapshot(GHashTable *node_widgets) {
  g_return_if_fail(node_widgets != NULL);

  guint index = 0;
  guint size = g_hash_table_size(node_widgets);
  g_debug("SGF rebuild node_widgets snapshot: size=%u", size);
  g_hash_table_foreach(node_widgets, sgf_view_log_node_widgets_entry, &index);
}

static void sgf_view_draw_tree(GtkDrawingArea * /*area*/,
                               cairo_t *cr,
                               int width,
                               int height,
                               gpointer user_data) {
  SgfView *self = SGF_VIEW(user_data);

  g_return_if_fail(SGF_IS_VIEW(self));
  g_return_if_fail(cr != NULL);

  sgf_view_link_renderer_draw(self->link_renderer,
                              self->lines_area,
                              self->node_widgets,
                              self->tree,
                              cr,
                              width,
                              height);
}

static void sgf_view_queue_scroll_to_selected(SgfView *self) {
  g_return_if_fail(SGF_IS_VIEW(self));

  const SgfNode *selected = self->tree ? sgf_tree_get_current(self->tree) : NULL;
  if (!selected) {
    g_debug("SGF view scroll request ignored: reason=no-selection");
    return;
  }

  sgf_view_scroller_scroll(self->scroller,
                           GTK_SCROLLED_WINDOW(self->root),
                           self->node_widgets,
                           selected);
}

static void sgf_view_sync_selection_from_model(SgfView *self) {
  g_return_if_fail(SGF_IS_VIEW(self));

  if (!self->tree) {
    self->styled_selected = NULL;
    return;
  }

  const SgfNode *selected = sgf_tree_get_current(self->tree);
  if (!sgf_view_selection_controller_apply_style(self->selection,
                                                 self->styled_selected,
                                                 selected,
                                                 self->node_widgets)) {
    g_debug("SGF view selection not ready to sync from model");
  }
  self->styled_selected = selected;

  sgf_view_queue_scroll_to_selected(self);
}

static void G_GNUC_UNUSED sgf_view_on_post_map(GtkWidget * /*widget*/, gpointer user_data) {
  SgfView *self = SGF_VIEW(user_data);

  g_return_if_fail(SGF_IS_VIEW(self));

  sgf_view_force_layout_sync(self);
}

static void G_GNUC_UNUSED sgf_view_on_post_notify(GObject *object,
                                                  GParamSpec *pspec,
                                                  gpointer user_data) {
  SgfView *self = SGF_VIEW(user_data);

  g_return_if_fail(SGF_IS_VIEW(self));
  g_return_if_fail(G_IS_OBJECT(object));
  g_return_if_fail(pspec != NULL);

  g_debug("SGF view post-notify: source=%s property=%s", G_OBJECT_TYPE_NAME(object), pspec->name);
  sgf_view_force_layout_sync(self);
}

static void sgf_view_select_node(SgfView *self, const SgfNode *node, gboolean emit_signal) {
  g_return_if_fail(SGF_IS_VIEW(self));

  gboolean has_node_selected_handler = g_signal_has_handler_pending(self,
                                                                    sgf_view_signals[SIGNAL_NODE_SELECTED],
                                                                    0,
                                                                    TRUE);

  if (!emit_signal || !has_node_selected_handler) {
    if (!sgf_view_apply_local_selection(self, node)) {
      g_debug("SGF view selection not ready to update selection");
      return;
    }
  }

  if (emit_signal && node) {
    g_signal_emit(self, sgf_view_signals[SIGNAL_NODE_SELECTED], 0, node);
  }
}

static gboolean sgf_view_apply_local_selection(SgfView *self, const SgfNode *node) {
  g_return_val_if_fail(SGF_IS_VIEW(self), FALSE);

  if (!self->tree) {
    g_debug("SGF view has no tree to update selection");
    return FALSE;
  }
  if (node && !sgf_tree_set_current(self->tree, node)) {
    g_debug("SGF view selection target is not part of the current tree");
    return FALSE;
  }

  if (!sgf_view_selection_controller_apply_style(self->selection,
                                                 self->styled_selected,
                                                 node,
                                                 self->node_widgets)) {
    return FALSE;
  }
  self->styled_selected = node;
  sgf_view_queue_scroll_to_selected(self);
  return TRUE;
}

static void sgf_view_on_disc_node_clicked(SgfViewDiscFactory *factory,
                                          const SgfNode *node,
                                          gpointer user_data) {
  SgfView *self = SGF_VIEW(user_data);

  g_return_if_fail(SGF_IS_VIEW(self));
  g_return_if_fail(SGF_IS_VIEW_DISC_FACTORY(factory));

  sgf_view_select_node(self, node, TRUE);
}

static gboolean sgf_view_on_key_pressed(GtkEventControllerKey * /*controller*/,
                                        guint keyval,
                                        guint /*keycode*/,
                                        GdkModifierType /*state*/,
                                        gpointer user_data) {
  SgfView *self = SGF_VIEW(user_data);

  g_return_val_if_fail(SGF_IS_VIEW(self), GDK_EVENT_PROPAGATE);

  SgfViewNavigation navigation;
  if (keyval == GDK_KEY_Left) {
    navigation = SGF_VIEW_NAVIGATE_PARENT;
  } else if (keyval == GDK_KEY_Right) {
    navigation = SGF_VIEW_NAVIGATE_CHILD;
  } else if (keyval == GDK_KEY_Up) {
    navigation = SGF_VIEW_NAVIGATE_PREVIOUS_SIBLING;
  } else if (keyval == GDK_KEY_Down) {
    navigation = SGF_VIEW_NAVIGATE_NEXT_SIBLING;
  } else {
    return GDK_EVENT_PROPAGATE;
  }

  const SgfNode *current = self->tree ? sgf_tree_get_current(self->tree) : NULL;
  const SgfNode *target = sgf_view_selection_controller_next(self->selection, current, navigation);
  if (!target) {
    return GDK_EVENT_STOP;
  }

  sgf_view_select_node(self, target, TRUE);
  return GDK_EVENT_STOP;
}

static void sgf_view_rebuild(SgfView *self) {
  g_return_if_fail(SGF_IS_VIEW(self));

  g_clear_pointer(&self->node_widgets, g_hash_table_unref);
  self->node_widgets = g_hash_table_new(g_direct_hash, g_direct_equal);

  const SgfNode *selected = self->tree ? sgf_tree_get_current(self->tree) : NULL;
  sgf_view_layout_build(self->layout,
                        GTK_GRID(self->tree_box),
                        self->tree,
                        self->node_widgets,
                        self->disc_factory,
                        selected,
                        sgf_view_disc_stride);
  self->styled_selected = selected;
  sgf_view_log_node_widgets_snapshot(self->node_widgets);
  gtk_widget_queue_draw(self->lines_area);
  sgf_view_queue_scroll_to_selected(self);
}

static void sgf_view_dispose(GObject *object) {
  SgfView *self = SGF_VIEW(object);

  gboolean root_removed = TRUE;
  if (self->root) {
    root_removed = gcheckers_widget_remove_from_parent(self->root);
    if (!root_removed && gtk_widget_get_parent(self->root)) {
      g_debug("Failed to remove SGF view root from parent during dispose\n");
    }
  }

  g_clear_object(&self->tree);
  if (root_removed) {
    g_clear_object(&self->root);
  } else {
    self->root = NULL;
  }
  g_clear_pointer(&self->node_widgets, g_hash_table_unref);
  g_clear_object(&self->disc_factory);
  g_clear_object(&self->layout);
  g_clear_object(&self->link_renderer);
  g_clear_object(&self->selection);
  g_clear_object(&self->scroller);

  G_OBJECT_CLASS(sgf_view_parent_class)->dispose(object);
}

static void sgf_view_class_init(SgfViewClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = sgf_view_dispose;

  sgf_view_signals[SIGNAL_NODE_SELECTED] = g_signal_new("node-selected",
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

static void sgf_view_init(SgfView *self) {
  self->root = gtk_scrolled_window_new();
  g_object_ref_sink(self->root);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(self->root),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
  gtk_widget_set_hexpand(self->root, TRUE);
  gtk_widget_set_vexpand(self->root, TRUE);
  gtk_widget_set_focusable(self->root, TRUE);

  self->overlay = gtk_overlay_new();
  gtk_widget_set_focusable(self->overlay, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(self->root), self->overlay);

  self->lines_area = gtk_drawing_area_new();
  gtk_widget_set_hexpand(self->lines_area, TRUE);
  gtk_widget_set_vexpand(self->lines_area, TRUE);
  gtk_widget_set_can_target(self->lines_area, FALSE);
  gtk_overlay_set_child(GTK_OVERLAY(self->overlay), self->lines_area);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self->lines_area), sgf_view_draw_tree, self, NULL);

  self->tree_box = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(self->tree_box), sgf_view_disc_spacing);
  gtk_grid_set_column_spacing(GTK_GRID(self->tree_box), sgf_view_disc_spacing);
  gtk_widget_set_margin_top(self->tree_box, 8);
  gtk_widget_set_margin_bottom(self->tree_box, 8);
  gtk_widget_set_margin_start(self->tree_box, 8);
  gtk_widget_set_margin_end(self->tree_box, 8);
  gtk_widget_set_hexpand(self->tree_box, FALSE);
  gtk_widget_set_vexpand(self->tree_box, FALSE);
  gtk_widget_set_focusable(self->tree_box, TRUE);
  gtk_widget_set_halign(self->tree_box, GTK_ALIGN_START);
  gtk_widget_set_valign(self->tree_box, GTK_ALIGN_START);
  gtk_overlay_add_overlay(GTK_OVERLAY(self->overlay), self->tree_box);
  gtk_overlay_set_measure_overlay(GTK_OVERLAY(self->overlay), self->tree_box, TRUE);

  self->disc_factory = sgf_view_disc_factory_new();
  self->layout = sgf_view_layout_new();
  self->link_renderer = sgf_view_link_renderer_new();
  self->selection = sgf_view_selection_controller_new();
  self->scroller = sgf_view_scroller_new();
  self->styled_selected = NULL;

  g_signal_connect(self->disc_factory,
                   "node-clicked",
                   G_CALLBACK(sgf_view_on_disc_node_clicked),
                   self);

  GtkEventController *key_controller = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(key_controller, GTK_PHASE_CAPTURE);
  g_signal_connect(key_controller, "key-pressed", G_CALLBACK(sgf_view_on_key_pressed), self);
  gtk_widget_add_controller(self->root, key_controller);
}

SgfView *sgf_view_new(void) {
  return g_object_new(SGF_TYPE_VIEW, NULL);
}

GtkWidget *sgf_view_get_widget(SgfView *self) {
  g_return_val_if_fail(SGF_IS_VIEW(self), NULL);

  return self->root;
}

void sgf_view_set_tree(SgfView *self, SgfTree *tree) {
  g_return_if_fail(SGF_IS_VIEW(self));

  if (self->tree == tree) {
    sgf_view_rebuild(self);
    return;
  }

  g_clear_object(&self->tree);
  self->tree = tree ? g_object_ref(tree) : NULL;
  self->styled_selected = tree ? sgf_tree_get_current(tree) : NULL;

  sgf_view_rebuild(self);
}

void sgf_view_set_selected(SgfView *self, const SgfNode *node) {
  g_return_if_fail(SGF_IS_VIEW(self));

  sgf_view_select_node(self, node, FALSE);
}

const SgfNode *sgf_view_get_selected(SgfView *self) {
  g_return_val_if_fail(SGF_IS_VIEW(self), NULL);

  if (!self->tree) {
    return NULL;
  }

  return sgf_tree_get_current(self->tree);
}

gboolean sgf_view_navigate(SgfView *self, SgfViewNavigation navigation) {
  g_return_val_if_fail(SGF_IS_VIEW(self), FALSE);

  const SgfNode *current = self->tree ? sgf_tree_get_current(self->tree) : NULL;
  const SgfNode *target = sgf_view_selection_controller_next(self->selection, current, navigation);
  if (!target) {
    return FALSE;
  }

  sgf_view_select_node(self, target, TRUE);
  return TRUE;
}

void sgf_view_refresh(SgfView *self) {
  g_return_if_fail(SGF_IS_VIEW(self));

  sgf_view_rebuild(self);
}

void sgf_view_force_layout_sync(SgfView *self) {
  g_return_if_fail(SGF_IS_VIEW(self));

  if (!self->root || !self->scroller) {
    g_debug("SGF view missing core widgets for layout sync\n");
    return;
  }

  if (!self->node_widgets) {
    g_debug("SGF view missing layout data for layout sync\n");
    return;
  }

  sgf_view_sync_selection_from_model(self);
}
