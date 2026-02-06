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
  int content_width;
  int content_height;
};

G_DEFINE_TYPE(SgfView, sgf_view, G_TYPE_OBJECT)

enum { SIGNAL_NODE_SELECTED, SIGNAL_LAST };

static guint sgf_view_signals[SIGNAL_LAST] = {0};

static const int sgf_view_disc_size = 32;
static const int sgf_view_disc_border = 1;
static const int sgf_view_disc_spacing = 8;
static const int sgf_view_disc_stride = sgf_view_disc_size + (sgf_view_disc_border * 2);

static void sgf_view_rebuild(SgfView *self);

static int sgf_view_sum_extents(GArray *extents, guint count, int fallback) {
  g_return_val_if_fail(fallback > 0, 0);

  if (!extents || extents->len < count) {
    if (extents && extents->len < count) {
      g_debug("SGF extents array shorter than expected\n");
    }
    return (int)count * fallback;
  }

  int total = 0;
  for (guint i = 0; i < count; ++i) {
    total += g_array_index(extents, int, i);
  }
  return total;
}

static void sgf_view_update_content_size(SgfView *self,
                                         gboolean has_nodes,
                                         guint max_row,
                                         guint max_column) {
  g_return_if_fail(SGF_IS_VIEW(self));

  self->content_width = 0;
  self->content_height = 0;

  if (!self->root || !self->tree_box || !self->overlay || !self->lines_area) {
    return;
  }

  GtkWidget *scrolled_child =
    gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(self->root));
  if (!scrolled_child) {
    g_debug("SGF view scrolled window has no child for sizing");
    return;
  }
  GtkWidget *viewport = GTK_IS_VIEWPORT(scrolled_child) ? scrolled_child : NULL;

  if (!has_nodes) {
    gtk_widget_set_size_request(self->overlay, 0, 0);
    gtk_widget_set_size_request(self->lines_area, 0, 0);
    if (viewport) {
      gtk_widget_set_size_request(viewport, 0, 0);
      gtk_widget_queue_resize(viewport);
    }
    gtk_widget_queue_resize(self->overlay);
    gtk_widget_queue_resize(self->lines_area);
    gtk_widget_queue_resize(self->root);
    return;
  }

  guint rows = max_row + 1;
  guint columns = max_column + 1;

  int margin_start = gtk_widget_get_margin_start(self->tree_box);
  int margin_end = gtk_widget_get_margin_end(self->tree_box);
  int margin_top = gtk_widget_get_margin_top(self->tree_box);
  int margin_bottom = gtk_widget_get_margin_bottom(self->tree_box);

  int columns_width = sgf_view_sum_extents(self->column_widths, columns, sgf_view_disc_stride);
  int width = margin_start + margin_end + columns_width;
  if (columns > 1) {
    width += (int)(columns - 1) * sgf_view_disc_spacing;
  }

  int rows_height = sgf_view_sum_extents(self->row_heights, rows, sgf_view_disc_stride);
  int height = margin_top + margin_bottom + rows_height;
  if (rows > 1) {
    height += (int)(rows - 1) * sgf_view_disc_spacing;
  }

  int measured_width = 0;
  int measured_height = 0;
  gtk_widget_measure(self->tree_box,
                     GTK_ORIENTATION_HORIZONTAL,
                     -1,
                     &measured_width,
                     NULL,
                     NULL,
                     NULL);
  gtk_widget_measure(self->tree_box,
                     GTK_ORIENTATION_VERTICAL,
                     -1,
                     &measured_height,
                     NULL,
                     NULL,
                     NULL);

  width = MAX(width, measured_width);
  height = MAX(height, measured_height);

  self->content_width = width;
  self->content_height = height;

  g_debug("SGF view content sizing: expected=%dx%d measured=%dx%d rows=%u columns=%u",
          width,
          height,
          measured_width,
          measured_height,
          rows,
          columns);

  gtk_widget_set_size_request(self->overlay, width, height);
  gtk_widget_set_size_request(self->lines_area, width, height);
  if (viewport) {
    gtk_widget_set_size_request(viewport, width, height);
    gtk_widget_queue_resize(viewport);
    g_debug("SGF view viewport size request: %dx%d", width, height);
  }
  g_debug("SGF view overlay size request: %dx%d", width, height);
  gtk_widget_queue_resize(self->overlay);
  gtk_widget_queue_resize(self->lines_area);
  gtk_widget_queue_resize(self->root);
}

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
  if (top_level) {
    window_width = gtk_widget_get_width(top_level);
    window_height = gtk_widget_get_height(top_level);
  }

  int root_width = gtk_widget_get_width(root_widget);
  int root_height = gtk_widget_get_height(root_widget);
  g_debug("SGF view layout sync: window=%dx%d scrolled=%dx%d",
          window_width,
          window_height,
          root_width,
          root_height);

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
}

static void sgf_view_queue_scroll_to_selected(SgfView *self) {
  g_return_if_fail(SGF_IS_VIEW(self));

  const SgfNode *selected = sgf_view_selection_controller_get_selected(self->selection);
  if (!selected) {
    g_debug("SGF view has no selection to scroll");
    return;
  }
  /* SGF scroller has only two triggers: selection requests and layout-updated retries. */
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
  sgf_view_scroller_on_layout_changed(self->scroller,
                                      GTK_SCROLLED_WINDOW(self->root),
                                      self->node_widgets,
                                      self->column_widths,
                                      self->row_heights);
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
  gboolean has_nodes = FALSE;
  if (self->tree) {
    const SgfNode *root = sgf_tree_get_root(self->tree);
    has_nodes = root != NULL;
  }

  guint max_row = 0;
  guint max_column = 0;
  sgf_view_layout_build(self->layout,
                        GTK_GRID(self->tree_box),
                        self->tree,
                        self->node_widgets,
                        self->disc_factory,
                        selected,
                        sgf_view_disc_stride,
                        self->column_widths,
                        self->row_heights,
                        &max_row,
                        &max_column);
  sgf_view_update_content_size(self, has_nodes, max_row, max_column);
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

  self->disc_factory = sgf_view_disc_factory_new();
  self->layout = sgf_view_layout_new();
  self->link_renderer = sgf_view_link_renderer_new();
  self->selection = sgf_view_selection_controller_new();
  self->scroller = sgf_view_scroller_new();
  self->content_width = 0;
  self->content_height = 0;

  g_signal_connect(self->disc_factory,
                   "node-clicked",
                   G_CALLBACK(sgf_view_on_disc_node_clicked),
                   self);
  g_signal_connect(self->layout, "layout-updated", G_CALLBACK(sgf_view_on_layout_updated), self);

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
