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

static GtkWidget *test_player_controls_panel_find_tooltip(GtkWidget *root, const char *tooltip) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(tooltip != NULL, NULL);

  const char *root_tooltip = gtk_widget_get_tooltip_text(root);
  if (g_strcmp0(root_tooltip, tooltip) == 0) {
    return root;
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL; child = gtk_widget_get_next_sibling(child)) {
    GtkWidget *match = test_player_controls_panel_find_tooltip(child, tooltip);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static void test_player_controls_panel_defaults(void) {
  PlayerControlsPanel *panel = test_player_controls_panel_new_owned();

  g_assert_cmpuint(player_controls_panel_get_selected(panel, 0), ==, PLAYER_CONTROL_MODE_USER);
  g_assert_cmpuint(player_controls_panel_get_selected(panel, 1), ==, PLAYER_CONTROL_MODE_COMPUTER);
  g_assert_true(player_controls_panel_is_user_control(panel, 0));
  g_assert_false(player_controls_panel_is_user_control(panel, 1));
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
  player_controls_panel_set_mode(panel, 0, PLAYER_CONTROL_MODE_COMPUTER);

  g_assert_cmpuint(count, >, 0);

  g_clear_object(&panel);
}

static void test_player_controls_panel_computer_depth(void) {
  PlayerControlsPanel *panel = test_player_controls_panel_new_owned();

  player_controls_panel_set_computer_depth(panel, PLAYER_COMPUTER_DEPTH_MIN);
  g_assert_cmpuint(player_controls_panel_get_computer_depth(panel), ==, PLAYER_COMPUTER_DEPTH_MIN);

  player_controls_panel_set_computer_depth(panel, 16);
  g_assert_cmpuint(player_controls_panel_get_computer_depth(panel), ==, 16);

  g_clear_object(&panel);
}

static void on_stop_computer(PlayerControlsPanel * /*panel*/, gpointer user_data) {
  guint *count = user_data;
  g_return_if_fail(count != NULL);
  (*count)++;
}

static void test_player_controls_panel_computer_thinking_state(void) {
  PlayerControlsPanel *panel = test_player_controls_panel_new_owned();
  GtkWidget *side0 = GTK_WIDGET(player_controls_panel_get_drop_down(panel, 0));
  GtkWidget *side1 = GTK_WIDGET(player_controls_panel_get_drop_down(panel, 1));
  GtkWidget *stop_button =
      test_player_controls_panel_find_tooltip(GTK_WIDGET(panel), "Stop computer move");

  g_assert_nonnull(stop_button);
  g_assert_false(player_controls_panel_get_computer_thinking(panel));
  g_assert_true(gtk_widget_get_sensitive(side0));
  g_assert_true(gtk_widget_get_sensitive(side1));
  g_assert_false(gtk_widget_get_sensitive(stop_button));

  player_controls_panel_set_computer_thinking(panel, TRUE);
  g_assert_true(player_controls_panel_get_computer_thinking(panel));
  g_assert_false(gtk_widget_get_sensitive(side0));
  g_assert_false(gtk_widget_get_sensitive(side1));
  g_assert_true(gtk_widget_get_sensitive(stop_button));

  player_controls_panel_set_computer_thinking(panel, FALSE);
  g_assert_false(player_controls_panel_get_computer_thinking(panel));
  g_assert_true(gtk_widget_get_sensitive(side0));
  g_assert_true(gtk_widget_get_sensitive(side1));
  g_assert_false(gtk_widget_get_sensitive(stop_button));

  g_clear_object(&panel);
}

static void test_player_controls_panel_stop_signal(void) {
  PlayerControlsPanel *panel = test_player_controls_panel_new_owned();
  GtkWidget *stop_button =
      test_player_controls_panel_find_tooltip(GTK_WIDGET(panel), "Stop computer move");
  guint count = 0;

  g_assert_nonnull(stop_button);
  g_signal_connect(panel, "stop-computer", G_CALLBACK(on_stop_computer), &count);

  g_signal_emit_by_name(stop_button, "clicked");
  g_assert_cmpuint(count, ==, 1);

  g_clear_object(&panel);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  if (!gtk_init_check()) {
    g_test_add_func("/player-controls/defaults", test_player_controls_panel_skip);
    g_test_add_func("/player-controls/control-signal", test_player_controls_panel_skip);
    g_test_add_func("/player-controls/computer-depth", test_player_controls_panel_skip);
    g_test_add_func("/player-controls/computer-thinking-state", test_player_controls_panel_skip);
    g_test_add_func("/player-controls/stop-signal", test_player_controls_panel_skip);
    return g_test_run();
  }

  g_test_add_func("/player-controls/defaults", test_player_controls_panel_defaults);
  g_test_add_func("/player-controls/control-signal", test_player_controls_panel_control_signal);
  g_test_add_func("/player-controls/computer-depth", test_player_controls_panel_computer_depth);
  g_test_add_func("/player-controls/computer-thinking-state", test_player_controls_panel_computer_thinking_state);
  g_test_add_func("/player-controls/stop-signal", test_player_controls_panel_stop_signal);
  return g_test_run();
}
