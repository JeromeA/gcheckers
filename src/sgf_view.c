#include "sgf_view.h"

#include <string.h>

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

static void sgf_view_clear_box(GtkWidget *box) {
  GtkWidget *child = gtk_widget_get_first_child(box);
  while (child) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_box_remove(GTK_BOX(box), child);
    child = next;
  }
}

static GtkWidget *sgf_view_build_row(guint depth) {
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, sgf_view_disc_spacing);
  gtk_widget_set_margin_start(row, (int)depth * (sgf_view_disc_stride + sgf_view_disc_spacing));
  return row;
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

static void sgf_view_on_tree_size_allocate(GtkWidget *widget,
                                           int width,
                                           int height,
                                           int /*baseline*/,
                                           gpointer user_data) {
  SgfView *self = SGF_VIEW(user_data);

  g_return_if_fail(SGF_IS_VIEW(self));
  g_return_if_fail(GTK_IS_WIDGET(widget));

  if (!self->lines_area) {
    return;
  }

  gtk_widget_set_size_request(self->lines_area, width, height);
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

static GtkWidget *sgf_view_build_disc(SgfView *self, const SgfNode *node) {
  g_return_val_if_fail(SGF_IS_VIEW(self), NULL);
  g_return_val_if_fail(node != NULL, NULL);

  char label[16];
  g_snprintf(label, sizeof(label), "%u", sgf_node_get_move_number(node));

  GtkWidget *button = gtk_button_new_with_label(label);
  gtk_widget_add_css_class(button, "sgf-disc");
  gtk_widget_set_size_request(button, sgf_view_disc_stride, sgf_view_disc_stride);
  gtk_widget_set_halign(button, GTK_ALIGN_START);
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

static void sgf_view_append_disc(SgfView *self, GtkWidget *row, const SgfNode *node) {
  g_return_if_fail(SGF_IS_VIEW(self));
  g_return_if_fail(GTK_IS_BOX(row));
  g_return_if_fail(node != NULL);

  GtkWidget *disc = sgf_view_build_disc(self, node);
  gtk_box_append(GTK_BOX(row), disc);
}

static void sgf_view_append_branch(SgfView *self, const SgfNode *parent, GtkWidget *row, guint depth) {
  const GPtrArray *children = sgf_node_get_children(parent);
  if (!children || children->len == 0) {
    return;
  }

  for (guint i = 0; i < children->len; ++i) {
    const SgfNode *child = g_ptr_array_index(children, i);
    if (i == 0) {
      sgf_view_append_disc(self, row, child);
      sgf_view_append_branch(self, child, row, depth + 1);
    } else {
      GtkWidget *branch_row = sgf_view_build_row(depth);
      gtk_box_append(GTK_BOX(self->tree_box), branch_row);
      sgf_view_append_disc(self, branch_row, child);
      sgf_view_append_branch(self, child, branch_row, depth + 1);
    }
  }
}

static void sgf_view_rebuild(SgfView *self) {
  g_return_if_fail(SGF_IS_VIEW(self));

  sgf_view_clear_box(self->tree_box);
  g_clear_pointer(&self->node_widgets, g_hash_table_unref);
  self->node_widgets = g_hash_table_new(g_direct_hash, g_direct_equal);

  if (!self->tree) {
    gtk_widget_queue_draw(self->lines_area);
    return;
  }

  GtkWidget *row = sgf_view_build_row(0);
  gtk_box_append(GTK_BOX(self->tree_box), row);

  const SgfNode *root = sgf_tree_get_root(self->tree);
  if (!root) {
    gtk_widget_queue_draw(self->lines_area);
    return;
  }

  sgf_view_append_branch(self, root, row, 0);
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

  self->overlay = gtk_overlay_new();
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(self->root), self->overlay);

  self->lines_area = gtk_drawing_area_new();
  gtk_widget_set_hexpand(self->lines_area, TRUE);
  gtk_widget_set_vexpand(self->lines_area, TRUE);
  gtk_widget_set_can_target(self->lines_area, FALSE);
  gtk_overlay_set_child(GTK_OVERLAY(self->overlay), self->lines_area);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self->lines_area), sgf_view_draw_tree, self, NULL);

  self->tree_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_top(self->tree_box, 8);
  gtk_widget_set_margin_bottom(self->tree_box, 8);
  gtk_widget_set_margin_start(self->tree_box, 8);
  gtk_widget_set_margin_end(self->tree_box, 8);
  gtk_widget_set_hexpand(self->tree_box, TRUE);
  gtk_widget_set_vexpand(self->tree_box, TRUE);
  gtk_overlay_add_overlay(GTK_OVERLAY(self->overlay), self->tree_box);
  g_signal_connect(self->tree_box, "size-allocate", G_CALLBACK(sgf_view_on_tree_size_allocate), self);
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
}

void sgf_view_set_selected(SgfView *self, const SgfNode *node) {
  g_return_if_fail(SGF_IS_VIEW(self));

  self->selected = node;
  sgf_view_rebuild(self);
}

void sgf_view_refresh(SgfView *self) {
  g_return_if_fail(SGF_IS_VIEW(self));

  sgf_view_rebuild(self);
}
