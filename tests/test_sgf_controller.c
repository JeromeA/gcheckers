#include <gtk/gtk.h>

#include "game_model.h"
#include "active_game_backend.h"
#include "board_view.h"
#if defined(GGAME_GAME_BOOP)
#include "games/boop/boop_game.h"
#include "games/boop/boop_types.h"
#endif
#include "games/checkers/checkers_model.h"
#include "games/checkers/rulesets.h"
#include "sgf_controller.h"
#include "sgf_io.h"
#include "sgf_move_props.h"

#include <glib/gstdio.h>
#include <string.h>

static void test_ggame_sgf_controller_skip(void) {
  g_test_skip("GTK display not available.");
}

static gboolean apply_first_move(GGameSgfController *controller,
                                 GCheckersModel *model,
                                 CheckersMove *out_move) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(controller), FALSE);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(model), FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  MoveList moves = gcheckers_model_list_moves(model);
  if (moves.count == 0) {
    g_debug("No available moves for SGF controller test\n");
    movelist_free(&moves);
    return FALSE;
  }

  *out_move = moves.moves[0];
  gboolean applied = ggame_sgf_controller_apply_move(controller, out_move);
  movelist_free(&moves);
  if (!applied) {
    g_debug("Failed to apply test move through SGF controller\n");
    return FALSE;
  }

  return TRUE;
}

static gboolean apply_first_distinct_move(GGameSgfController *controller,
                                          GCheckersModel *model,
                                          const CheckersMove *exclude,
                                          CheckersMove *out_move) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(controller), FALSE);
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

  gboolean applied = ggame_sgf_controller_apply_move(controller, out_move);
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

static int sgf_view_count_children(GtkWidget *parent) {
  int count = 0;
  GtkWidget *child = gtk_widget_get_first_child(parent);

  while (child != NULL) {
    count++;
    child = gtk_widget_get_next_sibling(child);
  }

  return count;
}

static GtkWidget *sgf_view_get_overlay(GtkWidget *root) {
  GtkWidget *child = NULL;

  g_return_val_if_fail(GTK_IS_SCROLLED_WINDOW(root), NULL);

  child = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(root));
  if (child == NULL) {
    g_debug("SGF view scrolled window had no child");
    return NULL;
  }

  if (GTK_IS_OVERLAY(child)) {
    return child;
  }

  if (GTK_IS_VIEWPORT(child)) {
    GtkWidget *viewport_child = gtk_viewport_get_child(GTK_VIEWPORT(child));
    if (GTK_IS_OVERLAY(viewport_child)) {
      return viewport_child;
    }
  }

  g_debug("Unexpected SGF view child type %s", G_OBJECT_TYPE_NAME(child));
  return NULL;
}

static GtkWidget *sgf_view_find_grid(GtkWidget *overlay) {
  GtkWidget *child = NULL;

  g_return_val_if_fail(GTK_IS_OVERLAY(overlay), NULL);

  child = gtk_widget_get_first_child(overlay);
  while (child != NULL) {
    if (GTK_IS_GRID(child)) {
      return child;
    }

    child = gtk_widget_get_next_sibling(child);
  }

  return NULL;
}

static int sgf_view_count_discs(SgfView *view) {
  GtkWidget *root = NULL;
  GtkWidget *overlay = NULL;
  GtkWidget *grid = NULL;

  g_return_val_if_fail(SGF_IS_VIEW(view), -1);

  root = sgf_view_get_widget(view);
  g_return_val_if_fail(GTK_IS_WIDGET(root), -1);

  overlay = sgf_view_get_overlay(root);
  g_return_val_if_fail(GTK_IS_WIDGET(overlay), -1);

  grid = sgf_view_find_grid(overlay);
  g_return_val_if_fail(GTK_IS_WIDGET(grid), -1);

  return sgf_view_count_children(grid);
}

typedef struct {
  guint count;
  const SgfNode *last_node;
} SgfControllerNodeChangedProbe;

static void test_ggame_sgf_controller_on_node_changed(GGameSgfController * /*controller*/,
                                                          const SgfNode *node,
                                                          gpointer user_data) {
  SgfControllerNodeChangedProbe *probe = user_data;
  g_return_if_fail(probe != NULL);
  g_return_if_fail(node != NULL);

  probe->count++;
  probe->last_node = node;
}

static void test_ggame_sgf_controller_appends_move_property(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  SgfView *view = NULL;
  ggame_sgf_controller_set_model(controller, model);

  CheckersMove move;
  g_assert_true(apply_first_move(controller, model, &move));
  view = ggame_sgf_controller_get_view(controller);
  g_assert_cmpint(sgf_view_count_discs(view), ==, 2);

  SgfTree *tree = ggame_sgf_controller_get_tree(controller);
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

static void test_ggame_sgf_controller_replay_branching(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_model(controller, model);

  CheckersMove move_1;
  CheckersMove move_2;
  g_assert_true(apply_first_move(controller, model, &move_1));
  g_assert_true(apply_first_move(controller, model, &move_2));

  SgfTree *tree = ggame_sgf_controller_get_tree(controller);
  const SgfNode *node_1 = sgf_tree_get_first_child(tree);
  g_assert_nonnull(node_1);

  const GPtrArray *children = sgf_node_get_children(node_1);
  g_assert_nonnull(children);
  g_assert_cmpuint(children->len, ==, 1);

  SgfView *view = ggame_sgf_controller_get_view(controller);
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

static void test_ggame_sgf_controller_new_game_clears_tree(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_model(controller, model);

  CheckersMove move;
  g_assert_true(apply_first_move(controller, model, &move));

  ggame_sgf_controller_new_game(controller);

  SgfTree *tree = ggame_sgf_controller_get_tree(controller);
  const SgfNode *root = sgf_tree_get_root(tree);
  g_assert_nonnull(root);
  const GPtrArray *children = sgf_node_get_children(root);
  g_assert_nonnull(children);
  g_assert_cmpuint(children->len, ==, 0);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void on_manual_requested_count(GGameSgfController * /*controller*/,
                                      const SgfNode * /*node*/,
                                      gpointer user_data) {
  guint *count = user_data;

  g_return_if_fail(count != NULL);

  (*count)++;
}

static void test_ggame_sgf_controller_new_game_does_not_request_manual_mode(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_model(controller, model);

  guint manual_requested_count = 0;
  g_signal_connect(controller, "manual-requested", G_CALLBACK(on_manual_requested_count), &manual_requested_count);

  CheckersMove move;
  g_assert_true(apply_first_move(controller, model, &move));

  ggame_sgf_controller_new_game(controller);

  g_assert_cmpuint(manual_requested_count, ==, 0);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_ggame_sgf_controller_step_ai_move(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_model(controller, model);

  CheckersMove move = {0};
  gboolean applied = ggame_sgf_controller_step_ai_move(controller, 4, &move);
  g_assert_true(applied);
  g_assert_cmpuint(move.length, >=, 2);

  SgfTree *tree = ggame_sgf_controller_get_tree(controller);
  const SgfNode *node = sgf_tree_get_current(tree);
  g_assert_nonnull(node);
  g_assert_cmpuint(sgf_node_get_move_number(node), ==, 1);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_ggame_sgf_controller_navigation_step_and_rewind(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_model(controller, model);

  CheckersMove move = {0};
  g_assert_true(apply_first_move(controller, model, &move));
  g_assert_true(apply_first_move(controller, model, &move));

  SgfTree *tree = ggame_sgf_controller_get_tree(controller);
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 2);

  g_assert_true(ggame_sgf_controller_step_backward(controller));
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 1);

  g_assert_true(ggame_sgf_controller_step_forward(controller));
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 2);

  g_assert_true(ggame_sgf_controller_rewind_to_start(controller));
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 0);

  const GameState *state = gcheckers_model_peek_state(model);
  g_assert_nonnull(state);
  g_assert_cmpuint(state->turn, ==, CHECKERS_COLOR_WHITE);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_ggame_sgf_controller_current_node_move_accessor(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_model(controller, model);
  board_view_set_sgf_controller(board_view, controller);

  CheckersMove move = {0};
  g_assert_false(ggame_sgf_controller_get_current_node_move(controller, &move));

  CheckersMove first_move = {0};
  g_assert_true(apply_first_move(controller, model, &first_move));
  g_assert_true(ggame_sgf_controller_get_current_node_move(controller, &move));
  g_assert_true(memcmp(&move, &first_move, sizeof(move)) == 0);

  g_assert_true(ggame_sgf_controller_rewind_to_start(controller));
  g_assert_false(ggame_sgf_controller_get_current_node_move(controller, &move));

  g_assert_true(ggame_sgf_controller_step_forward(controller));
  g_assert_true(ggame_sgf_controller_get_current_node_move(controller, &move));
  g_assert_true(memcmp(&move, &first_move, sizeof(move)) == 0);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_ggame_sgf_controller_navigation_forward_to_branch_and_end(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_model(controller, model);

  CheckersMove move_1 = {0};
  CheckersMove move_2 = {0};
  g_assert_true(apply_first_move(controller, model, &move_1));
  g_assert_true(apply_first_move(controller, model, &move_2));

  SgfTree *tree = ggame_sgf_controller_get_tree(controller);
  const SgfNode *node_1 = sgf_tree_get_first_child(tree);
  g_assert_nonnull(node_1);

  SgfView *view = ggame_sgf_controller_get_view(controller);
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

  g_assert_true(ggame_sgf_controller_rewind_to_start(controller));
  g_assert_true(ggame_sgf_controller_step_forward_to_branch(controller));
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 1);

  g_assert_true(ggame_sgf_controller_step_forward_to_end(controller));
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 3);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_ggame_sgf_controller_select_node_emits_node_changed(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_model(controller, model);

  CheckersMove move = {0};
  g_assert_true(apply_first_move(controller, model, &move));
  g_assert_true(apply_first_move(controller, model, &move));

  SgfTree *tree = ggame_sgf_controller_get_tree(controller);
  g_assert_nonnull(tree);
  const SgfNode *root = sgf_tree_get_root(tree);
  g_assert_nonnull(root);

  SgfControllerNodeChangedProbe probe = {0};
  g_signal_connect(controller,
                   "node-changed",
                   G_CALLBACK(test_ggame_sgf_controller_on_node_changed),
                   &probe);

  g_assert_true(ggame_sgf_controller_select_node(controller, root));
  g_assert_cmpuint(probe.count, ==, 1);
  g_assert_true(probe.last_node == root);
  g_assert_true(sgf_tree_get_current(tree) == root);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_ggame_sgf_controller_load_applies_setup_properties(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_model(controller, model);

  const char *content =
      "(;FF[4]CA[UTF-8]AP[gcheckers]GM[40]RU[international]AE[af]AB[af]ABK[af]PL[B];AE[af]AW[af]AWK[af]PL[W])";
  gchar *path = g_strdup_printf("%s/test-gcheckers-setup-%u.sgf", g_get_tmp_dir(), g_random_int());
  g_assert_nonnull(path);
  g_assert_true(g_file_set_contents(path, content, -1, NULL));

  g_autoptr(GError) error = NULL;
  g_assert_true(ggame_sgf_controller_load_file(controller, path, &error));
  g_assert_no_error(error);

  const GameState *state = gcheckers_model_peek_state(model);
  g_assert_nonnull(state);
  gint8 setup_index = board_index_from_coord(5, 0, state->board.board_size);
  g_assert_cmpint(setup_index, >=, 0);
  g_assert_cmpuint(state->turn, ==, CHECKERS_COLOR_BLACK);
  g_assert_cmpint(board_get(&state->board, (guint8)setup_index), ==, CHECKERS_PIECE_BLACK_KING);

  g_assert_true(ggame_sgf_controller_step_forward(controller));
  state = gcheckers_model_peek_state(model);
  g_assert_nonnull(state);
  g_assert_cmpuint(state->turn, ==, CHECKERS_COLOR_WHITE);
  g_assert_cmpint(board_get(&state->board, (guint8)setup_index), ==, CHECKERS_PIECE_WHITE_KING);

  g_remove(path);
  g_free(path);
  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_ggame_sgf_controller_load_file_requires_ru(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_model(controller, model);

  gchar *path = g_strdup_printf("%s/test-gcheckers-load-missing-ru-%u.sgf", g_get_tmp_dir(), g_random_int());
  g_assert_nonnull(path);
  g_assert_true(g_file_set_contents(path, "(;FF[4]CA[UTF-8]AP[gcheckers]GM[40];B[12-16])", -1, NULL));

  g_autoptr(GError) error = NULL;
  g_assert_false(ggame_sgf_controller_load_file(controller, path, &error));
  g_assert_error(error, g_quark_from_static_string("sgf-io-error"), 23);

  g_remove(path);
  g_free(path);
  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_ggame_sgf_controller_save_position_file(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_model(controller, model);

  Game game = {0};
  g_assert_true(gcheckers_model_copy_game(model, &game));
  guint8 squares = board_playable_squares(game.state.board.board_size);
  for (guint8 i = 0; i < squares; ++i) {
    board_set(&game.state.board, i, CHECKERS_PIECE_EMPTY);
  }

  gint8 black_king_square_i = board_index_from_coord(1, 2, game.state.board.board_size);
  gint8 black_man_square_i = board_index_from_coord(3, 0, game.state.board.board_size);
  gint8 white_king_square_i = board_index_from_coord(4, 3, game.state.board.board_size);
  gint8 white_man_square_i = board_index_from_coord(6, 1, game.state.board.board_size);
  g_assert_cmpint(black_king_square_i, >=, 0);
  g_assert_cmpint(black_man_square_i, >=, 0);
  g_assert_cmpint(white_king_square_i, >=, 0);
  g_assert_cmpint(white_man_square_i, >=, 0);
  guint8 black_king_square = (guint8)black_king_square_i;
  guint8 black_man_square = (guint8)black_man_square_i;
  guint8 white_king_square = (guint8)white_king_square_i;
  guint8 white_man_square = (guint8)white_man_square_i;
  board_set(&game.state.board, black_king_square, CHECKERS_PIECE_BLACK_KING);
  board_set(&game.state.board, black_man_square, CHECKERS_PIECE_BLACK_MAN);
  board_set(&game.state.board, white_king_square, CHECKERS_PIECE_WHITE_KING);
  board_set(&game.state.board, white_man_square, CHECKERS_PIECE_WHITE_MAN);
  game.state.turn = CHECKERS_COLOR_BLACK;
  game.state.winner = CHECKERS_WINNER_NONE;
  g_assert_true(gcheckers_model_set_state(model, &game.state));

  gchar *path = g_strdup_printf("%s/test-gcheckers-save-position-%u.sgf", g_get_tmp_dir(), g_random_int());
  g_assert_nonnull(path);

  g_autoptr(GError) error = NULL;
  g_assert_true(ggame_sgf_controller_save_position_file(controller, path, &error));
  g_assert_no_error(error);

  g_autofree char *saved = NULL;
  gsize saved_len = 0;
  g_assert_true(g_file_get_contents(path, &saved, &saved_len, &error));
  g_assert_no_error(error);
  g_assert_true(saved_len > 0);
  g_assert_nonnull(strstr(saved, "RU[american]"));
  g_assert_nonnull(strstr(saved, "PL[B]"));
  g_assert_nonnull(strstr(saved, "ABK["));
  g_assert_nonnull(strstr(saved, "AWK["));
  g_assert_null(strstr(saved, ";B["));
  g_assert_null(strstr(saved, ";W["));
  const char *ae_pos = strstr(saved, "AE[");
  const char *ab_pos = strstr(saved, "AB[");
  g_assert_nonnull(ae_pos);
  g_assert_nonnull(ab_pos);
  g_assert_cmpint((gint)(ae_pos - saved), <, (gint)(ab_pos - saved));

  g_autoptr(SgfTree) loaded = NULL;
  g_assert_true(sgf_io_load_data(saved, &loaded, &error));
  g_assert_no_error(error);
  g_assert_nonnull(loaded);
  const SgfNode *root = sgf_tree_get_root(loaded);
  g_assert_nonnull(root);
  const GPtrArray *children = sgf_node_get_children(root);
  g_assert_nonnull(children);
  g_assert_cmpuint(children->len, ==, 0);
  g_assert_nonnull(sgf_node_get_property_values(root, "AB"));
  g_assert_nonnull(sgf_node_get_property_values(root, "AW"));

  gcheckers_model_reset(model);
  g_assert_true(ggame_sgf_controller_load_file(controller, path, &error));
  g_assert_no_error(error);
  const GameState *loaded_state = gcheckers_model_peek_state(model);
  g_assert_nonnull(loaded_state);
  g_assert_cmpuint(loaded_state->turn, ==, CHECKERS_COLOR_BLACK);
  g_assert_cmpint(board_get(&loaded_state->board, black_king_square), ==, CHECKERS_PIECE_BLACK_KING);
  g_assert_cmpint(board_get(&loaded_state->board, black_man_square), ==, CHECKERS_PIECE_BLACK_MAN);
  g_assert_cmpint(board_get(&loaded_state->board, white_king_square), ==, CHECKERS_PIECE_WHITE_KING);
  g_assert_cmpint(board_get(&loaded_state->board, white_man_square), ==, CHECKERS_PIECE_WHITE_MAN);

  g_remove(path);
  g_free(path);
  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_ggame_sgf_controller_load_file_applies_ruleset_from_ru(void) {
  BoardView *board_view = board_view_new();
  GCheckersModel *model = gcheckers_model_new();
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_model(controller, model);

  gchar *path = g_strdup_printf("%s/test-gcheckers-load-ruleset-%u.sgf", g_get_tmp_dir(), g_random_int());
  g_assert_nonnull(path);

  const char *content = "(;FF[4]CA[UTF-8]AP[gcheckers]GM[40]RU[american]AE[1:32]AW[1]AB[2]PL[B])";
  g_autoptr(GError) error = NULL;
  g_assert_true(g_file_set_contents(path, content, -1, &error));
  g_assert_no_error(error);

  g_assert_true(ggame_sgf_controller_load_file(controller, path, &error));
  g_assert_no_error(error);

  const CheckersRules *rules = gcheckers_model_peek_rules(model);
  g_assert_nonnull(rules);
  g_assert_cmpuint(rules->board_size, ==, 8);

  g_remove(path);
  g_free(path);
  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_ggame_sgf_controller_replay_node_into_game_applies_setup_root(void) {
  g_autoptr(SgfTree) tree = sgf_tree_new();
  g_assert_nonnull(tree);

  SgfNode *root = (SgfNode *)sgf_tree_get_root(tree);
  g_assert_nonnull(root);
  g_assert_true(sgf_node_add_property(root, "AE", "1:50"));
  g_assert_true(sgf_node_add_property(root, "AW", "31"));
  g_assert_true(sgf_node_add_property(root, "AWK", "31"));
  g_assert_true(sgf_node_add_property(root, "AB", "8"));
  g_assert_true(sgf_node_add_property(root, "ABK", "8"));
  g_assert_true(sgf_node_add_property(root, "PL", "B"));

  SgfNode *child = (SgfNode *)sgf_tree_append_node(tree);
  g_assert_nonnull(child);
  CheckersMove move = {0};
  move.length = 2;
  move.path[0] = 7;
  move.path[1] = 11;
  g_autoptr(GError) move_error = NULL;
  g_assert_true(sgf_move_props_set_move(child, SGF_COLOR_BLACK, &move, &move_error));
  g_assert_no_error(move_error);

  Game game = {0};
  game_init_with_rules(&game, checkers_ruleset_get_rules(PLAYER_RULESET_INTERNATIONAL));
  g_autoptr(GError) replay_error = NULL;
  g_assert_true(ggame_sgf_controller_replay_node_into_game((const SgfNode *)child, &game, &replay_error));
  g_assert_no_error(replay_error);

  g_assert_cmpuint(game.state.turn, ==, CHECKERS_COLOR_WHITE);
  g_assert_cmpint(board_get(&game.state.board, 7), ==, CHECKERS_PIECE_EMPTY);
  g_assert_cmpint(board_get(&game.state.board, 11), ==, CHECKERS_PIECE_BLACK_KING);
  g_assert_cmpint(board_get(&game.state.board, 30), ==, CHECKERS_PIECE_WHITE_KING);

  game_destroy(&game);
}

#if defined(GGAME_GAME_BOOP)
static gboolean boop_moves_match(const BoopMove *left, const BoopMove *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  if (!boop_moves_equal(left, right) || left->path_length != right->path_length) {
    return FALSE;
  }

  return memcmp(left->path, right->path, left->path_length * sizeof(left->path[0])) == 0;
}

static gboolean boop_apply_first_move(GGameSgfController *controller, GGameModel *model, BoopMove *out_move) {
  const GameBackend *backend = NULL;
  GameBackendMoveList moves = {0};
  const BoopMove *move = NULL;
  gboolean applied = FALSE;

  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(controller), FALSE);
  g_return_val_if_fail(GGAME_IS_MODEL(model), FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  backend = ggame_model_peek_backend(model);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(backend->move_list_get != NULL, FALSE);
  g_return_val_if_fail(backend->move_list_free != NULL, FALSE);

  moves = ggame_model_list_moves(model);
  if (moves.count == 0) {
    g_debug("No available boop moves for SGF controller test");
    return FALSE;
  }

  move = backend->move_list_get(&moves, 0);
  g_return_val_if_fail(move != NULL, FALSE);

  *out_move = *move;
  applied = ggame_sgf_controller_apply_move(controller, out_move);
  backend->move_list_free(&moves);
  if (!applied) {
    g_debug("Failed to apply boop test move through SGF controller");
    return FALSE;
  }

  return TRUE;
}

static gboolean boop_apply_first_distinct_move(GGameSgfController *controller,
                                               GGameModel *model,
                                               const BoopMove *exclude,
                                               BoopMove *out_move) {
  const GameBackend *backend = NULL;
  GameBackendMoveList moves = {0};
  gboolean found = FALSE;
  gboolean applied = FALSE;

  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(controller), FALSE);
  g_return_val_if_fail(GGAME_IS_MODEL(model), FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  backend = ggame_model_peek_backend(model);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(backend->move_list_get != NULL, FALSE);
  g_return_val_if_fail(backend->move_list_free != NULL, FALSE);

  moves = ggame_model_list_moves(model);
  for (gsize i = 0; i < moves.count; ++i) {
    const BoopMove *candidate = backend->move_list_get(&moves, i);

    g_return_val_if_fail(candidate != NULL, FALSE);
    if (exclude != NULL && boop_moves_match(candidate, exclude)) {
      continue;
    }

    *out_move = *candidate;
    found = TRUE;
    break;
  }

  if (!found) {
    backend->move_list_free(&moves);
    g_debug("No distinct boop move available for SGF controller test");
    return FALSE;
  }

  applied = ggame_sgf_controller_apply_move(controller, out_move);
  backend->move_list_free(&moves);
  if (!applied) {
    g_debug("Failed to apply distinct boop test move through SGF controller");
    return FALSE;
  }

  return TRUE;
}

static void test_ggame_sgf_controller_boop_appends_move_property(void) {
  BoardView *board_view = board_view_new();
  GGameModel *model = ggame_model_new(GGAME_ACTIVE_GAME_BACKEND);
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  SgfView *view = NULL;
  ggame_sgf_controller_set_game_model(controller, model);

  BoopMove move = {0};
  g_assert_true(boop_apply_first_move(controller, model, &move));
  view = ggame_sgf_controller_get_view(controller);
  g_assert_cmpint(sgf_view_count_discs(view), ==, 2);

  SgfTree *tree = ggame_sgf_controller_get_tree(controller);
  const SgfNode *node = sgf_tree_get_first_child(tree);
  g_assert_nonnull(node);

  char expected[128] = {0};
  g_assert_true(boop_move_format(&move, expected, sizeof(expected)));
  g_assert_cmpstr(sgf_node_get_property_first(node, "B"), ==, expected);
  g_assert_null(sgf_node_get_property_first(node, "W"));

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_ggame_sgf_controller_boop_replay_branching(void) {
  BoardView *board_view = board_view_new();
  GGameModel *model = ggame_model_new(GGAME_ACTIVE_GAME_BACKEND);
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_game_model(controller, model);

  BoopMove move_1 = {0};
  BoopMove move_2 = {0};
  BoopMove branch_move = {0};
  g_assert_true(boop_apply_first_move(controller, model, &move_1));
  g_assert_true(boop_apply_first_move(controller, model, &move_2));

  SgfTree *tree = ggame_sgf_controller_get_tree(controller);
  const SgfNode *node_1 = sgf_tree_get_first_child(tree);
  g_assert_nonnull(node_1);

  SgfView *view = ggame_sgf_controller_get_view(controller);
  g_signal_emit_by_name(view, "node-selected", node_1);
  g_assert_true(boop_apply_first_distinct_move(controller, model, &move_2, &branch_move));

  const GPtrArray *children = sgf_node_get_children(node_1);
  g_assert_nonnull(children);
  g_assert_cmpuint(children->len, >=, 2);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_ggame_sgf_controller_boop_new_game_clears_tree(void) {
  BoardView *board_view = board_view_new();
  GGameModel *model = ggame_model_new(GGAME_ACTIVE_GAME_BACKEND);
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_game_model(controller, model);

  BoopMove move = {0};
  g_assert_true(boop_apply_first_move(controller, model, &move));

  ggame_model_reset(model, NULL);
  ggame_sgf_controller_new_game(controller);

  SgfTree *tree = ggame_sgf_controller_get_tree(controller);
  const SgfNode *root = sgf_tree_get_root(tree);
  const GPtrArray *children = sgf_node_get_children(root);
  g_assert_nonnull(root);
  g_assert_nonnull(children);
  g_assert_cmpuint(children->len, ==, 0);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_ggame_sgf_controller_boop_step_ai_move(void) {
  BoardView *board_view = board_view_new();
  GGameModel *model = ggame_model_new(GGAME_ACTIVE_GAME_BACKEND);
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_game_model(controller, model);

  BoopMove move = {0};
  g_assert_true(ggame_sgf_controller_step_ai_move(controller, 2, &move));
  g_assert_cmpuint(move.rank, >=, BOOP_PIECE_RANK_KITTEN);
  g_assert_cmpuint(move.rank, <=, BOOP_PIECE_RANK_CAT);
  g_assert_cmpuint(move.square, <, BOOP_SQUARE_COUNT);

  SgfTree *tree = ggame_sgf_controller_get_tree(controller);
  const SgfNode *node = sgf_tree_get_current(tree);
  g_assert_nonnull(node);
  g_assert_cmpuint(sgf_node_get_move_number(node), ==, 1);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_ggame_sgf_controller_boop_navigation_step_and_rewind(void) {
  BoardView *board_view = board_view_new();
  GGameModel *model = ggame_model_new(GGAME_ACTIVE_GAME_BACKEND);
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_game_model(controller, model);

  BoopMove move = {0};
  g_assert_true(boop_apply_first_move(controller, model, &move));
  g_assert_true(boop_apply_first_move(controller, model, &move));

  SgfTree *tree = ggame_sgf_controller_get_tree(controller);
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 2);

  g_assert_true(ggame_sgf_controller_step_backward(controller));
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 1);

  const BoopPosition *position = ggame_model_peek_position(model);
  g_assert_nonnull(position);
  g_assert_cmpuint(position->turn, ==, 1);

  g_assert_true(ggame_sgf_controller_step_forward(controller));
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 2);

  g_assert_true(ggame_sgf_controller_rewind_to_start(controller));
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 0);

  position = ggame_model_peek_position(model);
  g_assert_nonnull(position);
  g_assert_cmpuint(position->turn, ==, 0);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_ggame_sgf_controller_boop_current_node_move_accessor(void) {
  BoardView *board_view = board_view_new();
  GGameModel *model = ggame_model_new(GGAME_ACTIVE_GAME_BACKEND);
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_game_model(controller, model);
  board_view_set_sgf_controller(board_view, controller);

  BoopMove move = {0};
  BoopMove first_move = {0};
  g_assert_false(ggame_sgf_controller_get_current_node_move(controller, &move));

  g_assert_true(boop_apply_first_move(controller, model, &first_move));
  g_assert_true(ggame_sgf_controller_get_current_node_move(controller, &move));
  g_assert_true(boop_moves_match(&move, &first_move));

  g_assert_true(ggame_sgf_controller_rewind_to_start(controller));
  g_assert_false(ggame_sgf_controller_get_current_node_move(controller, &move));

  g_assert_true(ggame_sgf_controller_step_forward(controller));
  g_assert_true(ggame_sgf_controller_get_current_node_move(controller, &move));
  g_assert_true(boop_moves_match(&move, &first_move));

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_ggame_sgf_controller_boop_navigation_forward_to_branch_and_end(void) {
  BoardView *board_view = board_view_new();
  GGameModel *model = ggame_model_new(GGAME_ACTIVE_GAME_BACKEND);
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_game_model(controller, model);

  BoopMove move_1 = {0};
  BoopMove move_2 = {0};
  BoopMove branch_move = {0};
  g_assert_true(boop_apply_first_move(controller, model, &move_1));
  g_assert_true(boop_apply_first_move(controller, model, &move_2));

  SgfTree *tree = ggame_sgf_controller_get_tree(controller);
  const SgfNode *node_1 = sgf_tree_get_first_child(tree);
  const SgfNode *main_child = NULL;
  SgfView *view = ggame_sgf_controller_get_view(controller);
  g_assert_nonnull(node_1);

  g_signal_emit_by_name(view, "node-selected", node_1);
  g_assert_true(boop_apply_first_distinct_move(controller, model, &move_2, &branch_move));

  main_child = sgf_node_get_nth_child(node_1, 0);
  g_assert_nonnull(main_child);
  g_signal_emit_by_name(view, "node-selected", main_child);
  g_assert_true(boop_apply_first_move(controller, model, &move_2));

  g_assert_true(ggame_sgf_controller_rewind_to_start(controller));
  g_assert_true(ggame_sgf_controller_step_forward_to_branch(controller));
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 1);

  g_assert_true(ggame_sgf_controller_step_forward_to_end(controller));
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 3);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_ggame_sgf_controller_boop_select_node_emits_node_changed(void) {
  BoardView *board_view = board_view_new();
  GGameModel *model = ggame_model_new(GGAME_ACTIVE_GAME_BACKEND);
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_game_model(controller, model);

  BoopMove move = {0};
  g_assert_true(boop_apply_first_move(controller, model, &move));
  g_assert_true(boop_apply_first_move(controller, model, &move));

  SgfTree *tree = ggame_sgf_controller_get_tree(controller);
  const SgfNode *root = sgf_tree_get_root(tree);
  SgfControllerNodeChangedProbe probe = {0};
  g_assert_nonnull(root);

  g_signal_connect(controller,
                   "node-changed",
                   G_CALLBACK(test_ggame_sgf_controller_on_node_changed),
                   &probe);

  g_assert_true(ggame_sgf_controller_select_node(controller, root));
  g_assert_cmpuint(probe.count, ==, 1);
  g_assert_true(probe.last_node == root);
  g_assert_true(sgf_tree_get_current(tree) == root);

  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}

static void test_ggame_sgf_controller_boop_load_file_without_ru(void) {
  BoardView *board_view = board_view_new();
  GGameModel *model = ggame_model_new(GGAME_ACTIVE_GAME_BACKEND);
  GGameSgfController *controller = ggame_sgf_controller_new(board_view);
  ggame_sgf_controller_set_game_model(controller, model);

  gchar *path = g_strdup_printf("%s/test-gboop-load-%u.sgf", g_get_tmp_dir(), g_random_int());
  g_assert_nonnull(path);
  g_assert_true(g_file_set_contents(path, "(;FF[4]CA[UTF-8]AP[gcheckers]GM[40];B[K@a1])", -1, NULL));

  g_autoptr(GError) error = NULL;
  g_assert_true(ggame_sgf_controller_load_file(controller, path, &error));
  g_assert_no_error(error);

  const BoopPosition *position = ggame_model_peek_position(model);
  g_assert_nonnull(position);
  g_assert_cmpuint(position->turn, ==, 1);
  g_assert_cmpuint(position->board[0].rank, ==, BOOP_PIECE_RANK_KITTEN);
  g_assert_cmpuint(position->board[0].side, ==, 0);

  g_remove(path);
  g_free(path);
  g_clear_object(&controller);
  g_clear_object(&model);
  g_clear_object(&board_view);
}
#endif

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
#if defined(GGAME_GAME_BOOP)
  if (!gtk_init_check()) {
    g_test_add_func("/sgf-controller/appends-move-property", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/replay-branching", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/new-game", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/step-ai-move", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/navigation-step-and-rewind", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/current-node-move", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/navigation-forward-to-branch-and-end", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/select-node-emits-node-changed", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/load-file-without-ru", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/load-applies-setup-properties", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/load-file-requires-ru", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/replay-node-into-game-applies-setup-root", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/save-position-file", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/load-file-applies-ruleset-from-ru", test_ggame_sgf_controller_skip);
    return g_test_run();
  }

  g_test_add_func("/sgf-controller/appends-move-property",
                  test_ggame_sgf_controller_boop_appends_move_property);
  g_test_add_func("/sgf-controller/replay-branching", test_ggame_sgf_controller_boop_replay_branching);
  g_test_add_func("/sgf-controller/new-game", test_ggame_sgf_controller_boop_new_game_clears_tree);
  g_test_add_func("/sgf-controller/step-ai-move", test_ggame_sgf_controller_boop_step_ai_move);
  g_test_add_func("/sgf-controller/navigation-step-and-rewind",
                  test_ggame_sgf_controller_boop_navigation_step_and_rewind);
  g_test_add_func("/sgf-controller/current-node-move",
                  test_ggame_sgf_controller_boop_current_node_move_accessor);
  g_test_add_func("/sgf-controller/navigation-forward-to-branch-and-end",
                  test_ggame_sgf_controller_boop_navigation_forward_to_branch_and_end);
  g_test_add_func("/sgf-controller/select-node-emits-node-changed",
                  test_ggame_sgf_controller_boop_select_node_emits_node_changed);
  g_test_add_func("/sgf-controller/load-file-without-ru",
                  test_ggame_sgf_controller_boop_load_file_without_ru);
  g_test_add_func("/sgf-controller/load-applies-setup-properties", test_ggame_sgf_controller_skip);
  g_test_add_func("/sgf-controller/load-file-requires-ru", test_ggame_sgf_controller_skip);
  g_test_add_func("/sgf-controller/replay-node-into-game-applies-setup-root", test_ggame_sgf_controller_skip);
  g_test_add_func("/sgf-controller/save-position-file", test_ggame_sgf_controller_skip);
  g_test_add_func("/sgf-controller/load-file-applies-ruleset-from-ru", test_ggame_sgf_controller_skip);
  return g_test_run();
#endif
  if (!gtk_init_check()) {
    g_test_add_func("/sgf-controller/appends-move-property", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/replay-branching", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/new-game", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/step-ai-move", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/navigation-step-and-rewind", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/current-node-move", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/navigation-forward-to-branch-and-end", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/select-node-emits-node-changed", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/load-applies-setup-properties", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/load-file-requires-ru", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/replay-node-into-game-applies-setup-root", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/save-position-file", test_ggame_sgf_controller_skip);
    g_test_add_func("/sgf-controller/load-file-applies-ruleset-from-ru", test_ggame_sgf_controller_skip);
    return g_test_run();
  }

  g_test_add_func("/sgf-controller/appends-move-property", test_ggame_sgf_controller_appends_move_property);
  g_test_add_func("/sgf-controller/replay-branching", test_ggame_sgf_controller_replay_branching);
  g_test_add_func("/sgf-controller/new-game", test_ggame_sgf_controller_new_game_clears_tree);
  g_test_add_func("/sgf-controller/new-game-no-manual-request",
                  test_ggame_sgf_controller_new_game_does_not_request_manual_mode);
  g_test_add_func("/sgf-controller/step-ai-move", test_ggame_sgf_controller_step_ai_move);
  g_test_add_func("/sgf-controller/navigation-step-and-rewind",
                  test_ggame_sgf_controller_navigation_step_and_rewind);
  g_test_add_func("/sgf-controller/current-node-move",
                  test_ggame_sgf_controller_current_node_move_accessor);
  g_test_add_func("/sgf-controller/navigation-forward-to-branch-and-end",
                  test_ggame_sgf_controller_navigation_forward_to_branch_and_end);
  g_test_add_func("/sgf-controller/select-node-emits-node-changed",
                  test_ggame_sgf_controller_select_node_emits_node_changed);
  g_test_add_func("/sgf-controller/load-applies-setup-properties",
                  test_ggame_sgf_controller_load_applies_setup_properties);
  g_test_add_func("/sgf-controller/load-file-requires-ru", test_ggame_sgf_controller_load_file_requires_ru);
  g_test_add_func("/sgf-controller/replay-node-into-game-applies-setup-root",
                  test_ggame_sgf_controller_replay_node_into_game_applies_setup_root);
  g_test_add_func("/sgf-controller/save-position-file", test_ggame_sgf_controller_save_position_file);
  g_test_add_func("/sgf-controller/load-file-applies-ruleset-from-ru",
                  test_ggame_sgf_controller_load_file_applies_ruleset_from_ru);
  return g_test_run();
}
