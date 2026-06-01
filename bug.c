#include <gtk/gtk.h>

#include "src/game_app_profile.h"
#include "src/games/homeworlds/homeworlds_backend.h"
#include "src/window.h"

static GtkApplication *test_homeworlds_app = NULL;

static void test_homeworlds_drain_main_context(void) {
  while (g_main_context_iteration(NULL, FALSE)) {
  }
}

static GGameWindow *test_homeworlds_create_window(GtkApplication **out_app, GGameModel **out_model) {
  *out_app = g_object_ref(test_homeworlds_app);
  *out_model = ggame_model_new(&homeworlds_game_backend);

  return ggame_window_new(*out_app, *out_model);
}

int main(void) {
  GtkApplication *app;
  GGameModel *model;
  GGameWindow *window;

  g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL);

  gtk_init();

  ggame_app_profile_set_active(ggame_app_profile_get_by_kind(GGAME_APP_KIND_HOMEWORLDS));
  test_homeworlds_app = gtk_application_new("io.github.jeromea.ghomeworlds.test",
                                            G_APPLICATION_DEFAULT_FLAGS | G_APPLICATION_NON_UNIQUE);
  g_application_register(G_APPLICATION(test_homeworlds_app), NULL, NULL);

  window = test_homeworlds_create_window(&app, &model);
  gtk_window_present(GTK_WINDOW(window));
  test_homeworlds_drain_main_context();
  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);

  window = test_homeworlds_create_window(&app, &model);
  gtk_window_present(GTK_WINDOW(window));
  test_homeworlds_drain_main_context();
  gtk_paned_set_position(GTK_PANED(g_object_get_data(G_OBJECT(window), "main-paned")), 1400);
  test_homeworlds_drain_main_context();
  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);
}
