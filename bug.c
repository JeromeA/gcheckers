#include <gtk/gtk.h>

static GtkWidget *drawer_host;

static GtkWindow *create_window(void) {
  GtkWindow *window = GTK_WINDOW(gtk_window_new());

  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_window_set_child(window, paned);

  GtkWidget *left_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_paned_set_start_child(GTK_PANED(paned), left_panel);

  drawer_host = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  g_object_ref_sink(drawer_host);
  gtk_paned_set_end_child(GTK_PANED(paned), drawer_host);

  GtkWidget *middle_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  gtk_paned_set_end_child(GTK_PANED(paned), NULL);
  gtk_box_append(GTK_BOX(drawer_host), middle_panel);
  gtk_paned_set_end_child(GTK_PANED(paned), drawer_host);
  gtk_paned_set_position(GTK_PANED(paned), 960);

  return window;
}

static void destroy_window(GtkWindow *window) {
  gtk_box_remove(GTK_BOX(drawer_host), gtk_widget_get_first_child(drawer_host));
  gtk_paned_set_end_child(GTK_PANED(gtk_widget_get_parent(drawer_host)), NULL);
  g_clear_object(&drawer_host);
  gtk_window_destroy(window);
}

static void drain_main_context(void) {
  for (guint i = 0; i < 256; ++i) {
    g_main_context_iteration(NULL, FALSE);
  }
}

int main(void) {
  gtk_init();

  GtkWindow *window = create_window();
  gtk_window_present(window);
  drain_main_context();
  destroy_window(window);

  window = create_window();
  gtk_window_set_default_size(window, 2000, 700);
  gtk_window_present(window);
  drain_main_context();
  gtk_paned_set_position(GTK_PANED(gtk_window_get_child(window)), 1400);
  destroy_window(window);
}
