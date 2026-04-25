#include "application.h"

#include "app_settings.h"
#include "app_paths.h"
#include "games/checkers/checkers_model.h"
#include "window.h"

#include <curl/curl.h>

typedef struct {
  CheckersPuzzleProgressStore *store;
  char *report_url;
} GCheckersApplicationUploadTaskData;

struct _GCheckersApplication {
  GtkApplication parent_instance;
  CheckersPuzzleProgressStore *puzzle_progress_store;
  char *puzzle_report_url;
  guint puzzle_progress_startup_flush_source_id;
  gboolean puzzle_progress_upload_active;
  gboolean puzzle_progress_upload_pending;
  gboolean curl_initialized;
};

G_DEFINE_TYPE(GCheckersApplication, gcheckers_application, GTK_TYPE_APPLICATION)

static size_t gcheckers_application_discard_upload_response(char *ptr,
                                                            size_t size,
                                                            size_t nmemb,
                                                            void * /*user_data*/) {
  g_return_val_if_fail(ptr != NULL || size == 0 || nmemb == 0, 0);
  return size * nmemb;
}

static void gcheckers_application_upload_task_data_free(GCheckersApplicationUploadTaskData *task_data) {
  if (task_data == NULL) {
    return;
  }

  if (task_data->store != NULL) {
    checkers_puzzle_progress_store_unref(task_data->store);
  }
  g_clear_pointer(&task_data->report_url, g_free);
  g_free(task_data);
}

static void gcheckers_application_run_puzzle_progress_upload(GTask *task,
                                                             gpointer /*source_object*/,
                                                             gpointer task_data,
                                                             GCancellable * /*cancellable*/) {
  GCheckersApplicationUploadTaskData *upload_data = task_data;
  g_return_if_fail(upload_data != NULL);
  g_return_if_fail(upload_data->store != NULL);

  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) history =
      checkers_puzzle_progress_store_load_attempt_history(upload_data->store, &error);
  if (history == NULL) {
    g_task_return_error(task, g_steal_pointer(&error));
    return;
  }

  if (!checkers_puzzle_progress_should_send_report(history, g_get_real_time() / 1000)) {
    g_task_return_boolean(task, TRUE);
    return;
  }

  g_autoptr(GSettings) settings = gcheckers_app_settings_create();
  if (G_IS_SETTINGS(settings) &&
      !g_settings_get_boolean(settings, GCHECKERS_APP_SETTINGS_KEY_SEND_PUZZLE_USAGE)) {
    g_task_return_boolean(task, TRUE);
    return;
  }

  g_autofree char *user_id =
      checkers_puzzle_progress_store_get_or_create_user_id(upload_data->store, &error);
  if (user_id == NULL) {
    g_task_return_error(task, g_steal_pointer(&error));
    return;
  }

  g_autofree char *payload = checkers_puzzle_progress_build_upload_json(user_id, history);
  if (payload == NULL) {
    g_task_return_new_error(task,
                            G_IO_ERROR,
                            G_IO_ERROR_FAILED,
                            "Failed to build puzzle progress upload payload");
    return;
  }

  CURL *curl = curl_easy_init();
  if (curl == NULL) {
    g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to initialize libcurl");
    return;
  }

  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_URL, upload_data->report_url);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)strlen(payload));
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, gcheckers_application_discard_upload_response);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "gcheckers/dev");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

  CURLcode curl_result = curl_easy_perform(curl);
  long response_code = 0;
  if (curl_result == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (curl_result != CURLE_OK) {
    g_task_return_new_error(task,
                            G_IO_ERROR,
                            G_IO_ERROR_FAILED,
                            "Puzzle progress upload failed: %s",
                            curl_easy_strerror(curl_result));
    return;
  }
  if (response_code < 200 || response_code >= 300) {
    g_task_return_new_error(task,
                            G_IO_ERROR,
                            G_IO_ERROR_FAILED,
                            "Puzzle progress upload returned HTTP %ld",
                            response_code);
    return;
  }

  if (!checkers_puzzle_progress_store_mark_reported(upload_data->store, g_get_real_time() / 1000, &error)) {
    g_task_return_error(task, g_steal_pointer(&error));
    return;
  }

  g_task_return_boolean(task, TRUE);
}

static void gcheckers_application_puzzle_progress_upload_ready(GObject *source_object,
                                                               GAsyncResult *result,
                                                               gpointer /*user_data*/) {
  GCheckersApplication *self = GCHECKERS_APPLICATION(source_object);
  g_return_if_fail(GCHECKERS_IS_APPLICATION(self));

  g_autoptr(GError) error = NULL;
  if (!g_task_propagate_boolean(G_TASK(result), &error)) {
    g_debug("Puzzle progress upload did not complete: %s",
            error != NULL ? error->message : "unknown error");
  }

  self->puzzle_progress_upload_active = FALSE;
  if (!self->puzzle_progress_upload_pending) {
    return;
  }

  gcheckers_application_request_puzzle_progress_flush(self);
}

static void gcheckers_application_start_puzzle_progress_upload(GCheckersApplication *self) {
  g_return_if_fail(GCHECKERS_IS_APPLICATION(self));

  if (self->puzzle_progress_store == NULL || self->puzzle_progress_upload_active ||
      self->puzzle_report_url == NULL || self->puzzle_report_url[0] == '\0') {
    self->puzzle_progress_upload_pending = FALSE;
    return;
  }

  GCheckersApplicationUploadTaskData *task_data = g_new0(GCheckersApplicationUploadTaskData, 1);
  task_data->store = checkers_puzzle_progress_store_ref(self->puzzle_progress_store);
  task_data->report_url = g_strdup(self->puzzle_report_url);

  self->puzzle_progress_upload_active = TRUE;
  self->puzzle_progress_upload_pending = FALSE;

  GTask *task = g_task_new(self, NULL, gcheckers_application_puzzle_progress_upload_ready, NULL);
  g_task_set_task_data(task, task_data, (GDestroyNotify)gcheckers_application_upload_task_data_free);
  g_task_run_in_thread(task, gcheckers_application_run_puzzle_progress_upload);
  g_object_unref(task);
}

static gboolean gcheckers_application_request_startup_flush_cb(gpointer user_data) {
  GCheckersApplication *self = GCHECKERS_APPLICATION(user_data);
  g_return_val_if_fail(GCHECKERS_IS_APPLICATION(self), G_SOURCE_REMOVE);

  self->puzzle_progress_startup_flush_source_id = 0;
  gcheckers_application_request_puzzle_progress_flush(self);
  return G_SOURCE_REMOVE;
}

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

static void gcheckers_application_on_settings(GSimpleAction * /*action*/,
                                              GVariant * /*parameter*/,
                                              gpointer user_data) {
  GCheckersApplication *self = GCHECKERS_APPLICATION(user_data);
  g_return_if_fail(GCHECKERS_IS_APPLICATION(self));

  GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(self));
  if (!window) {
    g_debug("No active window for settings action");
    return;
  }
  if (!GCHECKERS_IS_WINDOW(window)) {
    g_debug("Active window is not a gcheckers window");
    return;
  }

  gcheckers_window_present_settings_dialog(GCHECKERS_WINDOW(window));
}

static void gcheckers_application_on_quit(GSimpleAction * /*action*/,
                                          GVariant * /*parameter*/,
                                          gpointer user_data) {
  GCheckersApplication *self = GCHECKERS_APPLICATION(user_data);
  g_return_if_fail(GCHECKERS_IS_APPLICATION(self));

  g_application_quit(G_APPLICATION(self));
}

static void gcheckers_application_startup(GApplication *app) {
  GCheckersApplication *self = GCHECKERS_APPLICATION(app);
  g_return_if_fail(GCHECKERS_IS_APPLICATION(self));

  G_APPLICATION_CLASS(gcheckers_application_parent_class)->startup(app);

  if (curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK) {
    self->curl_initialized = TRUE;
  } else {
    g_debug("Failed to initialize libcurl globally for puzzle progress uploads");
  }

  g_autoptr(GError) error = NULL;
  g_autofree char *state_dir =
      gcheckers_app_paths_get_user_state_subdir("GCHECKERS_PUZZLE_PROGRESS_DIR", "puzzle-progress", &error);
  if (state_dir == NULL) {
    g_debug("Failed to initialize puzzle progress storage: %s",
            error != NULL ? error->message : "unknown error");
  } else {
    self->puzzle_progress_store = checkers_puzzle_progress_store_new(state_dir);
  }
  self->puzzle_report_url = g_strdup(g_getenv("GCHECKERS_PUZZLE_REPORT_URL"));

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
          .name = "settings",
          .activate = gcheckers_application_on_settings,
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
  GMenu *file_settings_menu = g_menu_new();
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
  g_menu_append(file_settings_menu, "Settings...", "app.settings");
  g_menu_append_section(file_menu, NULL, G_MENU_MODEL(file_settings_menu));
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
  g_object_unref(file_settings_menu);
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

  self->puzzle_progress_startup_flush_source_id =
      g_timeout_add(250, gcheckers_application_request_startup_flush_cb, self);
}

static void gcheckers_application_activate(GApplication *app) {
  GCheckersApplication *self = GCHECKERS_APPLICATION(app);
  g_return_if_fail(GCHECKERS_IS_APPLICATION(self));

  GtkWindow *existing = gtk_application_get_active_window(GTK_APPLICATION(app));
  if (existing) {
    gtk_window_present(existing);
    return;
  }

  GCheckersModel *model = gcheckers_model_new();
  GtkWindow *window = GTK_WINDOW(gcheckers_window_new(GTK_APPLICATION(app), model));
  g_object_unref(model);

  gtk_window_present(window);

  g_autoptr(GSettings) settings = gcheckers_app_settings_create();
  if (G_IS_SETTINGS(settings) &&
      !gcheckers_app_settings_get_privacy_settings_shown(settings)) {
    gcheckers_window_present_settings_dialog(GCHECKERS_WINDOW(window));
  }
}

static void gcheckers_application_shutdown(GApplication *app) {
  GCheckersApplication *self = GCHECKERS_APPLICATION(app);
  g_return_if_fail(GCHECKERS_IS_APPLICATION(self));

  if (self->puzzle_progress_startup_flush_source_id != 0) {
    g_clear_handle_id(&self->puzzle_progress_startup_flush_source_id, g_source_remove);
  }
  gcheckers_application_request_puzzle_progress_flush(self);

  G_APPLICATION_CLASS(gcheckers_application_parent_class)->shutdown(app);
}

static void gcheckers_application_dispose(GObject *object) {
  GCheckersApplication *self = GCHECKERS_APPLICATION(object);

  if (self->puzzle_progress_startup_flush_source_id != 0) {
    g_clear_handle_id(&self->puzzle_progress_startup_flush_source_id, g_source_remove);
  }

  G_OBJECT_CLASS(gcheckers_application_parent_class)->dispose(object);
}

static void gcheckers_application_finalize(GObject *object) {
  GCheckersApplication *self = GCHECKERS_APPLICATION(object);

  if (self->puzzle_progress_store != NULL) {
    checkers_puzzle_progress_store_unref(self->puzzle_progress_store);
    self->puzzle_progress_store = NULL;
  }
  g_clear_pointer(&self->puzzle_report_url, g_free);
  if (self->curl_initialized) {
    curl_global_cleanup();
    self->curl_initialized = FALSE;
  }

  G_OBJECT_CLASS(gcheckers_application_parent_class)->finalize(object);
}

static void gcheckers_application_class_init(GCheckersApplicationClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  GApplicationClass *app_class = G_APPLICATION_CLASS(klass);

  object_class->dispose = gcheckers_application_dispose;
  object_class->finalize = gcheckers_application_finalize;
  app_class->startup = gcheckers_application_startup;
  app_class->activate = gcheckers_application_activate;
  app_class->shutdown = gcheckers_application_shutdown;
}

static void gcheckers_application_init(GCheckersApplication *self) {
  self->puzzle_progress_startup_flush_source_id = 0;
  self->puzzle_progress_upload_active = FALSE;
  self->puzzle_progress_upload_pending = FALSE;
  self->curl_initialized = FALSE;
}

GCheckersApplication *gcheckers_application_new(void) {
  return g_object_new(GCHECKERS_TYPE_APPLICATION,
                      "application-id",
                      "io.github.jeromea.gcheckers",
                      "flags",
                      G_APPLICATION_NON_UNIQUE,
                      NULL);
}

CheckersPuzzleProgressStore *gcheckers_application_get_puzzle_progress_store(GCheckersApplication *self) {
  g_return_val_if_fail(GCHECKERS_IS_APPLICATION(self), NULL);

  g_autoptr(GError) error = NULL;
  g_autofree char *state_dir =
      gcheckers_app_paths_get_user_state_subdir("GCHECKERS_PUZZLE_PROGRESS_DIR", "puzzle-progress", &error);
  if (state_dir == NULL) {
    g_debug("Failed to resolve puzzle progress storage: %s", error != NULL ? error->message : "unknown error");
    return self->puzzle_progress_store;
  }

  g_autofree char *current_state_dir =
      self->puzzle_progress_store != NULL
          ? checkers_puzzle_progress_store_dup_state_dir(self->puzzle_progress_store)
          : NULL;
  if (g_strcmp0(current_state_dir, state_dir) != 0) {
    if (self->puzzle_progress_store != NULL) {
      checkers_puzzle_progress_store_unref(self->puzzle_progress_store);
    }
    self->puzzle_progress_store = checkers_puzzle_progress_store_new(state_dir);
  }

  return self->puzzle_progress_store;
}

void gcheckers_application_request_puzzle_progress_flush(GCheckersApplication *self) {
  g_return_if_fail(GCHECKERS_IS_APPLICATION(self));

  self->puzzle_progress_upload_pending = TRUE;
  if (self->puzzle_progress_upload_active) {
    return;
  }

  gcheckers_application_start_puzzle_progress_upload(self);
}
