#include "sgf_view.h"

#include "sgf_view_disc_factory.h"
#include "sgf_view_layout.h"
#include "sgf_view_link_renderer.h"
#include "sgf_view_scroller.h"
#include "sgf_view_selection_controller.h"
#include "widget_utils.h"

#include <gdk/gdkkeysyms.h>

struct _SgfView {
  GObject parent_instance;
  GtkWidget *root;
  GtkWidget *overlay;
  GtkWidget *lines_area;
  GtkWidget *tree_box;
  SgfTree *tree;
  GHashTable *node_widgets;
  SgfViewDiscFactory *disc_factory;
  SgfViewLayout *layout;
  SgfViewLinkRenderer *link_renderer;
  SgfViewSelectionController *selection;
  SgfViewScroller *scroller;
  GArray *column_widths;
  GArray *row_heights;
};

G_DEFINE_TYPE(SgfView, sgf_view, G_TYPE_OBJECT)

enum { SIGNAL_NODE_SELECTED, SIGNAL_LAST };

static guint sgf_view_signals[SIGNAL_LAST] = {0};

static const int sgf_view_disc_size = 32;
static const int sgf_view_disc_border = 1;
static const int sgf_view_disc_spacing = 8;
static const int sgf_view_disc_stride = sgf_view_disc_size + (sgf_view_disc_border * 2);

gboolean sgf_view_has_horizontal_position_inconsistency(double scroll_window_position,
                                                        double content_view_effective_position) {
  const double delta = content_view_effective_position - scroll_window_position;
  return (delta > 30.0) || (delta < -30.0);
}

static void sgf_view_rebuild(SgfView *self);

static void sgf_view_draw_tree(GtkDrawingArea * /*area*/,
                               cairo_t *cr,
                               int width,
                               int height,
                               gpointer user_data) {
  SgfView *self = SGF_VIEW(user_data);

  g_return_if_fail(SGF_IS_VIEW(self));
  g_return_if_fail(cr != NULL);

  sgf_view_link_renderer_draw(self->link_renderer,
                              self->lines_area,
                              self->node_widgets,
                              self->tree,
                              self->row_heights,
                              cr,
                              width,
                              height);
}

static void sgf_view_log_layout_sync_state(SgfView *self) {
  g_return_if_fail(SGF_IS_VIEW(self));

  GtkWidget *root_widget = self->root;
  if (!root_widget) {
    g_debug("SGF view layout sync: missing root widget");
    return;
  }

  GtkWidget *top_level = GTK_WIDGET(gtk_widget_get_root(root_widget));
  int window_width = -1;
  int window_height = -1;
  graphene_rect_t root_bounds;
  gboolean root_bounds_valid = FALSE;
  if (top_level) {
    window_width = gtk_widget_get_width(top_level);
    window_height = gtk_widget_get_height(top_level);
    root_bounds_valid = gtk_widget_compute_bounds(root_widget, top_level, &root_bounds);
  }

  int root_width = gtk_widget_get_width(root_widget);
  int root_height = gtk_widget_get_height(root_widget);
  g_debug("SGF view layout sync: window=%dx%d scrolled=%dx%d",
          window_width,
          window_height,
          root_width,
          root_height);

  if (root_bounds_valid) {
    g_debug("SGF view layout sync: scrolled bounds in window=%.1f,%.1f %.1fx%.1f",
            root_bounds.origin.x,
            root_bounds.origin.y,
            root_bounds.size.width,
            root_bounds.size.height);
  } else {
    g_debug("SGF view layout sync: unable to compute scrolled bounds in window");
  }

  GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(root_widget));
  GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(root_widget));
  GtkWidget *scrolled_child = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(root_widget));
  GtkWidget *viewport = GTK_IS_VIEWPORT(scrolled_child) ? scrolled_child : NULL;

  if (hadjustment && vadjustment) {
    const double hadjustment_value = gtk_adjustment_get_value(hadjustment);
    const double hadjustment_page = gtk_adjustment_get_page_size(hadjustment);
    const double hadjustment_upper = gtk_adjustment_get_upper(hadjustment);
    const double vadjustment_value = gtk_adjustment_get_value(vadjustment);
    const double vadjustment_page = gtk_adjustment_get_page_size(vadjustment);
    const double vadjustment_upper = gtk_adjustment_get_upper(vadjustment);

    g_debug("SGF view layout sync: hadj value=%.1f page=%.1f upper=%.1f",
            hadjustment_value,
            hadjustment_page,
            hadjustment_upper);
    g_debug("SGF view layout sync: vadj value=%.1f page=%.1f upper=%.1f",
            vadjustment_value,
            vadjustment_page,
            vadjustment_upper);

  } else {
    g_debug("SGF view layout sync: missing adjustments");
  }

  if (self->overlay) {
    int overlay_width = gtk_widget_get_width(self->overlay);
    int overlay_height = gtk_widget_get_height(self->overlay);
    g_debug("SGF view layout sync: overlay size=%dx%d", overlay_width, overlay_height);
  }

  if (!self->selection) {
    g_debug("SGF view layout sync: missing selection controller");
    return;
  }

  const SgfNode *selected = sgf_view_selection_controller_get_selected(self->selection);
  if (!selected) {
    g_debug("SGF view layout sync: no selected node");
    return;
  }

  if (!self->node_widgets) {
    g_debug("SGF view layout sync: missing node widgets");
    return;
  }

  GtkWidget *node_widget = g_hash_table_lookup(self->node_widgets, (gpointer)selected);
  if (!node_widget) {
    g_debug("SGF view layout sync: selected node has no widget");
    return;
  }

  GtkWidget *parent = gtk_widget_get_parent(node_widget);
  if (!parent || !GTK_IS_GRID(parent)) {
    g_debug("SGF view layout sync: selected node is not attached to a grid");
    return;
  }

  int column = -1;
  int row = -1;
  gtk_grid_query_child(GTK_GRID(parent), node_widget, &column, &row, NULL, NULL);
  if (column < 0 || row < 0) {
    g_debug("SGF view layout sync: unable to query selected node grid position");
    return;
  }

  int width = gtk_widget_get_width(node_widget);
  int height = gtk_widget_get_height(node_widget);
  if (width <= 0) {
    int min_width = 0;
    int natural_width = 0;
    gtk_widget_measure(node_widget,
                       GTK_ORIENTATION_HORIZONTAL,
                       -1,
                       &min_width,
                       &natural_width,
                       NULL,
                       NULL);
    width = MAX(min_width, natural_width);
  }
  if (height <= 0) {
    int min_height = 0;
    int natural_height = 0;
    gtk_widget_measure(node_widget,
                       GTK_ORIENTATION_VERTICAL,
                       -1,
                       &min_height,
                       &natural_height,
                       NULL,
                       NULL);
    height = MAX(min_height, natural_height);
  }

  if (!self->column_widths || !self->row_heights) {
    g_debug("SGF view layout sync: missing layout extents for geometry");
    return;
  }

  if (self->column_widths->len <= (guint)column ||
      self->row_heights->len <= (guint)row) {
    g_debug("SGF view layout sync: selected node is outside layout extents");
    return;
  }

  int column_spacing = gtk_grid_get_column_spacing(GTK_GRID(parent));
  int row_spacing = gtk_grid_get_row_spacing(GTK_GRID(parent));
  int margin_start = gtk_widget_get_margin_start(parent);
  int margin_top = gtk_widget_get_margin_top(parent);

  double x = margin_start + column * column_spacing;
  for (int i = 0; i < column; ++i) {
    x += g_array_index(self->column_widths, int, i);
  }

  double y = margin_top + row * row_spacing;
  for (int i = 0; i < row; ++i) {
    y += g_array_index(self->row_heights, int, i);
  }

  g_debug("SGF view layout sync: selected grid=%d,%d geometry=%.1f,%.1f %dx%d",
          column,
          row,
          x,
          y,
          width,
          height);

  if (viewport && hadjustment && vadjustment) {
    const double hadjustment_value = gtk_adjustment_get_value(hadjustment);

    graphene_rect_t overlay_bounds_in_viewport;
    gboolean overlay_bounds_valid = FALSE;
    if (self->overlay) {
      overlay_bounds_valid = gtk_widget_compute_bounds(self->overlay,
                                                       viewport,
                                                       &overlay_bounds_in_viewport);
    }

    const double scroll_window_position = -hadjustment_value;
    const double content_view_effective_position =
      overlay_bounds_valid ? overlay_bounds_in_viewport.origin.x : 0.0;
    const gboolean has_content_inconsistency =
      overlay_bounds_valid && sgf_view_has_horizontal_position_inconsistency(scroll_window_position,
                                                                             content_view_effective_position);

    if (has_content_inconsistency) {
      g_debug("GTK SCROLLEDWINDOW BIG INCONSISTENCY: scroll-window-pos=%.1f content-view-effective-pos=%.1f",
              scroll_window_position,
              content_view_effective_position);
    }
  }
}

static void sgf_view_queue_scroll_to_selected(SgfView *self) {
  g_return_if_fail(SGF_IS_VIEW(self));

  const SgfNode *selected = sgf_view_selection_controller_get_selected(self->selection);
  if (!selected) {
    g_debug("SGF view has no selection to scroll");
    return;
  }
  sgf_view_scroller_request_scroll(self->scroller,
                                   GTK_SCROLLED_WINDOW(self->root),
                                   self->node_widgets,
                                   self->column_widths,
                                   self->row_heights,
                                   selected);
}

static void sgf_view_sync_selection_from_model(SgfView *self) {
  g_return_if_fail(SGF_IS_VIEW(self));

  if (!self->tree) {
    sgf_view_selection_controller_set_selected_raw(self->selection, NULL);
    return;
  }

  const SgfNode *selected = sgf_tree_get_current(self->tree);
  if (!selected) {
    sgf_view_selection_controller_set_selected_raw(self->selection, NULL);
    return;
  }

  if (!sgf_view_selection_controller_set_selected(self->selection, selected, self->node_widgets)) {
    g_debug("SGF view selection not ready to sync from model");
    return;
  }

  sgf_view_queue_scroll_to_selected(self);
}

static void sgf_view_on_layout_updated(SgfViewLayout *layout, gpointer user_data) {
  SgfView *self = SGF_VIEW(user_data);

  g_return_if_fail(SGF_IS_VIEW(self));
  g_return_if_fail(SGF_IS_VIEW_LAYOUT(layout));

  sgf_view_sync_selection_from_model(self);
  sgf_view_log_layout_sync_state(self);
  sgf_view_scroller_on_layout_changed(self->scroller,
                                      GTK_SCROLLED_WINDOW(self->root),
                                      self->node_widgets,
                                      self->column_widths,
                                      self->row_heights);
}

static void G_GNUC_UNUSED sgf_view_on_post_map(GtkWidget * /*widget*/, gpointer user_data) {
  SgfView *self = SGF_VIEW(user_data);

  g_return_if_fail(SGF_IS_VIEW(self));

  sgf_view_force_layout_sync(self);
}

static void G_GNUC_UNUSED sgf_view_on_post_notify(GObject *object,
                                                  GParamSpec *pspec,
                                                  gpointer user_data) {
  SgfView *self = SGF_VIEW(user_data);

  g_return_if_fail(SGF_IS_VIEW(self));
  g_return_if_fail(G_IS_OBJECT(object));
  g_return_if_fail(pspec != NULL);

  g_debug("SGF view post-notify: source=%s property=%s", G_OBJECT_TYPE_NAME(object), pspec->name);
  sgf_view_force_layout_sync(self);
}

static void sgf_view_select_node(SgfView *self, const SgfNode *node, gboolean emit_signal) {
  g_return_if_fail(SGF_IS_VIEW(self));

  if (!sgf_view_selection_controller_set_selected(self->selection, node, self->node_widgets)) {
    g_debug("SGF view selection not ready to update selection");
    return;
  }
  sgf_view_queue_scroll_to_selected(self);

  if (emit_signal && node) {
    g_signal_emit(self, sgf_view_signals[SIGNAL_NODE_SELECTED], 0, node);
  }
}

static void sgf_view_on_disc_node_clicked(SgfViewDiscFactory *factory,
                                          const SgfNode *node,
                                          gpointer user_data) {
  SgfView *self = SGF_VIEW(user_data);

  g_return_if_fail(SGF_IS_VIEW(self));
  g_return_if_fail(SGF_IS_VIEW_DISC_FACTORY(factory));

  sgf_view_set_selected(self, node);
  g_signal_emit(self, sgf_view_signals[SIGNAL_NODE_SELECTED], 0, node);
}

static gboolean sgf_view_on_key_pressed(GtkEventControllerKey * /*controller*/,
                                        guint keyval,
                                        guint /*keycode*/,
                                        GdkModifierType /*state*/,
                                        gpointer user_data) {
  SgfView *self = SGF_VIEW(user_data);

  g_return_val_if_fail(SGF_IS_VIEW(self), GDK_EVENT_PROPAGATE);

  SgfViewNavigation navigation;
  if (keyval == GDK_KEY_Left) {
    navigation = SGF_VIEW_NAVIGATE_PARENT;
  } else if (keyval == GDK_KEY_Right) {
    navigation = SGF_VIEW_NAVIGATE_CHILD;
  } else if (keyval == GDK_KEY_Up) {
    navigation = SGF_VIEW_NAVIGATE_PREVIOUS_SIBLING;
  } else if (keyval == GDK_KEY_Down) {
    navigation = SGF_VIEW_NAVIGATE_NEXT_SIBLING;
  } else {
    return GDK_EVENT_PROPAGATE;
  }

  const SgfNode *target = sgf_view_selection_controller_next(self->selection, navigation);
  if (!target) {
    return GDK_EVENT_STOP;
  }

  sgf_view_select_node(self, target, TRUE);
  return GDK_EVENT_STOP;
}

static void sgf_view_rebuild(SgfView *self) {
  g_return_if_fail(SGF_IS_VIEW(self));

  g_clear_pointer(&self->node_widgets, g_hash_table_unref);
  self->node_widgets = g_hash_table_new(g_direct_hash, g_direct_equal);
  g_clear_pointer(&self->column_widths, g_array_unref);
  g_clear_pointer(&self->row_heights, g_array_unref);
  self->column_widths = g_array_new(FALSE, TRUE, sizeof(int));
  self->row_heights = g_array_new(FALSE, TRUE, sizeof(int));

  const SgfNode *selected = self->tree ? sgf_tree_get_current(self->tree) : NULL;
  sgf_view_layout_build(self->layout,
                        GTK_GRID(self->tree_box),
                        self->tree,
                        self->node_widgets,
                        self->disc_factory,
                        selected,
                        sgf_view_disc_stride,
                        self->column_widths,
                        self->row_heights,
                        NULL,
                        NULL);
  gtk_widget_queue_draw(self->lines_area);
  sgf_view_queue_scroll_to_selected(self);
}

static void sgf_view_dispose(GObject *object) {
  SgfView *self = SGF_VIEW(object);

  gboolean root_removed = TRUE;
  if (self->root) {
    root_removed = gcheckers_widget_remove_from_parent(self->root);
    if (!root_removed && gtk_widget_get_parent(self->root)) {
      g_debug("Failed to remove SGF view root from parent during dispose\n");
    }
  }

  g_clear_object(&self->tree);
  if (root_removed) {
    g_clear_object(&self->root);
  } else {
    self->root = NULL;
  }
  g_clear_pointer(&self->node_widgets, g_hash_table_unref);
  g_clear_pointer(&self->column_widths, g_array_unref);
  g_clear_pointer(&self->row_heights, g_array_unref);
  g_clear_object(&self->disc_factory);
  g_clear_object(&self->layout);
  g_clear_object(&self->link_renderer);
  g_clear_object(&self->selection);
  g_clear_object(&self->scroller);

  G_OBJECT_CLASS(sgf_view_parent_class)->dispose(object);
}

static void sgf_view_class_init(SgfViewClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = sgf_view_dispose;

  sgf_view_signals[SIGNAL_NODE_SELECTED] = g_signal_new("node-selected",
                                                       G_TYPE_FROM_CLASS(klass),
                                                       G_SIGNAL_RUN_LAST,
                                                       0,
                                                       NULL,
                                                       NULL,
                                                       NULL,
                                                       G_TYPE_NONE,
                                                       1,
                                                       G_TYPE_POINTER);
}

static void sgf_view_init(SgfView *self) {
  self->root = gtk_scrolled_window_new();
  g_object_ref_sink(self->root);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(self->root),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
  gtk_widget_set_hexpand(self->root, TRUE);
  gtk_widget_set_vexpand(self->root, TRUE);
  gtk_widget_set_focusable(self->root, TRUE);

  self->overlay = gtk_overlay_new();
  gtk_widget_set_focusable(self->overlay, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(self->root), self->overlay);

  self->lines_area = gtk_drawing_area_new();
  gtk_widget_set_hexpand(self->lines_area, TRUE);
  gtk_widget_set_vexpand(self->lines_area, TRUE);
  gtk_widget_set_can_target(self->lines_area, FALSE);
  gtk_overlay_set_child(GTK_OVERLAY(self->overlay), self->lines_area);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self->lines_area), sgf_view_draw_tree, self, NULL);

  self->tree_box = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(self->tree_box), sgf_view_disc_spacing);
  gtk_grid_set_column_spacing(GTK_GRID(self->tree_box), sgf_view_disc_spacing);
  gtk_widget_set_margin_top(self->tree_box, 8);
  gtk_widget_set_margin_bottom(self->tree_box, 8);
  gtk_widget_set_margin_start(self->tree_box, 8);
  gtk_widget_set_margin_end(self->tree_box, 8);
  gtk_widget_set_hexpand(self->tree_box, FALSE);
  gtk_widget_set_vexpand(self->tree_box, FALSE);
  gtk_widget_set_focusable(self->tree_box, TRUE);
  gtk_widget_set_halign(self->tree_box, GTK_ALIGN_START);
  gtk_widget_set_valign(self->tree_box, GTK_ALIGN_START);
  gtk_overlay_add_overlay(GTK_OVERLAY(self->overlay), self->tree_box);
  gtk_overlay_set_measure_overlay(GTK_OVERLAY(self->overlay), self->tree_box, TRUE);

  self->disc_factory = sgf_view_disc_factory_new();
  self->layout = sgf_view_layout_new();
  self->link_renderer = sgf_view_link_renderer_new();
  self->selection = sgf_view_selection_controller_new();
  self->scroller = sgf_view_scroller_new();

  g_signal_connect(self->disc_factory,
                   "node-clicked",
                   G_CALLBACK(sgf_view_on_disc_node_clicked),
                   self);
  g_signal_connect(self->layout, "layout-updated", G_CALLBACK(sgf_view_on_layout_updated), self);

  // Extra signals, hoping to catch any changes that might affect layout or selection sync after the initial map and
  // layout-updated signals.
  g_signal_connect(gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(self->root)),
                   "notify::upper",
                   G_CALLBACK(sgf_view_on_post_notify),
                   self);
  g_signal_connect(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(self->root)),
                   "notify::upper",
                   G_CALLBACK(sgf_view_on_post_notify),
                   self);
  g_signal_connect(self->root, "notify::width-request", G_CALLBACK(sgf_view_on_post_notify), self);
  g_signal_connect(self->root, "notify::height-request", G_CALLBACK(sgf_view_on_post_notify), self);

  GtkEventController *key_controller = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(key_controller, GTK_PHASE_CAPTURE);
  g_signal_connect(key_controller, "key-pressed", G_CALLBACK(sgf_view_on_key_pressed), self);
  gtk_widget_add_controller(self->root, key_controller);
}

SgfView *sgf_view_new(void) {
  return g_object_new(SGF_TYPE_VIEW, NULL);
}

GtkWidget *sgf_view_get_widget(SgfView *self) {
  g_return_val_if_fail(SGF_IS_VIEW(self), NULL);

  return self->root;
}

void sgf_view_set_tree(SgfView *self, SgfTree *tree) {
  g_return_if_fail(SGF_IS_VIEW(self));

  if (self->tree == tree) {
    sgf_view_rebuild(self);
    return;
  }

  g_clear_object(&self->tree);
  self->tree = tree ? g_object_ref(tree) : NULL;

  sgf_view_selection_controller_set_selected_raw(self->selection,
                                                 tree ? sgf_tree_get_current(tree) : NULL);

  sgf_view_rebuild(self);
}

void sgf_view_set_selected(SgfView *self, const SgfNode *node) {
  g_return_if_fail(SGF_IS_VIEW(self));

  sgf_view_select_node(self, node, FALSE);
}

const SgfNode *sgf_view_get_selected(SgfView *self) {
  g_return_val_if_fail(SGF_IS_VIEW(self), NULL);

  return sgf_view_selection_controller_get_selected(self->selection);
}

gboolean sgf_view_navigate(SgfView *self, SgfViewNavigation navigation) {
  g_return_val_if_fail(SGF_IS_VIEW(self), FALSE);

  const SgfNode *target = sgf_view_selection_controller_next(self->selection, navigation);
  if (!target) {
    return FALSE;
  }

  sgf_view_select_node(self, target, TRUE);
  return TRUE;
}

void sgf_view_refresh(SgfView *self) {
  g_return_if_fail(SGF_IS_VIEW(self));

  sgf_view_rebuild(self);
}

void sgf_view_force_layout_sync(SgfView *self) {
  g_return_if_fail(SGF_IS_VIEW(self));

  if (!self->root || !self->scroller) {
    g_debug("SGF view missing core widgets for layout sync\n");
    return;
  }

  if (!self->node_widgets || !self->column_widths || !self->row_heights) {
    g_debug("SGF view missing layout data for layout sync\n");
    return;
  }

  sgf_view_sync_selection_from_model(self);
  sgf_view_log_layout_sync_state(self);
  sgf_view_scroller_on_layout_changed(self->scroller,
                                      GTK_SCROLLED_WINDOW(self->root),
                                      self->node_widgets,
                                      self->column_widths,
                                      self->row_heights);
}
