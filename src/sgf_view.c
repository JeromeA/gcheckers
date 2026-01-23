#include "sgf_view.h"

#include <string.h>

#include <gdk/gdkkeysyms.h>

struct _SgfView {
  GObject parent_instance;
  GtkWidget *root;
  GtkWidget *overlay;
  GtkWidget *lines_area;
  GtkWidget *tree_box;
  SgfTree *tree;
  const SgfNode *selected;
  GHashTable *node_widgets;
};

G_DEFINE_TYPE(SgfView, sgf_view, G_TYPE_OBJECT)

enum { SIGNAL_NODE_SELECTED, SIGNAL_LAST };

static guint sgf_view_signals[SIGNAL_LAST] = {0};

static const int sgf_view_disc_size = 32;
static const int sgf_view_disc_border = 1;
static const int sgf_view_disc_spacing = 8;
static const int sgf_view_disc_stride = sgf_view_disc_size + (sgf_view_disc_border * 2);

static void sgf_view_rebuild(SgfView *self);
static void sgf_view_queue_scroll_to_selected(SgfView *self);

static void sgf_view_clear_container(GtkWidget *container) {
  GtkWidget *child = gtk_widget_get_first_child(container);
  while (child) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_widget_unparent(child);
    child = next;
  }
}

static gboolean sgf_view_get_disc_center(SgfView *self, const SgfNode *node, double *x, double *y) {
  g_return_val_if_fail(SGF_IS_VIEW(self), FALSE);
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(x != NULL, FALSE);
  g_return_val_if_fail(y != NULL, FALSE);

  if (!self->node_widgets || !self->lines_area) {
    return FALSE;
  }

  GtkWidget *widget = g_hash_table_lookup(self->node_widgets, node);
  if (!widget) {
    return FALSE;
  }

  int width = gtk_widget_get_width(widget);
  int height = gtk_widget_get_height(widget);

  graphene_point_t widget_point;
  graphene_point_t translated_point;
  graphene_point_init(&widget_point, width / 2.0, height / 2.0);

  if (!gtk_widget_compute_point(widget, self->lines_area, &widget_point, &translated_point)) {
    return FALSE;
  }

  *x = translated_point.x;
  *y = translated_point.y;
  return TRUE;
}

static void sgf_view_draw_links_for_node(SgfView *self, const SgfNode *node, cairo_t *cr) {
  const GPtrArray *children = sgf_node_get_children(node);
  if (!children || children->len == 0) {
    return;
  }

  double parent_x = 0.0;
  double parent_y = 0.0;
  gboolean has_parent = sgf_view_get_disc_center(self, node, &parent_x, &parent_y);

  for (guint i = 0; i < children->len; ++i) {
    const SgfNode *child = g_ptr_array_index(children, i);
    double child_x = 0.0;
    double child_y = 0.0;
    if (has_parent && sgf_view_get_disc_center(self, child, &child_x, &child_y)) {
      cairo_move_to(cr, parent_x, parent_y);
      cairo_line_to(cr, child_x, child_y);
      cairo_stroke(cr);
    }
    sgf_view_draw_links_for_node(self, child, cr);
  }
}

static void sgf_view_draw_tree(GtkDrawingArea * /*area*/, cairo_t *cr, int width, int height, gpointer user_data) {
  SgfView *self = SGF_VIEW(user_data);

  g_return_if_fail(SGF_IS_VIEW(self));
  g_return_if_fail(cr != NULL);

  if (!self->tree || width <= 0 || height <= 0) {
    return;
  }

  const SgfNode *root = sgf_tree_get_root(self->tree);
  if (!root) {
    return;
  }

  cairo_save(cr);
  cairo_set_source_rgb(cr, 0.45, 0.45, 0.45);
  cairo_set_line_width(cr, 2.0);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  sgf_view_draw_links_for_node(self, root, cr);
  cairo_restore(cr);
}

static void sgf_view_on_disc_clicked(GtkButton *button, gpointer user_data) {
  SgfView *self = SGF_VIEW(user_data);

  g_return_if_fail(SGF_IS_VIEW(self));
  g_return_if_fail(GTK_IS_BUTTON(button));

  const SgfNode *node = g_object_get_data(G_OBJECT(button), "sgf-node");
  if (!node) {
    g_debug("Missing SGF node for clicked disc\n");
    return;
  }

  sgf_view_set_selected(self, node);
  g_signal_emit(self, sgf_view_signals[SIGNAL_NODE_SELECTED], 0, node);
}

static void sgf_view_update_content_size(SgfView *self) {
  g_return_if_fail(SGF_IS_VIEW(self));

  if (!self->tree_box || !self->overlay || !self->lines_area) {
    return;
  }

  int min_width = 0;
  int nat_width = 0;
  int min_height = 0;
  int nat_height = 0;

  gtk_widget_measure(self->tree_box, GTK_ORIENTATION_HORIZONTAL, -1, &min_width, &nat_width, NULL, NULL);
  gtk_widget_measure(self->tree_box,
                     GTK_ORIENTATION_VERTICAL,
                     nat_width,
                     &min_height,
                     &nat_height,
                     NULL,
                     NULL);

  gtk_widget_set_size_request(self->overlay, nat_width, nat_height);
  gtk_widget_set_size_request(self->lines_area, nat_width, nat_height);
}

static const SgfNode *sgf_view_get_first_child(const SgfNode *node) {
  const GPtrArray *children = sgf_node_get_children(node);
  if (!children || children->len == 0) {
    return NULL;
  }
  return g_ptr_array_index(children, 0);
}

static const SgfNode *sgf_view_get_sibling(const SgfNode *node, int offset) {
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

static const SgfNode *sgf_view_next_selection(const SgfNode *current, SgfViewNavigation navigation) {
  if (!current) {
    return NULL;
  }

  if (navigation == SGF_VIEW_NAVIGATE_PARENT) {
    return sgf_node_get_parent(current);
  }

  if (navigation == SGF_VIEW_NAVIGATE_CHILD) {
    return sgf_view_get_first_child(current);
  }

  if (navigation == SGF_VIEW_NAVIGATE_PREVIOUS_SIBLING) {
    return sgf_view_get_sibling(current, -1);
  }

  if (navigation == SGF_VIEW_NAVIGATE_NEXT_SIBLING) {
    return sgf_view_get_sibling(current, 1);
  }

  return NULL;
}

static gboolean sgf_view_update_selection_style(SgfView *self,
                                                const SgfNode *previous,
                                                const SgfNode *current) {
  g_return_val_if_fail(SGF_IS_VIEW(self), FALSE);

  if (!self->node_widgets) {
    return FALSE;
  }

  gboolean handled = TRUE;

  if (previous) {
    GtkWidget *widget = g_hash_table_lookup(self->node_widgets, (gpointer)previous);
    if (widget) {
      gtk_widget_remove_css_class(widget, "sgf-disc-selected");
    } else {
      handled = FALSE;
    }
  }

  if (current) {
    GtkWidget *widget = g_hash_table_lookup(self->node_widgets, (gpointer)current);
    if (widget) {
      gtk_widget_add_css_class(widget, "sgf-disc-selected");
    } else {
      handled = FALSE;
    }
  }

  return handled;
}

static void sgf_view_select_node(SgfView *self, const SgfNode *node, gboolean emit_signal) {
  g_return_if_fail(SGF_IS_VIEW(self));

  const SgfNode *previous = self->selected;
  self->selected = node;
  if (!sgf_view_update_selection_style(self, previous, node)) {
    sgf_view_rebuild(self);
  }
  sgf_view_queue_scroll_to_selected(self);

  if (emit_signal && node) {
    g_signal_emit(self, sgf_view_signals[SIGNAL_NODE_SELECTED], 0, node);
  }
}

static gboolean sgf_view_scroll_to_selected_cb(gpointer user_data) {
  SgfView *self = SGF_VIEW(user_data);

  if (!self->selected || !self->node_widgets || !self->root || !self->overlay) {
    return G_SOURCE_REMOVE;
  }

  GtkWidget *widget = g_hash_table_lookup(self->node_widgets, (gpointer)self->selected);
  if (!widget) {
    return G_SOURCE_REMOVE;
  }

  graphene_rect_t bounds;
  if (!gtk_widget_compute_bounds(widget, self->overlay, &bounds)) {
    return G_SOURCE_REMOVE;
  }

  GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(self->root));
  GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(self->root));

  if (hadjustment) {
    gtk_adjustment_clamp_page(hadjustment,
                              bounds.origin.x,
                              bounds.origin.x + bounds.size.width);
  }

  if (vadjustment) {
    gtk_adjustment_clamp_page(vadjustment,
                              bounds.origin.y,
                              bounds.origin.y + bounds.size.height);
  }

  return G_SOURCE_REMOVE;
}

static void sgf_view_queue_scroll_to_selected(SgfView *self) {
  g_return_if_fail(SGF_IS_VIEW(self));

  g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                  sgf_view_scroll_to_selected_cb,
                  g_object_ref(self),
                  g_object_unref);
}

static gboolean sgf_view_on_key_pressed(GtkEventControllerKey * /*controller*/,
                                        guint keyval,
                                        guint /*keycode*/,
                                        GdkModifierType /*state*/,
                                        gpointer user_data) {
  SgfView *self = SGF_VIEW(user_data);

  g_return_val_if_fail(SGF_IS_VIEW(self), GDK_EVENT_PROPAGATE);

  if (!self->selected) {
    return GDK_EVENT_PROPAGATE;
  }

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

  const SgfNode *target = sgf_view_next_selection(self->selected, navigation);
  if (!target) {
    return GDK_EVENT_PROPAGATE;
  }

  sgf_view_select_node(self, target, TRUE);
  return GDK_EVENT_STOP;
}

static GtkWidget *sgf_view_build_disc(SgfView *self, const SgfNode *node) {
  g_return_val_if_fail(SGF_IS_VIEW(self), NULL);
  g_return_val_if_fail(node != NULL, NULL);

  char label[16];
  g_snprintf(label, sizeof(label), "%u", sgf_node_get_move_number(node));

  GtkWidget *button = gtk_button_new_with_label(label);
  gtk_widget_add_css_class(button, "sgf-disc");
  gtk_widget_set_size_request(button, sgf_view_disc_stride, sgf_view_disc_stride);
  gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(button, GTK_ALIGN_CENTER);

  SgfColor color = sgf_node_get_color(node);
  if (color == SGF_COLOR_BLACK) {
    gtk_widget_add_css_class(button, "sgf-disc-black");
  } else if (color == SGF_COLOR_WHITE) {
    gtk_widget_add_css_class(button, "sgf-disc-white");
  }

  if (self->selected == node) {
    gtk_widget_add_css_class(button, "sgf-disc-selected");
  }

  g_object_set_data(G_OBJECT(button), "sgf-node", (gpointer)node);
  g_signal_connect(button, "clicked", G_CALLBACK(sgf_view_on_disc_clicked), self);

  if (self->node_widgets) {
    g_hash_table_insert(self->node_widgets, (gpointer)node, button);
  }

  return button;
}

static void sgf_view_attach_disc(SgfView *self, const SgfNode *node, guint row, guint column) {
  g_return_if_fail(SGF_IS_VIEW(self));
  g_return_if_fail(node != NULL);

  GtkWidget *disc = sgf_view_build_disc(self, node);
  gtk_grid_attach(GTK_GRID(self->tree_box), disc, (int)column, (int)row, 1, 1);
}

static guint sgf_view_append_branch(SgfView *self, const SgfNode *parent, guint row, guint depth) {
  const GPtrArray *children = sgf_node_get_children(parent);
  if (!children || children->len == 0) {
    return row;
  }

  guint current_row = row;
  for (guint i = 0; i < children->len; ++i) {
    const SgfNode *child = g_ptr_array_index(children, i);
    if (i == 0) {
      sgf_view_attach_disc(self, child, row, depth);
      current_row = sgf_view_append_branch(self, child, row, depth + 1);
    } else {
      guint branch_row = current_row + 1;
      sgf_view_attach_disc(self, child, branch_row, depth);
      current_row = sgf_view_append_branch(self, child, branch_row, depth + 1);
    }
  }
  return current_row;
}

static void sgf_view_rebuild(SgfView *self) {
  g_return_if_fail(SGF_IS_VIEW(self));

  sgf_view_clear_container(self->tree_box);
  g_clear_pointer(&self->node_widgets, g_hash_table_unref);
  self->node_widgets = g_hash_table_new(g_direct_hash, g_direct_equal);

  if (!self->tree) {
    sgf_view_update_content_size(self);
    gtk_widget_queue_draw(self->lines_area);
    return;
  }

  const SgfNode *root = sgf_tree_get_root(self->tree);
  if (!root) {
    sgf_view_update_content_size(self);
    gtk_widget_queue_draw(self->lines_area);
    return;
  }

  sgf_view_append_branch(self, root, 0, 0);
  sgf_view_update_content_size(self);
  gtk_widget_queue_draw(self->lines_area);
}

static void sgf_view_dispose(GObject *object) {
  SgfView *self = SGF_VIEW(object);

  if (self->root && gtk_widget_get_parent(self->root)) {
    gtk_widget_unparent(self->root);
  }

  g_clear_object(&self->tree);
  g_clear_object(&self->root);
  g_clear_pointer(&self->node_widgets, g_hash_table_unref);

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

  if (self->tree) {
    g_clear_object(&self->tree);
  }

  self->tree = tree ? g_object_ref(tree) : NULL;
  self->selected = tree ? sgf_tree_get_current(tree) : NULL;

  sgf_view_rebuild(self);
  sgf_view_queue_scroll_to_selected(self);
}

void sgf_view_set_selected(SgfView *self, const SgfNode *node) {
  g_return_if_fail(SGF_IS_VIEW(self));

  sgf_view_select_node(self, node, FALSE);
}

const SgfNode *sgf_view_get_selected(SgfView *self) {
  g_return_val_if_fail(SGF_IS_VIEW(self), NULL);

  return self->selected;
}

gboolean sgf_view_navigate(SgfView *self, SgfViewNavigation navigation) {
  g_return_val_if_fail(SGF_IS_VIEW(self), FALSE);

  const SgfNode *target = sgf_view_next_selection(self->selected, navigation);
  if (!target) {
    return FALSE;
  }

  sgf_view_select_node(self, target, TRUE);
  return TRUE;
}

void sgf_view_refresh(SgfView *self) {
  g_return_if_fail(SGF_IS_VIEW(self));

  sgf_view_rebuild(self);
  sgf_view_queue_scroll_to_selected(self);
}
