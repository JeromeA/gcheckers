#include "settings_dialog.h"

#include "app_settings.h"

typedef struct {
  GCheckersWindow *self;
  GtkWindow *dialog;
  GtkCheckButton *send_puzzle_usage_check;
  GtkCheckButton *send_application_usage_check;
  GSettings *settings;
} GCheckersWindowSettingsDialogData;

static void gcheckers_window_settings_dialog_data_free(GCheckersWindowSettingsDialogData *data) {
  g_return_if_fail(data != NULL);

  g_clear_object(&data->settings);
  g_object_unref(data->self);
  g_free(data);
}

static void gcheckers_window_on_settings_dialog_destroy(GtkWindow * /*dialog*/, gpointer user_data) {
  GCheckersWindowSettingsDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  gcheckers_window_settings_dialog_data_free(data);
}

static void gcheckers_window_on_settings_dialog_cancel_clicked(GtkButton * /*button*/, gpointer user_data) {
  GCheckersWindowSettingsDialogData *data = user_data;
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_WINDOW(data->dialog));

  gtk_window_destroy(data->dialog);
}

static void gcheckers_window_on_settings_dialog_save_clicked(GtkButton * /*button*/, gpointer user_data) {
  GCheckersWindowSettingsDialogData *data = user_data;
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

  gtk_window_destroy(data->dialog);
}

void gcheckers_window_present_settings_dialog(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

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

  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(content), actions);

  GtkWidget *cancel_button = gtk_button_new_with_label("Cancel");
  GtkWidget *save_button = gtk_button_new_with_label("Save");
  gtk_box_append(GTK_BOX(actions), cancel_button);
  gtk_box_append(GTK_BOX(actions), save_button);

  GCheckersWindowSettingsDialogData *data = g_new0(GCheckersWindowSettingsDialogData, 1);
  data->self = g_object_ref(self);
  data->dialog = GTK_WINDOW(dialog);
  data->send_puzzle_usage_check = GTK_CHECK_BUTTON(send_puzzle_usage_check);
  data->send_application_usage_check = GTK_CHECK_BUTTON(send_application_usage_check);
  data->settings = gcheckers_app_settings_create();

  if (G_IS_SETTINGS(data->settings)) {
    (void)gcheckers_app_settings_mark_privacy_settings_shown(data->settings);
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

  g_signal_connect(cancel_button,
                   "clicked",
                   G_CALLBACK(gcheckers_window_on_settings_dialog_cancel_clicked),
                   data);
  g_signal_connect(save_button,
                   "clicked",
                   G_CALLBACK(gcheckers_window_on_settings_dialog_save_clicked),
                   data);
  g_signal_connect(dialog,
                   "destroy",
                   G_CALLBACK(gcheckers_window_on_settings_dialog_destroy),
                   data);
  gtk_window_present(GTK_WINDOW(dialog));
}
