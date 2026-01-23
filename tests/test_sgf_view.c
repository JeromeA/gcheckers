#include <gtk/gtk.h>

#include "sgf_tree.h"
#include "sgf_view.h"

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

static void test_sgf_view_connectors(void) {
  SgfTree *tree = sgf_tree_new();
  sgf_tree_append_move(tree, SGF_COLOR_BLACK, NULL);
  sgf_tree_append_move(tree, SGF_COLOR_WHITE, NULL);
  sgf_tree_append_move(tree, SGF_COLOR_BLACK, NULL);

  SgfView *view = sgf_view_new();
  sgf_view_set_tree(view, tree);

  GtkWidget *root = sgf_view_get_widget(view);
  GtkWidget *overlay = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(root));
  g_assert_true(GTK_IS_OVERLAY(overlay));

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
  const SgfNode *move_1 = sgf_tree_append_move(tree, SGF_COLOR_BLACK, NULL);
  const SgfNode *move_2 = sgf_tree_append_move(tree, SGF_COLOR_WHITE, NULL);
  sgf_tree_append_move(tree, SGF_COLOR_BLACK, NULL);
  g_assert_true(sgf_tree_set_current(tree, move_1));
  const SgfNode *branch_2 = sgf_tree_append_move(tree, SGF_COLOR_WHITE, NULL);

  SgfView *view = sgf_view_new();
  sgf_view_set_tree(view, tree);

  GtkWidget *root = sgf_view_get_widget(view);
  GtkWidget *overlay = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(root));
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

static void test_sgf_view_navigation(void) {
  SgfTree *tree = sgf_tree_new();
  const SgfNode *move_1 = sgf_tree_append_move(tree, SGF_COLOR_BLACK, NULL);
  const SgfNode *move_2 = sgf_tree_append_move(tree, SGF_COLOR_WHITE, NULL);
  sgf_tree_append_move(tree, SGF_COLOR_BLACK, NULL);
  g_assert_true(sgf_tree_set_current(tree, move_1));
  const SgfNode *branch_2 = sgf_tree_append_move(tree, SGF_COLOR_WHITE, NULL);

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
    return g_test_run();
  }

  g_test_add_func("/sgf-view/connectors", test_sgf_view_connectors);
  g_test_add_func("/sgf-view/branches", test_sgf_view_branch_columns);
  g_test_add_func("/sgf-view/navigation", test_sgf_view_navigation);
  return g_test_run();
}
