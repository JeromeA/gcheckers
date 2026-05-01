#include "homeworlds_app_window.h"

#include <gtk/gtk.h>

GtkWindow *ghomeworlds_app_window_create(GtkApplication *app) {
  g_return_val_if_fail(GTK_IS_APPLICATION(app), NULL);

  GtkWidget *window = gtk_application_window_new(app);
  g_return_val_if_fail(GTK_IS_APPLICATION_WINDOW(window), NULL);
  gtk_window_set_title(GTK_WINDOW(window), "Homeworlds");
  gtk_window_set_default_size(GTK_WINDOW(window), 720, 480);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 24);
  gtk_widget_set_margin_bottom(box, 24);
  gtk_widget_set_margin_start(box, 24);
  gtk_widget_set_margin_end(box, 24);
  gtk_window_set_child(GTK_WINDOW(window), box);

  GtkWidget *title = gtk_label_new("Homeworlds skeleton build");
  gtk_widget_add_css_class(title, "title-2");
  gtk_box_append(GTK_BOX(box), title);

  GtkWidget *body = gtk_label_new("The Homeworlds launcher now selects its profile at runtime.\n"
                                  "Gameplay integration is not implemented yet.");
  gtk_label_set_wrap(GTK_LABEL(body), TRUE);
  gtk_label_set_xalign(GTK_LABEL(body), 0.0f);
  gtk_box_append(GTK_BOX(box), body);

  return GTK_WINDOW(window);
}
