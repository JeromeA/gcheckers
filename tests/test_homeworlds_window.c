#include <gtk/gtk.h>
#include <string.h>

#include "../src/game_app_profile.h"
#include "../src/game_model.h"
#include "../src/games/homeworlds/homeworlds_backend.h"
#include "../src/games/homeworlds/homeworlds_game.h"
#include "../src/games/homeworlds/homeworlds_view.h"
#include "../src/player_controls_panel.h"
#include "../src/sgf_controller.h"
#include "../src/sgf_tree.h"
#include "../src/window.h"

static void test_homeworlds_window_skip(void) {
  g_test_skip("GTK display not available.");
}

static GtkApplication *test_homeworlds_app = NULL;

static GtkWidget *test_homeworlds_find_widget_named(GtkWidget *root, const char *name) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(name != NULL, NULL);

  if (g_strcmp0(gtk_widget_get_name(root), name) == 0) {
    return root;
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkWidget *match = test_homeworlds_find_widget_named(child, name);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkWidget *test_homeworlds_find_widget_for_action(GtkWidget *root, const char *action_name) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(action_name != NULL, NULL);

  if (GTK_IS_ACTIONABLE(root)) {
    const char *widget_action = gtk_actionable_get_action_name(GTK_ACTIONABLE(root));
    if (widget_action != NULL && g_strcmp0(widget_action, action_name) == 0) {
      return root;
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkWidget *match = test_homeworlds_find_widget_for_action(child, action_name);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkApplication *test_homeworlds_create_app(void) {
  if (test_homeworlds_app == NULL) {
    GError *error = NULL;

    test_homeworlds_app = gtk_application_new("io.github.jeromea.ghomeworlds.test",
                                              G_APPLICATION_DEFAULT_FLAGS | G_APPLICATION_NON_UNIQUE);
    g_assert_nonnull(test_homeworlds_app);
    g_assert_true(g_application_register(G_APPLICATION(test_homeworlds_app), NULL, &error));
    g_assert_no_error(error);
  }

  return g_object_ref(test_homeworlds_app);
}

static GGameWindow *test_homeworlds_create_window(GtkApplication **out_app, GGameModel **out_model) {
  GtkApplication *app = test_homeworlds_create_app();
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  GGameWindow *window = NULL;

  g_assert_nonnull(model);
  window = ggame_window_new(app, model);
  g_assert_nonnull(window);

  if (out_app != NULL) {
    *out_app = app;
  } else {
    g_object_unref(app);
  }
  if (out_model != NULL) {
    *out_model = model;
  } else {
    g_object_unref(model);
  }
  return window;
}

static HomeworldsView *test_homeworlds_get_window_view(GGameWindow *window) {
  GtkWidget *view_widget = NULL;
  HomeworldsView *view = NULL;

  g_return_val_if_fail(GGAME_IS_WINDOW(window), NULL);

  view_widget = test_homeworlds_find_widget_named(GTK_WIDGET(window), "homeworlds-view");
  g_return_val_if_fail(GTK_IS_WIDGET(view_widget), NULL);

  view = g_object_get_data(G_OBJECT(view_widget), "homeworlds-view-state");
  g_return_val_if_fail(view != NULL, NULL);
  return view;
}

static guint test_homeworlds_count_widgets_with_data(GtkWidget *root, const char *key) {
  guint count = 0;

  g_return_val_if_fail(GTK_IS_WIDGET(root), 0);
  g_return_val_if_fail(key != NULL, 0);

  if (g_object_get_data(G_OBJECT(root), key) != NULL) {
    count++;
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    count += test_homeworlds_count_widgets_with_data(child, key);
  }

  return count;
}

static GtkButton *test_homeworlds_find_selectable_bank_button(GtkWidget *root) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);

  if (GTK_IS_BUTTON(root) &&
      g_object_get_data(G_OBJECT(root), "homeworlds-board-bank-choice") != NULL &&
      gtk_widget_get_sensitive(root)) {
    return GTK_BUTTON(root);
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkButton *match = test_homeworlds_find_selectable_bank_button(child);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkButton *test_homeworlds_find_selectable_ship_button(GtkWidget *root) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);

  if (GTK_IS_BUTTON(root) &&
      g_object_get_data(G_OBJECT(root), "homeworlds-board-ship-choice") != NULL &&
      gtk_widget_get_sensitive(root)) {
    return GTK_BUTTON(root);
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkButton *match = test_homeworlds_find_selectable_ship_button(child);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkButton *test_homeworlds_find_selectable_system_button(GtkWidget *root) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);

  if (GTK_IS_BUTTON(root) &&
      g_object_get_data(G_OBJECT(root), "homeworlds-board-system-choice") != NULL &&
      gtk_widget_get_sensitive(root)) {
    return GTK_BUTTON(root);
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkButton *match = test_homeworlds_find_selectable_system_button(child);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkButton *test_homeworlds_find_cancel_choice_button(GtkWidget *root) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);

  if (GTK_IS_BUTTON(root) &&
      g_object_get_data(G_OBJECT(root), "homeworlds-cancel-choice") != NULL) {
    return GTK_BUTTON(root);
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkButton *match = test_homeworlds_find_cancel_choice_button(child);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkButton *test_homeworlds_find_bank_button_for_pyramid(GtkWidget *root, HomeworldsPyramid pyramid) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), NULL);

  if (GTK_IS_BUTTON(root) &&
      GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(root), "homeworlds-bank-pyramid")) == pyramid) {
    return GTK_BUTTON(root);
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkButton *match = test_homeworlds_find_bank_button_for_pyramid(child, pyramid);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static gboolean test_homeworlds_widget_is_visual_choice(GtkWidget *widget) {
  g_return_val_if_fail(GTK_IS_WIDGET(widget), FALSE);

  return g_object_get_data(G_OBJECT(widget), "homeworlds-board-bank-choice") != NULL ||
         g_object_get_data(G_OBJECT(widget), "homeworlds-board-ship-choice") != NULL ||
         g_object_get_data(G_OBJECT(widget), "homeworlds-board-system-choice") != NULL;
}

static guint test_homeworlds_count_non_visual_candidate_buttons(GtkWidget *root, HomeworldsCandidateKind kind) {
  guint count = 0;

  g_return_val_if_fail(GTK_IS_WIDGET(root), 0);

  if (GTK_IS_BUTTON(root) &&
      !test_homeworlds_widget_is_visual_choice(root)) {
    const HomeworldsMoveCandidate *candidate = g_object_get_data(G_OBJECT(root), "homeworlds-candidate");
    if (candidate != NULL && candidate->data.kind == kind) {
      count++;
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    count += test_homeworlds_count_non_visual_candidate_buttons(child, kind);
  }

  return count;
}

static GtkButton *test_homeworlds_find_non_visual_action_button(GtkWidget *root, HomeworldsStepKind action) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);

  if (GTK_IS_BUTTON(root) &&
      !test_homeworlds_widget_is_visual_choice(root)) {
    const HomeworldsMoveCandidate *candidate = g_object_get_data(G_OBJECT(root), "homeworlds-candidate");
    if (candidate != NULL &&
        candidate->data.kind == HOMEWORLDS_CANDIDATE_ACTION &&
        candidate->data.target_color == action) {
      return GTK_BUTTON(root);
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkButton *match = test_homeworlds_find_non_visual_action_button(child, action);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static void test_homeworlds_assert_action_button_label(GtkWidget *root,
                                                       HomeworldsStepKind action,
                                                       const char *expected_label) {
  GtkButton *button = NULL;

  g_return_if_fail(GTK_IS_WIDGET(root));
  g_return_if_fail(expected_label != NULL);

  button = test_homeworlds_find_non_visual_action_button(root, action);
  g_assert_nonnull(button);
  g_assert_cmpstr(gtk_button_get_label(button), ==, expected_label);
}

static HomeworldsMove test_homeworlds_setup_move(HomeworldsPyramid first_star,
                                                 HomeworldsPyramid second_star,
                                                 HomeworldsPyramid ship) {
  return (HomeworldsMove){
    .kind = HOMEWORLDS_MOVE_KIND_SETUP,
    .setup_stars = {first_star, second_star},
    .setup_ship = ship,
  };
}

static void test_homeworlds_prepare_play_position(HomeworldsPosition *position) {
  HomeworldsMove player_1 = test_homeworlds_setup_move(
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE));
  HomeworldsMove player_2 = test_homeworlds_setup_move(
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE));

  g_return_if_fail(position != NULL);

  homeworlds_position_init(position);
  g_assert_true(homeworlds_position_apply_move(position, &player_1));
  g_assert_true(homeworlds_position_apply_move(position, &player_2));
}

static void test_homeworlds_view_homeworld_layout_uses_player_perspective(void) {
  HomeworldsViewHomeworldLayout player_1 = {0};
  HomeworldsViewHomeworldLayout player_2 = {0};
  HomeworldsViewHomeworldLayout invalid = {0};

  g_assert_true(homeworlds_view_calculate_homeworld_layout(0, 100.0, 200.0, &player_1));
  g_assert_true(homeworlds_view_calculate_homeworld_layout(1, 100.0, 200.0, &player_2));
  g_assert_false(homeworlds_view_calculate_homeworld_layout(2, 100.0, 200.0, &invalid));

  g_assert_true(player_1.ship_points_up);
  g_assert_false(player_2.ship_points_up);
  g_assert_cmpfloat(player_1.ship_y, ==, player_1.star_y);
  g_assert_cmpfloat(player_2.ship_y, ==, player_2.star_y);
  g_assert_cmpfloat(player_1.ship_x, >, player_1.star_x[1]);
  g_assert_cmpfloat(player_2.ship_x, <, player_2.star_x[0]);
}

static void test_homeworlds_view_system_layout_groups_by_reachability(void) {
  HomeworldsPosition position = {0};
  double player_1_x = 0.0;
  double player_1_y = 0.0;
  double player_2_x = 0.0;
  double player_2_y = 0.0;
  double small_x = 0.0;
  double small_y = 0.0;
  double medium_x = 0.0;
  double medium_y = 0.0;
  double large_x = 0.0;
  double large_y = 0.0;
  double compact_a_x = 0.0;
  double compact_a_y = 0.0;
  double compact_b_x = 0.0;
  double compact_b_y = 0.0;

  position.phase = HOMEWORLDS_PHASE_PLAY;
  position.systems[0].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL);
  position.systems[0].stars[1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM);
  position.systems[1].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM);
  position.systems[1].stars[1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE);
  position.systems[2].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE);
  position.systems[3].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL);
  position.systems[4].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM);

  g_assert_true(homeworlds_view_calculate_system_center(&position, 0, 900.0, 600.0, &player_1_x, &player_1_y));
  g_assert_true(homeworlds_view_calculate_system_center(&position, 1, 900.0, 600.0, &player_2_x, &player_2_y));
  g_assert_true(homeworlds_view_calculate_system_center(&position, 2, 900.0, 600.0, &large_x, &large_y));
  g_assert_true(homeworlds_view_calculate_system_center(&position, 3, 900.0, 600.0, &small_x, &small_y));
  g_assert_true(homeworlds_view_calculate_system_center(&position, 4, 900.0, 600.0, &medium_x, &medium_y));

  g_assert_cmpfloat(player_2_y, <, small_y);
  g_assert_cmpfloat(small_y, <, medium_y);
  g_assert_cmpfloat(medium_y, <, large_y);
  g_assert_cmpfloat(large_y, <, player_1_y);

  position.systems[1].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL);
  position.systems[1].stars[1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM);
  g_assert_true(homeworlds_view_calculate_system_center(&position, 2, 900.0, 600.0, &compact_a_x, &compact_a_y));
  g_assert_true(homeworlds_view_calculate_system_center(&position, 3, 900.0, 600.0, &compact_b_x, &compact_b_y));
  g_assert_cmpfloat(compact_a_y, ==, compact_b_y);
}

static void test_homeworlds_view_piece_metrics_keep_pyramids_tall(void) {
  HomeworldsViewPyramidMetrics small = {0};
  HomeworldsViewPyramidMetrics medium = {0};
  HomeworldsViewPyramidMetrics large = {0};
  double pyramid_ratio = 0.0;
  double star_ratio = 0.0;

  g_assert_true(homeworlds_view_pyramid_metrics(HOMEWORLDS_SIZE_SMALL, &small));
  g_assert_true(homeworlds_view_pyramid_metrics(HOMEWORLDS_SIZE_MEDIUM, &medium));
  g_assert_true(homeworlds_view_pyramid_metrics(HOMEWORLDS_SIZE_LARGE, &large));

  pyramid_ratio = medium.height / small.height;
  star_ratio = homeworlds_view_star_side(HOMEWORLDS_SIZE_MEDIUM) /
               homeworlds_view_star_side(HOMEWORLDS_SIZE_SMALL);
  g_assert_cmpfloat_with_epsilon(small.height, 36.0, 0.001);
  g_assert_cmpfloat_with_epsilon(homeworlds_view_star_side(HOMEWORLDS_SIZE_SMALL), 20.0, 0.001);
  g_assert_cmpfloat_with_epsilon(homeworlds_view_pip_radius(), medium.height * 0.055, 0.001);
  g_assert_cmpfloat_with_epsilon(small.height / small.base, 2.0, 0.001);
  g_assert_cmpfloat_with_epsilon(medium.height / medium.base, 2.0, 0.001);
  g_assert_cmpfloat_with_epsilon(large.height / large.base, 2.0, 0.001);
  g_assert_cmpfloat_with_epsilon(large.height / medium.height, pyramid_ratio, 0.001);
  g_assert_cmpfloat_with_epsilon(homeworlds_view_star_side(HOMEWORLDS_SIZE_LARGE) /
                                 homeworlds_view_star_side(HOMEWORLDS_SIZE_MEDIUM),
                                 star_ratio,
                                 0.001);
  g_assert_cmpfloat(small.height, <, medium.height);
  g_assert_cmpfloat(medium.height, <, large.height);
}

static void test_homeworlds_window_replaces_skeleton(void) {
  GtkApplication *app = NULL;
  GGameModel *model = NULL;
  GGameWindow *window = NULL;
  GtkWidget *root = NULL;

  window = test_homeworlds_create_window(&app, &model);
  root = GTK_WIDGET(window);

  g_assert_nonnull(ggame_window_get_sgf_controller(window));
  g_assert_nonnull(test_homeworlds_find_widget_named(root, "homeworlds-view"));
  g_assert_nonnull(test_homeworlds_find_widget_named(root, "homeworlds-board"));
  g_assert_nonnull(test_homeworlds_find_widget_named(root, "homeworlds-board-bank"));
  g_assert_nonnull(test_homeworlds_find_widget_for_action(root, "app.new-game"));
  g_assert_nonnull(test_homeworlds_find_widget_for_action(root, "win.game-force-move"));
  g_assert_nonnull(test_homeworlds_find_widget_for_action(root, "win.navigation-step-forward"));
  g_assert_nonnull(test_homeworlds_find_widget_for_action(root, "win.navigation-step-backward"));
  g_assert_nonnull(test_homeworlds_find_widget_for_action(root, "win.navigation-rewind"));

  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);
}

static void test_homeworlds_window_defaults_to_fast_computer(void) {
  GtkApplication *app = NULL;
  GGameModel *model = NULL;
  GGameWindow *window = test_homeworlds_create_window(&app, &model);
  PlayerControlsPanel *panel = ggame_window_get_controls_panel(window);

  g_assert_nonnull(panel);
  g_assert_cmpuint(player_controls_panel_get_mode(panel, 0), ==, PLAYER_CONTROL_MODE_USER);
  g_assert_cmpuint(player_controls_panel_get_mode(panel, 1), ==, PLAYER_CONTROL_MODE_COMPUTER);
  g_assert_cmpuint(player_controls_panel_get_computer_depth(panel), ==, 0);

  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);
}

static void test_homeworlds_window_setup_moves_are_recorded_in_sgf(void) {
  GtkApplication *app = NULL;
  GGameModel *model = NULL;
  GGameWindow *window = test_homeworlds_create_window(&app, &model);
  HomeworldsView *view = test_homeworlds_get_window_view(window);
  GGameSgfController *controller = ggame_window_get_sgf_controller(window);
  SgfTree *tree = ggame_sgf_controller_get_tree(controller);
  const SgfNode *current = NULL;
  const HomeworldsPosition *position = NULL;

  g_assert_nonnull(view);
  g_assert_nonnull(controller);
  g_assert_nonnull(tree);

  for (guint i = 0; i < 6; ++i) {
    g_assert_true(homeworlds_view_apply_candidate_at(view, 0));
  }

  position = ggame_model_peek_position(model);
  g_assert_nonnull(position);
  g_assert_cmpuint(position->phase, ==, HOMEWORLDS_PHASE_PLAY);

  current = sgf_tree_get_current(tree);
  g_assert_nonnull(current);
  g_assert_cmpuint(sgf_node_get_move_number(current), ==, 2);
  g_assert_cmpstr(homeworlds_view_get_last_move_text(view), !=, "None");

  g_assert_true(ggame_sgf_controller_rewind_to_start(controller));
  g_assert_cmpstr(homeworlds_view_get_last_move_text(view), ==, "None");
  position = ggame_model_peek_position(model);
  g_assert_nonnull(position);
  g_assert_cmpuint(position->phase, ==, HOMEWORLDS_PHASE_SETUP);
  g_assert_cmpuint(position->turn, ==, 0);

  g_assert_true(ggame_sgf_controller_step_forward(controller));
  g_assert_cmpstr(homeworlds_view_get_last_move_text(view), !=, "None");
  position = ggame_model_peek_position(model);
  g_assert_nonnull(position);
  g_assert_cmpuint(position->phase, ==, HOMEWORLDS_PHASE_SETUP);
  g_assert_cmpuint(position->turn, ==, 1);

  g_assert_true(ggame_sgf_controller_step_forward(controller));
  g_assert_cmpstr(homeworlds_view_get_last_move_text(view), !=, "None");
  position = ggame_model_peek_position(model);
  g_assert_nonnull(position);
  g_assert_cmpuint(position->phase, ==, HOMEWORLDS_PHASE_PLAY);

  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);
}

static void test_homeworlds_view_setup_uses_board_bank_buttons(void) {
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = homeworlds_view_new(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  const HomeworldsPosition *position = NULL;
  HomeworldsPyramid large_star = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE);
  HomeworldsPyramid small_ship = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL);
  GtkButton *button = NULL;

  g_assert_cmpuint(test_homeworlds_count_widgets_with_data(root, "homeworlds-board-bank-choice"), ==, 12);

  button = test_homeworlds_find_bank_button_for_pyramid(root, large_star);
  g_assert_nonnull(button);
  g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(button)));
  g_assert_true(gtk_widget_has_css_class(GTK_WIDGET(button), "homeworlds-bank-choice"));
  g_signal_emit_by_name(button, "clicked");

  button = test_homeworlds_find_bank_button_for_pyramid(root, large_star);
  g_assert_nonnull(button);
  g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(button)));
  g_assert_true(gtk_widget_has_css_class(GTK_WIDGET(button), "homeworlds-bank-choice"));
  g_signal_emit_by_name(button, "clicked");

  button = test_homeworlds_find_bank_button_for_pyramid(root, small_ship);
  g_assert_nonnull(button);
  g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(button)));
  g_assert_true(gtk_widget_has_css_class(GTK_WIDGET(button), "homeworlds-bank-choice"));
  g_signal_emit_by_name(button, "clicked");

  for (guint i = 0; i < 3; ++i) {
    button = test_homeworlds_find_selectable_bank_button(root);
    g_assert_nonnull(button);
    g_signal_emit_by_name(button, "clicked");
  }

  position = ggame_model_peek_position(model);
  g_assert_nonnull(position);
  g_assert_cmpuint(position->phase, ==, HOMEWORLDS_PHASE_PLAY);

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_view_uses_board_ship_buttons(void) {
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = homeworlds_view_new(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  GtkButton *button = NULL;

  for (guint i = 0; i < 6; ++i) {
    g_assert_true(homeworlds_view_apply_candidate_at(view, 0));
  }

  g_assert_cmpuint(homeworlds_view_get_candidate_count(view), >, 0);
  g_assert_cmpuint(test_homeworlds_count_non_visual_candidate_buttons(root, HOMEWORLDS_CANDIDATE_SELECT_SHIP), ==, 0);

  button = test_homeworlds_find_selectable_ship_button(root);
  g_assert_nonnull(button);
  g_assert_true(gtk_widget_has_css_class(GTK_WIDGET(button), "homeworlds-board-choice"));
  g_signal_emit_by_name(button, "clicked");
  g_assert_true(homeworlds_view_has_partial_selection(view));

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_view_action_buttons_use_plain_labels(void) {
  HomeworldsPosition position = {0};
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = homeworlds_view_new(model);
  GtkWidget *root = homeworlds_view_get_widget(view);

  test_homeworlds_prepare_play_position(&position);
  memset(position.systems[0].ships[0], 0, sizeof(position.systems[0].ships[0]));
  position.systems[0].ships[0][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE);
  position.systems[0].ships[0][1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE);
  g_assert_true(ggame_model_set_position(model, &position));

  test_homeworlds_assert_action_button_label(root, HOMEWORLDS_STEP_PASS, "Pass");

  g_assert_true(homeworlds_view_apply_candidate_at(view, 0));
  test_homeworlds_assert_action_button_label(root, HOMEWORLDS_STEP_ATTACK, "Capture");
  test_homeworlds_assert_action_button_label(root, HOMEWORLDS_STEP_MOVE, "Move");
  test_homeworlds_assert_action_button_label(root, HOMEWORLDS_STEP_BUILD, "Build");
  test_homeworlds_assert_action_button_label(root, HOMEWORLDS_STEP_TRADE, "Trade");

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_view_choice_list_has_cancel_button(void) {
  HomeworldsPosition position = {0};
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = homeworlds_view_new(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  GtkButton *button = NULL;

  test_homeworlds_prepare_play_position(&position);
  g_assert_true(ggame_model_set_position(model, &position));
  g_assert_null(test_homeworlds_find_cancel_choice_button(root));

  g_assert_true(homeworlds_view_apply_candidate_at(view, 0));
  g_assert_true(homeworlds_view_has_partial_selection(view));

  button = test_homeworlds_find_cancel_choice_button(root);
  g_assert_nonnull(button);
  g_assert_cmpstr(gtk_button_get_label(button), ==, "Cancel");

  g_signal_emit_by_name(button, "clicked");
  g_assert_false(homeworlds_view_has_partial_selection(view));
  g_assert_null(test_homeworlds_find_cancel_choice_button(root));
  g_assert_nonnull(test_homeworlds_find_selectable_ship_button(root));

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_view_trade_targets_use_bank_buttons(void) {
  HomeworldsPosition position = {0};
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = homeworlds_view_new(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  HomeworldsPyramid blue_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE);
  GtkButton *button = NULL;

  test_homeworlds_prepare_play_position(&position);
  g_assert_true(ggame_model_set_position(model, &position));

  g_assert_true(homeworlds_view_apply_candidate_at(view, 0));
  button = test_homeworlds_find_non_visual_action_button(root, HOMEWORLDS_STEP_TRADE);
  g_assert_nonnull(button);
  g_signal_emit_by_name(button, "clicked");

  g_assert_cmpuint(test_homeworlds_count_non_visual_candidate_buttons(root, HOMEWORLDS_CANDIDATE_TRADE_COLOR), ==, 0);
  button = test_homeworlds_find_bank_button_for_pyramid(root, blue_large);
  g_assert_nonnull(button);
  g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(button)));
  g_assert_true(gtk_widget_has_css_class(GTK_WIDGET(button), "homeworlds-bank-choice"));
  g_signal_emit_by_name(button, "clicked");
  g_assert_false(homeworlds_view_has_partial_selection(view));

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_view_attack_targets_use_board_buttons(void) {
  HomeworldsPosition position = {0};
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = homeworlds_view_new(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  GtkButton *button = NULL;

  test_homeworlds_prepare_play_position(&position);
  position.systems[0].ships[1][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL);
  g_assert_true(ggame_model_set_position(model, &position));

  g_assert_true(homeworlds_view_apply_candidate_at(view, 0));
  button = test_homeworlds_find_non_visual_action_button(root, HOMEWORLDS_STEP_ATTACK);
  g_assert_nonnull(button);
  g_signal_emit_by_name(button, "clicked");

  g_assert_cmpuint(test_homeworlds_count_non_visual_candidate_buttons(root, HOMEWORLDS_CANDIDATE_ATTACK_TARGET), ==, 0);
  button = test_homeworlds_find_selectable_ship_button(root);
  g_assert_nonnull(button);
  g_assert_true(gtk_widget_has_css_class(GTK_WIDGET(button), "homeworlds-board-choice"));
  g_signal_emit_by_name(button, "clicked");
  g_assert_false(homeworlds_view_has_partial_selection(view));

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_view_move_targets_use_board_and_bank_buttons(void) {
  HomeworldsPosition position = {0};
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = homeworlds_view_new(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  HomeworldsPyramid blue_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE);
  GtkButton *button = NULL;

  test_homeworlds_prepare_play_position(&position);
  position.systems[0].ships[0][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE);
  position.systems[2].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE);
  g_assert_true(ggame_model_set_position(model, &position));

  g_assert_true(homeworlds_view_apply_candidate_at(view, 0));
  button = test_homeworlds_find_non_visual_action_button(root, HOMEWORLDS_STEP_MOVE);
  g_assert_nonnull(button);
  g_signal_emit_by_name(button, "clicked");

  g_assert_cmpuint(test_homeworlds_count_non_visual_candidate_buttons(root, HOMEWORLDS_CANDIDATE_MOVE_TARGET), ==, 0);
  button = test_homeworlds_find_selectable_system_button(root);
  g_assert_nonnull(button);
  g_assert_true(gtk_widget_has_css_class(GTK_WIDGET(button), "homeworlds-board-choice"));

  button = test_homeworlds_find_bank_button_for_pyramid(root, blue_large);
  g_assert_nonnull(button);
  g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(button)));
  g_assert_true(gtk_widget_has_css_class(GTK_WIDGET(button), "homeworlds-bank-choice"));

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_view_advances_setup(void) {
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = homeworlds_view_new(model);
  const HomeworldsPosition *position = NULL;

  for (guint i = 0; i < 6; ++i) {
    g_assert_cmpuint(homeworlds_view_get_candidate_count(view), >, 0);
    g_assert_true(homeworlds_view_apply_candidate_at(view, 0));
  }

  position = ggame_model_peek_position(model);
  g_assert_nonnull(position);
  g_assert_cmpuint(position->phase, ==, HOMEWORLDS_PHASE_PLAY);
  g_assert_true(homeworlds_system_has_star(&position->systems[0]));
  g_assert_true(homeworlds_system_has_star(&position->systems[1]));
  g_assert_cmpuint(homeworlds_system_ship_count_for_side(&position->systems[0], 0), ==, 1);
  g_assert_cmpuint(homeworlds_system_ship_count_for_side(&position->systems[1], 1), ==, 1);

  homeworlds_view_free(view);
  g_object_unref(model);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  const GGameAppProfile *profile = ggame_app_profile_get_by_kind(GGAME_APP_KIND_HOMEWORLDS);
  int result = 0;

  g_assert_nonnull(profile);
  g_assert_true(ggame_app_profile_set_active(profile));

  g_test_add_func("/homeworlds/view/homeworld-layout", test_homeworlds_view_homeworld_layout_uses_player_perspective);
  g_test_add_func("/homeworlds/view/system-layout", test_homeworlds_view_system_layout_groups_by_reachability);
  g_test_add_func("/homeworlds/view/piece-metrics", test_homeworlds_view_piece_metrics_keep_pyramids_tall);
  if (!gtk_init_check()) {
    g_test_add_func("/homeworlds/window/replaces-skeleton", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/window/defaults-to-fast-computer", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/window/setup-recorded-in-sgf", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/setup-bank-buttons", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/board-ship-buttons", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/action-button-labels", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/choice-list-cancel", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/trade-bank-buttons", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/attack-board-buttons", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/move-board-bank-buttons", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/advances-setup", test_homeworlds_window_skip);
  } else {
    g_test_add_func("/homeworlds/window/replaces-skeleton", test_homeworlds_window_replaces_skeleton);
    g_test_add_func("/homeworlds/window/defaults-to-fast-computer", test_homeworlds_window_defaults_to_fast_computer);
    g_test_add_func("/homeworlds/window/setup-recorded-in-sgf",
                    test_homeworlds_window_setup_moves_are_recorded_in_sgf);
    g_test_add_func("/homeworlds/view/setup-bank-buttons", test_homeworlds_view_setup_uses_board_bank_buttons);
    g_test_add_func("/homeworlds/view/board-ship-buttons", test_homeworlds_view_uses_board_ship_buttons);
    g_test_add_func("/homeworlds/view/action-button-labels", test_homeworlds_view_action_buttons_use_plain_labels);
    g_test_add_func("/homeworlds/view/choice-list-cancel",
                    test_homeworlds_view_choice_list_has_cancel_button);
    g_test_add_func("/homeworlds/view/trade-bank-buttons", test_homeworlds_view_trade_targets_use_bank_buttons);
    g_test_add_func("/homeworlds/view/attack-board-buttons", test_homeworlds_view_attack_targets_use_board_buttons);
    g_test_add_func("/homeworlds/view/move-board-bank-buttons",
                    test_homeworlds_view_move_targets_use_board_and_bank_buttons);
    g_test_add_func("/homeworlds/view/advances-setup", test_homeworlds_view_advances_setup);
  }

  result = g_test_run();
  g_clear_object(&test_homeworlds_app);
  return result;
}
