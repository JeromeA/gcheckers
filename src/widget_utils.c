#include "widget_utils.h"

gboolean gcheckers_widget_remove_from_parent(GtkWidget *widget) {
  g_return_val_if_fail(GTK_IS_WIDGET(widget), FALSE);

  GtkWidget *parent = gtk_widget_get_parent(widget);
  if (!parent) {
    return TRUE;
  }

  if (GTK_IS_BOX(parent)) {
    gtk_box_remove(GTK_BOX(parent), widget);
    return TRUE;
  }

  if (GTK_IS_GRID(parent)) {
    gtk_grid_remove(GTK_GRID(parent), widget);
    return TRUE;
  }

  g_debug("Unsupported parent type %s when removing widget\n", G_OBJECT_TYPE_NAME(parent));
  return FALSE;
}
