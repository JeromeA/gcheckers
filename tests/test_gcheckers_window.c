#include <gtk/gtk.h>

#include "checkers_model.h"
#include "gcheckers_sgf_controller.h"
#include "gcheckers_window.h"
#include "player_controls_panel.h"
#include "sgf_tree.h"

static void test_gcheckers_window_skip(void) {
  g_test_skip("GTK display not available.");
}

static GtkApplication *test_app = NULL;

static GtkApplication *test_gcheckers_window_create_app(void) {
  g_return_val_if_fail(GTK_IS_APPLICATION(test_app), NULL);
  return g_object_ref(test_app);
}

static GtkWidget *test_gcheckers_window_find_by_type(GtkWidget *root, GType widget_type) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(g_type_is_a(widget_type, GTK_TYPE_WIDGET), NULL);

  if (g_type_is_a(G_OBJECT_TYPE(root), widget_type)) {
    return root;
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkWidget *match = test_gcheckers_window_find_by_type(child, widget_type);
    if (match) {
      return match;
    }
  }

  return NULL;
}

static GtkWidget *test_gcheckers_window_find_board_square(GtkWidget *root) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);

  if (GTK_IS_BUTTON(root)) {
    gpointer data = g_object_get_data(G_OBJECT(root), "board-index");
    if (data != NULL) {
      return root;
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkWidget *match = test_gcheckers_window_find_board_square(child);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkToggleButton *test_gcheckers_window_find_toggle_button_with_label(GtkWidget *root, const char *label) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(label != NULL, NULL);

  if (GTK_IS_TOGGLE_BUTTON(root)) {
    const char *button_label = gtk_button_get_label(GTK_BUTTON(root));
    if (button_label && g_strcmp0(button_label, label) == 0) {
      return GTK_TOGGLE_BUTTON(root);
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkToggleButton *match = test_gcheckers_window_find_toggle_button_with_label(child, label);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static void test_gcheckers_window_drain_main_context(guint max_iterations) {
  g_return_if_fail(max_iterations > 0);

  for (guint i = 0; i < max_iterations; ++i) {
    if (!g_main_context_iteration(NULL, FALSE)) {
      return;
    }
  }

  g_debug("Main context still busy after %u iterations\n", max_iterations);
}

static gboolean apply_first_move(GCheckersSgfController *controller,
                                 GCheckersModel *model,
                                 CheckersMove *out_move) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(controller), FALSE);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(model), FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  MoveList moves = gcheckers_model_list_moves(model);
  if (moves.count == 0) {
    g_debug("No available moves for gcheckers window test\n");
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

static const SgfNode *sgf_tree_get_first_child(SgfTree *tree) {
  g_return_val_if_fail(SGF_IS_TREE(tree), NULL);

  const SgfNode *root = sgf_tree_get_root(tree);
  g_return_val_if_fail(root != NULL, NULL);

  const GPtrArray *children = sgf_node_get_children(root);
  if (children == NULL || children->len == 0) {
    g_debug("SGF tree root has no children\n");
    return NULL;
  }

  return g_ptr_array_index(children, 0);
}

static void test_gcheckers_window_unparents_controls_panel_on_dispose(void) {
  GtkApplication *app = test_gcheckers_window_create_app();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(app, model);

  GtkWidget *panel_widget = test_gcheckers_window_find_by_type(GTK_WIDGET(window), PLAYER_TYPE_CONTROLS_PANEL);
  g_assert_nonnull(panel_widget);
  g_assert_true(PLAYER_IS_CONTROLS_PANEL(panel_widget));

  g_object_ref(panel_widget);
  g_object_run_dispose(G_OBJECT(window));

  g_assert_null(gtk_widget_get_parent(panel_widget));

  g_object_unref(panel_widget);
  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_gcheckers_window_dispose_without_external_panel_ref(void) {
  GtkApplication *app = test_gcheckers_window_create_app();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(app, model);

  g_object_run_dispose(G_OBJECT(window));

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_gcheckers_window_dispose_after_panel_removed(void) {
  GtkApplication *app = test_gcheckers_window_create_app();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(app, model);

  GtkWidget *panel_widget = test_gcheckers_window_find_by_type(GTK_WIDGET(window), PLAYER_TYPE_CONTROLS_PANEL);
  g_assert_nonnull(panel_widget);

  GtkWidget *parent = gtk_widget_get_parent(panel_widget);
  g_assert_true(GTK_IS_BOX(parent));
  gtk_box_remove(GTK_BOX(parent), panel_widget);

  g_object_run_dispose(G_OBJECT(window));

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_gcheckers_window_computer_selection_keeps_board_enabled(void) {
  GtkApplication *app = test_gcheckers_window_create_app();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(app, model);

  PlayerControlsPanel *panel = gcheckers_window_get_controls_panel(window);
  g_assert_nonnull(panel);

  player_controls_panel_set_mode(panel, CHECKERS_COLOR_WHITE, PLAYER_CONTROL_MODE_COMPUTER);
  test_gcheckers_window_drain_main_context(8);

  GtkWidget *square = test_gcheckers_window_find_board_square(GTK_WIDGET(window));
  g_assert_nonnull(square);
  g_assert_true(gtk_widget_get_sensitive(square));

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_gcheckers_window_auto_moves_when_next_player_is_computer(void) {
  GtkApplication *app = test_gcheckers_window_create_app();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(app, model);

  PlayerControlsPanel *panel = gcheckers_window_get_controls_panel(window);
  g_assert_nonnull(panel);

  player_controls_panel_set_mode(panel, CHECKERS_COLOR_WHITE, PLAYER_CONTROL_MODE_USER);
  player_controls_panel_set_mode(panel, CHECKERS_COLOR_BLACK, PLAYER_CONTROL_MODE_COMPUTER);
  player_controls_panel_set_computer_level(panel, PLAYER_COMPUTER_LEVEL_2_DEPTH_4);

  GCheckersSgfController *controller = gcheckers_window_get_sgf_controller(window);
  g_assert_nonnull(controller);

  CheckersMove move;
  g_assert_true(apply_first_move(controller, model, &move));
  test_gcheckers_window_drain_main_context(16);

  const GameState *state = gcheckers_model_peek_state(model);
  g_assert_nonnull(state);
  g_assert_cmpuint(state->turn, ==, CHECKERS_COLOR_WHITE);

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_gcheckers_window_sgf_navigation_resets_controls_to_user(void) {
  GtkApplication *app = test_gcheckers_window_create_app();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(app, model);

  PlayerControlsPanel *panel = gcheckers_window_get_controls_panel(window);
  g_assert_nonnull(panel);

  player_controls_panel_set_mode(panel, CHECKERS_COLOR_WHITE, PLAYER_CONTROL_MODE_USER);
  player_controls_panel_set_mode(panel, CHECKERS_COLOR_BLACK, PLAYER_CONTROL_MODE_COMPUTER);
  player_controls_panel_set_computer_level(panel, PLAYER_COMPUTER_LEVEL_3_DEPTH_8);

  GCheckersSgfController *controller = gcheckers_window_get_sgf_controller(window);
  g_assert_nonnull(controller);

  CheckersMove move;
  g_assert_true(apply_first_move(controller, model, &move));
  test_gcheckers_window_drain_main_context(16);

  SgfTree *tree = gcheckers_sgf_controller_get_tree(controller);
  const SgfNode *node = sgf_tree_get_first_child(tree);
  g_assert_nonnull(node);

  SgfView *view = gcheckers_sgf_controller_get_view(controller);
  g_signal_emit_by_name(view, "node-selected", node);
  test_gcheckers_window_drain_main_context(16);

  g_assert_true(player_controls_panel_is_user_control(panel, CHECKERS_COLOR_WHITE));
  g_assert_true(player_controls_panel_is_user_control(panel, CHECKERS_COLOR_BLACK));

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_gcheckers_window_force_move_works_on_user_turn(void) {
  GtkApplication *app = test_gcheckers_window_create_app();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(app, model);

  PlayerControlsPanel *panel = gcheckers_window_get_controls_panel(window);
  g_assert_nonnull(panel);
  g_assert_true(player_controls_panel_is_user_control(panel, CHECKERS_COLOR_WHITE));
  g_assert_true(player_controls_panel_is_user_control(panel, CHECKERS_COLOR_BLACK));

  GtkWidget *button = player_controls_panel_get_force_move_button(panel);
  g_assert_nonnull(button);
  g_signal_emit_by_name(button, "clicked");
  test_gcheckers_window_drain_main_context(16);

  const GameState *state = gcheckers_model_peek_state(model);
  g_assert_nonnull(state);
  g_assert_cmpuint(state->turn, ==, CHECKERS_COLOR_BLACK);

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_gcheckers_window_analysis_toggle(void) {
  GtkApplication *app = test_gcheckers_window_create_app();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(app, model);

  GtkToggleButton *analyze_toggle =
      test_gcheckers_window_find_toggle_button_with_label(GTK_WIDGET(window), "Analyze");
  g_assert_nonnull(analyze_toggle);
  g_assert_false(gtk_toggle_button_get_active(analyze_toggle));

  gtk_toggle_button_set_active(analyze_toggle, TRUE);
  test_gcheckers_window_drain_main_context(8);
  g_assert_true(gtk_toggle_button_get_active(analyze_toggle));

  gtk_toggle_button_set_active(analyze_toggle, FALSE);
  test_gcheckers_window_drain_main_context(8);
  g_assert_false(gtk_toggle_button_get_active(analyze_toggle));

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  if (!gtk_init_check()) {
    g_test_add_func("/gcheckers-window/dispose-unparents-controls", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/dispose-without-panel-ref", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/dispose-after-panel-removed", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/computer-selection-keeps-board-enabled", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/auto-move-next-player-computer", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/sgf-navigation-resets-controls", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/force-move-user-turn", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/analysis-toggle", test_gcheckers_window_skip);
    return g_test_run();
  }

  g_autoptr(GError) error = NULL;
  test_app = gtk_application_new("org.example.gcheckers.tests", G_APPLICATION_DEFAULT_FLAGS);
  gboolean registered = g_application_register(G_APPLICATION(test_app), NULL, &error);
  if (!registered || error) {
    g_test_message("Skipping gcheckers window tests: failed to register application: %s",
                   error ? error->message : "unknown error");
    g_clear_object(&test_app);
    g_test_add_func("/gcheckers-window/dispose-unparents-controls", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/dispose-without-panel-ref", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/dispose-after-panel-removed", test_gcheckers_window_skip);
    return g_test_run();
  }

  g_test_add_func("/gcheckers-window/dispose-unparents-controls",
                  test_gcheckers_window_unparents_controls_panel_on_dispose);
  g_test_add_func("/gcheckers-window/dispose-without-panel-ref",
                  test_gcheckers_window_dispose_without_external_panel_ref);
  g_test_add_func("/gcheckers-window/dispose-after-panel-removed",
                  test_gcheckers_window_dispose_after_panel_removed);
  g_test_add_func("/gcheckers-window/computer-selection-keeps-board-enabled",
                  test_gcheckers_window_computer_selection_keeps_board_enabled);
  g_test_add_func("/gcheckers-window/auto-move-next-player-computer",
                  test_gcheckers_window_auto_moves_when_next_player_is_computer);
  g_test_add_func("/gcheckers-window/sgf-navigation-resets-controls",
                  test_gcheckers_window_sgf_navigation_resets_controls_to_user);
  g_test_add_func("/gcheckers-window/force-move-user-turn",
                  test_gcheckers_window_force_move_works_on_user_turn);
  g_test_add_func("/gcheckers-window/analysis-toggle", test_gcheckers_window_analysis_toggle);
  return g_test_run();
}
