#include "window.h"

#include "bga_client.h"

typedef enum {
  GCHECKERS_IMPORT_SITE_LIDRAUGHT = 0,
  GCHECKERS_IMPORT_SITE_FLYORDIE,
  GCHECKERS_IMPORT_SITE_PLAYOK,
  GCHECKERS_IMPORT_SITE_BOARDGAMEARENA
} GCheckersImportSite;

typedef enum {
  GCHECKERS_IMPORT_STEP_SITE = 0,
  GCHECKERS_IMPORT_STEP_CREDENTIALS,
  GCHECKERS_IMPORT_STEP_HISTORY
} GCheckersImportStep;

typedef struct {
  GGameWindow *self;
  GtkWindow *dialog;
  GtkStack *stack;
  GtkDropDown *site_drop_down;
  GtkButton *back_button;
  GtkButton *next_button;
  GtkEntry *email_entry;
  GtkEntry *password_entry;
  GtkCheckButton *remember_check;
  GtkListBox *history_list;
  GSettings *settings;
  GCheckersImportStep step;
} GGameWindowImportDialogData;

static const char *gcheckers_import_schema_id = "io.github.jeromea.gcheckers";
static const char *gcheckers_import_key_remember = "import-remember";
static const char *gcheckers_import_key_email = "import-email";
static const char *gcheckers_import_key_password = "import-password";

static GSettings *gcheckers_import_dialog_create_settings(void) {
  GSettingsSchemaSource *default_source = g_settings_schema_source_get_default();
  GSettingsSchema *schema = NULL;
  if (default_source != NULL) {
    schema = g_settings_schema_source_lookup(default_source, gcheckers_import_schema_id, TRUE);
  }

  g_autoptr(GSettingsSchemaSource) local_source = NULL;
  if (schema == NULL) {
    g_autoptr(GError) error = NULL;
    local_source =
        g_settings_schema_source_new_from_directory("data/schemas", default_source, FALSE, &error);
    if (!local_source) {
      g_debug("Unable to load local GSettings schemas: %s", error ? error->message : "unknown error");
      return NULL;
    }

    schema = g_settings_schema_source_lookup(local_source, gcheckers_import_schema_id, FALSE);
  }

  if (schema == NULL) {
    g_debug("Missing GSettings schema %s", gcheckers_import_schema_id);
    return NULL;
  }

  GSettings *settings = g_settings_new_full(schema, NULL, NULL);
  g_settings_schema_unref(schema);
  return settings;
}

static void gcheckers_import_dialog_load_credentials(GGameWindowImportDialogData *data) {
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_ENTRY(data->email_entry));
  g_return_if_fail(GTK_IS_ENTRY(data->password_entry));
  g_return_if_fail(GTK_IS_CHECK_BUTTON(data->remember_check));

  if (!G_IS_SETTINGS(data->settings)) {
    gtk_check_button_set_active(data->remember_check, FALSE);
    gtk_editable_set_text(GTK_EDITABLE(data->email_entry), "");
    gtk_editable_set_text(GTK_EDITABLE(data->password_entry), "");
    return;
  }

  gboolean remember = g_settings_get_boolean(data->settings, gcheckers_import_key_remember);
  gtk_check_button_set_active(data->remember_check, remember);
  if (!remember) {
    gtk_editable_set_text(GTK_EDITABLE(data->email_entry), "");
    gtk_editable_set_text(GTK_EDITABLE(data->password_entry), "");
    return;
  }

  g_autofree char *email = g_settings_get_string(data->settings, gcheckers_import_key_email);
  g_autofree char *password = g_settings_get_string(data->settings, gcheckers_import_key_password);
  gtk_editable_set_text(GTK_EDITABLE(data->email_entry), email ? email : "");
  gtk_editable_set_text(GTK_EDITABLE(data->password_entry), password ? password : "");
}

static void gcheckers_import_dialog_save_credentials(GGameWindowImportDialogData *data) {
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_ENTRY(data->email_entry));
  g_return_if_fail(GTK_IS_ENTRY(data->password_entry));
  g_return_if_fail(GTK_IS_CHECK_BUTTON(data->remember_check));

  if (!G_IS_SETTINGS(data->settings)) {
    return;
  }

  gboolean remember = gtk_check_button_get_active(data->remember_check);
  g_settings_set_boolean(data->settings, gcheckers_import_key_remember, remember);
  if (!remember) {
    g_settings_set_string(data->settings, gcheckers_import_key_email, "");
    g_settings_set_string(data->settings, gcheckers_import_key_password, "");
    return;
  }

  const char *email = gtk_editable_get_text(GTK_EDITABLE(data->email_entry));
  const char *password = gtk_editable_get_text(GTK_EDITABLE(data->password_entry));
  g_settings_set_string(data->settings, gcheckers_import_key_email, email ? email : "");
  g_settings_set_string(data->settings, gcheckers_import_key_password, password ? password : "");
}

static void gcheckers_import_dialog_on_error_ok_clicked(GtkButton *button, gpointer user_data) {
  GtkWindow *error_dialog = GTK_WINDOW(user_data);
  g_return_if_fail(GTK_IS_BUTTON(button));
  g_return_if_fail(GTK_IS_WINDOW(error_dialog));

  GtkWindow *wizard_dialog = g_object_get_data(G_OBJECT(error_dialog), "wizard-dialog");
  if (GTK_IS_WINDOW(wizard_dialog)) {
    gtk_window_destroy(wizard_dialog);
  }
  gtk_window_destroy(error_dialog);
}

static void gcheckers_import_dialog_show_error_and_close_wizard(GGameWindowImportDialogData *data,
                                                                const char *text) {
  g_return_if_fail(data != NULL);
  g_return_if_fail(text != NULL);

  GtkWidget *dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog), "Import error");
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(data->self));
  gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(content, 12);
  gtk_widget_set_margin_bottom(content, 12);
  gtk_widget_set_margin_start(content, 12);
  gtk_widget_set_margin_end(content, 12);
  gtk_window_set_child(GTK_WINDOW(dialog), content);

  GtkWidget *label = gtk_label_new(text);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_box_append(GTK_BOX(content), label);

  GtkWidget *ok_button = gtk_button_new_with_label("OK");
  gtk_widget_set_halign(ok_button, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(content), ok_button);
  g_object_set_data(G_OBJECT(dialog), "wizard-dialog", data->dialog);
  g_signal_connect(ok_button, "clicked", G_CALLBACK(gcheckers_import_dialog_on_error_ok_clicked), dialog);
  gtk_window_present(GTK_WINDOW(dialog));
}

static gboolean gcheckers_import_dialog_is_board_game_arena_selected(GGameWindowImportDialogData *data) {
  g_return_val_if_fail(data != NULL, FALSE);
  g_return_val_if_fail(GTK_IS_DROP_DOWN(data->site_drop_down), FALSE);

  return gtk_drop_down_get_selected(data->site_drop_down) == GCHECKERS_IMPORT_SITE_BOARDGAMEARENA;
}

static void ggame_window_import_dialog_data_free(GGameWindowImportDialogData *data) {
  g_return_if_fail(data != NULL);

  g_clear_object(&data->settings);
  g_object_unref(data->self);
  g_free(data);
}

static void ggame_window_on_import_dialog_destroy(GtkWindow * /*dialog*/, gpointer user_data) {
  GGameWindowImportDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  ggame_window_import_dialog_data_free(data);
}

static void ggame_window_import_dialog_update_step(GGameWindowImportDialogData *data) {
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_STACK(data->stack));
  g_return_if_fail(GTK_IS_BUTTON(data->back_button));
  g_return_if_fail(GTK_IS_BUTTON(data->next_button));

  if (data->step == GCHECKERS_IMPORT_STEP_SITE) {
    gtk_stack_set_visible_child_name(data->stack, "site");
    gtk_widget_set_sensitive(GTK_WIDGET(data->back_button), FALSE);
    gtk_button_set_label(data->next_button, "Next");
    gtk_widget_set_sensitive(GTK_WIDGET(data->next_button),
                             gcheckers_import_dialog_is_board_game_arena_selected(data));
    return;
  }

  if (data->step == GCHECKERS_IMPORT_STEP_HISTORY) {
    gtk_stack_set_visible_child_name(data->stack, "history");
    gtk_widget_set_sensitive(GTK_WIDGET(data->back_button), FALSE);
    gtk_button_set_label(data->next_button, "Close");
    gtk_widget_set_sensitive(GTK_WIDGET(data->next_button), TRUE);
    return;
  }

  gtk_stack_set_visible_child_name(data->stack, "credentials");
  gtk_widget_set_sensitive(GTK_WIDGET(data->back_button), TRUE);
  gtk_button_set_label(data->next_button, "Fetch game history");
  gtk_widget_set_sensitive(GTK_WIDGET(data->next_button), TRUE);
  gcheckers_import_dialog_load_credentials(data);
}

static void ggame_window_on_import_dialog_site_notify(GObject * /*object*/,
                                                          GParamSpec * /*pspec*/,
                                                          gpointer user_data) {
  GGameWindowImportDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  if (data->step != GCHECKERS_IMPORT_STEP_SITE) {
    return;
  }

  ggame_window_import_dialog_update_step(data);
}

static void ggame_window_on_import_dialog_cancel_clicked(GtkButton * /*button*/, gpointer user_data) {
  GGameWindowImportDialogData *data = user_data;
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_WINDOW(data->dialog));

  gtk_window_destroy(data->dialog);
}

static void ggame_window_on_import_dialog_back_clicked(GtkButton * /*button*/, gpointer user_data) {
  GGameWindowImportDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  if (data->step == GCHECKERS_IMPORT_STEP_SITE) {
    return;
  }

  data->step = GCHECKERS_IMPORT_STEP_SITE;
  ggame_window_import_dialog_update_step(data);
}

static void ggame_window_on_import_dialog_next_clicked(GtkButton * /*button*/, gpointer user_data) {
  GGameWindowImportDialogData *data = user_data;
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_WINDOW(data->dialog));
  g_return_if_fail(GTK_IS_ENTRY(data->email_entry));
  g_return_if_fail(GTK_IS_CHECK_BUTTON(data->remember_check));
  g_return_if_fail(GTK_IS_LIST_BOX(data->history_list));

  g_debug("Import flow: Next clicked at step=%d", (int)data->step);

  if (data->step == GCHECKERS_IMPORT_STEP_SITE) {
    if (!gcheckers_import_dialog_is_board_game_arena_selected(data)) {
      g_debug("Import source not implemented yet");
      return;
    }

    data->step = GCHECKERS_IMPORT_STEP_CREDENTIALS;
    g_debug("Import flow: moving to credentials step");
    ggame_window_import_dialog_update_step(data);
    return;
  }

  if (data->step == GCHECKERS_IMPORT_STEP_HISTORY) {
    g_debug("Import flow: closing wizard from history step");
    gtk_window_destroy(data->dialog);
    return;
  }

  g_debug("Import flow: starting BGA login sequence");
  gcheckers_import_dialog_save_credentials(data);
  const char *email = gtk_editable_get_text(GTK_EDITABLE(data->email_entry));
  const char *password = gtk_editable_get_text(GTK_EDITABLE(data->password_entry));
  gboolean remember = gtk_check_button_get_active(data->remember_check);
  BgaCredentials credentials = {
    .username = email ? email : "",
    .password = password ? password : "",
    .remember_me = remember,
  };
  g_autoptr(GError) error = NULL;
  BgaClientSession *session = bga_client_session_new(&error);
  if (session == NULL) {
    g_debug("Failed to initialize BoardGameArena client session: %s", error ? error->message : "unknown error");
    gcheckers_import_dialog_show_error_and_close_wizard(
        data,
        "Unable to initialize BoardGameArena session.");
    return;
  }

  g_autofree char *request_token = NULL;
  if (!bga_client_session_fetch_homepage_and_request_token(session, NULL, &request_token, &error)) {
    g_debug("Failed to fetch BoardGameArena request token: %s", error ? error->message : "unknown error");
    gcheckers_import_dialog_show_error_and_close_wizard(
        data,
        "Unable to fetch BoardGameArena request token.");
    bga_client_session_free(session);
    return;
  }

  BgaHttpResponse login_response = {0};
  if (!bga_client_session_login_with_password(session, &credentials, request_token, &login_response, &error)) {
    g_debug("Failed to login to BoardGameArena: %s", error ? error->message : "unknown error");
    gcheckers_import_dialog_show_error_and_close_wizard(
        data,
        "Unable to login to BoardGameArena.");
    bga_http_response_clear(&login_response);
    bga_client_session_free(session);
    return;
  }

  g_debug("BoardGameArena login HTTP %ld", login_response.http_status);
  g_debug("Import flow: login request completed, parsing response");
  BgaLoginResult parsed = {0};
  if (!bga_client_parse_login_response(login_response.body ? login_response.body : "", &parsed, &error)) {
    g_debug("Import flow: failed to parse BoardGameArena login response: %s",
            error ? error->message : "unknown error");
    gcheckers_import_dialog_show_error_and_close_wizard(
        data,
        "Unable to parse BoardGameArena login response.");
    bga_http_response_clear(&login_response);
    bga_client_session_free(session);
    return;
  }
  g_debug("Import flow: parsed login result kind=%d user_id=%s",
          (int)parsed.kind,
          parsed.user_id != NULL ? parsed.user_id : "(null)");

  if (parsed.kind == BGA_LOGIN_RESULT_STATUS_ZERO || parsed.kind == BGA_LOGIN_RESULT_SUCCESS_FALSE) {
    g_autofree char *dialog_text = NULL;
    if (parsed.kind == BGA_LOGIN_RESULT_STATUS_ZERO) {
      dialog_text = g_strdup_printf("Login failed.\nError: %s\nException: %s",
                                    parsed.error ? parsed.error : "(none)",
                                    parsed.exception ? parsed.exception : "(none)");
    } else {
      dialog_text = g_strdup_printf("Login failed.\nMessage: %s", parsed.message ? parsed.message : "(none)");
    }
    gcheckers_import_dialog_show_error_and_close_wizard(data, dialog_text);
    bga_login_result_clear(&parsed);
    bga_http_response_clear(&login_response);
    bga_client_session_free(session);
    g_debug("Import flow: login failed, showing error dialog and closing wizard after OK");
    return;
  }

  g_autofree char *history_user_id = g_strdup(parsed.user_id != NULL ? parsed.user_id : "");
  bga_login_result_clear(&parsed);
  bga_http_response_clear(&login_response);

  if (history_user_id[0] == '\0') {
    g_debug("Import flow: missing user_id in successful BoardGameArena login response");
    gcheckers_import_dialog_show_error_and_close_wizard(
        data,
        "BoardGameArena login succeeded but user_id is missing.");
    bga_client_session_free(session);
    return;
  }
  g_debug("Import flow: fetching checkers history for user_id=%s", history_user_id);

  BgaHttpResponse history_response = {0};
  if (!bga_client_session_fetch_checkers_history(session, history_user_id, &history_response, &error)) {
    g_debug("Import flow: failed to fetch BoardGameArena history: %s",
            error ? error->message : "unknown error");
    gcheckers_import_dialog_show_error_and_close_wizard(
        data,
        "Unable to fetch BoardGameArena checkers history.");
    bga_http_response_clear(&history_response);
    bga_client_session_free(session);
    return;
  }
  bga_client_session_free(session);
  g_debug("Import flow: history HTTP status=%ld", history_response.http_status);

  g_autoptr(GPtrArray) games = NULL;
  if (!bga_client_parse_checkers_history_games(history_response.body ? history_response.body : "", &games, &error)) {
    g_debug("Import flow: failed to parse BoardGameArena history: %s",
            error ? error->message : "unknown error");
    gcheckers_import_dialog_show_error_and_close_wizard(
        data,
        "Unable to parse BoardGameArena checkers history.");
    bga_http_response_clear(&history_response);
    return;
  }
  bga_http_response_clear(&history_response);
  g_debug("Import flow: parsed %u checkers games", games->len);

  GtkWidget *row = gtk_widget_get_first_child(GTK_WIDGET(data->history_list));
  while (row != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(row);
    gtk_list_box_remove(data->history_list, row);
    row = next;
  }

  for (guint i = 0; i < games->len; ++i) {
    BgaHistoryGameSummary *summary = g_ptr_array_index(games, i);
    g_return_if_fail(summary != NULL);

    GtkWidget *line = gtk_label_new(NULL);
    g_autofree char *text = g_strdup_printf("%s  |  %s  |  %s vs %s",
                                            summary->start_at ? summary->start_at : "",
                                            summary->table_id,
                                            summary->player_one,
                                            summary->player_two);
    gtk_label_set_text(GTK_LABEL(line), text);
    gtk_widget_set_halign(line, GTK_ALIGN_START);
    gtk_list_box_append(data->history_list, line);
  }

  data->step = GCHECKERS_IMPORT_STEP_HISTORY;
  ggame_window_import_dialog_update_step(data);
  g_debug("Import flow: switched wizard to history step");
}

void ggame_window_present_import_dialog(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  GtkWidget *dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog), "Import games");
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(self));
  gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 480, 320);

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(content, 12);
  gtk_widget_set_margin_bottom(content, 12);
  gtk_widget_set_margin_start(content, 12);
  gtk_widget_set_margin_end(content, 12);
  gtk_window_set_child(GTK_WINDOW(dialog), content);

  GtkWidget *stack = gtk_stack_new();
  gtk_widget_set_hexpand(stack, TRUE);
  gtk_widget_set_vexpand(stack, TRUE);
  gtk_box_append(GTK_BOX(content), stack);

  GtkWidget *site_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget *site_title = gtk_label_new("Select import website");
  gtk_widget_set_halign(site_title, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(site_page), site_title);

  static const char *site_options[] = {"lidraught", "FlyOrDie", "playOK", "BoardGameArena", NULL};
  GtkDropDown *site_drop_down = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(site_options));
  gtk_drop_down_set_selected(site_drop_down, GCHECKERS_IMPORT_SITE_BOARDGAMEARENA);
  gtk_box_append(GTK_BOX(site_page), GTK_WIDGET(site_drop_down));

  GtkWidget *site_note =
      gtk_label_new("Only BoardGameArena is implemented right now. Other websites are not available yet.");
  gtk_widget_set_halign(site_note, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(site_note), TRUE);
  gtk_box_append(GTK_BOX(site_page), site_note);

  GtkWidget *credentials_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget *credentials_title = gtk_label_new("BoardGameArena credentials");
  gtk_widget_set_halign(credentials_title, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(credentials_page), credentials_title);

  GtkWidget *credentials_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(credentials_grid), 8);
  gtk_grid_set_column_spacing(GTK_GRID(credentials_grid), 12);
  gtk_box_append(GTK_BOX(credentials_page), credentials_grid);

  GtkWidget *email_label = gtk_label_new("Email");
  gtk_widget_set_halign(email_label, GTK_ALIGN_START);
  GtkWidget *password_label = gtk_label_new("Password");
  gtk_widget_set_halign(password_label, GTK_ALIGN_START);

  GtkEntry *email_entry = GTK_ENTRY(gtk_entry_new());
  GtkEntry *password_entry = GTK_ENTRY(gtk_entry_new());
  gtk_entry_set_visibility(password_entry, FALSE);
  gtk_entry_set_input_purpose(email_entry, GTK_INPUT_PURPOSE_EMAIL);
  gtk_entry_set_input_purpose(password_entry, GTK_INPUT_PURPOSE_PASSWORD);
  gtk_widget_set_hexpand(GTK_WIDGET(email_entry), TRUE);
  gtk_widget_set_hexpand(GTK_WIDGET(password_entry), TRUE);

  gtk_grid_attach(GTK_GRID(credentials_grid), email_label, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(credentials_grid), GTK_WIDGET(email_entry), 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(credentials_grid), password_label, 0, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(credentials_grid), GTK_WIDGET(password_entry), 1, 1, 1, 1);

  GtkCheckButton *remember_check = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Remember credentials"));
  gtk_box_append(GTK_BOX(credentials_page), GTK_WIDGET(remember_check));

  GtkWidget *history_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget *history_title = gtk_label_new("BoardGameArena checkers history");
  gtk_widget_set_halign(history_title, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(history_page), history_title);

  GtkWidget *history_scroll = gtk_scrolled_window_new();
  gtk_widget_set_hexpand(history_scroll, TRUE);
  gtk_widget_set_vexpand(history_scroll, TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(history_scroll),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
  gtk_box_append(GTK_BOX(history_page), history_scroll);

  GtkListBox *history_list = GTK_LIST_BOX(gtk_list_box_new());
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(history_scroll), GTK_WIDGET(history_list));

  gtk_stack_add_named(GTK_STACK(stack), site_page, "site");
  gtk_stack_add_named(GTK_STACK(stack), credentials_page, "credentials");
  gtk_stack_add_named(GTK_STACK(stack), history_page, "history");

  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(content), actions);

  GtkWidget *cancel_button = gtk_button_new_with_label("Cancel");
  GtkWidget *back_button = gtk_button_new_with_label("Back");
  GtkWidget *next_button = gtk_button_new_with_label("Next");
  gtk_box_append(GTK_BOX(actions), cancel_button);
  gtk_box_append(GTK_BOX(actions), back_button);
  gtk_box_append(GTK_BOX(actions), next_button);

  GGameWindowImportDialogData *data = g_new0(GGameWindowImportDialogData, 1);
  data->self = g_object_ref(self);
  data->dialog = GTK_WINDOW(dialog);
  data->stack = GTK_STACK(stack);
  data->site_drop_down = site_drop_down;
  data->back_button = GTK_BUTTON(back_button);
  data->next_button = GTK_BUTTON(next_button);
  data->email_entry = email_entry;
  data->password_entry = password_entry;
  data->remember_check = remember_check;
  data->history_list = history_list;
  data->settings = gcheckers_import_dialog_create_settings();
  data->step = GCHECKERS_IMPORT_STEP_SITE;

  g_signal_connect(site_drop_down,
                   "notify::selected",
                   G_CALLBACK(ggame_window_on_import_dialog_site_notify),
                   data);
  g_signal_connect(cancel_button,
                   "clicked",
                   G_CALLBACK(ggame_window_on_import_dialog_cancel_clicked),
                   data);
  g_signal_connect(back_button,
                   "clicked",
                   G_CALLBACK(ggame_window_on_import_dialog_back_clicked),
                   data);
  g_signal_connect(next_button,
                   "clicked",
                   G_CALLBACK(ggame_window_on_import_dialog_next_clicked),
                   data);
  g_signal_connect(dialog,
                   "destroy",
                   G_CALLBACK(ggame_window_on_import_dialog_destroy),
                   data);

  ggame_window_import_dialog_update_step(data);
  gtk_window_present(GTK_WINDOW(dialog));
}
