#include "homeworlds_app_window.h"

__attribute__((weak)) GtkWindow *ghomeworlds_app_window_create(GtkApplication *app) {
  g_return_val_if_fail(app != NULL, NULL);
  return NULL;
}
