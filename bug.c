#include <gtk/gtk.h>

#include "game_app_profile.h"
#include "game_model.h"
#include "games/homeworlds/homeworlds_backend.h"
#include "window.h"

static GtkApplication *test_homeworlds_app = NULL;

static void drain_main_context(void) {
  for (guint i = 0; i < 64; ++i) {
    g_main_context_iteration(NULL, FALSE);
  }
}

static GGameWindow *create_window(GtkApplication **out_app, GGameModel **out_model) {
  if (test_homeworlds_app == NULL) {
    test_homeworlds_app = gtk_application_new("io.github.jeromea.ghomeworlds.test",
                                              G_APPLICATION_DEFAULT_FLAGS | G_APPLICATION_NON_UNIQUE);
    g_application_register(G_APPLICATION(test_homeworlds_app), NULL, NULL);
  }

  GtkApplication *app = g_object_ref(test_homeworlds_app);
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  GGameWindow *window = ggame_window_new(app, model);

  *out_app = app;
  *out_model = model;
  return window;
}

int main(void) {
  GtkApplication *app = NULL;
  GGameModel *model = NULL;
  GGameWindow *window = NULL;

  g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL);

  gtk_init();
  ggame_app_profile_set_active(ggame_app_profile_get_by_kind(GGAME_APP_KIND_HOMEWORLDS));

  window = create_window(&app, &model);
  gtk_window_present(GTK_WINDOW(window));
  drain_main_context();
  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);

  window = create_window(&app, &model);
  gtk_window_set_default_size(GTK_WINDOW(window), 2000, 700);
  gtk_window_present(GTK_WINDOW(window));
  drain_main_context();
  gtk_paned_set_position(GTK_PANED(g_object_get_data(G_OBJECT(window), "main-paned")), 1400);
  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);
}
