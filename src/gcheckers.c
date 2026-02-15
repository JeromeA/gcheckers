#include "gcheckers_application.h"

int main(int argc, char **argv) {
  gint exit_after_seconds = 0;
  gint auto_force_moves = 0;
  GOptionEntry options[] = {
    {
      "exit-after-seconds",
      0,
      0,
      G_OPTION_ARG_INT,
      &exit_after_seconds,
      "Exit the program after N seconds",
      "N"
    },
    {
      "auto-force-moves",
      0,
      0,
      G_OPTION_ARG_INT,
      &auto_force_moves,
      "Automatically press Force move N times",
      "N"
    },
    {NULL, 0, 0, 0, NULL, NULL, NULL}
  };

  g_autoptr(GOptionContext) context = g_option_context_new(NULL);
  g_option_context_set_ignore_unknown_options(context, TRUE);
  g_option_context_add_main_entries(context, options, NULL);

  g_autoptr(GError) error = NULL;
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("%s\n", error->message);
    return 1;
  }

  if (exit_after_seconds < 0 || auto_force_moves < 0) {
    g_printerr("Automation options must be non-negative.\n");
    return 1;
  }

  GCheckersApplication *app = gcheckers_application_new();
  gcheckers_application_configure_automation(app, (guint)exit_after_seconds, (guint)auto_force_moves);

  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
