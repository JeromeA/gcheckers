#include "sgf_view_scroller.h"

struct _SgfViewScroller {
  GObject parent_instance;
  const SgfNode *remembered_selected;
};

G_DEFINE_TYPE(SgfViewScroller, sgf_view_scroller, G_TYPE_OBJECT)

static void sgf_view_scroller_try_scroll_node_if_present(GtkScrolledWindow *root,
                                                         GHashTable *node_widgets,
                                                         GArray *column_widths,
                                                         GArray *row_heights,
                                                         const SgfNode *selected) {
  g_return_if_fail(GTK_IS_SCROLLED_WINDOW(root));
  g_return_if_fail(node_widgets != NULL);
  g_return_if_fail(column_widths != NULL);
  g_return_if_fail(row_heights != NULL);
  g_return_if_fail(selected != NULL);

  GtkWidget *widget = g_hash_table_lookup(node_widgets, (gpointer)selected);
  if (!widget) {
    g_debug("Unable to find SGF widget for selected node");
    return;
  }

  GtkWidget *parent = gtk_widget_get_parent(widget);
  if (!parent || !GTK_IS_GRID(parent)) {
    g_debug("SGF scroll widget is not attached to a grid");
    return;
  }

  int column = -1;
  int row = -1;
  gtk_grid_query_child(GTK_GRID(parent), widget, &column, &row, NULL, NULL);
  if (column < 0 || row < 0) {
    g_debug("Unable to query SGF grid position for selected node");
    return;
  }

  int width = gtk_widget_get_width(widget);
  int height = gtk_widget_get_height(widget);
  if (width <= 0) {
    int min_width = 0;
    int natural_width = 0;
    gtk_widget_measure(widget, GTK_ORIENTATION_HORIZONTAL, -1, &min_width, &natural_width, NULL, NULL);
    width = MAX(min_width, natural_width);
  }
  if (height <= 0) {
    int min_height = 0;
    int natural_height = 0;
    gtk_widget_measure(widget, GTK_ORIENTATION_VERTICAL, -1, &min_height, &natural_height, NULL, NULL);
    height = MAX(min_height, natural_height);
  }
  if (width <= 0 || height <= 0) {
    g_debug("Selected node bounds are not measurable yet");
    return;
  }

  if (column_widths->len <= (guint)column || row_heights->len <= (guint)row) {
    g_debug("Selected node is outside available SGF layout extents");
    return;
  }

  int column_spacing = gtk_grid_get_column_spacing(GTK_GRID(parent));
  int row_spacing = gtk_grid_get_row_spacing(GTK_GRID(parent));
  int margin_start = gtk_widget_get_margin_start(parent);
  int margin_top = gtk_widget_get_margin_top(parent);

  double x = margin_start + column * column_spacing;
  for (int i = 0; i < column; ++i) {
    x += g_array_index(column_widths, int, i);
  }

  double y = margin_top + row * row_spacing;
  for (int i = 0; i < row; ++i) {
    y += g_array_index(row_heights, int, i);
  }

  width = g_array_index(column_widths, int, column);
  height = g_array_index(row_heights, int, row);

  const double padding = (double)SGF_VIEW_SCROLLER_VISIBILITY_PADDING;
  const double h_start = MAX(0.0, x - padding);
  const double h_end = x + width + padding;
  const double v_start = MAX(0.0, y - padding);
  const double v_end = y + height + padding;

  GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(root);
  GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(root);

  if (hadjustment) {
    gtk_adjustment_clamp_page(hadjustment, h_start, h_end);
  }

  if (vadjustment) {
    gtk_adjustment_clamp_page(vadjustment, v_start, v_end);
  }

  if (!hadjustment && !vadjustment) {
    g_debug("SGF scroll has no adjustments to clamp");
  }
}

static void sgf_view_scroller_class_init(SgfViewScrollerClass */*klass*/) {}

static void sgf_view_scroller_init(SgfViewScroller *self) {
  self->remembered_selected = NULL;
}

SgfViewScroller *sgf_view_scroller_new(void) {
  return g_object_new(SGF_TYPE_VIEW_SCROLLER, NULL);
}

void sgf_view_scroller_request_scroll(SgfViewScroller *self,
                                      GtkScrolledWindow *root,
                                      GHashTable *node_widgets,
                                      GArray *column_widths,
                                      GArray *row_heights,
                                      const SgfNode *selected) {
  g_return_if_fail(SGF_IS_VIEW_SCROLLER(self));
  g_return_if_fail(GTK_IS_SCROLLED_WINDOW(root));
  g_return_if_fail(node_widgets != NULL);
  g_return_if_fail(column_widths != NULL);
  g_return_if_fail(row_heights != NULL);
  g_return_if_fail(selected != NULL);

  /* Two-path scroller truth table:
   * request remembers selection and attempts now;
   * layout updates retry remembered only.
   */
  self->remembered_selected = selected;
  sgf_view_scroller_try_scroll_node_if_present(root,
                                                node_widgets,
                                                column_widths,
                                                row_heights,
                                                selected);
}

void sgf_view_scroller_on_layout_changed(SgfViewScroller *self,
                                         GtkScrolledWindow *root,
                                         GHashTable *node_widgets,
                                         GArray *column_widths,
                                         GArray *row_heights) {
  g_return_if_fail(SGF_IS_VIEW_SCROLLER(self));
  g_return_if_fail(GTK_IS_SCROLLED_WINDOW(root));
  g_return_if_fail(node_widgets != NULL);
  g_return_if_fail(column_widths != NULL);
  g_return_if_fail(row_heights != NULL);

  if (!self->remembered_selected) {
    return;
  }

  sgf_view_scroller_try_scroll_node_if_present(root,
                                                node_widgets,
                                                column_widths,
                                                row_heights,
                                                self->remembered_selected);
}
