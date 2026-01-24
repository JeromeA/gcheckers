#include <gtk/gtk.h>

#include "checkers_model.h"
#include "gcheckers_window.h"
#include "player_controls_panel.h"

static void test_gcheckers_window_skip(void) {
  g_test_skip("GTK display not available.");
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

static void test_gcheckers_window_unparents_controls_panel_on_dispose(void) {
  GtkApplication *app =
      gtk_application_new("org.example.gcheckers.tests", G_APPLICATION_DEFAULT_FLAGS);
  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(app, model);

  GtkWidget *panel_widget =
      test_gcheckers_window_find_by_type(GTK_WIDGET(window), PLAYER_TYPE_CONTROLS_PANEL);
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
  GtkApplication *app =
      gtk_application_new("org.example.gcheckers.tests", G_APPLICATION_DEFAULT_FLAGS);
  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(app, model);

  g_object_run_dispose(G_OBJECT(window));

  g_clear_object(&window);
  g_clear_object(&model);
  g_clear_object(&app);
}

static void test_gcheckers_window_dispose_after_panel_removed(void) {
  GtkApplication *app =
      gtk_application_new("org.example.gcheckers.tests", G_APPLICATION_DEFAULT_FLAGS);
  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(app, model);

  GtkWidget *panel_widget =
      test_gcheckers_window_find_by_type(GTK_WIDGET(window), PLAYER_TYPE_CONTROLS_PANEL);
  g_assert_nonnull(panel_widget);

  GtkWidget *parent = gtk_widget_get_parent(panel_widget);
  g_assert_true(GTK_IS_BOX(parent));
  gtk_box_remove(GTK_BOX(parent), panel_widget);

  g_object_run_dispose(G_OBJECT(window));

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
    return g_test_run();
  }

  g_test_add_func("/gcheckers-window/dispose-unparents-controls",
                  test_gcheckers_window_unparents_controls_panel_on_dispose);
  g_test_add_func("/gcheckers-window/dispose-without-panel-ref",
                  test_gcheckers_window_dispose_without_external_panel_ref);
  g_test_add_func("/gcheckers-window/dispose-after-panel-removed",
                  test_gcheckers_window_dispose_after_panel_removed);
  return g_test_run();
}
