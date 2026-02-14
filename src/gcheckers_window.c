#include "gcheckers_window.h"

#include "gcheckers_sgf_controller.h"
#include "widget_utils.h"

struct _GCheckersWindow {
  GtkApplicationWindow parent_instance;
  GtkWidget *controls_panel;
  GCheckersSgfController *sgf_controller;
  guint startup_force_source_id;
  guint startup_force_remaining;
};

G_DEFINE_TYPE(GCheckersWindow, gcheckers_window, GTK_TYPE_APPLICATION_WINDOW)

static gboolean gcheckers_window_startup_force_move_cb(gpointer user_data);

static void gcheckers_window_update_control_state(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  if (!self->controls_panel) {
    g_debug("Missing controls panel while updating sensitivity\n");
  }
}

static gboolean gcheckers_window_startup_force_move_cb(gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), G_SOURCE_REMOVE);

  if (self->startup_force_remaining == 0) {
    self->startup_force_source_id = 0;
    return G_SOURCE_REMOVE;
  }
  g_debug("Startup synthetic SGF-only step executed; remaining-before-step=%u\n",
          self->startup_force_remaining);

  if (!gcheckers_sgf_controller_append_synthetic_move(self->sgf_controller)) {
    g_debug("Failed startup synthetic SGF-only step\n");
    self->startup_force_source_id = 0;
    return G_SOURCE_REMOVE;
  }
  self->startup_force_remaining--;

  if (self->startup_force_remaining == 0) {
    self->startup_force_source_id = 0;
    return G_SOURCE_REMOVE;
  }

  return G_SOURCE_CONTINUE;
}

static gboolean gcheckers_window_unparent_controls_panel(GCheckersWindow *self) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), FALSE);

  if (!self->controls_panel) {
    return TRUE;
  }

  gboolean removed = gcheckers_widget_remove_from_parent(self->controls_panel);
  if (!removed && gtk_widget_get_parent(self->controls_panel)) {
    g_debug("Failed to remove controls panel from parent during dispose\n");
    return FALSE;
  }

  return TRUE;
}

static void gcheckers_window_dispose(GObject *object) {
  GCheckersWindow *self = GCHECKERS_WINDOW(object);

  gboolean panel_removed = gcheckers_window_unparent_controls_panel(self);
  g_clear_handle_id(&self->startup_force_source_id, g_source_remove);

  gcheckers_window_unparent_controls_panel(self);

  g_clear_object(&self->sgf_controller);
  if (panel_removed) {
    g_clear_object(&self->controls_panel);
  } else {
    self->controls_panel = NULL;
  }

  G_OBJECT_CLASS(gcheckers_window_parent_class)->dispose(object);
}

static void gcheckers_window_class_init(GCheckersWindowClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = gcheckers_window_dispose;
}

static void gcheckers_window_init(GCheckersWindow *self) {
  self->startup_force_source_id = 0;
  self->startup_force_remaining = 10;

  gtk_window_set_title(GTK_WINDOW(self), "gcheckers");
  gtk_window_set_default_size(GTK_WINDOW(self), 600, 700);

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(content, 16);
  gtk_widget_set_margin_bottom(content, 16);
  gtk_widget_set_margin_start(content, 16);
  gtk_widget_set_margin_end(content, 16);
  gtk_window_set_child(GTK_WINDOW(self), content);

  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_hexpand(paned, TRUE);
  gtk_widget_set_vexpand(paned, TRUE);
  gtk_box_append(GTK_BOX(content), paned);

  GtkWidget *left_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(left_panel, FALSE);
  gtk_paned_set_start_child(GTK_PANED(paned), left_panel);
  gtk_paned_set_shrink_start_child(GTK_PANED(paned), TRUE);

  GtkWidget *right_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_hexpand(right_panel, TRUE);
  gtk_widget_set_vexpand(right_panel, TRUE);
  gtk_paned_set_end_child(GTK_PANED(paned), right_panel);
  gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);

  self->controls_panel = g_object_ref_sink(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  gtk_box_append(GTK_BOX(right_panel), self->controls_panel);

  self->sgf_controller = gcheckers_sgf_controller_new();
  GtkWidget *sgf_widget = gcheckers_sgf_controller_get_widget(self->sgf_controller);
  g_return_if_fail(sgf_widget != NULL);
  gtk_box_append(GTK_BOX(right_panel), sgf_widget);

  for (guint i = 0; i < self->startup_force_remaining; ++i) {
    if (!gcheckers_sgf_controller_append_synthetic_move(self->sgf_controller)) {
      g_debug("Failed to seed synthetic SGF tree for repro\n");
      break;
    }
  }

  self->startup_force_source_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                                                   gcheckers_window_startup_force_move_cb,
                                                   g_object_ref(self),
                                                   (GDestroyNotify)g_object_unref);

  gcheckers_window_update_control_state(self);
}

GCheckersWindow *gcheckers_window_new(GtkApplication *app) {
  g_return_val_if_fail(GTK_IS_APPLICATION(app), NULL);

  return g_object_new(GCHECKERS_TYPE_WINDOW, "application", app, NULL);
}
