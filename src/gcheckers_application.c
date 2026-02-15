#include "gcheckers_application.h"

#include "checkers_model.h"
#include "gcheckers_window.h"

struct _GCheckersApplication {
  GtkApplication parent_instance;
  guint exit_after_seconds;
  guint force_move_presses;
};

G_DEFINE_TYPE(GCheckersApplication, gcheckers_application, GTK_TYPE_APPLICATION)

static void gcheckers_application_activate(GApplication *app) {
  GtkWindow *existing = gtk_application_get_active_window(GTK_APPLICATION(app));
  if (existing) {
    gtk_window_present(existing);
    return;
  }

  GCheckersModel *model = gcheckers_model_new();
  GCheckersWindow *window = gcheckers_window_new(GTK_APPLICATION(app), model);
  g_object_unref(model);

  GCheckersApplication *self = GCHECKERS_APPLICATION(app);
  gcheckers_window_configure_automation(window, self->exit_after_seconds, self->force_move_presses);
  gtk_window_present(GTK_WINDOW(window));
}

static void gcheckers_application_class_init(GCheckersApplicationClass *klass) {
  GApplicationClass *app_class = G_APPLICATION_CLASS(klass);

  app_class->activate = gcheckers_application_activate;
}

static void gcheckers_application_init(GCheckersApplication *self) {
  self->exit_after_seconds = 0;
  self->force_move_presses = 0;
}

GCheckersApplication *gcheckers_application_new(void) {
  return g_object_new(GCHECKERS_TYPE_APPLICATION,
                      "application-id",
                      "com.example.gcheckers",
                      "flags",
                      G_APPLICATION_DEFAULT_FLAGS,
                      NULL);
}

void gcheckers_application_configure_automation(GCheckersApplication *self,
                                                guint exit_after_seconds,
                                                guint force_move_presses) {
  g_return_if_fail(GCHECKERS_IS_APPLICATION(self));

  self->exit_after_seconds = exit_after_seconds;
  self->force_move_presses = force_move_presses;
}
