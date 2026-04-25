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

static void test_sgf_tree_append_non_move_node(void) {
  SgfTree *tree = sgf_tree_new();
  const SgfNode *root = sgf_tree_get_root(tree);
  assert(root != NULL);

  const SgfNode *node = sgf_tree_append_node(tree);
  assert(node != NULL);
  assert(sgf_tree_get_current(tree) == node);
  assert(sgf_node_get_parent(node) == root);
  assert(sgf_node_get_move_number(node) == 0);
  assert(sgf_node_get_color(node) == SGF_COLOR_NONE);

  g_object_unref(tree);
}

static void test_sgf_tree_current_branch(void) {
  SgfTree *tree = sgf_tree_new();
  const SgfNode *root = sgf_tree_get_root(tree);
  assert(root != NULL);

  const SgfNode *main_1 = sgf_tree_append_move(tree, SGF_COLOR_WHITE, "21-17");
  assert(main_1 != NULL);
  const SgfNode *main_2 = sgf_tree_append_move(tree, SGF_COLOR_BLACK, "9-13");
  assert(main_2 != NULL);
  const SgfNode *main_3 = sgf_tree_append_move(tree, SGF_COLOR_WHITE, "24-20");
  assert(main_3 != NULL);

  assert(sgf_tree_set_current(tree, main_1));
  const SgfNode *branch_2 = sgf_tree_append_move(tree, SGF_COLOR_BLACK, "10-14");
  assert(branch_2 != NULL);
  const SgfNode *branch_3 = sgf_tree_append_move(tree, SGF_COLOR_WHITE, "22-18");
  assert(branch_3 != NULL);

  assert(sgf_tree_set_current(tree, branch_2));
  GPtrArray *branch = sgf_tree_build_current_branch(tree);
  assert(branch != NULL);
  assert(branch->len == 4);
  assert(g_ptr_array_index(branch, 0) == root);
  assert(g_ptr_array_index(branch, 1) == main_1);
  assert(g_ptr_array_index(branch, 2) == branch_2);
  assert(g_ptr_array_index(branch, 3) == branch_3);
  g_ptr_array_unref(branch);

  assert(sgf_tree_set_current(tree, main_1));
  branch = sgf_tree_build_current_branch(tree);
  assert(branch != NULL);
  assert(branch->len == 4);
  assert(g_ptr_array_index(branch, 0) == root);
  assert(g_ptr_array_index(branch, 1) == main_1);
  assert(g_ptr_array_index(branch, 2) == main_2);
  assert(g_ptr_array_index(branch, 3) == main_3);
  g_ptr_array_unref(branch);

  g_object_unref(tree);
}

static void test_sgf_tree_collect_nodes_preorder(void) {
  SgfTree *tree = sgf_tree_new();
  const SgfNode *root = sgf_tree_get_root(tree);
  assert(root != NULL);

  const SgfNode *a = sgf_tree_append_move(tree, SGF_COLOR_WHITE, "21-17");
  assert(a != NULL);
  const SgfNode *b = sgf_tree_append_move(tree, SGF_COLOR_BLACK, "9-13");
  assert(b != NULL);
  assert(sgf_tree_set_current(tree, a));
  const SgfNode *c = sgf_tree_append_move(tree, SGF_COLOR_BLACK, "10-14");
  assert(c != NULL);

  GPtrArray *nodes = sgf_tree_collect_nodes_preorder(tree);
  assert(nodes != NULL);
  assert(nodes->len == 4);
  assert(g_ptr_array_index(nodes, 0) == root);
  assert(g_ptr_array_index(nodes, 1) == a);
  assert(g_ptr_array_index(nodes, 2) == b);
  assert(g_ptr_array_index(nodes, 3) == c);
  g_ptr_array_unref(nodes);

  g_object_unref(tree);
}

static void test_sgf_tree_node_analysis_set_get_clear(void) {
  SgfTree *tree = sgf_tree_new();
  SgfNode *root = (SgfNode *)sgf_tree_get_root(tree);
  assert(root != NULL);

  SgfNodeAnalysis *analysis = sgf_node_analysis_new();
  assert(analysis != NULL);
  analysis->depth = 6;
  analysis->nodes = 456;
  analysis->tt_probes = 321;
  analysis->tt_hits = 123;
  analysis->tt_cutoffs = 78;

  assert(sgf_node_analysis_add_scored_move(analysis, "12-16", 42, 1234));
  assert(sgf_node_analysis_add_scored_move(analysis, "11-15", 12, 56));

  assert(sgf_node_set_analysis(root, analysis));
  sgf_node_analysis_free(analysis);

  SgfNodeAnalysis *saved = sgf_node_get_analysis(root);
  assert(saved != NULL);
  assert(saved->depth == 6);
  assert(saved->nodes == 456);
  assert(saved->tt_probes == 321);
  assert(saved->tt_hits == 123);
  assert(saved->tt_cutoffs == 78);
  assert(saved->moves != NULL);
  assert(saved->moves->len == 2);

  SgfNodeScoredMove *first = g_ptr_array_index(saved->moves, 0);
  assert(first != NULL);
  assert(first->score == 42);
  assert(first->nodes == 1234);
  assert(strcmp(first->move_text, "12-16") == 0);

  assert(sgf_node_clear_analysis(root));
  assert(sgf_node_get_analysis(root) == NULL);

  sgf_node_analysis_free(saved);
  g_object_unref(tree);
}

int main(void) {
  test_sgf_tree_append_and_select();
  test_sgf_tree_main_line();
  test_sgf_tree_append_existing_child();
  test_sgf_tree_append_non_move_node();
  test_sgf_tree_current_branch();
  test_sgf_tree_collect_nodes_preorder();
  test_sgf_tree_node_analysis_set_get_clear();

  return 0;
}
