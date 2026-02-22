#include "sgf_view_link_renderer.h"

struct _SgfViewLinkRenderer {
  GObject parent_instance;
};

G_DEFINE_TYPE(SgfViewLinkRenderer, sgf_view_link_renderer, G_TYPE_OBJECT)

static gboolean sgf_view_link_renderer_get_disc_info(GtkWidget *lines_area,
                                                     GHashTable *node_widgets,
                                                     const SgfNode *node,
                                                     double *x,
                                                     double *y,
                                                     graphene_rect_t *bounds) {
  g_return_val_if_fail(GTK_IS_WIDGET(lines_area), FALSE);
  g_return_val_if_fail(node_widgets != NULL, FALSE);
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(x != NULL, FALSE);
  g_return_val_if_fail(y != NULL, FALSE);
  g_return_val_if_fail(bounds != NULL, FALSE);

  GtkWidget *widget = g_hash_table_lookup(node_widgets, (gpointer)node);
  if (!widget) {
    return FALSE;
  }

  GtkWidget *parent = gtk_widget_get_parent(widget);
  if (!parent || !GTK_IS_GRID(parent)) {
    g_debug("SGF link renderer widget not attached to a grid\n");
    return FALSE;
  }

  if (!gtk_widget_compute_bounds(widget, lines_area, bounds)) {
    g_debug("SGF link renderer unable to compute disc bounds\n");
    return FALSE;
  }

  *x = bounds->origin.x + (bounds->size.width / 2.0);
  *y = bounds->origin.y + (bounds->size.height / 2.0);
  return TRUE;
}

static void sgf_view_link_renderer_draw_links_for_node(GtkWidget *lines_area,
                                                       GHashTable *node_widgets,
                                                       const SgfNode *node,
                                                       cairo_t *cr) {
  const GPtrArray *children = sgf_node_get_children(node);
  if (!children || children->len == 0) {
    return;
  }

  double parent_x = 0.0;
  double parent_y = 0.0;
  graphene_rect_t parent_bounds;
  gboolean has_parent = sgf_view_link_renderer_get_disc_info(lines_area,
                                                             node_widgets,
                                                             node,
                                                             &parent_x,
                                                             &parent_y,
                                                             &parent_bounds);

  for (guint i = 0; i < children->len; ++i) {
    const SgfNode *child = g_ptr_array_index(children, i);
    double child_x = 0.0;
    double child_y = 0.0;
    graphene_rect_t child_bounds;
    gboolean has_child = sgf_view_link_renderer_get_disc_info(lines_area,
                                                              node_widgets,
                                                              child,
                                                              &child_x,
                                                              &child_y,
                                                              &child_bounds);
    if (has_parent && has_child) {
      if (i >= 2) {
        const SgfNode *previous_child = g_ptr_array_index(children, i - 1);
        double previous_child_x = 0.0;
        double previous_child_y = 0.0;
        graphene_rect_t previous_child_bounds;
        gboolean has_previous_child = sgf_view_link_renderer_get_disc_info(lines_area,
                                                                           node_widgets,
                                                                           previous_child,
                                                                           &previous_child_x,
                                                                           &previous_child_y,
                                                                           &previous_child_bounds);
        (void)previous_child_x;
        (void)previous_child_bounds;
        if (has_previous_child) {
          cairo_move_to(cr, parent_x, parent_y);
          cairo_line_to(cr, parent_x, previous_child_y);
          cairo_line_to(cr, child_x, child_y);
          cairo_stroke(cr);
          sgf_view_link_renderer_draw_links_for_node(lines_area, node_widgets, child, cr);
          continue;
        }
      }

      cairo_move_to(cr, parent_x, parent_y);
      cairo_line_to(cr, child_x, child_y);
      cairo_stroke(cr);
    }
    sgf_view_link_renderer_draw_links_for_node(lines_area, node_widgets, child, cr);
  }
}

static void sgf_view_link_renderer_class_init(SgfViewLinkRendererClass *klass) {
  (void)klass;
}

static void sgf_view_link_renderer_init(SgfViewLinkRenderer *self) {
  (void)self;
}

SgfViewLinkRenderer *sgf_view_link_renderer_new(void) {
  return g_object_new(SGF_TYPE_VIEW_LINK_RENDERER, NULL);
}

void sgf_view_link_renderer_draw(SgfViewLinkRenderer *self,
                                 GtkWidget *lines_area,
                                 GHashTable *node_widgets,
                                 SgfTree *tree,
                                 cairo_t *cr,
                                 int width,
                                 int height) {
  g_return_if_fail(SGF_IS_VIEW_LINK_RENDERER(self));
  g_return_if_fail(GTK_IS_WIDGET(lines_area));
  g_return_if_fail(cr != NULL);

  if (!tree || width <= 0 || height <= 0 || !node_widgets) {
    return;
  }

  const SgfNode *root = sgf_tree_get_root(tree);
  if (!root) {
    return;
  }

  cairo_save(cr);
  cairo_set_source_rgb(cr, 0.45, 0.45, 0.45);
  cairo_set_line_width(cr, 2.0);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  sgf_view_link_renderer_draw_links_for_node(lines_area, node_widgets, root, cr);
  cairo_restore(cr);
}
