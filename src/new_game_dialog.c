#include "active_game_backend.h"
#include "window.h"
#include "games/checkers/rulesets.h"

typedef struct {
  GCheckersWindow *self;
  GtkWindow *dialog;
  GtkDropDown *variant;
  GtkLabel *variant_summary;
  GtkDropDown *side0_player;
  GtkDropDown *side1_player;
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
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  GCheckersWindowNewGameDialogData *data = user_data;
  PlayerRuleset ruleset = gcheckers_window_get_ruleset(data->self);

  g_return_if_fail(data != NULL);
  g_return_if_fail(GCHECKERS_IS_WINDOW(data->self));
  g_return_if_fail(GTK_IS_WINDOW(data->dialog));
  g_return_if_fail(backend != NULL);

  if (data->variant != NULL && backend->variant_count > 0) {
    ruleset = (PlayerRuleset) gtk_drop_down_get_selected(data->variant);
  }
  gcheckers_window_apply_new_game_settings(data->self,
                                           ruleset,
                                           (PlayerControlMode) gtk_drop_down_get_selected(data->side0_player),
                                           (PlayerControlMode) gtk_drop_down_get_selected(data->side1_player),
                                           (guint) gtk_range_get_value(GTK_RANGE(data->computer_depth_scale)));

  gtk_window_destroy(data->dialog);
}

static void gcheckers_window_new_game_update_variant_summary(GCheckersWindowNewGameDialogData *data) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  g_return_if_fail(data != NULL);
  g_return_if_fail(backend != NULL);

  if (backend->variant_count == 0 || data->variant_summary == NULL || data->variant == NULL) {
    return;
  }

  g_return_if_fail(GTK_IS_LABEL(data->variant_summary));
  g_return_if_fail(GTK_IS_DROP_DOWN(data->variant));
  g_return_if_fail(backend->variant_at != NULL);

  const GameBackendVariant *variant = backend->variant_at(gtk_drop_down_get_selected(data->variant));
  const char *summary = variant != NULL ? variant->summary : NULL;
  if (summary == NULL) {
    g_debug("Missing variant summary");
    return;
  }

  gtk_label_set_text(data->variant_summary, summary);
}

static void gcheckers_window_on_new_game_variant_selected(GObject * /*object*/,
                                                          GParamSpec * /*pspec*/,
                                                          gpointer user_data) {
  GCheckersWindowNewGameDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  gcheckers_window_new_game_update_variant_summary(data);
}

void gcheckers_window_present_new_game_dialog(GCheckersWindow *self) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(backend != NULL);

  PlayerControlsPanel *controls_panel = gcheckers_window_get_controls_panel(self);
  g_return_if_fail(controls_panel != NULL);

  GtkWidget *dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog), "New game");
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

  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
  gtk_box_append(GTK_BOX(content), grid);

  g_auto(GStrv) variant_options = NULL;
  if (backend->variant_count > 0) {
    g_return_if_fail(backend->variant_at != NULL);

    variant_options = g_new0(char *, backend->variant_count + 1);
    for (guint i = 0; i < backend->variant_count; ++i) {
      const GameBackendVariant *variant = backend->variant_at(i);
      if (variant == NULL || variant->name == NULL) {
        g_debug("Missing variant metadata");
        return;
      }
      variant_options[i] = g_strdup(variant->name);
    }
  }

  static const char *player_options[] = {"User", "Computer", NULL};
  const char *side0_label_text = backend->side_label != NULL ? backend->side_label(0) : NULL;
  const char *side1_label_text = backend->side_label != NULL ? backend->side_label(1) : NULL;
  GtkWidget *variant_label = gtk_label_new("Variant");
  gtk_widget_set_halign(variant_label, GTK_ALIGN_START);
  GtkWidget *side0_label = gtk_label_new(side0_label_text != NULL ? side0_label_text : "Side 1");
  gtk_widget_set_halign(side0_label, GTK_ALIGN_START);
  GtkWidget *side1_label = gtk_label_new(side1_label_text != NULL ? side1_label_text : "Side 2");
  gtk_widget_set_halign(side1_label, GTK_ALIGN_START);
  GtkWidget *level_label = gtk_label_new("AI level");
  gtk_widget_set_halign(level_label, GTK_ALIGN_START);
  GtkWidget *variant_summary = gtk_label_new(NULL);
  gtk_widget_set_halign(variant_summary, GTK_ALIGN_START);
  gtk_label_set_xalign(GTK_LABEL(variant_summary), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(variant_summary), FALSE);
  gtk_label_set_ellipsize(GTK_LABEL(variant_summary), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand(variant_summary, TRUE);

  GtkDropDown *variant = NULL;
  if (backend->variant_count > 0) {
    variant = GTK_DROP_DOWN(gtk_drop_down_new_from_strings((const char *const *) variant_options));
  }
  GtkDropDown *side0_player = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(player_options));
  GtkDropDown *side1_player = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(player_options));
  GtkScale *computer_depth_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                                       PLAYER_COMPUTER_DEPTH_MIN,
                                                                       PLAYER_COMPUTER_DEPTH_MAX,
                                                                       1));
  gtk_scale_set_digits(computer_depth_scale, 0);
  gtk_scale_set_draw_value(computer_depth_scale, TRUE);
  gtk_widget_set_hexpand(GTK_WIDGET(computer_depth_scale), TRUE);
  gtk_widget_set_size_request(GTK_WIDGET(computer_depth_scale), 100, -1);

  if (variant != NULL) {
    gtk_drop_down_set_selected(variant, gcheckers_window_get_ruleset(self));
  }
  gtk_drop_down_set_selected(side0_player, player_controls_panel_get_mode(controls_panel, 0));
  gtk_drop_down_set_selected(side1_player, player_controls_panel_get_mode(controls_panel, 1));
  gtk_range_set_value(GTK_RANGE(computer_depth_scale),
                      (gdouble) player_controls_panel_get_computer_depth(controls_panel));

  gint row = 0;
  if (variant != NULL) {
    gtk_grid_attach(GTK_GRID(grid), variant_label, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(variant), 1, row, 1, 1);
    row++;
    gtk_grid_attach(GTK_GRID(grid), variant_summary, 1, row, 1, 1);
    row++;
  }
  gtk_grid_attach(GTK_GRID(grid), side0_label, 0, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(side0_player), 1, row, 1, 1);
  row++;
  gtk_grid_attach(GTK_GRID(grid), side1_label, 0, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(side1_player), 1, row, 1, 1);
  row++;
  gtk_grid_attach(GTK_GRID(grid), level_label, 0, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(computer_depth_scale), 1, row, 1, 1);

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
  data->variant = variant;
  data->variant_summary = variant != NULL ? GTK_LABEL(variant_summary) : NULL;
  data->side0_player = side0_player;
  data->side1_player = side1_player;
  data->computer_depth_scale = computer_depth_scale;
  gcheckers_window_new_game_update_variant_summary(data);

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
  if (variant != NULL) {
    g_signal_connect(variant,
                     "notify::selected",
                     G_CALLBACK(gcheckers_window_on_new_game_variant_selected),
                     data);
  }
  gtk_window_present(GTK_WINDOW(dialog));
}
