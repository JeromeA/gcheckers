#include "sgf_view_scroller.h"

/*
 * SGF scroller behavior:
 * - A scroll request remembers selected-node context and tries to scroll immediately.
 * - Any failure to resolve selected-node geometry schedules one idle retry path.
 * - Callers only issue scroll requests; retries are fully internal.
 */

struct _SgfViewScroller {
  GObject parent_instance;
  GtkScrolledWindow *root;
  GHashTable *node_widgets;
  const SgfNode *selected;
  gboolean retry_scheduled;
};

G_DEFINE_TYPE(SgfViewScroller, sgf_view_scroller, G_TYPE_OBJECT)

static void sgf_view_scroller_clear_context(SgfViewScroller *self) {
  g_return_if_fail(SGF_IS_VIEW_SCROLLER(self));

  g_clear_object(&self->root);
  g_clear_pointer(&self->node_widgets, g_hash_table_unref);
  self->selected = NULL;
}

static void sgf_view_scroller_retry_callback(gpointer user_data) {
  SgfViewScroller *self = SGF_VIEW_SCROLLER(user_data);

  g_return_if_fail(SGF_IS_VIEW_SCROLLER(self));

  self->retry_scheduled = FALSE;
  if (!self->root || !self->node_widgets || !self->selected) {
    g_debug("SGF scroll retry skipped: context not ready");
    g_object_unref(self);
    return;
  }

  sgf_view_scroller_scroll(self, self->root, self->node_widgets, self->selected);
  g_object_unref(self);
}

static void sgf_view_scroller_schedule_retry(SgfViewScroller *self) {
  g_return_if_fail(SGF_IS_VIEW_SCROLLER(self));

  if (self->retry_scheduled) {
    return;
  }

  self->retry_scheduled = TRUE;
  g_idle_add_once(sgf_view_scroller_retry_callback, g_object_ref(self));
  g_debug("SGF scroll attempt: scheduled idle retry");
}

static void sgf_view_scroller_dispose(GObject *object) {
  SgfViewScroller *self = SGF_VIEW_SCROLLER(object);

  sgf_view_scroller_clear_context(self);

  G_OBJECT_CLASS(sgf_view_scroller_parent_class)->dispose(object);
}

void sgf_view_scroller_scroll(SgfViewScroller *self,
                              GtkScrolledWindow *root,
                              GHashTable *node_widgets,
                              const SgfNode *selected) {
  g_return_if_fail(SGF_IS_VIEW_SCROLLER(self));
  g_return_if_fail(GTK_IS_SCROLLED_WINDOW(root));
  g_return_if_fail(node_widgets != NULL);
  g_return_if_fail(selected != NULL);

  GtkScrolledWindow *root_ref = g_object_ref(root);
  GHashTable *node_widgets_ref = g_hash_table_ref(node_widgets);

  sgf_view_scroller_clear_context(self);
  self->root = root_ref;
  self->node_widgets = node_widgets_ref;
  self->selected = selected;

  GtkWidget *widget = g_hash_table_lookup(node_widgets, (gpointer)selected);
  if (!widget) {
    g_debug("SGF scroll attempt: selected widget missing");
    sgf_view_scroller_schedule_retry(self);
    return;
  }
  if (!GTK_IS_WIDGET(widget)) {
    g_debug("SGF scroll attempt: selected mapping is not a widget");
    sgf_view_scroller_schedule_retry(self);
    return;
  }

  GtkWidget *content = gtk_widget_get_ancestor(widget, GTK_TYPE_OVERLAY);
  if (!content) {
    g_debug("SGF scroll attempt: selected widget has no overlay ancestor");
    sgf_view_scroller_schedule_retry(self);
    return;
  }

  graphene_rect_t bounds;
  if (!gtk_widget_compute_bounds(widget, content, &bounds)) {
    g_debug("SGF scroll attempt: selected bounds unavailable");
    sgf_view_scroller_schedule_retry(self);
    return;
  }
  g_debug("SGF scroll attempt: selected bounds [x=%.1f y=%.1f w=%.1f h=%.1f]",
          bounds.origin.x,
          bounds.origin.y,
          bounds.size.width,
          bounds.size.height);

  if (bounds.origin.x < 0.0) {
    g_debug("SGF scroll attempt: selected bounds x %.1f is negative", bounds.origin.x);
    sgf_view_scroller_schedule_retry(self);
    return;
  }

  GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(root);
  if (!hadjustment) {
    g_debug("SGF scroll attempt: horizontal adjustment unavailable");
    sgf_view_scroller_schedule_retry(self);
    return;
  }

  const double h_start = bounds.origin.x;
  const double h_end = bounds.origin.x + bounds.size.width;
  gtk_adjustment_clamp_page(hadjustment, h_start, h_end);
  g_debug("SGF scroll attempt: clamped horizontal page to [%.1f, %.1f]", h_start, h_end);
}

static void sgf_view_scroller_class_init(SgfViewScrollerClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = sgf_view_scroller_dispose;
}

static void sgf_view_scroller_init(SgfViewScroller *self) {
  self->root = NULL;
  self->node_widgets = NULL;
  self->selected = NULL;
  self->retry_scheduled = FALSE;
}

SgfViewScroller *sgf_view_scroller_new(void) {
  return g_object_new(SGF_TYPE_VIEW_SCROLLER, NULL);
}
