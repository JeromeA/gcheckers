#include "gcheckers_window.h"

#include "board_view.h"
#include "gcheckers_sgf_controller.h"
#include "widget_utils.h"

struct _GCheckersWindow {
  GtkApplicationWindow parent_instance;
  GCheckersModel *model;
  BoardView *board_view;
  GtkWidget *controls_panel;
  GCheckersSgfController *sgf_controller;
  gulong state_handler_id;
  guint startup_force_source_id;
  guint startup_force_remaining;
};

G_DEFINE_TYPE(GCheckersWindow, gcheckers_window, GTK_TYPE_APPLICATION_WINDOW)

static void gcheckers_window_on_force_move_requested(gpointer /*panel*/, gpointer user_data);
static gboolean gcheckers_window_startup_force_move_cb(gpointer user_data);

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
  }
}

static void gcheckers_window_update_ui(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  board_view_update(self->board_view);
}

static gboolean gcheckers_window_startup_force_move_cb(gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), G_SOURCE_REMOVE);

  if (self->startup_force_remaining == 0) {
    self->startup_force_source_id = 0;
    return G_SOURCE_REMOVE;
  }
  if (!self->model) {
    g_debug("Missing model during startup forced moves\n");
    return G_SOURCE_CONTINUE;
  }

  g_debug("Startup forced move executed; remaining-before-step=%u\n", self->startup_force_remaining);
  gcheckers_window_on_force_move_requested(NULL, self);
  self->startup_force_remaining--;

  if (self->startup_force_remaining == 0) {
    self->startup_force_source_id = 0;
    return G_SOURCE_REMOVE;
  }

  return G_SOURCE_CONTINUE;
}

static void gcheckers_window_on_state_changed(GCheckersModel *model, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_MODEL(model));
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  gcheckers_window_update_ui(self);
  gcheckers_window_update_control_state(self);
}

static void gcheckers_window_on_force_move_requested(gpointer /*panel*/, gpointer user_data) {
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

  gcheckers_model_step_random_move(self->model, NULL);
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
  board_view_set_model(self->board_view, self->model);
  gcheckers_sgf_controller_set_model(self->sgf_controller, self->model);
  gcheckers_window_update_ui(self);
  gcheckers_window_update_control_state(self);
}

static gboolean gcheckers_window_unparent_controls_panel(GCheckersWindow *self) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), FALSE);

  if (!self->controls_panel) {
    return TRUE;
  }

  gboolean removed = gcheckers_widget_remove_from_parent(self->controls_panel);
  if (!removed && gtk_widget_get_parent(self->controls_panel)) {
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
  g_clear_handle_id(&self->startup_force_source_id, g_source_remove);

  gcheckers_window_unparent_controls_panel(self);

  g_clear_object(&self->sgf_controller);
  if (panel_removed) {
    g_clear_object(&self->controls_panel);
  } else {
    self->controls_panel = NULL;
  }
  g_clear_object(&self->board_view);
  g_clear_object(&self->model);

  G_OBJECT_CLASS(gcheckers_window_parent_class)->dispose(object);
}

static void gcheckers_window_class_init(GCheckersWindowClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = gcheckers_window_dispose;
}

static void gcheckers_window_init(GCheckersWindow *self) {
  self->startup_force_source_id = 0;
  self->startup_force_remaining = 10;

  gtk_window_set_title(GTK_WINDOW(self), "gcheckers");
  gtk_window_set_default_size(GTK_WINDOW(self), 600, 700);

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

  self->board_view = board_view_new();
  gtk_box_append(GTK_BOX(left_panel), board_view_get_widget(self->board_view));

  GtkWidget *right_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_hexpand(right_panel, TRUE);
  gtk_widget_set_vexpand(right_panel, TRUE);
  gtk_paned_set_end_child(GTK_PANED(paned), right_panel);
  gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);

  self->controls_panel = g_object_ref_sink(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
  gtk_box_append(GTK_BOX(right_panel), self->controls_panel);

  self->sgf_controller = gcheckers_sgf_controller_new(self->board_view);
  GtkWidget *sgf_widget = gcheckers_sgf_controller_get_widget(self->sgf_controller);
  g_return_if_fail(sgf_widget != NULL);
  gtk_box_append(GTK_BOX(right_panel), sgf_widget);

  self->startup_force_source_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                                                   gcheckers_window_startup_force_move_cb,
                                                   g_object_ref(self),
                                                   (GDestroyNotify)g_object_unref);
}

GCheckersWindow *gcheckers_window_new(GtkApplication *app, GCheckersModel *model) {
  g_return_val_if_fail(GTK_IS_APPLICATION(app), NULL);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(model), NULL);

  GCheckersWindow *window = g_object_new(GCHECKERS_TYPE_WINDOW, "application", app, NULL);
  gcheckers_window_set_model(window, model);
  return window;
}
