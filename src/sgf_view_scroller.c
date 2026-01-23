#include "sgf_view_scroller.h"

typedef struct {
  GtkScrolledWindow *root;
  GtkWidget *overlay;
  GHashTable *node_widgets;
  const SgfNode *selected;
} SgfViewScrollRequest;

struct _SgfViewScroller {
  GObject parent_instance;
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

static gboolean sgf_view_scroller_scroll_cb(gpointer user_data) {
  SgfViewScrollRequest *request = user_data;

  g_return_val_if_fail(request != NULL, G_SOURCE_REMOVE);

  if (!request->selected || !request->node_widgets || !request->root || !request->overlay) {
    return G_SOURCE_REMOVE;
  }

  GtkWidget *widget = g_hash_table_lookup(request->node_widgets, (gpointer)request->selected);
  if (!widget) {
    return G_SOURCE_REMOVE;
  }

  graphene_rect_t bounds;
  if (!gtk_widget_compute_bounds(widget, GTK_WIDGET(request->overlay), &bounds)) {
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

static void sgf_view_scroller_class_init(SgfViewScrollerClass *klass) {
  (void)klass;
}

static void sgf_view_scroller_init(SgfViewScroller *self) {
  (void)self;
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

  SgfViewScrollRequest *request = g_new0(SgfViewScrollRequest, 1);
  request->root = g_object_ref(root);
  request->overlay = g_object_ref(overlay);
  request->node_widgets = node_widgets ? g_hash_table_ref(node_widgets) : NULL;
  request->selected = selected;

  g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                  sgf_view_scroller_scroll_cb,
                  request,
                  sgf_view_scroller_request_free);
}
