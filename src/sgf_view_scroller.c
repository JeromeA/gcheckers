#include "sgf_view_scroller.h"

struct _SgfViewScroller {
  GObject parent_instance;
  const SgfNode *remembered_selected;
};

G_DEFINE_TYPE(SgfViewScroller, sgf_view_scroller, G_TYPE_OBJECT)

static gboolean sgf_view_scroller_lookup_widget(GHashTable *node_widgets,
                                                const SgfNode *selected,
                                                GtkWidget **out_widget) {
  g_return_val_if_fail(node_widgets != NULL, FALSE);
  g_return_val_if_fail(selected != NULL, FALSE);
  g_return_val_if_fail(out_widget != NULL, FALSE);

  GtkWidget *widget = g_hash_table_lookup(node_widgets, (gpointer)selected);
  if (!widget) {
    g_debug("Unable to find SGF widget for selected node");
    return FALSE;
  }

  *out_widget = widget;
  return TRUE;
}

static gboolean sgf_view_scroller_compute_bounds(GtkWidget *widget,
                                                 GArray *column_widths,
                                                 GArray *row_heights,
                                                 graphene_rect_t *out_bounds) {
  g_return_val_if_fail(GTK_IS_WIDGET(widget), FALSE);
  g_return_val_if_fail(column_widths != NULL, FALSE);
  g_return_val_if_fail(row_heights != NULL, FALSE);
  g_return_val_if_fail(out_bounds != NULL, FALSE);

  GtkWidget *parent = gtk_widget_get_parent(widget);
  if (!parent || !GTK_IS_GRID(parent)) {
    g_debug("SGF scroll widget is not attached to a grid");
    return FALSE;
  }

  int column = -1;
  int row = -1;
  gtk_grid_query_child(GTK_GRID(parent), widget, &column, &row, NULL, NULL);
  if (column < 0 || row < 0) {
    g_debug("Unable to query SGF grid position for selected node");
    return FALSE;
  }

  if (column_widths->len <= (guint)column || row_heights->len <= (guint)row) {
    g_debug("Selected node is outside available SGF layout extents");
    return FALSE;
  }

  int column_spacing = gtk_grid_get_column_spacing(GTK_GRID(parent));
  int row_spacing = gtk_grid_get_row_spacing(GTK_GRID(parent));
  int margin_start = gtk_widget_get_margin_start(parent);
  int margin_top = gtk_widget_get_margin_top(parent);

  double origin_x = margin_start + column * column_spacing;
  for (int i = 0; i < column; ++i) {
    origin_x += g_array_index(column_widths, int, i);
  }

  double origin_y = margin_top + row * row_spacing;
  for (int i = 0; i < row; ++i) {
    origin_y += g_array_index(row_heights, int, i);
  }

  int column_width = g_array_index(column_widths, int, column);
  int row_height = g_array_index(row_heights, int, row);
  if (column_width <= 0 || row_height <= 0) {
    g_debug("Selected node bounds are not measurable yet");
    return FALSE;
  }

  out_bounds->origin.x = origin_x;
  out_bounds->origin.y = origin_y;
  out_bounds->size.width = column_width;
  out_bounds->size.height = row_height;
  return TRUE;
}

static void sgf_view_scroller_clamp_to_bounds(GtkScrolledWindow *root,
                                              const graphene_rect_t *bounds) {
  g_return_if_fail(GTK_IS_SCROLLED_WINDOW(root));
  g_return_if_fail(bounds != NULL);

  const double padding = (double)SGF_VIEW_SCROLLER_VISIBILITY_PADDING;
  const double h_start = MAX(0.0, bounds->origin.x - padding);
  const double h_end = bounds->origin.x + bounds->size.width + padding;
  const double v_start = MAX(0.0, bounds->origin.y - padding);
  const double v_end = bounds->origin.y + bounds->size.height + padding;

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

static gboolean sgf_view_scroller_try_scroll_selected_if_visible(GtkScrolledWindow *root,
                                                                  GHashTable *node_widgets,
                                                                  GArray *column_widths,
                                                                  GArray *row_heights,
                                                                  const SgfNode *selected) {
  g_return_val_if_fail(GTK_IS_SCROLLED_WINDOW(root), FALSE);
  g_return_val_if_fail(node_widgets != NULL, FALSE);
  g_return_val_if_fail(column_widths != NULL, FALSE);
  g_return_val_if_fail(row_heights != NULL, FALSE);
  g_return_val_if_fail(selected != NULL, FALSE);

  GtkWidget *widget = NULL;
  if (!sgf_view_scroller_lookup_widget(node_widgets, selected, &widget)) {
    return FALSE;
  }

  graphene_rect_t bounds;
  if (!sgf_view_scroller_compute_bounds(widget, column_widths, row_heights, &bounds)) {
    return FALSE;
  }

  sgf_view_scroller_clamp_to_bounds(root, &bounds);
  return TRUE;
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

  /* Remember the selected node, then try to scroll if it is visible in the current view. */
  self->remembered_selected = selected;
  (void)sgf_view_scroller_try_scroll_selected_if_visible(root,
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

  /* On layout updates, retry scrolling for the last remembered selection only. */
  if (!self->remembered_selected) {
    return;
  }

  (void)sgf_view_scroller_try_scroll_selected_if_visible(root,
                                                         node_widgets,
                                                         column_widths,
                                                         row_heights,
                                                         self->remembered_selected);
}
