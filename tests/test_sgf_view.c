#include <gtk/gtk.h>

#include "sgf_tree.h"
#include "sgf_view.h"

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
  const SgfNode *last = NULL;
  for (guint i = 0; i < 10; ++i) {
    last = sgf_tree_append_move(tree, (i % 2 == 0) ? SGF_COLOR_BLACK : SGF_COLOR_WHITE, NULL);
  }

  SgfView *view = sgf_view_new();
  sgf_view_set_tree(view, tree);

  GtkWidget *root = sgf_view_get_widget(view);
  GtkWidget *window = gtk_window_new();
  gtk_window_set_default_size(GTK_WINDOW(window), 220, 180);
  gtk_window_set_child(GTK_WINDOW(window), root);
  gtk_window_present(GTK_WINDOW(window));
  sgf_view_wait(30);

  GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(root));
  g_assert_nonnull(vadjustment);
  gtk_adjustment_set_value(vadjustment, 0.0);
  sgf_view_wait(10);

  for (guint i = 0; i < 80; ++i) {
    last = sgf_tree_append_move(tree, (i % 2 == 0) ? SGF_COLOR_BLACK : SGF_COLOR_WHITE, NULL);
  }
  g_assert_nonnull(last);

  sgf_view_set_selected(view, last);
  sgf_view_wait(80);

  double value = gtk_adjustment_get_value(vadjustment);
  g_assert_cmpfloat(value, >, 0.0);

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
    return g_test_run();
  }

  g_test_add_func("/sgf-view/connectors", test_sgf_view_connectors);
  g_test_add_func("/sgf-view/branches", test_sgf_view_branch_columns);
  g_test_add_func("/sgf-view/navigation", test_sgf_view_navigation);
  g_test_add_func("/sgf-view/scrolls-to-new-node", test_sgf_view_scrolls_to_new_node);
  return g_test_run();
}
