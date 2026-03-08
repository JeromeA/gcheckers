#include <gtk/gtk.h>

#include "board_view.h"
#include "checkers_model.h"
#include "sgf_controller.h"

#include <string.h>

static void test_gcheckers_sgf_controller_skip(void) {
  g_test_skip("GTK display not available.");
}

static gboolean apply_first_move(GCheckersSgfController *controller,
                                 GCheckersModel *model,
                                 CheckersMove *out_move) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(controller), FALSE);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(model), FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  MoveList moves = gcheckers_model_list_moves(model);
  if (moves.count == 0) {
    g_debug("No available moves for SGF controller test\n");
    movelist_free(&moves);
    return FALSE;
  }

  *out_move = moves.moves[0];
  gboolean applied = gcheckers_sgf_controller_apply_move(controller, out_move);
  movelist_free(&moves);
  if (!applied) {
    g_debug("Failed to apply test move through SGF controller\n");
    return FALSE;
  }

  return TRUE;
}

static gboolean apply_first_distinct_move(GCheckersSgfController *controller,
                                          GCheckersModel *model,
                                          const CheckersMove *exclude,
                                          CheckersMove *out_move) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(controller), FALSE);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(model), FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  MoveList moves = gcheckers_model_list_moves(model);
  if (moves.count == 0) {
    g_debug("No available moves for distinct SGF controller test\n");
    movelist_free(&moves);
    return FALSE;
  }

  gboolean found = FALSE;
  for (guint i = 0; i < moves.count; ++i) {
    const CheckersMove *candidate = &moves.moves[i];
    if (exclude && memcmp(candidate, exclude, sizeof(*candidate)) == 0) {
      continue;
    }
    *out_move = *candidate;
    found = TRUE;
    break;
  }

  if (!found) {
    g_debug("No distinct move available for SGF controller branching test\n");
    movelist_free(&moves);
    return FALSE;
  }

  gboolean applied = gcheckers_sgf_controller_apply_move(controller, out_move);
  movelist_free(&moves);
  if (!applied) {
    g_debug("Failed to apply distinct test move through SGF controller\n");
    return FALSE;
  }

  return TRUE;
}

static const SgfNode *sgf_tree_get_first_child(SgfTree *tree) {
  g_return_val_if_fail(SGF_IS_TREE(tree), NULL);

  const SgfNode *root = sgf_tree_get_root(tree);
  g_return_val_if_fail(root != NULL, NULL);

  const GPtrArray *children = sgf_node_get_children(root);
  if (!children || children->len == 0) {
    g_debug("SGF tree root has no children\n");
    return NULL;
  }

  return g_ptr_array_index(children, 0);
}

static const SgfNode *sgf_node_get_nth_child(const SgfNode *node, guint index) {
  g_return_val_if_fail(node != NULL, NULL);

  const GPtrArray *children = sgf_node_get_children(node);
  if (!children || children->len <= index) {
    return NULL;
  }

  return g_ptr_array_index((GPtrArray *)children, index);
}

typedef struct {
  guint count;
  const SgfNode *last_node;
} SgfControllerNodeChangedProbe;

static void test_gcheckers_sgf_controller_on_node_changed(GCheckersSgfController * /*controller*/,
                                                          const SgfNode *node,
                                                          gpointer user_data) {
  SgfControllerNodeChangedProbe *probe = user_data;
  g_return_if_fail(probe != NULL);
  g_return_if_fail(node != NULL);

  probe->count++;
  probe->last_node = node;
}

static void test_gcheckers_sgf_controller_appends_move_property(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersSgfController *controller = gcheckers_sgf_controller_new(board_view);
  gcheckers_sgf_controller_set_model(controller, model);

  CheckersMove move;
  g_assert_true(apply_first_move(controller, model, &move));

  SgfTree *tree = gcheckers_sgf_controller_get_tree(controller);
  const SgfNode *node = sgf_tree_get_first_child(tree);
  g_assert_nonnull(node);

  char expected[128] = {0};
  g_assert_true(game_format_move_notation(&move, expected, sizeof(expected)));

  const char *black = sgf_node_get_property_first(node, "B");
  const char *white = sgf_node_get_property_first(node, "W");
  g_assert_true((black == NULL) != (white == NULL));
  g_assert_cmpstr(black != NULL ? black : white, ==, expected);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_gcheckers_sgf_controller_replay_branching(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersSgfController *controller = gcheckers_sgf_controller_new(board_view);
  gcheckers_sgf_controller_set_model(controller, model);

  CheckersMove move_1;
  CheckersMove move_2;
  g_assert_true(apply_first_move(controller, model, &move_1));
  g_assert_true(apply_first_move(controller, model, &move_2));

  SgfTree *tree = gcheckers_sgf_controller_get_tree(controller);
  const SgfNode *node_1 = sgf_tree_get_first_child(tree);
  g_assert_nonnull(node_1);

  const GPtrArray *children = sgf_node_get_children(node_1);
  g_assert_nonnull(children);
  g_assert_cmpuint(children->len, ==, 1);

  SgfView *view = gcheckers_sgf_controller_get_view(controller);
  g_signal_emit_by_name(view, "node-selected", node_1);

  const GameState *state = gcheckers_model_peek_state(model);
  g_assert_nonnull(state);
  g_assert_cmpuint(state->turn, ==, CHECKERS_COLOR_BLACK);

  CheckersMove branch_move;
  if (!apply_first_distinct_move(controller, model, &move_2, &branch_move)) {
    g_test_skip("No alternative branch move available.");
    g_clear_object(&controller);
    g_clear_object(&model);
    g_clear_object(&board_view);
    return;
  }

  children = sgf_node_get_children(node_1);
  g_assert_nonnull(children);
  g_assert_cmpuint(children->len, >=, 2);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_gcheckers_sgf_controller_new_game_clears_tree(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersSgfController *controller = gcheckers_sgf_controller_new(board_view);
  gcheckers_sgf_controller_set_model(controller, model);

  CheckersMove move;
  g_assert_true(apply_first_move(controller, model, &move));

  gcheckers_sgf_controller_new_game(controller);

  SgfTree *tree = gcheckers_sgf_controller_get_tree(controller);
  const SgfNode *root = sgf_tree_get_root(tree);
  g_assert_nonnull(root);
  const GPtrArray *children = sgf_node_get_children(root);
  g_assert_nonnull(children);
  g_assert_cmpuint(children->len, ==, 0);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_gcheckers_sgf_controller_step_ai_move(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersSgfController *controller = gcheckers_sgf_controller_new(board_view);
  gcheckers_sgf_controller_set_model(controller, model);

  CheckersMove move = {0};
  gboolean applied = gcheckers_sgf_controller_step_ai_move(controller, 4, &move);
  g_assert_true(applied);
  g_assert_cmpuint(move.length, >=, 2);

  SgfTree *tree = gcheckers_sgf_controller_get_tree(controller);
  const SgfNode *node = sgf_tree_get_current(tree);
  g_assert_nonnull(node);
  g_assert_cmpuint(sgf_node_get_move_number(node), ==, 1);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_gcheckers_sgf_controller_navigation_step_and_rewind(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersSgfController *controller = gcheckers_sgf_controller_new(board_view);
  gcheckers_sgf_controller_set_model(controller, model);

  CheckersMove move = {0};
  g_assert_true(apply_first_move(controller, model, &move));
  g_assert_true(apply_first_move(controller, model, &move));

  SgfTree *tree = gcheckers_sgf_controller_get_tree(controller);
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 2);

  g_assert_true(gcheckers_sgf_controller_step_backward(controller));
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 1);

  g_assert_true(gcheckers_sgf_controller_step_forward(controller));
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 2);

  g_assert_true(gcheckers_sgf_controller_rewind_to_start(controller));
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 0);

  const GameState *state = gcheckers_model_peek_state(model);
  g_assert_nonnull(state);
  g_assert_cmpuint(state->turn, ==, CHECKERS_COLOR_WHITE);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_gcheckers_sgf_controller_navigation_forward_to_branch_and_end(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersSgfController *controller = gcheckers_sgf_controller_new(board_view);
  gcheckers_sgf_controller_set_model(controller, model);

  CheckersMove move_1 = {0};
  CheckersMove move_2 = {0};
  g_assert_true(apply_first_move(controller, model, &move_1));
  g_assert_true(apply_first_move(controller, model, &move_2));

  SgfTree *tree = gcheckers_sgf_controller_get_tree(controller);
  const SgfNode *node_1 = sgf_tree_get_first_child(tree);
  g_assert_nonnull(node_1);

  SgfView *view = gcheckers_sgf_controller_get_view(controller);
  g_signal_emit_by_name(view, "node-selected", node_1);

  CheckersMove branch_move = {0};
  if (!apply_first_distinct_move(controller, model, &move_2, &branch_move)) {
    g_test_skip("No alternative branch move available.");
    g_clear_object(&controller);
    g_clear_object(&model);
    g_clear_object(&board_view);
    return;
  }

  const SgfNode *main_child = sgf_node_get_nth_child(node_1, 0);
  g_assert_nonnull(main_child);
  g_signal_emit_by_name(view, "node-selected", main_child);
  g_assert_true(apply_first_move(controller, model, &move_2));

  g_assert_true(gcheckers_sgf_controller_rewind_to_start(controller));
  g_assert_true(gcheckers_sgf_controller_step_forward_to_branch(controller));
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 1);

  g_assert_true(gcheckers_sgf_controller_step_forward_to_end(controller));
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 3);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_gcheckers_sgf_controller_select_node_emits_node_changed(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersSgfController *controller = gcheckers_sgf_controller_new(board_view);
  gcheckers_sgf_controller_set_model(controller, model);

  CheckersMove move = {0};
  g_assert_true(apply_first_move(controller, model, &move));
  g_assert_true(apply_first_move(controller, model, &move));

  SgfTree *tree = gcheckers_sgf_controller_get_tree(controller);
  g_assert_nonnull(tree);
  const SgfNode *root = sgf_tree_get_root(tree);
  g_assert_nonnull(root);

  SgfControllerNodeChangedProbe probe = {0};
  g_signal_connect(controller,
                   "node-changed",
                   G_CALLBACK(test_gcheckers_sgf_controller_on_node_changed),
                   &probe);

  g_assert_true(gcheckers_sgf_controller_select_node(controller, root));
  g_assert_cmpuint(probe.count, ==, 1);
  g_assert_true(probe.last_node == root);
  g_assert_true(sgf_tree_get_current(tree) == root);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  if (!gtk_init_check()) {
    g_test_add_func("/sgf-controller/appends-move-property", test_gcheckers_sgf_controller_skip);
    g_test_add_func("/sgf-controller/replay-branching", test_gcheckers_sgf_controller_skip);
    g_test_add_func("/sgf-controller/new-game", test_gcheckers_sgf_controller_skip);
    g_test_add_func("/sgf-controller/step-ai-move", test_gcheckers_sgf_controller_skip);
    g_test_add_func("/sgf-controller/navigation-step-and-rewind", test_gcheckers_sgf_controller_skip);
    g_test_add_func("/sgf-controller/navigation-forward-to-branch-and-end", test_gcheckers_sgf_controller_skip);
    g_test_add_func("/sgf-controller/select-node-emits-node-changed", test_gcheckers_sgf_controller_skip);
    return g_test_run();
  }

  g_test_add_func("/sgf-controller/appends-move-property", test_gcheckers_sgf_controller_appends_move_property);
  g_test_add_func("/sgf-controller/replay-branching", test_gcheckers_sgf_controller_replay_branching);
  g_test_add_func("/sgf-controller/new-game", test_gcheckers_sgf_controller_new_game_clears_tree);
  g_test_add_func("/sgf-controller/step-ai-move", test_gcheckers_sgf_controller_step_ai_move);
  g_test_add_func("/sgf-controller/navigation-step-and-rewind",
                  test_gcheckers_sgf_controller_navigation_step_and_rewind);
  g_test_add_func("/sgf-controller/navigation-forward-to-branch-and-end",
                  test_gcheckers_sgf_controller_navigation_forward_to_branch_and_end);
  g_test_add_func("/sgf-controller/select-node-emits-node-changed",
                  test_gcheckers_sgf_controller_select_node_emits_node_changed);
  return g_test_run();
}
