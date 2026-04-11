#include "application.h"

#include "checkers_model.h"
#include "window.h"

struct _GCheckersApplication {
  GtkApplication parent_instance;
};

G_DEFINE_TYPE(GCheckersApplication, gcheckers_application, GTK_TYPE_APPLICATION)

static void gcheckers_application_on_new_game(GSimpleAction * /*action*/,
                                              GVariant * /*parameter*/,
                                              gpointer user_data) {
  GCheckersApplication *self = GCHECKERS_APPLICATION(user_data);
  g_return_if_fail(GCHECKERS_IS_APPLICATION(self));

  GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(self));
  if (!window) {
    g_debug("No active window for new game action");
    return;
  }
  if (!GCHECKERS_IS_WINDOW(window)) {
    g_debug("Active window is not a gcheckers window");
    return;
  }

  gcheckers_window_present_new_game_dialog(GCHECKERS_WINDOW(window));
}

static void gcheckers_application_on_import(GSimpleAction * /*action*/,
                                            GVariant * /*parameter*/,
                                            gpointer user_data) {
  GCheckersApplication *self = GCHECKERS_APPLICATION(user_data);
  g_return_if_fail(GCHECKERS_IS_APPLICATION(self));

  GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(self));
  if (!window) {
    g_debug("No active window for import action");
    return;
  }
  if (!GCHECKERS_IS_WINDOW(window)) {
    g_debug("Active window is not a gcheckers window");
    return;
  }

  gcheckers_window_present_import_dialog(GCHECKERS_WINDOW(window));
}

static void gcheckers_application_on_quit(GSimpleAction * /*action*/,
                                          GVariant * /*parameter*/,
                                          gpointer user_data) {
  GCheckersApplication *self = GCHECKERS_APPLICATION(user_data);
  g_return_if_fail(GCHECKERS_IS_APPLICATION(self));

  g_application_quit(G_APPLICATION(self));
}

static void gcheckers_application_startup(GApplication *app) {
  G_APPLICATION_CLASS(gcheckers_application_parent_class)->startup(app);

  static const GActionEntry app_actions[] = {
      {
          .name = "new-game",
          .activate = gcheckers_application_on_new_game,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "import",
          .activate = gcheckers_application_on_import,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "quit",
          .activate = gcheckers_application_on_quit,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
  };
  g_action_map_add_action_entries(G_ACTION_MAP(app), app_actions, G_N_ELEMENTS(app_actions), app);

  GMenu *menubar = g_menu_new();
  GMenu *file_menu = g_menu_new();
  GMenu *file_primary_menu = g_menu_new();
  GMenu *file_quit_menu = g_menu_new();
  GMenu *game_menu = g_menu_new();
  GMenu *game_navigation_menu = g_menu_new();
  GMenu *analysis_menu = g_menu_new();
  GMenu *puzzle_menu = g_menu_new();
  GMenu *view_menu = g_menu_new();
  g_menu_append(file_primary_menu, "New game...", "app.new-game");
  g_menu_append(file_primary_menu, "Import...", "app.import");
  g_menu_append(file_primary_menu, "Load...", "win.sgf-load");
  g_menu_append(file_primary_menu, "Save as...", "win.sgf-save-as");
  g_menu_append(file_primary_menu, "Save position...", "win.sgf-save-position");
  g_menu_append_section(file_menu, NULL, G_MENU_MODEL(file_primary_menu));
  g_menu_append(file_quit_menu, "Quit", "app.quit");
  g_menu_append_section(file_menu, NULL, G_MENU_MODEL(file_quit_menu));
  g_menu_append(game_menu, "Force move", "win.game-force-move");
  g_menu_append(game_navigation_menu, "Rewind to start", "win.navigation-rewind");
  g_menu_append(game_navigation_menu, "Back one move", "win.navigation-step-backward");
  g_menu_append(game_navigation_menu, "Forward one move", "win.navigation-step-forward");
  g_menu_append(game_navigation_menu, "Forward to next branch", "win.navigation-step-forward-to-branch");
  g_menu_append(game_navigation_menu, "Forward to main line end", "win.navigation-step-forward-to-end");
  g_menu_append_section(game_menu, NULL, G_MENU_MODEL(game_navigation_menu));
  g_menu_append(analysis_menu, "Analyse this move", "win.analysis-current-position");
  g_menu_append(analysis_menu, "Analyse whole game", "win.analysis-whole-game");
  g_menu_append(puzzle_menu, "Play puzzles", "win.puzzle-play");
  g_menu_append(view_menu, "Show navigation drawer", "win.view-show-navigation-drawer");
  g_menu_append(view_menu, "Show analysis drawer", "win.view-show-analysis-drawer");
  g_menu_append_submenu(menubar, "File", G_MENU_MODEL(file_menu));
  g_menu_append_submenu(menubar, "Game", G_MENU_MODEL(game_menu));
  g_menu_append_submenu(menubar, "Analysis", G_MENU_MODEL(analysis_menu));
  g_menu_append_submenu(menubar, "Puzzle", G_MENU_MODEL(puzzle_menu));
  g_menu_append_submenu(menubar, "View", G_MENU_MODEL(view_menu));
  gtk_application_set_menubar(GTK_APPLICATION(app), G_MENU_MODEL(menubar));

  g_object_unref(file_quit_menu);
  g_object_unref(file_primary_menu);
  g_object_unref(game_navigation_menu);
  g_object_unref(game_menu);
  g_object_unref(analysis_menu);
  g_object_unref(puzzle_menu);
  g_object_unref(view_menu);
  g_object_unref(file_menu);
  g_object_unref(menubar);

  gtk_application_set_accels_for_action(GTK_APPLICATION(app),
                                        "app.new-game",
                                        (const char *[]){"<Primary>n", NULL});
  gtk_application_set_accels_for_action(GTK_APPLICATION(app),
                                        "app.quit",
                                        (const char *[]){"<Primary>q", NULL});
  gtk_application_set_accels_for_action(GTK_APPLICATION(app),
                                        "win.navigation-step-backward",
                                        (const char *[]){"Left", NULL});
  gtk_application_set_accels_for_action(GTK_APPLICATION(app),
                                        "win.navigation-step-forward",
                                        (const char *[]){"Right", NULL});
  gtk_application_set_accels_for_action(GTK_APPLICATION(app),
                                        "win.navigation-rewind",
                                        (const char *[]){"Home", NULL});
  gtk_application_set_accels_for_action(GTK_APPLICATION(app),
                                        "win.navigation-step-forward-to-end",
                                        (const char *[]){"End", NULL});
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
}

static void gcheckers_application_class_init(GCheckersApplicationClass *klass) {
  GApplicationClass *app_class = G_APPLICATION_CLASS(klass);

  app_class->startup = gcheckers_application_startup;
  app_class->activate = gcheckers_application_activate;
}

static void gcheckers_application_init(GCheckersApplication * /*self*/) {}

GCheckersApplication *gcheckers_application_new(void) {
  return g_object_new(GCHECKERS_TYPE_APPLICATION,
                      "application-id",
                      "io.github.JeromeA.gcheckers",
                      "flags",
                      G_APPLICATION_NON_UNIQUE,
                      NULL);
}
