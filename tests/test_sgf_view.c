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

  GtkWidget *tree_box = NULL;
  GtkWidget *child = gtk_widget_get_first_child(overlay);
  while (child) {
    if (child != lines_area && GTK_IS_BOX(child)) {
      tree_box = child;
      break;
    }
    child = gtk_widget_get_next_sibling(child);
  }
  g_assert_nonnull(tree_box);

  GtkWidget *row = gtk_widget_get_first_child(tree_box);
  g_assert_nonnull(row);

  g_assert_cmpint(sgf_view_count_children(row), ==, 3);

  child = gtk_widget_get_first_child(row);
  g_assert_true(GTK_IS_BUTTON(child));
  child = gtk_widget_get_next_sibling(child);
  g_assert_true(GTK_IS_BUTTON(child));
  child = gtk_widget_get_next_sibling(child);
  g_assert_true(GTK_IS_BUTTON(child));

  g_clear_object(&view);
  g_clear_object(&tree);
}

static void test_sgf_view_connectors_skip(void) {
  g_test_skip("GTK display not available.");
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  if (!gtk_init_check()) {
    g_test_add_func("/sgf-view/connectors", test_sgf_view_connectors_skip);
    return g_test_run();
  }

  g_test_add_func("/sgf-view/connectors", test_sgf_view_connectors);
  return g_test_run();
}
