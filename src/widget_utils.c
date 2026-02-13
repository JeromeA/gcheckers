#include "widget_utils.h"

static gboolean gcheckers_widget_remove_from_overlay(GtkWidget *parent, GtkWidget *widget) {
  g_return_val_if_fail(GTK_IS_OVERLAY(parent), FALSE);
  g_return_val_if_fail(GTK_IS_WIDGET(widget), FALSE);

  GtkOverlay *overlay = GTK_OVERLAY(parent);
  GtkWidget *child = gtk_overlay_get_child(overlay);
  if (child == widget) {
    gtk_overlay_set_child(overlay, NULL);
    return TRUE;
  }

  gtk_overlay_remove_overlay(overlay, widget);
  return TRUE;
}

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

  if (GTK_IS_OVERLAY(parent)) {
    return gcheckers_widget_remove_from_overlay(parent, widget);
  }


  g_debug("Unsupported parent type %s when removing widget\n", G_OBJECT_TYPE_NAME(parent));
  return FALSE;
}
