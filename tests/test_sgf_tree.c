#include <assert.h>
#include <glib.h>

#include "../src/sgf_tree.h"

static void test_sgf_tree_append_and_select(void) {
  SgfTree *tree = sgf_tree_new();
  const SgfNode *root = sgf_tree_get_root(tree);

  assert(root != NULL);
  assert(sgf_node_get_move_number(root) == 0);
  assert(sgf_tree_get_current(tree) == root);

  const SgfNode *node = sgf_tree_append_move(tree, SGF_COLOR_BLACK, "12-16");

  assert(node != NULL);
  assert(sgf_tree_get_current(tree) == node);
  assert(sgf_node_get_move_number(node) == 1);
  assert(sgf_node_get_color(node) == SGF_COLOR_BLACK);
  assert(sgf_node_get_parent(node) == root);

  const char *move = sgf_node_get_property_first(node, "B");
  assert(move != NULL);
  assert(g_strcmp0(move, "12-16") == 0);

  assert(sgf_tree_set_current(tree, root));
  assert(sgf_tree_get_current(tree) == root);

  g_object_unref(tree);
}

static void test_sgf_tree_main_line(void) {
  SgfTree *tree = sgf_tree_new();
  const SgfNode *first = sgf_tree_append_move(tree, SGF_COLOR_WHITE, "21-17");
  assert(first != NULL);
  const SgfNode *second = sgf_tree_append_move(tree, SGF_COLOR_BLACK, "9-13");
  assert(second != NULL);

  GPtrArray *line = sgf_tree_build_main_line(tree);
  assert(line != NULL);
  assert(line->len == 3);
  assert(g_ptr_array_index(line, 0) == sgf_tree_get_root(tree));
  assert(g_ptr_array_index(line, 1) == first);
  assert(g_ptr_array_index(line, 2) == second);
  g_ptr_array_unref(line);

  g_object_unref(tree);
}

static void test_sgf_tree_append_existing_child(void) {
  SgfTree *tree = sgf_tree_new();
  const SgfNode *root = sgf_tree_get_root(tree);

  const SgfNode *first = sgf_tree_append_move(tree, SGF_COLOR_WHITE, "21-17");
  assert(first != NULL);
  assert(sgf_tree_set_current(tree, root));

  const SgfNode *second = sgf_tree_append_move(tree, SGF_COLOR_WHITE, "21-17");
  assert(second == first);

  const GPtrArray *children = sgf_node_get_children(root);
  assert(children != NULL);
  assert(children->len == 1);

  g_object_unref(tree);
}

int main(void) {
  test_sgf_tree_append_and_select();
  test_sgf_tree_main_line();
  test_sgf_tree_append_existing_child();

  return 0;
}
