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
                                                     GtkWidget **grid,
                                                     int *row,
                                                     int *height) {
  g_return_val_if_fail(GTK_IS_WIDGET(lines_area), FALSE);
  g_return_val_if_fail(node_widgets != NULL, FALSE);
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(x != NULL, FALSE);
  g_return_val_if_fail(y != NULL, FALSE);
  g_return_val_if_fail(grid != NULL, FALSE);
  g_return_val_if_fail(row != NULL, FALSE);
  g_return_val_if_fail(height != NULL, FALSE);

  GtkWidget *widget = g_hash_table_lookup(node_widgets, (gpointer)node);
  if (!widget) {
    return FALSE;
  }

  GtkWidget *parent = gtk_widget_get_parent(widget);
  if (!parent || !GTK_IS_GRID(parent)) {
    g_debug("SGF link renderer widget not attached to a grid\n");
    return FALSE;
  }

  int column = -1;
  int widget_row = -1;
  gtk_grid_query_child(GTK_GRID(parent), widget, &column, &widget_row, NULL, NULL);
  if (column < 0 || widget_row < 0) {
    g_debug("SGF link renderer unable to query grid position\n");
    return FALSE;
  }

  int widget_width = gtk_widget_get_width(widget);
  int widget_height = gtk_widget_get_height(widget);
  if (widget_width <= 0 || widget_height <= 0) {
    gtk_widget_measure(widget, GTK_ORIENTATION_HORIZONTAL, -1, &widget_width, NULL, NULL, NULL);
    gtk_widget_measure(widget, GTK_ORIENTATION_VERTICAL, -1, &widget_height, NULL, NULL, NULL);
  }
  if (widget_width <= 0 || widget_height <= 0) {
    g_debug("SGF link renderer unable to determine disc size\n");
    return FALSE;
  }

  graphene_point_t widget_point;
  graphene_point_t translated_point;
  graphene_point_init(&widget_point, widget_width / 2.0, widget_height / 2.0);

  if (!gtk_widget_compute_point(widget, lines_area, &widget_point, &translated_point)) {
    return FALSE;
  }

  *x = translated_point.x;
  *y = translated_point.y;
  *grid = parent;
  *row = widget_row;

  int request_height = -1;
  gtk_widget_get_size_request(widget, NULL, &request_height);
  if (request_height > 0) {
    widget_height = MAX(widget_height, request_height);
  }
  *height = widget_height;
  return TRUE;
}

static gboolean sgf_view_link_renderer_get_row_center(GtkWidget *grid,
                                                      GArray *row_heights,
                                                      int row,
                                                      double *out_y) {
  g_return_val_if_fail(GTK_IS_GRID(grid), FALSE);
  g_return_val_if_fail(out_y != NULL, FALSE);

  if (!row_heights || row_heights->len <= (guint)row) {
    g_debug("SGF link renderer missing row heights for row %d\n", row);
    return FALSE;
  }

  int row_height = g_array_index(row_heights, int, row);
  if (row_height <= 0) {
    g_debug("SGF link renderer invalid row height for row %d\n", row);
    return FALSE;
  }

  int row_spacing = gtk_grid_get_row_spacing(GTK_GRID(grid));
  int margin_top = gtk_widget_get_margin_top(grid);
  double origin_y = margin_top + row * row_spacing;
  for (int i = 0; i < row; ++i) {
    origin_y += g_array_index(row_heights, int, i);
  }

  *out_y = origin_y + row_height / 2.0;
  return TRUE;
}

static void sgf_view_link_renderer_draw_links_for_node(GtkWidget *lines_area,
                                                       GHashTable *node_widgets,
                                                       GArray *row_heights,
                                                       const SgfNode *node,
                                                       cairo_t *cr) {
  const GPtrArray *children = sgf_node_get_children(node);
  if (!children || children->len == 0) {
    return;
  }

  double parent_x = 0.0;
  double parent_y = 0.0;
  GtkWidget *parent_grid = NULL;
  int parent_row = -1;
  int parent_height = 0;
  gboolean has_parent = sgf_view_link_renderer_get_disc_info(lines_area,
                                                             node_widgets,
                                                             node,
                                                             &parent_x,
                                                             &parent_y,
                                                             &parent_grid,
                                                             &parent_row,
                                                             &parent_height);
  (void)parent_height;

  for (guint i = 0; i < children->len; ++i) {
    const SgfNode *child = g_ptr_array_index(children, i);
    double child_x = 0.0;
    double child_y = 0.0;
    GtkWidget *child_grid = NULL;
    int child_row = -1;
    int child_height = 0;
    gboolean has_child = sgf_view_link_renderer_get_disc_info(lines_area,
                                                              node_widgets,
                                                              child,
                                                              &child_x,
                                                              &child_y,
                                                              &child_grid,
                                                              &child_row,
                                                              &child_height);
    if (has_parent && has_child) {
      if (child_row == parent_row) {
        cairo_move_to(cr, parent_x, parent_y);
        cairo_line_to(cr, child_x, parent_y);
      } else if (child_row == parent_row + 1) {
        cairo_move_to(cr, parent_x, parent_y);
        cairo_line_to(cr, child_x, child_y);
      } else if (child_row > parent_row + 1) {
        double intermediate_y = 0.0;
        gboolean have_row_center =
          sgf_view_link_renderer_get_row_center(child_grid, row_heights, child_row - 1, &intermediate_y);
        if (!have_row_center) {
          int row_spacing = gtk_grid_get_row_spacing(GTK_GRID(child_grid));
          double step = (double)child_height + row_spacing;
          if (step > 0.0) {
            intermediate_y = child_y - step;
            have_row_center = TRUE;
          }
        }

        if (have_row_center && intermediate_y > parent_y) {
          cairo_move_to(cr, parent_x, parent_y);
          cairo_line_to(cr, parent_x, intermediate_y);
          cairo_line_to(cr, child_x, child_y);
        } else {
          cairo_move_to(cr, parent_x, parent_y);
          cairo_line_to(cr, child_x, child_y);
        }
      } else {
        cairo_move_to(cr, parent_x, parent_y);
        cairo_line_to(cr, child_x, child_y);
      }
      cairo_stroke(cr);
    }
    sgf_view_link_renderer_draw_links_for_node(lines_area, node_widgets, row_heights, child, cr);
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
                                 GArray *row_heights,
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
  sgf_view_link_renderer_draw_links_for_node(lines_area, node_widgets, row_heights, root, cr);
  cairo_restore(cr);
}
