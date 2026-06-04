#include <gtk/gtk.h>

static gboolean ggame_widget_remove_from_parent(GtkWidget *widget) {
  g_return_val_if_fail(GTK_IS_WIDGET(widget), FALSE);

  GtkWidget *parent = gtk_widget_get_parent(widget);
  if (!parent) {
    return TRUE;
  }

  if (GTK_IS_BOX(parent)) {
    gtk_box_remove(GTK_BOX(parent), widget);
    return TRUE;
  }

  if (GTK_IS_PANED(parent)) {
    GtkPaned *paned = GTK_PANED(parent);
    if (gtk_paned_get_start_child(paned) == widget) {
      gtk_paned_set_start_child(paned, NULL);
      return TRUE;
    }
    if (gtk_paned_get_end_child(paned) == widget) {
      gtk_paned_set_end_child(paned, NULL);
      return TRUE;
    }

    g_debug("Widget was not a paned child during removal\n");
    return FALSE;
  }

  g_debug("Unsupported parent type %s when removing widget\n", G_OBJECT_TYPE_NAME(parent));
  return FALSE;
}

#define GGAME_TYPE_WINDOW (ggame_window_get_type())

G_DECLARE_FINAL_TYPE(GGameWindow, ggame_window, GGAME, WINDOW, GtkApplicationWindow)

struct _GGameWindow {
  GtkApplicationWindow parent_instance;
  GtkWidget *main_paned;
  GtkWidget *drawer_host;
  GtkWidget *navigation_panel;
};

G_DEFINE_TYPE(GGameWindow, ggame_window, GTK_TYPE_APPLICATION_WINDOW)

static void ggame_window_dispose(GObject *object) {
  GGameWindow *self = GGAME_WINDOW(object);

  if (self->navigation_panel != NULL) {
    ggame_widget_remove_from_parent(self->navigation_panel);
    g_clear_object(&self->navigation_panel);
  }
  if (self->drawer_host != NULL) {
    ggame_widget_remove_from_parent(self->drawer_host);
    g_clear_object(&self->drawer_host);
  }
  G_OBJECT_CLASS(ggame_window_parent_class)->dispose(object);
}

static void ggame_window_class_init(GGameWindowClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = ggame_window_dispose;
}

static void ggame_window_init(GGameWindow *self) {
  gtk_window_set_default_size(GTK_WINDOW(self), 1260, 700);

  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_window_set_child(GTK_WINDOW(self), paned);
  self->main_paned = paned;

  GtkWidget *left_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_paned_set_start_child(GTK_PANED(paned), left_panel);

  GtkWidget *drawer_host = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  g_object_ref_sink(drawer_host);
  gtk_paned_set_end_child(GTK_PANED(paned), drawer_host);
  self->drawer_host = drawer_host;

  GtkWidget *middle_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  g_object_ref_sink(middle_panel);
  self->navigation_panel = middle_panel;

  ggame_widget_remove_from_parent(drawer_host);
  gtk_box_append(GTK_BOX(drawer_host), middle_panel);
  gtk_paned_set_end_child(GTK_PANED(paned), drawer_host);
  gtk_widget_set_size_request(left_panel, 760, -1);
  gtk_paned_set_position(GTK_PANED(paned), 960);
}

static GGameWindow *ggame_window_new(GtkApplication *app) {
  g_return_val_if_fail(GTK_IS_APPLICATION(app), NULL);

  return g_object_new(GGAME_TYPE_WINDOW, "application", app, NULL);
}

static void drain_main_context(void) {
  for (guint i = 0; i < 256; ++i) {
    g_main_context_iteration(NULL, FALSE);
  }
}

int main(void) {
  g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL);

  gtk_init();

  GtkApplication *app = gtk_application_new("io.github.jeromea.ghomeworlds.test",
                                            G_APPLICATION_DEFAULT_FLAGS | G_APPLICATION_NON_UNIQUE);
  g_application_register(G_APPLICATION(app), NULL, NULL);

  GGameWindow *window = ggame_window_new(app);
  gtk_window_present(GTK_WINDOW(window));
  drain_main_context();
  gtk_window_destroy(GTK_WINDOW(window));

  window = ggame_window_new(app);
  gtk_window_set_default_size(GTK_WINDOW(window), 2000, 700);
  gtk_window_present(GTK_WINDOW(window));
  drain_main_context();
  gtk_paned_set_position(GTK_PANED(window->main_paned), 1400);
  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(app);
}
