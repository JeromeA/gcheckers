#include "gcheckers_window.h"
#include "gcheckers_board_view.h"

struct _GCheckersWindow {
  GtkApplicationWindow parent_instance;
  GCheckersModel *model;
  GtkWidget *status_label;
  GtkWidget *reset_button;
  GCheckersBoardView *board_view;
  gulong state_handler_id;
  gboolean ai_in_progress;
};

G_DEFINE_TYPE(GCheckersWindow, gcheckers_window, GTK_TYPE_APPLICATION_WINDOW)

static void gcheckers_window_print_move(const char *label, const CheckersMove *move) {
  g_return_if_fail(label != NULL);
  g_return_if_fail(move != NULL);

  char buffer[128];
  if (!game_format_move_notation(move, buffer, sizeof(buffer))) {
    g_debug("Failed to format move notation\n");
    return;
  }
  g_print("%s plays: %s\n", label, buffer);
}

static void gcheckers_window_maybe_play_ai_move(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));

  const GameState *state = gcheckers_model_peek_state(self->model);
  if (!state) {
    g_debug("Failed to fetch game state for AI move\n");
    return;
  }
  if (state->winner != CHECKERS_WINNER_NONE || state->turn != CHECKERS_COLOR_BLACK) {
    return;
  }
  if (self->ai_in_progress) {
    return;
  }

  self->ai_in_progress = TRUE;
  CheckersMove move;
  if (gcheckers_model_step_random_move(self->model, &move)) {
    gcheckers_window_print_move("AI", &move);
  }
  self->ai_in_progress = FALSE;
}

static void gcheckers_window_update_status(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));

  g_autofree char *status = gcheckers_model_format_status(self->model);
  if (!status) {
    g_debug("Failed to format status text\n");
    return;
  }
  gtk_label_set_text(GTK_LABEL(self->status_label), status);

  gcheckers_board_view_update(self->board_view);
}

static void gcheckers_window_on_state_changed(GCheckersModel *model, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_MODEL(model));
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  gcheckers_window_update_status(self);
  gcheckers_window_maybe_play_ai_move(self);
}

static void gcheckers_window_on_reset_clicked(GtkButton * /*button*/, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));

  gcheckers_model_reset(self->model);
  gcheckers_board_view_clear_selection(self->board_view);
}

static void gcheckers_window_set_model(GCheckersWindow *self, GCheckersModel *model) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(model));

  if (self->model) {
    if (self->state_handler_id != 0) {
      g_signal_handler_disconnect(self->model, self->state_handler_id);
      self->state_handler_id = 0;
    }
    g_clear_object(&self->model);
  }

  self->model = g_object_ref(model);
  self->state_handler_id = g_signal_connect(self->model,
                                            "state-changed",
                                            G_CALLBACK(gcheckers_window_on_state_changed),
                                            self);
  gcheckers_board_view_set_model(self->board_view, self->model);
  gcheckers_window_update_status(self);
}

static void gcheckers_window_dispose(GObject *object) {
  GCheckersWindow *self = GCHECKERS_WINDOW(object);

  if (self->model && self->state_handler_id != 0) {
    g_signal_handler_disconnect(self->model, self->state_handler_id);
    self->state_handler_id = 0;
  }
  g_clear_object(&self->model);
  g_clear_object(&self->board_view);

  G_OBJECT_CLASS(gcheckers_window_parent_class)->dispose(object);
}

static void gcheckers_window_class_init(GCheckersWindowClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = gcheckers_window_dispose;
}

static void gcheckers_window_init(GCheckersWindow *self) {
  gtk_window_set_title(GTK_WINDOW(self), "gcheckers");
  gtk_window_set_default_size(GTK_WINDOW(self), 600, 700);

  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(
      provider,
      ".board { background-color: #2a160b; border: 2px solid #000; }"
      ".board-light { background-color: #e6d1a8; border-radius: 0; }"
      ".board-dark {"
      "  background-color: #3b2412;"
      "  background-image: none;"
      "  border-radius: 0;"
      "}"
      "button.board-dark {"
      "  background-color: #3b2412;"
      "  background-image: none;"
      "}"
      ".board-square { padding: 0; border-radius: 0; }"
      ".piece-label {"
      "  font-size: 30px;"
      "  color: #fff;"
      "  background-color: rgba(0, 0, 0, 0.6);"
      "  border-radius: 6px;"
      "  padding: 2px 6px;"
      "}"
      ".square-index {"
      "  font-size: 6px;"
      "  font-weight: 600;"
      "  padding: 1px 3px;"
      "  border-radius: 6px;"
      "  background-color: rgba(0, 0, 0, 0.6);"
      "  color: #fff;"
      "}"
      ".board-halo {"
      "  background-image:"
      "    radial-gradient(circle,"
      "      rgba(247, 215, 77, 0.85) 0,"
      "      rgba(247, 215, 77, 0.4) 50%,"
      "      rgba(247, 215, 77, 0.0) 72%);"
      "}"
      ".board-halo-selected {"
      "  background-image:"
      "    radial-gradient(circle,"
      "      rgba(96, 214, 120, 0.85) 0,"
      "      rgba(96, 214, 120, 0.4) 50%,"
      "      rgba(96, 214, 120, 0.0) 72%);"
      "}"
      "button.board-halo {"
      "  background-image:"
      "    radial-gradient(circle,"
      "      rgba(247, 215, 77, 0.85) 0,"
      "      rgba(247, 215, 77, 0.4) 50%,"
      "      rgba(247, 215, 77, 0.0) 72%);"
      "}"
      "button.board-halo-selected {"
      "  background-image:"
      "    radial-gradient(circle,"
      "      rgba(96, 214, 120, 0.85) 0,"
      "      rgba(96, 214, 120, 0.4) 50%,"
      "      rgba(96, 214, 120, 0.0) 72%);"
      "}");
  GdkDisplay *display = gdk_display_get_default();
  if (!display) {
    g_debug("Failed to fetch default display for CSS styling\n");
  } else {
    gtk_style_context_add_provider_for_display(display,
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  }
  g_object_unref(provider);

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(content, 16);
  gtk_widget_set_margin_bottom(content, 16);
  gtk_widget_set_margin_start(content, 16);
  gtk_widget_set_margin_end(content, 16);
  gtk_window_set_child(GTK_WINDOW(self), content);

  self->status_label = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(self->status_label), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(self->status_label), TRUE);
  gtk_box_append(GTK_BOX(content), self->status_label);

  self->board_view = gcheckers_board_view_new();
  gtk_box_append(GTK_BOX(content), gcheckers_board_view_get_widget(self->board_view));

  GtkWidget *button_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_append(GTK_BOX(content), button_row);

  self->reset_button = gtk_button_new_with_label("Reset");
  g_signal_connect(self->reset_button, "clicked", G_CALLBACK(gcheckers_window_on_reset_clicked), self);
  gtk_box_append(GTK_BOX(button_row), self->reset_button);
}

GCheckersWindow *gcheckers_window_new(GtkApplication *app, GCheckersModel *model) {
  g_return_val_if_fail(GTK_IS_APPLICATION(app), NULL);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(model), NULL);

  GCheckersWindow *window = g_object_new(GCHECKERS_TYPE_WINDOW, "application", app, NULL);
  gcheckers_window_set_model(window, model);
  return window;
}
