#include <gtk/gtk.h>

#include "active_game_backend.h"
#include "application.h"
#include "game_model.h"
#include "games/boop/boop_types.h"
#include "player_controls_panel.h"
#include "sgf_controller.h"
#include "sgf_tree.h"
#include "window.h"

static void test_ggame_window_skip(void) {
  g_test_skip("GTK display not available.");
}

static GtkApplication *test_app = NULL;

static GtkApplication *test_ggame_window_create_app(void) {
  g_return_val_if_fail(GTK_IS_APPLICATION(test_app), NULL);
  return g_object_ref(test_app);
}

static GGameWindow *test_ggame_window_new(GtkApplication *app, GGameModel *model) {
  g_return_val_if_fail(GTK_IS_APPLICATION(app), NULL);
  g_return_val_if_fail(GGAME_IS_MODEL(model), NULL);

  return ggame_window_new(app, model);
}

static GtkWidget *test_ggame_window_find_widget_for_action(GtkWidget *root, const char *action_name) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(action_name != NULL, NULL);

  if (GTK_IS_ACTIONABLE(root)) {
    const char *bound_action = gtk_actionable_get_action_name(GTK_ACTIONABLE(root));
    if (bound_action != NULL && g_strcmp0(bound_action, action_name) == 0) {
      return root;
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkWidget *match = test_ggame_window_find_widget_for_action(child, action_name);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkWidget *test_ggame_window_find_widget_with_uint_data(GtkWidget *root, const char *key, guint value) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(key != NULL, NULL);

  gpointer data = g_object_get_data(G_OBJECT(root), key);
  if (data != NULL && GPOINTER_TO_UINT(data) == value) {
    return root;
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkWidget *match = test_ggame_window_find_widget_with_uint_data(child, key, value);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkLabel *test_ggame_window_find_label_with_text(GtkWidget *root, const char *text) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(text != NULL, NULL);

  if (GTK_IS_LABEL(root)) {
    const char *label_text = gtk_label_get_text(GTK_LABEL(root));
    if (label_text != NULL && g_strcmp0(label_text, text) == 0) {
      return GTK_LABEL(root);
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkLabel *match = test_ggame_window_find_label_with_text(child, text);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkButton *test_ggame_window_find_button_with_label(GtkWidget *root, const char *label) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(label != NULL, NULL);

  if (GTK_IS_BUTTON(root)) {
    const char *button_label = gtk_button_get_label(GTK_BUTTON(root));
    if (button_label != NULL && g_strcmp0(button_label, label) == 0) {
      return GTK_BUTTON(root);
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkButton *match = test_ggame_window_find_button_with_label(child, label);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static void test_ggame_window_assert_coordinate_label(GtkWidget *root,
                                                      const char *data_key,
                                                      guint ordinal,
                                                      const char *expected_text) {
  GtkWidget *label = NULL;

  g_return_if_fail(GTK_IS_WIDGET(root));
  g_return_if_fail(data_key != NULL);
  g_return_if_fail(expected_text != NULL);

  label = test_ggame_window_find_widget_with_uint_data(root, data_key, ordinal);
  g_assert_nonnull(label);
  g_assert_true(GTK_IS_LABEL(label));
  g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(label)), ==, expected_text);
}

static GtkDropDown *test_ggame_window_find_mode_dropdown(GtkWidget *root) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);

  if (GTK_IS_DROP_DOWN(root)) {
    GListModel *model = gtk_drop_down_get_model(GTK_DROP_DOWN(root));
    if (model != NULL && g_list_model_get_n_items(model) == 2 && GTK_IS_STRING_LIST(model)) {
      const char *first = gtk_string_list_get_string(GTK_STRING_LIST(model), 0);
      const char *second = gtk_string_list_get_string(GTK_STRING_LIST(model), 1);
      if (g_strcmp0(first, "Play") == 0 && g_strcmp0(second, "Edit") == 0) {
        return GTK_DROP_DOWN(root);
      }
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkDropDown *match = test_ggame_window_find_mode_dropdown(child);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkWindow *test_ggame_window_find_toplevel_by_title(const char *title) {
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
    if (window_title != NULL && g_strcmp0(window_title, title) == 0) {
      return window;
    }
    g_object_unref(window);
  }

  return NULL;
}

static void test_ggame_window_drain_main_context(guint max_iterations) {
  g_return_if_fail(max_iterations > 0);

  for (guint i = 0; i < max_iterations; ++i) {
    if (!g_main_context_iteration(NULL, FALSE)) {
      return;
    }
  }

  g_debug("Main context still busy after %u iterations", max_iterations);
}

static gboolean test_ggame_window_node_has_analysis(const SgfNode *node) {
  g_return_val_if_fail(node != NULL, FALSE);

  g_autoptr(SgfNodeAnalysis) analysis = sgf_node_get_analysis(node);
  return analysis != NULL;
}

static gboolean test_ggame_window_wait_for_node_analysis(const SgfNode *node, gint64 timeout_us) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(timeout_us > 0, FALSE);

  gint64 deadline = g_get_monotonic_time() + timeout_us;
  while (g_get_monotonic_time() < deadline) {
    if (test_ggame_window_node_has_analysis(node)) {
      return TRUE;
    }
    test_ggame_window_drain_main_context(16);
    g_usleep(1000);
  }

  return test_ggame_window_node_has_analysis(node);
}

static gboolean test_ggame_window_wait_for_two_node_analyses(const SgfNode *first,
                                                             const SgfNode *second,
                                                             gint64 timeout_us) {
  g_return_val_if_fail(first != NULL, FALSE);
  g_return_val_if_fail(second != NULL, FALSE);
  g_return_val_if_fail(timeout_us > 0, FALSE);

  gint64 deadline = g_get_monotonic_time() + timeout_us;
  while (g_get_monotonic_time() < deadline) {
    if (test_ggame_window_node_has_analysis(first) && test_ggame_window_node_has_analysis(second)) {
      return TRUE;
    }
    test_ggame_window_drain_main_context(16);
    g_usleep(1000);
  }

  return test_ggame_window_node_has_analysis(first) && test_ggame_window_node_has_analysis(second);
}

static gboolean test_ggame_window_apply_first_generic_move(GGameSgfController *controller, GGameModel *model) {
  const GameBackend *backend = NULL;
  GameBackendMoveList moves = {0};
  gconstpointer move = NULL;
  gboolean applied = FALSE;

  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(controller), FALSE);
  g_return_val_if_fail(GGAME_IS_MODEL(model), FALSE);

  backend = ggame_model_peek_backend(model);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(backend->move_list_get != NULL, FALSE);
  g_return_val_if_fail(backend->move_list_free != NULL, FALSE);

  moves = ggame_model_list_moves(model);
  if (moves.count == 0) {
    backend->move_list_free(&moves);
    g_debug("No available moves for generic window test");
    return FALSE;
  }

  move = backend->move_list_get(&moves, 0);
  g_return_val_if_fail(move != NULL, FALSE);

  applied = ggame_sgf_controller_apply_move(controller, move);
  backend->move_list_free(&moves);
  if (!applied) {
    g_debug("Failed to apply generic test move through SGF controller");
    return FALSE;
  }

  return TRUE;
}

static void test_ggame_window_boop_shared_shell_widgets_exist(void) {
  GtkApplication *app = test_ggame_window_create_app();
  GGameModel *model = ggame_model_new(GGAME_ACTIVE_GAME_BACKEND);
  GGameWindow *window = test_ggame_window_new(app, model);
  gtk_window_present(GTK_WINDOW(window));
  test_ggame_window_drain_main_context(24);

  g_assert_nonnull(ggame_window_get_controls_panel(window));
  g_assert_nonnull(ggame_window_get_sgf_controller(window));
  g_assert_nonnull(test_ggame_window_find_widget_for_action(GTK_WIDGET(window), "app.new-game"));
  g_assert_nonnull(test_ggame_window_find_widget_for_action(GTK_WIDGET(window), "win.game-force-move"));
  g_assert_nonnull(test_ggame_window_find_widget_for_action(GTK_WIDGET(window), "win.navigation-step-forward"));
  g_assert_nonnull(test_ggame_window_find_widget_for_action(GTK_WIDGET(window), "win.navigation-step-backward"));
  g_assert_nonnull(test_ggame_window_find_widget_for_action(GTK_WIDGET(window), "win.navigation-rewind"));
  g_assert_nonnull(test_ggame_window_find_widget_for_action(GTK_WIDGET(window), "win.navigation-step-forward-to-end"));
  g_assert_nonnull(test_ggame_window_find_widget_for_action(GTK_WIDGET(window), "win.analysis-current-position"));
  g_assert_nonnull(test_ggame_window_find_widget_for_action(GTK_WIDGET(window), "win.analysis-whole-game"));
  g_assert_true(g_action_group_get_action_enabled(G_ACTION_GROUP(window), "analysis-current-position"));
  g_assert_true(g_action_group_get_action_enabled(G_ACTION_GROUP(window), "analysis-whole-game"));

  GtkWidget *side0_panel = test_ggame_window_find_widget_with_uint_data(GTK_WIDGET(window), "boop-side", 1);
  GtkWidget *side1_panel = test_ggame_window_find_widget_with_uint_data(GTK_WIDGET(window), "boop-side", 2);
  g_assert_nonnull(side0_panel);
  g_assert_nonnull(side1_panel);
  g_assert_true(gtk_widget_has_css_class(side0_panel, "boop-supply-panel"));
  g_assert_true(gtk_widget_has_css_class(side1_panel, "boop-supply-panel"));
  g_assert_nonnull(test_ggame_window_find_widget_with_uint_data(side0_panel, "boop-rank", BOOP_PIECE_RANK_KITTEN));
  g_assert_nonnull(test_ggame_window_find_widget_with_uint_data(side1_panel, "boop-rank", BOOP_PIECE_RANK_KITTEN));

  GtkDropDown *mode_dropdown = test_ggame_window_find_mode_dropdown(GTK_WIDGET(window));
  g_assert_nonnull(mode_dropdown);
  g_assert_false(gtk_widget_get_sensitive(GTK_WIDGET(mode_dropdown)));

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_ggame_window_boop_current_position_analysis_attaches_to_current_node(void) {
  GtkApplication *app = test_ggame_window_create_app();
  GGameModel *model = ggame_model_new(GGAME_ACTIVE_GAME_BACKEND);
  GGameWindow *window = test_ggame_window_new(app, model);
  GGameSgfController *controller = ggame_window_get_sgf_controller(window);
  SgfTree *tree = NULL;
  const SgfNode *current = NULL;
  GtkWidget *analysis_panel = NULL;

  gtk_window_present(GTK_WINDOW(window));
  test_ggame_window_drain_main_context(24);

  g_assert_nonnull(controller);
  tree = ggame_sgf_controller_get_tree(controller);
  g_assert_nonnull(tree);
  current = sgf_tree_get_current(tree);
  g_assert_nonnull(current);

  analysis_panel = g_object_get_data(G_OBJECT(window), "analysis-panel");
  g_assert_nonnull(analysis_panel);

  ggame_window_set_analysis_depth(window, 1);
  g_action_group_change_action_state(G_ACTION_GROUP(window),
                                     "view-show-analysis-drawer",
                                     g_variant_new_boolean(TRUE));
  test_ggame_window_drain_main_context(16);
  g_assert_nonnull(gtk_widget_get_parent(analysis_panel));

  g_action_group_activate_action(G_ACTION_GROUP(window), "analysis-current-position", NULL);
  g_assert_true(test_ggame_window_wait_for_node_analysis(current, 5 * G_USEC_PER_SEC));

  g_autoptr(SgfNodeAnalysis) analysis = sgf_node_get_analysis(current);
  g_assert_nonnull(analysis);
  g_assert_cmpuint(analysis->depth, ==, 1);
  g_assert_nonnull(analysis->moves);
  g_assert_cmpuint(analysis->moves->len, >, 0);

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_ggame_window_boop_full_game_analysis_attaches_to_replayed_nodes(void) {
  GtkApplication *app = test_ggame_window_create_app();
  GGameModel *model = ggame_model_new(GGAME_ACTIVE_GAME_BACKEND);
  GGameWindow *window = test_ggame_window_new(app, model);
  GGameSgfController *controller = ggame_window_get_sgf_controller(window);
  SgfTree *tree = NULL;
  const SgfNode *root = NULL;
  const SgfNode *current = NULL;

  g_assert_nonnull(controller);
  tree = ggame_sgf_controller_get_tree(controller);
  g_assert_nonnull(tree);

  g_assert_true(test_ggame_window_apply_first_generic_move(controller, model));
  g_assert_true(test_ggame_window_apply_first_generic_move(controller, model));
  root = sgf_tree_get_root(tree);
  current = sgf_tree_get_current(tree);
  g_assert_nonnull(root);
  g_assert_nonnull(current);
  g_assert_true(root != current);

  ggame_window_set_analysis_depth(window, 1);
  g_action_group_activate_action(G_ACTION_GROUP(window), "analysis-whole-game", NULL);
  g_assert_true(test_ggame_window_wait_for_two_node_analyses(root, current, 5 * G_USEC_PER_SEC));

  g_autoptr(SgfNodeAnalysis) root_analysis = sgf_node_get_analysis(root);
  g_autoptr(SgfNodeAnalysis) current_analysis = sgf_node_get_analysis(current);
  g_assert_nonnull(root_analysis);
  g_assert_nonnull(current_analysis);
  g_assert_cmpuint(root_analysis->depth, ==, 1);
  g_assert_cmpuint(current_analysis->depth, ==, 1);
  g_assert_nonnull(root_analysis->moves);
  g_assert_nonnull(current_analysis->moves);
  g_assert_cmpuint(root_analysis->moves->len, >, 0);
  g_assert_cmpuint(current_analysis->moves->len, >, 0);

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_ggame_window_boop_layout_defaults_fit_board_host(void) {
  GtkApplication *app = test_ggame_window_create_app();
  GGameModel *model = ggame_model_new(GGAME_ACTIVE_GAME_BACKEND);
  GGameWindow *window = test_ggame_window_new(app, model);
  gtk_window_present(GTK_WINDOW(window));
  test_ggame_window_drain_main_context(24);

  GtkWidget *board_panel = g_object_get_data(G_OBJECT(window), "board-panel");
  GtkWidget *analysis_panel = g_object_get_data(G_OBJECT(window), "analysis-panel");
  gint board_panel_width_request = -1;
  gint board_panel_height_request = -1;
  g_assert_nonnull(board_panel);
  g_assert_nonnull(analysis_panel);
  gtk_widget_get_size_request(board_panel, &board_panel_width_request, &board_panel_height_request);
  g_assert_cmpint(board_panel_width_request, >=, 700);
  g_assert_null(gtk_widget_get_parent(analysis_panel));

  g_autoptr(GVariant) analysis_drawer_state =
      g_action_group_get_action_state(G_ACTION_GROUP(window), "view-show-analysis-drawer");
  g_assert_nonnull(analysis_drawer_state);
  g_assert_false(g_variant_get_boolean(analysis_drawer_state));

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_ggame_window_boop_board_uses_edge_coordinates(void) {
  GtkApplication *app = test_ggame_window_create_app();
  GGameModel *model = ggame_model_new(GGAME_ACTIVE_GAME_BACKEND);
  GGameWindow *window = test_ggame_window_new(app, model);
  static const char *expected_columns[] = {"a", "b", "c", "d", "e", "f"};
  static const char *expected_rows[] = {"6", "5", "4", "3", "2", "1"};
  static const char *expected_flipped_columns[] = {"f", "e", "d", "c", "b", "a"};
  static const char *expected_flipped_rows[] = {"1", "2", "3", "4", "5", "6"};

  gtk_window_present(GTK_WINDOW(window));
  test_ggame_window_drain_main_context(24);

  g_assert_null(test_ggame_window_find_label_with_text(GTK_WIDGET(window), "36"));
  for (guint i = 0; i < BOOP_BOARD_SIZE; ++i) {
    test_ggame_window_assert_coordinate_label(GTK_WIDGET(window),
                                              "boop-coordinate-column",
                                              i + 1,
                                              expected_columns[i]);
    test_ggame_window_assert_coordinate_label(GTK_WIDGET(window),
                                              "boop-coordinate-row",
                                              i + 1,
                                              expected_rows[i]);
  }

  ggame_window_set_board_orientation_mode(window, GGAME_WINDOW_BOARD_ORIENTATION_FIXED);
  ggame_window_set_board_bottom_color(window, CHECKERS_COLOR_BLACK);
  test_ggame_window_drain_main_context(16);

  for (guint i = 0; i < BOOP_BOARD_SIZE; ++i) {
    test_ggame_window_assert_coordinate_label(GTK_WIDGET(window),
                                              "boop-coordinate-column",
                                              i + 1,
                                              expected_flipped_columns[i]);
    test_ggame_window_assert_coordinate_label(GTK_WIDGET(window),
                                              "boop-coordinate-row",
                                              i + 1,
                                              expected_flipped_rows[i]);
  }

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_ggame_window_boop_auto_moves_when_next_player_is_computer(void) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  GtkApplication *app = test_ggame_window_create_app();
  GGameModel *model = ggame_model_new(backend);
  GGameWindow *window = test_ggame_window_new(app, model);
  PlayerControlsPanel *panel = ggame_window_get_controls_panel(window);
  GGameSgfController *controller = ggame_window_get_sgf_controller(window);
  SgfTree *tree = ggame_sgf_controller_get_tree(controller);

  g_assert_nonnull(panel);
  g_assert_nonnull(controller);
  g_assert_nonnull(tree);

  player_controls_panel_set_mode(panel, 0, PLAYER_CONTROL_MODE_USER);
  player_controls_panel_set_mode(panel, 1, PLAYER_CONTROL_MODE_COMPUTER);
  player_controls_panel_set_computer_depth(panel, 2);

  g_assert_true(test_ggame_window_apply_first_generic_move(controller, model));
  test_ggame_window_drain_main_context(32);

  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 2);
  g_assert_cmpuint(backend->position_turn(ggame_model_peek_position(model)), ==, 0);

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_ggame_window_boop_sgf_actions_navigate_timeline(void) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  GtkApplication *app = test_ggame_window_create_app();
  GGameModel *model = ggame_model_new(backend);
  GGameWindow *window = test_ggame_window_new(app, model);
  GGameSgfController *controller = ggame_window_get_sgf_controller(window);
  SgfTree *tree = ggame_sgf_controller_get_tree(controller);

  g_assert_nonnull(controller);
  g_assert_nonnull(tree);
  g_assert_true(test_ggame_window_apply_first_generic_move(controller, model));
  g_assert_true(test_ggame_window_apply_first_generic_move(controller, model));
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 2);

  g_action_group_activate_action(G_ACTION_GROUP(window), "navigation-step-backward", NULL);
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 1);
  g_assert_cmpuint(backend->position_turn(ggame_model_peek_position(model)), ==, 1);

  g_action_group_activate_action(G_ACTION_GROUP(window), "navigation-step-forward", NULL);
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 2);
  g_assert_cmpuint(backend->position_turn(ggame_model_peek_position(model)), ==, 0);

  g_action_group_activate_action(G_ACTION_GROUP(window), "navigation-rewind", NULL);
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 0);
  g_assert_cmpuint(backend->position_turn(ggame_model_peek_position(model)), ==, 0);

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_ggame_window_boop_supply_selection_tracks_turn(void) {
  GtkApplication *app = test_ggame_window_create_app();
  GGameModel *model = ggame_model_new(GGAME_ACTIVE_GAME_BACKEND);
  GGameWindow *window = test_ggame_window_new(app, model);
  GGameSgfController *controller = ggame_window_get_sgf_controller(window);
  GtkWidget *side0_panel = test_ggame_window_find_widget_with_uint_data(GTK_WIDGET(window), "boop-side", 1);
  GtkWidget *side1_panel = test_ggame_window_find_widget_with_uint_data(GTK_WIDGET(window), "boop-side", 2);
  GtkWidget *side0_kitten = test_ggame_window_find_widget_with_uint_data(side0_panel,
                                                                          "boop-rank",
                                                                          BOOP_PIECE_RANK_KITTEN);
  GtkWidget *side1_kitten = test_ggame_window_find_widget_with_uint_data(side1_panel,
                                                                          "boop-rank",
                                                                          BOOP_PIECE_RANK_KITTEN);

  g_assert_nonnull(controller);
  g_assert_nonnull(side0_panel);
  g_assert_nonnull(side1_panel);
  g_assert_nonnull(side0_kitten);
  g_assert_nonnull(side1_kitten);

  g_assert_true(gtk_widget_has_css_class(side0_panel, "boop-supply-active"));
  g_assert_false(gtk_widget_has_css_class(side1_panel, "boop-supply-active"));
  g_assert_true(gtk_widget_has_css_class(side0_kitten, "boop-pile-selected"));
  g_assert_false(gtk_widget_has_css_class(side1_kitten, "boop-pile-selected"));

  g_assert_true(test_ggame_window_apply_first_generic_move(controller, model));
  test_ggame_window_drain_main_context(16);

  g_assert_false(gtk_widget_has_css_class(side0_panel, "boop-supply-active"));
  g_assert_true(gtk_widget_has_css_class(side1_panel, "boop-supply-active"));
  g_assert_false(gtk_widget_has_css_class(side0_kitten, "boop-pile-selected"));
  g_assert_true(gtk_widget_has_css_class(side1_kitten, "boop-pile-selected"));

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_ggame_window_boop_new_game_dialog_uses_shared_controls(void) {
  GtkApplication *app = test_ggame_window_create_app();
  GGameModel *model = ggame_model_new(GGAME_ACTIVE_GAME_BACKEND);
  GGameWindow *window = test_ggame_window_new(app, model);
  gtk_window_present(GTK_WINDOW(window));
  test_ggame_window_drain_main_context(24);

  g_action_group_activate_action(G_ACTION_GROUP(app), "new-game", NULL);
  test_ggame_window_drain_main_context(24);

  GtkWindow *dialog = test_ggame_window_find_toplevel_by_title("New game");
  g_assert_nonnull(dialog);
  g_assert_null(test_ggame_window_find_label_with_text(GTK_WIDGET(dialog), "Variant"));
  g_assert_nonnull(test_ggame_window_find_label_with_text(GTK_WIDGET(dialog), "Player 1"));
  g_assert_nonnull(test_ggame_window_find_label_with_text(GTK_WIDGET(dialog), "Player 2"));
  g_assert_nonnull(test_ggame_window_find_button_with_label(GTK_WIDGET(dialog), "New Game"));

  g_clear_object(&dialog);
  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_ggame_window_boop_settings_dialog_hides_puzzle_progress(void) {
  g_setenv("GSETTINGS_BACKEND", "memory", TRUE);

  GtkApplication *app = test_ggame_window_create_app();
  GGameModel *model = ggame_model_new(GGAME_ACTIVE_GAME_BACKEND);
  GGameWindow *window = test_ggame_window_new(app, model);
  gtk_window_present(GTK_WINDOW(window));
  test_ggame_window_drain_main_context(24);

  GAction *settings_action = g_action_map_lookup_action(G_ACTION_MAP(app), "settings");
  g_assert_nonnull(settings_action);
  g_action_group_activate_action(G_ACTION_GROUP(app), "settings", NULL);
  test_ggame_window_drain_main_context(24);

  GtkWindow *dialog = test_ggame_window_find_toplevel_by_title("Settings");
  g_assert_nonnull(dialog);
  g_assert_nonnull(test_ggame_window_find_button_with_label(GTK_WIDGET(dialog), "Save"));
  g_assert_nonnull(test_ggame_window_find_button_with_label(GTK_WIDGET(dialog), "Cancel"));
  g_assert_nonnull(
      test_ggame_window_find_label_with_text(GTK_WIDGET(dialog), "Send anonymized data about puzzle usage"));
  g_assert_nonnull(
      test_ggame_window_find_label_with_text(GTK_WIDGET(dialog), "Send anonymized data about application usage"));
  g_assert_null(test_ggame_window_find_label_with_text(GTK_WIDGET(dialog), "Puzzle Progress"));
  g_assert_null(test_ggame_window_find_button_with_label(GTK_WIDGET(dialog), "Clear Progress"));

  g_clear_object(&dialog);
  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  if (!gtk_init_check()) {
    g_test_add_func("/ggame-window/boop/shared-shell-widgets", test_ggame_window_skip);
    g_test_add_func("/ggame-window/boop/current-position-analysis", test_ggame_window_skip);
    g_test_add_func("/ggame-window/boop/full-game-analysis", test_ggame_window_skip);
    g_test_add_func("/ggame-window/boop/layout-defaults-fit-board-host", test_ggame_window_skip);
    g_test_add_func("/ggame-window/boop/board-edge-coordinates", test_ggame_window_skip);
    g_test_add_func("/ggame-window/boop/auto-move-next-player-computer", test_ggame_window_skip);
    g_test_add_func("/ggame-window/boop/sgf-actions-navigate", test_ggame_window_skip);
    g_test_add_func("/ggame-window/boop/supply-selection-tracks-turn", test_ggame_window_skip);
    g_test_add_func("/ggame-window/boop/new-game-dialog-shared-controls", test_ggame_window_skip);
    g_test_add_func("/ggame-window/boop/settings-dialog-no-puzzle-progress", test_ggame_window_skip);
    return g_test_run();
  }

  g_autoptr(GError) error = NULL;
  test_app = GTK_APPLICATION(ggame_application_new());
  gboolean registered = g_application_register(G_APPLICATION(test_app), NULL, &error);
  if (!registered || error != NULL) {
    g_test_message("Skipping boop window tests: failed to register application: %s",
                   error != NULL ? error->message : "unknown error");
    g_clear_object(&test_app);
    g_test_add_func("/ggame-window/boop/shared-shell-widgets", test_ggame_window_skip);
    g_test_add_func("/ggame-window/boop/current-position-analysis", test_ggame_window_skip);
    g_test_add_func("/ggame-window/boop/full-game-analysis", test_ggame_window_skip);
    g_test_add_func("/ggame-window/boop/layout-defaults-fit-board-host", test_ggame_window_skip);
    g_test_add_func("/ggame-window/boop/board-edge-coordinates", test_ggame_window_skip);
    g_test_add_func("/ggame-window/boop/auto-move-next-player-computer", test_ggame_window_skip);
    g_test_add_func("/ggame-window/boop/sgf-actions-navigate", test_ggame_window_skip);
    g_test_add_func("/ggame-window/boop/supply-selection-tracks-turn", test_ggame_window_skip);
    g_test_add_func("/ggame-window/boop/new-game-dialog-shared-controls", test_ggame_window_skip);
    g_test_add_func("/ggame-window/boop/settings-dialog-no-puzzle-progress", test_ggame_window_skip);
    return g_test_run();
  }

  g_test_add_func("/ggame-window/boop/shared-shell-widgets", test_ggame_window_boop_shared_shell_widgets_exist);
  g_test_add_func("/ggame-window/boop/current-position-analysis",
                  test_ggame_window_boop_current_position_analysis_attaches_to_current_node);
  g_test_add_func("/ggame-window/boop/full-game-analysis",
                  test_ggame_window_boop_full_game_analysis_attaches_to_replayed_nodes);
  g_test_add_func("/ggame-window/boop/layout-defaults-fit-board-host",
                  test_ggame_window_boop_layout_defaults_fit_board_host);
  g_test_add_func("/ggame-window/boop/board-edge-coordinates",
                  test_ggame_window_boop_board_uses_edge_coordinates);
  g_test_add_func("/ggame-window/boop/auto-move-next-player-computer",
                  test_ggame_window_boop_auto_moves_when_next_player_is_computer);
  g_test_add_func("/ggame-window/boop/sgf-actions-navigate",
                  test_ggame_window_boop_sgf_actions_navigate_timeline);
  g_test_add_func("/ggame-window/boop/supply-selection-tracks-turn",
                  test_ggame_window_boop_supply_selection_tracks_turn);
  g_test_add_func("/ggame-window/boop/new-game-dialog-shared-controls",
                  test_ggame_window_boop_new_game_dialog_uses_shared_controls);
  g_test_add_func("/ggame-window/boop/settings-dialog-no-puzzle-progress",
                  test_ggame_window_boop_settings_dialog_hides_puzzle_progress);
  return g_test_run();
}
