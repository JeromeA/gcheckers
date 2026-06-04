#include <gtk/gtk.h>

static void create_and_detroy_window(void) {
  /* Create window. */
  GtkWindow *window = GTK_WINDOW(gtk_window_new());

  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_window_set_child(window, paned);

  GtkWidget *drawer_host = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  gtk_paned_set_end_child(GTK_PANED(paned), drawer_host);

  gtk_window_present(window);

  /* Drain main context. */
  for (guint i = 0; i < 256; ++i) {
    g_main_context_iteration(NULL, FALSE);
  }

  /* Destroy window. */
  gtk_window_destroy(window);
}

int main(void) {
  gtk_init();

  create_and_detroy_window();
  create_and_detroy_window();
}
