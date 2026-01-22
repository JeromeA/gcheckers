#include "gcheckers_window.h"
#include "gcheckers_board_view.h"
#include "sgf_tree.h"
#include "sgf_view.h"

#include <string.h>

struct _GCheckersWindow {
  GtkApplicationWindow parent_instance;
  GCheckersModel *model;
  GtkWidget *status_label;
  GtkWidget *reset_button;
  GCheckersBoardView *board_view;
  SgfTree *sgf_tree;
  SgfView *sgf_view;
  GtkDropDown *white_control;
  GtkDropDown *black_control;
  GtkWidget *force_move_button;
  gulong state_handler_id;
  gboolean is_replaying;
  guint last_history_size;
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

static SgfColor gcheckers_window_color_from_turn(CheckersColor color) {
  switch (color) {
    case CHECKERS_COLOR_BLACK:
      return SGF_COLOR_BLACK;
    case CHECKERS_COLOR_WHITE:
      return SGF_COLOR_WHITE;
    default:
      return SGF_COLOR_NONE;
  }
}

static gboolean gcheckers_window_is_user_control(GCheckersWindow *self, CheckersColor color) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), FALSE);

  GtkDropDown *control = color == CHECKERS_COLOR_WHITE ? self->white_control : self->black_control;
  if (!control) {
    return TRUE;
  }

  return gtk_drop_down_get_selected(control) == 0;
}

static void gcheckers_window_update_control_state(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  if (!self->model) {
    g_debug("Missing model while updating control state\n");
    return;
  }

  const GameState *state = gcheckers_model_peek_state(self->model);
  if (!state) {
    g_debug("Failed to fetch game state for control update\n");
    return;
  }

  gboolean input_enabled = state->winner == CHECKERS_WINNER_NONE &&
                           gcheckers_window_is_user_control(self, state->turn);
  gcheckers_board_view_set_input_enabled(self->board_view, input_enabled);

  if (self->force_move_button) {
    gtk_widget_set_sensitive(self->force_move_button, state->winner == CHECKERS_WINNER_NONE);
  }
}

static void gcheckers_window_append_sgf_move(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));
  g_return_if_fail(self->sgf_tree != NULL);

  if (self->is_replaying) {
    return;
  }

  guint history_size = gcheckers_model_get_history_size(self->model);
  if (history_size <= self->last_history_size) {
    self->last_history_size = history_size;
    return;
  }

  const CheckersMove *last_move = gcheckers_model_peek_last_move(self->model);
  if (!last_move) {
    self->last_history_size = history_size;
    return;
  }

  const GameState *state = gcheckers_model_peek_state(self->model);
  if (!state) {
    g_debug("Failed to fetch game state for SGF update\n");
    return;
  }

  CheckersColor mover = state->turn == CHECKERS_COLOR_WHITE ? CHECKERS_COLOR_BLACK : CHECKERS_COLOR_WHITE;
  SgfColor sgf_color = gcheckers_window_color_from_turn(mover);
  GBytes *payload = g_bytes_new(last_move, sizeof(*last_move));
  const SgfNode *node = sgf_tree_append_move(self->sgf_tree, sgf_color, payload);
  g_bytes_unref(payload);

  if (node) {
    sgf_view_set_selected(self->sgf_view, node);
  }
  self->last_history_size = history_size;
}

static GPtrArray *gcheckers_window_build_node_path(const SgfNode *node) {
  g_return_val_if_fail(node != NULL, NULL);

  GPtrArray *path = g_ptr_array_new();
  const SgfNode *cursor = node;
  while (cursor) {
    g_ptr_array_add(path, (gpointer)cursor);
    cursor = sgf_node_get_parent(cursor);
  }

  for (guint i = 0; i < path->len / 2; ++i) {
    gpointer tmp = g_ptr_array_index(path, i);
    g_ptr_array_index(path, i) = g_ptr_array_index(path, path->len - 1 - i);
    g_ptr_array_index(path, path->len - 1 - i) = tmp;
  }

  return path;
}

static void gcheckers_window_replay_to_node(GCheckersWindow *self, const SgfNode *node) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));
  g_return_if_fail(node != NULL);

  self->is_replaying = TRUE;
  gcheckers_model_reset(self->model);
  gcheckers_board_view_clear_selection(self->board_view);

  GPtrArray *path = gcheckers_window_build_node_path(node);
  if (!path) {
    self->is_replaying = FALSE;
    return;
  }

  for (guint i = 0; i < path->len; ++i) {
    const SgfNode *step = g_ptr_array_index(path, i);
    if (sgf_node_get_move_number(step) == 0) {
      continue;
    }

    GBytes *payload = sgf_node_get_payload(step);
    if (!payload) {
      g_debug("Missing payload for SGF node %u\n", sgf_node_get_move_number(step));
      continue;
    }

    gsize size = 0;
    const void *data = g_bytes_get_data(payload, &size);
    if (size != sizeof(CheckersMove)) {
      g_debug("Unexpected SGF payload size %zu\n", size);
      g_bytes_unref(payload);
      continue;
    }

    CheckersMove move;
    memcpy(&move, data, sizeof(move));
    if (!gcheckers_model_apply_move(self->model, &move)) {
      g_debug("Failed to replay SGF move %u\n", sgf_node_get_move_number(step));
    }
    g_bytes_unref(payload);
  }

  g_ptr_array_unref(path);
  self->last_history_size = gcheckers_model_get_history_size(self->model);
  self->is_replaying = FALSE;
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
  gcheckers_window_append_sgf_move(self);
  gcheckers_window_update_control_state(self);
}

static void gcheckers_window_on_reset_clicked(GtkButton * /*button*/, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));

  gcheckers_model_reset(self->model);
  gcheckers_board_view_clear_selection(self->board_view);
  sgf_tree_reset(self->sgf_tree);
  sgf_view_set_tree(self->sgf_view, self->sgf_tree);
  self->last_history_size = 0;
}

static void gcheckers_window_on_control_changed(GObject * /*object*/,
                                                GParamSpec * /*pspec*/,
                                                gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  gcheckers_window_update_control_state(self);
}

static void gcheckers_window_on_force_move_clicked(GtkButton * /*button*/, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));

  const GameState *state = gcheckers_model_peek_state(self->model);
  if (!state) {
    g_debug("Failed to fetch game state for forced move\n");
    return;
  }
  if (state->winner != CHECKERS_WINNER_NONE) {
    g_debug("Ignoring forced move after game end\n");
    return;
  }
  if (self->is_replaying) {
    g_debug("Ignoring forced move while replaying SGF\n");
    return;
  }

  CheckersMove move;
  if (gcheckers_model_step_random_move(self->model, &move)) {
    gcheckers_window_print_move("AI", &move);
  }
}

static void gcheckers_window_on_sgf_node_selected(SgfView * /*view*/,
                                                  const SgfNode *node,
                                                  gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(node != NULL);

  if (!sgf_tree_set_current(self->sgf_tree, node)) {
    g_debug("Failed to select SGF node\n");
    return;
  }

  gcheckers_window_replay_to_node(self, node);
  sgf_view_set_selected(self->sgf_view, node);
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
  sgf_tree_reset(self->sgf_tree);
  sgf_view_set_tree(self->sgf_view, self->sgf_tree);
  self->last_history_size = 0;
  self->is_replaying = FALSE;
  gcheckers_window_update_status(self);
  gcheckers_window_update_control_state(self);
}

static void gcheckers_window_dispose(GObject *object) {
  GCheckersWindow *self = GCHECKERS_WINDOW(object);

  if (self->model && self->state_handler_id != 0) {
    g_signal_handler_disconnect(self->model, self->state_handler_id);
    self->state_handler_id = 0;
  }
  g_clear_object(&self->model);
  g_clear_object(&self->board_view);
  g_clear_object(&self->sgf_view);
  g_clear_object(&self->sgf_tree);

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
      "}"
      ".sgf-panel { background-color: #f5f5f5; border: 1px solid #ccc; }"
      ".sgf-disc {"
      "  background-image: none;"
      "  border: 1px solid #222;"
      "  border-radius: 999px;"
      "  padding: 0;"
      "  min-width: 32px;"
      "  min-height: 32px;"
      "}"
      ".sgf-disc-black { background-color: #222; color: #fff; }"
      ".sgf-disc-white { background-color: #eee; color: #111; border: 1px solid #222; }"
      ".sgf-disc-selected { box-shadow: 0 0 0 3px #5cc7ff; }");
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

  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_hexpand(paned, TRUE);
  gtk_widget_set_vexpand(paned, TRUE);
  gtk_box_append(GTK_BOX(content), paned);

  GtkWidget *left_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_hexpand(left_panel, TRUE);
  gtk_widget_set_vexpand(left_panel, TRUE);
  gtk_paned_set_start_child(GTK_PANED(paned), left_panel);
  gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);

  self->status_label = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(self->status_label), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(self->status_label), TRUE);
  gtk_box_append(GTK_BOX(left_panel), self->status_label);

  self->board_view = gcheckers_board_view_new();
  gtk_box_append(GTK_BOX(left_panel), gcheckers_board_view_get_widget(self->board_view));

  GtkWidget *button_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_append(GTK_BOX(left_panel), button_row);

  self->reset_button = gtk_button_new_with_label("Reset");
  g_signal_connect(self->reset_button, "clicked", G_CALLBACK(gcheckers_window_on_reset_clicked), self);
  gtk_box_append(GTK_BOX(button_row), self->reset_button);

  GtkWidget *right_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_hexpand(right_panel, TRUE);
  gtk_widget_set_vexpand(right_panel, TRUE);
  gtk_paned_set_end_child(GTK_PANED(paned), right_panel);
  gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);

  GtkWidget *controls_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_append(GTK_BOX(right_panel), controls_row);

  GtkWidget *controls_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(controls_grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(controls_grid), 8);
  gtk_box_append(GTK_BOX(controls_row), controls_grid);

  GtkWidget *white_label = gtk_label_new("White");
  gtk_widget_set_halign(white_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(controls_grid), white_label, 0, 0, 1, 1);

  GtkWidget *black_label = gtk_label_new("Black");
  gtk_widget_set_halign(black_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(controls_grid), black_label, 0, 1, 1, 1);

  static const char *control_options[] = {"User", "Computer", NULL};
  self->white_control = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(control_options));
  self->black_control = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(control_options));
  gtk_drop_down_set_selected(self->white_control, 0);
  gtk_drop_down_set_selected(self->black_control, 1);
  gtk_grid_attach(GTK_GRID(controls_grid), GTK_WIDGET(self->white_control), 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(controls_grid), GTK_WIDGET(self->black_control), 1, 1, 1, 1);

  g_signal_connect(self->white_control,
                   "notify::selected",
                   G_CALLBACK(gcheckers_window_on_control_changed),
                   self);
  g_signal_connect(self->black_control,
                   "notify::selected",
                   G_CALLBACK(gcheckers_window_on_control_changed),
                   self);

  self->force_move_button = gtk_button_new_with_label("Force move");
  g_signal_connect(self->force_move_button,
                   "clicked",
                   G_CALLBACK(gcheckers_window_on_force_move_clicked),
                   self);
  gtk_box_append(GTK_BOX(controls_row), self->force_move_button);

  self->sgf_tree = sgf_tree_new();
  self->sgf_view = sgf_view_new();
  sgf_view_set_tree(self->sgf_view, self->sgf_tree);
  g_signal_connect(self->sgf_view, "node-selected", G_CALLBACK(gcheckers_window_on_sgf_node_selected), self);
  GtkWidget *sgf_widget = sgf_view_get_widget(self->sgf_view);
  gtk_widget_add_css_class(sgf_widget, "sgf-panel");
  gtk_box_append(GTK_BOX(right_panel), sgf_widget);

  self->is_replaying = FALSE;
  self->last_history_size = 0;
}

GCheckersWindow *gcheckers_window_new(GtkApplication *app, GCheckersModel *model) {
  g_return_val_if_fail(GTK_IS_APPLICATION(app), NULL);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(model), NULL);

  GCheckersWindow *window = g_object_new(GCHECKERS_TYPE_WINDOW, "application", app, NULL);
  gcheckers_window_set_model(window, model);
  return window;
}
