#include <glib.h>

#include "../src/active_game_backend.h"
#if defined(GGAME_GAME_BOOP)
#include "../src/games/boop/boop_game.h"
#endif
#include "../src/games/checkers/game.h"
#include "../src/games/checkers/rulesets.h"
#include "../src/sgf_io.h"
#include "../src/sgf_move_props.h"

#if defined(GGAME_GAME_BOOP)
#define TEST_SGF_IO_CHECKERS_ONLY G_GNUC_UNUSED
#else
#define TEST_SGF_IO_CHECKERS_ONLY
#endif

static CheckersMove TEST_SGF_IO_CHECKERS_ONLY test_sgf_io_make_move(const guint8 *path,
                                                                    guint8 length,
                                                                    guint8 captures) {
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

  g_autoptr(SgfNodeAnalysis) left_analysis = sgf_node_get_analysis(left);
  g_autoptr(SgfNodeAnalysis) right_analysis = sgf_node_get_analysis(right);
  if ((left_analysis == NULL) != (right_analysis == NULL)) {
    return FALSE;
  }
  if (left_analysis != NULL) {
    if (left_analysis->depth != right_analysis->depth || left_analysis->nodes != right_analysis->nodes ||
        left_analysis->tt_probes != right_analysis->tt_probes || left_analysis->tt_hits != right_analysis->tt_hits ||
        left_analysis->tt_cutoffs != right_analysis->tt_cutoffs) {
      return FALSE;
    }
    if (left_analysis->moves == NULL || right_analysis->moves == NULL ||
        left_analysis->moves->len != right_analysis->moves->len) {
      return FALSE;
    }
    for (guint i = 0; i < left_analysis->moves->len; ++i) {
      const SgfNodeScoredMove *left_move = g_ptr_array_index(left_analysis->moves, i);
      const SgfNodeScoredMove *right_move = g_ptr_array_index(right_analysis->moves, i);
      if (left_move == NULL || right_move == NULL) {
        return FALSE;
      }
      if (left_move->score != right_move->score || left_move->nodes != right_move->nodes ||
          g_strcmp0(left_move->move_text, right_move->move_text) != 0) {
        return FALSE;
      }
    }
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

static const SgfNode * TEST_SGF_IO_CHECKERS_ONLY test_sgf_io_append_move(SgfTree *tree,
                                                                         SgfColor color,
                                                                         const CheckersMove *move) {
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

static void TEST_SGF_IO_CHECKERS_ONLY test_sgf_io_roundtrip_single_move(void) {
  g_autoptr(SgfTree) source = sgf_tree_new();
  const guint8 path[] = {12, 16};
  CheckersMove move = test_sgf_io_make_move(path, 2, 0);
  const SgfNode *node = test_sgf_io_append_move(source, SGF_COLOR_WHITE, &move);
  g_assert_nonnull(node);

  test_sgf_io_assert_roundtrip(source);
}

static void TEST_SGF_IO_CHECKERS_ONLY test_sgf_io_roundtrip_multiple_moves(void) {
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

static void TEST_SGF_IO_CHECKERS_ONLY test_sgf_io_roundtrip_single_capture(void) {
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

static void TEST_SGF_IO_CHECKERS_ONLY test_sgf_io_roundtrip_multi_capture(void) {
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

static void TEST_SGF_IO_CHECKERS_ONLY test_sgf_io_roundtrip_branches(void) {
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

static void TEST_SGF_IO_CHECKERS_ONLY test_sgf_io_preserves_repeated_property_values(void) {
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

static void TEST_SGF_IO_CHECKERS_ONLY test_sgf_io_roundtrip_node_analysis_properties(void) {
  g_autoptr(SgfTree) source = sgf_tree_new();
  const guint8 path[] = {12, 16};
  CheckersMove move = test_sgf_io_make_move(path, 2, 0);
  SgfNode *node = (SgfNode *)test_sgf_io_append_move(source, SGF_COLOR_WHITE, &move);
  g_assert_nonnull(node);

  g_autoptr(SgfNodeAnalysis) analysis = sgf_node_analysis_new();
  g_assert_nonnull(analysis);
  analysis->depth = 7;
  analysis->nodes = 1500;
  analysis->tt_probes = 700;
  analysis->tt_hits = 250;
  analysis->tt_cutoffs = 90;
  g_assert_true(sgf_node_analysis_add_scored_move(analysis, "13-17", 12, 345));
  g_assert_true(sgf_node_set_analysis(node, analysis));

  g_autoptr(GError) error = NULL;
  g_autofree char *serialized = sgf_io_save_data(source, &error);
  g_assert_no_error(error);
  g_assert_nonnull(serialized);
  g_assert_nonnull(strstr(serialized, "GCAD[7]"));
  g_assert_nonnull(strstr(serialized, "GCAS[nodes=1500;tt_probes=700;tt_hits=250;tt_cutoffs=90]"));
  g_assert_nonnull(strstr(serialized, "GCAN[13-17:12:345]"));

  g_autoptr(SgfTree) loaded = NULL;
  g_assert_true(sgf_io_load_data(serialized, &loaded, &error));
  g_assert_no_error(error);
  g_assert_nonnull(loaded);

  const SgfNode *root = sgf_tree_get_root(loaded);
  const GPtrArray *children = sgf_node_get_children(root);
  g_assert_nonnull(children);
  g_assert_cmpuint(children->len, ==, 1);
  const SgfNode *loaded_node = g_ptr_array_index((GPtrArray *)children, 0);
  g_assert_nonnull(loaded_node);

  g_autoptr(SgfNodeAnalysis) loaded_analysis = sgf_node_get_analysis(loaded_node);
  g_assert_nonnull(loaded_analysis);
  g_assert_cmpuint(loaded_analysis->depth, ==, 7);
  g_assert_cmpuint(loaded_analysis->moves->len, ==, 1);
  const SgfNodeScoredMove *loaded_move = g_ptr_array_index(loaded_analysis->moves, 0);
  g_assert_nonnull(loaded_move);
  g_assert_cmpstr(loaded_move->move_text, ==, "13-17");
}

static void TEST_SGF_IO_CHECKERS_ONLY test_sgf_io_roundtrip_ruleset_property(void) {
  g_autoptr(SgfTree) tree = sgf_tree_new();
  const GameBackendVariant *variant = GGAME_ACTIVE_GAME_BACKEND->variant_by_short_name("russian");
  g_assert_nonnull(variant);
  g_assert_true(sgf_io_tree_set_variant(tree, variant));

  g_autoptr(GError) error = NULL;
  g_autofree char *serialized = sgf_io_save_data(tree, &error);
  g_assert_no_error(error);
  g_assert_nonnull(serialized);
  g_assert_nonnull(strstr(serialized, "RU[russian]"));

  g_autoptr(SgfTree) loaded = NULL;
  g_assert_true(sgf_io_load_data(serialized, &loaded, &error));
  g_assert_no_error(error);
  g_assert_nonnull(loaded);

  const GameBackendVariant *loaded_variant = NULL;
  g_assert_true(sgf_io_tree_get_variant(loaded, &loaded_variant, &error));
  g_assert_no_error(error);
  g_assert_nonnull(loaded_variant);
  g_assert_cmpstr(loaded_variant->short_name, ==, "russian");
}

static void TEST_SGF_IO_CHECKERS_ONLY test_sgf_io_rejects_unknown_ruleset_property(void) {
  g_autoptr(SgfTree) tree = NULL;
  g_autoptr(GError) error = NULL;
  g_assert_true(sgf_io_load_data("(;FF[4]CA[UTF-8]AP[gcheckers]GM[40]RU[unknown])", &tree, &error));
  g_assert_no_error(error);
  g_assert_nonnull(tree);

  const GameBackendVariant *variant = NULL;
  g_assert_false(sgf_io_tree_get_variant(tree, &variant, &error));
  g_assert_error(error, g_quark_from_static_string("sgf-io-error"), 22);
}

static void TEST_SGF_IO_CHECKERS_ONLY test_sgf_io_rejects_missing_ruleset_property(void) {
  g_autoptr(SgfTree) tree = NULL;
  g_autoptr(GError) error = NULL;
  g_assert_true(sgf_io_load_data("(;FF[4]CA[UTF-8]AP[gcheckers]GM[40])", &tree, &error));
  g_assert_no_error(error);
  g_assert_nonnull(tree);

  const GameBackendVariant *variant = NULL;
  g_assert_false(sgf_io_tree_get_variant(tree, &variant, &error));
  g_assert_error(error, g_quark_from_static_string("sgf-io-error"), 23);
}

static void TEST_SGF_IO_CHECKERS_ONLY test_sgf_io_load_setup_and_player_to_play_properties(void) {
  const char *content =
      "(;FF[4]CA[UTF-8]AP[gcheckers]GM[40]AB[ab][cd]AW[bc]ABK[ab]AE[ef]PL[B];AE[ab]AW[ab]AWK[ab]PL[W])";

  g_autoptr(SgfTree) loaded = NULL;
  g_autoptr(GError) error = NULL;
  g_assert_true(sgf_io_load_data(content, &loaded, &error));
  g_assert_no_error(error);
  g_assert_nonnull(loaded);

  const SgfNode *root = sgf_tree_get_root(loaded);
  g_assert_nonnull(root);
  const GPtrArray *ab_root = sgf_node_get_property_values(root, "AB");
  const GPtrArray *aw_root = sgf_node_get_property_values(root, "AW");
  const GPtrArray *abk_root = sgf_node_get_property_values(root, "ABK");
  const GPtrArray *ae_root = sgf_node_get_property_values(root, "AE");
  g_assert_nonnull(ab_root);
  g_assert_nonnull(aw_root);
  g_assert_nonnull(abk_root);
  g_assert_nonnull(ae_root);
  g_assert_cmpuint(ab_root->len, ==, 2);
  g_assert_cmpuint(aw_root->len, ==, 1);
  g_assert_cmpuint(abk_root->len, ==, 1);
  g_assert_cmpuint(ae_root->len, ==, 1);
  g_assert_cmpstr(sgf_node_get_property_first(root, "PL"), ==, "B");

  const GPtrArray *children = sgf_node_get_children(root);
  g_assert_nonnull(children);
  g_assert_cmpuint(children->len, ==, 1);
  const SgfNode *child = g_ptr_array_index((GPtrArray *)children, 0);
  g_assert_nonnull(child);
  g_assert_cmpuint(sgf_node_get_move_number(child), ==, 0);
  g_assert_cmpint((gint)sgf_node_get_color(child), ==, (gint)SGF_COLOR_NONE);
  g_assert_cmpstr(sgf_node_get_property_first(child, "PL"), ==, "W");
  const GPtrArray *aw_child = sgf_node_get_property_values(child, "AW");
  const GPtrArray *awk_child = sgf_node_get_property_values(child, "AWK");
  g_assert_nonnull(aw_child);
  g_assert_nonnull(awk_child);
  g_assert_cmpuint(aw_child->len, ==, 1);
  g_assert_cmpuint(awk_child->len, ==, 1);

  g_autofree char *saved = sgf_io_save_data(loaded, &error);
  g_assert_no_error(error);
  g_assert_nonnull(saved);
  g_assert_nonnull(strstr(saved, "AB[ab][cd]"));
  g_assert_nonnull(strstr(saved, "AW[bc]"));
  g_assert_nonnull(strstr(saved, "ABK[ab]"));
  g_assert_nonnull(strstr(saved, "AE[ef]"));
  g_assert_nonnull(strstr(saved, "PL[B]"));
  g_assert_nonnull(strstr(saved, ";AE[ab]AW[ab]AWK[ab]PL[W]"));
}

static void TEST_SGF_IO_CHECKERS_ONLY test_sgf_io_load_legacy_analysis_move_properties(void) {
  const char *content =
      "(;FF[4]CA[UTF-8]AP[gcheckers]GM[40];GCAD[7]GCAS[nodes=1500;tt_probes=700;tt_hits=250;tt_cutoffs=90]"
      "GCAN[13-17:12]W[13-17])";

  g_autoptr(SgfTree) loaded = NULL;
  g_autoptr(GError) error = NULL;
  g_assert_true(sgf_io_load_data(content, &loaded, &error));
  g_assert_no_error(error);
  g_assert_nonnull(loaded);

  const SgfNode *root = sgf_tree_get_root(loaded);
  const GPtrArray *children = sgf_node_get_children(root);
  g_assert_nonnull(children);
  g_assert_cmpuint(children->len, ==, 1);

  const SgfNode *node = g_ptr_array_index((GPtrArray *)children, 0);
  g_assert_nonnull(node);
  g_autoptr(SgfNodeAnalysis) analysis = sgf_node_get_analysis(node);
  g_assert_nonnull(analysis);
  g_assert_cmpuint(analysis->moves->len, ==, 1);

  const SgfNodeScoredMove *entry = g_ptr_array_index(analysis->moves, 0);
  g_assert_nonnull(entry);
  g_assert_cmpint(entry->score, ==, 12);
  g_assert_true(entry->nodes == 0);
}

#if defined(GGAME_GAME_BOOP)
static const SgfNode *test_sgf_io_append_boop_move(SgfTree *tree, SgfColor color, const BoopMove *move) {
  g_return_val_if_fail(SGF_IS_TREE(tree), NULL);
  g_return_val_if_fail(move != NULL, NULL);

  char notation[128] = {0};
  g_autoptr(GError) error = NULL;
  if (!sgf_move_props_format_notation(move, notation, sizeof(notation), &error)) {
    return NULL;
  }
  return sgf_tree_append_move(tree, color, notation);
}

static void test_sgf_io_boop_roundtrip_single_move(void) {
  g_autoptr(SgfTree) source = sgf_tree_new();
  BoopMove move = {
    .square = 0,
    .rank = BOOP_PIECE_RANK_KITTEN,
  };
  const SgfNode *node = test_sgf_io_append_boop_move(source, SGF_COLOR_BLACK, &move);
  g_assert_nonnull(node);

  g_autoptr(GError) error = NULL;
  g_autofree char *serialized = sgf_io_save_data(source, &error);
  g_assert_no_error(error);
  g_assert_nonnull(serialized);
  g_assert_nonnull(strstr(serialized, "B[K@a1]"));

  test_sgf_io_assert_roundtrip(source);
}

static void test_sgf_io_boop_roundtrip_promotion_move(void) {
  g_autoptr(SgfTree) source = sgf_tree_new();
  BoopMove move = {
    .square = 0,
    .rank = BOOP_PIECE_RANK_KITTEN,
    .promotion_mask = G_GUINT64_CONSTANT(0x7),
  };
  const SgfNode *node = test_sgf_io_append_boop_move(source, SGF_COLOR_BLACK, &move);
  g_assert_nonnull(node);

  g_autoptr(GError) error = NULL;
  g_autofree char *serialized = sgf_io_save_data(source, &error);
  g_assert_no_error(error);
  g_assert_nonnull(serialized);
  g_assert_nonnull(strstr(serialized, "B[K@a1+a1,b1,c1]"));

  test_sgf_io_assert_roundtrip(source);
}

static void test_sgf_io_boop_preserves_repeated_property_values(void) {
  const char *content = "(;FF[4]CA[UTF-8]AP[gboop]GM[0]C[root-a][root-b];B[K@a1]N[line-a][line-b])";

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

  const GPtrArray *root_children = sgf_node_get_children(root);
  g_assert_nonnull(root_children);
  g_assert_cmpuint(root_children->len, ==, 1);
  const SgfNode *move = g_ptr_array_index((GPtrArray *)root_children, 0);
  g_assert_nonnull(move);
  const GPtrArray *names = sgf_node_get_property_values(move, "N");
  g_assert_nonnull(names);
  g_assert_cmpuint(names->len, ==, 2);
}

static void test_sgf_io_boop_accepts_missing_ruleset_property(void) {
  g_autoptr(SgfTree) tree = NULL;
  g_autoptr(GError) error = NULL;
  g_assert_true(sgf_io_load_data("(;FF[4]CA[UTF-8]AP[gboop]GM[0])", &tree, &error));
  g_assert_no_error(error);
  g_assert_nonnull(tree);

  const GameBackendVariant *variant = NULL;
  g_assert_true(sgf_io_tree_get_variant(tree, &variant, &error));
  g_assert_no_error(error);
  g_assert_null(variant);
}

static void test_sgf_io_boop_rejects_ruleset_property(void) {
  g_autoptr(SgfTree) tree = NULL;
  g_autoptr(GError) error = NULL;
  g_assert_true(sgf_io_load_data("(;FF[4]CA[UTF-8]AP[gboop]GM[0]RU[unknown])", &tree, &error));
  g_assert_no_error(error);
  g_assert_nonnull(tree);

  const GameBackendVariant *variant = NULL;
  g_assert_false(sgf_io_tree_get_variant(tree, &variant, &error));
  g_assert_error(error, g_quark_from_static_string("sgf-io-error"), 22);
}
#endif

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

#if defined(GGAME_GAME_BOOP)
  g_test_add_func("/sgf-io/roundtrip-single-move", test_sgf_io_boop_roundtrip_single_move);
  g_test_add_func("/sgf-io/roundtrip-promotion-move", test_sgf_io_boop_roundtrip_promotion_move);
  g_test_add_func("/sgf-io/load-invalid-header", test_sgf_io_load_rejects_invalid_header);
  g_test_add_func("/sgf-io/repeated-property-values", test_sgf_io_boop_preserves_repeated_property_values);
  g_test_add_func("/sgf-io/ruleset-missing", test_sgf_io_boop_accepts_missing_ruleset_property);
  g_test_add_func("/sgf-io/ruleset-invalid", test_sgf_io_boop_rejects_ruleset_property);
#else
  g_test_add_func("/sgf-io/roundtrip-single-move", test_sgf_io_roundtrip_single_move);
  g_test_add_func("/sgf-io/roundtrip-multiple-moves", test_sgf_io_roundtrip_multiple_moves);
  g_test_add_func("/sgf-io/roundtrip-single-capture", test_sgf_io_roundtrip_single_capture);
  g_test_add_func("/sgf-io/roundtrip-multi-capture", test_sgf_io_roundtrip_multi_capture);
  g_test_add_func("/sgf-io/roundtrip-branches", test_sgf_io_roundtrip_branches);
  g_test_add_func("/sgf-io/load-invalid-header", test_sgf_io_load_rejects_invalid_header);
  g_test_add_func("/sgf-io/repeated-property-values", test_sgf_io_preserves_repeated_property_values);
  g_test_add_func("/sgf-io/ruleset-roundtrip", test_sgf_io_roundtrip_ruleset_property);
  g_test_add_func("/sgf-io/ruleset-invalid", test_sgf_io_rejects_unknown_ruleset_property);
  g_test_add_func("/sgf-io/ruleset-missing", test_sgf_io_rejects_missing_ruleset_property);
  g_test_add_func("/sgf-io/analysis-roundtrip", test_sgf_io_roundtrip_node_analysis_properties);
  g_test_add_func("/sgf-io/load-legacy-analysis-move", test_sgf_io_load_legacy_analysis_move_properties);
  g_test_add_func("/sgf-io/load-setup-and-pl", test_sgf_io_load_setup_and_player_to_play_properties);
#endif
  return g_test_run();
}
