#include "window.h"

#include "app_paths.h"
#include "bga_client.h"
#include "common_settings.h"
#include "game_app_profile.h"
#include "sgf_io.h"
#include "sgf_metadata.h"

#include <errno.h>
#include <glib/gstdio.h>

#define GGAME_IMPORT_BGA_CACHE_ENV "GCHECKERS_BGA_IMPORT_DIR"
#define GGAME_IMPORT_BGA_CACHE_SUBDIR "bga-imports"

typedef enum {
  GGAME_IMPORT_SITE_LIDRAUGHT = 0,
  GGAME_IMPORT_SITE_FLYORDIE,
  GGAME_IMPORT_SITE_PLAYOK,
  GGAME_IMPORT_SITE_BOARDGAMEARENA
} GGameImportSite;

typedef enum {
  GGAME_IMPORT_STEP_SITE = 0,
  GGAME_IMPORT_STEP_CREDENTIALS,
  GGAME_IMPORT_STEP_HISTORY
} GGameImportStep;

typedef struct {
  GGameWindow *self;
  const GGameAppProfile *profile;
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
  BgaClientSession *bga_session;
  GGameImportStep step;
} GGameWindowImportDialogData;

typedef struct {
  char *path;
  char *date;
  char *player_one;
  char *player_two;
  char *winner;
  char *table_id;
} GGameLibraryEntry;

typedef struct {
  GGameWindow *self;
  GtkWindow *dialog;
  GtkListBox *list;
  GtkButton *load_button;
} GGameWindowLibraryDialogData;

static void ggame_library_entry_free(gpointer data) {
  GGameLibraryEntry *entry = data;
  if (entry == NULL) {
    return;
  }

  g_free(entry->path);
  g_free(entry->date);
  g_free(entry->player_one);
  g_free(entry->player_two);
  g_free(entry->winner);
  g_free(entry->table_id);
  g_free(entry);
}

static void ggame_window_library_dialog_data_free(GGameWindowLibraryDialogData *data) {
  if (data == NULL) {
    return;
  }

  g_clear_object(&data->self);
  g_free(data);
}

static void ggame_import_dialog_load_credentials(GGameWindowImportDialogData *data) {
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

  gboolean remember = g_settings_get_boolean(data->settings, GGAME_COMMON_SETTINGS_KEY_IMPORT_REMEMBER);
  gtk_check_button_set_active(data->remember_check, remember);
  if (!remember) {
    gtk_editable_set_text(GTK_EDITABLE(data->email_entry), "");
    gtk_editable_set_text(GTK_EDITABLE(data->password_entry), "");
    return;
  }

  g_autofree char *email = g_settings_get_string(data->settings, GGAME_COMMON_SETTINGS_KEY_IMPORT_EMAIL);
  g_autofree char *password = g_settings_get_string(data->settings, GGAME_COMMON_SETTINGS_KEY_IMPORT_PASSWORD);
  gtk_editable_set_text(GTK_EDITABLE(data->email_entry), email ? email : "");
  gtk_editable_set_text(GTK_EDITABLE(data->password_entry), password ? password : "");
}

static void ggame_import_dialog_save_credentials(GGameWindowImportDialogData *data) {
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_ENTRY(data->email_entry));
  g_return_if_fail(GTK_IS_ENTRY(data->password_entry));
  g_return_if_fail(GTK_IS_CHECK_BUTTON(data->remember_check));

  if (!G_IS_SETTINGS(data->settings)) {
    return;
  }

  gboolean remember = gtk_check_button_get_active(data->remember_check);
  g_settings_set_boolean(data->settings, GGAME_COMMON_SETTINGS_KEY_IMPORT_REMEMBER, remember);
  if (!remember) {
    g_settings_set_string(data->settings, GGAME_COMMON_SETTINGS_KEY_IMPORT_EMAIL, "");
    g_settings_set_string(data->settings, GGAME_COMMON_SETTINGS_KEY_IMPORT_PASSWORD, "");
    return;
  }

  const char *email = gtk_editable_get_text(GTK_EDITABLE(data->email_entry));
  const char *password = gtk_editable_get_text(GTK_EDITABLE(data->password_entry));
  g_settings_set_string(data->settings, GGAME_COMMON_SETTINGS_KEY_IMPORT_EMAIL, email ? email : "");
  g_settings_set_string(data->settings, GGAME_COMMON_SETTINGS_KEY_IMPORT_PASSWORD, password ? password : "");
}

static void ggame_import_dialog_on_error_ok_clicked(GtkButton *button, gpointer user_data) {
  GtkWindow *error_dialog = GTK_WINDOW(user_data);
  g_return_if_fail(GTK_IS_BUTTON(button));
  g_return_if_fail(GTK_IS_WINDOW(error_dialog));

  GtkWindow *wizard_dialog = g_object_get_data(G_OBJECT(error_dialog), "wizard-dialog");
  if (GTK_IS_WINDOW(wizard_dialog)) {
    gtk_window_destroy(wizard_dialog);
  }
  gtk_window_destroy(error_dialog);
}

static void ggame_import_dialog_show_error_and_close_wizard(GGameWindowImportDialogData *data,
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
  g_signal_connect(ok_button, "clicked", G_CALLBACK(ggame_import_dialog_on_error_ok_clicked), dialog);
  gtk_window_present(GTK_WINDOW(dialog));
}

static gboolean ggame_import_dialog_is_board_game_arena_selected(GGameWindowImportDialogData *data) {
  g_return_val_if_fail(data != NULL, FALSE);

  if (data->profile != NULL && !data->profile->import.show_site_step) {
    return TRUE;
  }

  g_return_val_if_fail(GTK_IS_DROP_DOWN(data->site_drop_down), FALSE);
  return gtk_drop_down_get_selected(data->site_drop_down) == GGAME_IMPORT_SITE_BOARDGAMEARENA;
}

static const char *ggame_import_dialog_selected_history_table_id(GGameWindowImportDialogData *data) {
  g_return_val_if_fail(data != NULL, NULL);
  g_return_val_if_fail(GTK_IS_LIST_BOX(data->history_list), NULL);

  GtkListBoxRow *row = gtk_list_box_get_selected_row(data->history_list);
  if (row == NULL) {
    return NULL;
  }

  return g_object_get_data(G_OBJECT(row), "bga-table-id");
}

static gboolean ggame_import_dialog_selected_history_is_cached(GGameWindowImportDialogData *data) {
  g_return_val_if_fail(data != NULL, FALSE);
  g_return_val_if_fail(GTK_IS_LIST_BOX(data->history_list), FALSE);

  GtkListBoxRow *row = gtk_list_box_get_selected_row(data->history_list);
  if (row == NULL) {
    return FALSE;
  }

  return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "bga-cached"));
}

static char *ggame_import_dialog_sanitize_cache_name(const char *text) {
  g_return_val_if_fail(text != NULL, NULL);

  GString *sanitized = g_string_new(NULL);
  for (const char *c = text; *c != '\0'; c++) {
    if (g_ascii_isalnum(*c) || *c == '-' || *c == '_') {
      g_string_append_c(sanitized, *c);
    } else {
      g_string_append_c(sanitized, '_');
    }
  }
  if (sanitized->len == 0) {
    g_string_append(sanitized, "unknown");
  }

  return g_string_free(sanitized, FALSE);
}

static char *ggame_import_dialog_bga_profile_cache_dir(const GGameAppProfile *profile, GError **error) {
  g_return_val_if_fail(profile != NULL, NULL);
  g_return_val_if_fail(profile->id != NULL, NULL);

  g_autofree char *base_dir =
      ggame_app_paths_get_user_state_subdir(GGAME_IMPORT_BGA_CACHE_ENV, GGAME_IMPORT_BGA_CACHE_SUBDIR, error);
  if (base_dir == NULL) {
    return NULL;
  }

  g_autofree char *profile_dir_name = ggame_import_dialog_sanitize_cache_name(profile->id);
  g_autofree char *profile_dir = g_build_filename(base_dir, profile_dir_name, NULL);
  if (g_mkdir_with_parents(profile_dir, 0755) != 0) {
    int saved_errno = errno;
    g_set_error(error,
                G_FILE_ERROR,
                g_file_error_from_errno(saved_errno),
                "Failed to create BoardGameArena import cache directory %s: %s",
                profile_dir,
                g_strerror(saved_errno));
    return NULL;
  }

  return g_steal_pointer(&profile_dir);
}

static char *ggame_import_dialog_bga_cache_path_for_profile(const GGameAppProfile *profile,
                                                            const char *table_id,
                                                            GError **error) {
  g_return_val_if_fail(profile != NULL, NULL);
  g_return_val_if_fail(table_id != NULL, NULL);

  g_autofree char *profile_dir = ggame_import_dialog_bga_profile_cache_dir(profile, error);
  if (profile_dir == NULL) {
    return NULL;
  }

  g_autofree char *cache_name = ggame_import_dialog_sanitize_cache_name(table_id);
  g_autofree char *file_name = g_strdup_printf("%s.sgf", cache_name);
  return g_build_filename(profile_dir, file_name, NULL);
}

static char *ggame_import_dialog_bga_cache_path(GGameWindowImportDialogData *data,
                                                const char *table_id,
                                                GError **error) {
  g_return_val_if_fail(data != NULL, NULL);
  g_return_val_if_fail(data->profile != NULL, NULL);
  g_return_val_if_fail(table_id != NULL, NULL);

  return ggame_import_dialog_bga_cache_path_for_profile(data->profile, table_id, error);
}

static gboolean ggame_import_dialog_bga_table_is_cached(GGameWindowImportDialogData *data,
                                                        const char *table_id) {
  g_return_val_if_fail(data != NULL, FALSE);
  g_return_val_if_fail(table_id != NULL, FALSE);

  g_autoptr(GError) error = NULL;
  g_autofree char *path = ggame_import_dialog_bga_cache_path(data, table_id, &error);
  if (path == NULL) {
    g_debug("Unable to resolve BoardGameArena import cache path for table_id=%s: %s",
            table_id,
            error ? error->message : "unknown error");
    return FALSE;
  }

  return g_file_test(path, G_FILE_TEST_IS_REGULAR);
}

static gboolean ggame_window_load_sgf_path_from_library(GGameWindow *window, const char *path, GError **error) {
  g_return_val_if_fail(GGAME_IS_WINDOW(window), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  GGameSgfController *controller = ggame_window_get_sgf_controller(window);
  if (!ggame_sgf_controller_load_file(controller, path, error)) {
    return FALSE;
  }

  SgfTree *tree = ggame_sgf_controller_get_tree(controller);
  const GameBackendVariant *loaded_variant = NULL;
  if (tree != NULL && sgf_io_tree_get_variant(tree, &loaded_variant, NULL) && loaded_variant != NULL) {
    ggame_window_set_loaded_variant(window, loaded_variant);
  }
  ggame_window_set_loaded_source_path(window, path);
  ggame_window_set_board_orientation_mode(window, GGAME_WINDOW_BOARD_ORIENTATION_FIXED);
  return TRUE;
}

static gboolean ggame_import_dialog_load_sgf_path(GGameWindowImportDialogData *data,
                                                  const char *path,
                                                  GError **error) {
  g_return_val_if_fail(data != NULL, FALSE);
  g_return_val_if_fail(GGAME_IS_WINDOW(data->self), FALSE);

  if (!ggame_window_load_sgf_path_from_library(data->self, path, error)) {
    return FALSE;
  }

  return TRUE;
}

static void ggame_window_import_dialog_data_free(GGameWindowImportDialogData *data) {
  g_return_if_fail(data != NULL);

  g_clear_pointer(&data->bga_session, bga_client_session_free);
  g_clear_object(&data->settings);
  g_object_unref(data->self);
  g_free(data);
}

static void ggame_window_on_import_dialog_destroy(GtkWindow * /*dialog*/, gpointer user_data) {
  GGameWindowImportDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  ggame_window_import_dialog_data_free(data);
}

static void ggame_window_on_library_dialog_destroy(GtkWindow * /*dialog*/, gpointer user_data) {
  GGameWindowLibraryDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  ggame_window_library_dialog_data_free(data);
}

static GGameLibraryEntry *ggame_library_entry_new_from_sgf_path(const char *path) {
  g_return_val_if_fail(path != NULL, NULL);

  g_autoptr(GError) error = NULL;
  g_autoptr(SgfTree) tree = NULL;
  if (!sgf_io_load_file(path, &tree, &error)) {
    g_debug("Skipping imported game library entry %s: %s", path, error != NULL ? error->message : "unknown error");
    return NULL;
  }

  const SgfNode *root = sgf_tree_get_root(tree);
  if (root == NULL) {
    g_debug("Skipping imported game library entry %s without an SGF root", path);
    return NULL;
  }

  GGameLibraryEntry *entry = g_new0(GGameLibraryEntry, 1);
  entry->path = g_strdup(path);
  entry->date = g_strdup(sgf_node_get_property_first(root, GGAME_SGF_PROP_DATE));
  entry->player_one = g_strdup(sgf_node_get_property_first(root, "PB"));
  entry->player_two = g_strdup(sgf_node_get_property_first(root, "PW"));
  entry->winner = g_strdup(sgf_node_get_property_first(root, GGAME_SGF_PROP_RESULT));
  entry->table_id = g_strdup(sgf_node_get_property_first(root, GGAME_SGF_PROP_BGA_TABLE_ID));
  if (entry->table_id == NULL || entry->table_id[0] == '\0') {
    g_autofree char *basename = g_path_get_basename(path);
    if (g_str_has_suffix(basename, ".sgf")) {
      basename[strlen(basename) - 4] = '\0';
    }
    g_free(entry->table_id);
    entry->table_id = g_strdup(basename);
  }

  return entry;
}

static gint ggame_library_entry_compare(gconstpointer a, gconstpointer b) {
  const GGameLibraryEntry *left = *(GGameLibraryEntry * const *)a;
  const GGameLibraryEntry *right = *(GGameLibraryEntry * const *)b;

  g_return_val_if_fail(left != NULL, 0);
  g_return_val_if_fail(right != NULL, 0);

  gint date_order = g_strcmp0(right->date, left->date);
  if (date_order != 0) {
    return date_order;
  }

  return g_strcmp0(left->table_id, right->table_id);
}

static GPtrArray *ggame_library_collect_imported_games(const GGameAppProfile *profile, GError **error) {
  g_return_val_if_fail(profile != NULL, NULL);

  g_autofree char *profile_dir = ggame_import_dialog_bga_profile_cache_dir(profile, error);
  if (profile_dir == NULL) {
    return NULL;
  }

  GPtrArray *entries = g_ptr_array_new_with_free_func(ggame_library_entry_free);
  g_autoptr(GDir) dir = g_dir_open(profile_dir, 0, error);
  if (dir == NULL) {
    g_ptr_array_unref(entries);
    return NULL;
  }

  const char *name = NULL;
  while ((name = g_dir_read_name(dir)) != NULL) {
    if (!g_str_has_suffix(name, ".sgf")) {
      continue;
    }

    g_autofree char *path = g_build_filename(profile_dir, name, NULL);
    GGameLibraryEntry *entry = ggame_library_entry_new_from_sgf_path(path);
    if (entry != NULL) {
      g_ptr_array_add(entries, entry);
    }
  }
  g_ptr_array_sort(entries, ggame_library_entry_compare);

  return entries;
}

static GtkWidget *ggame_library_new_cell(const char *text, gboolean header, int width) {
  GtkWidget *label = gtk_label_new(text != NULL ? text : "");
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
  gtk_widget_set_halign(label, GTK_ALIGN_FILL);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_widget_set_size_request(label, width, -1);
  if (header) {
    gtk_widget_add_css_class(label, "heading");
  }
  return label;
}

static GtkWidget *ggame_library_new_grid_row(const GGameLibraryEntry *entry) {
  g_return_val_if_fail(entry != NULL, NULL);

  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
  gtk_widget_set_margin_top(grid, 4);
  gtk_widget_set_margin_bottom(grid, 4);
  gtk_widget_set_margin_start(grid, 6);
  gtk_widget_set_margin_end(grid, 6);

  gtk_grid_attach(GTK_GRID(grid), ggame_library_new_cell(entry->date, FALSE, 92), 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), ggame_library_new_cell(entry->player_one, FALSE, 140), 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), ggame_library_new_cell(entry->player_two, FALSE, 140), 2, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), ggame_library_new_cell(entry->winner, FALSE, 120), 3, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), ggame_library_new_cell(entry->table_id, FALSE, 96), 4, 0, 1, 1);
  return grid;
}

static GtkWidget *ggame_library_new_header_row(void) {
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
  gtk_widget_set_margin_top(grid, 4);
  gtk_widget_set_margin_bottom(grid, 4);
  gtk_widget_set_margin_start(grid, 6);
  gtk_widget_set_margin_end(grid, 6);

  gtk_grid_attach(GTK_GRID(grid), ggame_library_new_cell("Date", TRUE, 92), 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), ggame_library_new_cell("Player 1", TRUE, 140), 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), ggame_library_new_cell("Player 2", TRUE, 140), 2, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), ggame_library_new_cell("Winner", TRUE, 120), 3, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), ggame_library_new_cell("Table ID", TRUE, 96), 4, 0, 1, 1);
  return grid;
}

static void ggame_library_dialog_update_load_button(GGameWindowLibraryDialogData *data) {
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_LIST_BOX(data->list));
  g_return_if_fail(GTK_IS_BUTTON(data->load_button));

  GtkListBoxRow *row = gtk_list_box_get_selected_row(data->list);
  const char *path = row != NULL ? g_object_get_data(G_OBJECT(row), "library-path") : NULL;
  gtk_widget_set_sensitive(GTK_WIDGET(data->load_button), path != NULL && path[0] != '\0');
}

static void ggame_window_on_library_row_selected(GtkListBox * /*box*/,
                                                 GtkListBoxRow * /*row*/,
                                                 gpointer user_data) {
  GGameWindowLibraryDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  ggame_library_dialog_update_load_button(data);
}

static void ggame_window_on_library_cancel_clicked(GtkButton * /*button*/, gpointer user_data) {
  GGameWindowLibraryDialogData *data = user_data;
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_WINDOW(data->dialog));

  gtk_window_destroy(data->dialog);
}

static void ggame_window_on_library_error_ok_clicked(GtkButton * /*button*/, gpointer user_data) {
  GtkWindow *dialog = GTK_WINDOW(user_data);
  g_return_if_fail(GTK_IS_WINDOW(dialog));

  gtk_window_destroy(dialog);
}

static void ggame_library_dialog_show_error(GGameWindowLibraryDialogData *data, const char *message) {
  g_return_if_fail(data != NULL);
  g_return_if_fail(message != NULL);

  GtkWidget *dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog), "Library error");
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_transient_for(GTK_WINDOW(dialog), data->dialog);
  gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(content, 12);
  gtk_widget_set_margin_bottom(content, 12);
  gtk_widget_set_margin_start(content, 12);
  gtk_widget_set_margin_end(content, 12);
  gtk_window_set_child(GTK_WINDOW(dialog), content);

  GtkWidget *label = gtk_label_new(message);
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(content), label);

  GtkWidget *ok_button = gtk_button_new_with_label("OK");
  gtk_widget_set_halign(ok_button, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(content), ok_button);
  g_signal_connect(ok_button, "clicked", G_CALLBACK(ggame_window_on_library_error_ok_clicked), dialog);

  gtk_window_present(GTK_WINDOW(dialog));
}

static void ggame_window_on_library_load_clicked(GtkButton * /*button*/, gpointer user_data) {
  GGameWindowLibraryDialogData *data = user_data;
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_LIST_BOX(data->list));

  GtkListBoxRow *row = gtk_list_box_get_selected_row(data->list);
  const char *path = row != NULL ? g_object_get_data(G_OBJECT(row), "library-path") : NULL;
  if (path == NULL || path[0] == '\0') {
    ggame_library_dialog_update_load_button(data);
    return;
  }

  g_autoptr(GError) error = NULL;
  if (!ggame_window_load_sgf_path_from_library(data->self, path, &error)) {
    g_debug("Failed to load library game %s: %s", path, error != NULL ? error->message : "unknown error");
    ggame_library_dialog_show_error(data, "Unable to load the selected game.");
    return;
  }

  gtk_window_destroy(data->dialog);
}

static void ggame_window_import_dialog_update_step(GGameWindowImportDialogData *data) {
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_STACK(data->stack));
  g_return_if_fail(GTK_IS_BUTTON(data->back_button));
  g_return_if_fail(GTK_IS_BUTTON(data->next_button));

  if (data->step == GGAME_IMPORT_STEP_SITE) {
    gtk_stack_set_visible_child_name(data->stack, "site");
    gtk_widget_set_sensitive(GTK_WIDGET(data->back_button), FALSE);
    gtk_button_set_label(data->next_button, "Next");
    gtk_widget_set_sensitive(GTK_WIDGET(data->next_button),
                             ggame_import_dialog_is_board_game_arena_selected(data));
    return;
  }

  if (data->step == GGAME_IMPORT_STEP_HISTORY) {
    gtk_stack_set_visible_child_name(data->stack, "history");
    gtk_widget_set_sensitive(GTK_WIDGET(data->back_button), FALSE);
    const char *selected_table_id = ggame_import_dialog_selected_history_table_id(data);
    gtk_button_set_label(data->next_button,
                         ggame_import_dialog_selected_history_is_cached(data) ? "Load" : "Import");
    gtk_widget_set_sensitive(GTK_WIDGET(data->next_button), selected_table_id != NULL);
    return;
  }

  gtk_stack_set_visible_child_name(data->stack, "credentials");
  gtk_widget_set_sensitive(GTK_WIDGET(data->back_button),
                           data->profile != NULL && data->profile->import.show_site_step);
  gtk_button_set_label(data->next_button, "Fetch game history");
  gtk_widget_set_sensitive(GTK_WIDGET(data->next_button), TRUE);
  ggame_import_dialog_load_credentials(data);
}

static void ggame_window_on_import_dialog_site_notify(GObject * /*object*/,
                                                      GParamSpec * /*pspec*/,
                                                      gpointer user_data) {
  GGameWindowImportDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  if (data->step != GGAME_IMPORT_STEP_SITE) {
    return;
  }

  ggame_window_import_dialog_update_step(data);
}

static void ggame_window_on_import_dialog_history_row_selected(GtkListBox * /*box*/,
                                                               GtkListBoxRow * /*row*/,
                                                               gpointer user_data) {
  GGameWindowImportDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  if (data->step != GGAME_IMPORT_STEP_HISTORY) {
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

  if (data->step == GGAME_IMPORT_STEP_SITE || data->profile == NULL || !data->profile->import.show_site_step) {
    return;
  }

  data->step = GGAME_IMPORT_STEP_SITE;
  ggame_window_import_dialog_update_step(data);
}

void ggame_window_present_library_dialog(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  const GGameAppProfile *profile = ggame_active_app_profile();
  g_return_if_fail(profile != NULL);

  GtkWidget *dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog), "Library");
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(self));
  gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 760, 360);

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(content, 12);
  gtk_widget_set_margin_bottom(content, 12);
  gtk_widget_set_margin_start(content, 12);
  gtk_widget_set_margin_end(content, 12);
  gtk_window_set_child(GTK_WINDOW(dialog), content);

  GtkWidget *title = gtk_label_new("Imported games");
  gtk_widget_set_halign(title, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(content), title);
  gtk_box_append(GTK_BOX(content), ggame_library_new_header_row());

  GtkWidget *scroll = gtk_scrolled_window_new();
  gtk_widget_set_hexpand(scroll, TRUE);
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_append(GTK_BOX(content), scroll);

  GtkListBox *list = GTK_LIST_BOX(gtk_list_box_new());
  gtk_list_box_set_selection_mode(list, GTK_SELECTION_SINGLE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(list));

  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) entries = ggame_library_collect_imported_games(profile, &error);
  if (entries == NULL) {
    g_debug("Unable to collect imported game library: %s", error != NULL ? error->message : "unknown error");
    entries = g_ptr_array_new_with_free_func(ggame_library_entry_free);
  }

  for (guint i = 0; i < entries->len; i++) {
    GGameLibraryEntry *entry = g_ptr_array_index(entries, i);
    GtkWidget *row = gtk_list_box_row_new();
    g_object_set_data_full(G_OBJECT(row), "library-path", g_strdup(entry->path), g_free);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), ggame_library_new_grid_row(entry));
    gtk_list_box_append(list, row);
  }

  if (entries->len == 0) {
    GtkWidget *empty_label = gtk_label_new("No imported games.");
    gtk_widget_set_halign(empty_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(content), empty_label);
  }

  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(content), actions);

  GtkWidget *cancel_button = gtk_button_new_with_label("Cancel");
  GtkWidget *load_button = gtk_button_new_with_label("Load");
  gtk_widget_set_sensitive(load_button, FALSE);
  gtk_box_append(GTK_BOX(actions), cancel_button);
  gtk_box_append(GTK_BOX(actions), load_button);

  GGameWindowLibraryDialogData *data = g_new0(GGameWindowLibraryDialogData, 1);
  data->self = g_object_ref(self);
  data->dialog = GTK_WINDOW(dialog);
  data->list = list;
  data->load_button = GTK_BUTTON(load_button);

  g_signal_connect(cancel_button, "clicked", G_CALLBACK(ggame_window_on_library_cancel_clicked), data);
  g_signal_connect(load_button, "clicked", G_CALLBACK(ggame_window_on_library_load_clicked), data);
  g_signal_connect(list, "row-selected", G_CALLBACK(ggame_window_on_library_row_selected), data);
  g_signal_connect(dialog, "destroy", G_CALLBACK(ggame_window_on_library_dialog_destroy), data);

  gtk_window_present(GTK_WINDOW(dialog));
}

static void ggame_import_dialog_import_selected_history_game(GGameWindowImportDialogData *data) {
  g_return_if_fail(data != NULL);

  const char *table_id = ggame_import_dialog_selected_history_table_id(data);
  if (table_id == NULL) {
    g_debug("Import flow: Import clicked without a selected history row");
    ggame_window_import_dialog_update_step(data);
    return;
  }

  g_autoptr(GError) error = NULL;
  g_autofree char *cache_path = ggame_import_dialog_bga_cache_path(data, table_id, &error);
  if (cache_path == NULL) {
    g_debug("Import flow: failed to resolve BGA import cache path for table_id=%s: %s",
            table_id,
            error ? error->message : "unknown error");
    ggame_import_dialog_show_error_and_close_wizard(data, "Unable to resolve BoardGameArena import cache path.");
    return;
  }

  if (ggame_import_dialog_selected_history_is_cached(data)) {
    g_debug("Import flow: loading cached BoardGameArena table_id=%s from %s", table_id, cache_path);
    if (!ggame_import_dialog_load_sgf_path(data, cache_path, &error)) {
      g_debug("Import flow: failed to load cached BoardGameArena table_id=%s: %s",
              table_id,
              error ? error->message : "unknown error");
      ggame_import_dialog_show_error_and_close_wizard(data, "Unable to load cached BoardGameArena game.");
      return;
    }
    gtk_window_destroy(data->dialog);
    return;
  }

  if (data->bga_session == NULL) {
    g_debug("Import flow: missing BoardGameArena session while importing table_id=%s", table_id);
    ggame_import_dialog_show_error_and_close_wizard(
        data,
        "The BoardGameArena session is no longer available.");
    return;
  }
  if (data->profile == NULL || data->profile->kind != GGAME_APP_KIND_HOMEWORLDS) {
    ggame_import_dialog_show_error_and_close_wizard(
        data,
        "BoardGameArena archive import parsing is only implemented for Homeworlds right now.");
    return;
  }

  g_debug("Import flow: fetching BoardGameArena archive logs for table_id=%s", table_id);
  g_autofree char *debug_path = NULL;
  BgaHttpResponse response = {0};
  if (!bga_client_session_fetch_archive_logs(data->bga_session, table_id, &response, &debug_path, &error)) {
    g_debug("Import flow: failed to fetch BoardGameArena archive logs for table_id=%s: %s",
            table_id,
            error ? error->message : "unknown error");
    ggame_import_dialog_show_error_and_close_wizard(
        data,
        "Unable to fetch BoardGameArena archive logs.");
    bga_http_response_clear(&response);
    return;
  }

  g_debug("Import flow: archive logs HTTP status=%ld saved to %s",
          response.http_status,
          debug_path ? debug_path : "(null)");
  g_autofree char *sgf = NULL;
  if (!bga_client_parse_homeworlds_archive_logs_sgf(response.body ? response.body : "", table_id, &sgf, &error)) {
    g_debug("Import flow: failed to parse BoardGameArena archive logs for table_id=%s: %s",
            table_id,
            error ? error->message : "unknown error");
    ggame_import_dialog_show_error_and_close_wizard(data, "Unable to parse BoardGameArena archive logs.");
    bga_http_response_clear(&response);
    return;
  }
  bga_http_response_clear(&response);

  if (!g_file_set_contents(cache_path, sgf, -1, &error)) {
    g_debug("Import flow: failed to store BoardGameArena SGF cache for table_id=%s in %s: %s",
            table_id,
            cache_path,
            error ? error->message : "unknown error");
    ggame_import_dialog_show_error_and_close_wizard(data, "Unable to store imported BoardGameArena game.");
    return;
  }

  if (!ggame_import_dialog_load_sgf_path(data, cache_path, &error)) {
    g_debug("Import flow: failed to load imported BoardGameArena SGF for table_id=%s from %s: %s",
            table_id,
            cache_path,
            error ? error->message : "unknown error");
    ggame_import_dialog_show_error_and_close_wizard(data, "Unable to load imported BoardGameArena game.");
    return;
  }

  gtk_window_destroy(data->dialog);
}

static void ggame_window_on_import_dialog_next_clicked(GtkButton * /*button*/, gpointer user_data) {
  GGameWindowImportDialogData *data = user_data;
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_WINDOW(data->dialog));
  g_return_if_fail(GTK_IS_ENTRY(data->email_entry));
  g_return_if_fail(GTK_IS_CHECK_BUTTON(data->remember_check));
  g_return_if_fail(GTK_IS_LIST_BOX(data->history_list));

  g_debug("Import flow: Next clicked at step=%d", (int)data->step);

  if (data->step == GGAME_IMPORT_STEP_SITE) {
    if (!ggame_import_dialog_is_board_game_arena_selected(data)) {
      g_debug("Import source not implemented yet");
      return;
    }

    data->step = GGAME_IMPORT_STEP_CREDENTIALS;
    g_debug("Import flow: moving to credentials step");
    ggame_window_import_dialog_update_step(data);
    return;
  }

  if (data->step == GGAME_IMPORT_STEP_HISTORY) {
    ggame_import_dialog_import_selected_history_game(data);
    return;
  }

  if (data->profile == NULL || data->profile->import.board_game_arena_game_id == 0) {
    g_debug("Import flow: no BoardGameArena import game id configured");
    ggame_import_dialog_show_error_and_close_wizard(data, "This game does not support BoardGameArena import.");
    return;
  }

  g_debug("Import flow: starting BGA login sequence");
  g_clear_pointer(&data->bga_session, bga_client_session_free);
  ggame_import_dialog_save_credentials(data);
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
    ggame_import_dialog_show_error_and_close_wizard(
        data,
        "Unable to initialize BoardGameArena session.");
    return;
  }

  g_autofree char *request_token = NULL;
  if (!bga_client_session_fetch_homepage_and_request_token(session, NULL, &request_token, &error)) {
    g_debug("Failed to fetch BoardGameArena request token: %s", error ? error->message : "unknown error");
    ggame_import_dialog_show_error_and_close_wizard(
        data,
        "Unable to fetch BoardGameArena request token.");
    bga_client_session_free(session);
    return;
  }

  BgaHttpResponse login_response = {0};
  if (!bga_client_session_login_with_password(session, &credentials, request_token, &login_response, &error)) {
    g_debug("Failed to login to BoardGameArena: %s", error ? error->message : "unknown error");
    ggame_import_dialog_show_error_and_close_wizard(
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
    ggame_import_dialog_show_error_and_close_wizard(
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
    ggame_import_dialog_show_error_and_close_wizard(data, dialog_text);
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
    ggame_import_dialog_show_error_and_close_wizard(
        data,
        "BoardGameArena login succeeded but user_id is missing.");
    bga_client_session_free(session);
    return;
  }
  g_debug("Import flow: fetching BoardGameArena game_id=%u history for user_id=%s",
          data->profile->import.board_game_arena_game_id,
          history_user_id);

  BgaHttpResponse history_response = {0};
  if (!bga_client_session_fetch_game_history(session,
                                             history_user_id,
                                             data->profile->import.board_game_arena_game_id,
                                             &history_response,
                                             &error)) {
    g_debug("Import flow: failed to fetch BoardGameArena history: %s",
            error ? error->message : "unknown error");
    g_autofree char *dialog_text =
        g_strdup_printf("Unable to fetch BoardGameArena %s history.", data->profile->display_name);
    ggame_import_dialog_show_error_and_close_wizard(
        data,
        dialog_text);
    bga_http_response_clear(&history_response);
    bga_client_session_free(session);
    return;
  }
  g_debug("Import flow: history HTTP status=%ld", history_response.http_status);

  g_autoptr(GPtrArray) games = NULL;
  if (!bga_client_parse_history_games(history_response.body ? history_response.body : "", &games, &error)) {
    g_debug("Import flow: failed to parse BoardGameArena history: %s",
            error ? error->message : "unknown error");
    g_autofree char *dialog_text =
        g_strdup_printf("Unable to parse BoardGameArena %s history.", data->profile->display_name);
    ggame_import_dialog_show_error_and_close_wizard(
        data,
        dialog_text);
    bga_http_response_clear(&history_response);
    bga_client_session_free(session);
    return;
  }
  bga_http_response_clear(&history_response);
  g_debug("Import flow: parsed %u BoardGameArena games", games->len);

  data->bga_session = session;
  session = NULL;

  GtkWidget *row = gtk_widget_get_first_child(GTK_WIDGET(data->history_list));
  while (row != NULL) {
    GtkWidget *next = gtk_widget_get_next_sibling(row);
    gtk_list_box_remove(data->history_list, row);
    row = next;
  }

  for (guint i = 0; i < games->len; ++i) {
    BgaHistoryGameSummary *summary = g_ptr_array_index(games, i);
    g_return_if_fail(summary != NULL);

    GtkWidget *history_row = gtk_list_box_row_new();
    g_object_set_data_full(G_OBJECT(history_row), "bga-table-id", g_strdup(summary->table_id), g_free);
    gboolean cached = ggame_import_dialog_bga_table_is_cached(data, summary->table_id);
    g_object_set_data(G_OBJECT(history_row), "bga-cached", GINT_TO_POINTER(cached));

    GtkWidget *line = gtk_label_new(NULL);
    g_autofree char *text = g_strdup_printf("%s  |  %s  |  %s vs %s%s",
                                            summary->start_at ? summary->start_at : "",
                                            summary->table_id ? summary->table_id : "",
                                            summary->player_one ? summary->player_one : "",
                                            summary->player_two ? summary->player_two : "",
                                            cached ? " (cached)" : "");
    gtk_label_set_text(GTK_LABEL(line), text);
    gtk_widget_set_halign(line, GTK_ALIGN_START);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(history_row), line);
    gtk_list_box_append(data->history_list, history_row);
  }
  gtk_list_box_unselect_all(data->history_list);

  data->step = GGAME_IMPORT_STEP_HISTORY;
  ggame_window_import_dialog_update_step(data);
  g_debug("Import flow: switched wizard to history step");
}

void ggame_window_present_import_dialog(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  const GGameAppProfile *profile = ggame_active_app_profile();
  g_return_if_fail(profile != NULL);
  g_return_if_fail(profile->features.supports_import);
  g_return_if_fail(profile->import.board_game_arena_game_id > 0);

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
  gtk_drop_down_set_selected(site_drop_down, GGAME_IMPORT_SITE_BOARDGAMEARENA);
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
  g_autofree char *history_title_text =
      g_strdup_printf("BoardGameArena %s history", profile->display_name);
  GtkWidget *history_title = gtk_label_new(history_title_text);
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
  gtk_list_box_set_selection_mode(history_list, GTK_SELECTION_SINGLE);
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
  data->profile = profile;
  data->dialog = GTK_WINDOW(dialog);
  data->stack = GTK_STACK(stack);
  data->site_drop_down = site_drop_down;
  data->back_button = GTK_BUTTON(back_button);
  data->next_button = GTK_BUTTON(next_button);
  data->email_entry = email_entry;
  data->password_entry = password_entry;
  data->remember_check = remember_check;
  data->history_list = history_list;
  data->settings = ggame_common_settings_create();
  data->step = profile->import.show_site_step ? GGAME_IMPORT_STEP_SITE : GGAME_IMPORT_STEP_CREDENTIALS;

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
  g_signal_connect(history_list,
                   "row-selected",
                   G_CALLBACK(ggame_window_on_import_dialog_history_row_selected),
                   data);
  g_signal_connect(dialog,
                   "destroy",
                   G_CALLBACK(ggame_window_on_import_dialog_destroy),
                   data);

  ggame_window_import_dialog_update_step(data);
  gtk_window_present(GTK_WINDOW(dialog));
}
