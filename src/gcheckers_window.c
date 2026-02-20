#include "gcheckers_window.h"

#include "board_view.h"
#include "gcheckers_sgf_controller.h"
#include "gcheckers_style.h"
#include "player_controls_panel.h"
#include "widget_utils.h"

struct _GCheckersWindow {
  GtkApplicationWindow parent_instance;
  GCheckersModel *model;
  GtkWidget *status_label;
  GtkWidget *reset_button;
  GtkWidget *reselect_sgf_button;
  GtkWidget *controls_row;
  BoardView *board_view;
  PlayerControlsPanel *controls_panel;
  GCheckersSgfController *sgf_controller;
  gulong state_handler_id;
  guint auto_move_source_id;
};

G_DEFINE_TYPE(GCheckersWindow, gcheckers_window, GTK_TYPE_APPLICATION_WINDOW)

static void gcheckers_window_on_force_move_requested(PlayerControlsPanel *panel, gpointer user_data);

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

static gboolean gcheckers_window_apply_player_move(const CheckersMove *move, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self->sgf_controller), FALSE);

  if (!gcheckers_sgf_controller_apply_move(self->sgf_controller, move)) {
    return FALSE;
  }

  gcheckers_window_print_move("Player", move);
  return TRUE;
}

static gboolean gcheckers_window_is_user_control(GCheckersWindow *self, CheckersColor color) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), FALSE);

  if (!self->controls_panel) {
    g_debug("Missing controls panel when checking control mode\n");
    return TRUE;
  }

  return player_controls_panel_is_user_control(self->controls_panel, color);
}

static gboolean gcheckers_window_is_computer_control(GCheckersWindow *self, CheckersColor color) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), FALSE);

  return !gcheckers_window_is_user_control(self, color);
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

  gboolean input_enabled = state->winner == CHECKERS_WINNER_NONE;
  board_view_set_input_enabled(self->board_view, input_enabled);

  if (!self->controls_panel) {
    g_debug("Missing controls panel while updating sensitivity\n");
    return;
  }
  player_controls_panel_set_force_move_sensitive(self->controls_panel,
                                                 state->winner == CHECKERS_WINNER_NONE);
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

  board_view_update(self->board_view);
}

static gboolean gcheckers_window_auto_force_move_cb(gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), G_SOURCE_REMOVE);

  self->auto_move_source_id = 0;
  gcheckers_window_on_force_move_requested(NULL, self);
  return G_SOURCE_REMOVE;
}

static void gcheckers_window_schedule_auto_force_move(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  if (self->auto_move_source_id != 0) {
    return;
  }

  self->auto_move_source_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                                              gcheckers_window_auto_force_move_cb,
                                              g_object_ref(self),
                                              (GDestroyNotify)g_object_unref);
}

static void gcheckers_window_maybe_trigger_auto_move(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));

  if (!self->sgf_controller) {
    g_debug("Missing SGF controller for auto move\n");
    return;
  }
  if (gcheckers_sgf_controller_is_replaying(self->sgf_controller)) {
    return;
  }

  const GameState *state = gcheckers_model_peek_state(self->model);
  if (!state) {
    g_debug("Failed to fetch game state for auto move\n");
    return;
  }
  if (state->winner != CHECKERS_WINNER_NONE) {
    return;
  }
  if (!gcheckers_window_is_computer_control(self, state->turn)) {
    return;
  }

  gcheckers_window_schedule_auto_force_move(self);
}

static void gcheckers_window_on_state_changed(GCheckersModel *model, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_MODEL(model));
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  gcheckers_window_update_status(self);
  gcheckers_window_update_control_state(self);
  gcheckers_window_maybe_trigger_auto_move(self);
}

static void gcheckers_window_on_reset_clicked(GtkButton * /*button*/, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));

  gcheckers_model_reset(self->model);
  board_view_clear_selection(self->board_view);
  gcheckers_sgf_controller_new_game(self->sgf_controller);
}

static void gcheckers_window_on_sgf_reselect_clicked(GtkButton * /*button*/, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  if (!self->sgf_controller) {
    g_debug("Missing SGF controller for layout resync\n");
    return;
  }

  gcheckers_sgf_controller_force_layout_resync(self->sgf_controller);
}

static void gcheckers_window_on_control_changed(PlayerControlsPanel * /*panel*/, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  gcheckers_window_update_control_state(self);
}

static void gcheckers_window_on_analysis_requested(GCheckersSgfController * /*controller*/,
                                                   gpointer /*node*/,
                                                   gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(self->controls_panel != NULL);

  player_controls_panel_set_all_user(self->controls_panel);
  gcheckers_window_update_control_state(self);
}

static void gcheckers_window_on_force_move_requested(PlayerControlsPanel * /*panel*/, gpointer user_data) {
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
  if (gcheckers_sgf_controller_is_replaying(self->sgf_controller)) {
    g_debug("Ignoring forced move while replaying SGF\n");
    return;
  }

  CheckersMove move;
  if (gcheckers_sgf_controller_step_random_move(self->sgf_controller, &move)) {
    gcheckers_window_print_move("AI", &move);
  }
}

static void gcheckers_window_set_model(GCheckersWindow *self, GCheckersModel *model) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(model));

  g_clear_handle_id(&self->auto_move_source_id, g_source_remove);

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
  board_view_set_model(self->board_view, self->model);
  gcheckers_sgf_controller_set_model(self->sgf_controller, self->model);
  gcheckers_window_update_status(self);
  gcheckers_window_update_control_state(self);
}

static gboolean gcheckers_window_unparent_controls_panel(GCheckersWindow *self) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), FALSE);

  if (!self->controls_panel) {
    return TRUE;
  }

  GtkWidget *panel_widget = GTK_WIDGET(self->controls_panel);
  gboolean removed = gcheckers_widget_remove_from_parent(panel_widget);
  if (!removed && gtk_widget_get_parent(panel_widget)) {
    g_debug("Failed to remove controls panel from parent during dispose\n");
    return FALSE;
  }

  return TRUE;
}

static void gcheckers_window_dispose(GObject *object) {
  GCheckersWindow *self = GCHECKERS_WINDOW(object);

  if (self->model && self->state_handler_id != 0) {
    g_signal_handler_disconnect(self->model, self->state_handler_id);
    self->state_handler_id = 0;
  }

  gboolean panel_removed = gcheckers_window_unparent_controls_panel(self);
  g_clear_handle_id(&self->auto_move_source_id, g_source_remove);

  gcheckers_window_unparent_controls_panel(self);

  g_clear_object(&self->sgf_controller);
  if (panel_removed) {
    g_clear_object(&self->controls_panel);
  } else {
    self->controls_panel = NULL;
  }
  g_clear_object(&self->board_view);
  g_clear_object(&self->model);
  self->controls_row = NULL;
  G_OBJECT_CLASS(gcheckers_window_parent_class)->dispose(object);
}

static void gcheckers_window_class_init(GCheckersWindowClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = gcheckers_window_dispose;
}

static void gcheckers_window_init(GCheckersWindow *self) {
  self->auto_move_source_id = 0;

  gtk_window_set_title(GTK_WINDOW(self), "gcheckers");
  gtk_window_set_default_size(GTK_WINDOW(self), 600, 700);

  gcheckers_style_init();

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

  self->board_view = board_view_new();
  gtk_box_append(GTK_BOX(left_panel), board_view_get_widget(self->board_view));

  GtkWidget *button_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_append(GTK_BOX(left_panel), button_row);

  self->reset_button = gtk_button_new_with_label("Reset");
  g_signal_connect(self->reset_button,
                   "clicked",
                   G_CALLBACK(gcheckers_window_on_reset_clicked),
                   self);
  gtk_box_append(GTK_BOX(button_row), self->reset_button);

  self->reselect_sgf_button = gtk_button_new_with_label("Reselect SGF");
  g_signal_connect(self->reselect_sgf_button,
                   "clicked",
                   G_CALLBACK(gcheckers_window_on_sgf_reselect_clicked),
                   self);
  gtk_box_append(GTK_BOX(button_row), self->reselect_sgf_button);

  GtkWidget *right_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_hexpand(right_panel, TRUE);
  gtk_widget_set_vexpand(right_panel, TRUE);
  gtk_paned_set_end_child(GTK_PANED(paned), right_panel);
  gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);

  self->controls_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_append(GTK_BOX(right_panel), self->controls_row);

  self->controls_panel = g_object_ref_sink(player_controls_panel_new());
  gtk_box_append(GTK_BOX(self->controls_row), GTK_WIDGET(self->controls_panel));
  g_signal_connect(self->controls_panel,
                   "control-changed",
                   G_CALLBACK(gcheckers_window_on_control_changed),
                   self);
  g_signal_connect(self->controls_panel,
                   "force-move-requested",
                   G_CALLBACK(gcheckers_window_on_force_move_requested),
                   self);

  self->sgf_controller = gcheckers_sgf_controller_new(self->board_view);
  board_view_set_move_handler(self->board_view, gcheckers_window_apply_player_move, self);
  GtkWidget *sgf_widget = gcheckers_sgf_controller_get_widget(self->sgf_controller);
  g_return_if_fail(sgf_widget != NULL);
  g_signal_connect(self->sgf_controller,
                   "analysis-requested",
                   G_CALLBACK(gcheckers_window_on_analysis_requested),
                   self);
  gtk_widget_add_css_class(sgf_widget, "sgf-panel");
  gtk_box_append(GTK_BOX(right_panel), sgf_widget);
}

PlayerControlsPanel *gcheckers_window_get_controls_panel(GCheckersWindow *self) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), NULL);

  if (!self->controls_panel) {
    g_debug("Missing controls panel\n");
    return NULL;
  }

  return self->controls_panel;
}

GCheckersSgfController *gcheckers_window_get_sgf_controller(GCheckersWindow *self) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), NULL);

  if (!self->sgf_controller) {
    g_debug("Missing SGF controller\n");
    return NULL;
  }

  return self->sgf_controller;
}

GCheckersWindow *gcheckers_window_new(GtkApplication *app, GCheckersModel *model) {
  g_return_val_if_fail(GTK_IS_APPLICATION(app), NULL);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(model), NULL);

  GCheckersWindow *window = g_object_new(GCHECKERS_TYPE_WINDOW, "application", app, NULL);
  gcheckers_window_set_model(window, model);
  return window;
}
