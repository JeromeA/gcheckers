#include "window.h"
#include "rulesets.h"

typedef struct {
  GCheckersWindow *self;
  GtkWindow *dialog;
  GtkDropDown *ruleset;
  GtkLabel *ruleset_summary;
  GtkDropDown *white_player;
  GtkDropDown *black_player;
  GtkScale *computer_depth_scale;
} GCheckersWindowNewGameDialogData;

static void gcheckers_window_new_game_dialog_data_free(GCheckersWindowNewGameDialogData *data) {
  g_return_if_fail(data != NULL);

  g_object_unref(data->self);
  g_free(data);
}

static void gcheckers_window_on_new_game_dialog_destroy(GtkWindow * /*dialog*/, gpointer user_data) {
  GCheckersWindowNewGameDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  gcheckers_window_new_game_dialog_data_free(data);
}

static void gcheckers_window_on_new_game_dialog_cancel_clicked(GtkButton * /*button*/, gpointer user_data) {
  GCheckersWindowNewGameDialogData *data = user_data;
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_WINDOW(data->dialog));

  gtk_window_destroy(data->dialog);
}

static void gcheckers_window_on_new_game_dialog_confirm_clicked(GtkButton * /*button*/, gpointer user_data) {
  GCheckersWindowNewGameDialogData *data = user_data;
  g_return_if_fail(data != NULL);
  g_return_if_fail(GCHECKERS_IS_WINDOW(data->self));
  g_return_if_fail(GTK_IS_WINDOW(data->dialog));

  gcheckers_window_apply_new_game_settings(data->self,
                                           (PlayerRuleset)gtk_drop_down_get_selected(data->ruleset),
                                           (PlayerControlMode)gtk_drop_down_get_selected(data->white_player),
                                           (PlayerControlMode)gtk_drop_down_get_selected(data->black_player),
                                           (guint)gtk_range_get_value(GTK_RANGE(data->computer_depth_scale)));

  gtk_window_destroy(data->dialog);
}

static void gcheckers_window_new_game_update_ruleset_summary(GCheckersWindowNewGameDialogData *data) {
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_LABEL(data->ruleset_summary));
  g_return_if_fail(GTK_IS_DROP_DOWN(data->ruleset));

  PlayerRuleset ruleset = (PlayerRuleset)gtk_drop_down_get_selected(data->ruleset);
  const char *summary = checkers_ruleset_summary(ruleset);
  if (summary == NULL) {
    g_debug("Missing ruleset summary");
    return;
  }

  gtk_label_set_text(data->ruleset_summary, summary);
}

static void gcheckers_window_on_new_game_ruleset_selected(GObject * /*object*/,
                                                          GParamSpec * /*pspec*/,
                                                          gpointer user_data) {
  GCheckersWindowNewGameDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  gcheckers_window_new_game_update_ruleset_summary(data);
}

void gcheckers_window_present_new_game_dialog(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  PlayerControlsPanel *controls_panel = gcheckers_window_get_controls_panel(self);
  g_return_if_fail(controls_panel != NULL);

  GtkWidget *dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog), "New game");
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(self));
  gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(content, 12);
  gtk_widget_set_margin_bottom(content, 12);
  gtk_widget_set_margin_start(content, 12);
  gtk_widget_set_margin_end(content, 12);
  gtk_window_set_child(GTK_WINDOW(dialog), content);

  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
  gtk_box_append(GTK_BOX(content), grid);

  guint ruleset_count = checkers_ruleset_count();
  g_return_if_fail(ruleset_count > 0);
  g_return_if_fail(ruleset_count <= G_MAXUINT8);
  g_auto(GStrv) ruleset_options = g_new0(char *, ruleset_count + 1);
  for (guint i = 0; i < ruleset_count; ++i) {
    const char *name = checkers_ruleset_name((PlayerRuleset)i);
    if (name == NULL) {
      g_debug("Missing ruleset name");
      return;
    }
    ruleset_options[i] = g_strdup(name);
  }
  static const char *player_options[] = {"User", "Computer", NULL};
  GtkWidget *ruleset_label = gtk_label_new("Ruleset");
  gtk_widget_set_halign(ruleset_label, GTK_ALIGN_START);
  GtkWidget *white_label = gtk_label_new("White");
  gtk_widget_set_halign(white_label, GTK_ALIGN_START);
  GtkWidget *black_label = gtk_label_new("Black");
  gtk_widget_set_halign(black_label, GTK_ALIGN_START);
  GtkWidget *level_label = gtk_label_new("AI level");
  gtk_widget_set_halign(level_label, GTK_ALIGN_START);
  GtkWidget *ruleset_summary = gtk_label_new(NULL);
  gtk_widget_set_halign(ruleset_summary, GTK_ALIGN_START);
  gtk_label_set_xalign(GTK_LABEL(ruleset_summary), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(ruleset_summary), TRUE);
  gtk_widget_set_hexpand(ruleset_summary, TRUE);

  GtkDropDown *ruleset = GTK_DROP_DOWN(
      gtk_drop_down_new_from_strings((const char *const *)ruleset_options));
  GtkDropDown *white_player = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(player_options));
  GtkDropDown *black_player = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(player_options));
  GtkScale *computer_depth_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                                       PLAYER_COMPUTER_DEPTH_MIN,
                                                                       PLAYER_COMPUTER_DEPTH_MAX,
                                                                       1));
  gtk_scale_set_digits(computer_depth_scale, 0);
  gtk_scale_set_draw_value(computer_depth_scale, TRUE);
  gtk_widget_set_hexpand(GTK_WIDGET(computer_depth_scale), TRUE);
  gtk_widget_set_size_request(GTK_WIDGET(computer_depth_scale), 100, -1);

  gtk_drop_down_set_selected(ruleset, gcheckers_window_get_ruleset(self));
  gtk_drop_down_set_selected(white_player, player_controls_panel_get_mode(controls_panel, CHECKERS_COLOR_WHITE));
  gtk_drop_down_set_selected(black_player, player_controls_panel_get_mode(controls_panel, CHECKERS_COLOR_BLACK));
  gtk_range_set_value(GTK_RANGE(computer_depth_scale),
                      (gdouble)player_controls_panel_get_computer_depth(controls_panel));

  gtk_grid_attach(GTK_GRID(grid), ruleset_label, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ruleset), 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), ruleset_summary, 1, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), white_label, 0, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(white_player), 1, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), black_label, 0, 3, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(black_player), 1, 3, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), level_label, 0, 4, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(computer_depth_scale), 1, 4, 1, 1);

  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(content), actions);

  GtkWidget *cancel_button = gtk_button_new_with_label("Cancel");
  GtkWidget *confirm_button = gtk_button_new_with_label("New Game");
  gtk_box_append(GTK_BOX(actions), cancel_button);
  gtk_box_append(GTK_BOX(actions), confirm_button);

  GCheckersWindowNewGameDialogData *data = g_new0(GCheckersWindowNewGameDialogData, 1);
  data->self = g_object_ref(self);
  data->dialog = GTK_WINDOW(dialog);
  data->ruleset = ruleset;
  data->ruleset_summary = GTK_LABEL(ruleset_summary);
  data->white_player = white_player;
  data->black_player = black_player;
  data->computer_depth_scale = computer_depth_scale;
  gcheckers_window_new_game_update_ruleset_summary(data);

  g_signal_connect(cancel_button,
                   "clicked",
                   G_CALLBACK(gcheckers_window_on_new_game_dialog_cancel_clicked),
                   data);
  g_signal_connect(confirm_button,
                   "clicked",
                   G_CALLBACK(gcheckers_window_on_new_game_dialog_confirm_clicked),
                   data);
  g_signal_connect(dialog,
                   "destroy",
                   G_CALLBACK(gcheckers_window_on_new_game_dialog_destroy),
                   data);
  g_signal_connect(ruleset,
                   "notify::selected",
                   G_CALLBACK(gcheckers_window_on_new_game_ruleset_selected),
                   data);
  gtk_window_present(GTK_WINDOW(dialog));
}
