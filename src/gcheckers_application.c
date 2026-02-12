#include "gcheckers_application.h"

#include "checkers_model.h"
#include "gcheckers_window.h"

struct _GCheckersApplication {
  GtkApplication parent_instance;
};

G_DEFINE_TYPE(GCheckersApplication, gcheckers_application, GTK_TYPE_APPLICATION)

static gboolean gcheckers_application_quit_after_delay_cb(gpointer user_data) {
  GApplication *app = G_APPLICATION(user_data);

  g_return_val_if_fail(G_IS_APPLICATION(app), G_SOURCE_REMOVE);

  g_application_quit(app);
  return G_SOURCE_REMOVE;
}

static void gcheckers_application_activate(GApplication *app) {
  GtkWindow *existing = gtk_application_get_active_window(GTK_APPLICATION(app));
  if (existing) {
    gtk_window_present(existing);
    return;
  }

  GCheckersModel *model = gcheckers_model_new();
  GtkWindow *window = GTK_WINDOW(gcheckers_window_new(GTK_APPLICATION(app), model));
  g_object_unref(model);

  gtk_window_present(window);

  g_timeout_add_full(G_PRIORITY_DEFAULT,
                     2000,
                     gcheckers_application_quit_after_delay_cb,
                     g_object_ref(app),
                     (GDestroyNotify)g_object_unref);
}

static void gcheckers_application_class_init(GCheckersApplicationClass *klass) {
  GApplicationClass *app_class = G_APPLICATION_CLASS(klass);

  app_class->activate = gcheckers_application_activate;
}

static void gcheckers_application_init(GCheckersApplication * /*self*/) {}

GCheckersApplication *gcheckers_application_new(void) {
  return g_object_new(GCHECKERS_TYPE_APPLICATION,
                      "application-id",
                      "com.example.gcheckers",
                      "flags",
                      G_APPLICATION_DEFAULT_FLAGS,
                      NULL);
}
