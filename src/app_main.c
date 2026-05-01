#include "app_main.h"

#include "application.h"

int ggame_app_main_run(int argc, char **argv, const GGameAppProfile *profile) {
  g_return_val_if_fail(profile != NULL, 1);
  g_return_val_if_fail(ggame_app_profile_set_active(profile), 1);

  GGameApplication *app = ggame_application_new();
  g_return_val_if_fail(GGAME_IS_APPLICATION(app), 1);

  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
