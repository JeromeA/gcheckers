#include "sgf_view_scroller.h"

typedef struct {
  GtkScrolledWindow *root;
  GtkWidget *overlay;
  GHashTable *node_widgets;
  const SgfNode *selected;
  SgfViewScroller *scroller;
  guint attempts;
} SgfViewScrollRequest;

struct _SgfViewScroller {
  GObject parent_instance;
  guint pending_tick_id;
  SgfViewScrollRequest *pending_request;
};

G_DEFINE_TYPE(SgfViewScroller, sgf_view_scroller, G_TYPE_OBJECT)

static void sgf_view_scroller_request_free(gpointer data) {
  SgfViewScrollRequest *request = data;

  if (!request) {
    return;
  }

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
  if (!gtk_widget_compute_bounds(widget, GTK_WIDGET(request->overlay), &bounds)) {
    request->attempts++;
    if (request->attempts < 5) {
      return G_SOURCE_CONTINUE;
    }
    g_debug("Unable to compute SGF bounds for selected node");
    return G_SOURCE_REMOVE;
  }

  GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(request->root);
  GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(request->root);

  if (hadjustment) {
    gtk_adjustment_clamp_page(hadjustment, bounds.origin.x, bounds.origin.x + bounds.size.width);
  }

  if (vadjustment) {
    gtk_adjustment_clamp_page(vadjustment, bounds.origin.y, bounds.origin.y + bounds.size.height);
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
                             const SgfNode *selected) {
  g_return_if_fail(SGF_IS_VIEW_SCROLLER(self));
  g_return_if_fail(GTK_IS_SCROLLED_WINDOW(root));
  g_return_if_fail(GTK_IS_WIDGET(overlay));

  sgf_view_scroller_cancel_pending(self);

  SgfViewScrollRequest *request = g_new0(SgfViewScrollRequest, 1);
  request->root = g_object_ref(root);
  request->overlay = g_object_ref(overlay);
  request->node_widgets = node_widgets ? g_hash_table_ref(node_widgets) : NULL;
  request->selected = selected;
  request->scroller = g_object_ref(self);
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
}
