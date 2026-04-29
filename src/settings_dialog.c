#include "settings_dialog.h"

#include "app_settings.h"
#include "application.h"
#include "game_app_profile.h"
#include "puzzle_catalog.h"
#include "puzzle_progress.h"

typedef struct {
  GGameWindow *self;
  GtkWindow *dialog;
  GtkCheckButton *send_puzzle_usage_check;
  GtkCheckButton *send_application_usage_check;
  GtkLabel *puzzle_progress_label;
  GSettings *settings;
  GGamePuzzleProgressStore *puzzle_progress_store;
} GGameWindowSettingsDialogData;

static void ggame_window_settings_dialog_data_free(GGameWindowSettingsDialogData *data) {
  g_return_if_fail(data != NULL);

  g_clear_object(&data->settings);
  g_clear_pointer(&data->puzzle_progress_store, ggame_puzzle_progress_store_unref);
  g_object_unref(data->self);
  g_free(data);
}

static gboolean ggame_window_settings_dialog_destroy_hidden_cb(gpointer user_data) {
  GtkWindow *dialog = user_data;
  g_return_val_if_fail(GTK_IS_WINDOW(dialog), G_SOURCE_REMOVE);

  gtk_window_destroy(dialog);
  g_object_unref(dialog);
  return G_SOURCE_REMOVE;
}

static void ggame_window_settings_dialog_close(GtkWindow *dialog) {
  g_return_if_fail(GTK_IS_WINDOW(dialog));

  gtk_widget_set_visible(GTK_WIDGET(dialog), FALSE);
  g_idle_add_full(G_PRIORITY_LOW,
                  ggame_window_settings_dialog_destroy_hidden_cb,
                  g_object_ref(dialog),
                  NULL);
}

static guint ggame_window_settings_dialog_count_known_puzzles(GHashTable *known_puzzle_ids) {
  const GGameAppProfile *profile = ggame_active_app_profile();
  const GameBackend *backend = profile != NULL ? profile->backend : NULL;

  g_return_val_if_fail(known_puzzle_ids != NULL, 0);
  g_return_val_if_fail(backend != NULL, 0);
  g_return_val_if_fail(backend->variant_at != NULL || backend->variant_count == 0, 0);

  guint total = 0;
  for (guint i = 0; i < backend->variant_count; i++) {
    const GameBackendVariant *variant = backend->variant_at(i);
    if (variant == NULL || variant->short_name == NULL) {
      g_debug("Skipping invalid puzzle variant at index %u", i);
      continue;
    }

    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) entries = game_puzzle_catalog_load_variant(backend, variant, &error);
    if (entries == NULL) {
      g_debug("Failed to load puzzle catalog for settings summary: %s",
              error != NULL ? error->message : "unknown error");
      continue;
    }

    total += entries->len;
    for (guint j = 0; j < entries->len; j++) {
      GamePuzzleCatalogEntry *entry = g_ptr_array_index(entries, j);
      if (entry != NULL && entry->puzzle_id != NULL) {
        g_hash_table_add(known_puzzle_ids, g_strdup(entry->puzzle_id));
      }
    }
  }

  return total;
}

static void ggame_window_settings_dialog_update_puzzle_progress(GGameWindowSettingsDialogData *data) {
  g_return_if_fail(data != NULL);

  if (!GTK_IS_LABEL(data->puzzle_progress_label)) {
    return;
  }

  g_autoptr(GHashTable) known_puzzle_ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  guint total = ggame_window_settings_dialog_count_known_puzzles(known_puzzle_ids);
  guint solved = 0;

  if (data->puzzle_progress_store != NULL) {
    g_autoptr(GError) error = NULL;
    g_autoptr(GHashTable) status_map =
        ggame_puzzle_progress_store_load_status_map(data->puzzle_progress_store, &error);
    if (status_map == NULL) {
      g_debug("Failed to load puzzle progress for settings summary: %s",
              error != NULL ? error->message : "unknown error");
    } else {
      GHashTableIter iter;
      gpointer key = NULL;
      gpointer value = NULL;
      g_hash_table_iter_init(&iter, status_map);
      while (g_hash_table_iter_next(&iter, &key, &value)) {
        GGamePuzzleStatusEntry *entry = value;
        if (entry != NULL && entry->status == GGAME_PUZZLE_STATUS_SOLVED &&
            g_hash_table_contains(known_puzzle_ids, key)) {
          solved++;
        }
      }
    }
  }

  g_autofree char *summary = g_strdup_printf("%u of %u puzzles solved", solved, total);
  gtk_label_set_text(data->puzzle_progress_label, summary);
}

static void ggame_window_on_settings_dialog_destroy(GtkWindow * /*dialog*/, gpointer user_data) {
  GGameWindowSettingsDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  ggame_window_settings_dialog_data_free(data);
}

static void ggame_window_on_settings_dialog_cancel_clicked(GtkButton * /*button*/, gpointer user_data) {
  GGameWindowSettingsDialogData *data = user_data;
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_WINDOW(data->dialog));

  ggame_window_settings_dialog_close(data->dialog);
}

static void ggame_window_on_settings_dialog_save_clicked(GtkButton * /*button*/, gpointer user_data) {
  GGameWindowSettingsDialogData *data = user_data;
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_WINDOW(data->dialog));
  g_return_if_fail(GTK_IS_CHECK_BUTTON(data->send_puzzle_usage_check));
  g_return_if_fail(GTK_IS_CHECK_BUTTON(data->send_application_usage_check));

  if (G_IS_SETTINGS(data->settings)) {
    g_settings_set_boolean(data->settings,
                           GCHECKERS_APP_SETTINGS_KEY_SEND_PUZZLE_USAGE,
                           gtk_check_button_get_active(data->send_puzzle_usage_check));
    g_settings_set_boolean(data->settings,
                           GCHECKERS_APP_SETTINGS_KEY_SEND_APPLICATION_USAGE,
                           gtk_check_button_get_active(data->send_application_usage_check));
  }

  ggame_window_settings_dialog_close(data->dialog);
}

static void ggame_window_on_settings_dialog_clear_progress_clicked(GtkButton * /*button*/, gpointer user_data) {
  GGameWindowSettingsDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  if (data->puzzle_progress_store == NULL) {
    g_debug("Puzzle progress storage unavailable; cannot clear progress");
    return;
  }

  g_autoptr(GError) error = NULL;
  if (!ggame_puzzle_progress_store_clear_progress(data->puzzle_progress_store, &error)) {
    g_debug("Failed to clear puzzle progress: %s", error != NULL ? error->message : "unknown error");
    return;
  }

  ggame_window_settings_dialog_update_puzzle_progress(data);
}

void ggame_window_present_settings_dialog(GGameWindow *self) {
  const GGameAppProfile *profile = ggame_active_app_profile();
  gboolean show_puzzle_progress = ggame_app_profile_supports_puzzle_catalog(profile);

  g_return_if_fail(GGAME_IS_WINDOW(self));

  GtkWidget *dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog), "Settings");
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(self));
  gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(content, 12);
  gtk_widget_set_margin_bottom(content, 12);
  gtk_widget_set_margin_start(content, 12);
  gtk_widget_set_margin_end(content, 12);
  gtk_window_set_child(GTK_WINDOW(dialog), content);

  GtkWidget *group = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_box_append(GTK_BOX(content), group);

  GtkWidget *title = gtk_label_new("Privacy");
  gtk_widget_set_halign(title, GTK_ALIGN_START);
  gtk_widget_add_css_class(title, "title-4");
  gtk_box_append(GTK_BOX(group), title);

  GtkWidget *send_puzzle_usage_check =
      gtk_check_button_new_with_label("Send anonymized data about puzzle usage");
  gtk_widget_set_halign(send_puzzle_usage_check, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(group), send_puzzle_usage_check);

  GtkWidget *send_application_usage_check =
      gtk_check_button_new_with_label("Send anonymized data about application usage");
  gtk_widget_set_halign(send_application_usage_check, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(group), send_application_usage_check);

  GtkWidget *progress_label = NULL;
  GtkWidget *clear_progress_button = NULL;
  if (show_puzzle_progress) {
    GtkWidget *progress_group = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(content), progress_group);

    GtkWidget *progress_title = gtk_label_new("Puzzle Progress");
    gtk_widget_set_halign(progress_title, GTK_ALIGN_START);
    gtk_widget_add_css_class(progress_title, "title-4");
    gtk_box_append(GTK_BOX(progress_group), progress_title);

    GtkWidget *progress_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(progress_row, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(progress_group), progress_row);

    progress_label = gtk_label_new("0 of 0 puzzles solved");
    gtk_widget_set_halign(progress_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(progress_label, TRUE);
    gtk_label_set_xalign(GTK_LABEL(progress_label), 0.0f);
    gtk_box_append(GTK_BOX(progress_row), progress_label);

    clear_progress_button = gtk_button_new_with_label("Clear Progress");
    gtk_widget_set_halign(clear_progress_button, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(progress_row), clear_progress_button);
  }

  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(content), actions);

  GtkWidget *cancel_button = gtk_button_new_with_label("Cancel");
  GtkWidget *save_button = gtk_button_new_with_label("Save");
  gtk_box_append(GTK_BOX(actions), cancel_button);
  gtk_box_append(GTK_BOX(actions), save_button);

  GGameWindowSettingsDialogData *data = g_new0(GGameWindowSettingsDialogData, 1);
  data->self = g_object_ref(self);
  data->dialog = GTK_WINDOW(dialog);
  data->send_puzzle_usage_check = GTK_CHECK_BUTTON(send_puzzle_usage_check);
  data->send_application_usage_check = GTK_CHECK_BUTTON(send_application_usage_check);
  data->puzzle_progress_label = GTK_LABEL(progress_label);
  data->settings = ggame_app_settings_create();

  if (show_puzzle_progress) {
    GtkApplication *app = gtk_window_get_application(GTK_WINDOW(self));
    if (GGAME_IS_APPLICATION(app)) {
      GGamePuzzleProgressStore *store = ggame_application_get_puzzle_progress_store(GGAME_APPLICATION(app));
      if (store != NULL) {
        data->puzzle_progress_store = ggame_puzzle_progress_store_ref(store);
      }
    }
  }

  if (G_IS_SETTINGS(data->settings)) {
    (void)ggame_app_settings_mark_privacy_settings_shown(data->settings);
    gtk_check_button_set_active(data->send_puzzle_usage_check,
                                g_settings_get_boolean(data->settings,
                                                       GCHECKERS_APP_SETTINGS_KEY_SEND_PUZZLE_USAGE));
    gtk_check_button_set_active(data->send_application_usage_check,
                                g_settings_get_boolean(data->settings,
                                                       GCHECKERS_APP_SETTINGS_KEY_SEND_APPLICATION_USAGE));
  } else {
    gtk_check_button_set_active(data->send_puzzle_usage_check, TRUE);
    gtk_check_button_set_active(data->send_application_usage_check, TRUE);
  }
  if (show_puzzle_progress) {
    ggame_window_settings_dialog_update_puzzle_progress(data);
  }

  if (clear_progress_button != NULL) {
    g_signal_connect(clear_progress_button,
                     "clicked",
                     G_CALLBACK(ggame_window_on_settings_dialog_clear_progress_clicked),
                     data);
  }
  g_signal_connect(cancel_button,
                   "clicked",
                   G_CALLBACK(ggame_window_on_settings_dialog_cancel_clicked),
                   data);
  g_signal_connect(save_button,
                   "clicked",
                   G_CALLBACK(ggame_window_on_settings_dialog_save_clicked),
                   data);
  g_signal_connect(dialog,
                   "destroy",
                   G_CALLBACK(ggame_window_on_settings_dialog_destroy),
                   data);
  gtk_window_present(GTK_WINDOW(dialog));
}
