#include "ai_alpha_beta.h"
#include "window.h"

#include "board_view.h"
#include "sgf_file_actions.h"
#include "sgf_controller.h"
#include "style.h"
#include "player_controls_panel.h"
#include "widget_utils.h"

#include <string.h>

struct _GCheckersWindow {
  GtkApplicationWindow parent_instance;
  GCheckersModel *model;
  GtkWidget *main_paned;
  GtkWidget *analyze_toggle_button;
  GtkTextBuffer *analysis_buffer;
  BoardView *board_view;
  PlayerControlsPanel *controls_panel;
  GtkDropDown *sgf_mode_control;
  GCheckersSgfController *sgf_controller;
  PlayerRuleset applied_ruleset;
  gulong state_handler_id;
  guint auto_move_source_id;
  guint paned_tick_id;
  guint analysis_ui_source_id;
  gint analysis_generation;
  GMutex analysis_report_mutex;
  char *analysis_report_text;
  gint analysis_report_generation;
};

G_DEFINE_TYPE(GCheckersWindow, gcheckers_window, GTK_TYPE_APPLICATION_WINDOW)

typedef struct {
  GCheckersWindow *self;
  Game game;
  gint generation;
  CheckersAiTranspositionTable *tt;
  guint current_depth;
  gint64 last_progress_publish_us;
  CheckersAiSearchStats current_stats;
  guint last_completed_depth;
  char *last_completed_text;
} GCheckersWindowAnalysisTask;

enum {
  GCHECKERS_WINDOW_ANALYSIS_UI_INTERVAL_MS = 100,
  GCHECKERS_WINDOW_ANALYSIS_TT_SIZE_MB = 64,
};

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

static void gcheckers_window_start_new_game(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));

  gcheckers_model_reset(self->model);
  board_view_clear_selection(self->board_view);
  gcheckers_sgf_controller_new_game(self->sgf_controller);
}

static gboolean gcheckers_window_apply_player_move(const CheckersMove *move, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self->sgf_controller), FALSE);

  if (!gcheckers_sgf_controller_apply_move(self->sgf_controller, move)) {
    return FALSE;
  }

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

  guint configured_depth = player_controls_panel_get_computer_depth(self->controls_panel);
  guint effective_depth = configured_depth == 0 ? 1 : configured_depth;
  return gcheckers_sgf_controller_step_ai_move(self->sgf_controller, effective_depth, move);
}

static CheckersRules gcheckers_window_rules_from_selection(PlayerRuleset ruleset) {
  if (ruleset == PLAYER_RULESET_INTERNATIONAL) {
    return game_rules_international_draughts();
  }

  return game_rules_american_checkers();
}

static void gcheckers_window_set_ruleset(GCheckersWindow *self, PlayerRuleset ruleset) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self->sgf_controller));

  if (ruleset != PLAYER_RULESET_AMERICAN && ruleset != PLAYER_RULESET_INTERNATIONAL) {
    g_debug("Unexpected ruleset value");
    return;
  }

  if (ruleset == self->applied_ruleset) {
    const GameState *state = gcheckers_model_peek_state(self->model);
    if (state != NULL) {
      guint expected_board_size = ruleset == PLAYER_RULESET_INTERNATIONAL ? 10u : 8u;
      if (state->board.board_size == expected_board_size) {
        return;
      }
    }
  }

  CheckersRules rules = gcheckers_window_rules_from_selection(ruleset);
  gcheckers_model_set_rules(self->model, &rules);
  board_view_clear_selection(self->board_view);
  gcheckers_sgf_controller_new_game(self->sgf_controller);
  self->applied_ruleset = ruleset;
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

static void gcheckers_window_analysis_publish(GCheckersWindow *self, gint generation, const char *text) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(text != NULL);

  g_mutex_lock(&self->analysis_report_mutex);
  if (generation >= self->analysis_report_generation) {
    g_free(self->analysis_report_text);
    self->analysis_report_text = g_strdup(text);
    self->analysis_report_generation = generation;
  }
  g_mutex_unlock(&self->analysis_report_mutex);
}

static gboolean gcheckers_window_analysis_flush_cb(gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), G_SOURCE_REMOVE);

  g_autofree char *text = NULL;
  gint generation = 0;

  g_mutex_lock(&self->analysis_report_mutex);
  if (self->analysis_report_text != NULL) {
    text = g_strdup(self->analysis_report_text);
    generation = self->analysis_report_generation;
    g_clear_pointer(&self->analysis_report_text, g_free);
  }
  g_mutex_unlock(&self->analysis_report_mutex);

  if (text != NULL && g_atomic_int_get(&self->analysis_generation) == generation) {
    gcheckers_window_set_analysis_text(self, text);
  }

  if (!self->analyze_toggle_button || !GTK_IS_TOGGLE_BUTTON(self->analyze_toggle_button)) {
    self->analysis_ui_source_id = 0;
    return G_SOURCE_REMOVE;
  }
  if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self->analyze_toggle_button))) {
    self->analysis_ui_source_id = 0;
    return G_SOURCE_REMOVE;
  }

  return G_SOURCE_CONTINUE;
}

static void gcheckers_window_start_analysis_flush_loop(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  if (self->analysis_ui_source_id != 0) {
    return;
  }

  self->analysis_ui_source_id = g_timeout_add_full(G_PRIORITY_DEFAULT,
                                                   GCHECKERS_WINDOW_ANALYSIS_UI_INTERVAL_MS,
                                                   gcheckers_window_analysis_flush_cb,
                                                   self,
                                                   NULL);
}

static char *gcheckers_window_analysis_format_depth(const CheckersScoredMoveList *moves,
                                                    guint depth,
                                                    guint64 nodes,
                                                    const CheckersAiSearchStats *stats) {
  g_return_val_if_fail(moves != NULL, NULL);
  g_return_val_if_fail(stats != NULL, NULL);

  GString *text = g_string_new(NULL);
  gdouble ratio = stats->tt_probes == 0 ? 0.0 : (100.0 * (gdouble)stats->tt_hits) / (gdouble)stats->tt_probes;
  g_string_append_printf(text, "Analysis depth: %u\n", depth);
  g_string_append_printf(text, "Nodes: %" G_GUINT64_FORMAT "\n", nodes);
  g_string_append_printf(text, "TT hits: %" G_GUINT64_FORMAT "\n", stats->tt_hits);
  g_string_append_printf(text, "TT probes: %" G_GUINT64_FORMAT "\n", stats->tt_probes);
  g_string_append_printf(text, "TT hit ratio: %.2f%%\n", ratio);
  g_string_append_printf(text, "TT cutoffs: %" G_GUINT64_FORMAT "\n", stats->tt_cutoffs);
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

static char *gcheckers_window_analysis_extract_scores(const char *report_text) {
  g_return_val_if_fail(report_text != NULL, NULL);

  const char *scores = strstr(report_text, "Best to worst:\n");
  if (scores == NULL) {
    return g_strdup("(scores unavailable)\n");
  }

  return g_strdup(scores);
}

static void gcheckers_window_analysis_append_tt_stats(GString *text, const CheckersAiSearchStats *stats) {
  g_return_if_fail(text != NULL);
  g_return_if_fail(stats != NULL);

  gdouble ratio = stats->tt_probes == 0 ? 0.0 : (100.0 * (gdouble)stats->tt_hits) / (gdouble)stats->tt_probes;
  g_string_append_printf(text, "TT hits: %" G_GUINT64_FORMAT "\n", stats->tt_hits);
  g_string_append_printf(text, "TT probes: %" G_GUINT64_FORMAT "\n", stats->tt_probes);
  g_string_append_printf(text, "TT hit ratio: %.2f%%\n", ratio);
  g_string_append_printf(text, "TT cutoffs: %" G_GUINT64_FORMAT "\n", stats->tt_cutoffs);
}

static char *gcheckers_window_analysis_format_progress(const GCheckersWindowAnalysisTask *task, guint64 nodes) {
  g_return_val_if_fail(task != NULL, NULL);

  GString *text = g_string_new(NULL);
  g_string_append_printf(text, "Analysis depth: %u (searching)\n", task->current_depth);
  g_string_append_printf(text, "Nodes: %" G_GUINT64_FORMAT "\n", nodes);
  gcheckers_window_analysis_append_tt_stats(text, &task->current_stats);

  if (task->last_completed_text == NULL) {
    g_string_append(text, "Best to worst:\n");
    g_string_append(text, "(searching...)\n");
    return g_string_free(text, FALSE);
  }

  g_autofree char *scores = gcheckers_window_analysis_extract_scores(task->last_completed_text);
  g_string_append_printf(text, "Last completed depth: %u\n", task->last_completed_depth);
  g_string_append(text, scores);
  return g_string_free(text, FALSE);
}

static void gcheckers_window_analysis_on_progress(guint64 nodes, gpointer user_data) {
  GCheckersWindowAnalysisTask *task = user_data;
  g_return_if_fail(task != NULL);
  g_return_if_fail(GCHECKERS_IS_WINDOW(task->self));

  const gint64 interval_us = (gint64)GCHECKERS_WINDOW_ANALYSIS_UI_INTERVAL_MS * 1000;
  gint64 now = g_get_monotonic_time();
  if (task->last_progress_publish_us != 0 && now - task->last_progress_publish_us < interval_us) {
    return;
  }

  task->last_progress_publish_us = now;
  g_autofree char *text = gcheckers_window_analysis_format_progress(task, nodes);
  if (text == NULL) {
    g_debug("Failed to format analysis progress text");
    return;
  }

  gcheckers_window_analysis_publish(task->self, task->generation, text);
}

static gpointer gcheckers_window_analysis_thread(gpointer user_data) {
  GCheckersWindowAnalysisTask *task = user_data;
  g_return_val_if_fail(task != NULL, NULL);

  guint depth = 8;
  while (!gcheckers_window_should_cancel_analysis(task)) {
    task->current_depth = depth;
    task->last_progress_publish_us = 0;
    memset(&task->current_stats, 0, sizeof(task->current_stats));
    gcheckers_window_analysis_on_progress(0, task);

    CheckersScoredMoveList moves = {0};
    guint64 nodes = 0;
    gboolean ok = checkers_ai_alpha_beta_analyze_moves_cancellable_with_tt(
        &task->game,
        depth,
        &moves,
        gcheckers_window_should_cancel_analysis,
        task,
        &nodes,
        gcheckers_window_analysis_on_progress,
        task,
        task->tt,
        &task->current_stats);
    if (!ok) {
      if (!gcheckers_window_should_cancel_analysis(task)) {
        gcheckers_window_analysis_publish(task->self, task->generation, "No legal moves to analyze.");
      }
      break;
    }

    g_autofree char *text = gcheckers_window_analysis_format_depth(&moves, depth, nodes, &task->current_stats);
    checkers_scored_move_list_free(&moves);
    if (!text) {
      break;
    }

    g_free(task->last_completed_text);
    task->last_completed_text = g_strdup(text);
    task->last_completed_depth = depth;
    gcheckers_window_analysis_publish(task->self, task->generation, text);
    if (depth == G_MAXUINT) {
      break;
    }
    depth++;
  }

  g_object_unref(task->self);
  checkers_ai_tt_free(task->tt);
  g_free(task->last_completed_text);
  g_free(task);
  return NULL;
}

static void gcheckers_window_stop_analysis(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  g_atomic_int_inc(&self->analysis_generation);
  g_clear_handle_id(&self->analysis_ui_source_id, g_source_remove);

  g_mutex_lock(&self->analysis_report_mutex);
  g_clear_pointer(&self->analysis_report_text, g_free);
  g_mutex_unlock(&self->analysis_report_mutex);
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
  gcheckers_window_start_analysis_flush_loop(self);

  GCheckersWindowAnalysisTask *task = g_new0(GCheckersWindowAnalysisTask, 1);
  task->self = g_object_ref(self);
  task->game = game;
  task->generation = generation;
  task->tt = checkers_ai_tt_new(GCHECKERS_WINDOW_ANALYSIS_TT_SIZE_MB);
  if (task->tt == NULL) {
    g_debug("Failed to allocate analysis TT, continuing without TT caching");
  }
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
}

static void gcheckers_window_update_status(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));

  board_view_update(self->board_view);
}

static gboolean gcheckers_window_auto_force_move_cb(gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), G_SOURCE_REMOVE);

  self->auto_move_source_id = 0;
  gcheckers_window_force_move(self);
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
}

static void gcheckers_window_on_sgf_rewind(GSimpleAction * /*action*/,
                                           GVariant * /*parameter*/,
                                           gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self->sgf_controller));

  if (!gcheckers_sgf_controller_rewind_to_start(self->sgf_controller)) {
    g_debug("SGF rewind ignored");
  }
}

static void gcheckers_window_on_sgf_step_backward(GSimpleAction * /*action*/,
                                                  GVariant * /*parameter*/,
                                                  gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self->sgf_controller));

  if (!gcheckers_sgf_controller_step_backward(self->sgf_controller)) {
    g_debug("SGF step backward ignored");
  }
}

static void gcheckers_window_on_sgf_step_forward(GSimpleAction * /*action*/,
                                                 GVariant * /*parameter*/,
                                                 gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self->sgf_controller));

  if (!gcheckers_sgf_controller_step_forward(self->sgf_controller)) {
    g_debug("SGF step forward ignored");
  }
}

static void gcheckers_window_on_sgf_step_forward_to_branch(GSimpleAction * /*action*/,
                                                           GVariant * /*parameter*/,
                                                           gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self->sgf_controller));

  if (!gcheckers_sgf_controller_step_forward_to_branch(self->sgf_controller)) {
    g_debug("SGF step forward to branch ignored");
  }
}

static void gcheckers_window_on_sgf_step_forward_to_end(GSimpleAction * /*action*/,
                                                        GVariant * /*parameter*/,
                                                        gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self->sgf_controller));

  if (!gcheckers_sgf_controller_step_forward_to_end(self->sgf_controller)) {
    g_debug("SGF step forward to end ignored");
  }
}

static GtkWidget *gcheckers_window_new_toolbar_action_button(const char *icon_name,
                                                             const char *tooltip_text,
                                                             const char *action_name) {
  g_return_val_if_fail(icon_name != NULL, NULL);
  g_return_val_if_fail(tooltip_text != NULL, NULL);
  g_return_val_if_fail(action_name != NULL, NULL);

  GtkWidget *button = gtk_button_new_from_icon_name(icon_name);
  gtk_widget_set_tooltip_text(button, tooltip_text);
  gtk_actionable_set_action_name(GTK_ACTIONABLE(button), action_name);
  return button;
}

void gcheckers_window_force_move(GCheckersWindow *self) {
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
  gcheckers_window_choose_computer_move(self, &move);
}

PlayerRuleset gcheckers_window_get_ruleset(GCheckersWindow *self) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), PLAYER_RULESET_INTERNATIONAL);

  return self->applied_ruleset;
}

void gcheckers_window_apply_new_game_settings(GCheckersWindow *self,
                                              PlayerRuleset ruleset,
                                              PlayerControlMode white_mode,
                                              PlayerControlMode black_mode,
                                              guint computer_depth) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(self->controls_panel != NULL);

  player_controls_panel_set_mode(self->controls_panel, CHECKERS_COLOR_WHITE, white_mode);
  player_controls_panel_set_mode(self->controls_panel, CHECKERS_COLOR_BLACK, black_mode);
  player_controls_panel_set_computer_depth(self->controls_panel, computer_depth);

  gcheckers_window_set_ruleset(self, ruleset);
  gcheckers_window_start_new_game(self);
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
  gcheckers_window_set_ruleset(self, self->applied_ruleset);
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
  g_clear_handle_id(&self->analysis_ui_source_id, g_source_remove);

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
  self->sgf_mode_control = NULL;
  G_OBJECT_CLASS(gcheckers_window_parent_class)->dispose(object);
}

static void gcheckers_window_finalize(GObject *object) {
  GCheckersWindow *self = GCHECKERS_WINDOW(object);

  g_clear_pointer(&self->analysis_report_text, g_free);
  g_mutex_clear(&self->analysis_report_mutex);

  G_OBJECT_CLASS(gcheckers_window_parent_class)->finalize(object);
}

static void gcheckers_window_class_init(GCheckersWindowClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = gcheckers_window_dispose;
  object_class->finalize = gcheckers_window_finalize;
}

static void gcheckers_window_init(GCheckersWindow *self) {
  self->auto_move_source_id = 0;
  self->paned_tick_id = 0;
  self->analysis_ui_source_id = 0;
  self->analysis_generation = 1;
  self->analysis_report_text = NULL;
  self->analysis_report_generation = 0;
  g_mutex_init(&self->analysis_report_mutex);
  self->applied_ruleset = PLAYER_RULESET_INTERNATIONAL;

  static const GActionEntry window_actions[] = {
      {
          .name = "sgf-rewind",
          .activate = gcheckers_window_on_sgf_rewind,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "sgf-step-backward",
          .activate = gcheckers_window_on_sgf_step_backward,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "sgf-step-forward",
          .activate = gcheckers_window_on_sgf_step_forward,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "sgf-step-forward-to-branch",
          .activate = gcheckers_window_on_sgf_step_forward_to_branch,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "sgf-step-forward-to-end",
          .activate = gcheckers_window_on_sgf_step_forward_to_end,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
  };
  g_action_map_add_action_entries(G_ACTION_MAP(self),
                                  window_actions,
                                  G_N_ELEMENTS(window_actions),
                                  self);
  gcheckers_window_install_sgf_file_actions(self);

  gtk_window_set_title(GTK_WINDOW(self), "gcheckers");
  gtk_window_set_default_size(GTK_WINDOW(self), 1100, 700);

  gcheckers_style_init();

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_window_set_child(GTK_WINDOW(self), content);

  GApplication *app = g_application_get_default();
  if (GTK_IS_APPLICATION(app)) {
    GMenuModel *menubar = gtk_application_get_menubar(GTK_APPLICATION(app));
    if (menubar != NULL) {
      GtkWidget *menu_bar = gtk_popover_menu_bar_new_from_model(menubar);
      gtk_box_append(GTK_BOX(content), menu_bar);
    }
  }

  GtkWidget *toolbar = gtk_action_bar_new();
  GtkWidget *new_game_button =
      gcheckers_window_new_toolbar_action_button("document-new-symbolic", "New game...", "app.new-game");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), new_game_button);

  GtkWidget *force_move_button =
      gcheckers_window_new_toolbar_action_button("media-playback-start-symbolic",
                                                 "Force move",
                                                 "app.force-move");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), force_move_button);

  GtkWidget *toolbar_separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), toolbar_separator);

  GtkWidget *rewind_button = gcheckers_window_new_toolbar_action_button("media-skip-backward-symbolic",
                                                                         "Rewind to start",
                                                                         "win.sgf-rewind");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), rewind_button);

  GtkWidget *step_backward_button =
      gcheckers_window_new_toolbar_action_button("go-previous-symbolic",
                                                 "Back one move",
                                                 "win.sgf-step-backward");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), step_backward_button);

  GtkWidget *step_forward_button = gcheckers_window_new_toolbar_action_button("go-next-symbolic",
                                                                               "Forward one move",
                                                                               "win.sgf-step-forward");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), step_forward_button);

  GtkWidget *step_to_branch_button =
      gcheckers_window_new_toolbar_action_button("media-seek-forward-symbolic",
                                                 "Forward to next branch point",
                                                 "win.sgf-step-forward-to-branch");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), step_to_branch_button);

  GtkWidget *step_to_end_button = gcheckers_window_new_toolbar_action_button("media-skip-forward-symbolic",
                                                                              "Forward to main line end",
                                                                              "win.sgf-step-forward-to-end");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), step_to_end_button);
  gtk_box_append(GTK_BOX(content), toolbar);

  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_hexpand(paned, TRUE);
  gtk_widget_set_vexpand(paned, TRUE);
  gtk_box_append(GTK_BOX(content), paned);
  self->main_paned = paned;
  self->paned_tick_id = gtk_widget_add_tick_callback(paned,
                                                      gcheckers_window_constrain_main_split_cb,
                                                      self,
                                                      NULL);

  GtkWidget *left_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(left_panel, TRUE);
  gtk_widget_set_vexpand(left_panel, TRUE);
  gtk_widget_set_margin_top(left_panel, 8);
  gtk_widget_set_margin_bottom(left_panel, 8);
  gtk_widget_set_margin_start(left_panel, 8);
  gtk_widget_set_margin_end(left_panel, 8);
  gtk_paned_set_start_child(GTK_PANED(paned), left_panel);
  gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);

  self->controls_panel = g_object_ref_sink(player_controls_panel_new());
  gtk_box_append(GTK_BOX(left_panel), GTK_WIDGET(self->controls_panel));
  g_signal_connect(self->controls_panel,
                   "control-changed",
                   G_CALLBACK(gcheckers_window_on_control_changed),
                   self);

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

  GtkWidget *middle_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(middle_panel, TRUE);
  gtk_widget_set_vexpand(middle_panel, TRUE);
  gtk_widget_set_margin_top(middle_panel, 8);
  gtk_widget_set_margin_bottom(middle_panel, 8);
  gtk_widget_set_margin_start(middle_panel, 8);
  gtk_widget_set_margin_end(middle_panel, 8);
  gtk_paned_set_start_child(GTK_PANED(right_split), middle_panel);
  gtk_paned_set_shrink_start_child(GTK_PANED(right_split), FALSE);

  GtkWidget *analysis_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(analysis_panel, TRUE);
  gtk_widget_set_vexpand(analysis_panel, TRUE);
  gtk_widget_set_margin_top(analysis_panel, 8);
  gtk_widget_set_margin_bottom(analysis_panel, 8);
  gtk_widget_set_margin_start(analysis_panel, 8);
  gtk_widget_set_margin_end(analysis_panel, 8);
  gtk_paned_set_end_child(GTK_PANED(right_split), analysis_panel);
  gtk_paned_set_shrink_end_child(GTK_PANED(right_split), FALSE);
  gtk_paned_set_position(GTK_PANED(paned), 500);
  gtk_paned_set_position(GTK_PANED(right_split), 300);

  GtkWidget *sgf_mode_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *sgf_mode_label = gtk_label_new("Mode");
  gtk_widget_set_halign(sgf_mode_label, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(sgf_mode_row), sgf_mode_label);
  self->sgf_mode_control = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(
      (const char *[]){"Play", "Edit", NULL}));
  gtk_drop_down_set_selected(self->sgf_mode_control, 0);
  gtk_box_append(GTK_BOX(sgf_mode_row), GTK_WIDGET(self->sgf_mode_control));
  gtk_box_append(GTK_BOX(middle_panel), sgf_mode_row);

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
