#include "sgf_view_scroller.h"

#include <math.h>

typedef struct {
  GtkScrolledWindow *root;
  GtkWidget *overlay;
  GHashTable *node_widgets;
  GArray *column_widths;
  GArray *row_heights;
  const SgfNode *selected;
  SgfViewScroller *scroller;
  int expected_width;
  int expected_height;
  GdkFrameClock *frame_clock;
  gboolean frame_clock_updating;
  guint attempts;
  graphene_rect_t last_bounds;
  gboolean have_last_bounds;
  guint stable_frames;
  guint ticks;
} SgfViewScrollRequest;

struct _SgfViewScroller {
  GObject parent_instance;
  guint pending_tick_id;
  SgfViewScrollRequest *pending_request;
};

G_DEFINE_TYPE(SgfViewScroller, sgf_view_scroller, G_TYPE_OBJECT)

static const guint sgf_view_scroller_max_attempts = 120;
static const guint sgf_view_scroller_stable_frames_required = 1;
static const guint sgf_view_scroller_min_ticks = 1;

static gboolean sgf_view_scroller_compute_bounds(GtkWidget *widget,
                                                 GArray *column_widths,
                                                 GArray *row_heights,
                                                 graphene_rect_t *out_bounds,
                                                 gboolean *out_valid) {
  g_return_val_if_fail(GTK_IS_WIDGET(widget), FALSE);
  g_return_val_if_fail(out_bounds != NULL, FALSE);
  g_return_val_if_fail(out_valid != NULL, FALSE);

  *out_valid = FALSE;

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
      *out_valid = TRUE;
      return TRUE;
    }
  }

  int request_width = -1;
  int request_height = -1;
  gtk_widget_get_size_request(widget, &request_width, &request_height);
  if (request_width <= 0 || request_height <= 0) {
    gtk_widget_measure(widget,
                       GTK_ORIENTATION_HORIZONTAL,
                       -1,
                       &request_width,
                       NULL,
                       NULL,
                       NULL);
    gtk_widget_measure(widget,
                       GTK_ORIENTATION_VERTICAL,
                       -1,
                       &request_height,
                       NULL,
                       NULL,
                       NULL);
  }
  if (request_width <= 0 || request_height <= 0) {
    g_debug("Unable to determine SGF disc request size for scrolling");
    return FALSE;
  }

  graphene_rect_t computed_bounds;
  gboolean have_computed_bounds = gtk_widget_compute_bounds(widget, parent, &computed_bounds);
  if (have_computed_bounds &&
      computed_bounds.size.width > 0.0f &&
      computed_bounds.size.height > 0.0f) {
    const double min_origin_x = margin_start + column * (request_width + column_spacing);
    const double min_origin_y = margin_top + row * (request_height + row_spacing);
    const double epsilon = 1.0;
    gboolean origin_valid = computed_bounds.origin.x + epsilon >= min_origin_x &&
                            computed_bounds.origin.y + epsilon >= min_origin_y;
    if (origin_valid) {
      *out_bounds = computed_bounds;
      *out_valid = TRUE;
      return TRUE;
    }
  }

  int disc_width = -1;
  int disc_height = -1;
  disc_width = gtk_widget_get_width(widget);
  disc_height = gtk_widget_get_height(widget);
  if (disc_width <= 0 || disc_height <= 0) {
    gtk_widget_get_size_request(widget, &disc_width, &disc_height);
  }
  if (disc_width <= 0 || disc_height <= 0) {
    gtk_widget_measure(widget,
                       GTK_ORIENTATION_HORIZONTAL,
                       -1,
                       &disc_width,
                       NULL,
                       NULL,
                       NULL);
    gtk_widget_measure(widget,
                       GTK_ORIENTATION_VERTICAL,
                       -1,
                       &disc_height,
                       NULL,
                       NULL,
                       NULL);
  }
  if (disc_width <= 0 || disc_height <= 0) {
    g_debug("Unable to determine SGF disc size for scrolling");
    return FALSE;
  }

  disc_width = MAX(disc_width, request_width);
  disc_height = MAX(disc_height, request_height);

  out_bounds->origin.x = margin_start + column * (disc_width + column_spacing);
  out_bounds->origin.y = margin_top + row * (disc_height + row_spacing);
  out_bounds->size.width = disc_width;
  out_bounds->size.height = disc_height;

  return TRUE;
}

static void sgf_view_scroller_request_free(gpointer data) {
  SgfViewScrollRequest *request = data;

  if (!request) {
    return;
  }

  if (request->frame_clock_updating && request->frame_clock) {
    gdk_frame_clock_end_updating(request->frame_clock);
    request->frame_clock_updating = FALSE;
  }

  g_clear_object(&request->frame_clock);
  g_clear_object(&request->root);
  g_clear_object(&request->overlay);
  g_clear_pointer(&request->node_widgets, g_hash_table_unref);
  g_clear_pointer(&request->column_widths, g_array_unref);
  g_clear_pointer(&request->row_heights, g_array_unref);
  g_free(request);
}

static void sgf_view_scroller_request_complete(gpointer data) {
  SgfViewScrollRequest *request = data;

  g_return_if_fail(request != NULL);

  if (request->scroller) {
    if (request->scroller->pending_request == request) {
      request->scroller->pending_request = NULL;
      request->scroller->pending_tick_id = 0;
    }
    g_object_unref(request->scroller);
    request->scroller = NULL;
  }

  sgf_view_scroller_request_free(request);
}

static void sgf_view_scroller_cancel_pending(SgfViewScroller *self) {
  g_return_if_fail(SGF_IS_VIEW_SCROLLER(self));

  if (self->pending_tick_id != 0 && self->pending_request && self->pending_request->overlay) {
    GtkWidget *overlay = self->pending_request->overlay;
    guint tick_id = self->pending_tick_id;

    self->pending_tick_id = 0;
    self->pending_request = NULL;
    gtk_widget_remove_tick_callback(overlay, tick_id);
    return;
  }

  self->pending_tick_id = 0;
  g_clear_pointer(&self->pending_request, sgf_view_scroller_request_complete);
}

static gboolean sgf_view_scroller_axis_visible(GtkAdjustment *adjustment,
                                               double start,
                                               double end) {
  if (!adjustment) {
    return TRUE;
  }

  double value = gtk_adjustment_get_value(adjustment);
  double visible_end = value + gtk_adjustment_get_page_size(adjustment);
  const double epsilon = 1.0;

  return value <= start + epsilon && visible_end + epsilon >= end;
}

static gboolean sgf_view_scroller_bounds_equal(const graphene_rect_t *a,
                                               const graphene_rect_t *b) {
  g_return_val_if_fail(a != NULL, FALSE);
  g_return_val_if_fail(b != NULL, FALSE);

  const double epsilon = 0.5;

  return fabs(a->origin.x - b->origin.x) <= epsilon &&
         fabs(a->origin.y - b->origin.y) <= epsilon &&
         fabs(a->size.width - b->size.width) <= epsilon &&
         fabs(a->size.height - b->size.height) <= epsilon;
}

static gboolean sgf_view_scroller_scroll_cb(GtkWidget * /*widget*/,
                                            GdkFrameClock * /*frame_clock*/,
                                            gpointer user_data) {
  SgfViewScrollRequest *request = user_data;

  g_return_val_if_fail(request != NULL, G_SOURCE_REMOVE);

  request->ticks++;

  if (!request->selected || !request->node_widgets || !request->root || !request->overlay) {
    g_debug("Incomplete SGF scroll request");
    return G_SOURCE_REMOVE;
  }

  GtkWidget *widget = g_hash_table_lookup(request->node_widgets, (gpointer)request->selected);
  if (!widget) {
    request->attempts++;
    if (request->attempts < sgf_view_scroller_max_attempts) {
      return G_SOURCE_CONTINUE;
    }
    g_debug("Unable to find SGF widget for selected node");
    return G_SOURCE_REMOVE;
  }

  graphene_rect_t bounds;
  gboolean bounds_valid = FALSE;
  if (!sgf_view_scroller_compute_bounds(widget,
                                        request->column_widths,
                                        request->row_heights,
                                        &bounds,
                                        &bounds_valid)) {
    request->attempts++;
    if (request->attempts < sgf_view_scroller_max_attempts) {
      return G_SOURCE_CONTINUE;
    }
    g_debug("Unable to compute SGF bounds for selected node");
    return G_SOURCE_REMOVE;
  }

  if (!bounds_valid) {
    request->have_last_bounds = FALSE;
    request->stable_frames = 0;
  } else if (request->have_last_bounds &&
             sgf_view_scroller_bounds_equal(&request->last_bounds, &bounds)) {
    request->stable_frames++;
  } else {
    request->last_bounds = bounds;
    request->have_last_bounds = TRUE;
    request->stable_frames = 0;
  }

  GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(request->root);
  GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(request->root);

  if (vadjustment) {
    double v_upper = gtk_adjustment_get_upper(vadjustment);
    double v_expected = (double)request->expected_height;
    if (request->expected_height > 0 && v_upper + 1.0 < v_expected) {
      gtk_adjustment_set_upper(vadjustment, v_expected);
      v_upper = v_expected;
    }

    double v_page_size = gtk_adjustment_get_page_size(vadjustment);
    double v_scrollable = v_upper - v_page_size;
    gboolean v_expect_scrollable = request->expected_height > 0 &&
                                   v_expected > v_page_size + 1.0;
    gboolean v_wait_for_scrollable = v_expect_scrollable && bounds.origin.y > 0.0 &&
                                     v_scrollable <= 0.0 &&
                                     request->attempts < sgf_view_scroller_max_attempts;
    if (v_wait_for_scrollable) {
      request->attempts++;
      return G_SOURCE_CONTINUE;
    }
  }

  if (hadjustment) {
    double h_upper = gtk_adjustment_get_upper(hadjustment);
    double h_expected = (double)request->expected_width;
    if (request->expected_width > 0 && h_upper + 1.0 < h_expected) {
      gtk_adjustment_set_upper(hadjustment, h_expected);
      h_upper = h_expected;
    }

    double h_page_size = gtk_adjustment_get_page_size(hadjustment);
    double h_scrollable = h_upper - h_page_size;
    gboolean h_expect_scrollable = request->expected_width > 0 &&
                                   h_expected > h_page_size + 1.0;
    gboolean h_wait_for_scrollable = h_expect_scrollable && bounds.origin.x > 0.0 &&
                                     h_scrollable <= 0.0 &&
                                     request->attempts < sgf_view_scroller_max_attempts;
    if (h_wait_for_scrollable) {
      request->attempts++;
      return G_SOURCE_CONTINUE;
    }
  }

  const double padding = (double)SGF_VIEW_SCROLLER_VISIBILITY_PADDING;
  const double h_start = MAX(0.0, bounds.origin.x - padding);
  const double h_end = bounds.origin.x + bounds.size.width + padding;
  const double v_start = MAX(0.0, bounds.origin.y - padding);
  const double v_end = bounds.origin.y + bounds.size.height + padding;

  if (hadjustment) {
    gtk_adjustment_clamp_page(hadjustment, h_start, h_end);
  }

  if (vadjustment) {
    gtk_adjustment_clamp_page(vadjustment, v_start, v_end);
  }

  gboolean fully_visible = sgf_view_scroller_axis_visible(hadjustment, h_start, h_end) &&
                           sgf_view_scroller_axis_visible(vadjustment, v_start, v_end);
  gboolean bounds_stable = bounds_valid &&
                           request->have_last_bounds &&
                           request->stable_frames >= sgf_view_scroller_stable_frames_required;
  gboolean min_ticks_elapsed = request->ticks >= sgf_view_scroller_min_ticks;
  if (fully_visible && bounds_stable && min_ticks_elapsed) {
    return G_SOURCE_REMOVE;
  }

  if (request->attempts + 1 >= sgf_view_scroller_max_attempts) {
    g_debug("SGF scroll request did not converge within the attempt limit");
    return G_SOURCE_REMOVE;
  }

  request->attempts++;
  return G_SOURCE_CONTINUE;
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
  self->pending_tick_id = 0;
  self->pending_request = NULL;
}

SgfViewScroller *sgf_view_scroller_new(void) {
  return g_object_new(SGF_TYPE_VIEW_SCROLLER, NULL);
}

void sgf_view_scroller_queue(SgfViewScroller *self,
                             GtkScrolledWindow *root,
                             GtkWidget *overlay,
                             GHashTable *node_widgets,
                             const SgfNode *selected,
                             int expected_width,
                             int expected_height,
                             GArray *column_widths,
                             GArray *row_heights) {
  g_return_if_fail(SGF_IS_VIEW_SCROLLER(self));
  g_return_if_fail(GTK_IS_SCROLLED_WINDOW(root));
  g_return_if_fail(GTK_IS_WIDGET(overlay));
  g_return_if_fail(expected_width >= 0);
  g_return_if_fail(expected_height >= 0);
  g_return_if_fail(column_widths != NULL);
  g_return_if_fail(row_heights != NULL);

  sgf_view_scroller_cancel_pending(self);

  SgfViewScrollRequest *request = g_new0(SgfViewScrollRequest, 1);
  request->root = g_object_ref(root);
  request->overlay = g_object_ref(overlay);
  request->node_widgets = node_widgets ? g_hash_table_ref(node_widgets) : NULL;
  request->column_widths = g_array_ref(column_widths);
  request->row_heights = g_array_ref(row_heights);
  request->selected = selected;
  request->scroller = g_object_ref(self);
  request->expected_width = expected_width;
  request->expected_height = expected_height;
  request->attempts = 0;

  self->pending_request = request;
  self->pending_tick_id = gtk_widget_add_tick_callback(request->overlay,
                                                       sgf_view_scroller_scroll_cb,
                                                       request,
                                                       sgf_view_scroller_request_complete);
  if (self->pending_tick_id == 0) {
    g_debug("Failed to queue SGF scroll tick callback");
    self->pending_request = NULL;
    sgf_view_scroller_request_complete(request);
    return;
  }

  gtk_widget_queue_draw(request->overlay);

  GdkFrameClock *frame_clock = gtk_widget_get_frame_clock(request->overlay);
  if (!frame_clock) {
    if (gtk_widget_get_mapped(request->overlay)) {
      g_debug("Missing frame clock for SGF scroll");
    }
    return;
  }

  request->frame_clock = g_object_ref(frame_clock);
  gdk_frame_clock_begin_updating(frame_clock);
  request->frame_clock_updating = TRUE;

  gdk_frame_clock_request_phase(frame_clock, GDK_FRAME_CLOCK_PHASE_UPDATE);
  gdk_frame_clock_request_phase(frame_clock, GDK_FRAME_CLOCK_PHASE_LAYOUT);
}
