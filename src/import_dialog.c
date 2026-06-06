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
#define GGAME_IMPORT_BGA_HISTORY_CACHE_FILE "history.ini"
#define GGAME_IMPORT_BGA_HISTORY_PAGE_BATCH_SIZE 10

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
  BgaClientSession *session;
  char *user_id;
  GPtrArray *history_games;
  guint history_next_page;
  gboolean history_more_available;
} GGameBgaImportSessionCache;

typedef struct {
  GGameWindow *self;
  const GGameAppProfile *profile;
  GGameBgaImportSessionCache *bga_cache;
  GtkWindow *dialog;
  GtkStack *stack;
  GtkDropDown *site_drop_down;
  GtkButton *cancel_button;
  GtkButton *back_button;
  GtkButton *next_button;
  GtkButton *reload_button;
  GtkButton *more_button;
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

static GHashTable *bga_import_session_caches = NULL;
#ifdef GGAME_TESTING
static gboolean bga_import_dialog_test_auto_history_refresh_enabled = TRUE;
#endif

static gboolean ggame_import_dialog_save_bga_history_cache_for_profile(const GGameAppProfile *profile,
                                                                       GPtrArray *games,
                                                                       GError **error);
static GPtrArray *ggame_import_dialog_load_bga_history_cache_for_profile(const GGameAppProfile *profile,
                                                                         GError **error);

static BgaHistoryGameSummary *ggame_bga_history_game_summary_copy(const BgaHistoryGameSummary *summary) {
  g_return_val_if_fail(summary != NULL, NULL);

  BgaHistoryGameSummary *copy = g_new0(BgaHistoryGameSummary, 1);
  copy->table_id = g_strdup(summary->table_id);
  copy->start_at = g_strdup(summary->start_at);
  copy->player_one = g_strdup(summary->player_one);
  copy->player_two = g_strdup(summary->player_two);
  return copy;
}

static GPtrArray *ggame_bga_history_games_copy(GPtrArray *games) {
  g_return_val_if_fail(games != NULL, NULL);

  GPtrArray *copy = g_ptr_array_new_with_free_func((GDestroyNotify)bga_history_game_summary_free);
  for (guint i = 0; i < games->len; i++) {
    BgaHistoryGameSummary *summary = g_ptr_array_index(games, i);
    if (summary != NULL) {
      g_ptr_array_add(copy, ggame_bga_history_game_summary_copy(summary));
    }
  }
  return copy;
}

static gboolean ggame_bga_history_games_contains_table_id(GPtrArray *games, const char *table_id) {
  g_return_val_if_fail(games != NULL, FALSE);
  g_return_val_if_fail(table_id != NULL, FALSE);

  for (guint i = 0; i < games->len; ++i) {
    BgaHistoryGameSummary *summary = g_ptr_array_index(games, i);
    if (summary != NULL && g_strcmp0(summary->table_id, table_id) == 0) {
      return TRUE;
    }
  }
  return FALSE;
}

static GHashTable *ggame_bga_history_games_table_id_set(GPtrArray *games) {
  g_return_val_if_fail(games != NULL, NULL);

  GHashTable *table_ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  for (guint i = 0; i < games->len; ++i) {
    BgaHistoryGameSummary *summary = g_ptr_array_index(games, i);
    if (summary != NULL && summary->table_id != NULL && summary->table_id[0] != '\0') {
      g_hash_table_add(table_ids, g_strdup(summary->table_id));
    }
  }
  return table_ids;
}

static GPtrArray *ggame_bga_history_games_merge_new_then_cached(GPtrArray *new_games, GPtrArray *cached_games) {
  g_return_val_if_fail(new_games != NULL, NULL);
  g_return_val_if_fail(cached_games != NULL, NULL);

  GPtrArray *merged = g_ptr_array_new_with_free_func((GDestroyNotify)bga_history_game_summary_free);
  for (guint i = 0; i < new_games->len; ++i) {
    BgaHistoryGameSummary *summary = g_ptr_array_index(new_games, i);
    if (summary != NULL && summary->table_id != NULL && summary->table_id[0] != '\0' &&
        !ggame_bga_history_games_contains_table_id(merged, summary->table_id)) {
      g_ptr_array_add(merged, ggame_bga_history_game_summary_copy(summary));
    }
  }
  for (guint i = 0; i < cached_games->len; ++i) {
    BgaHistoryGameSummary *summary = g_ptr_array_index(cached_games, i);
    if (summary != NULL && summary->table_id != NULL && summary->table_id[0] != '\0' &&
        !ggame_bga_history_games_contains_table_id(merged, summary->table_id)) {
      g_ptr_array_add(merged, ggame_bga_history_game_summary_copy(summary));
    }
  }
  return merged;
}

static void ggame_bga_history_games_append_or_replace(GPtrArray *destination, GPtrArray *source) {
  g_return_if_fail(destination != NULL);
  g_return_if_fail(source != NULL);

  for (guint source_index = 0; source_index < source->len; ++source_index) {
    BgaHistoryGameSummary *source_summary = g_ptr_array_index(source, source_index);
    if (source_summary == NULL || source_summary->table_id == NULL || source_summary->table_id[0] == '\0') {
      continue;
    }

    gboolean replaced = FALSE;
    for (guint destination_index = 0; destination_index < destination->len; ++destination_index) {
      BgaHistoryGameSummary *destination_summary = g_ptr_array_index(destination, destination_index);
      if (destination_summary != NULL && g_strcmp0(destination_summary->table_id, source_summary->table_id) == 0) {
        g_ptr_array_remove_index(destination, destination_index);
        g_ptr_array_insert(destination, destination_index, ggame_bga_history_game_summary_copy(source_summary));
        replaced = TRUE;
        break;
      }
    }
    if (!replaced) {
      g_ptr_array_add(destination, ggame_bga_history_game_summary_copy(source_summary));
    }
  }
}

static void ggame_bga_import_session_cache_free(gpointer data) {
  GGameBgaImportSessionCache *cache = data;
  if (cache == NULL) {
    return;
  }

  g_clear_pointer(&cache->session, bga_client_session_free);
  g_clear_pointer(&cache->user_id, g_free);
  g_clear_pointer(&cache->history_games, g_ptr_array_unref);
  g_free(cache);
}

static GHashTable *ggame_bga_import_session_cache_table(void) {
  if (bga_import_session_caches == NULL) {
    bga_import_session_caches =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, ggame_bga_import_session_cache_free);
  }
  return bga_import_session_caches;
}

static GGameBgaImportSessionCache *ggame_bga_import_session_cache_get(const GGameAppProfile *profile) {
  g_return_val_if_fail(profile != NULL, NULL);
  g_return_val_if_fail(profile->id != NULL, NULL);

  GHashTable *caches = ggame_bga_import_session_cache_table();
  GGameBgaImportSessionCache *cache = g_hash_table_lookup(caches, profile->id);
  if (cache == NULL) {
    cache = g_new0(GGameBgaImportSessionCache, 1);
    g_hash_table_insert(caches, g_strdup(profile->id), cache);
  }
  return cache;
}

static gboolean ggame_bga_import_session_cache_has_history(const GGameBgaImportSessionCache *cache) {
  return cache != NULL && cache->session != NULL && cache->user_id != NULL && cache->user_id[0] != '\0' &&
         cache->history_games != NULL;
}

static gboolean ggame_bga_import_session_cache_has_session(const GGameBgaImportSessionCache *cache) {
  return cache != NULL && cache->session != NULL && cache->user_id != NULL && cache->user_id[0] != '\0';
}

static void ggame_bga_import_session_cache_store(GGameBgaImportSessionCache *cache,
                                                 BgaClientSession *session,
                                                 const char *user_id,
                                                 GPtrArray *history_games,
                                                 guint history_next_page,
                                                 gboolean history_more_available) {
  g_return_if_fail(cache != NULL);
  g_return_if_fail(session != NULL);
  g_return_if_fail(user_id != NULL);
  g_return_if_fail(history_games != NULL);

  if (cache->session != session) {
    g_clear_pointer(&cache->session, bga_client_session_free);
    cache->session = session;
  }
  g_free(cache->user_id);
  cache->user_id = g_strdup(user_id);
  g_clear_pointer(&cache->history_games, g_ptr_array_unref);
  cache->history_games = ggame_bga_history_games_copy(history_games);
  cache->history_next_page = history_next_page;
  cache->history_more_available = history_more_available;
}

static void ggame_bga_import_session_cache_store_history(GGameBgaImportSessionCache *cache,
                                                         GPtrArray *history_games,
                                                         guint history_next_page,
                                                         gboolean history_more_available) {
  g_return_if_fail(cache != NULL);
  g_return_if_fail(history_games != NULL);

  g_clear_pointer(&cache->history_games, g_ptr_array_unref);
  cache->history_games = ggame_bga_history_games_copy(history_games);
  cache->history_next_page = history_next_page;
  cache->history_more_available = history_more_available;
}

#ifdef GGAME_TESTING
void ggame_import_dialog_test_clear_bga_session_cache(void) {
  g_clear_pointer(&bga_import_session_caches, g_hash_table_unref);
  bga_import_dialog_test_auto_history_refresh_enabled = TRUE;
}

void ggame_import_dialog_test_set_auto_history_refresh_enabled(gboolean enabled) {
  bga_import_dialog_test_auto_history_refresh_enabled = enabled;
}

void ggame_import_dialog_test_seed_bga_history(const char *profile_id,
                                               const char *table_id,
                                               const char *start_at,
                                               const char *player_one,
                                               const char *player_two) {
  g_return_if_fail(profile_id != NULL);
  g_return_if_fail(table_id != NULL);

  GHashTable *caches = ggame_bga_import_session_cache_table();
  GGameBgaImportSessionCache *cache = g_hash_table_lookup(caches, profile_id);
  if (cache == NULL) {
    cache = g_new0(GGameBgaImportSessionCache, 1);
    g_hash_table_insert(caches, g_strdup(profile_id), cache);
  }

  g_autoptr(GError) error = NULL;
  BgaClientSession *session = bga_client_session_new(&error);
  if (session == NULL) {
    g_debug("Unable to seed BGA import session cache for tests: %s", error != NULL ? error->message : "unknown");
    return;
  }

  g_autoptr(GPtrArray) games = g_ptr_array_new_with_free_func((GDestroyNotify)bga_history_game_summary_free);
  BgaHistoryGameSummary *summary = g_new0(BgaHistoryGameSummary, 1);
  summary->table_id = g_strdup(table_id);
  summary->start_at = g_strdup(start_at != NULL ? start_at : "");
  summary->player_one = g_strdup(player_one != NULL ? player_one : "");
  summary->player_two = g_strdup(player_two != NULL ? player_two : "");
  g_ptr_array_add(games, summary);
  ggame_bga_import_session_cache_store(cache, session, "test-user", games, 2, TRUE);

  const GGameAppProfile *profile = ggame_app_profile_lookup_by_id(profile_id);
  if (profile != NULL) {
    g_autoptr(GError) error = NULL;
    if (!ggame_import_dialog_save_bga_history_cache_for_profile(profile, games, &error)) {
      g_debug("Unable to seed BGA history cache for tests: %s", error != NULL ? error->message : "unknown");
    }
  }
}

guint ggame_import_dialog_test_count_bga_history_cache(const char *profile_id) {
  g_return_val_if_fail(profile_id != NULL, 0);

  const GGameAppProfile *profile = ggame_app_profile_lookup_by_id(profile_id);
  g_return_val_if_fail(profile != NULL, 0);

  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) games = ggame_import_dialog_load_bga_history_cache_for_profile(profile, &error);
  if (games == NULL) {
    g_debug("Unable to load BGA history cache for tests: %s", error != NULL ? error->message : "unknown");
    return 0;
  }
  return games->len;
}
#endif

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

static char *ggame_import_dialog_bga_history_cache_path_for_profile(const GGameAppProfile *profile,
                                                                    GError **error) {
  g_return_val_if_fail(profile != NULL, NULL);

  g_autofree char *profile_dir = ggame_import_dialog_bga_profile_cache_dir(profile, error);
  if (profile_dir == NULL) {
    return NULL;
  }

  return g_build_filename(profile_dir, GGAME_IMPORT_BGA_HISTORY_CACHE_FILE, NULL);
}

static gboolean ggame_import_dialog_save_bga_history_cache_for_profile(const GGameAppProfile *profile,
                                                                       GPtrArray *games,
                                                                       GError **error) {
  g_return_val_if_fail(profile != NULL, FALSE);
  g_return_val_if_fail(games != NULL, FALSE);

  g_autofree char *path = ggame_import_dialog_bga_history_cache_path_for_profile(profile, error);
  if (path == NULL) {
    return FALSE;
  }

  g_autoptr(GKeyFile) key_file = g_key_file_new();
  g_key_file_set_uint64(key_file, "history", "count", games->len);
  for (guint i = 0; i < games->len; ++i) {
    BgaHistoryGameSummary *summary = g_ptr_array_index(games, i);
    if (summary == NULL) {
      continue;
    }

    g_autofree char *group = g_strdup_printf("game %u", i);
    g_key_file_set_string(key_file, group, "table_id", summary->table_id ? summary->table_id : "");
    g_key_file_set_string(key_file, group, "start_at", summary->start_at ? summary->start_at : "");
    g_key_file_set_string(key_file, group, "player_one", summary->player_one ? summary->player_one : "");
    g_key_file_set_string(key_file, group, "player_two", summary->player_two ? summary->player_two : "");
  }

  gsize length = 0;
  g_autofree char *contents = g_key_file_to_data(key_file, &length, error);
  if (contents == NULL) {
    return FALSE;
  }

  return g_file_set_contents(path, contents, length, error);
}

static GPtrArray *ggame_import_dialog_load_bga_history_cache_for_profile(const GGameAppProfile *profile,
                                                                         GError **error) {
  g_return_val_if_fail(profile != NULL, NULL);

  g_autofree char *path = ggame_import_dialog_bga_history_cache_path_for_profile(profile, error);
  if (path == NULL) {
    return NULL;
  }

  GPtrArray *games = g_ptr_array_new_with_free_func((GDestroyNotify)bga_history_game_summary_free);
  if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
    return games;
  }

  g_autoptr(GKeyFile) key_file = g_key_file_new();
  if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, error)) {
    g_ptr_array_unref(games);
    return NULL;
  }

  guint64 count = g_key_file_get_uint64(key_file, "history", "count", error);
  if (error != NULL && *error != NULL) {
    g_ptr_array_unref(games);
    return NULL;
  }
  if (count > G_MAXUINT) {
    g_ptr_array_unref(games);
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "BoardGameArena history cache has too many entries");
    return NULL;
  }

  for (guint i = 0; i < (guint)count; ++i) {
    g_autofree char *group = g_strdup_printf("game %u", i);
    g_autofree char *table_id = g_key_file_get_string(key_file, group, "table_id", NULL);
    if (table_id == NULL || table_id[0] == '\0' || ggame_bga_history_games_contains_table_id(games, table_id)) {
      continue;
    }

    BgaHistoryGameSummary *summary = g_new0(BgaHistoryGameSummary, 1);
    summary->table_id = g_steal_pointer(&table_id);
    summary->start_at = g_key_file_get_string(key_file, group, "start_at", NULL);
    summary->player_one = g_key_file_get_string(key_file, group, "player_one", NULL);
    summary->player_two = g_key_file_get_string(key_file, group, "player_two", NULL);
    g_ptr_array_add(games, summary);
  }

  return games;
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

static void ggame_import_dialog_load_persistent_history_cache(GGameWindowImportDialogData *data) {
  g_return_if_fail(data != NULL);
  g_return_if_fail(data->profile != NULL);
  g_return_if_fail(data->bga_cache != NULL);

  if (data->bga_cache->history_games != NULL) {
    return;
  }

  g_autoptr(GError) error = NULL;
  GPtrArray *cached_games = ggame_import_dialog_load_bga_history_cache_for_profile(data->profile, &error);
  if (cached_games == NULL) {
    g_debug("Unable to load BoardGameArena history cache: %s", error != NULL ? error->message : "unknown error");
    return;
  }
  if (cached_games->len == 0) {
    g_ptr_array_unref(cached_games);
    return;
  }

  data->bga_cache->history_games = cached_games;
  data->bga_cache->history_next_page = 1;
  data->bga_cache->history_more_available = TRUE;
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

  g_clear_object(&data->settings);
  g_object_unref(data->self);
  g_free(data);
}

static void ggame_window_import_dialog_disconnect_child_signals(GGameWindowImportDialogData *data) {
  g_return_if_fail(data != NULL);

  if (G_IS_OBJECT(data->site_drop_down)) {
    g_signal_handlers_disconnect_by_data(data->site_drop_down, data);
  }
  if (G_IS_OBJECT(data->cancel_button)) {
    g_signal_handlers_disconnect_by_data(data->cancel_button, data);
  }
  if (G_IS_OBJECT(data->back_button)) {
    g_signal_handlers_disconnect_by_data(data->back_button, data);
  }
  if (G_IS_OBJECT(data->next_button)) {
    g_signal_handlers_disconnect_by_data(data->next_button, data);
  }
  if (G_IS_OBJECT(data->reload_button)) {
    g_signal_handlers_disconnect_by_data(data->reload_button, data);
  }
  if (G_IS_OBJECT(data->more_button)) {
    g_signal_handlers_disconnect_by_data(data->more_button, data);
  }
  if (G_IS_OBJECT(data->history_list)) {
    g_signal_handlers_disconnect_by_data(data->history_list, data);
  }
}

static void ggame_window_import_dialog_destroy(GGameWindowImportDialogData *data) {
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_WINDOW(data->dialog));

  ggame_window_import_dialog_disconnect_child_signals(data);
  gtk_window_destroy(data->dialog);
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

  gtk_grid_attach(GTK_GRID(grid), ggame_library_new_cell(entry->date, FALSE, 128), 0, 0, 1, 1);
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

  gtk_grid_attach(GTK_GRID(grid), ggame_library_new_cell("Date/time", TRUE, 128), 0, 0, 1, 1);
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

static void ggame_window_import_dialog_update_step(GGameWindowImportDialogData *data);

static void ggame_import_dialog_show_error(GGameWindowImportDialogData *data, const char *text) {
  g_return_if_fail(data != NULL);
  g_return_if_fail(text != NULL);

  GtkWidget *dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog), "Import error");
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(data->dialog));
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
  g_signal_connect(ok_button, "clicked", G_CALLBACK(ggame_import_dialog_on_error_ok_clicked), dialog);
  gtk_window_present(GTK_WINDOW(dialog));
}

static void ggame_import_dialog_replace_history_rows(GGameWindowImportDialogData *data, GPtrArray *games) {
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_LIST_BOX(data->history_list));
  g_return_if_fail(games != NULL);

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
}

static void ggame_import_dialog_show_history(GGameWindowImportDialogData *data, GPtrArray *games) {
  g_return_if_fail(data != NULL);
  g_return_if_fail(games != NULL);

  ggame_import_dialog_replace_history_rows(data, games);
  data->step = GGAME_IMPORT_STEP_HISTORY;
  ggame_window_import_dialog_update_step(data);
}

static gboolean ggame_import_dialog_fetch_bga_history_batch(GGameWindowImportDialogData *data,
                                                            BgaClientSession *session,
                                                            const char *user_id,
                                                            guint first_page,
                                                            gboolean stop_at_cached_history,
                                                            GPtrArray **out_games,
                                                            BgaHistoryFetchResult *out_result,
                                                            GError **error) {
  g_return_val_if_fail(data != NULL, FALSE);
  g_return_val_if_fail(data->profile != NULL, FALSE);
  g_return_val_if_fail(session != NULL, FALSE);
  g_return_val_if_fail(user_id != NULL, FALSE);
  g_return_val_if_fail(first_page > 0, FALSE);
  g_return_val_if_fail(out_games != NULL, FALSE);

  g_autoptr(GHashTable) stop_table_ids = NULL;
  if (stop_at_cached_history && data->bga_cache != NULL && data->bga_cache->history_games != NULL &&
      data->bga_cache->history_games->len > 0) {
    stop_table_ids = ggame_bga_history_games_table_id_set(data->bga_cache->history_games);
  }

  return bga_client_session_fetch_game_history_pages(session,
                                                     user_id,
                                                     data->profile->import.board_game_arena_game_id,
                                                     first_page,
                                                     GGAME_IMPORT_BGA_HISTORY_PAGE_BATCH_SIZE,
                                                     stop_table_ids,
                                                     out_games,
                                                     out_result,
                                                     error);
}

static gboolean ggame_import_dialog_update_bga_history_from_network(GGameWindowImportDialogData *data,
                                                                    BgaClientSession *session,
                                                                    const char *user_id,
                                                                    guint first_page,
                                                                    gboolean stop_at_cached_history,
                                                                    gboolean append_to_existing_history,
                                                                    GError **error) {
  g_return_val_if_fail(data != NULL, FALSE);
  g_return_val_if_fail(data->profile != NULL, FALSE);
  g_return_val_if_fail(data->bga_cache != NULL, FALSE);
  g_return_val_if_fail(session != NULL, FALSE);
  g_return_val_if_fail(user_id != NULL, FALSE);
  g_return_val_if_fail(first_page > 0, FALSE);

  g_autoptr(GPtrArray) network_games = NULL;
  BgaHistoryFetchResult fetch_result = {0};
  if (!ggame_import_dialog_fetch_bga_history_batch(data,
                                                   session,
                                                   user_id,
                                                   first_page,
                                                   stop_at_cached_history,
                                                   &network_games,
                                                   &fetch_result,
                                                   error)) {
    return FALSE;
  }

  g_autoptr(GPtrArray) cached_games = data->bga_cache->history_games != NULL
      ? ggame_bga_history_games_copy(data->bga_cache->history_games)
      : g_ptr_array_new_with_free_func((GDestroyNotify)bga_history_game_summary_free);
  g_autoptr(GPtrArray) merged_games = NULL;
  if (append_to_existing_history) {
    merged_games = ggame_bga_history_games_copy(cached_games);
    ggame_bga_history_games_append_or_replace(merged_games, network_games);
  } else {
    merged_games = ggame_bga_history_games_merge_new_then_cached(network_games, cached_games);
  }

  g_autoptr(GError) write_error = NULL;
  if (!ggame_import_dialog_save_bga_history_cache_for_profile(data->profile, merged_games, &write_error)) {
    g_debug("Unable to save BoardGameArena history cache: %s",
            write_error != NULL ? write_error->message : "unknown error");
  }

  gboolean more_available = !fetch_result.reached_end;
  guint next_page = fetch_result.next_page > 0 ? fetch_result.next_page : first_page + 1;
  if (data->bga_cache->session == NULL) {
    ggame_bga_import_session_cache_store(data->bga_cache, session, user_id, merged_games, next_page, more_available);
  } else {
    ggame_bga_import_session_cache_store_history(data->bga_cache, merged_games, next_page, more_available);
  }
  return TRUE;
}

static void ggame_import_dialog_reload_cached_history(GGameWindowImportDialogData *data) {
  g_return_if_fail(data != NULL);

  if (data->bga_cache == NULL || data->bga_cache->session == NULL ||
      data->bga_cache->user_id == NULL || data->bga_cache->user_id[0] == '\0') {
    g_debug("Import flow: reload requested without a cached BoardGameArena session");
    ggame_import_dialog_show_error(data, "BoardGameArena history cannot be reloaded without an active session.");
    return;
  }

  g_autoptr(GError) error = NULL;
  if (!ggame_import_dialog_update_bga_history_from_network(data,
                                                           data->bga_cache->session,
                                                           data->bga_cache->user_id,
                                                           1,
                                                           TRUE,
                                                           FALSE,
                                                           &error)) {
    g_debug("Import flow: failed to reload BoardGameArena history: %s",
            error != NULL ? error->message : "unknown error");
    ggame_import_dialog_show_error(data, "Unable to reload BoardGameArena history.");
    return;
  }

  g_debug("Import flow: reloaded %u BoardGameArena games", data->bga_cache->history_games->len);
  ggame_import_dialog_show_history(data, data->bga_cache->history_games);
}

static void ggame_import_dialog_fetch_more_history(GGameWindowImportDialogData *data) {
  g_return_if_fail(data != NULL);

  if (!ggame_bga_import_session_cache_has_session(data->bga_cache)) {
    g_debug("Import flow: More requested without a cached BoardGameArena session");
    ggame_import_dialog_show_error(data, "BoardGameArena history cannot be extended without an active session.");
    return;
  }

  guint first_page = data->bga_cache->history_next_page > 0 ? data->bga_cache->history_next_page : 1;
  g_autoptr(GError) error = NULL;
  if (!ggame_import_dialog_update_bga_history_from_network(data,
                                                           data->bga_cache->session,
                                                           data->bga_cache->user_id,
                                                           first_page,
                                                           FALSE,
                                                           TRUE,
                                                           &error)) {
    g_debug("Import flow: failed to fetch more BoardGameArena history: %s",
            error != NULL ? error->message : "unknown error");
    ggame_import_dialog_show_error(data, "Unable to fetch more BoardGameArena history.");
    return;
  }

  g_debug("Import flow: extended history to %u BoardGameArena games", data->bga_cache->history_games->len);
  ggame_import_dialog_show_history(data, data->bga_cache->history_games);
}

static void ggame_window_import_dialog_update_step(GGameWindowImportDialogData *data) {
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_STACK(data->stack));
  g_return_if_fail(GTK_IS_BUTTON(data->back_button));
  g_return_if_fail(GTK_IS_BUTTON(data->next_button));
  g_return_if_fail(GTK_IS_BUTTON(data->reload_button));
  g_return_if_fail(GTK_IS_BUTTON(data->more_button));

  if (data->step == GGAME_IMPORT_STEP_SITE) {
    gtk_stack_set_visible_child_name(data->stack, "site");
    gtk_widget_set_sensitive(GTK_WIDGET(data->back_button), FALSE);
    gtk_button_set_label(data->next_button, "Next");
    gtk_widget_set_sensitive(GTK_WIDGET(data->next_button),
                             ggame_import_dialog_is_board_game_arena_selected(data));
    gtk_widget_set_sensitive(GTK_WIDGET(data->reload_button), FALSE);
    gtk_widget_set_visible(GTK_WIDGET(data->more_button), FALSE);
    return;
  }

  if (data->step == GGAME_IMPORT_STEP_HISTORY) {
    gtk_stack_set_visible_child_name(data->stack, "history");
    gtk_widget_set_sensitive(GTK_WIDGET(data->back_button), FALSE);
    const char *selected_table_id = ggame_import_dialog_selected_history_table_id(data);
    gtk_button_set_label(data->next_button,
                         ggame_import_dialog_selected_history_is_cached(data) ? "Load" : "Import");
    gtk_widget_set_sensitive(GTK_WIDGET(data->next_button), selected_table_id != NULL);
    gtk_widget_set_sensitive(GTK_WIDGET(data->reload_button),
                             data->bga_cache != NULL && data->bga_cache->session != NULL &&
                             data->bga_cache->user_id != NULL && data->bga_cache->user_id[0] != '\0');
    gtk_widget_set_visible(GTK_WIDGET(data->more_button), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(data->more_button),
                             ggame_bga_import_session_cache_has_session(data->bga_cache) &&
                             data->bga_cache->history_more_available);
    return;
  }

  gtk_stack_set_visible_child_name(data->stack, "credentials");
  gtk_widget_set_sensitive(GTK_WIDGET(data->back_button),
                           data->profile != NULL && data->profile->import.show_site_step);
  gtk_button_set_label(data->next_button, "Fetch game history");
  gtk_widget_set_sensitive(GTK_WIDGET(data->next_button), TRUE);
  gtk_widget_set_sensitive(GTK_WIDGET(data->reload_button), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(data->more_button), FALSE);
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

  ggame_window_import_dialog_destroy(data);
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

static void ggame_window_on_import_dialog_reload_clicked(GtkButton * /*button*/, gpointer user_data) {
  GGameWindowImportDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  if (data->step != GGAME_IMPORT_STEP_HISTORY) {
    return;
  }

  ggame_import_dialog_reload_cached_history(data);
}

static void ggame_window_on_import_dialog_more_clicked(GtkButton * /*button*/, gpointer user_data) {
  GGameWindowImportDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  if (data->step != GGAME_IMPORT_STEP_HISTORY) {
    return;
  }

  ggame_import_dialog_fetch_more_history(data);
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
    ggame_window_import_dialog_destroy(data);
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

  ggame_window_import_dialog_destroy(data);
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
  data->bga_session = NULL;
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

  if (!ggame_import_dialog_update_bga_history_from_network(data,
                                                           session,
                                                           history_user_id,
                                                           1,
                                                           data->bga_cache->history_games != NULL,
                                                           FALSE,
                                                           &error)) {
    g_debug("Import flow: failed to fetch BoardGameArena history: %s",
            error ? error->message : "unknown error");
    g_autofree char *dialog_text =
        g_strdup_printf("Unable to fetch BoardGameArena %s history.", data->profile->display_name);
    ggame_import_dialog_show_error_and_close_wizard(
        data,
        dialog_text);
    bga_client_session_free(session);
    return;
  }
  g_debug("Import flow: parsed %u BoardGameArena games", data->bga_cache->history_games->len);

  data->bga_session = data->bga_cache->session;
  session = NULL;
  ggame_import_dialog_show_history(data, data->bga_cache->history_games);
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
  GtkWidget *history_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(history_header, GTK_ALIGN_FILL);
  gtk_box_append(GTK_BOX(history_page), history_header);

  g_autofree char *history_title_text =
      g_strdup_printf("BoardGameArena %s history", profile->display_name);
  GtkWidget *history_title = gtk_label_new(history_title_text);
  gtk_widget_set_halign(history_title, GTK_ALIGN_START);
  gtk_widget_set_hexpand(history_title, TRUE);
  gtk_box_append(GTK_BOX(history_header), history_title);

  GtkWidget *reload_button = gtk_button_new_with_label("Reload");
  gtk_widget_set_name(reload_button, "import-history-reload-button");
  gtk_widget_set_halign(reload_button, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(history_header), reload_button);

  GtkWidget *more_button = gtk_button_new_with_label("More...");
  gtk_widget_set_name(more_button, "import-history-more-button");
  gtk_widget_set_halign(more_button, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(history_header), more_button);

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
  data->bga_cache = ggame_bga_import_session_cache_get(profile);
  data->dialog = GTK_WINDOW(dialog);
  data->stack = GTK_STACK(stack);
  data->site_drop_down = site_drop_down;
  data->cancel_button = GTK_BUTTON(cancel_button);
  data->back_button = GTK_BUTTON(back_button);
  data->next_button = GTK_BUTTON(next_button);
  data->reload_button = GTK_BUTTON(reload_button);
  data->more_button = GTK_BUTTON(more_button);
  data->email_entry = email_entry;
  data->password_entry = password_entry;
  data->remember_check = remember_check;
  data->history_list = history_list;
  data->settings = ggame_common_settings_create();
  ggame_import_dialog_load_persistent_history_cache(data);
  if (ggame_bga_import_session_cache_has_history(data->bga_cache)) {
    data->step = GGAME_IMPORT_STEP_HISTORY;
    data->bga_session = data->bga_cache->session;
  } else {
    data->step = profile->import.show_site_step ? GGAME_IMPORT_STEP_SITE : GGAME_IMPORT_STEP_CREDENTIALS;
  }

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
  g_signal_connect(reload_button,
                   "clicked",
                   G_CALLBACK(ggame_window_on_import_dialog_reload_clicked),
                   data);
  g_signal_connect(more_button,
                   "clicked",
                   G_CALLBACK(ggame_window_on_import_dialog_more_clicked),
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

  if (ggame_bga_import_session_cache_has_history(data->bga_cache)) {
    gboolean should_refresh_cached_history = TRUE;
#ifdef GGAME_TESTING
    should_refresh_cached_history = bga_import_dialog_test_auto_history_refresh_enabled;
#endif
    g_autoptr(GError) error = NULL;
    if (should_refresh_cached_history &&
        !ggame_import_dialog_update_bga_history_from_network(data,
                                                             data->bga_cache->session,
                                                             data->bga_cache->user_id,
                                                             1,
                                                             TRUE,
                                                             FALSE,
                                                             &error)) {
      g_debug("Import flow: failed to refresh cached BoardGameArena history on open: %s",
              error != NULL ? error->message : "unknown error");
    }
    ggame_import_dialog_show_history(data, data->bga_cache->history_games);
  } else {
    ggame_window_import_dialog_update_step(data);
  }
  gtk_window_present(GTK_WINDOW(dialog));
}
