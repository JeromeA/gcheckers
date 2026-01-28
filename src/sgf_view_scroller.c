#include "sgf_view_scroller.h"

typedef struct {
  GtkScrolledWindow *root;
  GtkWidget *layout_widget;
  GHashTable *node_widgets;
  GArray *column_widths;
  GArray *row_heights;
  const SgfNode *selected;
  SgfViewScroller *scroller;
  gulong width_request_id;
  gulong height_request_id;
} SgfViewScrollRequest;

struct _SgfViewScroller {
  GObject parent_instance;
  SgfViewScrollRequest *pending_request;
};

G_DEFINE_TYPE(SgfViewScroller, sgf_view_scroller, G_TYPE_OBJECT)

static gboolean sgf_view_scroller_compute_bounds(GtkWidget *widget,
                                                 GArray *column_widths,
                                                 GArray *row_heights,
                                                 graphene_rect_t *out_bounds) {
  g_return_val_if_fail(GTK_IS_WIDGET(widget), FALSE);
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

  int column_spacing = gtk_grid_get_column_spacing(GTK_GRID(parent));
  int row_spacing = gtk_grid_get_row_spacing(GTK_GRID(parent));
  int margin_start = gtk_widget_get_margin_start(parent);
  int margin_top = gtk_widget_get_margin_top(parent);

  gboolean have_column_extents = column_widths && column_widths->len > (guint)column;
  gboolean have_row_extents = row_heights && row_heights->len > (guint)row;
  if (have_column_extents && have_row_extents) {
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
    if (column_width > 0 && row_height > 0) {
      out_bounds->origin.x = origin_x;
      out_bounds->origin.y = origin_y;
      out_bounds->size.width = column_width;
      out_bounds->size.height = row_height;
      return TRUE;
    }
  }

  graphene_rect_t computed_bounds;
  if (gtk_widget_compute_bounds(widget, parent, &computed_bounds) &&
      computed_bounds.size.width > 0.0f &&
      computed_bounds.size.height > 0.0f) {
    *out_bounds = computed_bounds;
    return TRUE;
  }

  g_debug("Unable to compute SGF bounds for selected node");
  return FALSE;
}

static gboolean sgf_view_scroller_try_scroll(GtkScrolledWindow *root,
                                             GtkWidget *layout_widget,
                                             GHashTable *node_widgets,
                                             GArray *column_widths,
                                             GArray *row_heights,
                                             const SgfNode *selected) {
  g_return_val_if_fail(GTK_IS_SCROLLED_WINDOW(root), FALSE);
  g_return_val_if_fail(GTK_IS_WIDGET(layout_widget), FALSE);
  g_return_val_if_fail(node_widgets != NULL, FALSE);
  g_return_val_if_fail(column_widths != NULL, FALSE);
  g_return_val_if_fail(row_heights != NULL, FALSE);
  g_return_val_if_fail(selected != NULL, FALSE);

  GtkWidget *widget = g_hash_table_lookup(node_widgets, (gpointer)selected);
  if (!widget) {
    g_debug("Unable to find SGF widget for selected node");
    return FALSE;
  }

  graphene_rect_t bounds;
  if (!sgf_view_scroller_compute_bounds(widget, column_widths, row_heights, &bounds)) {
    return FALSE;
  }

  const double padding = (double)SGF_VIEW_SCROLLER_VISIBILITY_PADDING;
  const double h_start = MAX(0.0, bounds.origin.x - padding);
  const double h_end = bounds.origin.x + bounds.size.width + padding;
  const double v_start = MAX(0.0, bounds.origin.y - padding);
  const double v_end = bounds.origin.y + bounds.size.height + padding;

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

  return TRUE;
}

static void sgf_view_scroller_request_free(SgfViewScrollRequest *request) {
  if (!request) {
    return;
  }

  g_clear_object(&request->root);
  g_clear_object(&request->layout_widget);
  g_clear_pointer(&request->node_widgets, g_hash_table_unref);
  g_clear_pointer(&request->column_widths, g_array_unref);
  g_clear_pointer(&request->row_heights, g_array_unref);
  g_free(request);
}

static void sgf_view_scroller_request_complete(SgfViewScrollRequest *request) {
  g_return_if_fail(request != NULL);

  if (request->scroller) {
    if (request->scroller->pending_request == request) {
      request->scroller->pending_request = NULL;
    }
    g_object_unref(request->scroller);
    request->scroller = NULL;
  }

  sgf_view_scroller_request_free(request);
}

static void sgf_view_scroller_cancel_pending(SgfViewScroller *self) {
  g_return_if_fail(SGF_IS_VIEW_SCROLLER(self));

  if (!self->pending_request) {
    return;
  }

  if (self->pending_request->layout_widget && self->pending_request->width_request_id != 0) {
    g_signal_handler_disconnect(self->pending_request->layout_widget,
                                self->pending_request->width_request_id);
    self->pending_request->width_request_id = 0;
  }

  if (self->pending_request->layout_widget && self->pending_request->height_request_id != 0) {
    g_signal_handler_disconnect(self->pending_request->layout_widget,
                                self->pending_request->height_request_id);
    self->pending_request->height_request_id = 0;
  }

  g_clear_pointer(&self->pending_request, sgf_view_scroller_request_complete);
}

static void sgf_view_scroller_on_size_request_notify(GObject * /*object*/,
                                                     GParamSpec * /*pspec*/,
                                                     gpointer user_data) {
  SgfViewScrollRequest *request = user_data;

  g_return_if_fail(request != NULL);

  if (!sgf_view_scroller_try_scroll(request->root,
                                    request->layout_widget,
                                    request->node_widgets,
                                    request->column_widths,
                                    request->row_heights,
                                    request->selected)) {
    return;
  }

  if (request->layout_widget && request->width_request_id != 0) {
    g_signal_handler_disconnect(request->layout_widget, request->width_request_id);
    request->width_request_id = 0;
  }

  if (request->layout_widget && request->height_request_id != 0) {
    g_signal_handler_disconnect(request->layout_widget, request->height_request_id);
    request->height_request_id = 0;
  }

  sgf_view_scroller_request_complete(request);
}

static void sgf_view_scroller_dispose(GObject *object) {
  SgfViewScroller *self = SGF_VIEW_SCROLLER(object);

  sgf_view_scroller_cancel_pending(self);

  G_OBJECT_CLASS(sgf_view_scroller_parent_class)->dispose(object);
}

static void sgf_view_scroller_class_init(SgfViewScrollerClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = sgf_view_scroller_dispose;
}

static void sgf_view_scroller_init(SgfViewScroller *self) {
  self->pending_request = NULL;
}

SgfViewScroller *sgf_view_scroller_new(void) {
  return g_object_new(SGF_TYPE_VIEW_SCROLLER, NULL);
}

void sgf_view_scroller_queue(SgfViewScroller *self,
                             GtkScrolledWindow *root,
                             GtkWidget *layout_widget,
                             GHashTable *node_widgets,
                             GArray *column_widths,
                             GArray *row_heights,
                             const SgfNode *selected) {
  g_return_if_fail(SGF_IS_VIEW_SCROLLER(self));
  g_return_if_fail(GTK_IS_SCROLLED_WINDOW(root));
  g_return_if_fail(GTK_IS_WIDGET(layout_widget));
  g_return_if_fail(node_widgets != NULL);
  g_return_if_fail(column_widths != NULL);
  g_return_if_fail(row_heights != NULL);
  g_return_if_fail(selected != NULL);

  sgf_view_scroller_cancel_pending(self);

  if (!sgf_view_scroller_try_scroll(root,
                                    layout_widget,
                                    node_widgets,
                                    column_widths,
                                    row_heights,
                                    selected)) {
    SgfViewScrollRequest *request = g_new0(SgfViewScrollRequest, 1);
    request->root = g_object_ref(root);
    request->layout_widget = g_object_ref(layout_widget);
    request->node_widgets = g_hash_table_ref(node_widgets);
    request->column_widths = g_array_ref(column_widths);
    request->row_heights = g_array_ref(row_heights);
    request->selected = selected;
    request->scroller = g_object_ref(self);
    request->width_request_id = 0;
    request->height_request_id = 0;

    self->pending_request = request;
    request->width_request_id = g_signal_connect(request->layout_widget,
                                                 "notify::width-request",
                                                 G_CALLBACK(sgf_view_scroller_on_size_request_notify),
                                                 request);
    request->height_request_id = g_signal_connect(request->layout_widget,
                                                  "notify::height-request",
                                                  G_CALLBACK(sgf_view_scroller_on_size_request_notify),
                                                  request);
    if (request->width_request_id == 0 || request->height_request_id == 0) {
      g_debug("Failed to connect SGF scroll size request callbacks");
      if (request->width_request_id != 0) {
        g_signal_handler_disconnect(request->layout_widget, request->width_request_id);
        request->width_request_id = 0;
      }
      if (request->height_request_id != 0) {
        g_signal_handler_disconnect(request->layout_widget, request->height_request_id);
        request->height_request_id = 0;
      }
      self->pending_request = NULL;
      sgf_view_scroller_request_complete(request);
    }
  }
}
