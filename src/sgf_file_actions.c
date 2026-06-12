#include "sgf_file_actions.h"

#include "active_game_backend.h"
#include "common_settings.h"
#include "file_dialog_history.h"
#include "game_text_io.h"
#include "sgf_io.h"

static void ggame_window_show_file_error_dialog(GGameWindow *self, const char *title, const char *message) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(title != NULL);
  g_return_if_fail(message != NULL);

  GtkWidget *dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog), title);
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(self));
  gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(content, 12);
  gtk_widget_set_margin_bottom(content, 12);
  gtk_widget_set_margin_start(content, 12);
  gtk_widget_set_margin_end(content, 12);
  gtk_window_set_child(GTK_WINDOW(dialog), content);

  GtkWidget *label = gtk_label_new(message);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_box_append(GTK_BOX(content), label);

  GtkWidget *ok_button = gtk_button_new_with_label("OK");
  gtk_widget_set_halign(ok_button, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(content), ok_button);
  g_signal_connect_swapped(ok_button, "clicked", G_CALLBACK(gtk_window_destroy), dialog);

  gtk_window_present(GTK_WINDOW(dialog));
}

static void ggame_window_configure_sgf_filters(GtkFileDialog *dialog) {
  g_return_if_fail(GTK_IS_FILE_DIALOG(dialog));

  GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);

  GtkFileFilter *filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "SGF files");
  gtk_file_filter_add_pattern(filter, "*.sgf");
  gtk_file_filter_add_pattern(filter, "*.gsgf");
  g_list_store_append(filters, filter);
  gtk_file_dialog_set_default_filter(dialog, filter);

  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  if (backend != NULL &&
      backend->supports_ascii_game_io &&
      backend->ascii_game_file_extension != NULL &&
      backend->ascii_game_file_extension[0] != '\0') {
    GtkFileFilter *text_filter = gtk_file_filter_new();
    g_autofree char *pattern = g_strdup_printf("*.%s", backend->ascii_game_file_extension);
    gtk_file_filter_set_name(text_filter,
                             backend->ascii_game_file_description != NULL ?
                                 backend->ascii_game_file_description :
                                 "Text game files");
    gtk_file_filter_add_pattern(text_filter, pattern);
    g_list_store_append(filters, text_filter);
  }

  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
  g_object_unref(filters);
}

static void ggame_window_on_sgf_load_dialog_finish(GObject *source_object,
                                                       GAsyncResult *result,
                                                       gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GTK_IS_FILE_DIALOG(dialog));

  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) file = gtk_file_dialog_open_finish(dialog, result, &error);
  if (file == NULL) {
    if (error != NULL && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
      g_autofree char *text =
          g_strdup_printf("Unable to load SGF file.\n%s", error != NULL ? error->message : "Unknown error");
      ggame_window_show_file_error_dialog(self, "Load failed", text);
    }
    g_object_unref(self);
    return;
  }

  g_autofree char *path = g_file_get_path(file);
  if (path == NULL) {
    ggame_window_show_file_error_dialog(self, "Load failed", "Unable to resolve selected file path.");
    g_object_unref(self);
    return;
  }

  g_autoptr(GSettings) settings = ggame_file_dialog_history_create_settings();
  if (G_IS_SETTINGS(settings)) {
    (void)ggame_file_dialog_history_remember_parent(settings, GGAME_COMMON_SETTINGS_KEY_SGF_LAST_FOLDER, file);
  }

  GGameSgfController *controller = ggame_window_get_sgf_controller(self);
  if (!ggame_sgf_controller_load_file(controller, path, &error)) {
    g_autofree char *text =
        g_strdup_printf("Unable to load SGF file.\n%s", error != NULL ? error->message : "Unknown error");
    ggame_window_show_file_error_dialog(self, "Load failed", text);
  } else {
    SgfTree *tree = ggame_sgf_controller_get_tree(controller);
    const GameBackendVariant *loaded_variant = NULL;
    if (tree != NULL && sgf_io_tree_get_variant(tree, &loaded_variant, NULL) && loaded_variant != NULL) {
      ggame_window_set_loaded_variant(self, loaded_variant);
    }
    ggame_window_set_loaded_source_path(self, path);
    ggame_window_set_board_orientation_mode(self, GGAME_WINDOW_BOARD_ORIENTATION_FIXED);
  }

  g_object_unref(self);
}

static void ggame_window_on_sgf_save_dialog_finish(GObject *source_object,
                                                       GAsyncResult *result,
                                                       gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GTK_IS_FILE_DIALOG(dialog));

  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) file = gtk_file_dialog_save_finish(dialog, result, &error);
  if (file == NULL) {
    if (error != NULL && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
      g_autofree char *text =
          g_strdup_printf("Unable to save SGF file.\n%s", error != NULL ? error->message : "Unknown error");
      ggame_window_show_file_error_dialog(self, "Save failed", text);
    }
    g_object_unref(self);
    return;
  }

  g_autofree char *path = g_file_get_path(file);
  if (path == NULL) {
    ggame_window_show_file_error_dialog(self, "Save failed", "Unable to resolve selected file path.");
    g_object_unref(self);
    return;
  }

  g_autoptr(GSettings) settings = ggame_file_dialog_history_create_settings();
  if (G_IS_SETTINGS(settings)) {
    (void)ggame_file_dialog_history_remember_parent(settings, GGAME_COMMON_SETTINGS_KEY_SGF_LAST_FOLDER, file);
  }

  GGameSgfController *controller = ggame_window_get_sgf_controller(self);
  if (!ggame_sgf_controller_save_file(controller, path, &error)) {
    g_autofree char *text =
        g_strdup_printf("Unable to save SGF file.\n%s", error != NULL ? error->message : "Unknown error");
    ggame_window_show_file_error_dialog(self, "Save failed", text);
  } else {
    ggame_window_set_loaded_source_path(self, path);
  }

  g_object_unref(self);
}

static void ggame_window_on_sgf_save_position_dialog_finish(GObject *source_object,
                                                                GAsyncResult *result,
                                                                gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GTK_IS_FILE_DIALOG(dialog));

  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) file = gtk_file_dialog_save_finish(dialog, result, &error);
  if (file == NULL) {
    if (error != NULL && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
      g_autofree char *text =
          g_strdup_printf("Unable to save SGF position.\n%s", error != NULL ? error->message : "Unknown error");
      ggame_window_show_file_error_dialog(self, "Save position failed", text);
    }
    g_object_unref(self);
    return;
  }

  g_autofree char *path = g_file_get_path(file);
  if (path == NULL) {
    ggame_window_show_file_error_dialog(self, "Save position failed", "Unable to resolve selected file path.");
    g_object_unref(self);
    return;
  }

  g_autoptr(GSettings) settings = ggame_file_dialog_history_create_settings();
  if (G_IS_SETTINGS(settings)) {
    (void)ggame_file_dialog_history_remember_parent(settings, GGAME_COMMON_SETTINGS_KEY_SGF_LAST_FOLDER, file);
  }

  GGameSgfController *controller = ggame_window_get_sgf_controller(self);
  if (!ggame_sgf_controller_save_position_file(controller, path, &error)) {
    g_autofree char *text =
        g_strdup_printf("Unable to save SGF position.\n%s", error != NULL ? error->message : "Unknown error");
    ggame_window_show_file_error_dialog(self, "Save position failed", text);
  }

  g_object_unref(self);
}

static void ggame_window_on_sgf_load_action(GSimpleAction * /*action*/,
                                                GVariant * /*parameter*/,
                                                gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));

  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Load SGF");
  ggame_window_configure_sgf_filters(dialog);
  g_autoptr(GSettings) settings = ggame_file_dialog_history_create_settings();
  if (G_IS_SETTINGS(settings)) {
    g_autoptr(GFile) folder =
        ggame_file_dialog_history_get_initial_folder(settings, GGAME_COMMON_SETTINGS_KEY_SGF_LAST_FOLDER);
    if (folder != NULL) {
      gtk_file_dialog_set_initial_folder(dialog, folder);
    }
  }
  gtk_file_dialog_open(dialog, GTK_WINDOW(self), NULL, ggame_window_on_sgf_load_dialog_finish, g_object_ref(self));
  g_object_unref(dialog);
}

static void ggame_window_on_sgf_save_action(GSimpleAction * /*action*/,
                                            GVariant * /*parameter*/,
                                            gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));

  const char *path = ggame_window_get_loaded_source_path(self);
  if (path == NULL || path[0] == '\0') {
    ggame_window_show_file_error_dialog(self, "Save failed", "No current SGF file is known.");
    return;
  }

  g_autoptr(GError) error = NULL;
  GGameSgfController *controller = ggame_window_get_sgf_controller(self);
  if (!ggame_sgf_controller_save_file(controller, path, &error)) {
    g_autofree char *text =
        g_strdup_printf("Unable to save SGF file.\n%s", error != NULL ? error->message : "Unknown error");
    ggame_window_show_file_error_dialog(self, "Save failed", text);
  }
}

static void ggame_window_on_sgf_save_as_action(GSimpleAction * /*action*/,
                                                   GVariant * /*parameter*/,
                                                   gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));

  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Save SGF As");
  gtk_file_dialog_set_initial_name(dialog, "game.sgf");
  ggame_window_configure_sgf_filters(dialog);
  g_autoptr(GSettings) settings = ggame_file_dialog_history_create_settings();
  if (G_IS_SETTINGS(settings)) {
    g_autoptr(GFile) folder =
        ggame_file_dialog_history_get_initial_folder(settings, GGAME_COMMON_SETTINGS_KEY_SGF_LAST_FOLDER);
    if (folder != NULL) {
      gtk_file_dialog_set_initial_folder(dialog, folder);
    }
  }
  gtk_file_dialog_save(dialog, GTK_WINDOW(self), NULL, ggame_window_on_sgf_save_dialog_finish, g_object_ref(self));
  g_object_unref(dialog);
}

static void ggame_window_on_sgf_save_position_action(GSimpleAction * /*action*/,
                                                         GVariant * /*parameter*/,
                                                         gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));

  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Save SGF Position As");
  gtk_file_dialog_set_initial_name(dialog, "position.sgf");
  ggame_window_configure_sgf_filters(dialog);
  g_autoptr(GSettings) settings = ggame_file_dialog_history_create_settings();
  if (G_IS_SETTINGS(settings)) {
    g_autoptr(GFile) folder =
        ggame_file_dialog_history_get_initial_folder(settings, GGAME_COMMON_SETTINGS_KEY_SGF_LAST_FOLDER);
    if (folder != NULL) {
      gtk_file_dialog_set_initial_folder(dialog, folder);
    }
  }
  gtk_file_dialog_save(dialog,
                       GTK_WINDOW(self),
                       NULL,
                       ggame_window_on_sgf_save_position_dialog_finish,
                       g_object_ref(self));
  g_object_unref(dialog);
}

void ggame_window_install_sgf_file_actions(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  static const GActionEntry file_actions[] = {
      {
          .name = "sgf-load",
          .activate = ggame_window_on_sgf_load_action,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "sgf-save",
          .activate = ggame_window_on_sgf_save_action,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "sgf-save-as",
          .activate = ggame_window_on_sgf_save_as_action,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "sgf-save-position",
          .activate = ggame_window_on_sgf_save_position_action,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
  };

  g_action_map_add_action_entries(G_ACTION_MAP(self), file_actions, G_N_ELEMENTS(file_actions), self);
}
