#include "sgf_view_scroller.h"

/*
 * SGF scroller behavior:
 * - A scroll request remembers the selected node and tries to scroll immediately.
 * - A layout change retries scrolling for the remembered node.
 * - Missing or not-yet-measurable geometry is treated as a no-op for that attempt.
 */

struct _SgfViewScroller {
  GObject parent_instance;
  const SgfNode *remembered_selected;
};

G_DEFINE_TYPE(SgfViewScroller, sgf_view_scroller, G_TYPE_OBJECT)

typedef struct {
  SgfViewScroller *self;
  GtkScrolledWindow *root;
  GtkWidget *widget;
} SgfViewScrollerIdleRetry;

static void sgf_view_scroller_try_scroll_widget(SgfViewScroller *self,
                                                GtkScrolledWindow *root,
                                                GtkWidget *widget);

static void sgf_view_scroller_idle_retry(gpointer user_data) {
  SgfViewScrollerIdleRetry *retry = user_data;

  g_return_if_fail(retry != NULL);
  g_return_if_fail(SGF_IS_VIEW_SCROLLER(retry->self));
  g_return_if_fail(GTK_IS_SCROLLED_WINDOW(retry->root));
  g_return_if_fail(GTK_IS_WIDGET(retry->widget));

  sgf_view_scroller_try_scroll_widget(retry->self, retry->root, retry->widget);

  g_clear_object(&retry->widget);
  g_clear_object(&retry->root);
  g_clear_object(&retry->self);
  g_free(retry);
}

static void sgf_view_scroller_schedule_idle_retry(SgfViewScroller *self,
                                                  GtkScrolledWindow *root,
                                                  GtkWidget *widget) {
  g_return_if_fail(SGF_IS_VIEW_SCROLLER(self));
  g_return_if_fail(GTK_IS_SCROLLED_WINDOW(root));
  g_return_if_fail(GTK_IS_WIDGET(widget));

  SgfViewScrollerIdleRetry *retry = g_new0(SgfViewScrollerIdleRetry, 1);
  if (!retry) {
    g_debug("Unable to allocate SGF scroller idle retry state");
    return;
  }

  retry->self = g_object_ref(self);
  retry->root = g_object_ref(root);
  retry->widget = g_object_ref(widget);
  g_idle_add_once(sgf_view_scroller_idle_retry, retry);
  g_debug("SGF scroll attempt: scheduled idle retry");
}

static void sgf_view_scroller_try_scroll_widget(SgfViewScroller *self,
                                                GtkScrolledWindow *root,
                                                GtkWidget *widget) {
  g_return_if_fail(SGF_IS_VIEW_SCROLLER(self));
  g_return_if_fail(GTK_IS_SCROLLED_WINDOW(root));
  g_return_if_fail(GTK_IS_WIDGET(widget));

  GtkWidget *content = gtk_widget_get_ancestor(widget, GTK_TYPE_OVERLAY);
  if (!content) {
    g_debug("SGF scroll attempt: selected widget has no overlay ancestor");
    return;
  }

  graphene_rect_t bounds;
  if (!gtk_widget_compute_bounds(widget, content, &bounds)) {
    g_debug("SGF scroll attempt: selected bounds unavailable");
    sgf_view_scroller_schedule_idle_retry(self, root, widget);
    return;
  }
  g_debug("SGF scroll attempt: selected bounds [x=%.1f y=%.1f w=%.1f h=%.1f]",
          bounds.origin.x,
          bounds.origin.y,
          bounds.size.width,
          bounds.size.height);

  if (bounds.origin.x < 0.0) {
    g_debug("SGF scroll attempt: selected bounds x %.1f is negative", bounds.origin.x);
    sgf_view_scroller_schedule_idle_retry(self, root, widget);
    return;
  }

  GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(root);
  if (!hadjustment) {
    g_debug("SGF scroll attempt: horizontal adjustment unavailable");
    return;
  }

  const double h_start = bounds.origin.x;
  const double h_end = bounds.origin.x + bounds.size.width;
  gtk_adjustment_clamp_page(hadjustment, h_start, h_end);
  g_debug("SGF scroll attempt: clamped horizontal page to [%.1f, %.1f]", h_start, h_end);
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
                                      const SgfNode *selected) {
  g_return_if_fail(SGF_IS_VIEW_SCROLLER(self));
  g_return_if_fail(GTK_IS_SCROLLED_WINDOW(root));
  g_return_if_fail(node_widgets != NULL);
  g_return_if_fail(selected != NULL);

  GtkWidget *widget = g_hash_table_lookup(node_widgets, (gpointer)selected);
  if (!widget) {
    g_debug("Unable to find SGF widget for selected node");
    return;
  }

  self->remembered_selected = selected;
  sgf_view_scroller_try_scroll_widget(self, root, widget);
}

void sgf_view_scroller_on_layout_changed(SgfViewScroller *self,
                                         GtkScrolledWindow *root,
                                         GHashTable *node_widgets) {
  g_return_if_fail(SGF_IS_VIEW_SCROLLER(self));
  g_return_if_fail(GTK_IS_SCROLLED_WINDOW(root));
  g_return_if_fail(node_widgets != NULL);

  if (!self->remembered_selected) {
    return;
  }

  GtkWidget *widget = g_hash_table_lookup(node_widgets, (gpointer)self->remembered_selected);
  if (!widget) {
    g_debug("Unable to find SGF widget for remembered selected node");
    return;
  }

  sgf_view_scroller_try_scroll_widget(self, root, widget);
}
