#include <gtk/gtk.h>

#include "../src/game_model.h"
#include "../src/games/homeworlds/homeworlds_app_window.h"
#include "../src/games/homeworlds/homeworlds_backend.h"
#include "../src/games/homeworlds/homeworlds_game.h"
#include "../src/games/homeworlds/homeworlds_view.h"

static void test_homeworlds_window_skip(void) {
  g_test_skip("GTK display not available.");
}

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

static GtkLabel *test_homeworlds_find_label_with_text(GtkWidget *root, const char *text) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(text != NULL, NULL);

  if (GTK_IS_LABEL(root) && g_strcmp0(gtk_label_get_text(GTK_LABEL(root)), text) == 0) {
    return GTK_LABEL(root);
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkLabel *match = test_homeworlds_find_label_with_text(child, text);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkButton *test_homeworlds_find_button_with_label(GtkWidget *root, const char *label) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(label != NULL, NULL);

  if (GTK_IS_BUTTON(root) && g_strcmp0(gtk_button_get_label(GTK_BUTTON(root)), label) == 0) {
    return GTK_BUTTON(root);
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkButton *match = test_homeworlds_find_button_with_label(child, label);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
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
  GtkApplication *app = gtk_application_new("io.github.jeromea.ghomeworlds.test", G_APPLICATION_DEFAULT_FLAGS);
  GError *error = NULL;
  GtkWindow *window = NULL;
  GtkWidget *root = NULL;

  g_assert_true(g_application_register(G_APPLICATION(app), NULL, &error));
  g_assert_no_error(error);

  window = ghomeworlds_app_window_create(app);
  g_assert_nonnull(window);
  root = GTK_WIDGET(window);

  g_assert_null(test_homeworlds_find_label_with_text(root, "Homeworlds skeleton build"));
  g_assert_nonnull(test_homeworlds_find_widget_named(root, "homeworlds-window"));
  g_assert_nonnull(test_homeworlds_find_widget_named(root, "homeworlds-view"));
  g_assert_nonnull(test_homeworlds_find_widget_named(root, "homeworlds-board"));
  g_assert_nonnull(test_homeworlds_find_widget_named(root, "homeworlds-board-bank"));
  g_assert_nonnull(test_homeworlds_find_button_with_label(root, "Random AI"));

  gtk_window_destroy(window);
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
  g_signal_emit_by_name(button, "clicked");

  button = test_homeworlds_find_bank_button_for_pyramid(root, large_star);
  g_assert_nonnull(button);
  g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(button)));
  g_signal_emit_by_name(button, "clicked");

  button = test_homeworlds_find_bank_button_for_pyramid(root, small_ship);
  g_assert_nonnull(button);
  g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(button)));
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

static void test_homeworlds_view_applies_random_ai_after_setup(void) {
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = homeworlds_view_new(model);
  const HomeworldsPosition *position = NULL;

  for (guint i = 0; i < 6; ++i) {
    g_assert_true(homeworlds_view_apply_candidate_at(view, 0));
  }
  g_assert_true(homeworlds_view_apply_random_move(view));

  position = ggame_model_peek_position(model);
  g_assert_nonnull(position);
  g_assert_cmpuint(position->phase, !=, HOMEWORLDS_PHASE_SETUP);

  homeworlds_view_free(view);
  g_object_unref(model);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/homeworlds/view/homeworld-layout", test_homeworlds_view_homeworld_layout_uses_player_perspective);
  g_test_add_func("/homeworlds/view/piece-metrics", test_homeworlds_view_piece_metrics_keep_pyramids_tall);
  if (!gtk_init_check()) {
    g_test_add_func("/homeworlds/window/replaces-skeleton", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/setup-bank-buttons", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/advances-setup", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/random-ai-after-setup", test_homeworlds_window_skip);
  } else {
    g_test_add_func("/homeworlds/window/replaces-skeleton", test_homeworlds_window_replaces_skeleton);
    g_test_add_func("/homeworlds/view/setup-bank-buttons", test_homeworlds_view_setup_uses_board_bank_buttons);
    g_test_add_func("/homeworlds/view/advances-setup", test_homeworlds_view_advances_setup);
    g_test_add_func("/homeworlds/view/random-ai-after-setup", test_homeworlds_view_applies_random_ai_after_setup);
  }

  return g_test_run();
}
