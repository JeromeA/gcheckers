#include "puzzle_dialog.h"

#include "active_game_backend.h"
#include "puzzle_catalog.h"

typedef struct {
  GtkWindow *dialog;
  GtkDropDown *variant;
  GtkLabel *variant_summary;
  GtkWidget *puzzle_area;
  GHashTable *status_map;
  GGamePuzzleDialogDoneFunc done_func;
  gpointer user_data;
  GDestroyNotify user_data_destroy;
} GGameWindowPuzzleDialogData;

typedef struct {
  GGameWindowPuzzleDialogData *dialog_data;
  const GameBackendVariant *variant;
  char *path;
} GGameWindowPuzzleButtonData;

typedef struct {
  GtkScrolledWindow *scroller;
  guint row;
} GGameWindowPuzzleScrollData;

static gboolean ggame_window_puzzle_dialog_destroy_hidden_cb(gpointer user_data) {
  GtkWindow *dialog = user_data;
  g_return_val_if_fail(GTK_IS_WINDOW(dialog), G_SOURCE_REMOVE);

  gtk_window_destroy(dialog);
  g_object_unref(dialog);
  return G_SOURCE_REMOVE;
}

static void ggame_window_puzzle_button_data_free(GGameWindowPuzzleButtonData *data) {
  if (data == NULL) {
    return;
  }

  g_clear_pointer(&data->path, g_free);
  g_free(data);
}

static void ggame_window_puzzle_scroll_data_free(GGameWindowPuzzleScrollData *data) {
  if (data == NULL) {
    return;
  }

  g_clear_object(&data->scroller);
  g_free(data);
}

static gboolean ggame_window_puzzle_dialog_scroll_to_row_cb(gpointer user_data) {
  GGameWindowPuzzleScrollData *data = user_data;
  g_return_val_if_fail(data != NULL, G_SOURCE_REMOVE);
  g_return_val_if_fail(GTK_IS_SCROLLED_WINDOW(data->scroller), G_SOURCE_REMOVE);

  GtkAdjustment *adjustment = gtk_scrolled_window_get_vadjustment(data->scroller);
  g_return_val_if_fail(GTK_IS_ADJUSTMENT(adjustment), G_SOURCE_REMOVE);

  double target = 8.0 + (double)data->row * (52.0 + 8.0);
  double lower = gtk_adjustment_get_lower(adjustment);
  double upper = gtk_adjustment_get_upper(adjustment);
  double page_size = gtk_adjustment_get_page_size(adjustment);
  double max_value = MAX(lower, upper - page_size);
  gtk_adjustment_set_value(adjustment, CLAMP(target, lower, max_value));
  return G_SOURCE_REMOVE;
}

static void ggame_window_puzzle_dialog_data_free(GGameWindowPuzzleDialogData *data) {
  if (data == NULL) {
    return;
  }

  g_clear_pointer(&data->status_map, g_hash_table_unref);
  if (data->user_data_destroy != NULL) {
    data->user_data_destroy(data->user_data);
  }
  g_free(data);
}

static void ggame_window_on_puzzle_dialog_destroy(GtkWindow * /*dialog*/, gpointer user_data) {
  GGameWindowPuzzleDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  if (data->done_func != NULL) {
    data->done_func(FALSE, NULL, NULL, data->user_data);
  }
  ggame_window_puzzle_dialog_data_free(data);
}

static void ggame_window_puzzle_dialog_update_variant_summary(GGameWindowPuzzleDialogData *data) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  g_return_if_fail(data != NULL);
  g_return_if_fail(backend != NULL);
  g_return_if_fail(GTK_IS_DROP_DOWN(data->variant));
  g_return_if_fail(GTK_IS_LABEL(data->variant_summary));
  g_return_if_fail(backend->variant_at != NULL);

  const GameBackendVariant *variant = backend->variant_at(gtk_drop_down_get_selected(data->variant));
  const char *summary = variant != NULL ? variant->summary : NULL;
  if (summary == NULL) {
    g_debug("Missing puzzle variant summary");
    return;
  }

  gtk_label_set_text(data->variant_summary, summary);
}

static const char *ggame_window_puzzle_status_icon_name(GGamePuzzleStatus status) {
  switch (status) {
    case GGAME_PUZZLE_STATUS_SOLVED:
      return "object-select-symbolic";
    case GGAME_PUZZLE_STATUS_FAILED:
      return "window-close-symbolic";
    case GGAME_PUZZLE_STATUS_UNTRIED:
    default:
      return NULL;
  }
}

static void ggame_window_puzzle_dialog_apply_status_css(GtkWidget *button, GGamePuzzleStatus status) {
  g_return_if_fail(GTK_IS_WIDGET(button));

  gtk_widget_add_css_class(button, "puzzle-picker-button");
  gtk_widget_remove_css_class(button, "puzzle-picker-untried");
  gtk_widget_remove_css_class(button, "puzzle-picker-solved");
  gtk_widget_remove_css_class(button, "puzzle-picker-failed");

  if (status == GGAME_PUZZLE_STATUS_SOLVED) {
    gtk_widget_add_css_class(button, "puzzle-picker-solved");
  } else if (status == GGAME_PUZZLE_STATUS_FAILED) {
    gtk_widget_add_css_class(button, "puzzle-picker-failed");
  } else {
    gtk_widget_add_css_class(button, "puzzle-picker-untried");
  }
}

static GtkWidget *ggame_window_puzzle_dialog_create_puzzle_button(const GamePuzzleCatalogEntry *entry,
                                                                  const GameBackendVariant *variant,
                                                                  GGamePuzzleStatus status,
                                                                  GGameWindowPuzzleDialogData *data) {
  g_return_val_if_fail(entry != NULL, NULL);
  g_return_val_if_fail(variant != NULL, NULL);
  g_return_val_if_fail(data != NULL, NULL);

  GtkWidget *button = gtk_button_new();
  gtk_widget_set_size_request(button, 52, 52);
  ggame_window_puzzle_dialog_apply_status_css(button, status);

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(content, GTK_ALIGN_CENTER);
  gtk_button_set_child(GTK_BUTTON(button), content);

  g_autofree char *number_text = g_strdup_printf("%u", entry->puzzle_number);
  GtkWidget *number_label = gtk_label_new(number_text);
  gtk_widget_add_css_class(number_label, "puzzle-picker-number");
  gtk_box_append(GTK_BOX(content), number_label);

  const char *icon_name = ggame_window_puzzle_status_icon_name(status);
  if (icon_name != NULL) {
    GtkWidget *image = gtk_image_new_from_icon_name(icon_name);
    gtk_widget_add_css_class(image, "puzzle-picker-icon");
    gtk_box_append(GTK_BOX(content), image);
  }

  GGameWindowPuzzleButtonData *button_data = g_new0(GGameWindowPuzzleButtonData, 1);
  button_data->dialog_data = data;
  button_data->variant = variant;
  button_data->path = g_strdup(entry->path);
  g_object_set_data_full(G_OBJECT(button),
                         "gcheckers-puzzle-button-data",
                         button_data,
                         (GDestroyNotify)ggame_window_puzzle_button_data_free);
  g_object_set_data(G_OBJECT(button), "puzzle-number", GUINT_TO_POINTER(entry->puzzle_number + 1));

  g_autofree char *tooltip = g_strdup_printf("%s puzzle %u",
                                             variant->name != NULL ? variant->name : "Puzzle",
                                             entry->puzzle_number);
  gtk_widget_set_tooltip_text(button, tooltip);
  return button;
}

static void ggame_window_on_puzzle_button_clicked(GtkButton *button, gpointer /*user_data*/) {
  g_return_if_fail(GTK_IS_BUTTON(button));

  GGameWindowPuzzleButtonData *button_data =
      g_object_get_data(G_OBJECT(button), "gcheckers-puzzle-button-data");
  g_return_if_fail(button_data != NULL);
  g_return_if_fail(button_data->dialog_data != NULL);
  g_return_if_fail(GTK_IS_WINDOW(button_data->dialog_data->dialog));

  GGameWindowPuzzleDialogData *data = button_data->dialog_data;
  GGamePuzzleDialogDoneFunc done_func = data->done_func;
  gpointer callback_user_data = data->user_data;
  GDestroyNotify callback_user_data_destroy = data->user_data_destroy;
  const GameBackendVariant *variant = button_data->variant;
  g_autofree char *path = g_strdup(button_data->path);

  data->done_func = NULL;
  data->user_data = NULL;
  data->user_data_destroy = NULL;

  gtk_widget_set_visible(GTK_WIDGET(data->dialog), FALSE);
  if (done_func != NULL) {
    done_func(TRUE, variant, path, callback_user_data);
  }
  if (callback_user_data_destroy != NULL) {
    callback_user_data_destroy(callback_user_data);
  }
  g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                  ggame_window_puzzle_dialog_destroy_hidden_cb,
                  g_object_ref(data->dialog),
                  NULL);
}

static void ggame_window_puzzle_dialog_rebuild_grid(GGameWindowPuzzleDialogData *data) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  g_return_if_fail(data != NULL);
  g_return_if_fail(backend != NULL);
  g_return_if_fail(backend->variant_at != NULL);
  g_return_if_fail(GTK_IS_WIDGET(data->puzzle_area));

  GtkWidget *old_child = gtk_widget_get_first_child(data->puzzle_area);
  if (old_child != NULL) {
    gtk_box_remove(GTK_BOX(data->puzzle_area), old_child);
  }

  const GameBackendVariant *variant = backend->variant_at(gtk_drop_down_get_selected(data->variant));
  if (variant == NULL) {
    g_debug("Failed to resolve puzzle variant");
    GtkWidget *label = gtk_label_new("Failed to resolve this puzzle variant.");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_top(label, 8);
    gtk_box_append(GTK_BOX(data->puzzle_area), label);
    return;
  }

  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) entries = game_puzzle_catalog_load_variant(backend, variant, &error);
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

  guint first_untried_row = 0;
  gboolean have_first_untried = FALSE;
  for (guint i = 0; i < entries->len; i++) {
    GamePuzzleCatalogEntry *entry = g_ptr_array_index(entries, i);
    g_return_if_fail(entry != NULL);

    GGamePuzzleStatus status = GGAME_PUZZLE_STATUS_UNTRIED;
    if (data->status_map != NULL) {
      GGamePuzzleStatusEntry *status_entry = g_hash_table_lookup(data->status_map, entry->puzzle_id);
      if (status_entry != NULL) {
        status = status_entry->status;
      }
    }
    if (!have_first_untried && status == GGAME_PUZZLE_STATUS_UNTRIED) {
      first_untried_row = i / 10;
      have_first_untried = TRUE;
    }

    GtkWidget *button = ggame_window_puzzle_dialog_create_puzzle_button(entry, variant, status, data);
    g_signal_connect(button, "clicked", G_CALLBACK(ggame_window_on_puzzle_button_clicked), NULL);
    gtk_grid_attach(GTK_GRID(grid), button, (gint)(i % 10), (gint)(i / 10), 1, 1);
  }

  if (have_first_untried && first_untried_row > 0) {
    GGameWindowPuzzleScrollData *scroll_data = g_new0(GGameWindowPuzzleScrollData, 1);
    scroll_data->scroller = g_object_ref(GTK_SCROLLED_WINDOW(scroller));
    scroll_data->row = first_untried_row;
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                    ggame_window_puzzle_dialog_scroll_to_row_cb,
                    scroll_data,
                    (GDestroyNotify)ggame_window_puzzle_scroll_data_free);
  }
}

static void ggame_window_on_puzzle_dialog_variant_selected(GObject * /*object*/,
                                                           GParamSpec * /*pspec*/,
                                                           gpointer user_data) {
  GGameWindowPuzzleDialogData *data = user_data;
  g_return_if_fail(data != NULL);

  ggame_window_puzzle_dialog_update_variant_summary(data);
  ggame_window_puzzle_dialog_rebuild_grid(data);
}

static void ggame_window_on_puzzle_dialog_cancel_clicked(GtkButton *button, gpointer user_data) {
  GGameWindowPuzzleDialogData *data = user_data;
  g_return_if_fail(data != NULL);
  g_return_if_fail(GTK_IS_BUTTON(button));
  g_return_if_fail(GTK_IS_WINDOW(data->dialog));

  gtk_widget_set_visible(GTK_WIDGET(data->dialog), FALSE);
  g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                  ggame_window_puzzle_dialog_destroy_hidden_cb,
                  g_object_ref(data->dialog),
                  NULL);
}

void ggame_puzzle_dialog_present(GtkWindow *parent,
                                     const GameBackendVariant *initial_variant,
                                     GGamePuzzleProgressStore *store,
                                     GGamePuzzleDialogDoneFunc done_func,
                                     gpointer user_data,
                                     GDestroyNotify user_data_destroy) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  g_return_if_fail(GTK_IS_WINDOW(parent));
  g_return_if_fail(backend != NULL);
  g_return_if_fail(backend->variant_count > 0);
  g_return_if_fail(backend->variant_at != NULL);

  GtkWidget *dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog), "Play puzzles");
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
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

  g_auto(GStrv) variant_options = g_new0(char *, backend->variant_count + 1);
  for (guint i = 0; i < backend->variant_count; ++i) {
    const GameBackendVariant *variant = backend->variant_at(i);
    if (variant == NULL || variant->name == NULL) {
      g_debug("Missing puzzle variant name");
      gtk_window_destroy(GTK_WINDOW(dialog));
      return;
    }
    variant_options[i] = g_strdup(variant->name);
  }

  GtkWidget *variant_label = gtk_label_new("Variant");
  gtk_widget_set_halign(variant_label, GTK_ALIGN_START);
  GtkWidget *variant_summary = gtk_label_new(NULL);
  gtk_widget_set_halign(variant_summary, GTK_ALIGN_START);
  gtk_label_set_xalign(GTK_LABEL(variant_summary), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(variant_summary), FALSE);
  gtk_label_set_ellipsize(GTK_LABEL(variant_summary), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand(variant_summary, TRUE);

  GtkDropDown *variant = GTK_DROP_DOWN(gtk_drop_down_new_from_strings((const char *const *) variant_options));
  if (initial_variant != NULL) {
    for (guint i = 0; i < backend->variant_count; ++i) {
      if (backend->variant_at(i) == initial_variant) {
        gtk_drop_down_set_selected(variant, i);
        break;
      }
    }
  }

  gtk_grid_attach(GTK_GRID(grid), variant_label, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(variant), 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), variant_summary, 1, 1, 1, 1);

  GtkWidget *puzzle_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_vexpand(puzzle_area, TRUE);
  gtk_box_append(GTK_BOX(content), puzzle_area);

  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(content), actions);

  GtkWidget *cancel_button = gtk_button_new_with_label("Cancel");
  gtk_box_append(GTK_BOX(actions), cancel_button);

  GGameWindowPuzzleDialogData *data = g_new0(GGameWindowPuzzleDialogData, 1);
  data->dialog = GTK_WINDOW(dialog);
  data->variant = variant;
  data->variant_summary = GTK_LABEL(variant_summary);
  data->puzzle_area = puzzle_area;
  data->done_func = done_func;
  data->user_data = user_data;
  data->user_data_destroy = user_data_destroy;

  if (store != NULL) {
    g_autoptr(GError) error = NULL;
    data->status_map = ggame_puzzle_progress_store_load_status_map(store, &error);
    if (data->status_map == NULL) {
      g_debug("Failed to load puzzle status map: %s", error != NULL ? error->message : "unknown error");
    }
  }

  ggame_window_puzzle_dialog_update_variant_summary(data);
  ggame_window_puzzle_dialog_rebuild_grid(data);

  g_signal_connect(cancel_button,
                   "clicked",
                   G_CALLBACK(ggame_window_on_puzzle_dialog_cancel_clicked),
                   data);
  g_signal_connect(dialog,
                   "destroy",
                   G_CALLBACK(ggame_window_on_puzzle_dialog_destroy),
                   data);
  g_signal_connect(variant,
                   "notify::selected",
                   G_CALLBACK(ggame_window_on_puzzle_dialog_variant_selected),
                   data);
  gtk_window_present(GTK_WINDOW(dialog));
}
