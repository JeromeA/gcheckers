/* This crash reproducer embeds reduced window code below. Do not link src/window.c here. */
#include <gtk/gtk.h>

/* Begin copied file: src/window.c */
#include "window.h"

#include "widget_utils.h"

struct _GGameWindow {
  GtkApplicationWindow parent_instance;
  GtkWidget *main_paned;
  GtkWidget *drawer_host;
  GtkWidget *drawer_split;
  GtkWidget *navigation_panel;
  GtkWidget *analysis_panel;
};

G_DEFINE_TYPE(GGameWindow, ggame_window, GTK_TYPE_APPLICATION_WINDOW)

enum {
  GGAME_WINDOW_DEFAULT_HEIGHT = 700,
};

static void ggame_window_dispose(GObject *object) {
  GGameWindow *self = GGAME_WINDOW(object);

  if (self->navigation_panel != NULL) {
    ggame_widget_remove_from_parent(self->navigation_panel);
    g_clear_object(&self->navigation_panel);
  }
  if (self->analysis_panel != NULL) {
    ggame_widget_remove_from_parent(self->analysis_panel);
    g_clear_object(&self->analysis_panel);
  }
  if (self->drawer_split != NULL) {
    ggame_widget_remove_from_parent(self->drawer_split);
    g_clear_object(&self->drawer_split);
  }
  if (self->drawer_host != NULL) {
    ggame_widget_remove_from_parent(self->drawer_host);
    g_clear_object(&self->drawer_host);
  }
  self->main_paned = NULL;
  G_OBJECT_CLASS(ggame_window_parent_class)->dispose(object);
}

static void ggame_window_class_init(GGameWindowClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = ggame_window_dispose;
}

static void ggame_window_init(GGameWindow *self) {
  gtk_window_set_default_size(GTK_WINDOW(self), 1260, GGAME_WINDOW_DEFAULT_HEIGHT);

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_window_set_child(GTK_WINDOW(self), content);

  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_hexpand(paned, TRUE);
  gtk_widget_set_vexpand(paned, TRUE);
  gtk_box_append(GTK_BOX(content), paned);
  self->main_paned = paned;
  g_object_set_data(G_OBJECT(self), "main-paned", paned);

  GtkWidget *left_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(left_panel, TRUE);
  gtk_widget_set_vexpand(left_panel, TRUE);
  gtk_paned_set_start_child(GTK_PANED(paned), left_panel);
  gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);

  GtkWidget *right_split = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  g_object_ref_sink(right_split);
  gtk_widget_set_hexpand(right_split, TRUE);
  gtk_widget_set_vexpand(right_split, TRUE);
  self->drawer_split = right_split;

  GtkWidget *drawer_host = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  g_object_ref_sink(drawer_host);
  gtk_widget_set_hexpand(drawer_host, TRUE);
  gtk_widget_set_vexpand(drawer_host, TRUE);
  gtk_paned_set_end_child(GTK_PANED(paned), drawer_host);
  gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
  self->drawer_host = drawer_host;

  GtkWidget *middle_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  g_object_ref_sink(middle_panel);
  gtk_widget_set_hexpand(middle_panel, TRUE);
  gtk_widget_set_vexpand(middle_panel, TRUE);
  gtk_paned_set_start_child(GTK_PANED(right_split), middle_panel);
  gtk_paned_set_shrink_start_child(GTK_PANED(right_split), FALSE);
  self->navigation_panel = middle_panel;

  GtkWidget *analysis_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  g_object_ref_sink(analysis_panel);
  gtk_widget_set_hexpand(analysis_panel, TRUE);
  gtk_widget_set_vexpand(analysis_panel, TRUE);
  gtk_paned_set_end_child(GTK_PANED(right_split), analysis_panel);
  gtk_paned_set_shrink_end_child(GTK_PANED(right_split), FALSE);
  self->analysis_panel = analysis_panel;

  ggame_widget_remove_from_parent(middle_panel);
  ggame_widget_remove_from_parent(analysis_panel);
  ggame_widget_remove_from_parent(drawer_host);
  ggame_widget_remove_from_parent(right_split);
  gtk_box_append(GTK_BOX(drawer_host), middle_panel);
  gtk_paned_set_end_child(GTK_PANED(paned), drawer_host);
  gtk_widget_set_size_request(left_panel, 760, -1);
  gtk_paned_set_position(GTK_PANED(paned), 960);
  gtk_paned_set_position(GTK_PANED(right_split), 300);
}

GGameWindow *ggame_window_new(GtkApplication *app, GGameModel * /*model*/) {
  g_return_val_if_fail(GTK_IS_APPLICATION(app), NULL);

  return g_object_new(GGAME_TYPE_WINDOW, "application", app, NULL);
}
/* End copied file: src/window.c */

static void drain_main_context(void) {
  for (guint i = 0; i < 256; ++i) {
    g_main_context_iteration(NULL, FALSE);
  }
}

int main(void) {
  GtkApplication *app = NULL;
  GGameWindow *window = NULL;

  g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL);

  gtk_init();

  app = gtk_application_new("io.github.jeromea.ghomeworlds.test",
                            G_APPLICATION_DEFAULT_FLAGS | G_APPLICATION_NON_UNIQUE);
  g_application_register(G_APPLICATION(app), NULL, NULL);

  window = ggame_window_new(app, NULL);
  gtk_window_present(GTK_WINDOW(window));
  drain_main_context();
  gtk_window_destroy(GTK_WINDOW(window));

  window = ggame_window_new(app, NULL);
  gtk_window_set_default_size(GTK_WINDOW(window), 2000, 700);
  gtk_window_present(GTK_WINDOW(window));
  drain_main_context();
  gtk_paned_set_position(GTK_PANED(g_object_get_data(G_OBJECT(window), "main-paned")), 1400);
  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(app);
}
