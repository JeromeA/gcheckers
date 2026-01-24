#include "sgf_view_scroller.h"

typedef struct {
  GtkScrolledWindow *root;
  GtkWidget *overlay;
  GHashTable *node_widgets;
  const SgfNode *selected;
  SgfViewScroller *scroller;
  int expected_width;
  int expected_height;
  GdkFrameClock *frame_clock;
  gboolean frame_clock_updating;
  guint attempts;
} SgfViewScrollRequest;

struct _SgfViewScroller {
  GObject parent_instance;
  guint pending_tick_id;
  SgfViewScrollRequest *pending_request;
};

G_DEFINE_TYPE(SgfViewScroller, sgf_view_scroller, G_TYPE_OBJECT)

static const guint sgf_view_scroller_max_attempts = 120;

static gboolean sgf_view_scroller_compute_bounds(GtkWidget *widget,
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

  int disc_width = -1;
  int disc_height = -1;
  gtk_widget_get_size_request(widget, &disc_width, &disc_height);
  if (disc_width <= 0 || disc_height <= 0) {
    disc_width = gtk_widget_get_width(widget);
    disc_height = gtk_widget_get_height(widget);
  }
  if (disc_width <= 0 || disc_height <= 0) {
    g_debug("Unable to determine SGF disc size for scrolling");
    return FALSE;
  }

  int column_spacing = gtk_grid_get_column_spacing(GTK_GRID(parent));
  int row_spacing = gtk_grid_get_row_spacing(GTK_GRID(parent));
  int margin_start = gtk_widget_get_margin_start(parent);
  int margin_top = gtk_widget_get_margin_top(parent);

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

static gboolean sgf_view_scroller_scroll_cb(GtkWidget * /*widget*/,
                                            GdkFrameClock * /*frame_clock*/,
                                            gpointer user_data) {
  SgfViewScrollRequest *request = user_data;

  g_return_val_if_fail(request != NULL, G_SOURCE_REMOVE);

  if (!request->selected || !request->node_widgets || !request->root || !request->overlay) {
    g_debug("Incomplete SGF scroll request");
    return G_SOURCE_REMOVE;
  }

  GtkWidget *widget = g_hash_table_lookup(request->node_widgets, (gpointer)request->selected);
  if (!widget) {
    request->attempts++;
    if (request->attempts < 5) {
      return G_SOURCE_CONTINUE;
    }
    g_debug("Unable to find SGF widget for selected node");
    return G_SOURCE_REMOVE;
  }

  graphene_rect_t bounds;
  if (!sgf_view_scroller_compute_bounds(widget, &bounds)) {
    request->attempts++;
    if (request->attempts < 5) {
      return G_SOURCE_CONTINUE;
    }
    g_debug("Unable to compute SGF bounds for selected node");
    return G_SOURCE_REMOVE;
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
                             int expected_height) {
  g_return_if_fail(SGF_IS_VIEW_SCROLLER(self));
  g_return_if_fail(GTK_IS_SCROLLED_WINDOW(root));
  g_return_if_fail(GTK_IS_WIDGET(overlay));
  g_return_if_fail(expected_width >= 0);
  g_return_if_fail(expected_height >= 0);

  sgf_view_scroller_cancel_pending(self);

  SgfViewScrollRequest *request = g_new0(SgfViewScrollRequest, 1);
  request->root = g_object_ref(root);
  request->overlay = g_object_ref(overlay);
  request->node_widgets = node_widgets ? g_hash_table_ref(node_widgets) : NULL;
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
