#include <gtk/gtk.h>

#include "player_controls_panel.h"

static void test_player_controls_panel_skip(void) {
  g_test_skip("GTK display not available.");
}

static PlayerControlsPanel *test_player_controls_panel_new_owned(void) {
  PlayerControlsPanel *panel = player_controls_panel_new();
  g_return_val_if_fail(PLAYER_IS_CONTROLS_PANEL(panel), NULL);

  g_object_ref_sink(panel);
  return panel;
}

static void test_player_controls_panel_defaults(void) {
  PlayerControlsPanel *panel = test_player_controls_panel_new_owned();

  g_assert_cmpuint(player_controls_panel_get_selected(panel, CHECKERS_COLOR_WHITE), ==, PLAYER_CONTROL_MODE_USER);
  g_assert_cmpuint(player_controls_panel_get_selected(panel, CHECKERS_COLOR_BLACK), ==, PLAYER_CONTROL_MODE_USER);
  g_assert_true(player_controls_panel_is_user_control(panel, CHECKERS_COLOR_WHITE));
  g_assert_true(player_controls_panel_is_user_control(panel, CHECKERS_COLOR_BLACK));
  g_assert_cmpuint(player_controls_panel_get_computer_depth(panel), ==, PLAYER_COMPUTER_DEPTH_DEFAULT);

  g_clear_object(&panel);
}

static void on_control_changed(PlayerControlsPanel * /*panel*/, gpointer user_data) {
  guint *count = user_data;
  g_return_if_fail(count != NULL);
  (*count)++;
}

static void test_player_controls_panel_control_signal(void) {
  PlayerControlsPanel *panel = test_player_controls_panel_new_owned();
  guint count = 0;

  g_signal_connect(panel, "control-changed", G_CALLBACK(on_control_changed), &count);
  player_controls_panel_set_mode(panel, CHECKERS_COLOR_BLACK, PLAYER_CONTROL_MODE_COMPUTER);

  g_assert_cmpuint(count, >, 0);

  g_clear_object(&panel);
}

static void test_player_controls_panel_computer_depth(void) {
  PlayerControlsPanel *panel = test_player_controls_panel_new_owned();

  player_controls_panel_set_computer_depth(panel, 0);
  g_assert_cmpuint(player_controls_panel_get_computer_depth(panel), ==, 0);

  player_controls_panel_set_computer_depth(panel, 16);
  g_assert_cmpuint(player_controls_panel_get_computer_depth(panel), ==, 16);

  g_clear_object(&panel);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  if (!gtk_init_check()) {
    g_test_add_func("/player-controls/defaults", test_player_controls_panel_skip);
    g_test_add_func("/player-controls/control-signal", test_player_controls_panel_skip);
    g_test_add_func("/player-controls/computer-depth", test_player_controls_panel_skip);
    return g_test_run();
  }

  g_test_add_func("/player-controls/defaults", test_player_controls_panel_defaults);
  g_test_add_func("/player-controls/control-signal", test_player_controls_panel_control_signal);
  g_test_add_func("/player-controls/computer-depth", test_player_controls_panel_computer_depth);
  return g_test_run();
}
