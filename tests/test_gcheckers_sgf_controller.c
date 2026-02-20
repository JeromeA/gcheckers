#include <gtk/gtk.h>

#include "board_view.h"
#include "checkers_model.h"
#include "gcheckers_sgf_controller.h"

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

static void test_gcheckers_sgf_controller_appends_payload(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersSgfController *controller = gcheckers_sgf_controller_new(board_view);
  gcheckers_sgf_controller_set_model(controller, model);

  CheckersMove move;
  g_assert_true(apply_first_move(controller, model, &move));

  SgfTree *tree = gcheckers_sgf_controller_get_tree(controller);
  const SgfNode *node = sgf_tree_get_first_child(tree);
  g_assert_nonnull(node);

  GBytes *payload = sgf_node_get_payload(node);
  g_assert_nonnull(payload);

  gsize size = 0;
  const void *data = g_bytes_get_data(payload, &size);
  g_assert_cmpuint(size, ==, sizeof(CheckersMove));
  g_assert_cmpint(memcmp(data, &move, sizeof(move)), ==, 0);

  g_bytes_unref(payload);
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

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  if (!gtk_init_check()) {
    g_test_add_func("/sgf-controller/appends-payload", test_gcheckers_sgf_controller_skip);
    g_test_add_func("/sgf-controller/replay-branching", test_gcheckers_sgf_controller_skip);
    g_test_add_func("/sgf-controller/new-game", test_gcheckers_sgf_controller_skip);
    g_test_add_func("/sgf-controller/step-ai-move", test_gcheckers_sgf_controller_skip);
    return g_test_run();
  }

  g_test_add_func("/sgf-controller/appends-payload", test_gcheckers_sgf_controller_appends_payload);
  g_test_add_func("/sgf-controller/replay-branching", test_gcheckers_sgf_controller_replay_branching);
  g_test_add_func("/sgf-controller/new-game", test_gcheckers_sgf_controller_new_game_clears_tree);
  g_test_add_func("/sgf-controller/step-ai-move", test_gcheckers_sgf_controller_step_ai_move);
  return g_test_run();
}
