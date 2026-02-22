#include "gcheckers_window.h"

typedef enum {
  GCHECKERS_IMPORT_SITE_LIDRAUGHT = 0,
  GCHECKERS_IMPORT_SITE_FLYORDIE,
  GCHECKERS_IMPORT_SITE_PLAYOK,
  GCHECKERS_IMPORT_SITE_BOARDGAMEARENA
} GCheckersImportSite;

typedef enum {
  GCHECKERS_IMPORT_STEP_SITE = 0,
  GCHECKERS_IMPORT_STEP_CREDENTIALS
} GCheckersImportStep;

typedef struct {
  GCheckersWindow *self;
  GtkWindow *dialog;
  GtkStack *stack;
  GtkDropDown *site_drop_down;
  GtkButton *back_button;
  GtkButton *next_button;
  GtkEntry *email_entry;
  GtkEntry *password_entry;
  GtkCheckButton *remember_check;
  GSettings *settings;
  GCheckersImportStep step;
} GCheckersWindowImportDialogData;

static const char *gcheckers_import_schema_id = "com.example.gcheckers";
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

static void gcheckers_import_dialog_load_credentials(GCheckersWindowImportDialogData *data) {
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

static void gcheckers_import_dialog_save_credentials(GCheckersWindowImportDialogData *data) {
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

static gboolean gcheckers_import_dialog_is_board_game_arena_selected(GCheckersWindowImportDialogData *data) {
  g_return_val_if_fail(data != NULL, FALSE);
  g_return_val_if_fail(GTK_IS_DROP_DOWN(data->site_drop_down), FALSE);

  return gtk_drop_down_get_selected(data->site_drop_down) == GCHECKERS_IMPORT_SITE_BOARDGAMEARENA;
}

static void gcheckers_window_import_dialog_data_free(GCheckersWindowImportDialogData *data) {
  g_return_if_fail(data != NULL);

  g_clear_object(&data->settings);
  g_object_unref(data->self);
  g_free(data);
}

static void gcheckers_window_on_import_dialog_destroy(GtkWindow * /*dialog*/, gpointer user_data) {
  GCheckersWindowImportDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  gcheckers_window_import_dialog_data_free(data);
}

static void gcheckers_window_import_dialog_update_step(GCheckersWindowImportDialogData *data) {
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

  gtk_stack_set_visible_child_name(data->stack, "credentials");
  gtk_widget_set_sensitive(GTK_WIDGET(data->back_button), TRUE);
  gtk_button_set_label(data->next_button, "Fetch game history");
  gtk_widget_set_sensitive(GTK_WIDGET(data->next_button), TRUE);
  gcheckers_import_dialog_load_credentials(data);
}

static void gcheckers_window_on_import_dialog_site_notify(GObject * /*object*/,
                                                          GParamSpec * /*pspec*/,
                                                          gpointer user_data) {
  GCheckersWindowImportDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  if (data->step != GCHECKERS_IMPORT_STEP_SITE) {
    return;
  }

  gcheckers_window_import_dialog_update_step(data);
}

static void gcheckers_window_on_import_dialog_cancel_clicked(GtkButton * /*button*/, gpointer user_data) {
  GCheckersWindowImportDialogData *data = user_data;
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_WINDOW(data->dialog));

  gtk_window_destroy(data->dialog);
}

static void gcheckers_window_on_import_dialog_back_clicked(GtkButton * /*button*/, gpointer user_data) {
  GCheckersWindowImportDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  if (data->step == GCHECKERS_IMPORT_STEP_SITE) {
    return;
  }

  data->step = GCHECKERS_IMPORT_STEP_SITE;
  gcheckers_window_import_dialog_update_step(data);
}

static void gcheckers_window_on_import_dialog_next_clicked(GtkButton * /*button*/, gpointer user_data) {
  GCheckersWindowImportDialogData *data = user_data;
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_WINDOW(data->dialog));
  g_return_if_fail(GTK_IS_ENTRY(data->email_entry));
  g_return_if_fail(GTK_IS_CHECK_BUTTON(data->remember_check));

  if (data->step == GCHECKERS_IMPORT_STEP_SITE) {
    if (!gcheckers_import_dialog_is_board_game_arena_selected(data)) {
      g_debug("Import source not implemented yet");
      return;
    }

    data->step = GCHECKERS_IMPORT_STEP_CREDENTIALS;
    gcheckers_window_import_dialog_update_step(data);
    return;
  }

  gcheckers_import_dialog_save_credentials(data);
  const char *email = gtk_editable_get_text(GTK_EDITABLE(data->email_entry));
  gboolean remember = gtk_check_button_get_active(data->remember_check);
  g_debug("BoardGameArena history fetch requested for %s (remember=%s)",
          email ? email : "",
          remember ? "true" : "false");
  gtk_window_destroy(data->dialog);
}

void gcheckers_window_present_import_dialog(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

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

  gtk_stack_add_named(GTK_STACK(stack), site_page, "site");
  gtk_stack_add_named(GTK_STACK(stack), credentials_page, "credentials");

  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(content), actions);

  GtkWidget *cancel_button = gtk_button_new_with_label("Cancel");
  GtkWidget *back_button = gtk_button_new_with_label("Back");
  GtkWidget *next_button = gtk_button_new_with_label("Next");
  gtk_box_append(GTK_BOX(actions), cancel_button);
  gtk_box_append(GTK_BOX(actions), back_button);
  gtk_box_append(GTK_BOX(actions), next_button);

  GCheckersWindowImportDialogData *data = g_new0(GCheckersWindowImportDialogData, 1);
  data->self = g_object_ref(self);
  data->dialog = GTK_WINDOW(dialog);
  data->stack = GTK_STACK(stack);
  data->site_drop_down = site_drop_down;
  data->back_button = GTK_BUTTON(back_button);
  data->next_button = GTK_BUTTON(next_button);
  data->email_entry = email_entry;
  data->password_entry = password_entry;
  data->remember_check = remember_check;
  data->settings = gcheckers_import_dialog_create_settings();
  data->step = GCHECKERS_IMPORT_STEP_SITE;

  g_signal_connect(site_drop_down,
                   "notify::selected",
                   G_CALLBACK(gcheckers_window_on_import_dialog_site_notify),
                   data);
  g_signal_connect(cancel_button,
                   "clicked",
                   G_CALLBACK(gcheckers_window_on_import_dialog_cancel_clicked),
                   data);
  g_signal_connect(back_button,
                   "clicked",
                   G_CALLBACK(gcheckers_window_on_import_dialog_back_clicked),
                   data);
  g_signal_connect(next_button,
                   "clicked",
                   G_CALLBACK(gcheckers_window_on_import_dialog_next_clicked),
                   data);
  g_signal_connect(dialog,
                   "destroy",
                   G_CALLBACK(gcheckers_window_on_import_dialog_destroy),
                   data);

  gcheckers_window_import_dialog_update_step(data);
  gtk_window_present(GTK_WINDOW(dialog));
}
