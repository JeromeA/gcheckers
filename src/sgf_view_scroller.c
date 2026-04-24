#include "sgf_view_scroller.h"

/*
 * SGF scroller behavior:
 * - A scroll request remembers selected-node context and tries to scroll immediately.
 * - Transient geometry/adjustment failures schedule one idle retry path.
 * - Callers only issue scroll requests; retries are fully internal.
 */

struct _SgfViewScroller {
  GObject parent_instance;
  GtkScrolledWindow *root;
  GHashTable *node_widgets;
  const SgfNode *selected;
  gboolean retry_scheduled;
  guint retry_generation;
  guint retry_count;
};

G_DEFINE_TYPE(SgfViewScroller, sgf_view_scroller, G_TYPE_OBJECT)

static void sgf_view_scroller_clear_context(SgfViewScroller *self) {
  g_return_if_fail(SGF_IS_VIEW_SCROLLER(self));

  g_clear_object(&self->root);
  g_clear_pointer(&self->node_widgets, g_hash_table_unref);
  self->selected = NULL;
  self->retry_generation = 0;
  self->retry_count = 0;
}

static void sgf_view_scroller_retry_callback(gpointer user_data) {
  SgfViewScroller *self = SGF_VIEW_SCROLLER(user_data);

  g_return_if_fail(SGF_IS_VIEW_SCROLLER(self));

  self->retry_scheduled = FALSE;
  if (!self->root || !self->node_widgets || !self->selected) {
    g_object_unref(self);
    return;
  }

  self->retry_count++;
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

  gboolean same_context =
      self->root == root && self->node_widgets == node_widgets && self->selected == selected;
  if (!same_context) {
    GtkScrolledWindow *root_ref = g_object_ref(root);
    GHashTable *node_widgets_ref = g_hash_table_ref(node_widgets);

    sgf_view_scroller_clear_context(self);
    self->root = root_ref;
    self->node_widgets = node_widgets_ref;
    self->selected = selected;
    self->retry_generation++;
    self->retry_count = 0;
  }

  GtkWidget *widget = g_hash_table_lookup(node_widgets, (gpointer)selected);
  if (!widget) {
    return;
  }
  if (!GTK_IS_WIDGET(widget)) {
    return;
  }

  gboolean has_parent_bounds = FALSE;
  graphene_rect_t parent_bounds;
  GtkWidget *parent = gtk_widget_get_parent(widget);
  if (parent) {
    if (gtk_widget_compute_bounds(widget, parent, &parent_bounds)) {
      has_parent_bounds = TRUE;
    }
  }

  GtkWidget *content = gtk_widget_get_ancestor(widget, GTK_TYPE_OVERLAY);
  if (!content) {
    sgf_view_scroller_schedule_retry(self);
    return;
  }

  graphene_rect_t bounds;
  gboolean has_bounds = gtk_widget_compute_bounds(widget, content, &bounds);
  if (!has_bounds) {
    sgf_view_scroller_schedule_retry(self);
    return;
  }

  if (has_parent_bounds && parent_bounds.origin.x < 0.0) {
    sgf_view_scroller_schedule_retry(self);
    return;
  }

  GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(root);
  if (!hadjustment) {
    sgf_view_scroller_schedule_retry(self);
    return;
  }

  const double h_start = bounds.origin.x;
  const double h_end = bounds.origin.x + bounds.size.width;
  gtk_adjustment_clamp_page(hadjustment, h_start, h_end);
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
  self->retry_generation = 0;
  self->retry_count = 0;
}

SgfViewScroller *sgf_view_scroller_new(void) {
  return g_object_new(SGF_TYPE_VIEW_SCROLLER, NULL);
}
