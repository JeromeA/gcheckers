#include <gtk/gtk.h>
#include <math.h>

#include "sgf_tree.h"
#include "sgf_view.h"
#include "sgf_view_disc_factory.h"
#include "sgf_view_link_renderer.h"
#include "sgf_view_scroller.h"

static gboolean sgf_view_quit_loop_cb(gpointer user_data) {
  GMainLoop *loop = user_data;

  g_return_val_if_fail(loop != NULL, G_SOURCE_REMOVE);

  g_main_loop_quit(loop);
  return G_SOURCE_REMOVE;
}

static void sgf_view_wait(guint timeout_ms) {
  GMainLoop *loop = g_main_loop_new(NULL, FALSE);
  g_timeout_add(timeout_ms, sgf_view_quit_loop_cb, loop);
  g_main_loop_run(loop);
  g_main_loop_unref(loop);
}

typedef struct {
  GtkAdjustment *hadjustment;
  GtkAdjustment *vadjustment;
  GMainLoop *loop;
  gint64 deadline_us;
  gboolean scrolled;
} SgfViewScrollWait;

static gboolean sgf_view_wait_for_scroll_cb(gpointer user_data) {
  SgfViewScrollWait *wait = user_data;

  g_return_val_if_fail(wait != NULL, G_SOURCE_REMOVE);

  double h_value = gtk_adjustment_get_value(wait->hadjustment);
  double v_value = gtk_adjustment_get_value(wait->vadjustment);
  if (h_value > 0.0 || v_value > 0.0) {
    wait->scrolled = TRUE;
    g_main_loop_quit(wait->loop);
    return G_SOURCE_REMOVE;
  }

  if (g_get_monotonic_time() >= wait->deadline_us) {
    g_main_loop_quit(wait->loop);
    return G_SOURCE_REMOVE;
  }

  return G_SOURCE_CONTINUE;
}

static gboolean sgf_view_wait_for_scroll(GtkAdjustment *hadjustment,
                                         GtkAdjustment *vadjustment,
                                         guint timeout_ms) {
  g_return_val_if_fail(hadjustment != NULL, FALSE);
  g_return_val_if_fail(vadjustment != NULL, FALSE);

  GMainLoop *loop = g_main_loop_new(NULL, FALSE);
  SgfViewScrollWait wait = {
    .hadjustment = hadjustment,
    .vadjustment = vadjustment,
    .loop = loop,
    .deadline_us = g_get_monotonic_time() + ((gint64)timeout_ms * 1000),
    .scrolled = FALSE,
  };

  g_timeout_add(20, sgf_view_wait_for_scroll_cb, &wait);
  g_main_loop_run(loop);
  g_main_loop_unref(loop);

  return wait.scrolled;
}

static int sgf_view_count_children(GtkWidget *parent) {
  int count = 0;
  GtkWidget *child = gtk_widget_get_first_child(parent);
  while (child) {
    count++;
    child = gtk_widget_get_next_sibling(child);
  }
  return count;
}

static GtkWidget *sgf_view_find_grid(GtkWidget *overlay) {
  GtkWidget *child = gtk_widget_get_first_child(overlay);
  while (child) {
    if (GTK_IS_GRID(child)) {
      return child;
    }
    child = gtk_widget_get_next_sibling(child);
  }
  return NULL;
}

static GtkWidget *sgf_view_find_disc_for_node(GtkWidget *grid, const SgfNode *node) {
  GtkWidget *child = gtk_widget_get_first_child(grid);
  while (child) {
    if (g_object_get_data(G_OBJECT(child), "sgf-node") == node) {
      return child;
    }
    child = gtk_widget_get_next_sibling(child);
  }
  return NULL;
}

static GtkWidget *sgf_view_get_overlay(GtkWidget *root) {
  g_return_val_if_fail(GTK_IS_SCROLLED_WINDOW(root), NULL);

  GtkWidget *child = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(root));
  if (!child) {
    g_debug("SGF view scrolled window had no child\n");
    return NULL;
  }

  if (GTK_IS_OVERLAY(child)) {
    return child;
  }

  if (GTK_IS_VIEWPORT(child)) {
    GtkWidget *viewport_child = gtk_viewport_get_child(GTK_VIEWPORT(child));
    if (GTK_IS_OVERLAY(viewport_child)) {
      return viewport_child;
    }
  }

  g_debug("Unexpected SGF view child type %s\n", G_OBJECT_TYPE_NAME(child));
  return NULL;
}

static void sgf_view_update_extent(GArray *extents, guint index, int value) {
  g_return_if_fail(extents != NULL);

  if (extents->len <= index) {
    g_array_set_size(extents, index + 1);
  }

  int current = g_array_index(extents, int, index);
  if (value > current) {
    g_array_index(extents, int, index) = value;
  }
}

static gboolean sgf_view_compute_widget_center(GtkWidget *lines_area, GtkWidget *widget, double *x, double *y) {
  g_return_val_if_fail(GTK_IS_WIDGET(lines_area), FALSE);
  g_return_val_if_fail(GTK_IS_WIDGET(widget), FALSE);
  g_return_val_if_fail(x != NULL, FALSE);
  g_return_val_if_fail(y != NULL, FALSE);

  int width = gtk_widget_get_width(widget);
  int height = gtk_widget_get_height(widget);
  if (width <= 0 || height <= 0) {
    g_debug("Unable to measure SGF widget center for link test\n");
    return FALSE;
  }

  graphene_point_t widget_point;
  graphene_point_t translated_point;
  graphene_point_init(&widget_point, width / 2.0, height / 2.0);

  if (!gtk_widget_compute_point(widget, lines_area, &widget_point, &translated_point)) {
    g_debug("Unable to compute SGF widget center for link test\n");
    return FALSE;
  }

  *x = translated_point.x;
  *y = translated_point.y;
  return TRUE;
}

static gboolean sgf_view_compute_row_center(GtkWidget *grid, GArray *row_heights, int row, double *out_y) {
  g_return_val_if_fail(GTK_IS_GRID(grid), FALSE);
  g_return_val_if_fail(row_heights != NULL, FALSE);
  g_return_val_if_fail(out_y != NULL, FALSE);

  if (row_heights->len <= (guint)row) {
    g_debug("Row heights too short for link test\n");
    return FALSE;
  }

  int row_height = g_array_index(row_heights, int, row);
  if (row_height <= 0) {
    g_debug("Row height too small for link test\n");
    return FALSE;
  }

  int row_spacing = gtk_grid_get_row_spacing(GTK_GRID(grid));
  int margin_top = gtk_widget_get_margin_top(grid);
  double origin_y = margin_top + row * row_spacing;
  for (int i = 0; i < row; ++i) {
    origin_y += g_array_index(row_heights, int, i);
  }

  *out_y = origin_y + row_height / 2.0;
  return TRUE;
}

static gboolean sgf_view_surface_has_ink(cairo_surface_t *surface, int x, int y, int radius) {
  g_return_val_if_fail(surface != NULL, FALSE);

  int width = cairo_image_surface_get_width(surface);
  int height = cairo_image_surface_get_height(surface);
  if (width <= 0 || height <= 0) {
    g_debug("Invalid surface size for link test\n");
    return FALSE;
  }

  int start_x = MAX(0, x - radius);
  int end_x = MIN(width - 1, x + radius);
  int start_y = MAX(0, y - radius);
  int end_y = MIN(height - 1, y + radius);

  cairo_surface_flush(surface);
  unsigned char *data = cairo_image_surface_get_data(surface);
  int stride = cairo_image_surface_get_stride(surface);
  for (int sample_y = start_y; sample_y <= end_y; ++sample_y) {
    for (int sample_x = start_x; sample_x <= end_x; ++sample_x) {
      guint32 pixel = *(guint32 *)(data + sample_y * stride + sample_x * 4);
      guint8 alpha = (pixel >> 24) & 0xff;
      if (alpha > 0) {
        return TRUE;
      }
    }
  }

  return FALSE;
}

static gboolean sgf_view_compute_disc_bounds(GtkWidget *grid,
                                             GtkWidget *disc,
                                             graphene_rect_t *bounds) {
  g_return_val_if_fail(GTK_IS_GRID(grid), FALSE);
  g_return_val_if_fail(GTK_IS_WIDGET(disc), FALSE);
  g_return_val_if_fail(bounds != NULL, FALSE);

  int column = -1;
  int row = -1;
  gtk_grid_query_child(GTK_GRID(grid), disc, &column, &row, NULL, NULL);
  if (column < 0 || row < 0) {
    g_debug("Unable to query SGF grid position for disc bounds\n");
    return FALSE;
  }

  GArray *column_widths = g_array_new(FALSE, TRUE, sizeof(int));
  GArray *row_heights = g_array_new(FALSE, TRUE, sizeof(int));
  if (!column_widths || !row_heights) {
    g_debug("Unable to allocate SGF extent arrays for visibility test\n");
    if (column_widths) {
      g_array_unref(column_widths);
    }
    if (row_heights) {
      g_array_unref(row_heights);
    }
    return FALSE;
  }

  GtkWidget *child = gtk_widget_get_first_child(grid);
  while (child) {
    int child_column = -1;
    int child_row = -1;
    gtk_grid_query_child(GTK_GRID(grid), child, &child_column, &child_row, NULL, NULL);
    if (child_column < 0 || child_row < 0) {
      g_debug("Unable to query SGF grid position for extent calculation\n");
      g_array_unref(column_widths);
      g_array_unref(row_heights);
      return FALSE;
    }

    int min_width = 0;
    int natural_width = 0;
    int min_height = 0;
    int natural_height = 0;
    gtk_widget_measure(child,
                       GTK_ORIENTATION_HORIZONTAL,
                       -1,
                       &min_width,
                       &natural_width,
                       NULL,
                       NULL);
    gtk_widget_measure(child,
                       GTK_ORIENTATION_VERTICAL,
                       -1,
                       &min_height,
                       &natural_height,
                       NULL,
                       NULL);

    int measured_width = MAX(natural_width, min_width);
    int measured_height = MAX(natural_height, min_height);
    int request_width = -1;
    int request_height = -1;
    gtk_widget_get_size_request(child, &request_width, &request_height);
    if (request_width > 0) {
      measured_width = MAX(measured_width, request_width);
    }
    if (request_height > 0) {
      measured_height = MAX(measured_height, request_height);
    }
    if (measured_width <= 0 || measured_height <= 0) {
      g_debug("Unable to determine SGF disc extent for visibility test\n");
      g_array_unref(column_widths);
      g_array_unref(row_heights);
      return FALSE;
    }

    sgf_view_update_extent(column_widths, (guint)child_column, measured_width);
    sgf_view_update_extent(row_heights, (guint)child_row, measured_height);
    child = gtk_widget_get_next_sibling(child);
  }

  if (column_widths->len <= (guint)column || row_heights->len <= (guint)row) {
    g_debug("SGF extent arrays shorter than expected for visibility test\n");
    g_array_unref(column_widths);
    g_array_unref(row_heights);
    return FALSE;
  }

  int column_spacing = gtk_grid_get_column_spacing(GTK_GRID(grid));
  int row_spacing = gtk_grid_get_row_spacing(GTK_GRID(grid));
  int margin_start = gtk_widget_get_margin_start(grid);
  int margin_top = gtk_widget_get_margin_top(grid);

  double origin_x = margin_start + column * column_spacing;
  for (int i = 0; i < column; ++i) {
    origin_x += g_array_index(column_widths, int, i);
  }

  double origin_y = margin_top + row * row_spacing;
  for (int i = 0; i < row; ++i) {
    origin_y += g_array_index(row_heights, int, i);
  }

  bounds->origin.x = origin_x;
  bounds->origin.y = origin_y;
  bounds->size.width = g_array_index(column_widths, int, column);
  bounds->size.height = g_array_index(row_heights, int, row);
  g_array_unref(column_widths);
  g_array_unref(row_heights);
  return TRUE;
}

static void sgf_view_assert_disc_visible(GtkAdjustment *hadjustment,
                                         GtkAdjustment *vadjustment,
                                         const graphene_rect_t *bounds) {
  g_return_if_fail(hadjustment != NULL);
  g_return_if_fail(vadjustment != NULL);
  g_return_if_fail(bounds != NULL);

  const double visible_start_x = gtk_adjustment_get_value(hadjustment);
  const double visible_end_x = visible_start_x + gtk_adjustment_get_page_size(hadjustment);
  const double visible_start_y = gtk_adjustment_get_value(vadjustment);
  const double visible_end_y = visible_start_y + gtk_adjustment_get_page_size(vadjustment);
  const double epsilon = 1.0;

  gboolean h_visible = visible_start_x <= bounds->origin.x + epsilon &&
                       visible_end_x + epsilon >= (bounds->origin.x + bounds->size.width);
  gboolean v_visible = visible_start_y <= bounds->origin.y + epsilon &&
                       visible_end_y + epsilon >= (bounds->origin.y + bounds->size.height);

  g_assert_true(h_visible);
  g_assert_true(v_visible);
}

static void test_sgf_view_horizontal_position_inconsistency_detection(void) {
  g_assert_false(sgf_view_has_horizontal_position_inconsistency(-20.0, -20.0));
  g_assert_false(sgf_view_has_horizontal_position_inconsistency(-20.0, -50.0));
  g_assert_false(sgf_view_has_horizontal_position_inconsistency(-20.0, 10.0));

  g_assert_true(sgf_view_has_horizontal_position_inconsistency(-20.0, -50.1));
  g_assert_true(sgf_view_has_horizontal_position_inconsistency(-20.0, 10.1));
}

static void test_sgf_view_connectors(void) {
  SgfTree *tree = sgf_tree_new();
  sgf_tree_append_move(tree, SGF_COLOR_BLACK, NULL);
  sgf_tree_append_move(tree, SGF_COLOR_WHITE, NULL);
  sgf_tree_append_move(tree, SGF_COLOR_BLACK, NULL);

  SgfView *view = sgf_view_new();
  sgf_view_set_tree(view, tree);

  GtkWidget *root = sgf_view_get_widget(view);
  GtkWidget *overlay = sgf_view_get_overlay(root);
  g_assert_nonnull(overlay);

  GtkWidget *lines_area = gtk_overlay_get_child(GTK_OVERLAY(overlay));
  g_assert_true(GTK_IS_DRAWING_AREA(lines_area));

  GtkWidget *tree_grid = sgf_view_find_grid(overlay);
  g_assert_nonnull(tree_grid);

  g_assert_cmpint(sgf_view_count_children(tree_grid), ==, 4);

  GtkWidget *child = gtk_widget_get_first_child(tree_grid);
  g_assert_true(GTK_IS_BUTTON(child));
  child = gtk_widget_get_next_sibling(child);
  g_assert_true(GTK_IS_BUTTON(child));
  child = gtk_widget_get_next_sibling(child);
  g_assert_true(GTK_IS_BUTTON(child));

  g_clear_object(&view);
  g_clear_object(&tree);
}

static void test_sgf_view_root_disc(void) {
  SgfTree *tree = sgf_tree_new();
  const SgfNode *move_1 = sgf_tree_append_move(tree, SGF_COLOR_BLACK, NULL);

  SgfView *view = sgf_view_new();
  sgf_view_set_tree(view, tree);

  GtkWidget *root = sgf_view_get_widget(view);
  GtkWidget *overlay = sgf_view_get_overlay(root);
  g_assert_nonnull(overlay);
  GtkWidget *tree_grid = sgf_view_find_grid(overlay);
  g_assert_nonnull(tree_grid);

  const SgfNode *root_node = sgf_tree_get_root(tree);
  g_assert_nonnull(root_node);
  GtkWidget *root_disc = sgf_view_find_disc_for_node(tree_grid, root_node);
  g_assert_nonnull(root_disc);
  g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(root_disc)), ==, "\u2022");

  int root_column = -1;
  int root_row = -1;
  gtk_grid_query_child(GTK_GRID(tree_grid), root_disc, &root_column, &root_row, NULL, NULL);
  g_assert_cmpint(root_column, ==, 0);
  g_assert_cmpint(root_row, ==, 0);

  GtkWidget *move_disc = sgf_view_find_disc_for_node(tree_grid, move_1);
  g_assert_nonnull(move_disc);
  int move_column = -1;
  int move_row = -1;
  gtk_grid_query_child(GTK_GRID(tree_grid), move_disc, &move_column, &move_row, NULL, NULL);
  g_assert_cmpint(move_column, ==, 1);
  g_assert_cmpint(move_row, ==, 0);

  g_clear_object(&view);
  g_clear_object(&tree);
}

static void test_sgf_view_tree_box_is_measured_overlay(void) {
  SgfView *view = sgf_view_new();
  GtkWidget *root = sgf_view_get_widget(view);
  GtkWidget *overlay = sgf_view_get_overlay(root);
  g_assert_nonnull(overlay);

  GtkWidget *tree_grid = sgf_view_find_grid(overlay);
  g_assert_nonnull(tree_grid);
  g_assert_true(gtk_overlay_get_measure_overlay(GTK_OVERLAY(overlay), tree_grid));

  g_clear_object(&view);
}

static void test_sgf_view_branch_columns(void) {
  SgfTree *tree = sgf_tree_new();
  g_autoptr(GBytes) move_1_payload = g_bytes_new_static("m1", sizeof("m1") - 1);
  const SgfNode *move_1 = sgf_tree_append_move(tree, SGF_COLOR_BLACK, move_1_payload);
  g_autoptr(GBytes) move_2_payload = g_bytes_new_static("m2a", sizeof("m2a") - 1);
  const SgfNode *move_2 = sgf_tree_append_move(tree, SGF_COLOR_WHITE, move_2_payload);
  g_autoptr(GBytes) move_3_payload = g_bytes_new_static("m3", sizeof("m3") - 1);
  sgf_tree_append_move(tree, SGF_COLOR_BLACK, move_3_payload);
  g_assert_true(sgf_tree_set_current(tree, move_1));
  g_autoptr(GBytes) branch_2_payload = g_bytes_new_static("m2b", sizeof("m2b") - 1);
  const SgfNode *branch_2 = sgf_tree_append_move(tree, SGF_COLOR_WHITE, branch_2_payload);

  SgfView *view = sgf_view_new();
  sgf_view_set_tree(view, tree);

  GtkWidget *root = sgf_view_get_widget(view);
  GtkWidget *overlay = sgf_view_get_overlay(root);
  g_assert_nonnull(overlay);
  GtkWidget *tree_grid = sgf_view_find_grid(overlay);
  g_assert_nonnull(tree_grid);

  GtkWidget *main_disc = sgf_view_find_disc_for_node(tree_grid, move_2);
  g_assert_nonnull(main_disc);

  GtkWidget *branch_disc = sgf_view_find_disc_for_node(tree_grid, branch_2);
  g_assert_nonnull(branch_disc);

  int main_column = -1;
  int main_row = -1;
  int main_width = 0;
  int main_height = 0;
  gtk_grid_query_child(GTK_GRID(tree_grid), main_disc, &main_column, &main_row, &main_width, &main_height);

  int branch_column = -1;
  int branch_row = -1;
  int branch_width = 0;
  int branch_height = 0;
  gtk_grid_query_child(GTK_GRID(tree_grid), branch_disc, &branch_column, &branch_row, &branch_width, &branch_height);

  g_assert_cmpint(main_column, ==, 2);
  g_assert_cmpint(branch_column, ==, 2);
  g_assert_cmpint(branch_row, >, main_row);

  g_clear_object(&view);
  g_clear_object(&tree);
}

static void test_sgf_view_link_angles(void) {
  const int disc_stride = 34;
  const int row_spacing = 8;
  const int window_width = 300;
  const int window_height = 220;

  struct {
    int parent_row;
    int child_row;
  } cases[] = {
    {0, 0},
    {0, 1},
    {0, 2},
  };

  for (guint i = 0; i < G_N_ELEMENTS(cases); ++i) {
    SgfTree *tree = sgf_tree_new();
    const SgfNode *move_1 = sgf_tree_append_move(tree, SGF_COLOR_BLACK, NULL);
    const SgfNode *move_2 = sgf_tree_append_move(tree, SGF_COLOR_WHITE, NULL);

    g_autoptr(GHashTable) node_widgets = g_hash_table_new(g_direct_hash, g_direct_equal);
    GtkWidget *overlay = gtk_overlay_new();
    GtkWidget *lines_area = gtk_drawing_area_new();
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), row_spacing);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);

    gtk_widget_set_size_request(lines_area, window_width, window_height);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), lines_area);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), grid);

    SgfViewDiscFactory *factory = sgf_view_disc_factory_new();
    GtkWidget *parent_disc =
      sgf_view_disc_factory_build(factory, move_1, NULL, node_widgets, disc_stride);
    GtkWidget *child_disc =
      sgf_view_disc_factory_build(factory, move_2, NULL, node_widgets, disc_stride);
    gtk_grid_attach(GTK_GRID(grid), parent_disc, 0, cases[i].parent_row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), child_disc, 1, cases[i].child_row, 1, 1);

    GtkWidget *window = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(window), window_width, window_height);
    gtk_window_set_child(GTK_WINDOW(window), overlay);
    gtk_window_present(GTK_WINDOW(window));
    sgf_view_wait(40);

    double parent_x = 0.0;
    double parent_y = 0.0;
    double child_x = 0.0;
    double child_y = 0.0;
    g_assert_true(sgf_view_compute_widget_center(lines_area, parent_disc, &parent_x, &parent_y));
    g_assert_true(sgf_view_compute_widget_center(lines_area, child_disc, &child_x, &child_y));

    int surface_width = gtk_widget_get_width(lines_area);
    int surface_height = gtk_widget_get_height(lines_area);
    g_assert_cmpint(surface_width, >, 0);
    g_assert_cmpint(surface_height, >, 0);

    g_autoptr(GArray) row_heights = g_array_new(FALSE, TRUE, sizeof(int));
    g_array_set_size(row_heights, (guint)cases[i].child_row + 1);
    for (guint row = 0; row < row_heights->len; ++row) {
      g_array_index(row_heights, int, row) = disc_stride;
    }

    SgfViewLinkRenderer *renderer = sgf_view_link_renderer_new();
    cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surface_width, surface_height);
    cairo_t *cr = cairo_create(surface);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    sgf_view_link_renderer_draw(renderer,
                                lines_area,
                                node_widgets,
                                tree,
                                row_heights,
                                cr,
                                surface_width,
                                surface_height);
    cairo_destroy(cr);

    int sample_radius = 1;
    if (cases[i].child_row == cases[i].parent_row) {
      int mid_x = (int)round((parent_x + child_x) / 2.0);
      int mid_y = (int)round(parent_y);
      g_assert_true(sgf_view_surface_has_ink(surface, mid_x, mid_y, sample_radius));
    } else if (cases[i].child_row == cases[i].parent_row + 1) {
      int mid_x = (int)round((parent_x + child_x) / 2.0);
      int mid_y = (int)round((parent_y + child_y) / 2.0);
      g_assert_true(sgf_view_surface_has_ink(surface, mid_x, mid_y, sample_radius));
    } else {
      double row_above_y = 0.0;
      g_assert_true(sgf_view_compute_row_center(grid, row_heights, cases[i].child_row - 1, &row_above_y));
      int mid_x = (int)round(parent_x);
      int mid_y = (int)round((parent_y + row_above_y) / 2.0);
      g_assert_true(sgf_view_surface_has_ink(surface, mid_x, mid_y, sample_radius));
    }

    cairo_surface_destroy(surface);
    g_clear_object(&renderer);
    g_clear_object(&factory);
    gtk_window_destroy(GTK_WINDOW(window));
    g_clear_object(&tree);
  }
}

static void test_sgf_view_connectors_skip(void) {
  g_test_skip("GTK display not available.");
}

static void test_sgf_view_scrolls_to_new_node(void) {
  SgfTree *tree = sgf_tree_new();
  const guint initial_moves = 10;
  const guint appended_moves = 80;
  const guint total_moves = initial_moves + appended_moves;
  const SgfNode *last = NULL;
  for (guint i = 0; i < initial_moves; ++i) {
    SgfColor color = (i % 2 == 0) ? SGF_COLOR_BLACK : SGF_COLOR_WHITE;
    last = sgf_tree_append_move(tree, color, NULL);
  }

  SgfView *view = sgf_view_new();
  sgf_view_set_tree(view, tree);

  GtkWidget *root = sgf_view_get_widget(view);
  GtkWidget *window = gtk_window_new();
  gtk_window_set_default_size(GTK_WINDOW(window), 220, 180);
  gtk_window_set_child(GTK_WINDOW(window), root);
  gtk_window_present(GTK_WINDOW(window));
  sgf_view_wait(30);

  GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(root));
  GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(root));
  g_assert_nonnull(hadjustment);
  g_assert_nonnull(vadjustment);
  gtk_adjustment_set_value(hadjustment, 0.0);
  gtk_adjustment_set_value(vadjustment, 0.0);
  sgf_view_wait(10);

  for (guint i = 0; i < appended_moves; ++i) {
    SgfColor color = (i % 2 == 0) ? SGF_COLOR_BLACK : SGF_COLOR_WHITE;
    last = sgf_tree_append_move(tree, color, NULL);
  }
  g_assert_nonnull(last);

  sgf_view_refresh(view);
  gboolean scrolled = sgf_view_wait_for_scroll(hadjustment, vadjustment, 2000);
  g_assert_true(scrolled);

  GtkWidget *overlay = sgf_view_get_overlay(root);
  g_assert_nonnull(overlay);
  GtkWidget *tree_grid = sgf_view_find_grid(overlay);
  g_assert_nonnull(tree_grid);
  int disc_count = sgf_view_count_children(tree_grid);
  g_assert_cmpint(disc_count, ==, (int)total_moves + 1);

  gtk_window_destroy(GTK_WINDOW(window));
  g_clear_object(&view);
  g_clear_object(&tree);
}

static void test_sgf_view_scrolls_selected_disc_fully_into_view(void) {
  SgfTree *tree = sgf_tree_new();
  const guint total_moves = 120;
  const SgfNode *last = NULL;
  for (guint i = 0; i < total_moves; ++i) {
    SgfColor color = (i % 2 == 0) ? SGF_COLOR_BLACK : SGF_COLOR_WHITE;
    last = sgf_tree_append_move(tree, color, NULL);
  }
  g_assert_nonnull(last);

  const SgfNode *root = sgf_tree_get_root(tree);
  g_assert_nonnull(root);
  g_assert_true(sgf_tree_set_current(tree, root));

  SgfView *view = sgf_view_new();
  sgf_view_set_tree(view, tree);

  GtkWidget *root_widget = sgf_view_get_widget(view);
  GtkWidget *window = gtk_window_new();
  gtk_window_set_default_size(GTK_WINDOW(window), 220, 180);
  gtk_window_set_child(GTK_WINDOW(window), root_widget);
  gtk_window_present(GTK_WINDOW(window));
  sgf_view_wait(40);

  GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(root_widget));
  GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(root_widget));
  g_assert_nonnull(hadjustment);
  g_assert_nonnull(vadjustment);

  gtk_adjustment_set_value(hadjustment, 0.0);
  gtk_adjustment_set_value(vadjustment, 0.0);
  sgf_view_wait(20);

  sgf_view_set_selected(view, last);
  gboolean scrolled = sgf_view_wait_for_scroll(hadjustment, vadjustment, 2000);
  g_assert_true(scrolled);
  sgf_view_wait(200);

  GtkWidget *overlay = sgf_view_get_overlay(root_widget);
  g_assert_nonnull(overlay);
  GtkWidget *tree_grid = sgf_view_find_grid(overlay);
  g_assert_nonnull(tree_grid);
  GtkWidget *disc = sgf_view_find_disc_for_node(tree_grid, last);
  g_assert_nonnull(disc);

  graphene_rect_t bounds;
  gboolean has_bounds = sgf_view_compute_disc_bounds(tree_grid, disc, &bounds);
  g_assert_true(has_bounds);
  sgf_view_assert_disc_visible(hadjustment, vadjustment, &bounds);

  gtk_window_destroy(GTK_WINDOW(window));
  g_clear_object(&view);
  g_clear_object(&tree);
}

static void test_sgf_view_navigation(void) {
  SgfTree *tree = sgf_tree_new();
  g_autoptr(GBytes) move_1_payload = g_bytes_new_static("n1", sizeof("n1") - 1);
  const SgfNode *move_1 = sgf_tree_append_move(tree, SGF_COLOR_BLACK, move_1_payload);
  g_autoptr(GBytes) move_2_payload = g_bytes_new_static("n2a", sizeof("n2a") - 1);
  const SgfNode *move_2 = sgf_tree_append_move(tree, SGF_COLOR_WHITE, move_2_payload);
  g_autoptr(GBytes) move_3_payload = g_bytes_new_static("n3", sizeof("n3") - 1);
  sgf_tree_append_move(tree, SGF_COLOR_BLACK, move_3_payload);
  g_assert_true(sgf_tree_set_current(tree, move_1));
  g_autoptr(GBytes) branch_2_payload = g_bytes_new_static("n2b", sizeof("n2b") - 1);
  const SgfNode *branch_2 = sgf_tree_append_move(tree, SGF_COLOR_WHITE, branch_2_payload);

  SgfView *view = sgf_view_new();
  sgf_view_set_tree(view, tree);
  sgf_view_set_selected(view, move_1);

  g_assert_true(sgf_view_navigate(view, SGF_VIEW_NAVIGATE_CHILD));
  g_assert_true(sgf_view_get_selected(view) == move_2);

  g_assert_true(sgf_view_navigate(view, SGF_VIEW_NAVIGATE_NEXT_SIBLING));
  g_assert_true(sgf_view_get_selected(view) == branch_2);

  g_assert_true(sgf_view_navigate(view, SGF_VIEW_NAVIGATE_PREVIOUS_SIBLING));
  g_assert_true(sgf_view_get_selected(view) == move_2);

  g_assert_true(sgf_view_navigate(view, SGF_VIEW_NAVIGATE_PARENT));
  g_assert_true(sgf_view_get_selected(view) == move_1);

  g_clear_object(&view);
  g_clear_object(&tree);
}

static void test_sgf_view_layout_syncs_selection(void) {
  SgfTree *tree = sgf_tree_new();
  const SgfNode *move_1 = sgf_tree_append_move(tree, SGF_COLOR_BLACK, NULL);
  const SgfNode *move_2 = sgf_tree_append_move(tree, SGF_COLOR_WHITE, NULL);

  g_assert_nonnull(move_2);
  g_assert_true(sgf_tree_set_current(tree, move_1));

  SgfView *view = sgf_view_new();
  sgf_view_set_tree(view, tree);
  g_assert_true(sgf_view_get_selected(view) == move_1);

  g_assert_true(sgf_tree_set_current(tree, move_2));
  sgf_view_refresh(view);
  g_assert_true(sgf_view_get_selected(view) == move_2);

  g_clear_object(&view);
  g_clear_object(&tree);
}

static void test_sgf_view_force_layout_syncs_selection(void) {
  SgfTree *tree = sgf_tree_new();
  const SgfNode *move_1 = sgf_tree_append_move(tree, SGF_COLOR_BLACK, NULL);
  const SgfNode *move_2 = sgf_tree_append_move(tree, SGF_COLOR_WHITE, NULL);

  g_assert_nonnull(move_2);
  g_assert_true(sgf_tree_set_current(tree, move_1));

  SgfView *view = sgf_view_new();
  sgf_view_set_tree(view, tree);
  g_assert_true(sgf_view_get_selected(view) == move_1);

  g_assert_true(sgf_tree_set_current(tree, move_2));
  sgf_view_force_layout_sync(view);
  g_assert_true(sgf_view_get_selected(view) == move_2);

  g_clear_object(&view);
  g_clear_object(&tree);
}

static void test_sgf_view_scroller_remembers_missing_node(void) {
  SgfViewScroller *scroller = sgf_view_scroller_new();
  g_assert_nonnull(scroller);

  GtkWidget *window = gtk_window_new();
  gtk_window_set_default_size(GTK_WINDOW(window), 120, 100);

  GtkWidget *root = gtk_scrolled_window_new();
  GtkWidget *overlay = gtk_overlay_new();
  GtkWidget *grid = gtk_grid_new();
  gtk_widget_set_size_request(overlay, 640, 320);
  gtk_widget_set_size_request(grid, 640, 320);
  gtk_overlay_set_child(GTK_OVERLAY(overlay), grid);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(root), overlay);
  gtk_window_set_child(GTK_WINDOW(window), root);
  gtk_window_present(GTK_WINDOW(window));
  sgf_view_wait(40);

  GHashTable *node_widgets = g_hash_table_new(g_direct_hash, g_direct_equal);
  g_assert_nonnull(node_widgets);

  const SgfNode *selected = (const SgfNode *)0x1234;
  sgf_view_scroller_request_scroll(scroller,
                                   GTK_SCROLLED_WINDOW(root),
                                   node_widgets,
                                   selected);

  GtkAdjustment *hadjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(root));
  GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(root));
  g_assert_nonnull(hadjustment);
  g_assert_nonnull(vadjustment);
  g_assert_cmpfloat(gtk_adjustment_get_value(hadjustment), ==, 0.0);
  g_assert_cmpfloat(gtk_adjustment_get_value(vadjustment), ==, 0.0);

  GtkWidget *disc = gtk_button_new();
  gtk_widget_set_size_request(disc, 80, 80);
  gtk_grid_attach(GTK_GRID(grid), disc, 7, 1, 1, 1);
  g_hash_table_insert(node_widgets, (gpointer)selected, disc);
  gtk_widget_queue_resize(grid);
  gtk_widget_queue_resize(overlay);
  sgf_view_wait(40);

  sgf_view_scroller_on_layout_changed(scroller,
                                      GTK_SCROLLED_WINDOW(root),
                                      node_widgets);
  sgf_view_wait(80);

  g_assert_cmpfloat(gtk_adjustment_get_value(hadjustment), >, 0.0);
  g_assert_cmpfloat(gtk_adjustment_get_value(vadjustment), ==, 0.0);

  g_hash_table_unref(node_widgets);
  gtk_window_destroy(GTK_WINDOW(window));
  g_clear_object(&scroller);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  if (!gtk_init_check()) {
    g_test_add_func("/sgf-view/horizontal-position-inconsistency-detection",
                    test_sgf_view_horizontal_position_inconsistency_detection);
    g_test_add_func("/sgf-view/connectors", test_sgf_view_connectors_skip);
    g_test_add_func("/sgf-view/root-disc", test_sgf_view_connectors_skip);
    g_test_add_func("/sgf-view/tree-box-measure-overlay", test_sgf_view_connectors_skip);
    g_test_add_func("/sgf-view/branches", test_sgf_view_connectors_skip);
    g_test_add_func("/sgf-view/link-angles", test_sgf_view_connectors_skip);
    g_test_add_func("/sgf-view/navigation", test_sgf_view_connectors_skip);
    g_test_add_func("/sgf-view/scrolls-to-new-node", test_sgf_view_connectors_skip);
    g_test_add_func("/sgf-view/scrolls-selected-disc-fully-into-view",
                    test_sgf_view_connectors_skip);
    g_test_add_func("/sgf-view/layout-syncs-selection", test_sgf_view_connectors_skip);
    g_test_add_func("/sgf-view/force-layout-syncs-selection", test_sgf_view_connectors_skip);
    g_test_add_func("/sgf-view/scroller-remembers-missing-node", test_sgf_view_connectors_skip);
    return g_test_run();
  }

  g_test_add_func("/sgf-view/horizontal-position-inconsistency-detection",
                  test_sgf_view_horizontal_position_inconsistency_detection);
  g_test_add_func("/sgf-view/connectors", test_sgf_view_connectors);
  g_test_add_func("/sgf-view/root-disc", test_sgf_view_root_disc);
  g_test_add_func("/sgf-view/tree-box-measure-overlay", test_sgf_view_tree_box_is_measured_overlay);
  g_test_add_func("/sgf-view/branches", test_sgf_view_branch_columns);
  g_test_add_func("/sgf-view/link-angles", test_sgf_view_link_angles);
  g_test_add_func("/sgf-view/navigation", test_sgf_view_navigation);
  g_test_add_func("/sgf-view/scrolls-to-new-node", test_sgf_view_scrolls_to_new_node);
  g_test_add_func("/sgf-view/scrolls-selected-disc-fully-into-view",
                  test_sgf_view_scrolls_selected_disc_fully_into_view);
  g_test_add_func("/sgf-view/layout-syncs-selection", test_sgf_view_layout_syncs_selection);
  g_test_add_func("/sgf-view/force-layout-syncs-selection", test_sgf_view_force_layout_syncs_selection);
  g_test_add_func("/sgf-view/scroller-remembers-missing-node", test_sgf_view_scroller_remembers_missing_node);
  return g_test_run();
}
