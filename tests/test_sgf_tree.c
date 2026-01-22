#include <assert.h>
#include <string.h>

#include <glib.h>

#include "../src/sgf_tree.h"

typedef struct {
  int value;
} DummyPayload;

static void test_sgf_tree_append_and_select(void) {
  SgfTree *tree = sgf_tree_new();
  const SgfNode *root = sgf_tree_get_root(tree);

  assert(root != NULL);
  assert(sgf_node_get_move_number(root) == 0);
  assert(sgf_tree_get_current(tree) == root);

  DummyPayload payload = {.value = 42};
  GBytes *bytes = g_bytes_new(&payload, sizeof(payload));
  const SgfNode *node = sgf_tree_append_move(tree, SGF_COLOR_BLACK, bytes);
  g_bytes_unref(bytes);

  assert(node != NULL);
  assert(sgf_tree_get_current(tree) == node);
  assert(sgf_node_get_move_number(node) == 1);
  assert(sgf_node_get_color(node) == SGF_COLOR_BLACK);
  assert(sgf_node_get_parent(node) == root);

  GBytes *stored = sgf_node_get_payload(node);
  assert(stored != NULL);
  gsize size = 0;
  const DummyPayload *stored_data = g_bytes_get_data(stored, &size);
  assert(size == sizeof(DummyPayload));
  assert(stored_data->value == 42);
  g_bytes_unref(stored);

  assert(sgf_tree_set_current(tree, root));
  assert(sgf_tree_get_current(tree) == root);

  g_object_unref(tree);
}

static void test_sgf_tree_main_line(void) {
  SgfTree *tree = sgf_tree_new();
  GBytes *bytes = g_bytes_new("X", 1);

  const SgfNode *first = sgf_tree_append_move(tree, SGF_COLOR_WHITE, bytes);
  assert(first != NULL);
  const SgfNode *second = sgf_tree_append_move(tree, SGF_COLOR_BLACK, bytes);
  assert(second != NULL);
  g_bytes_unref(bytes);

  GPtrArray *line = sgf_tree_build_main_line(tree);
  assert(line != NULL);
  assert(line->len == 3);
  assert(g_ptr_array_index(line, 0) == sgf_tree_get_root(tree));
  assert(g_ptr_array_index(line, 1) == first);
  assert(g_ptr_array_index(line, 2) == second);
  g_ptr_array_unref(line);

  g_object_unref(tree);
}

int main(void) {
  test_sgf_tree_append_and_select();
  test_sgf_tree_main_line();

  return 0;
}
