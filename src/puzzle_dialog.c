#include "puzzle_dialog.h"

#include "application.h"
#include "puzzle_catalog.h"
#include "puzzle_progress.h"
#include "rulesets.h"

typedef struct {
  GCheckersWindow *self;
  GtkWindow *dialog;
  GtkDropDown *ruleset;
  GtkLabel *ruleset_summary;
  GtkWidget *puzzle_area;
  GHashTable *status_map;
} GCheckersWindowPuzzleDialogData;

typedef struct {
  GCheckersWindowPuzzleDialogData *dialog_data;
  PlayerRuleset ruleset;
  char *path;
} GCheckersWindowPuzzleButtonData;

static void gcheckers_window_puzzle_button_data_free(GCheckersWindowPuzzleButtonData *data) {
  if (data == NULL) {
    return;
  }

  g_clear_pointer(&data->path, g_free);
  g_free(data);
}

static void gcheckers_window_puzzle_dialog_data_free(GCheckersWindowPuzzleDialogData *data) {
  if (data == NULL) {
    return;
  }

  g_clear_pointer(&data->status_map, g_hash_table_unref);
  g_clear_object(&data->self);
  g_free(data);
}

static void gcheckers_window_on_puzzle_dialog_destroy(GtkWindow * /*dialog*/, gpointer user_data) {
  GCheckersWindowPuzzleDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  gcheckers_window_puzzle_dialog_data_free(data);
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

static const char *gcheckers_window_puzzle_status_icon_name(CheckersPuzzleStatus status) {
  switch (status) {
    case CHECKERS_PUZZLE_STATUS_SOLVED:
      return "object-select-symbolic";
    case CHECKERS_PUZZLE_STATUS_FAILED:
      return "window-close-symbolic";
    case CHECKERS_PUZZLE_STATUS_UNTRIED:
    default:
      return NULL;
  }
}

static void gcheckers_window_puzzle_dialog_apply_status_css(GtkWidget *button, CheckersPuzzleStatus status) {
  g_return_if_fail(GTK_IS_WIDGET(button));

  gtk_widget_add_css_class(button, "puzzle-picker-button");
  gtk_widget_remove_css_class(button, "puzzle-picker-untried");
  gtk_widget_remove_css_class(button, "puzzle-picker-solved");
  gtk_widget_remove_css_class(button, "puzzle-picker-failed");

  if (status == CHECKERS_PUZZLE_STATUS_SOLVED) {
    gtk_widget_add_css_class(button, "puzzle-picker-solved");
  } else if (status == CHECKERS_PUZZLE_STATUS_FAILED) {
    gtk_widget_add_css_class(button, "puzzle-picker-failed");
  } else {
    gtk_widget_add_css_class(button, "puzzle-picker-untried");
  }
}

static GtkWidget *gcheckers_window_puzzle_dialog_create_puzzle_button(const CheckersPuzzleCatalogEntry *entry,
                                                                      PlayerRuleset ruleset,
                                                                      CheckersPuzzleStatus status,
                                                                      GCheckersWindowPuzzleDialogData *data) {
  g_return_val_if_fail(entry != NULL, NULL);
  g_return_val_if_fail(data != NULL, NULL);

  GtkWidget *button = gtk_button_new();
  gtk_widget_set_size_request(button, 52, 52);
  gcheckers_window_puzzle_dialog_apply_status_css(button, status);

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(content, GTK_ALIGN_CENTER);
  gtk_button_set_child(GTK_BUTTON(button), content);

  g_autofree char *number_text = g_strdup_printf("%u", entry->puzzle_number);
  GtkWidget *number_label = gtk_label_new(number_text);
  gtk_widget_add_css_class(number_label, "puzzle-picker-number");
  gtk_box_append(GTK_BOX(content), number_label);

  const char *icon_name = gcheckers_window_puzzle_status_icon_name(status);
  if (icon_name != NULL) {
    GtkWidget *image = gtk_image_new_from_icon_name(icon_name);
    gtk_widget_add_css_class(image, "puzzle-picker-icon");
    gtk_box_append(GTK_BOX(content), image);
  }

  GCheckersWindowPuzzleButtonData *button_data = g_new0(GCheckersWindowPuzzleButtonData, 1);
  button_data->dialog_data = data;
  button_data->ruleset = ruleset;
  button_data->path = g_strdup(entry->path);
  g_object_set_data_full(G_OBJECT(button),
                         "gcheckers-puzzle-button-data",
                         button_data,
                         (GDestroyNotify)gcheckers_window_puzzle_button_data_free);
  g_object_set_data(G_OBJECT(button), "puzzle-number", GUINT_TO_POINTER(entry->puzzle_number + 1));

  const char *ruleset_name = checkers_ruleset_name(ruleset);
  g_autofree char *tooltip = g_strdup_printf("%s puzzle %u", ruleset_name != NULL ? ruleset_name : "Puzzle",
                                             entry->puzzle_number);
  gtk_widget_set_tooltip_text(button, tooltip);
  return button;
}

static void gcheckers_window_on_puzzle_button_clicked(GtkButton *button, gpointer /*user_data*/) {
  g_return_if_fail(GTK_IS_BUTTON(button));

  GCheckersWindowPuzzleButtonData *button_data =
      g_object_get_data(G_OBJECT(button), "gcheckers-puzzle-button-data");
  g_return_if_fail(button_data != NULL);
  g_return_if_fail(button_data->dialog_data != NULL);
  g_return_if_fail(GCHECKERS_IS_WINDOW(button_data->dialog_data->self));
  g_return_if_fail(GTK_IS_WINDOW(button_data->dialog_data->dialog));

  if (!gcheckers_window_start_puzzle_mode_for_path(button_data->dialog_data->self,
                                                   button_data->ruleset,
                                                   button_data->path)) {
    g_debug("Failed to start puzzle mode for path %s", button_data->path);
    return;
  }

  gtk_window_destroy(button_data->dialog_data->dialog);
}

static void gcheckers_window_puzzle_dialog_rebuild_grid(GCheckersWindowPuzzleDialogData *data) {
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_WIDGET(data->puzzle_area));

  GtkWidget *old_child = gtk_widget_get_first_child(data->puzzle_area);
  if (old_child != NULL) {
    gtk_box_remove(GTK_BOX(data->puzzle_area), old_child);
  }

  PlayerRuleset ruleset = (PlayerRuleset)gtk_drop_down_get_selected(data->ruleset);
  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) entries = checkers_puzzle_catalog_load_for_ruleset(ruleset, &error);
  if (entries == NULL) {
    g_debug("Failed to build puzzle picker catalog: %s", error != NULL ? error->message : "unknown error");
    GtkWidget *label = gtk_label_new("Failed to load puzzles for this variant.");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_top(label, 8);
    gtk_box_append(GTK_BOX(data->puzzle_area), label);
    return;
  }

  if (entries->len == 0) {
    GtkWidget *label = gtk_label_new("No puzzles found for this variant.");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_top(label, 8);
    gtk_box_append(GTK_BOX(data->puzzle_area), label);
    return;
  }

  GtkWidget *scroller = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(scroller, TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_box_append(GTK_BOX(data->puzzle_area), scroller);

  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
  gtk_widget_set_margin_top(grid, 8);
  gtk_widget_set_margin_bottom(grid, 8);
  gtk_widget_set_margin_start(grid, 2);
  gtk_widget_set_margin_end(grid, 2);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), grid);

  for (guint i = 0; i < entries->len; i++) {
    CheckersPuzzleCatalogEntry *entry = g_ptr_array_index(entries, i);
    g_return_if_fail(entry != NULL);

    CheckersPuzzleStatus status = CHECKERS_PUZZLE_STATUS_UNTRIED;
    if (data->status_map != NULL) {
      CheckersPuzzleStatusEntry *status_entry = g_hash_table_lookup(data->status_map, entry->puzzle_id);
      if (status_entry != NULL) {
        status = status_entry->status;
      }
    }

    GtkWidget *button =
        gcheckers_window_puzzle_dialog_create_puzzle_button(entry, ruleset, status, data);
    g_signal_connect(button, "clicked", G_CALLBACK(gcheckers_window_on_puzzle_button_clicked), NULL);
    gtk_grid_attach(GTK_GRID(grid), button, (gint)(i % 10), (gint)(i / 10), 1, 1);
  }
}

static void gcheckers_window_on_puzzle_dialog_ruleset_selected(GObject * /*object*/,
                                                               GParamSpec * /*pspec*/,
                                                               gpointer user_data) {
  GCheckersWindowPuzzleDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  gcheckers_window_puzzle_dialog_update_ruleset_summary(data);
  gcheckers_window_puzzle_dialog_rebuild_grid(data);
}

static void gcheckers_window_on_puzzle_dialog_cancel_clicked(GtkButton * /*button*/, gpointer user_data) {
  GCheckersWindowPuzzleDialogData *data = user_data;
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_WINDOW(data->dialog));

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
  gtk_window_set_default_size(GTK_WINDOW(dialog), 640, 420);

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

  GtkWidget *puzzle_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_vexpand(puzzle_area, TRUE);
  gtk_box_append(GTK_BOX(content), puzzle_area);

  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(content), actions);

  GtkWidget *cancel_button = gtk_button_new_with_label("Cancel");
  gtk_box_append(GTK_BOX(actions), cancel_button);

  GCheckersWindowPuzzleDialogData *data = g_new0(GCheckersWindowPuzzleDialogData, 1);
  data->self = g_object_ref(self);
  data->dialog = GTK_WINDOW(dialog);
  data->ruleset = ruleset;
  data->ruleset_summary = GTK_LABEL(ruleset_summary);
  data->puzzle_area = puzzle_area;

  GCheckersApplication *app = GCHECKERS_APPLICATION(gtk_window_get_application(GTK_WINDOW(self)));
  if (app != NULL) {
    CheckersPuzzleProgressStore *store = gcheckers_application_get_puzzle_progress_store(app);
    if (store != NULL) {
      g_autoptr(GError) error = NULL;
      data->status_map = checkers_puzzle_progress_store_load_status_map(store, &error);
      if (data->status_map == NULL) {
        g_debug("Failed to load puzzle status map: %s", error != NULL ? error->message : "unknown error");
      }
    }
  }

  gcheckers_window_puzzle_dialog_update_ruleset_summary(data);
  gcheckers_window_puzzle_dialog_rebuild_grid(data);

  g_signal_connect(cancel_button,
                   "clicked",
                   G_CALLBACK(gcheckers_window_on_puzzle_dialog_cancel_clicked),
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
