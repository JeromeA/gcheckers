#include <gtk/gtk.h>

static void gboop_on_activate(GtkApplication *app, gpointer /*user_data*/) {
  g_return_if_fail(GTK_IS_APPLICATION(app));

  GtkWidget *window = gtk_application_window_new(app);
  g_return_if_fail(GTK_IS_APPLICATION_WINDOW(window));
  gtk_window_set_title(GTK_WINDOW(window), "Boop");
  gtk_window_set_default_size(GTK_WINDOW(window), 720, 480);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 24);
  gtk_widget_set_margin_bottom(box, 24);
  gtk_widget_set_margin_start(box, 24);
  gtk_widget_set_margin_end(box, 24);
  gtk_window_set_child(GTK_WINDOW(window), box);

  GtkWidget *title = gtk_label_new("Boop skeleton build");
  gtk_widget_add_css_class(title, "title-2");
  gtk_box_append(GTK_BOX(box), title);

  GtkWidget *body = gtk_label_new("Milestone 1 wires build-time GAME=boop selection and a stub backend.\n"
                                  "Gameplay integration is not implemented yet.");
  gtk_label_set_wrap(GTK_LABEL(body), TRUE);
  gtk_label_set_xalign(GTK_LABEL(body), 0.0f);
  gtk_box_append(GTK_BOX(box), body);

  gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
  GtkApplication *app = gtk_application_new("io.github.jeromea.gboop", G_APPLICATION_DEFAULT_FLAGS);
  int status = 0;

  g_return_val_if_fail(GTK_IS_APPLICATION(app), 1);

  g_signal_connect(app, "activate", G_CALLBACK(gboop_on_activate), NULL);
  status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
