#include <gtk/gtk.h>

#include "checkers_model.h"
#include "sgf_controller.h"
#include "window.h"
#include "player_controls_panel.h"
#include "analysis_graph.h"
#include "sgf_tree.h"

static void test_gcheckers_window_skip(void) {
  g_test_skip("GTK display not available.");
}

static GtkApplication *test_app = NULL;

static void test_analysis_graph_score_compression(void) {
  g_assert_cmpfloat_with_epsilon(analysis_graph_compress_score(0.0), 0.0, 0.000001);
  g_assert_cmpfloat_with_epsilon(analysis_graph_compress_score(1800.0), 900.0, 0.000001);
  g_assert_cmpfloat_with_epsilon(analysis_graph_compress_score(-1800.0), -900.0, 0.000001);
  g_assert_cmpfloat_with_epsilon(analysis_graph_compress_score(3600.0), 1200.0, 0.000001);
  g_assert_cmpfloat_with_epsilon(analysis_graph_compress_score(-3600.0), -1200.0, 0.000001);
}

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

static GtkButton *test_gcheckers_window_find_button_with_label(GtkWidget *root, const char *label) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(label != NULL, NULL);

  if (GTK_IS_BUTTON(root)) {
    const char *button_label = gtk_button_get_label(GTK_BUTTON(root));
    if (button_label && g_strcmp0(button_label, label) == 0) {
      return GTK_BUTTON(root);
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkButton *match = test_gcheckers_window_find_button_with_label(child, label);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkCheckButton *test_gcheckers_window_find_check_button_with_label(GtkWidget *root, const char *label) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(label != NULL, NULL);

  if (GTK_IS_CHECK_BUTTON(root)) {
    const char *button_label = gtk_check_button_get_label(GTK_CHECK_BUTTON(root));
    if (button_label && g_strcmp0(button_label, label) == 0) {
      return GTK_CHECK_BUTTON(root);
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkCheckButton *match = test_gcheckers_window_find_check_button_with_label(child, label);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkLabel *test_gcheckers_window_find_label_with_text(GtkWidget *root, const char *text) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(text != NULL, NULL);

  if (GTK_IS_LABEL(root)) {
    const char *label_text = gtk_label_get_text(GTK_LABEL(root));
    if (label_text && g_strcmp0(label_text, text) == 0) {
      return GTK_LABEL(root);
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkLabel *match = test_gcheckers_window_find_label_with_text(child, text);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkWidget *test_gcheckers_window_find_widget_for_action(GtkWidget *root, const char *action_name) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(action_name != NULL, NULL);

  if (GTK_IS_ACTIONABLE(root)) {
    const char *bound_action = gtk_actionable_get_action_name(GTK_ACTIONABLE(root));
    if (bound_action && g_strcmp0(bound_action, action_name) == 0) {
      return root;
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkWidget *match = test_gcheckers_window_find_widget_for_action(child, action_name);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkDropDown *test_gcheckers_window_find_ruleset_dropdown(GtkWidget *root) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);

  if (GTK_IS_DROP_DOWN(root)) {
    GListModel *model = gtk_drop_down_get_model(GTK_DROP_DOWN(root));
    if (GTK_IS_STRING_LIST(model) && g_list_model_get_n_items(model) == 3) {
      const char *first = gtk_string_list_get_string(GTK_STRING_LIST(model), 0);
      const char *second = gtk_string_list_get_string(GTK_STRING_LIST(model), 1);
      const char *third = gtk_string_list_get_string(GTK_STRING_LIST(model), 2);
      if (g_strcmp0(first, "American (8x8)") == 0 && g_strcmp0(second, "International (10x10)") == 0 &&
          g_strcmp0(third, "Russian (8x8)") == 0) {
        return GTK_DROP_DOWN(root);
      }
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkDropDown *match = test_gcheckers_window_find_ruleset_dropdown(child);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkWindow *test_gcheckers_window_find_toplevel_by_title(const char *title) {
  g_return_val_if_fail(title != NULL, NULL);

  GListModel *toplevels = gtk_window_get_toplevels();
  guint count = g_list_model_get_n_items(toplevels);
  for (guint i = 0; i < count; ++i) {
    GtkWindow *window = g_list_model_get_item(toplevels, i);
    if (!GTK_IS_WINDOW(window)) {
      if (window != NULL) {
        g_object_unref(window);
      }
      continue;
    }

    const char *window_title = gtk_window_get_title(window);
    if (window_title && g_strcmp0(window_title, title) == 0) {
      return window;
    }
    g_object_unref(window);
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

static AnalysisGraph *test_gcheckers_window_get_analysis_graph(GCheckersWindow *window) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(window), NULL);

  gpointer data = g_object_get_data(G_OBJECT(window), "analysis-graph");
  if (data == NULL) {
    return NULL;
  }

  return ANALYSIS_GRAPH(data);
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
  player_controls_panel_set_computer_depth(panel, 4);

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
  player_controls_panel_set_computer_depth(panel, 8);

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

  gcheckers_window_force_move(window);
  test_gcheckers_window_drain_main_context(16);

  const GameState *state = gcheckers_model_peek_state(model);
  g_assert_nonnull(state);
  g_assert_cmpuint(state->turn, ==, CHECKERS_COLOR_BLACK);

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_gcheckers_window_toolbar_actions_exist(void) {
  GtkApplication *app = test_gcheckers_window_create_app();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(app, model);

  GtkWidget *new_game_button = test_gcheckers_window_find_widget_for_action(GTK_WIDGET(window), "app.new-game");
  GtkWidget *force_move_button = test_gcheckers_window_find_widget_for_action(GTK_WIDGET(window), "app.force-move");
  GtkWidget *rewind_button = test_gcheckers_window_find_widget_for_action(GTK_WIDGET(window), "win.sgf-rewind");
  GtkWidget *step_backward_button =
      test_gcheckers_window_find_widget_for_action(GTK_WIDGET(window), "win.sgf-step-backward");
  GtkWidget *step_forward_button =
      test_gcheckers_window_find_widget_for_action(GTK_WIDGET(window), "win.sgf-step-forward");
  GtkWidget *step_to_branch_button =
      test_gcheckers_window_find_widget_for_action(GTK_WIDGET(window), "win.sgf-step-forward-to-branch");
  GtkWidget *step_to_end_button =
      test_gcheckers_window_find_widget_for_action(GTK_WIDGET(window), "win.sgf-step-forward-to-end");
  g_assert_nonnull(new_game_button);
  g_assert_nonnull(force_move_button);
  g_assert_nonnull(rewind_button);
  g_assert_nonnull(step_backward_button);
  g_assert_nonnull(step_forward_button);
  g_assert_nonnull(step_to_branch_button);
  g_assert_nonnull(step_to_end_button);
  g_assert_nonnull(g_action_map_lookup_action(G_ACTION_MAP(window), "sgf-load"));
  g_assert_nonnull(g_action_map_lookup_action(G_ACTION_MAP(window), "sgf-save-as"));

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_gcheckers_window_sgf_actions_navigate_timeline(void) {
  GtkApplication *app = test_gcheckers_window_create_app();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(app, model);

  GCheckersSgfController *controller = gcheckers_window_get_sgf_controller(window);
  g_assert_nonnull(controller);

  CheckersMove move = {0};
  g_assert_true(apply_first_move(controller, model, &move));
  g_assert_true(apply_first_move(controller, model, &move));

  SgfTree *tree = gcheckers_sgf_controller_get_tree(controller);
  g_assert_nonnull(tree);
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 2);

  g_action_group_activate_action(G_ACTION_GROUP(window), "sgf-step-backward", NULL);
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 1);

  g_action_group_activate_action(G_ACTION_GROUP(window), "sgf-step-forward", NULL);
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 2);

  g_action_group_activate_action(G_ACTION_GROUP(window), "sgf-rewind", NULL);
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 0);

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

static void test_gcheckers_window_analysis_full_button_exists(void) {
  GtkApplication *app = test_gcheckers_window_create_app();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(app, model);

  GtkButton *full_button =
      test_gcheckers_window_find_button_with_label(GTK_WIDGET(window), "Analyze full game");
  g_assert_nonnull(full_button);
  g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(full_button)));

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_gcheckers_window_graph_selection_tracks_sgf_selection(void) {
  GtkApplication *app = test_gcheckers_window_create_app();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(app, model);

  GCheckersSgfController *controller = gcheckers_window_get_sgf_controller(window);
  g_assert_nonnull(controller);

  CheckersMove move = {0};
  g_assert_true(apply_first_move(controller, model, &move));
  g_assert_true(apply_first_move(controller, model, &move));
  test_gcheckers_window_drain_main_context(16);

  AnalysisGraph *graph = test_gcheckers_window_get_analysis_graph(window);
  g_assert_nonnull(graph);
  g_assert_cmpuint(analysis_graph_get_node_count(graph), ==, 3);
  g_assert_cmpuint(analysis_graph_get_selected_index(graph), ==, 2);

  g_action_group_activate_action(G_ACTION_GROUP(window), "sgf-step-backward", NULL);
  test_gcheckers_window_drain_main_context(16);
  g_assert_cmpuint(analysis_graph_get_selected_index(graph), ==, 1);

  g_action_group_activate_action(G_ACTION_GROUP(window), "sgf-rewind", NULL);
  test_gcheckers_window_drain_main_context(16);
  g_assert_cmpuint(analysis_graph_get_selected_index(graph), ==, 0);

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_gcheckers_window_graph_activation_changes_sgf_selection(void) {
  GtkApplication *app = test_gcheckers_window_create_app();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(app, model);

  GCheckersSgfController *controller = gcheckers_window_get_sgf_controller(window);
  g_assert_nonnull(controller);
  SgfTree *tree = gcheckers_sgf_controller_get_tree(controller);
  g_assert_nonnull(tree);

  CheckersMove move = {0};
  g_assert_true(apply_first_move(controller, model, &move));
  test_gcheckers_window_drain_main_context(16);

  const SgfNode *first = sgf_tree_get_first_child(tree);
  g_assert_nonnull(first);
  const SgfNode *root = sgf_tree_get_root(tree);
  g_assert_nonnull(root);
  g_assert_true(sgf_tree_get_current(tree) == first);

  AnalysisGraph *graph = test_gcheckers_window_get_analysis_graph(window);
  g_assert_nonnull(graph);
  g_signal_emit_by_name(graph, "node-activated", root);
  test_gcheckers_window_drain_main_context(16);
  g_assert_true(sgf_tree_get_current(tree) == root);

  g_signal_emit_by_name(graph, "node-activated", first);
  test_gcheckers_window_drain_main_context(16);
  g_assert_true(sgf_tree_get_current(tree) == first);

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_gcheckers_window_import_wizard_flow(void) {
  GtkApplication *app = test_gcheckers_window_create_app();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(app, model);
  gtk_window_present(GTK_WINDOW(window));
  test_gcheckers_window_drain_main_context(16);

  g_action_group_activate_action(G_ACTION_GROUP(app), "import", NULL);
  test_gcheckers_window_drain_main_context(32);

  GtkWindow *dialog = test_gcheckers_window_find_toplevel_by_title("Import games");
  g_assert_nonnull(dialog);

  GtkButton *next_button = test_gcheckers_window_find_button_with_label(GTK_WIDGET(dialog), "Next");
  g_assert_nonnull(next_button);
  g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(next_button)));

  GtkWidget *drop_down_widget = test_gcheckers_window_find_by_type(GTK_WIDGET(dialog), GTK_TYPE_DROP_DOWN);
  g_assert_nonnull(drop_down_widget);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(drop_down_widget), 3);
  test_gcheckers_window_drain_main_context(16);
  g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(next_button)));

  g_signal_emit_by_name(next_button, "clicked");
  test_gcheckers_window_drain_main_context(16);
  g_assert_cmpstr(gtk_button_get_label(next_button), ==, "Fetch game history");
  g_assert_nonnull(test_gcheckers_window_find_label_with_text(GTK_WIDGET(dialog), "Email"));
  g_assert_nonnull(test_gcheckers_window_find_label_with_text(GTK_WIDGET(dialog), "Password"));
  g_assert_nonnull(test_gcheckers_window_find_check_button_with_label(GTK_WIDGET(dialog), "Remember credentials"));

  GtkButton *cancel_button = test_gcheckers_window_find_button_with_label(GTK_WIDGET(dialog), "Cancel");
  g_assert_nonnull(cancel_button);
  g_signal_emit_by_name(cancel_button, "clicked");
  test_gcheckers_window_drain_main_context(16);
  GtkWindow *after_close = test_gcheckers_window_find_toplevel_by_title("Import games");
  g_assert_null(after_close);

  g_clear_object(&dialog);
  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_gcheckers_window_ruleset_switch_resets_model(void) {
  GtkApplication *app = test_gcheckers_window_create_app();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(app, model);

  const GameState *state = gcheckers_model_peek_state(model);
  g_assert_nonnull(state);
  g_assert_cmpuint(state->board.board_size, ==, 10);
  g_assert_cmpuint(gcheckers_window_get_ruleset(window), ==, PLAYER_RULESET_INTERNATIONAL);

  gcheckers_window_apply_new_game_settings(window,
                                           PLAYER_RULESET_AMERICAN,
                                           PLAYER_CONTROL_MODE_USER,
                                           PLAYER_CONTROL_MODE_USER,
                                           0);
  test_gcheckers_window_drain_main_context(16);

  state = gcheckers_model_peek_state(model);
  g_assert_nonnull(state);
  g_assert_cmpuint(state->board.board_size, ==, 8);
  g_assert_cmpuint(gcheckers_window_get_ruleset(window), ==, PLAYER_RULESET_AMERICAN);
  g_assert_cmpuint(state->turn, ==, CHECKERS_COLOR_WHITE);
  g_assert_cmpuint(state->winner, ==, CHECKERS_WINNER_NONE);

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_gcheckers_window_new_game_dialog_ruleset_options_and_russian_apply(void) {
  GtkApplication *app = test_gcheckers_window_create_app();
  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(app, model);

  gcheckers_window_apply_new_game_settings(window,
                                           PLAYER_RULESET_AMERICAN,
                                           PLAYER_CONTROL_MODE_USER,
                                           PLAYER_CONTROL_MODE_USER,
                                           0);
  test_gcheckers_window_drain_main_context(16);

  gtk_window_present(GTK_WINDOW(window));
  test_gcheckers_window_drain_main_context(16);

  g_action_group_activate_action(G_ACTION_GROUP(app), "new-game", NULL);
  test_gcheckers_window_drain_main_context(16);

  GtkWindow *dialog = test_gcheckers_window_find_toplevel_by_title("New game");
  g_assert_nonnull(dialog);

  const char *american_summary =
      "8x8 board, mandatory captures, short kings, and no backward captures for men.";
  GtkLabel *summary_label = test_gcheckers_window_find_label_with_text(GTK_WIDGET(dialog), american_summary);
  g_assert_nonnull(summary_label);
  int initial_height = gtk_widget_get_height(GTK_WIDGET(dialog));
  g_assert_cmpint(initial_height, >, 0);

  GtkDropDown *ruleset_dropdown = test_gcheckers_window_find_ruleset_dropdown(GTK_WIDGET(dialog));
  g_assert_nonnull(ruleset_dropdown);
  gtk_drop_down_set_selected(ruleset_dropdown, PLAYER_RULESET_INTERNATIONAL);
  test_gcheckers_window_drain_main_context(16);

  const char *international_summary =
      "10x10 board, mandatory longest captures, flying kings, and backward captures for men.";
  summary_label = test_gcheckers_window_find_label_with_text(GTK_WIDGET(dialog), international_summary);
  g_assert_nonnull(summary_label);
  g_assert_cmpint(gtk_widget_get_height(GTK_WIDGET(dialog)), ==, initial_height);

  gtk_drop_down_set_selected(ruleset_dropdown, PLAYER_RULESET_RUSSIAN);
  test_gcheckers_window_drain_main_context(16);

  const char *russian_summary =
      "8x8 board, mandatory longest captures, flying kings, and backward captures for men.";
  summary_label = test_gcheckers_window_find_label_with_text(GTK_WIDGET(dialog), russian_summary);
  g_assert_nonnull(summary_label);
  g_assert_cmpint(gtk_widget_get_height(GTK_WIDGET(dialog)), ==, initial_height);

  GtkButton *confirm_button = test_gcheckers_window_find_button_with_label(GTK_WIDGET(dialog), "New Game");
  g_assert_nonnull(confirm_button);
  g_signal_emit_by_name(confirm_button, "clicked");
  test_gcheckers_window_drain_main_context(16);

  g_assert_cmpuint(gcheckers_window_get_ruleset(window), ==, PLAYER_RULESET_RUSSIAN);
  Game game = {0};
  g_assert_true(gcheckers_model_copy_game(model, &game));
  g_assert_cmpuint(game.rules->board_size, ==, 8);
  g_assert_true(game.rules->capture_mandatory);
  g_assert_true(game.rules->longest_capture_mandatory);
  g_assert_true(game.rules->kings_can_fly);
  g_assert_true(game.rules->men_can_jump_backwards);

  g_clear_object(&dialog);
  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/analysis-graph/score-compression", test_analysis_graph_score_compression);

  if (!gtk_init_check()) {
    g_test_add_func("/gcheckers-window/dispose-unparents-controls", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/dispose-without-panel-ref", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/dispose-after-panel-removed", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/computer-selection-keeps-board-enabled", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/auto-move-next-player-computer", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/sgf-navigation-resets-controls", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/force-move-user-turn", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/toolbar-actions", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/sgf-actions-navigate", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/analysis-toggle", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/analysis-full-button", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/graph-selection-sync", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/graph-activation-selects-node", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/import-wizard-flow", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/ruleset-switch", test_gcheckers_window_skip);
    g_test_add_func("/gcheckers-window/new-game-ruleset-options-russian", test_gcheckers_window_skip);
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
  g_test_add_func("/gcheckers-window/toolbar-actions", test_gcheckers_window_toolbar_actions_exist);
  g_test_add_func("/gcheckers-window/sgf-actions-navigate",
                  test_gcheckers_window_sgf_actions_navigate_timeline);
  g_test_add_func("/gcheckers-window/analysis-toggle", test_gcheckers_window_analysis_toggle);
  g_test_add_func("/gcheckers-window/analysis-full-button", test_gcheckers_window_analysis_full_button_exists);
  g_test_add_func("/gcheckers-window/graph-selection-sync",
                  test_gcheckers_window_graph_selection_tracks_sgf_selection);
  g_test_add_func("/gcheckers-window/graph-activation-selects-node",
                  test_gcheckers_window_graph_activation_changes_sgf_selection);
  g_test_add_func("/gcheckers-window/import-wizard-flow", test_gcheckers_window_import_wizard_flow);
  g_test_add_func("/gcheckers-window/ruleset-switch", test_gcheckers_window_ruleset_switch_resets_model);
  g_test_add_func("/gcheckers-window/new-game-ruleset-options-russian",
                  test_gcheckers_window_new_game_dialog_ruleset_options_and_russian_apply);
  return g_test_run();
}
