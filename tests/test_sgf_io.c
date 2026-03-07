#include <glib.h>

#include "../src/game.h"
#include "../src/sgf_io.h"
#include "../src/sgf_move_props.h"

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

  g_autoptr(GPtrArray) left_idents = sgf_node_copy_property_idents(left);
  g_autoptr(GPtrArray) right_idents = sgf_node_copy_property_idents(right);
  if (left_idents == NULL || right_idents == NULL) {
    return FALSE;
  }
  if (left_idents->len != right_idents->len) {
    return FALSE;
  }
  for (guint i = 0; i < left_idents->len; ++i) {
    const char *left_ident = g_ptr_array_index(left_idents, i);
    const char *right_ident = g_ptr_array_index(right_idents, i);
    if (g_strcmp0(left_ident, right_ident) != 0) {
      return FALSE;
    }

    const GPtrArray *left_values = sgf_node_get_property_values(left, left_ident);
    const GPtrArray *right_values = sgf_node_get_property_values(right, right_ident);
    if (left_values == NULL || right_values == NULL || left_values->len != right_values->len) {
      return FALSE;
    }

    for (guint j = 0; j < left_values->len; ++j) {
      const char *left_value = g_ptr_array_index((GPtrArray *)left_values, j);
      const char *right_value = g_ptr_array_index((GPtrArray *)right_values, j);
      if (g_strcmp0(left_value, right_value) != 0) {
        return FALSE;
      }
    }
  }

  if (sgf_node_get_move_number(left) != sgf_node_get_move_number(right)) {
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

  char notation[128] = {0};
  g_autoptr(GError) error = NULL;
  if (!sgf_move_props_format_notation(move, notation, sizeof(notation), &error)) {
    return NULL;
  }
  return sgf_tree_append_move(tree, color, notation);
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

static void test_sgf_io_preserves_repeated_property_values(void) {
  const char *content = "(;FF[4]CA[UTF-8]AP[gcheckers]GM[40]C[root-a][root-b];B[12-16]N[line-a][line-b])";

  g_autoptr(SgfTree) loaded = NULL;
  g_autoptr(GError) error = NULL;
  g_assert_true(sgf_io_load_data(content, &loaded, &error));
  g_assert_no_error(error);
  g_assert_nonnull(loaded);

  const SgfNode *root = sgf_tree_get_root(loaded);
  g_assert_nonnull(root);
  const GPtrArray *root_comments = sgf_node_get_property_values(root, "C");
  g_assert_nonnull(root_comments);
  g_assert_cmpuint(root_comments->len, ==, 2);
  g_assert_cmpstr(g_ptr_array_index((GPtrArray *)root_comments, 0), ==, "root-a");
  g_assert_cmpstr(g_ptr_array_index((GPtrArray *)root_comments, 1), ==, "root-b");

  const GPtrArray *root_children = sgf_node_get_children(root);
  g_assert_nonnull(root_children);
  g_assert_cmpuint(root_children->len, ==, 1);
  const SgfNode *move = g_ptr_array_index((GPtrArray *)root_children, 0);
  g_assert_nonnull(move);
  const GPtrArray *names = sgf_node_get_property_values(move, "N");
  g_assert_nonnull(names);
  g_assert_cmpuint(names->len, ==, 2);
  g_assert_cmpstr(g_ptr_array_index((GPtrArray *)names, 0), ==, "line-a");
  g_assert_cmpstr(g_ptr_array_index((GPtrArray *)names, 1), ==, "line-b");

  g_autofree char *saved = sgf_io_save_data(loaded, &error);
  g_assert_no_error(error);
  g_assert_nonnull(saved);

  g_autoptr(SgfTree) roundtrip = NULL;
  g_assert_true(sgf_io_load_data(saved, &roundtrip, &error));
  g_assert_no_error(error);
  g_assert_nonnull(roundtrip);

  const SgfNode *roundtrip_root = sgf_tree_get_root(roundtrip);
  const GPtrArray *roundtrip_comments = sgf_node_get_property_values(roundtrip_root, "C");
  g_assert_nonnull(roundtrip_comments);
  g_assert_cmpuint(roundtrip_comments->len, ==, 2);
  g_assert_cmpstr(g_ptr_array_index((GPtrArray *)roundtrip_comments, 0), ==, "root-a");
  g_assert_cmpstr(g_ptr_array_index((GPtrArray *)roundtrip_comments, 1), ==, "root-b");
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/sgf-io/roundtrip-single-move", test_sgf_io_roundtrip_single_move);
  g_test_add_func("/sgf-io/roundtrip-multiple-moves", test_sgf_io_roundtrip_multiple_moves);
  g_test_add_func("/sgf-io/roundtrip-single-capture", test_sgf_io_roundtrip_single_capture);
  g_test_add_func("/sgf-io/roundtrip-multi-capture", test_sgf_io_roundtrip_multi_capture);
  g_test_add_func("/sgf-io/roundtrip-branches", test_sgf_io_roundtrip_branches);
  g_test_add_func("/sgf-io/load-invalid-header", test_sgf_io_load_rejects_invalid_header);
  g_test_add_func("/sgf-io/repeated-property-values", test_sgf_io_preserves_repeated_property_values);
  return g_test_run();
}
