#include "puzzle_dialog.h"

#include "rulesets.h"

typedef struct {
  GCheckersWindow *self;
  GtkWindow *dialog;
  GtkDropDown *ruleset;
  GtkLabel *ruleset_summary;
} GCheckersWindowPuzzleDialogData;

static void gcheckers_window_puzzle_dialog_data_free(GCheckersWindowPuzzleDialogData *data) {
  g_return_if_fail(data != NULL);

  g_object_unref(data->self);
  g_free(data);
}

static void gcheckers_window_on_puzzle_dialog_destroy(GtkWindow * /*dialog*/, gpointer user_data) {
  GCheckersWindowPuzzleDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  gcheckers_window_puzzle_dialog_data_free(data);
}

static void gcheckers_window_on_puzzle_dialog_cancel_clicked(GtkButton * /*button*/, gpointer user_data) {
  GCheckersWindowPuzzleDialogData *data = user_data;
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_WINDOW(data->dialog));

  gtk_window_destroy(data->dialog);
}

static void gcheckers_window_puzzle_dialog_update_ruleset_summary(GCheckersWindowPuzzleDialogData *data) {
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_DROP_DOWN(data->ruleset));
  g_return_if_fail(GTK_IS_LABEL(data->ruleset_summary));

  PlayerRuleset ruleset = (PlayerRuleset)gtk_drop_down_get_selected(data->ruleset);
  const char *summary = checkers_ruleset_summary(ruleset);
  if (summary == NULL) {
    g_debug("Missing puzzle ruleset summary");
    return;
  }

  gtk_label_set_text(data->ruleset_summary, summary);
}

static void gcheckers_window_on_puzzle_dialog_ruleset_selected(GObject * /*object*/,
                                                               GParamSpec * /*pspec*/,
                                                               gpointer user_data) {
  GCheckersWindowPuzzleDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  gcheckers_window_puzzle_dialog_update_ruleset_summary(data);
}

static void gcheckers_window_on_puzzle_dialog_start_clicked(GtkButton * /*button*/, gpointer user_data) {
  GCheckersWindowPuzzleDialogData *data = user_data;
  g_return_if_fail(data != NULL);
  g_return_if_fail(GCHECKERS_IS_WINDOW(data->self));
  g_return_if_fail(GTK_IS_WINDOW(data->dialog));

  PlayerRuleset ruleset = (PlayerRuleset)gtk_drop_down_get_selected(data->ruleset);
  if (!gcheckers_window_start_random_puzzle_mode_for_ruleset(data->self, ruleset)) {
    g_debug("Failed to start puzzle mode for ruleset %s", checkers_ruleset_short_name(ruleset));
    return;
  }

  gtk_window_destroy(data->dialog);
}

void gcheckers_window_present_puzzle_dialog(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  guint ruleset_count = checkers_ruleset_count();
  g_return_if_fail(ruleset_count > 0);
  g_return_if_fail(ruleset_count <= G_MAXUINT8);

  GtkWidget *dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog), "Play puzzles");
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

  g_auto(GStrv) ruleset_options = g_new0(char *, ruleset_count + 1);
  for (guint i = 0; i < ruleset_count; ++i) {
    const char *name = checkers_ruleset_name((PlayerRuleset)i);
    if (name == NULL) {
      g_debug("Missing puzzle ruleset name");
      gtk_window_destroy(GTK_WINDOW(dialog));
      return;
    }
    ruleset_options[i] = g_strdup(name);
  }

  GtkWidget *ruleset_label = gtk_label_new("Variant");
  gtk_widget_set_halign(ruleset_label, GTK_ALIGN_START);
  GtkWidget *ruleset_summary = gtk_label_new(NULL);
  gtk_widget_set_halign(ruleset_summary, GTK_ALIGN_START);
  gtk_label_set_xalign(GTK_LABEL(ruleset_summary), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(ruleset_summary), FALSE);
  gtk_label_set_ellipsize(GTK_LABEL(ruleset_summary), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand(ruleset_summary, TRUE);

  GtkDropDown *ruleset = GTK_DROP_DOWN(
      gtk_drop_down_new_from_strings((const char *const *)ruleset_options));
  gtk_drop_down_set_selected(ruleset, gcheckers_window_get_ruleset(self));

  gtk_grid_attach(GTK_GRID(grid), ruleset_label, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(ruleset), 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), ruleset_summary, 1, 1, 1, 1);

  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(content), actions);

  GtkWidget *cancel_button = gtk_button_new_with_label("Cancel");
  GtkWidget *start_button = gtk_button_new_with_label("Start");
  gtk_box_append(GTK_BOX(actions), cancel_button);
  gtk_box_append(GTK_BOX(actions), start_button);

  GCheckersWindowPuzzleDialogData *data = g_new0(GCheckersWindowPuzzleDialogData, 1);
  data->self = g_object_ref(self);
  data->dialog = GTK_WINDOW(dialog);
  data->ruleset = ruleset;
  data->ruleset_summary = GTK_LABEL(ruleset_summary);
  gcheckers_window_puzzle_dialog_update_ruleset_summary(data);

  g_signal_connect(cancel_button,
                   "clicked",
                   G_CALLBACK(gcheckers_window_on_puzzle_dialog_cancel_clicked),
                   data);
  g_signal_connect(start_button,
                   "clicked",
                   G_CALLBACK(gcheckers_window_on_puzzle_dialog_start_clicked),
                   data);
  g_signal_connect(dialog,
                   "destroy",
                   G_CALLBACK(gcheckers_window_on_puzzle_dialog_destroy),
                   data);
  g_signal_connect(ruleset,
                   "notify::selected",
                   G_CALLBACK(gcheckers_window_on_puzzle_dialog_ruleset_selected),
                   data);
  gtk_window_present(GTK_WINDOW(dialog));
}
