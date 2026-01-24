#include <gtk/gtk.h>

#include "sgf_tree.h"
#include "sgf_view.h"
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

  const double padding = (double)SGF_VIEW_SCROLLER_VISIBILITY_PADDING;
  const double visible_start_x = gtk_adjustment_get_value(hadjustment);
  const double visible_end_x = visible_start_x + gtk_adjustment_get_page_size(hadjustment);
  const double visible_start_y = gtk_adjustment_get_value(vadjustment);
  const double visible_end_y = visible_start_y + gtk_adjustment_get_page_size(vadjustment);
  const double padded_start_x = MAX(0.0, bounds->origin.x - padding);
  const double padded_end_x = bounds->origin.x + bounds->size.width + padding;
  const double padded_start_y = MAX(0.0, bounds->origin.y - padding);
  const double padded_end_y = bounds->origin.y + bounds->size.height + padding;
  const double epsilon = 1.0;

  gboolean h_visible = visible_start_x <= padded_start_x + epsilon &&
                       visible_end_x + epsilon >= padded_end_x;
  gboolean v_visible = visible_start_y <= padded_start_y + epsilon &&
                       visible_end_y + epsilon >= padded_end_y;

  g_assert_true(h_visible);
  g_assert_true(v_visible);
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

  g_assert_cmpint(sgf_view_count_children(tree_grid), ==, 3);

  GtkWidget *child = gtk_widget_get_first_child(tree_grid);
  g_assert_true(GTK_IS_BUTTON(child));
  child = gtk_widget_get_next_sibling(child);
  g_assert_true(GTK_IS_BUTTON(child));
  child = gtk_widget_get_next_sibling(child);
  g_assert_true(GTK_IS_BUTTON(child));

  g_clear_object(&view);
  g_clear_object(&tree);
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

  g_assert_cmpint(main_column, ==, 1);
  g_assert_cmpint(branch_column, ==, 1);
  g_assert_cmpint(branch_row, >, main_row);

  g_clear_object(&view);
  g_clear_object(&tree);
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

  sgf_view_set_selected(view, last);
  gboolean scrolled = sgf_view_wait_for_scroll(hadjustment, vadjustment, 2000);
  g_assert_true(scrolled);

  GtkWidget *overlay = sgf_view_get_overlay(root);
  g_assert_nonnull(overlay);
  GtkWidget *tree_grid = sgf_view_find_grid(overlay);
  g_assert_nonnull(tree_grid);
  int disc_count = sgf_view_count_children(tree_grid);
  g_assert_cmpint(disc_count, ==, (int)total_moves);

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

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  if (!gtk_init_check()) {
    g_test_add_func("/sgf-view/connectors", test_sgf_view_connectors_skip);
    g_test_add_func("/sgf-view/branches", test_sgf_view_connectors_skip);
    g_test_add_func("/sgf-view/navigation", test_sgf_view_connectors_skip);
    g_test_add_func("/sgf-view/scrolls-to-new-node", test_sgf_view_connectors_skip);
    g_test_add_func("/sgf-view/scrolls-selected-disc-fully-into-view",
                    test_sgf_view_connectors_skip);
    return g_test_run();
  }

  g_test_add_func("/sgf-view/connectors", test_sgf_view_connectors);
  g_test_add_func("/sgf-view/branches", test_sgf_view_branch_columns);
  g_test_add_func("/sgf-view/navigation", test_sgf_view_navigation);
  g_test_add_func("/sgf-view/scrolls-to-new-node", test_sgf_view_scrolls_to_new_node);
  g_test_add_func("/sgf-view/scrolls-selected-disc-fully-into-view",
                  test_sgf_view_scrolls_selected_disc_fully_into_view);
  return g_test_run();
}
