#include "ai_alpha_beta.h"
#include "gcheckers_window.h"

#include "board_view.h"
#include "gcheckers_sgf_controller.h"
#include "gcheckers_style.h"
#include "player_controls_panel.h"
#include "widget_utils.h"

struct _GCheckersWindow {
  GtkApplicationWindow parent_instance;
  GCheckersModel *model;
  GtkWidget *main_paned;
  GtkWidget *status_label;
  GtkWidget *new_game_button;
  GtkWidget *analyze_toggle_button;
  GtkTextBuffer *analysis_buffer;
  GtkWidget *controls_row;
  BoardView *board_view;
  PlayerControlsPanel *controls_panel;
  GCheckersSgfController *sgf_controller;
  gulong state_handler_id;
  guint auto_move_source_id;
  guint paned_tick_id;
  gint analysis_generation;
};

G_DEFINE_TYPE(GCheckersWindow, gcheckers_window, GTK_TYPE_APPLICATION_WINDOW)

static void gcheckers_window_on_force_move_requested(PlayerControlsPanel *panel, gpointer user_data);

typedef struct {
  GCheckersWindow *self;
  Game game;
  gint generation;
} GCheckersWindowAnalysisTask;

typedef struct {
  GCheckersWindow *self;
  gint generation;
  char *text;
} GCheckersWindowAnalysisUpdate;

static gboolean gcheckers_window_constrain_main_split_cb(GtkWidget * /*widget*/,
                                                         GdkFrameClock * /*frame_clock*/,
                                                         gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), G_SOURCE_CONTINUE);

  if (!self->main_paned || !GTK_IS_PANED(self->main_paned)) {
    return G_SOURCE_CONTINUE;
  }

  int height = gtk_widget_get_height(self->main_paned);
  int position = gtk_paned_get_position(GTK_PANED(self->main_paned));
  if (height > 0 && position > height) {
    gtk_paned_set_position(GTK_PANED(self->main_paned), height);
  }

  return G_SOURCE_CONTINUE;
}

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

static gboolean gcheckers_window_choose_computer_move(GCheckersWindow *self, CheckersMove *move) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(self->controls_panel != NULL, FALSE);
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self->sgf_controller), FALSE);

  PlayerComputerLevel level = player_controls_panel_get_computer_level(self->controls_panel);
  guint depth = 0;
  if (!player_controls_panel_computer_level_depth(level, &depth)) {
    g_debug("No computer depth for current level");
    return FALSE;
  }

  if (depth == 0) {
    return gcheckers_sgf_controller_step_random_move(self->sgf_controller, move);
  }

  return gcheckers_sgf_controller_step_ai_move(self->sgf_controller, depth, move);
}

static void gcheckers_window_set_analysis_text(GCheckersWindow *self, const char *text) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(text != NULL);

  if (!self->analysis_buffer) {
    g_debug("Missing analysis buffer");
    return;
  }

  gtk_text_buffer_set_text(self->analysis_buffer, text, -1);
}

static gboolean gcheckers_window_should_cancel_analysis(gpointer user_data) {
  GCheckersWindowAnalysisTask *task = user_data;
  g_return_val_if_fail(task != NULL, TRUE);
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(task->self), TRUE);

  return g_atomic_int_get(&task->self->analysis_generation) != task->generation;
}

static gboolean gcheckers_window_analysis_publish_cb(gpointer user_data) {
  GCheckersWindowAnalysisUpdate *update = user_data;
  g_return_val_if_fail(update != NULL, G_SOURCE_REMOVE);

  if (g_atomic_int_get(&update->self->analysis_generation) == update->generation) {
    gcheckers_window_set_analysis_text(update->self, update->text);
  }

  g_free(update->text);
  g_object_unref(update->self);
  g_free(update);
  return G_SOURCE_REMOVE;
}

static void gcheckers_window_analysis_publish(GCheckersWindow *self, gint generation, const char *text) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(text != NULL);

  GCheckersWindowAnalysisUpdate *update = g_new0(GCheckersWindowAnalysisUpdate, 1);
  update->self = g_object_ref(self);
  update->generation = generation;
  update->text = g_strdup(text);
  g_main_context_invoke(NULL, gcheckers_window_analysis_publish_cb, update);
}

static char *gcheckers_window_analysis_format_depth(const CheckersScoredMoveList *moves, guint depth) {
  g_return_val_if_fail(moves != NULL, NULL);

  GString *text = g_string_new(NULL);
  g_string_append_printf(text, "Analysis depth: %u\n", depth);
  g_string_append(text, "Best to worst:\n");
  for (size_t i = 0; i < moves->count; ++i) {
    char notation[128];
    if (!game_format_move_notation(&moves->moves[i].move, notation, sizeof(notation))) {
      g_strlcpy(notation, "?", sizeof(notation));
    }
    g_string_append_printf(text, "%zu. %s : %d\n", i + 1, notation, moves->moves[i].score);
  }

  return g_string_free(text, FALSE);
}

static gpointer gcheckers_window_analysis_thread(gpointer user_data) {
  GCheckersWindowAnalysisTask *task = user_data;
  g_return_val_if_fail(task != NULL, NULL);

  guint depth = 1;
  while (!gcheckers_window_should_cancel_analysis(task)) {
    CheckersScoredMoveList moves = {0};
    gboolean ok = checkers_ai_alpha_beta_analyze_moves_cancellable(&task->game,
                                                                   depth,
                                                                   &moves,
                                                                   gcheckers_window_should_cancel_analysis,
                                                                   task);
    if (!ok) {
      if (!gcheckers_window_should_cancel_analysis(task)) {
        gcheckers_window_analysis_publish(task->self, task->generation, "No legal moves to analyze.");
      }
      break;
    }

    g_autofree char *text = gcheckers_window_analysis_format_depth(&moves, depth);
    checkers_scored_move_list_free(&moves);
    if (!text) {
      break;
    }

    gcheckers_window_analysis_publish(task->self, task->generation, text);
    if (depth == G_MAXUINT) {
      break;
    }
    depth++;
  }

  g_object_unref(task->self);
  g_free(task);
  return NULL;
}

static void gcheckers_window_stop_analysis(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  g_atomic_int_inc(&self->analysis_generation);
}

static void gcheckers_window_start_analysis(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));

  Game game = {0};
  if (!gcheckers_model_copy_game(self->model, &game)) {
    g_debug("Failed to snapshot game for threaded analysis");
    return;
  }

  gint generation = g_atomic_int_add(&self->analysis_generation, 1) + 1;
  gcheckers_window_set_analysis_text(self, "Analyzing...");

  GCheckersWindowAnalysisTask *task = g_new0(GCheckersWindowAnalysisTask, 1);
  task->self = g_object_ref(self);
  task->game = game;
  task->generation = generation;
  GThread *thread = g_thread_new("analysis-thread", gcheckers_window_analysis_thread, task);
  g_thread_unref(thread);
}

static void gcheckers_window_restart_analysis_if_active(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  if (!self->analyze_toggle_button || !GTK_IS_TOGGLE_BUTTON(self->analyze_toggle_button)) {
    return;
  }

  if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self->analyze_toggle_button))) {
    return;
  }

  gcheckers_window_stop_analysis(self);
  gcheckers_window_start_analysis(self);
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
  gcheckers_window_restart_analysis_if_active(self);
}

static void gcheckers_window_on_new_game_clicked(GtkButton * /*button*/, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));

  gcheckers_model_reset(self->model);
  board_view_clear_selection(self->board_view);
  gcheckers_sgf_controller_new_game(self->sgf_controller);
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
  gcheckers_window_restart_analysis_if_active(self);
}

static void gcheckers_window_on_analyze_toggled(GtkToggleButton *button, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));
  g_return_if_fail(GTK_IS_TOGGLE_BUTTON(button));

  gboolean active = gtk_toggle_button_get_active(button);
  if (active) {
    gcheckers_window_start_analysis(self);
    return;
  }

  gcheckers_window_stop_analysis(self);
  gcheckers_window_set_analysis_text(self, "Analysis stopped.");
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
  if (gcheckers_window_choose_computer_move(self, &move)) {
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

  gcheckers_window_stop_analysis(self);
  if (self->paned_tick_id != 0 && self->main_paned) {
    gtk_widget_remove_tick_callback(self->main_paned, self->paned_tick_id);
    self->paned_tick_id = 0;
  }
  g_clear_object(&self->sgf_controller);
  if (panel_removed) {
    g_clear_object(&self->controls_panel);
  } else {
    self->controls_panel = NULL;
  }
  g_clear_object(&self->board_view);
  g_clear_object(&self->model);
  self->main_paned = NULL;
  self->analyze_toggle_button = NULL;
  self->analysis_buffer = NULL;
  self->controls_row = NULL;
  G_OBJECT_CLASS(gcheckers_window_parent_class)->dispose(object);
}

static void gcheckers_window_class_init(GCheckersWindowClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = gcheckers_window_dispose;
}

static void gcheckers_window_init(GCheckersWindow *self) {
  self->auto_move_source_id = 0;
  self->paned_tick_id = 0;
  self->analysis_generation = 1;

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
  self->main_paned = paned;
  self->paned_tick_id = gtk_widget_add_tick_callback(paned,
                                                      gcheckers_window_constrain_main_split_cb,
                                                      self,
                                                      NULL);

  GtkWidget *left_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_hexpand(left_panel, TRUE);
  gtk_widget_set_vexpand(left_panel, TRUE);
  gtk_paned_set_start_child(GTK_PANED(paned), left_panel);
  gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);

  self->board_view = board_view_new();
  GtkWidget *board_aspect = gtk_aspect_frame_new(0.5f, 0.5f, 1.0f, FALSE);
  gtk_widget_set_hexpand(board_aspect, TRUE);
  gtk_widget_set_vexpand(board_aspect, TRUE);
  gtk_box_append(GTK_BOX(left_panel), board_aspect);
  gtk_aspect_frame_set_child(GTK_ASPECT_FRAME(board_aspect), board_view_get_widget(self->board_view));

  GtkWidget *right_split = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_hexpand(right_split, TRUE);
  gtk_widget_set_vexpand(right_split, TRUE);
  gtk_paned_set_end_child(GTK_PANED(paned), right_split);
  gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);

  GtkWidget *middle_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_hexpand(middle_panel, TRUE);
  gtk_widget_set_vexpand(middle_panel, TRUE);
  gtk_paned_set_start_child(GTK_PANED(right_split), middle_panel);
  gtk_paned_set_shrink_start_child(GTK_PANED(right_split), FALSE);

  GtkWidget *analysis_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_hexpand(analysis_panel, TRUE);
  gtk_widget_set_vexpand(analysis_panel, TRUE);
  gtk_widget_set_size_request(analysis_panel, 400, -1);
  gtk_paned_set_end_child(GTK_PANED(right_split), analysis_panel);
  gtk_paned_set_shrink_end_child(GTK_PANED(right_split), FALSE);

  self->status_label = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(self->status_label), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(self->status_label), TRUE);
  gtk_box_append(GTK_BOX(middle_panel), self->status_label);

  self->controls_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_append(GTK_BOX(middle_panel), self->controls_row);

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

  self->new_game_button = gtk_button_new_with_label("New game");
  g_signal_connect(self->new_game_button,
                   "clicked",
                   G_CALLBACK(gcheckers_window_on_new_game_clicked),
                   self);
  gtk_box_append(GTK_BOX(self->controls_row), self->new_game_button);

  self->sgf_controller = gcheckers_sgf_controller_new(self->board_view);
  board_view_set_move_handler(self->board_view, gcheckers_window_apply_player_move, self);
  GtkWidget *sgf_widget = gcheckers_sgf_controller_get_widget(self->sgf_controller);
  g_return_if_fail(sgf_widget != NULL);
  g_signal_connect(self->sgf_controller,
                   "analysis-requested",
                   G_CALLBACK(gcheckers_window_on_analysis_requested),
                   self);
  gtk_widget_add_css_class(sgf_widget, "sgf-panel");
  gtk_box_append(GTK_BOX(middle_panel), sgf_widget);

  self->analyze_toggle_button = gtk_toggle_button_new_with_label("Analyze");
  g_signal_connect(self->analyze_toggle_button,
                   "toggled",
                   G_CALLBACK(gcheckers_window_on_analyze_toggled),
                   self);
  gtk_box_append(GTK_BOX(analysis_panel), self->analyze_toggle_button);

  GtkWidget *analysis_scroller = gtk_scrolled_window_new();
  gtk_widget_set_hexpand(analysis_scroller, TRUE);
  gtk_widget_set_vexpand(analysis_scroller, TRUE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(analysis_scroller),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
  gtk_box_append(GTK_BOX(analysis_panel), analysis_scroller);

  GtkWidget *analysis_view = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(analysis_view), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(analysis_view), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(analysis_view), GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_monospace(GTK_TEXT_VIEW(analysis_view), TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(analysis_scroller), analysis_view);
  self->analysis_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(analysis_view));
  gcheckers_window_set_analysis_text(self, "Toggle Analyze to start/stop iterative analysis.");
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
