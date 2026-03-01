#include <glib.h>

#include "../src/game.h"
#include "../src/sgf_io.h"

static CheckersMove test_sgf_io_make_move(const guint8 *path, guint8 length, guint8 captures) {
  g_return_val_if_fail(path != NULL, (CheckersMove){0});
  g_return_val_if_fail(length >= 2, (CheckersMove){0});
  g_return_val_if_fail(length <= CHECKERS_MAX_MOVE_LENGTH, (CheckersMove){0});
  g_return_val_if_fail(captures <= length - 1, (CheckersMove){0});

  CheckersMove move = {0};
  move.length = length;
  move.captures = captures;
  for (guint8 i = 0; i < length; ++i) {
    move.path[i] = path[i];
  }
  return move;
}

static gboolean test_sgf_io_nodes_equal(const SgfNode *left, const SgfNode *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  if (sgf_node_get_color(left) != sgf_node_get_color(right)) {
    return FALSE;
  }

  g_autoptr(GBytes) left_payload = sgf_node_get_payload(left);
  g_autoptr(GBytes) right_payload = sgf_node_get_payload(right);
  if (left_payload == NULL && right_payload != NULL) {
    return FALSE;
  }
  if (left_payload != NULL && right_payload == NULL) {
    return FALSE;
  }
  if (left_payload != NULL && !g_bytes_equal(left_payload, right_payload)) {
    return FALSE;
  }

  const GPtrArray *left_children = sgf_node_get_children(left);
  const GPtrArray *right_children = sgf_node_get_children(right);
  guint left_count = left_children != NULL ? left_children->len : 0;
  guint right_count = right_children != NULL ? right_children->len : 0;
  if (left_count != right_count) {
    return FALSE;
  }

  for (guint i = 0; i < left_count; ++i) {
    const SgfNode *left_child = g_ptr_array_index((GPtrArray *)left_children, i);
    const SgfNode *right_child = g_ptr_array_index((GPtrArray *)right_children, i);
    if (!test_sgf_io_nodes_equal(left_child, right_child)) {
      return FALSE;
    }
  }

  return TRUE;
}

static const SgfNode *test_sgf_io_append_move(SgfTree *tree, SgfColor color, const CheckersMove *move) {
  g_return_val_if_fail(SGF_IS_TREE(tree), NULL);
  g_return_val_if_fail(move != NULL, NULL);

  g_autoptr(GBytes) payload = g_bytes_new(move, sizeof(*move));
  return sgf_tree_append_move(tree, color, payload);
}

static void test_sgf_io_assert_roundtrip(SgfTree *source) {
  g_return_if_fail(SGF_IS_TREE(source));

  g_autoptr(GError) error = NULL;
  g_autofree char *serialized = sgf_io_save_data(source, &error);
  g_assert_no_error(error);
  g_assert_nonnull(serialized);

  g_autoptr(SgfTree) loaded = NULL;
  gboolean loaded_ok = sgf_io_load_data(serialized, &loaded, &error);
  g_assert_no_error(error);
  g_assert_true(loaded_ok);
  g_assert_nonnull(loaded);
  g_assert_true(test_sgf_io_nodes_equal(sgf_tree_get_root(source), sgf_tree_get_root(loaded)));

  const SgfNode *loaded_current = sgf_tree_get_current(loaded);
  g_assert_nonnull(loaded_current);
  g_assert_cmpuint(sgf_node_get_move_number(loaded_current), ==, 0);
}

static void test_sgf_io_roundtrip_single_move(void) {
  g_autoptr(SgfTree) source = sgf_tree_new();
  const guint8 path[] = {12, 16};
  CheckersMove move = test_sgf_io_make_move(path, 2, 0);
  const SgfNode *node = test_sgf_io_append_move(source, SGF_COLOR_WHITE, &move);
  g_assert_nonnull(node);

  test_sgf_io_assert_roundtrip(source);
}

static void test_sgf_io_roundtrip_multiple_moves(void) {
  g_autoptr(SgfTree) source = sgf_tree_new();
  const guint8 path_a[] = {12, 16};
  const guint8 path_b[] = {23, 18};
  const guint8 path_c[] = {11, 15};
  CheckersMove move_a = test_sgf_io_make_move(path_a, 2, 0);
  CheckersMove move_b = test_sgf_io_make_move(path_b, 2, 0);
  CheckersMove move_c = test_sgf_io_make_move(path_c, 2, 0);

  const SgfNode *node_a = test_sgf_io_append_move(source, SGF_COLOR_WHITE, &move_a);
  g_assert_nonnull(node_a);
  g_assert_true(sgf_tree_set_current(source, node_a));
  const SgfNode *node_b = test_sgf_io_append_move(source, SGF_COLOR_BLACK, &move_b);
  g_assert_nonnull(node_b);
  g_assert_true(sgf_tree_set_current(source, node_b));
  const SgfNode *node_c = test_sgf_io_append_move(source, SGF_COLOR_WHITE, &move_c);
  g_assert_nonnull(node_c);

  test_sgf_io_assert_roundtrip(source);
}

static void test_sgf_io_roundtrip_single_capture(void) {
  g_autoptr(SgfTree) source = sgf_tree_new();
  const guint8 path[] = {23, 18};
  CheckersMove move = test_sgf_io_make_move(path, 2, 1);
  const SgfNode *node = test_sgf_io_append_move(source, SGF_COLOR_BLACK, &move);
  g_assert_nonnull(node);

  g_autoptr(GError) error = NULL;
  g_autofree char *serialized = sgf_io_save_data(source, &error);
  g_assert_no_error(error);
  g_assert_nonnull(serialized);
  g_assert_nonnull(strstr(serialized, "B[24x19]"));

  test_sgf_io_assert_roundtrip(source);
}

static void test_sgf_io_roundtrip_multi_capture(void) {
  g_autoptr(SgfTree) source = sgf_tree_new();
  const guint8 path[] = {31, 24, 17, 10};
  CheckersMove move = test_sgf_io_make_move(path, 4, 3);
  const SgfNode *node = test_sgf_io_append_move(source, SGF_COLOR_WHITE, &move);
  g_assert_nonnull(node);

  g_autoptr(GError) error = NULL;
  g_autofree char *serialized = sgf_io_save_data(source, &error);
  g_assert_no_error(error);
  g_assert_nonnull(serialized);
  g_assert_nonnull(strstr(serialized, "W[32x25x18x11]"));

  test_sgf_io_assert_roundtrip(source);
}

static void test_sgf_io_roundtrip_branches(void) {
  g_autoptr(SgfTree) source = sgf_tree_new();
  const SgfNode *root = sgf_tree_get_root(source);
  g_assert_nonnull(root);

  const guint8 path_a[] = {12, 16};
  const guint8 path_b[] = {10, 14};
  const guint8 path_a1[] = {23, 19};
  const guint8 path_b1[] = {22, 18};

  CheckersMove move_a = test_sgf_io_make_move(path_a, 2, 0);
  CheckersMove move_b = test_sgf_io_make_move(path_b, 2, 0);
  CheckersMove move_a1 = test_sgf_io_make_move(path_a1, 2, 0);
  CheckersMove move_b1 = test_sgf_io_make_move(path_b1, 2, 0);

  const SgfNode *node_a = test_sgf_io_append_move(source, SGF_COLOR_WHITE, &move_a);
  g_assert_nonnull(node_a);
  g_assert_true(sgf_tree_set_current(source, node_a));
  const SgfNode *node_a1 = test_sgf_io_append_move(source, SGF_COLOR_BLACK, &move_a1);
  g_assert_nonnull(node_a1);

  g_assert_true(sgf_tree_set_current(source, root));
  const SgfNode *node_b = test_sgf_io_append_move(source, SGF_COLOR_WHITE, &move_b);
  g_assert_nonnull(node_b);
  g_assert_true(sgf_tree_set_current(source, node_b));
  const SgfNode *node_b1 = test_sgf_io_append_move(source, SGF_COLOR_BLACK, &move_b1);
  g_assert_nonnull(node_b1);

  test_sgf_io_assert_roundtrip(source);

  g_autoptr(GError) error = NULL;
  g_autofree char *serialized = sgf_io_save_data(source, &error);
  g_assert_no_error(error);
  g_assert_nonnull(serialized);
  g_assert_nonnull(strstr(serialized, "W[13-17]"));
  g_assert_nonnull(strstr(serialized, "W[11-15]"));
}

static void test_sgf_io_load_rejects_invalid_header(void) {
  g_autoptr(SgfTree) loaded = NULL;
  g_autoptr(GError) error = NULL;
  gboolean ok = sgf_io_load_data("invalid-header\n", &loaded, &error);
  g_assert_false(ok);
  g_assert_error(error, g_quark_from_static_string("sgf-io-error"), 1);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/sgf-io/roundtrip-single-move", test_sgf_io_roundtrip_single_move);
  g_test_add_func("/sgf-io/roundtrip-multiple-moves", test_sgf_io_roundtrip_multiple_moves);
  g_test_add_func("/sgf-io/roundtrip-single-capture", test_sgf_io_roundtrip_single_capture);
  g_test_add_func("/sgf-io/roundtrip-multi-capture", test_sgf_io_roundtrip_multi_capture);
  g_test_add_func("/sgf-io/roundtrip-branches", test_sgf_io_roundtrip_branches);
  g_test_add_func("/sgf-io/load-invalid-header", test_sgf_io_load_rejects_invalid_header);
  return g_test_run();
}
