#include "application.h"
#include "active_game_backend.h"
#include "game_app_profile.h"
#include "window.h"

#include "ai_search.h"
#include "app_paths.h"
#include "analysis_graph.h"
#include "board_view.h"
#include "puzzle_dialog.h"
#include "puzzle_catalog.h"
#include "games/checkers/rulesets.h"
#include "sgf_file_actions.h"
#include "sgf_controller.h"
#include "sgf_io.h"
#include "sgf_move_props.h"
#include "style.h"
#include "player_controls_panel.h"
#include "puzzle_progress.h"
#include "widget_utils.h"

#include <string.h>

typedef enum {
  GGAME_WINDOW_LAYOUT_MODE_NORMAL = 0,
  GGAME_WINDOW_LAYOUT_MODE_PUZZLE
} GGameWindowLayoutMode;

struct _GGameWindow {
  GtkApplicationWindow parent_instance;
  const GGameAppProfile *profile;
  GGameModel *game_model;
  GtkWidget *main_paned;
  GtkWidget *board_panel;
  GtkWidget *board_host_box;
  GtkWidget *board_host;
  GtkWidget *drawer_host;
  GtkWidget *drawer_split;
  GtkWidget *navigation_panel;
  GtkWidget *analysis_panel;
  GtkScale *analysis_depth_scale;
  GtkLabel *analysis_status_label;
  GtkTextBuffer *analysis_buffer;
  BoardView *board_view;
  PlayerControlsPanel *controls_panel;
  GtkDropDown *sgf_mode_control;
  GGameSgfController *sgf_controller;
  AnalysisGraph *analysis_graph;
  char *loaded_source_name;
  PlayerRuleset applied_ruleset;
  gulong state_handler_id;
  guint auto_move_source_id;
  guint paned_tick_id;
  gint analysis_mode;
  gint analysis_generation;
  GMutex analysis_report_mutex;
  GQueue *analysis_report_queue;
  guint analysis_expected_nodes;
  guint analysis_attached_nodes;
  guint analysis_processed_nodes;
  const SgfNode *analysis_last_updated_node;
  gboolean analysis_done_received;
  gboolean analysis_canceled;
  gboolean puzzle_mode;
  gboolean puzzle_finished;
  gboolean puzzle_feedback_locked;
  gboolean edit_mode_enabled;
  gboolean puzzle_saved_show_navigation_drawer;
  gboolean puzzle_saved_show_analysis_drawer;
  gboolean show_navigation_drawer;
  gboolean show_analysis_drawer;
  GGameWindowLayoutMode layout_mode;
  gboolean syncing_layout_default_size;
  gint board_panel_width;
  gint navigation_panel_width;
  gint analysis_panel_width;
  gint extra_width;
  gint puzzle_board_panel_width;
  gint puzzle_navigation_panel_width;
  gint puzzle_analysis_panel_width;
  gint puzzle_extra_width;
  GGameWindowBoardOrientationMode board_orientation_mode;
  CheckersColor board_bottom_color;
  PlayerRuleset puzzle_ruleset;
  CheckersColor puzzle_attacker;
  guint puzzle_number;
  guint puzzle_expected_step;
  guint puzzle_wrong_move_source_id;
  GArray *puzzle_steps;
  GtkWidget *puzzle_panel;
  GtkLabel *puzzle_message_label;
  GtkButton *puzzle_next_button;
  GtkButton *puzzle_analyze_button;
  GGamePuzzleProgressStore *puzzle_progress_store;
  gboolean puzzle_attempt_started;
  gboolean puzzle_attempt_made_player_move;
  GGamePuzzleAttemptRecord puzzle_attempt;
  char *puzzle_path;
};

G_DEFINE_TYPE(GGameWindow, ggame_window, GTK_TYPE_APPLICATION_WINDOW)

typedef struct {
  GGameWindow *self;
  const GameBackend *backend;
  guint8 *position;
  gint generation;
  GameAiTranspositionTable *tt;
  guint current_depth;
  guint target_depth;
  gint64 last_progress_publish_us;
  GameAiSearchStats cumulative_stats;
  guint last_completed_depth;
  SgfNodeAnalysis *last_completed_analysis;
  const SgfNode *target_node;
} GGameWindowAnalysisTask;

typedef enum {
  GGAME_WINDOW_ANALYSIS_MODE_NONE = 0,
  GGAME_WINDOW_ANALYSIS_MODE_CURRENT,
  GGAME_WINDOW_ANALYSIS_MODE_FULL_GAME
} GGameWindowAnalysisMode;

typedef struct {
  gint generation;
  GGameWindowAnalysisMode mode;
  gboolean done;
  gboolean canceled;
  gboolean is_payload;
  char *status_text;
  SgfNodeAnalysis *analysis;
  const SgfNode *node;
} GGameWindowAnalysisEvent;

typedef struct {
  const SgfNode *node;
} GGameWindowFullNodeJob;

typedef struct {
  CheckersColor color;
  CheckersMove move;
} GGameWindowPuzzleStep;

typedef struct {
  GGameWindow *self;
  gint generation;
  gboolean use_checkers_replay;
  const CheckersRules *checkers_rules;
  const GameBackend *backend;
  const GameBackendVariant *variant;
  guint depth;
  GameAiTranspositionTable *tt;
  GPtrArray *jobs;
  guint64 explored_nodes;
  guint current_job_index;
  gint64 last_progress_publish_us;
} GGameWindowFullAnalysisTask;

static void ggame_window_analysis_sync_ui(GGameWindow *self);
static void ggame_window_analysis_reset_runtime_state(GGameWindow *self);
static void ggame_window_analysis_begin_session(GGameWindow *self,
                                                    GGameWindowAnalysisMode mode,
                                                    guint expected_nodes);
static void ggame_window_analysis_finish_session(GGameWindow *self);
static gboolean ggame_window_is_edit_mode(GGameWindow *self);
static void ggame_window_set_action_enabled(GActionMap *map, const char *name, gboolean enabled);
static void ggame_window_sync_mode_ui(GGameWindow *self);
static void ggame_window_sync_drawer_ui(GGameWindow *self);
static void ggame_window_capture_panel_widths(GGameWindow *self);
static gint ggame_window_current_extra_width(GGameWindow *self);
static void ggame_window_apply_saved_panel_widths(GGameWindow *self);
static gint ggame_window_expected_default_width(GGameWindow *self);
static gboolean ggame_window_format_setup_point(uint8_t index, uint8_t board_size, char out_point[3]);
static gboolean ggame_window_update_node_setup_piece(SgfNode *node, const char *point, CheckersPiece piece);
static gboolean ggame_window_on_board_square_action(guint8 index, guint button, gpointer user_data);
static void ggame_window_sync_board_orientation(GGameWindow *self);
static void ggame_window_sync_puzzle_ui(GGameWindow *self);
static void ggame_window_leave_puzzle_mode(GGameWindow *self, gboolean restore_drawers);
static void ggame_window_sync_drawer_ui_with_capture(GGameWindow *self, gboolean capture_current_layout);
static void ggame_window_stop_analysis(GGameWindow *self);
static void ggame_window_sync_title(GGameWindow *self);
static void ggame_window_set_analysis_status(GGameWindow *self, const char *text);
static void ggame_window_show_analysis_for_current_node(GGameWindow *self);
static gboolean ggame_window_puzzle_attempt_ensure_started(GGameWindow *self);
static gboolean ggame_window_puzzle_attempt_finish_success(GGameWindow *self);
static gboolean ggame_window_puzzle_attempt_finish_failure(GGameWindow *self,
                                                               gboolean failure_on_first_move,
                                                               const CheckersMove *failed_first_move);
static gboolean ggame_window_puzzle_attempt_finish_analyze(GGameWindow *self);
static void ggame_window_puzzle_attempt_reset(GGameWindow *self);
static void ggame_window_start_opened_puzzle_attempt(GGameWindow *self);
static gboolean ggame_window_start_next_puzzle_mode_for_ruleset(GGameWindow *self, PlayerRuleset ruleset);
static char *ggame_window_analysis_format_complete(const SgfNodeAnalysis *analysis);
static void ggame_window_rebuild_board_host(GGameWindow *self);
static void ggame_window_sync_drawer_action_states(GGameWindow *self);

enum {
  GGAME_WINDOW_DEFAULT_BOARD_PANEL_WIDTH = 500,
  GGAME_WINDOW_DEFAULT_NAVIGATION_PANEL_WIDTH = 300,
  GGAME_WINDOW_DEFAULT_ANALYSIS_PANEL_WIDTH = 300,
  GGAME_WINDOW_DEFAULT_HEIGHT = 700,
  GGAME_WINDOW_ANALYSIS_PROGRESS_INTERVAL_MS = 100,
  GGAME_WINDOW_ANALYSIS_TT_SIZE_MB = 256,
  GGAME_WINDOW_ANALYSIS_DEPTH_MIN = 1,
  GGAME_WINDOW_ANALYSIS_DEPTH_MAX = 16,
  GGAME_WINDOW_ANALYSIS_DEPTH_DEFAULT = 8,
  GGAME_WINDOW_PUZZLE_WRONG_MOVE_DELAY_MS = 1000,
};

static void ggame_window_refresh_analysis_graph(GGameWindow *self);

static const GGameAppProfile *ggame_window_get_profile(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);

  if (self->profile == NULL) {
    g_debug("Missing active game application profile");
    return NULL;
  }

  return self->profile;
}

static GGameModel *ggame_window_get_game_model(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);

  if (self->game_model == NULL) {
    g_debug("Missing generic game model");
    return NULL;
  }

  return self->game_model;
}

static const GameBackend *ggame_window_get_game_backend(GGameWindow *self) {
  GGameModel *game_model = ggame_window_get_game_model(self);
  g_return_val_if_fail(game_model != NULL, NULL);

  const GameBackend *backend = ggame_model_peek_backend(game_model);
  if (backend == NULL) {
    g_debug("Missing active game backend");
    return NULL;
  }

  return backend;
}

static gconstpointer ggame_window_get_game_position(GGameWindow *self) {
  GGameModel *game_model = ggame_window_get_game_model(self);
  g_return_val_if_fail(game_model != NULL, NULL);

  gconstpointer position = ggame_model_peek_position(game_model);
  if (position == NULL) {
    g_debug("Missing active game position");
    return NULL;
  }

  return position;
}

static const Game *ggame_window_get_checkers_game(GGameWindow *self) {
  const GGameAppProfile *profile = ggame_window_get_profile(self);
  gconstpointer position = NULL;

  g_return_val_if_fail(profile != NULL, NULL);
  if (profile->kind != GGAME_APP_KIND_CHECKERS) {
    return NULL;
  }

  position = ggame_window_get_game_position(self);
  g_return_val_if_fail(position != NULL, NULL);
  return position;
}

static const GameState *ggame_window_get_checkers_state(GGameWindow *self) {
  const Game *game = ggame_window_get_checkers_game(self);

  if (game == NULL) {
    return NULL;
  }

  return &game->state;
}

static gboolean ggame_window_copy_analysis_position(GGameWindow *self,
                                                    const GameBackend **out_backend,
                                                    const GameBackendVariant **out_variant,
                                                    guint8 **out_position) {
  const GameBackend *backend = NULL;
  const GameBackendVariant *variant = NULL;
  gconstpointer position = NULL;
  guint8 *copy = NULL;

  g_return_val_if_fail(out_backend != NULL, FALSE);
  g_return_val_if_fail(out_position != NULL, FALSE);

  backend = ggame_window_get_game_backend(self);
  position = ggame_window_get_game_position(self);
  variant = ggame_window_get_variant(self);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(backend->position_size > 0, FALSE);
  g_return_val_if_fail(backend->position_init != NULL, FALSE);
  g_return_val_if_fail(backend->position_clear != NULL, FALSE);
  g_return_val_if_fail(backend->position_copy != NULL, FALSE);

  if (backend->variant_count > 0 && variant == NULL) {
    g_debug("Missing active backend variant while copying analysis position");
    return FALSE;
  }

  copy = g_malloc0(backend->position_size);
  if (copy == NULL) {
    g_debug("Failed to allocate analysis position snapshot");
    return FALSE;
  }

  backend->position_init(copy, variant);
  backend->position_copy(copy, position);

  *out_backend = backend;
  if (out_variant != NULL) {
    *out_variant = variant;
  }
  *out_position = copy;
  return TRUE;
}

static gboolean ggame_window_moves_equal(const CheckersMove *left, const CheckersMove *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  if (left->length != right->length || left->captures != right->captures) {
    return FALSE;
  }
  if (left->length == 0) {
    return TRUE;
  }

  return memcmp(left->path, right->path, left->length * sizeof(left->path[0])) == 0;
}

static const char *ggame_window_color_name(CheckersColor color) {
  const GameBackend *backend = ggame_active_app_profile()->backend;
  g_return_val_if_fail(backend != NULL, NULL);
  g_return_val_if_fail(backend->side_label != NULL, NULL);

  return backend->side_label(color == CHECKERS_COLOR_BLACK ? 1u : 0u);
}

static void ggame_window_sync_side_labels(GGameWindow *self) {
  const GGameAppProfile *profile = ggame_window_get_profile(self);
  const GameBackend *backend = profile != NULL ? profile->backend : NULL;
  const char *side0_label = NULL;
  const char *side1_label = NULL;

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(self->controls_panel != NULL);
  g_return_if_fail(backend != NULL);
  g_return_if_fail(backend->side_label != NULL);

  side0_label = backend->side_label(0);
  side1_label = backend->side_label(1);
  player_controls_panel_set_side_labels(self->controls_panel, side0_label, side1_label);
}

static GGameApplication *ggame_window_get_application_instance(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);

  GtkApplication *app = gtk_window_get_application(GTK_WINDOW(self));
  if (!GGAME_IS_APPLICATION(app)) {
    return NULL;
  }

  return GGAME_APPLICATION(app);
}

static void ggame_window_request_puzzle_progress_flush(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  GGameApplication *app = ggame_window_get_application_instance(self);
  if (app == NULL) {
    return;
  }

  ggame_application_request_puzzle_progress_flush(app);
}

static gboolean ggame_window_puzzle_attempt_is_terminal(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  if (!self->puzzle_attempt_started) {
    return FALSE;
  }

  return ggame_puzzle_attempt_record_is_resolved(&self->puzzle_attempt);
}

static gboolean ggame_window_puzzle_attempt_store_update(GGameWindow *self, gboolean append_record) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  if (self->puzzle_progress_store == NULL) {
    return FALSE;
  }

  g_autoptr(GError) error = NULL;
  gboolean ok = append_record
                    ? ggame_puzzle_progress_store_append_attempt(self->puzzle_progress_store,
                                                                    &self->puzzle_attempt,
                                                                    &error)
                    : ggame_puzzle_progress_store_replace_attempt(self->puzzle_progress_store,
                                                                     &self->puzzle_attempt,
                                                                     &error);
  if (!ok) {
    g_debug("Failed to persist puzzle attempt: %s", error != NULL ? error->message : "unknown error");
  }
  return ok;
}

static gboolean ggame_window_puzzle_attempt_ensure_started(GGameWindow *self) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(backend->id != NULL, FALSE);

  if (self->puzzle_attempt_started) {
    return TRUE;
  }
  if (self->puzzle_progress_store == NULL) {
    g_debug("Puzzle progress storage unavailable; attempt will not be persisted");
    return FALSE;
  }
  if (self->puzzle_path == NULL || self->puzzle_path[0] == '\0') {
    g_debug("Cannot start puzzle attempt without a puzzle path");
    return FALSE;
  }

  const char *ruleset_short_name = checkers_ruleset_short_name(self->puzzle_ruleset);
  self->puzzle_attempt = (GGamePuzzleAttemptRecord){
      .attempt_id = g_uuid_string_random(),
      .puzzle_number = self->puzzle_number,
      .puzzle_source_name = g_path_get_basename(self->puzzle_path),
      .puzzle_variant = g_strdup(ruleset_short_name),
      .attacker_side = self->puzzle_attacker == CHECKERS_COLOR_BLACK ? 1u : 0u,
      .started_unix_ms = g_get_real_time() / 1000,
      .finished_unix_ms = 0,
      .result = GGAME_PUZZLE_ATTEMPT_RESULT_UNRESOLVED,
      .failure_on_first_move = FALSE,
      .has_failed_first_move = FALSE,
      .first_reported_unix_ms = 0,
      .report_count = 0,
  };

  g_autofree char *basename = g_path_get_basename(self->puzzle_path);
  if (basename == NULL || ruleset_short_name == NULL) {
    ggame_window_puzzle_attempt_reset(self);
    return FALSE;
  }
  self->puzzle_attempt.puzzle_id = g_strdup_printf("%s/%s/%s", backend->id, ruleset_short_name, basename);

  if (!ggame_window_puzzle_attempt_store_update(self, TRUE)) {
    ggame_window_puzzle_attempt_reset(self);
    return FALSE;
  }

  self->puzzle_attempt_started = TRUE;
  self->puzzle_attempt_made_player_move = FALSE;
  return TRUE;
}

static gboolean ggame_window_puzzle_attempt_finish_success(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  if (!self->puzzle_attempt_started || ggame_window_puzzle_attempt_is_terminal(self)) {
    return FALSE;
  }

  self->puzzle_attempt.finished_unix_ms = g_get_real_time() / 1000;
  self->puzzle_attempt.result = GGAME_PUZZLE_ATTEMPT_RESULT_SUCCESS;
  if (!ggame_window_puzzle_attempt_store_update(self, FALSE)) {
    return FALSE;
  }

  ggame_window_request_puzzle_progress_flush(self);
  return TRUE;
}

static gboolean ggame_window_puzzle_attempt_finish_failure(GGameWindow *self,
                                                               gboolean failure_on_first_move,
                                                               const CheckersMove *failed_first_move) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  if (!self->puzzle_attempt_started || ggame_window_puzzle_attempt_is_terminal(self)) {
    return FALSE;
  }

  self->puzzle_attempt.finished_unix_ms = g_get_real_time() / 1000;
  self->puzzle_attempt.result = GGAME_PUZZLE_ATTEMPT_RESULT_FAILURE;
  self->puzzle_attempt.failure_on_first_move = failure_on_first_move;
  self->puzzle_attempt.has_failed_first_move = failure_on_first_move && failed_first_move != NULL;
  if (self->puzzle_attempt.has_failed_first_move) {
    char notation[128] = {0};
    if (!sgf_move_props_format_notation(failed_first_move, notation, sizeof(notation), NULL)) {
      g_debug("Failed to format failed first move for puzzle progress");
      self->puzzle_attempt.has_failed_first_move = FALSE;
    } else {
      g_free(self->puzzle_attempt.failed_first_move_text);
      self->puzzle_attempt.failed_first_move_text = g_strdup(notation);
    }
  }
  if (!ggame_window_puzzle_attempt_store_update(self, FALSE)) {
    return FALSE;
  }

  ggame_window_request_puzzle_progress_flush(self);
  return TRUE;
}

static gboolean ggame_window_puzzle_attempt_finish_analyze(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  if (!self->puzzle_attempt_started || ggame_window_puzzle_attempt_is_terminal(self)) {
    return FALSE;
  }

  self->puzzle_attempt.finished_unix_ms = g_get_real_time() / 1000;
  self->puzzle_attempt.result = GGAME_PUZZLE_ATTEMPT_RESULT_ANALYZE;
  if (!ggame_window_puzzle_attempt_store_update(self, FALSE)) {
    return FALSE;
  }

  ggame_window_request_puzzle_progress_flush(self);
  return TRUE;
}

static void ggame_window_puzzle_attempt_reset(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (!self->puzzle_attempt_started && self->puzzle_attempt.attempt_id == NULL &&
      self->puzzle_attempt.puzzle_id == NULL && self->puzzle_attempt.puzzle_source_name == NULL) {
    return;
  }

  self->puzzle_attempt_started = FALSE;
  self->puzzle_attempt_made_player_move = FALSE;
  ggame_puzzle_attempt_record_clear(&self->puzzle_attempt);
}

static gboolean ggame_window_analysis_depth_valid(guint depth) {
  return depth >= GGAME_WINDOW_ANALYSIS_DEPTH_MIN && depth <= GGAME_WINDOW_ANALYSIS_DEPTH_MAX;
}

static gboolean ggame_window_layout_mode_valid(GGameWindowLayoutMode mode) {
  return mode == GGAME_WINDOW_LAYOUT_MODE_NORMAL || mode == GGAME_WINDOW_LAYOUT_MODE_PUZZLE;
}

static gint *ggame_window_saved_board_panel_width_ptr(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);
  g_return_val_if_fail(ggame_window_layout_mode_valid(self->layout_mode), NULL);

  return self->layout_mode == GGAME_WINDOW_LAYOUT_MODE_PUZZLE ? &self->puzzle_board_panel_width
                                                                  : &self->board_panel_width;
}

static gint *ggame_window_saved_navigation_panel_width_ptr(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);
  g_return_val_if_fail(ggame_window_layout_mode_valid(self->layout_mode), NULL);

  return self->layout_mode == GGAME_WINDOW_LAYOUT_MODE_PUZZLE ? &self->puzzle_navigation_panel_width
                                                                  : &self->navigation_panel_width;
}

static gint *ggame_window_saved_analysis_panel_width_ptr(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);
  g_return_val_if_fail(ggame_window_layout_mode_valid(self->layout_mode), NULL);

  return self->layout_mode == GGAME_WINDOW_LAYOUT_MODE_PUZZLE ? &self->puzzle_analysis_panel_width
                                                                  : &self->analysis_panel_width;
}

static gint *ggame_window_saved_extra_width_ptr(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);
  g_return_val_if_fail(ggame_window_layout_mode_valid(self->layout_mode), NULL);

  return self->layout_mode == GGAME_WINDOW_LAYOUT_MODE_PUZZLE ? &self->puzzle_extra_width : &self->extra_width;
}

static gboolean ggame_window_board_orientation_mode_valid(GGameWindowBoardOrientationMode mode) {
  return mode == GGAME_WINDOW_BOARD_ORIENTATION_FIXED ||
         mode == GGAME_WINDOW_BOARD_ORIENTATION_FOLLOW_PLAYER ||
         mode == GGAME_WINDOW_BOARD_ORIENTATION_FOLLOW_TURN;
}

static gboolean ggame_window_try_resolve_follow_player_bottom_color(GGameWindow *self,
                                                                        CheckersColor *out_color) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(out_color != NULL, FALSE);
  g_return_val_if_fail(self->controls_panel != NULL, FALSE);

  gboolean white_is_user = player_controls_panel_is_user_control(self->controls_panel, 0);
  gboolean black_is_user = player_controls_panel_is_user_control(self->controls_panel, 1);
  if (white_is_user == black_is_user) {
    return FALSE;
  }

  *out_color = white_is_user ? CHECKERS_COLOR_WHITE : CHECKERS_COLOR_BLACK;
  return TRUE;
}

static CheckersColor ggame_window_resolve_board_bottom_color(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), CHECKERS_COLOR_WHITE);

  switch (self->board_orientation_mode) {
    case GGAME_WINDOW_BOARD_ORIENTATION_FIXED:
      return self->board_bottom_color;
    case GGAME_WINDOW_BOARD_ORIENTATION_FOLLOW_PLAYER: {
      CheckersColor bottom_color = self->board_bottom_color;
      if (ggame_window_try_resolve_follow_player_bottom_color(self, &bottom_color)) {
        return bottom_color;
      }
      return self->board_bottom_color;
    }
    case GGAME_WINDOW_BOARD_ORIENTATION_FOLLOW_TURN: {
      const GameBackend *backend = ggame_window_get_game_backend(self);
      gconstpointer position = ggame_window_get_game_position(self);

      if (backend != NULL && position != NULL && backend->position_turn != NULL) {
        guint side = backend->position_turn(position);
        return side == 0 ? CHECKERS_COLOR_WHITE : CHECKERS_COLOR_BLACK;
      }
      return self->board_bottom_color;
    }
    default:
      g_debug("Unexpected board orientation mode %d", self->board_orientation_mode);
      return self->board_bottom_color;
  }
}

static void ggame_window_sync_board_orientation(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(self->board_view != NULL);

  CheckersColor bottom_color = ggame_window_resolve_board_bottom_color(self);
  self->board_bottom_color = bottom_color;
  board_view_set_bottom_side(self->board_view, bottom_color);
}

static gboolean ggame_window_puzzle_name_matches(const char *name) {
  g_return_val_if_fail(name != NULL, FALSE);

  return (g_str_has_prefix(name, "puzzle-") || g_str_has_prefix(name, "puzzles-")) &&
         g_str_has_suffix(name, ".sgf");
}

static gboolean ggame_window_parse_puzzle_number_from_path(const char *path, guint *out_number) {
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(out_number != NULL, FALSE);

  g_autofree char *name = g_path_get_basename(path);
  g_return_val_if_fail(name != NULL, FALSE);

  if (!ggame_window_puzzle_name_matches(name)) {
    return FALSE;
  }

  const char *dash = strrchr(name, '-');
  const char *dot = strrchr(name, '.');
  if (dash == NULL || dot == NULL || dot <= dash + 1) {
    return FALSE;
  }

  g_autofree char *number_text = g_strndup(dash + 1, (gsize)(dot - dash - 1));
  char *end_ptr = NULL;
  guint64 number = g_ascii_strtoull(number_text, &end_ptr, 10);
  if (end_ptr == number_text || (end_ptr != NULL && *end_ptr != '\0') || number > G_MAXUINT) {
    return FALSE;
  }

  *out_number = (guint)number;
  return TRUE;
}

static gboolean ggame_window_sgf_color_to_checkers(SgfColor color, CheckersColor *out_color) {
  g_return_val_if_fail(out_color != NULL, FALSE);

  if (color == SGF_COLOR_BLACK) {
    *out_color = CHECKERS_COLOR_BLACK;
    return TRUE;
  }
  if (color == SGF_COLOR_WHITE) {
    *out_color = CHECKERS_COLOR_WHITE;
    return TRUE;
  }
  return FALSE;
}

static gboolean ggame_window_load_puzzle_steps_from_tree(SgfTree *tree, GArray *out_steps) {
  g_return_val_if_fail(SGF_IS_TREE(tree), FALSE);
  g_return_val_if_fail(out_steps != NULL, FALSE);

  const SgfNode *node = sgf_tree_get_root(tree);
  g_return_val_if_fail(node != NULL, FALSE);

  while (TRUE) {
    const GPtrArray *children = sgf_node_get_children(node);
    if (children == NULL || children->len == 0) {
      return out_steps->len > 0;
    }

    node = g_ptr_array_index((GPtrArray *)children, 0);
    g_return_val_if_fail(node != NULL, FALSE);

    SgfColor sgf_color = SGF_COLOR_NONE;
    CheckersMove move = {0};
    gboolean has_move = FALSE;
    g_autoptr(GError) error = NULL;
    if (!sgf_move_props_try_parse_node(node, &sgf_color, &move, &has_move, &error)) {
      g_debug("Failed to parse puzzle node move: %s", error != NULL ? error->message : "unknown error");
      return FALSE;
    }
    if (!has_move) {
      g_debug("Puzzle main line node was missing a move");
      return FALSE;
    }

    CheckersColor color = CHECKERS_COLOR_WHITE;
    if (!ggame_window_sgf_color_to_checkers(sgf_color, &color)) {
      g_debug("Puzzle main line node had unexpected color");
      return FALSE;
    }

    GGameWindowPuzzleStep step = {
      .color = color,
      .move = move,
    };
    g_array_append_val(out_steps, step);
  }
}

static void ggame_window_set_puzzle_message(GGameWindow *self, const char *message) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(message != NULL);

  if (self->puzzle_message_label != NULL) {
    gtk_label_set_text(self->puzzle_message_label, message);
  }
}

static void ggame_window_set_default_puzzle_message(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  g_autofree char *message =
      g_strdup_printf("Puzzle %04u. Find the best sequence for %s.",
                      self->puzzle_number,
                      ggame_window_color_name(self->puzzle_attacker));
  ggame_window_set_puzzle_message(self, message);
}

static void ggame_window_clear_board_banner(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(self->board_view != NULL);

  board_view_set_banner_text(self->board_view, NULL);
}

static gboolean ggame_window_constrain_main_split_cb(GtkWidget * /*widget*/,
                                                         GdkFrameClock * /*frame_clock*/,
                                                         gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_val_if_fail(GGAME_IS_WINDOW(self), G_SOURCE_CONTINUE);

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

static void ggame_window_analysis_sync_ui(GGameWindow *self) {
  const GGameAppProfile *profile = ggame_window_get_profile(self);
  gboolean analysis_supported = profile != NULL && profile->features.supports_analysis;

  g_return_if_fail(GGAME_IS_WINDOW(self));

  gboolean full_game_active = self->analysis_mode == GGAME_WINDOW_ANALYSIS_MODE_FULL_GAME;
  GAction *action = g_action_map_lookup_action(G_ACTION_MAP(self), "analysis-whole-game");
  if (action != NULL && G_IS_SIMPLE_ACTION(action)) {
    g_simple_action_set_enabled(G_SIMPLE_ACTION(action),
                                analysis_supported && !full_game_active && !self->puzzle_mode);
  }

  action = g_action_map_lookup_action(G_ACTION_MAP(self), "analysis-current-position");
  if (action != NULL && G_IS_SIMPLE_ACTION(action)) {
    g_simple_action_set_enabled(G_SIMPLE_ACTION(action), analysis_supported && !self->puzzle_mode);
  }

  if (!full_game_active && self->analysis_graph != NULL) {
    analysis_graph_clear_progress_node(self->analysis_graph);
  }
}

static void ggame_window_sync_drawer_action_states(GGameWindow *self) {
  GAction *action = NULL;

  g_return_if_fail(GGAME_IS_WINDOW(self));

  action = g_action_map_lookup_action(G_ACTION_MAP(self), "view-show-navigation-drawer");
  if (G_IS_SIMPLE_ACTION(action)) {
    g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(self->show_navigation_drawer));
  }

  action = g_action_map_lookup_action(G_ACTION_MAP(self), "view-show-analysis-drawer");
  if (G_IS_SIMPLE_ACTION(action)) {
    g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(self->show_analysis_drawer));
  }
}

static void ggame_window_sync_title(GGameWindow *self) {
  const GGameAppProfile *profile = ggame_window_get_profile(self);
  const char *window_title_name = profile != NULL ? profile->window_title_name : "ggame";

  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (self->loaded_source_name == NULL || *self->loaded_source_name == '\0') {
    gtk_window_set_title(GTK_WINDOW(self), window_title_name);
    return;
  }

  g_autofree char *title = g_strdup_printf("%s - %s", window_title_name, self->loaded_source_name);
  gtk_window_set_title(GTK_WINDOW(self), title);
}

static void ggame_window_capture_panel_widths(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  gint *board_panel_width = ggame_window_saved_board_panel_width_ptr(self);
  gint *navigation_panel_width = ggame_window_saved_navigation_panel_width_ptr(self);
  gint *analysis_panel_width = ggame_window_saved_analysis_panel_width_ptr(self);
  g_return_if_fail(board_panel_width != NULL);
  g_return_if_fail(navigation_panel_width != NULL);
  g_return_if_fail(analysis_panel_width != NULL);

  if (self->board_panel != NULL && gtk_widget_get_visible(self->board_panel)) {
    gint width = gtk_widget_get_width(self->board_panel);
    if (width > 0) {
      *board_panel_width = width;
    }
  }

  if (self->navigation_panel != NULL && gtk_widget_get_parent(self->navigation_panel) != NULL) {
    gint width = gtk_widget_get_width(self->navigation_panel);
    if (width > 0) {
      *navigation_panel_width = width;
    }
  }

  if (self->analysis_panel != NULL && gtk_widget_get_parent(self->analysis_panel) != NULL) {
    gint width = gtk_widget_get_width(self->analysis_panel);
    if (width > 0) {
      *analysis_panel_width = width;
    }
  }

  gint *extra_width = ggame_window_saved_extra_width_ptr(self);
  g_return_if_fail(extra_width != NULL);
  *extra_width = ggame_window_current_extra_width(self);
}

static gint ggame_window_current_extra_width(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), 0);

  gint window_width = gtk_widget_get_width(GTK_WIDGET(self));
  if (window_width <= 0) {
    return 0;
  }

  gint panel_width = 0;
  if (self->board_panel != NULL) {
    panel_width += MAX(0, gtk_widget_get_width(self->board_panel));
  }
  if (self->navigation_panel != NULL && gtk_widget_get_parent(self->navigation_panel) != NULL) {
    panel_width += MAX(0, gtk_widget_get_width(self->navigation_panel));
  }
  if (self->analysis_panel != NULL && gtk_widget_get_parent(self->analysis_panel) != NULL) {
    panel_width += MAX(0, gtk_widget_get_width(self->analysis_panel));
  }

  return MAX(0, window_width - panel_width);
}

static gint ggame_window_expected_default_width(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), GGAME_WINDOW_DEFAULT_BOARD_PANEL_WIDTH);

  gint *board_panel_width = ggame_window_saved_board_panel_width_ptr(self);
  gint *navigation_panel_width = ggame_window_saved_navigation_panel_width_ptr(self);
  gint *analysis_panel_width = ggame_window_saved_analysis_panel_width_ptr(self);
  gint *extra_width = ggame_window_saved_extra_width_ptr(self);
  g_return_val_if_fail(board_panel_width != NULL, GGAME_WINDOW_DEFAULT_BOARD_PANEL_WIDTH);
  g_return_val_if_fail(navigation_panel_width != NULL, GGAME_WINDOW_DEFAULT_NAVIGATION_PANEL_WIDTH);
  g_return_val_if_fail(analysis_panel_width != NULL, GGAME_WINDOW_DEFAULT_ANALYSIS_PANEL_WIDTH);
  g_return_val_if_fail(extra_width != NULL, 0);

  gint drawer_width = 0;
  if (self->show_navigation_drawer) {
    drawer_width += MAX(1, *navigation_panel_width);
  }
  if (self->show_analysis_drawer) {
    drawer_width += MAX(1, *analysis_panel_width);
  }

  return MAX(1, *board_panel_width) + drawer_width + MAX(0, *extra_width);
}

static void ggame_window_apply_saved_panel_widths(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  gint *board_panel_width = ggame_window_saved_board_panel_width_ptr(self);
  gint *navigation_panel_width = ggame_window_saved_navigation_panel_width_ptr(self);
  gint *analysis_panel_width = ggame_window_saved_analysis_panel_width_ptr(self);
  gint *extra_width = ggame_window_saved_extra_width_ptr(self);
  g_return_if_fail(board_panel_width != NULL);
  g_return_if_fail(navigation_panel_width != NULL);
  g_return_if_fail(analysis_panel_width != NULL);
  g_return_if_fail(extra_width != NULL);

  gint board_width = MAX(1, *board_panel_width);
  gint navigation_width = MAX(1, *navigation_panel_width);
  gint analysis_width = MAX(1, *analysis_panel_width);
  gint drawer_width = 0;

  if (self->show_navigation_drawer) {
    drawer_width += navigation_width;
  }
  if (self->show_analysis_drawer) {
    drawer_width += analysis_width;
  }

  gint current_height = gtk_widget_get_height(GTK_WIDGET(self));
  if (current_height <= 0) {
    current_height = GGAME_WINDOW_DEFAULT_HEIGHT;
  }

  self->syncing_layout_default_size = TRUE;
  gtk_window_set_default_size(GTK_WINDOW(self), board_width + drawer_width + MAX(0, *extra_width), current_height);
  self->syncing_layout_default_size = FALSE;

  if (self->main_paned != NULL && (self->show_navigation_drawer || self->show_analysis_drawer)) {
    gtk_paned_set_position(GTK_PANED(self->main_paned), board_width);
  }
  if (self->drawer_split != NULL && self->show_navigation_drawer && self->show_analysis_drawer) {
    gtk_paned_set_position(GTK_PANED(self->drawer_split), navigation_width);
  }
  if (self->board_panel != NULL) {
    gtk_widget_set_size_request(self->board_panel, board_width, -1);
  }
  if (self->drawer_host != NULL) {
    gtk_widget_set_size_request(self->drawer_host,
                                self->show_navigation_drawer || self->show_analysis_drawer ? drawer_width : 0,
                                -1);
  }
  if (self->drawer_split != NULL) {
    gtk_widget_set_size_request(self->drawer_split,
                                self->show_navigation_drawer && self->show_analysis_drawer ? drawer_width : -1,
                                -1);
  }
}

static void ggame_window_sync_drawer_ui(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  ggame_window_sync_drawer_ui_with_capture(self, TRUE);
}

static void ggame_window_sync_drawer_ui_with_capture(GGameWindow *self, gboolean capture_current_layout) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (capture_current_layout) {
    ggame_window_capture_panel_widths(self);
  }

  if (self->navigation_panel != NULL) {
    ggame_widget_remove_from_parent(self->navigation_panel);
  }
  if (self->analysis_panel != NULL) {
    ggame_widget_remove_from_parent(self->analysis_panel);
  }
  if (self->drawer_host != NULL) {
    ggame_widget_remove_from_parent(self->drawer_host);
  }
  if (self->drawer_split != NULL) {
    ggame_widget_remove_from_parent(self->drawer_split);
  }

  if (self->show_navigation_drawer && self->show_analysis_drawer) {
    g_return_if_fail(self->drawer_host != NULL);
    g_return_if_fail(self->drawer_split != NULL);
    g_return_if_fail(self->navigation_panel != NULL);
    g_return_if_fail(self->analysis_panel != NULL);
    gtk_paned_set_start_child(GTK_PANED(self->drawer_split), self->navigation_panel);
    gtk_paned_set_end_child(GTK_PANED(self->drawer_split), self->analysis_panel);
    gtk_box_append(GTK_BOX(self->drawer_host), self->drawer_split);
    gtk_widget_set_visible(self->navigation_panel, TRUE);
    gtk_widget_set_visible(self->analysis_panel, TRUE);
    gtk_widget_set_visible(self->drawer_split, TRUE);
    gtk_widget_set_visible(self->drawer_host, TRUE);
    gtk_paned_set_end_child(GTK_PANED(self->main_paned), self->drawer_host);
  } else if (self->show_navigation_drawer) {
    g_return_if_fail(self->drawer_host != NULL);
    g_return_if_fail(self->navigation_panel != NULL);
    gtk_box_append(GTK_BOX(self->drawer_host), self->navigation_panel);
    gtk_widget_set_visible(self->navigation_panel, TRUE);
    gtk_widget_set_visible(self->drawer_host, TRUE);
    gtk_paned_set_end_child(GTK_PANED(self->main_paned), self->drawer_host);
  } else if (self->show_analysis_drawer) {
    g_return_if_fail(self->drawer_host != NULL);
    g_return_if_fail(self->analysis_panel != NULL);
    gtk_box_append(GTK_BOX(self->drawer_host), self->analysis_panel);
    gtk_widget_set_visible(self->analysis_panel, TRUE);
    gtk_widget_set_visible(self->drawer_host, TRUE);
    gtk_paned_set_end_child(GTK_PANED(self->main_paned), self->drawer_host);
  } else {
    gtk_paned_set_end_child(GTK_PANED(self->main_paned), NULL);
  }

  ggame_window_apply_saved_panel_widths(self);
  gtk_widget_queue_allocate(GTK_WIDGET(self));
}

static void ggame_window_sync_puzzle_ui(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (self->controls_panel != NULL) {
    gtk_widget_set_visible(GTK_WIDGET(self->controls_panel), !self->puzzle_mode);
  }
  if (self->puzzle_panel != NULL) {
    gtk_widget_set_visible(self->puzzle_panel, self->puzzle_mode);
  }
  if (self->puzzle_next_button != NULL) {
    gtk_widget_set_sensitive(GTK_WIDGET(self->puzzle_next_button), self->puzzle_mode && self->puzzle_finished);
  }
  if (self->puzzle_analyze_button != NULL) {
    gtk_widget_set_sensitive(GTK_WIDGET(self->puzzle_analyze_button), self->puzzle_mode);
  }
}

static void ggame_window_on_show_navigation_drawer_change_state(GSimpleAction *action,
                                                                    GVariant *value,
                                                                    gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(G_IS_SIMPLE_ACTION(action));
  g_return_if_fail(value != NULL);

  self->show_navigation_drawer = g_variant_get_boolean(value);
  g_simple_action_set_state(action, value);
  ggame_window_sync_drawer_ui(self);
}

static void ggame_window_on_show_analysis_drawer_change_state(GSimpleAction *action,
                                                                  GVariant *value,
                                                                  gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(G_IS_SIMPLE_ACTION(action));
  g_return_if_fail(value != NULL);

  self->show_analysis_drawer = g_variant_get_boolean(value);
  g_simple_action_set_state(action, value);
  ggame_window_sync_drawer_ui(self);
}

static void ggame_window_analysis_reset_runtime_state(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  self->analysis_expected_nodes = 0;
  self->analysis_attached_nodes = 0;
  self->analysis_processed_nodes = 0;
  self->analysis_last_updated_node = NULL;
  self->analysis_done_received = FALSE;
  self->analysis_canceled = FALSE;
}

static void ggame_window_analysis_begin_session(GGameWindow *self,
                                                    GGameWindowAnalysisMode mode,
                                                    guint expected_nodes) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(mode == GGAME_WINDOW_ANALYSIS_MODE_CURRENT ||
                   mode == GGAME_WINDOW_ANALYSIS_MODE_FULL_GAME);

  self->analysis_mode = mode;
  ggame_window_analysis_reset_runtime_state(self);
  self->analysis_expected_nodes = expected_nodes;
  ggame_window_set_analysis_status(self, "");
  ggame_window_analysis_sync_ui(self);
}

static void ggame_window_analysis_finish_session(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  self->analysis_mode = GGAME_WINDOW_ANALYSIS_MODE_NONE;
  ggame_window_analysis_reset_runtime_state(self);
  ggame_window_set_analysis_status(self, "");
  ggame_window_analysis_sync_ui(self);
}

static gboolean ggame_window_is_edit_mode(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  return self->edit_mode_enabled;
}

static void ggame_window_set_action_enabled(GActionMap *map, const char *name, gboolean enabled) {
  g_return_if_fail(map != NULL);
  g_return_if_fail(name != NULL);

  GAction *action = g_action_map_lookup_action(map, name);
  if (action == NULL) {
    g_debug("Missing action while toggling enabled state: %s", name);
    return;
  }

  if (!G_IS_SIMPLE_ACTION(action)) {
    g_debug("Unsupported non-simple action while toggling enabled state: %s", name);
    return;
  }

  g_simple_action_set_enabled(G_SIMPLE_ACTION(action), enabled);
}

static void ggame_window_sync_mode_ui(GGameWindow *self) {
  const GGameAppProfile *profile = ggame_window_get_profile(self);
  gboolean supports_save_position = FALSE;
  gboolean supports_analysis = FALSE;
  gboolean supports_puzzles = FALSE;
  gboolean supports_edit_mode = FALSE;

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(profile != NULL);

  supports_save_position = profile->features.supports_save_position;
  supports_analysis = profile->features.supports_analysis;
  supports_puzzles = profile->features.supports_puzzles;
  supports_edit_mode = profile->features.supports_edit_mode;

  gboolean allow_navigation = !self->edit_mode_enabled && !self->puzzle_mode;
  gboolean allow_sgf_file_actions = !self->puzzle_mode;
  gboolean allow_view_actions = !self->puzzle_mode;
  gboolean allow_edit_mode_selection = supports_edit_mode && !self->puzzle_mode;

  ggame_window_set_action_enabled(G_ACTION_MAP(self), "game-force-move", allow_navigation);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "puzzle-play", supports_puzzles && !self->puzzle_mode);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "navigation-rewind", allow_navigation);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "navigation-step-backward", allow_navigation);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "navigation-step-forward", allow_navigation);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "navigation-step-forward-to-branch", allow_navigation);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "navigation-step-forward-to-end", allow_navigation);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "sgf-load", allow_sgf_file_actions);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "sgf-save-as", allow_sgf_file_actions);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "sgf-save-position", allow_sgf_file_actions && supports_save_position);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "view-show-navigation-drawer", allow_view_actions);
  ggame_window_set_action_enabled(G_ACTION_MAP(self), "view-show-analysis-drawer", allow_view_actions && supports_analysis);

  if (self->analysis_graph != NULL) {
    GtkWidget *graph_widget = analysis_graph_get_widget(self->analysis_graph);
    if (graph_widget != NULL) {
      gtk_widget_set_sensitive(graph_widget, allow_navigation && supports_analysis);
    }
  }

  if (self->sgf_controller != NULL) {
    GtkWidget *sgf_widget = ggame_sgf_controller_get_widget(self->sgf_controller);
    if (sgf_widget != NULL) {
      gtk_widget_set_sensitive(sgf_widget, allow_navigation);
    }
  }

  if (self->sgf_mode_control != NULL) {
    gtk_widget_set_sensitive(GTK_WIDGET(self->sgf_mode_control), allow_edit_mode_selection);
  }
}

static gboolean ggame_window_format_setup_point(uint8_t index, uint8_t board_size, char out_point[3]) {
  g_return_val_if_fail(out_point != NULL, FALSE);
  g_return_val_if_fail(board_size > 0, FALSE);

  gint row = 0;
  gint col = 0;
  board_coord_from_index(index, &row, &col, board_size);
  if (row < 0 || col < 0 || row >= 26 || col >= 26) {
    g_debug("Unsupported SGF setup coordinate for board size %u", board_size);
    return FALSE;
  }

  out_point[0] = (char)('a' + col);
  out_point[1] = (char)('a' + row);
  out_point[2] = '\0';
  return TRUE;
}

static const char *ggame_window_piece_label(CheckersPiece piece) {
  switch (piece) {
    case CHECKERS_PIECE_EMPTY:
      return "empty";
    case CHECKERS_PIECE_BLACK_MAN:
      return "black-man";
    case CHECKERS_PIECE_BLACK_KING:
      return "black-king";
    case CHECKERS_PIECE_WHITE_MAN:
      return "white-man";
    case CHECKERS_PIECE_WHITE_KING:
      return "white-king";
    default:
      return "unknown";
  }
}

static gboolean ggame_window_node_set_prop_has_point(SgfNode *node,
                                                         const char *ident,
                                                         const char *point,
                                                         gboolean has_point) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(ident != NULL, FALSE);
  g_return_val_if_fail(point != NULL, FALSE);

  g_autoptr(GPtrArray) next_values = g_ptr_array_new_with_free_func(g_free);
  const GPtrArray *existing = sgf_node_get_property_values(node, ident);
  if (existing != NULL) {
    for (guint i = 0; i < existing->len; ++i) {
      const char *value = g_ptr_array_index((GPtrArray *)existing, i);
      g_return_val_if_fail(value != NULL, FALSE);
      if (g_strcmp0(value, point) == 0) {
        continue;
      }
      g_ptr_array_add(next_values, g_strdup(value));
    }
  }
  if (has_point) {
    g_ptr_array_add(next_values, g_strdup(point));
  }

  sgf_node_clear_property(node, ident);
  for (guint i = 0; i < next_values->len; ++i) {
    const char *value = g_ptr_array_index(next_values, i);
    g_return_val_if_fail(value != NULL, FALSE);
    if (!sgf_node_add_property(node, ident, value)) {
      g_debug("Failed to add SGF setup property value");
      return FALSE;
    }
  }

  return TRUE;
}

static const GameBackendVariant *ggame_window_variant_for_ruleset(PlayerRuleset ruleset) {
  const char *short_name = checkers_ruleset_short_name(ruleset);

  if (short_name == NULL) {
    g_debug("Missing short name for ruleset %d", (gint) ruleset);
    return NULL;
  }

  return GGAME_ACTIVE_GAME_BACKEND->variant_by_short_name(short_name);
}

static gboolean ggame_window_ruleset_from_variant(const GameBackendVariant *variant, PlayerRuleset *out_ruleset) {
  g_return_val_if_fail(variant != NULL, FALSE);
  g_return_val_if_fail(out_ruleset != NULL, FALSE);
  g_return_val_if_fail(variant->short_name != NULL, FALSE);

  if (!checkers_ruleset_find_by_short_name(variant->short_name, out_ruleset)) {
    g_debug("Unable to map variant %s to a checkers ruleset", variant->short_name);
    return FALSE;
  }

  return TRUE;
}

static char *ggame_window_build_puzzle_variant_dir(GGameWindow *self, const GameBackendVariant *variant) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);
  g_return_val_if_fail(backend != NULL, NULL);
  g_return_val_if_fail(backend->id != NULL, NULL);
  g_return_val_if_fail(variant != NULL, NULL);
  g_return_val_if_fail(variant->short_name != NULL, NULL);

  g_autofree char *puzzle_root = ggame_app_paths_find_data_subdir("GCHECKERS_PUZZLES_DIR", "puzzles");
  if (puzzle_root == NULL) {
    g_debug("Failed to resolve puzzle root directory");
    return NULL;
  }

  return g_build_filename(puzzle_root, backend->id, variant->short_name, NULL);
}

static gboolean ggame_window_update_node_setup_piece(SgfNode *node, const char *point, CheckersPiece piece) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(point != NULL, FALSE);

  gboolean is_empty = piece == CHECKERS_PIECE_EMPTY;
  gboolean is_black = piece == CHECKERS_PIECE_BLACK_MAN || piece == CHECKERS_PIECE_BLACK_KING;
  gboolean is_white = piece == CHECKERS_PIECE_WHITE_MAN || piece == CHECKERS_PIECE_WHITE_KING;
  gboolean is_black_king = piece == CHECKERS_PIECE_BLACK_KING;
  gboolean is_white_king = piece == CHECKERS_PIECE_WHITE_KING;

  if (!ggame_window_node_set_prop_has_point(node, "AE", point, is_empty) ||
      !ggame_window_node_set_prop_has_point(node, "AB", point, is_black) ||
      !ggame_window_node_set_prop_has_point(node, "AW", point, is_white) ||
      !ggame_window_node_set_prop_has_point(node, "ABK", point, is_black_king) ||
      !ggame_window_node_set_prop_has_point(node, "AWK", point, is_white_king)) {
    g_debug("Edit update failed while setting SGF setup properties at point=%s target=%s",
            point,
            ggame_window_piece_label(piece));
    return FALSE;
  }

  sgf_node_clear_analysis(node);
  g_debug("Edit update wrote SGF setup properties at point=%s target=%s",
          point,
          ggame_window_piece_label(piece));
  return TRUE;
}

static gboolean ggame_window_on_board_square_action(guint8 index, guint button, gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  const GameState *state = NULL;

  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);
  if (button != GDK_BUTTON_PRIMARY && button != GDK_BUTTON_SECONDARY) {
    return FALSE;
  }

  if (!ggame_window_is_edit_mode(self)) {
    return FALSE;
  }

  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self->sgf_controller), FALSE);

  state = ggame_window_get_checkers_state(self);
  if (state == NULL) {
    g_debug("Missing game state for edit-mode square action");
    return TRUE;
  }

  guint8 max_square = board_playable_squares(state->board.board_size);
  if (index >= max_square) {
    g_debug("Edit-mode square index out of range");
    return TRUE;
  }

  CheckersPiece current = board_get(&state->board, index);
  CheckersPiece next = CHECKERS_PIECE_EMPTY;
  if (button == GDK_BUTTON_PRIMARY) {
    if (current == CHECKERS_PIECE_EMPTY) {
      next = CHECKERS_PIECE_WHITE_MAN;
    } else if (current == CHECKERS_PIECE_WHITE_MAN) {
      next = CHECKERS_PIECE_WHITE_KING;
    }
  } else {
    if (current == CHECKERS_PIECE_EMPTY) {
      next = CHECKERS_PIECE_BLACK_MAN;
    } else if (current == CHECKERS_PIECE_BLACK_MAN) {
      next = CHECKERS_PIECE_BLACK_KING;
    }
  }

  SgfTree *tree = ggame_sgf_controller_get_tree(self->sgf_controller);
  if (tree == NULL) {
    g_debug("Missing SGF tree for edit-mode square action");
    return TRUE;
  }
  SgfNode *current_node = (SgfNode *)sgf_tree_get_current(tree);
  if (current_node == NULL) {
    g_debug("Missing SGF current node for edit-mode square action");
    return TRUE;
  }

  char point[3] = {0};
  if (!ggame_window_format_setup_point(index, state->board.board_size, point)) {
    g_debug("Edit click failed formatting setup point: index=%u board_size=%u", index, state->board.board_size);
    return TRUE;
  }
  if (!ggame_window_update_node_setup_piece(current_node, point, next)) {
    g_debug("Edit click failed SGF setup update: index=%u point=%s", index, point);
    return TRUE;
  }
  if (!ggame_sgf_controller_refresh_current_node(self->sgf_controller)) {
    g_debug("Failed to refresh model from edited SGF current node");
    return TRUE;
  }

  const GameState *after = ggame_window_get_checkers_state(self);
  if (after == NULL) {
    g_debug("Edit click missing post-refresh game state: index=%u point=%s", index, point);
    return TRUE;
  }
  (void)after;

  return TRUE;
}

static void ggame_window_start_new_game(GGameWindow *self) {
  const GameBackendVariant *variant = NULL;

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_MODEL(self->game_model));

  ggame_window_leave_puzzle_mode(self, TRUE);
  variant = ggame_window_get_variant(self);
  ggame_model_reset(self->game_model, variant);
  board_view_clear_selection(self->board_view);
  ggame_sgf_controller_new_game(self->sgf_controller);
  g_clear_pointer(&self->loaded_source_name, g_free);
  ggame_window_sync_title(self);
}

static gboolean ggame_window_revert_wrong_puzzle_move_cb(gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_val_if_fail(GGAME_IS_WINDOW(self), G_SOURCE_REMOVE);

  self->puzzle_wrong_move_source_id = 0;
  self->puzzle_feedback_locked = FALSE;
  if (!ggame_sgf_controller_refresh_current_node(self->sgf_controller)) {
    g_debug("Failed to restore puzzle position after wrong move");
  }
  board_view_clear_selection(self->board_view);
  ggame_window_clear_board_banner(self);
  ggame_window_set_default_puzzle_message(self);
  ggame_window_sync_puzzle_ui(self);
  g_object_unref(self);
  return G_SOURCE_REMOVE;
}

static gboolean ggame_window_play_next_puzzle_step_if_needed(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  if (!self->puzzle_mode || self->puzzle_steps == NULL) {
    return FALSE;
  }

  while (self->puzzle_expected_step < self->puzzle_steps->len) {
    GGameWindowPuzzleStep *step =
        &g_array_index(self->puzzle_steps, GGameWindowPuzzleStep, self->puzzle_expected_step);
    if (step->color == self->puzzle_attacker) {
      break;
    }
    if (!ggame_sgf_controller_apply_move(self->sgf_controller, &step->move)) {
      g_debug("Failed to apply puzzle defense move at step %u", self->puzzle_expected_step);
      return FALSE;
    }
    self->puzzle_expected_step++;
  }

  if (self->puzzle_expected_step >= self->puzzle_steps->len) {
    self->puzzle_finished = TRUE;
    (void)ggame_window_puzzle_attempt_finish_success(self);
    g_autofree char *message = g_strdup_printf("Puzzle %04u.", self->puzzle_number);
    ggame_window_set_puzzle_message(self, message);
    board_view_set_banner_text(self->board_view, "Puzzle solved");
  } else {
    ggame_window_clear_board_banner(self);
    ggame_window_set_default_puzzle_message(self);
  }
  ggame_window_sync_puzzle_ui(self);
  return TRUE;
}

static void ggame_window_leave_puzzle_mode(GGameWindow *self, gboolean restore_drawers) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (!self->puzzle_mode) {
    return;
  }

  if (self->puzzle_attempt_started && !ggame_window_puzzle_attempt_is_terminal(self)) {
    (void)ggame_window_puzzle_attempt_finish_failure(self, FALSE, NULL);
  }

  ggame_window_capture_panel_widths(self);
  g_clear_handle_id(&self->puzzle_wrong_move_source_id, g_source_remove);
  ggame_window_clear_board_banner(self);
  self->puzzle_feedback_locked = FALSE;
  self->puzzle_mode = FALSE;
  self->layout_mode = GGAME_WINDOW_LAYOUT_MODE_NORMAL;
  self->puzzle_finished = FALSE;
  self->puzzle_expected_step = 0;
  self->puzzle_ruleset = PLAYER_RULESET_INTERNATIONAL;
  self->puzzle_attacker = CHECKERS_COLOR_WHITE;
  self->puzzle_number = 0;
  g_clear_pointer(&self->puzzle_path, g_free);
  if (self->puzzle_steps != NULL) {
    g_array_unref(self->puzzle_steps);
    self->puzzle_steps = NULL;
  }
  ggame_window_puzzle_attempt_reset(self);

  if (restore_drawers) {
    self->show_navigation_drawer = self->puzzle_saved_show_navigation_drawer;
    self->show_analysis_drawer = self->puzzle_saved_show_analysis_drawer;
  }
  ggame_window_sync_drawer_ui_with_capture(self, FALSE);
  ggame_window_sync_puzzle_ui(self);
  ggame_window_sync_mode_ui(self);
  ggame_window_analysis_sync_ui(self);
}

static gboolean ggame_window_enter_puzzle_mode_with_path(GGameWindow *self, const char *path) {
  const GameState *state = NULL;

  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  ggame_window_stop_analysis(self);

  g_autoptr(GError) error = NULL;
  if (!ggame_sgf_controller_load_file(self->sgf_controller, path, &error)) {
    g_debug("Failed to load puzzle file %s: %s", path, error != NULL ? error->message : "unknown error");
    return FALSE;
  }
  ggame_window_set_loaded_source_path(self, path);

  SgfTree *tree = ggame_sgf_controller_get_tree(self->sgf_controller);
  if (tree == NULL) {
    g_debug("Puzzle file load did not produce an SGF tree");
    return FALSE;
  }
  const GameBackendVariant *loaded_variant = NULL;
  if (sgf_io_tree_get_variant(tree, &loaded_variant, NULL) && loaded_variant != NULL) {
    ggame_window_set_loaded_variant(self, loaded_variant);
  }

  g_autoptr(GArray) steps = g_array_new(FALSE, FALSE, sizeof(GGameWindowPuzzleStep));
  if (!ggame_window_load_puzzle_steps_from_tree(tree, steps)) {
    g_debug("Puzzle file %s did not contain a valid main-line solution", path);
    return FALSE;
  }

  state = ggame_window_get_checkers_state(self);
  if (state == NULL) {
    g_debug("Missing model state after puzzle load");
    return FALSE;
  }

  GGameWindowPuzzleStep *first_step = &g_array_index(steps, GGameWindowPuzzleStep, 0);
  if (first_step->color != state->turn) {
    g_debug("Puzzle file %s first move does not match side to move", path);
    return FALSE;
  }

  if (!self->puzzle_mode) {
    ggame_window_capture_panel_widths(self);
    self->puzzle_saved_show_navigation_drawer = self->show_navigation_drawer;
    self->puzzle_saved_show_analysis_drawer = self->show_analysis_drawer;
    self->puzzle_board_panel_width = self->board_panel_width;
    self->puzzle_navigation_panel_width = self->navigation_panel_width;
    self->puzzle_analysis_panel_width = self->analysis_panel_width;
    self->puzzle_extra_width = self->extra_width;
  } else if (self->puzzle_steps != NULL) {
    if (self->puzzle_attempt_started && !ggame_window_puzzle_attempt_is_terminal(self)) {
      (void)ggame_window_puzzle_attempt_finish_failure(self, FALSE, NULL);
    }
    ggame_window_capture_panel_widths(self);
    g_array_unref(self->puzzle_steps);
    self->puzzle_steps = NULL;
    g_clear_pointer(&self->puzzle_path, g_free);
    ggame_window_puzzle_attempt_reset(self);
  }

  g_clear_handle_id(&self->puzzle_wrong_move_source_id, g_source_remove);
  self->puzzle_feedback_locked = FALSE;
  self->puzzle_mode = TRUE;
  self->layout_mode = GGAME_WINDOW_LAYOUT_MODE_PUZZLE;
  self->puzzle_finished = FALSE;
  self->puzzle_ruleset = self->applied_ruleset;
  self->puzzle_attacker = state->turn;
  if (!ggame_window_parse_puzzle_number_from_path(path, &self->puzzle_number)) {
    self->puzzle_number = 0;
  }
  self->puzzle_path = g_strdup(path);
  self->puzzle_attempt_made_player_move = FALSE;
  self->puzzle_expected_step = 0;
  self->puzzle_steps = g_steal_pointer(&steps);
  self->show_navigation_drawer = FALSE;
  self->show_analysis_drawer = FALSE;
  self->edit_mode_enabled = FALSE;
  if (self->sgf_mode_control != NULL) {
    gtk_drop_down_set_selected(self->sgf_mode_control, 0);
  }
  board_view_clear_selection(self->board_view);
  ggame_window_clear_board_banner(self);
  ggame_window_set_board_orientation_mode(self, GGAME_WINDOW_BOARD_ORIENTATION_FIXED);
  ggame_window_set_board_bottom_color(self, self->puzzle_attacker);
  ggame_window_set_default_puzzle_message(self);
  ggame_window_sync_drawer_ui_with_capture(self, FALSE);
  ggame_window_sync_puzzle_ui(self);
  ggame_window_sync_mode_ui(self);
  ggame_window_analysis_sync_ui(self);
  return TRUE;
}

gboolean ggame_window_start_puzzle_mode_for_path(GGameWindow *self,
                                                 const GameBackendVariant *variant,
                                                 const char *path) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(variant != NULL, FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  g_autofree char *variant_dir = ggame_window_build_puzzle_variant_dir(self, variant);
  g_return_val_if_fail(variant_dir != NULL, FALSE);
  if (!g_str_has_prefix(path, variant_dir)) {
    g_debug("Puzzle path %s does not match variant directory %s", path, variant_dir);
    return FALSE;
  }
  if (!ggame_window_enter_puzzle_mode_with_path(self, path)) {
    return FALSE;
  }

  if (!ggame_window_ruleset_from_variant(variant, &self->puzzle_ruleset)) {
    return FALSE;
  }
  return TRUE;
}

static void ggame_window_start_opened_puzzle_attempt(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  (void)ggame_window_puzzle_attempt_ensure_started(self);
}

static gboolean ggame_window_start_next_puzzle_mode_for_ruleset(GGameWindow *self, PlayerRuleset ruleset) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(self->puzzle_path != NULL, FALSE);

  const GameBackendVariant *variant = ggame_window_variant_for_ruleset(ruleset);
  g_return_val_if_fail(variant != NULL, FALSE);

  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) puzzle_entries = game_puzzle_catalog_load_variant(GGAME_ACTIVE_GAME_BACKEND, variant, &error);
  if (puzzle_entries == NULL) {
    g_debug("Failed to load puzzle catalog: %s", error != NULL ? error->message : "unknown error");
    return FALSE;
  }
  if (puzzle_entries->len == 0) {
    g_autofree char *dir_path = ggame_window_build_puzzle_variant_dir(self, variant);
    g_debug("No puzzle files found in %s", dir_path != NULL ? dir_path : "(unknown)");
    return FALSE;
  }

  guint current_index = G_MAXUINT;
  for (guint i = 0; i < puzzle_entries->len; i++) {
    GamePuzzleCatalogEntry *entry = g_ptr_array_index(puzzle_entries, i);
    g_return_val_if_fail(entry != NULL, FALSE);
    if (g_strcmp0(entry->path, self->puzzle_path) == 0) {
      current_index = i;
      break;
    }
  }

  if (current_index == G_MAXUINT) {
    g_debug("Current puzzle path %s was not found in the %s puzzle catalog", self->puzzle_path,
            checkers_ruleset_short_name(ruleset));
    return FALSE;
  }

  guint next_index = current_index + 1;
  if (next_index >= puzzle_entries->len) {
    next_index = 0;
  }

  GamePuzzleCatalogEntry *entry = g_ptr_array_index(puzzle_entries, next_index);
  g_return_val_if_fail(entry != NULL, FALSE);
  g_return_val_if_fail(entry->path != NULL, FALSE);
  if (!ggame_window_start_puzzle_mode_for_path(self, variant, entry->path)) {
    return FALSE;
  }

  ggame_window_start_opened_puzzle_attempt(self);
  return TRUE;
}

static void ggame_window_on_puzzle_dialog_done(gboolean selected,
                                               const GameBackendVariant *variant,
                                               const char *path,
                                               gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (!selected) {
    return;
  }
  if (path == NULL || path[0] == '\0') {
    g_debug("Puzzle dialog returned an empty puzzle path");
    return;
  }
  if (variant == NULL) {
    g_debug("Puzzle dialog returned no variant");
    return;
  }
  if (!ggame_window_start_puzzle_mode_for_path(self, variant, path)) {
    g_debug("Failed to load selected puzzle %s", path);
    return;
  }

  ggame_window_start_opened_puzzle_attempt(self);
}

void ggame_window_present_puzzle_dialog(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  ggame_puzzle_dialog_present(GTK_WINDOW(self),
                                  ggame_window_variant_for_ruleset(self->applied_ruleset),
                                  self->puzzle_progress_store,
                                  ggame_window_on_puzzle_dialog_done,
                                  g_object_ref(self),
                                  (GDestroyNotify)g_object_unref);
}

static gboolean ggame_window_apply_player_move(const CheckersMove *move, gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);

  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self->sgf_controller), FALSE);

  if (self->puzzle_mode) {
    if (self->puzzle_feedback_locked || self->puzzle_finished || self->puzzle_steps == NULL) {
      return FALSE;
    }
    if (self->puzzle_expected_step >= self->puzzle_steps->len) {
      g_debug("Puzzle move attempted after the solution was already complete");
      return FALSE;
    }

    GGameWindowPuzzleStep *expected =
        &g_array_index(self->puzzle_steps, GGameWindowPuzzleStep, self->puzzle_expected_step);
    (void)ggame_window_puzzle_attempt_ensure_started(self);
    gboolean failure_on_first_move = !self->puzzle_attempt_made_player_move;
    if (!ggame_window_moves_equal(move, &expected->move)) {
      (void)ggame_window_puzzle_attempt_finish_failure(self, failure_on_first_move, move);
      self->puzzle_feedback_locked = TRUE;
      ggame_window_set_puzzle_message(self, "");
      board_view_set_banner_text_red(self->board_view, "Wrong move");
      if (!ggame_model_apply_move(self->game_model, move)) {
        self->puzzle_feedback_locked = FALSE;
        ggame_window_clear_board_banner(self);
        ggame_window_set_default_puzzle_message(self);
        return FALSE;
      }
      board_view_clear_selection(self->board_view);
      self->puzzle_wrong_move_source_id = g_timeout_add_full(G_PRIORITY_DEFAULT,
                                                             GGAME_WINDOW_PUZZLE_WRONG_MOVE_DELAY_MS,
                                                             ggame_window_revert_wrong_puzzle_move_cb,
                                                             g_object_ref(self),
                                                             NULL);
      return TRUE;
    }

    if (!ggame_sgf_controller_apply_move(self->sgf_controller, move)) {
      return FALSE;
    }

    self->puzzle_attempt_made_player_move = TRUE;
    self->puzzle_expected_step++;
    return ggame_window_play_next_puzzle_step_if_needed(self);
  }

  if (!ggame_sgf_controller_apply_move(self->sgf_controller, move)) {
    return FALSE;
  }

  return TRUE;
}

static gboolean ggame_window_is_user_control(GGameWindow *self, guint side) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  if (!self->controls_panel) {
    g_debug("Missing controls panel when checking control mode\n");
    return TRUE;
  }

  return player_controls_panel_is_user_control(self->controls_panel, side);
}

static gboolean ggame_window_is_computer_control(GGameWindow *self, guint side) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  return !ggame_window_is_user_control(self, side);
}

guint ggame_window_get_analysis_depth(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), GGAME_WINDOW_ANALYSIS_DEPTH_DEFAULT);
  g_return_val_if_fail(self->analysis_depth_scale != NULL, GGAME_WINDOW_ANALYSIS_DEPTH_DEFAULT);

  guint depth = (guint)gtk_range_get_value(GTK_RANGE(self->analysis_depth_scale));
  if (!ggame_window_analysis_depth_valid(depth)) {
    g_debug("Unexpected analysis depth value");
    return GGAME_WINDOW_ANALYSIS_DEPTH_DEFAULT;
  }

  return depth;
}

void ggame_window_set_analysis_depth(GGameWindow *self, guint depth) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(ggame_window_analysis_depth_valid(depth));
  g_return_if_fail(self->analysis_depth_scale != NULL);

  gtk_range_set_value(GTK_RANGE(self->analysis_depth_scale), (gdouble)depth);
}

static void ggame_window_set_ruleset(GGameWindow *self, PlayerRuleset ruleset) {
  const GameBackendVariant *variant = NULL;

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_MODEL(self->game_model));
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(self->sgf_controller));

  variant = ggame_window_variant_for_ruleset(ruleset);
  if (variant == NULL) {
    return;
  }

  if (ggame_model_peek_variant(self->game_model) == variant) {
    self->applied_ruleset = ruleset;
    return;
  }

  ggame_model_reset(self->game_model, variant);
  board_view_clear_selection(self->board_view);
  ggame_sgf_controller_new_game(self->sgf_controller);
  self->applied_ruleset = ruleset;
}

void ggame_window_set_loaded_variant(GGameWindow *self, const GameBackendVariant *variant) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(variant != NULL);

  if (ggame_window_get_profile(self)->kind == GGAME_APP_KIND_CHECKERS) {
    if (!ggame_window_ruleset_from_variant(variant, &self->applied_ruleset)) {
      return;
    }
  }
}

static void ggame_window_set_analysis_text(GGameWindow *self, const char *text) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(text != NULL);

  if (!self->analysis_buffer) {
    g_debug("Missing analysis buffer");
    return;
  }

  gtk_text_buffer_set_text(self->analysis_buffer, text, -1);
}

static void ggame_window_set_analysis_status(GGameWindow *self, const char *text) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (self->analysis_status_label == NULL) {
    g_debug("Missing analysis status label");
    return;
  }
  if (!GTK_IS_LABEL(self->analysis_status_label)) {
    g_debug("Analysis status label is no longer a live GtkLabel");
    return;
  }

  gtk_label_set_text(self->analysis_status_label, text != NULL ? text : "");
}

static void ggame_window_show_analysis_for_current_node(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (!ggame_window_get_profile(self)->features.supports_analysis) {
    ggame_window_set_analysis_text(self, "");
    return;
  }

  SgfTree *tree = ggame_sgf_controller_get_tree(self->sgf_controller);
  const SgfNode *node = tree != NULL ? sgf_tree_get_current(tree) : NULL;
  if (node == NULL) {
    return;
  }

  g_autoptr(SgfNodeAnalysis) analysis = sgf_node_get_analysis(node);
  if (analysis != NULL) {
    g_autofree char *text = ggame_window_analysis_format_complete(analysis);
    if (text != NULL) {
      ggame_window_set_analysis_text(self, text);
    }
    return;
  }

  ggame_window_set_analysis_text(self, "");
}

static gboolean ggame_window_should_cancel_analysis(gpointer user_data) {
  GGameWindowAnalysisTask *task = user_data;
  g_return_val_if_fail(task != NULL, TRUE);
  g_return_val_if_fail(GGAME_IS_WINDOW(task->self), TRUE);

  return g_atomic_int_get(&task->self->analysis_generation) != task->generation;
}

static gboolean ggame_window_should_cancel_full_analysis(gpointer user_data) {
  GGameWindowFullAnalysisTask *task = user_data;
  g_return_val_if_fail(task != NULL, TRUE);
  g_return_val_if_fail(GGAME_IS_WINDOW(task->self), TRUE);

  return g_atomic_int_get(&task->self->analysis_generation) != task->generation;
}

static SgfNodeAnalysis *ggame_window_analysis_from_scored_moves(const GameBackend *backend,
                                                                const GameAiScoredMoveList *moves,
                                                                guint depth,
                                                                const GameAiSearchStats *stats) {
  g_return_val_if_fail(backend != NULL, NULL);
  g_return_val_if_fail(moves != NULL, NULL);
  g_return_val_if_fail(stats != NULL, NULL);
  g_return_val_if_fail(backend->format_move != NULL, NULL);

  SgfNodeAnalysis *analysis = sgf_node_analysis_new();
  if (analysis == NULL) {
    return NULL;
  }

  analysis->depth = depth;
  analysis->nodes = stats->nodes;
  analysis->tt_probes = stats->tt_probes;
  analysis->tt_hits = stats->tt_hits;
  analysis->tt_cutoffs = stats->tt_cutoffs;

  for (gsize i = 0; i < moves->count; ++i) {
    char notation[128] = {0};
    if (moves->moves[i].move == NULL || !backend->format_move(moves->moves[i].move, notation, sizeof(notation))) {
      sgf_node_analysis_free(analysis);
      return NULL;
    }
    if (!sgf_node_analysis_add_scored_move(analysis,
                                           notation,
                                           moves->moves[i].score,
                                           moves->moves[i].nodes)) {
      sgf_node_analysis_free(analysis);
      return NULL;
    }
  }

  return analysis;
}

static gboolean ggame_window_node_first_score(const SgfNode *node, gint *out_score) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(out_score != NULL, FALSE);

  g_autoptr(SgfNodeAnalysis) analysis = sgf_node_get_analysis(node);
  if (analysis == NULL || analysis->moves == NULL || analysis->moves->len == 0) {
    return FALSE;
  }

  const SgfNodeScoredMove *entry = g_ptr_array_index(analysis->moves, 0);
  if (entry == NULL) {
    return FALSE;
  }

  *out_score = entry->score;
  return TRUE;
}

static void ggame_window_analysis_event_free(gpointer data) {
  GGameWindowAnalysisEvent *event = data;
  if (event == NULL) {
    return;
  }

  g_clear_pointer(&event->status_text, g_free);
  sgf_node_analysis_free(event->analysis);
  g_free(event);
}

static void ggame_window_maybe_finish_full_analysis(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (self->analysis_mode != GGAME_WINDOW_ANALYSIS_MODE_FULL_GAME) {
    return;
  }
  if (!self->analysis_done_received) {
    return;
  }
  if (!self->analysis_canceled && self->analysis_processed_nodes < self->analysis_expected_nodes) {
    return;
  }

  ggame_window_analysis_finish_session(self);
}

static gboolean ggame_window_analysis_dispatch_cb(gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_val_if_fail(GGAME_IS_WINDOW(self), G_SOURCE_REMOVE);

  while (TRUE) {
    GGameWindowAnalysisEvent *event = NULL;

    g_mutex_lock(&self->analysis_report_mutex);
    if (self->analysis_report_queue != NULL && !g_queue_is_empty(self->analysis_report_queue)) {
      event = g_queue_pop_head(self->analysis_report_queue);
    }
    g_mutex_unlock(&self->analysis_report_mutex);

    if (event == NULL) {
      break;
    }

    gint current_generation = g_atomic_int_get(&self->analysis_generation);
    if (event->generation != current_generation) {
      g_debug("Analysis dispatch dropped stale event: event_gen=%d current_gen=%d mode=%d payload=%d status=%d",
              event->generation,
              current_generation,
              event->mode,
              event->is_payload,
              event->status_text != NULL);
      ggame_window_analysis_event_free(event);
      continue;
    }

    if (event->status_text != NULL) {
      ggame_window_set_analysis_status(self, event->status_text);
      if (event->mode == GGAME_WINDOW_ANALYSIS_MODE_FULL_GAME && !event->done) {
        self->analysis_processed_nodes++;
      }
    }

    if (event->is_payload && event->analysis != NULL && event->node != NULL) {
      if (!sgf_node_set_analysis((SgfNode *)event->node, event->analysis)) {
        g_debug("Failed to attach analysis to selected SGF node");
      } else {
        if (event->mode == GGAME_WINDOW_ANALYSIS_MODE_FULL_GAME) {
          self->analysis_attached_nodes++;
          self->analysis_last_updated_node = event->node;
        }
        ggame_window_refresh_analysis_graph(self);
        SgfTree *tree = ggame_sgf_controller_get_tree(self->sgf_controller);
        if (tree != NULL && sgf_tree_get_current(tree) == event->node) {
          ggame_window_show_analysis_for_current_node(self);
        }
      }
    }

    if (event->done && event->mode == GGAME_WINDOW_ANALYSIS_MODE_FULL_GAME) {
      self->analysis_done_received = TRUE;
      self->analysis_canceled = event->canceled;
    }

    ggame_window_analysis_event_free(event);
  }

  ggame_window_maybe_finish_full_analysis(self);
  g_object_unref(self);
  return G_SOURCE_REMOVE;
}

static void ggame_window_analysis_enqueue_event(GGameWindow *self, GGameWindowAnalysisEvent *event) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(event != NULL);

  g_mutex_lock(&self->analysis_report_mutex);
  g_return_if_fail(self->analysis_report_queue != NULL);
  g_queue_push_tail(self->analysis_report_queue, event);
  g_mutex_unlock(&self->analysis_report_mutex);

  g_main_context_invoke(NULL, ggame_window_analysis_dispatch_cb, g_object_ref(self));
}

static void ggame_window_analysis_publish_status(GGameWindow *self,
                                                     gint generation,
                                                     GGameWindowAnalysisMode mode,
                                                     gboolean done,
                                                     gboolean canceled,
                                                     const char *text) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  GGameWindowAnalysisEvent *event = g_new0(GGameWindowAnalysisEvent, 1);
  event->generation = generation;
  event->mode = mode;
  event->done = done;
  event->canceled = canceled;
  if (text != NULL) {
    event->status_text = g_strdup(text);
  }
  ggame_window_analysis_enqueue_event(self, event);
}

static void ggame_window_analysis_publish_payload(GGameWindow *self,
                                                      gint generation,
                                                      GGameWindowAnalysisMode mode,
                                                      const SgfNodeAnalysis *analysis,
                                                      const SgfNode *node) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(analysis != NULL);
  g_return_if_fail(node != NULL);

  GGameWindowAnalysisEvent *event = g_new0(GGameWindowAnalysisEvent, 1);
  event->generation = generation;
  event->mode = mode;
  event->is_payload = TRUE;
  event->analysis = sgf_node_analysis_copy(analysis);
  event->node = node;
  if (event->analysis == NULL) {
    g_debug("Failed to copy payload analysis event");
    ggame_window_analysis_event_free(event);
    return;
  }
  ggame_window_analysis_enqueue_event(self, event);
}

static void ggame_window_refresh_analysis_graph(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(ANALYSIS_IS_GRAPH(self->analysis_graph));

  if (!ggame_window_get_profile(self)->features.supports_analysis) {
    analysis_graph_set_nodes(self->analysis_graph, NULL, 0);
    analysis_graph_clear_progress_node(self->analysis_graph);
    return;
  }

  SgfTree *tree = ggame_sgf_controller_get_tree(self->sgf_controller);
  if (tree == NULL) {
    analysis_graph_set_nodes(self->analysis_graph, NULL, 0);
    analysis_graph_clear_progress_node(self->analysis_graph);
    return;
  }

  g_autoptr(GPtrArray) branch = sgf_tree_build_current_branch(tree);
  if (branch == NULL) {
    analysis_graph_set_nodes(self->analysis_graph, NULL, 0);
    analysis_graph_clear_progress_node(self->analysis_graph);
    return;
  }

  const SgfNode *current = sgf_tree_get_current(tree);
  guint selected_index = 0;
  for (guint i = 0; i < branch->len; ++i) {
    if (g_ptr_array_index(branch, i) == current) {
      selected_index = i;
      break;
    }
  }

  guint analyzed_count = 0;
  for (guint i = 0; i < branch->len; ++i) {
    const SgfNode *node = g_ptr_array_index(branch, i);
    if (node == NULL) {
      continue;
    }
    gint score = 0;
    gboolean has_score = ggame_window_node_first_score(node, &score);
    (void)score;
    (void)current;
    if (has_score) {
      analyzed_count++;
    }
  }
  (void)analyzed_count;

  analysis_graph_set_nodes(self->analysis_graph, branch, selected_index);
  if (self->analysis_mode != GGAME_WINDOW_ANALYSIS_MODE_FULL_GAME || self->analysis_last_updated_node == NULL) {
    analysis_graph_clear_progress_node(self->analysis_graph);
    return;
  }

  for (guint i = 0; i < branch->len; ++i) {
    const SgfNode *branch_node = g_ptr_array_index(branch, i);
    if (branch_node == self->analysis_last_updated_node) {
      analysis_graph_set_progress_node(self->analysis_graph, branch_node);
      return;
    }
  }
  analysis_graph_clear_progress_node(self->analysis_graph);
}

char *ggame_window_format_analysis_score(gint score) {
  gint abs_score = ABS(score);
  if (abs_score >= 2900 && abs_score <= 3000) {
    gint distance = 3000 - abs_score;
    return g_strdup_printf("%c#%d", score > 0 ? 'W' : 'B', distance);
  }
  if (abs_score >= 99000 && abs_score <= 100000) {
    gint distance = 100000 - abs_score;
    return g_strdup_printf("%c#%d", score > 0 ? 'W' : 'B', distance);
  }

  return g_strdup_printf("%+d", score);
}

static void ggame_window_analysis_append_scored_moves(GString *text, const SgfNodeAnalysis *analysis) {
  g_return_if_fail(text != NULL);
  g_return_if_fail(analysis != NULL);
  g_return_if_fail(analysis->moves != NULL);

  gsize score_width = 0;
  for (guint i = 0; i < analysis->moves->len; ++i) {
    const SgfNodeScoredMove *entry = g_ptr_array_index(analysis->moves, i);
    if (entry == NULL) {
      continue;
    }

    g_autofree char *score_text = ggame_window_format_analysis_score(entry->score);
    if (score_text == NULL) {
      g_debug("Failed to format analysis score");
      continue;
    }

    score_width = MAX(score_width, strlen(score_text));
  }

  for (guint i = 0; i < analysis->moves->len; ++i) {
    const SgfNodeScoredMove *entry = g_ptr_array_index(analysis->moves, i);
    if (entry == NULL) {
      continue;
    }
    const char *notation = entry->move_text != NULL ? entry->move_text : "?";

    g_autofree char *score_text = ggame_window_format_analysis_score(entry->score);
    if (score_text == NULL) {
      g_debug("Failed to format analysis score");
      continue;
    }
    g_string_append_printf(text, "%*s  %s\n", (gint)score_width, score_text, notation);
  }
}

char *ggame_window_format_analysis_report(const SgfNodeAnalysis *analysis) {
  g_return_val_if_fail(analysis != NULL, NULL);
  g_return_val_if_fail(analysis->moves != NULL, NULL);

  GString *text = g_string_new(NULL);
  g_string_append_printf(text, "Analysis depth: %u\n", analysis->depth);
  ggame_window_analysis_append_scored_moves(text, analysis);
  return g_string_free(text, FALSE);
}

static char *ggame_window_analysis_format_complete(const SgfNodeAnalysis *analysis) {
  return ggame_window_format_analysis_report(analysis);
}

static char *ggame_window_analysis_format_full_game_status(guint completed_nodes,
                                                               guint total_nodes,
                                                               guint64 explored_nodes,
                                                               const char *detail) {
  g_return_val_if_fail(total_nodes > 0, NULL);

  if (detail != NULL && *detail != '\0') {
    return g_strdup_printf("Analysis: %u/%u (%" G_GUINT64_FORMAT ") %s.",
                           completed_nodes,
                           total_nodes,
                           explored_nodes,
                           detail);
  }

  return g_strdup_printf("Analysis: %u/%u (%" G_GUINT64_FORMAT ").",
                         completed_nodes,
                         total_nodes,
                         explored_nodes);
}

static char *ggame_window_analysis_format_progress(const GGameWindowAnalysisTask *task,
                                                   const GameAiSearchStats *stats) {
  g_return_val_if_fail(task != NULL, NULL);
  g_return_val_if_fail(stats != NULL, NULL);

  GString *text = g_string_new(NULL);
  g_string_append_printf(text, "Analysis depth: %u (searching)\n", task->current_depth);
  g_string_append_printf(text, "Nodes: %" G_GUINT64_FORMAT "\n", stats->nodes);

  if (task->last_completed_analysis == NULL) {
    g_string_append(text, "Best to worst:\n");
    g_string_append(text, "(searching...)\n");
    return g_string_free(text, FALSE);
  }

  g_string_append_printf(text, "Last completed depth: %u\n", task->last_completed_depth);
  ggame_window_analysis_append_scored_moves(text, task->last_completed_analysis);
  return g_string_free(text, FALSE);
}

static void ggame_window_analysis_on_progress(const GameAiSearchStats *stats, gpointer user_data) {
  GGameWindowAnalysisTask *task = user_data;
  g_return_if_fail(stats != NULL);
  g_return_if_fail(task != NULL);
  g_return_if_fail(GGAME_IS_WINDOW(task->self));

  const gint64 interval_us = (gint64)GGAME_WINDOW_ANALYSIS_PROGRESS_INTERVAL_MS * 1000;
  gint64 now = g_get_monotonic_time();
  if (task->last_progress_publish_us != 0 && now - task->last_progress_publish_us < interval_us) {
    return;
  }

  task->last_progress_publish_us = now;
  g_autofree char *text = ggame_window_analysis_format_progress(task, stats);
  if (text == NULL) {
    g_debug("Failed to format analysis progress text");
    return;
  }

  ggame_window_analysis_publish_status(task->self,
                                           task->generation,
                                           GGAME_WINDOW_ANALYSIS_MODE_CURRENT,
                                           FALSE,
                                           FALSE,
                                           text);
}

static void ggame_window_full_analysis_on_progress(const GameAiSearchStats *stats, gpointer user_data) {
  GGameWindowFullAnalysisTask *task = user_data;
  g_return_if_fail(stats != NULL);
  g_return_if_fail(task != NULL);
  g_return_if_fail(GGAME_IS_WINDOW(task->self));
  g_return_if_fail(task->jobs != NULL);

  const gint64 interval_us = (gint64)GGAME_WINDOW_ANALYSIS_PROGRESS_INTERVAL_MS * 1000;
  gint64 now = g_get_monotonic_time();
  if (task->last_progress_publish_us != 0 && now - task->last_progress_publish_us < interval_us) {
    return;
  }

  task->last_progress_publish_us = now;
  g_autofree char *text = ggame_window_analysis_format_full_game_status(task->current_job_index,
                                                                            task->jobs->len,
                                                                            task->explored_nodes + stats->nodes,
                                                                            NULL);
  if (text == NULL) {
    g_debug("Failed to format full-game analysis progress text");
    return;
  }

  ggame_window_analysis_publish_status(task->self,
                                           task->generation,
                                           GGAME_WINDOW_ANALYSIS_MODE_FULL_GAME,
                                           FALSE,
                                           FALSE,
                                           text);
}

static gpointer ggame_window_analysis_thread(gpointer user_data) {
  GGameWindowAnalysisTask *task = user_data;
  g_return_val_if_fail(task != NULL, NULL);
  g_return_val_if_fail(task->backend != NULL, NULL);
  g_return_val_if_fail(task->position != NULL, NULL);

  for (guint depth = GGAME_WINDOW_ANALYSIS_DEPTH_MIN;
       depth <= task->target_depth && !ggame_window_should_cancel_analysis(task);
       ++depth) {
    task->current_depth = depth;
    task->last_progress_publish_us = 0;
    ggame_window_analysis_on_progress(&task->cumulative_stats, task);

    GameAiScoredMoveList moves = {0};
    gboolean ok = game_ai_search_analyze_moves_cancellable_with_tt(
        task->backend,
        task->position,
        depth,
        &moves,
        ggame_window_should_cancel_analysis,
        task,
        ggame_window_analysis_on_progress,
        task,
        task->tt,
        &task->cumulative_stats);
    if (!ok) {
      if (!ggame_window_should_cancel_analysis(task)) {
        ggame_window_analysis_publish_status(task->self,
                                                 task->generation,
                                                 GGAME_WINDOW_ANALYSIS_MODE_CURRENT,
                                                 TRUE,
                                                 FALSE,
                                                 "No legal moves to analyze.");
      }
      break;
    }

    g_autoptr(SgfNodeAnalysis) analysis =
        ggame_window_analysis_from_scored_moves(task->backend, &moves, depth, &task->cumulative_stats);
    game_ai_scored_move_list_free(&moves);
    if (analysis == NULL) {
      g_debug("Failed to build SGF node analysis payload");
      break;
    }

    g_autofree char *text = ggame_window_analysis_format_complete(analysis);
    if (text == NULL) {
      break;
    }

    sgf_node_analysis_free(task->last_completed_analysis);
    task->last_completed_analysis = sgf_node_analysis_copy(analysis);
    task->last_completed_depth = depth;
    ggame_window_analysis_publish_payload(task->self,
                                              task->generation,
                                              GGAME_WINDOW_ANALYSIS_MODE_CURRENT,
                                              analysis,
                                              task->target_node);
    ggame_window_analysis_publish_status(task->self,
                                             task->generation,
                                             GGAME_WINDOW_ANALYSIS_MODE_CURRENT,
                                             FALSE,
                                             FALSE,
                                             text);
  }

  ggame_window_analysis_publish_status(task->self,
                                           task->generation,
                                           GGAME_WINDOW_ANALYSIS_MODE_CURRENT,
                                           TRUE,
                                           ggame_window_should_cancel_analysis(task),
                                           ggame_window_should_cancel_analysis(task) ? "Analysis stopped." : NULL);

  g_object_unref(task->self);
  game_ai_tt_free(task->tt);
  task->backend->position_clear(task->position);
  g_free(task->position);
  sgf_node_analysis_free(task->last_completed_analysis);
  g_free(task);
  return NULL;
}

static void ggame_window_full_node_job_free(gpointer data) {
  GGameWindowFullNodeJob *job = data;
  if (job == NULL) {
    return;
  }

  g_free(job);
}

static GPtrArray *ggame_window_build_full_analysis_jobs(SgfTree *tree) {
  g_return_val_if_fail(SGF_IS_TREE(tree), NULL);

  g_autoptr(GPtrArray) nodes = sgf_tree_collect_nodes_preorder(tree);
  if (nodes == NULL) {
    g_debug("Failed to collect SGF nodes for full analysis");
    return NULL;
  }

  GPtrArray *jobs = g_ptr_array_new_with_free_func(ggame_window_full_node_job_free);
  for (guint i = 0; i < nodes->len; ++i) {
    const SgfNode *node = g_ptr_array_index(nodes, i);
    g_return_val_if_fail(node != NULL, NULL);

    GGameWindowFullNodeJob *job = g_new0(GGameWindowFullNodeJob, 1);
    job->node = node;

    g_ptr_array_add(jobs, job);
  }

  for (guint i = 0; i < jobs->len / 2; ++i) {
    guint swap_i = jobs->len - 1 - i;
    gpointer tmp = g_ptr_array_index(jobs, i);
    jobs->pdata[i] = jobs->pdata[swap_i];
    jobs->pdata[swap_i] = tmp;
  }

  return jobs;
}

static gpointer ggame_window_full_analysis_thread(gpointer user_data) {
  GGameWindowFullAnalysisTask *task = user_data;
  g_return_val_if_fail(task != NULL, NULL);
  g_return_val_if_fail(task->jobs != NULL, NULL);
  g_return_val_if_fail(task->backend != NULL, NULL);
  if (!task->use_checkers_replay) {
    g_return_val_if_fail(task->backend->position_size > 0, NULL);
    g_return_val_if_fail(task->backend->position_init != NULL, NULL);
    g_return_val_if_fail(task->backend->position_clear != NULL, NULL);
  }

  for (guint i = 0; i < task->jobs->len; ++i) {
    if (ggame_window_should_cancel_full_analysis(task)) {
      break;
    }

    task->current_job_index = i;
    task->last_progress_publish_us = 0;

    GGameWindowFullNodeJob *job = g_ptr_array_index(task->jobs, i);
    g_return_val_if_fail(job != NULL, NULL);

    g_autoptr(GError) replay_error = NULL;
    GameAiScoredMoveList moves = {0};
    GameAiSearchStats stats = {0};
    ggame_window_full_analysis_on_progress(&stats, task);
    gboolean ok = FALSE;
    gboolean replay_ok = FALSE;
    gconstpointer analysis_position = NULL;
    g_autofree guint8 *generic_position = NULL;
    Game checkers_game = {0};

    if (task->use_checkers_replay) {
      g_return_val_if_fail(task->checkers_rules != NULL, NULL);
      game_init_with_rules(&checkers_game, task->checkers_rules);
      replay_ok = ggame_sgf_controller_replay_node_into_game(job->node, &checkers_game, &replay_error);
      analysis_position = &checkers_game;
    } else {
      generic_position = g_malloc0(task->backend->position_size);
      if (generic_position == NULL) {
        g_debug("Failed to allocate replay position for full-game analysis");
        continue;
      }

      task->backend->position_init(generic_position, task->variant);
      replay_ok =
          ggame_sgf_controller_replay_node_into_position(job->node, task->backend, generic_position, &replay_error);
      analysis_position = generic_position;
    }

    if (!replay_ok) {
      if (!task->use_checkers_replay && generic_position != NULL) {
        task->backend->position_clear(generic_position);
      }
      if (task->use_checkers_replay) {
        game_destroy(&checkers_game);
      }
      g_debug("Skipping full analysis for SGF node %u: %s",
              sgf_node_get_move_number(job->node),
              replay_error != NULL ? replay_error->message : "unknown error");
      g_autofree char *text = ggame_window_analysis_format_full_game_status(i + 1,
                                                                                task->jobs->len,
                                                                                task->explored_nodes,
                                                                                "replay skipped");
      ggame_window_analysis_publish_status(task->self,
                                               task->generation,
                                               GGAME_WINDOW_ANALYSIS_MODE_FULL_GAME,
                                               FALSE,
                                               FALSE,
                                               text);
      continue;
    }

    ok = game_ai_search_analyze_moves_cancellable_with_tt(
        task->backend,
        analysis_position,
        task->depth,
        &moves,
        ggame_window_should_cancel_full_analysis,
        task,
        ggame_window_full_analysis_on_progress,
        task,
        task->tt,
        &stats);
    if (!task->use_checkers_replay && generic_position != NULL) {
      task->backend->position_clear(generic_position);
    }
    if (task->use_checkers_replay) {
      game_destroy(&checkers_game);
    }
    if (!ok) {
      game_ai_scored_move_list_free(&moves);
      task->explored_nodes += stats.nodes;
      g_autofree char *text = ggame_window_analysis_format_full_game_status(i + 1,
                                                                                task->jobs->len,
                                                                                task->explored_nodes,
                                                                                "no legal moves");
      ggame_window_analysis_publish_status(task->self,
                                               task->generation,
                                               GGAME_WINDOW_ANALYSIS_MODE_FULL_GAME,
                                               FALSE,
                                               FALSE,
                                               text);
      continue;
    }

    g_autoptr(SgfNodeAnalysis) analysis =
        ggame_window_analysis_from_scored_moves(task->backend, &moves, task->depth, &stats);
    game_ai_scored_move_list_free(&moves);
    task->explored_nodes += stats.nodes;
    if (analysis == NULL) {
      g_autofree char *text = ggame_window_analysis_format_full_game_status(i + 1,
                                                                                task->jobs->len,
                                                                                task->explored_nodes,
                                                                                "analysis payload failed");
      ggame_window_analysis_publish_status(task->self,
                                               task->generation,
                                               GGAME_WINDOW_ANALYSIS_MODE_FULL_GAME,
                                               FALSE,
                                               FALSE,
                                               text);
      continue;
    }

    g_autofree char *text =
        ggame_window_analysis_format_full_game_status(i + 1, task->jobs->len, task->explored_nodes, NULL);
    gint first_score = 0;
    gboolean has_first_score = FALSE;
    if (analysis->moves != NULL && analysis->moves->len > 0) {
      const SgfNodeScoredMove *entry = g_ptr_array_index(analysis->moves, 0);
      if (entry != NULL) {
        first_score = entry->score;
        has_first_score = TRUE;
      }
    }
    ggame_window_analysis_publish_payload(task->self,
                                              task->generation,
                                              GGAME_WINDOW_ANALYSIS_MODE_FULL_GAME,
                                              analysis,
                                              job->node);
    ggame_window_analysis_publish_status(task->self,
                                             task->generation,
                                             GGAME_WINDOW_ANALYSIS_MODE_FULL_GAME,
                                             FALSE,
                                             FALSE,
                                             text);
    (void)first_score;
    (void)has_first_score;
  }

  const gboolean canceled = ggame_window_should_cancel_full_analysis(task);
  ggame_window_analysis_publish_status(task->self,
                                           task->generation,
                                           GGAME_WINDOW_ANALYSIS_MODE_FULL_GAME,
                                           TRUE,
                                           canceled,
                                           canceled ? "Full-game analysis canceled." : NULL);

  g_ptr_array_unref(task->jobs);
  game_ai_tt_free(task->tt);
  g_object_unref(task->self);
  g_free(task);
  return NULL;
}

static void ggame_window_stop_analysis(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (self->analysis_mode == GGAME_WINDOW_ANALYSIS_MODE_NONE) {
    ggame_window_analysis_finish_session(self);
    return;
  }

  g_atomic_int_inc(&self->analysis_generation);

  g_mutex_lock(&self->analysis_report_mutex);
  if (self->analysis_report_queue != NULL) {
    g_queue_clear_full(self->analysis_report_queue, ggame_window_analysis_event_free);
  }
  g_mutex_unlock(&self->analysis_report_mutex);
  ggame_window_analysis_finish_session(self);
}

static void ggame_window_start_analysis(GGameWindow *self) {
  const GameBackend *backend = NULL;
  g_autofree guint8 *position = NULL;

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(self->sgf_controller));

  if (!ggame_window_copy_analysis_position(self, &backend, NULL, &position)) {
    g_debug("Failed to snapshot backend position for threaded analysis");
    return;
  }
  g_return_if_fail(backend != NULL);
  g_return_if_fail(backend->move_size > 0);

  SgfTree *tree = ggame_sgf_controller_get_tree(self->sgf_controller);
  if (tree == NULL) {
    g_debug("Missing SGF tree for analysis");
    return;
  }
  const SgfNode *target_node = sgf_tree_get_current(tree);
  if (target_node == NULL) {
    g_debug("Missing SGF current node for analysis");
    return;
  }

  gint generation = g_atomic_int_add(&self->analysis_generation, 1) + 1;
  ggame_window_analysis_begin_session(self, GGAME_WINDOW_ANALYSIS_MODE_CURRENT, 0);
  ggame_window_set_analysis_status(self, "Analyzing current position...");

  GGameWindowAnalysisTask *task = g_new0(GGameWindowAnalysisTask, 1);
  task->self = g_object_ref(self);
  task->backend = backend;
  task->position = g_steal_pointer(&position);
  task->generation = generation;
  task->target_node = target_node;
  task->target_depth = ggame_window_get_analysis_depth(self);
  task->tt = game_ai_tt_new(GGAME_WINDOW_ANALYSIS_TT_SIZE_MB, backend->move_size);
  if (task->tt == NULL) {
    g_debug("Failed to allocate analysis TT, continuing without TT caching");
  }
  GThread *thread = g_thread_new("analysis-thread", ggame_window_analysis_thread, task);
  g_thread_unref(thread);
}

static void ggame_window_start_full_game_analysis(GGameWindow *self) {
  const GGameAppProfile *profile = NULL;
  const GameBackend *backend = NULL;
  const GameBackendVariant *variant = NULL;
  const Game *checkers_game = NULL;
  const CheckersRules *checkers_rules = NULL;

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(self->sgf_controller));
  g_return_if_fail(self->controls_panel != NULL);

  SgfTree *tree = ggame_sgf_controller_get_tree(self->sgf_controller);
  if (tree == NULL) {
    g_debug("Missing SGF tree for full-game analysis");
    return;
  }

  g_autoptr(GPtrArray) jobs = ggame_window_build_full_analysis_jobs(tree);
  if (jobs == NULL) {
    g_debug("Failed to build full-game analysis jobs");
    return;
  }

  profile = ggame_window_get_profile(self);
  backend = ggame_window_get_game_backend(self);
  variant = ggame_window_get_variant(self);
  g_return_if_fail(profile != NULL);
  g_return_if_fail(backend != NULL);
  g_return_if_fail(backend->move_size > 0);
  if (backend->variant_count > 0 && variant == NULL) {
    g_debug("Missing active backend variant for full-game analysis");
    return;
  }

  if (profile->kind == GGAME_APP_KIND_CHECKERS) {
    checkers_game = ggame_window_get_checkers_game(self);
    if (checkers_game == NULL || checkers_game->rules == NULL) {
      g_debug("Missing checkers rules for full-game analysis");
      return;
    }
    checkers_rules = checkers_game->rules;
  }

  guint depth = ggame_window_get_analysis_depth(self);
  gint generation = g_atomic_int_add(&self->analysis_generation, 1) + 1;
  ggame_window_analysis_begin_session(self, GGAME_WINDOW_ANALYSIS_MODE_FULL_GAME, jobs->len);
  g_autofree char *initial_status = ggame_window_analysis_format_full_game_status(0, jobs->len, 0, NULL);
  if (initial_status == NULL) {
    g_debug("Failed to format initial full-game analysis status");
    return;
  }
  ggame_window_set_analysis_status(self, initial_status);

  GGameWindowFullAnalysisTask *task = g_new0(GGameWindowFullAnalysisTask, 1);
  task->self = g_object_ref(self);
  task->generation = generation;
  task->use_checkers_replay = profile->kind == GGAME_APP_KIND_CHECKERS;
  task->checkers_rules = checkers_rules;
  task->backend = backend;
  task->variant = variant;
  task->depth = depth;
  task->jobs = g_steal_pointer(&jobs);
  task->tt = game_ai_tt_new(GGAME_WINDOW_ANALYSIS_TT_SIZE_MB, backend->move_size);
  if (task->tt == NULL) {
    g_debug("Failed to allocate full analysis TT, continuing without TT caching");
  }

  GThread *thread = g_thread_new("full-analysis-thread", ggame_window_full_analysis_thread, task);
  g_thread_unref(thread);
}

static void ggame_window_update_control_state(GGameWindow *self) {
  const GameBackend *backend = NULL;
  gconstpointer position = NULL;
  GameBackendOutcome outcome = GAME_BACKEND_OUTCOME_ONGOING;

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(self->board_view != NULL);

  backend = ggame_window_get_game_backend(self);
  position = ggame_window_get_game_position(self);
  g_return_if_fail(backend != NULL);
  g_return_if_fail(position != NULL);
  g_return_if_fail(backend->position_outcome != NULL);

  outcome = backend->position_outcome(position);
  gboolean input_enabled = outcome == GAME_BACKEND_OUTCOME_ONGOING;
  board_view_set_input_enabled(self->board_view, input_enabled);
}

static void ggame_window_update_status(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_MODEL(self->game_model));

  board_view_update(self->board_view);
}

static gboolean ggame_window_auto_force_move_cb(gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);

  g_return_val_if_fail(GGAME_IS_WINDOW(self), G_SOURCE_REMOVE);

  self->auto_move_source_id = 0;
  ggame_window_force_move(self);
  return G_SOURCE_REMOVE;
}

static void ggame_window_schedule_auto_force_move(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (self->auto_move_source_id != 0) {
    return;
  }

  self->auto_move_source_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                                              ggame_window_auto_force_move_cb,
                                              g_object_ref(self),
                                              (GDestroyNotify)g_object_unref);
}

static void ggame_window_maybe_trigger_auto_move(GGameWindow *self) {
  const GameBackend *backend = NULL;
  gconstpointer position = NULL;

  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (ggame_window_is_edit_mode(self)) {
    return;
  }
  if (self->puzzle_mode) {
    return;
  }

  if (!self->sgf_controller) {
    g_debug("Missing SGF controller for auto move\n");
    return;
  }
  if (ggame_sgf_controller_is_replaying(self->sgf_controller)) {
    return;
  }

  backend = ggame_window_get_game_backend(self);
  position = ggame_window_get_game_position(self);
  if (backend == NULL || position == NULL) {
    return;
  }
  g_return_if_fail(backend->position_outcome != NULL);
  g_return_if_fail(backend->position_turn != NULL);

  if (backend->position_outcome(position) != GAME_BACKEND_OUTCOME_ONGOING) {
    return;
  }

  guint side = backend->position_turn(position);
  if (!ggame_window_is_computer_control(self, side)) {
    return;
  }

  ggame_window_schedule_auto_force_move(self);
}

static void ggame_window_on_state_changed(GGameModel *model, gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);

  g_return_if_fail(GGAME_IS_MODEL(model));
  g_return_if_fail(GGAME_IS_WINDOW(self));

  ggame_window_sync_board_orientation(self);
  ggame_window_update_status(self);
  ggame_window_update_control_state(self);
  ggame_window_maybe_trigger_auto_move(self);
  ggame_window_refresh_analysis_graph(self);
}

static void ggame_window_on_control_changed(PlayerControlsPanel * /*panel*/, gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);

  g_return_if_fail(GGAME_IS_WINDOW(self));

  ggame_window_sync_board_orientation(self);
  ggame_window_update_control_state(self);
}

static void ggame_window_on_mode_selected_notify(GObject * /*object*/,
                                                     GParamSpec * /*pspec*/,
                                                     gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GTK_IS_DROP_DOWN(self->sgf_mode_control));

  if (self->puzzle_mode) {
    gtk_drop_down_set_selected(self->sgf_mode_control, 0);
    return;
  }

  self->edit_mode_enabled = gtk_drop_down_get_selected(self->sgf_mode_control) == 1;
  board_view_clear_selection(self->board_view);
  ggame_window_sync_mode_ui(self);
}

static void ggame_window_on_default_size_notify(GObject *object,
                                                    GParamSpec *pspec,
                                                    gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GTK_IS_WINDOW(object));
  g_return_if_fail(pspec != NULL);

  if (self->syncing_layout_default_size) {
    return;
  }
  if (!self->puzzle_mode || self->layout_mode != GGAME_WINDOW_LAYOUT_MODE_PUZZLE) {
    return;
  }
  if (g_strcmp0(g_param_spec_get_name(pspec), "default-width") != 0) {
    return;
  }

  gint default_width = -1;
  gtk_window_get_default_size(GTK_WINDOW(self), &default_width, NULL);
  gint expected_width = ggame_window_expected_default_width(self);
  if (default_width == expected_width || default_width <= 0) {
    return;
  }

  ggame_window_apply_saved_panel_widths(self);
  gtk_widget_queue_allocate(GTK_WIDGET(self));
}

static void ggame_window_on_manual_requested(GGameSgfController * /*controller*/,
                                                 gpointer /*node*/,
                                                 gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(self->controls_panel != NULL);

  ggame_window_set_board_orientation_mode(self, GGAME_WINDOW_BOARD_ORIENTATION_FIXED);
  player_controls_panel_set_all_user(self->controls_panel);
  ggame_window_update_control_state(self);
  ggame_window_refresh_analysis_graph(self);
}

static void ggame_window_on_sgf_node_changed(GGameSgfController * /*controller*/,
                                                 const SgfNode *node,
                                                 gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(node != NULL);

  ggame_window_show_analysis_for_current_node(self);
  ggame_window_refresh_analysis_graph(self);
}

static void ggame_window_on_analysis_graph_node_activated(AnalysisGraph * /*graph*/,
                                                              const SgfNode *node,
                                                              gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(node != NULL);

  if (ggame_window_is_edit_mode(self)) {
    return;
  }

  if (!ggame_sgf_controller_select_node(self->sgf_controller, node)) {
    g_debug("Failed to select SGF node from analysis graph");
  }
}

static void ggame_window_on_analyze_current_position_action(GSimpleAction * /*action*/,
                                                                GVariant * /*parameter*/,
                                                                gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);

  g_return_if_fail(GGAME_IS_WINDOW(self));
  if (!ggame_window_get_profile(self)->features.supports_analysis) {
    return;
  }

  ggame_window_stop_analysis(self);
  ggame_window_start_analysis(self);
}

static void ggame_window_on_analyze_full_game_action(GSimpleAction * /*action*/,
                                                         GVariant * /*parameter*/,
                                                         gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  if (!ggame_window_get_profile(self)->features.supports_analysis) {
    return;
  }

  ggame_window_stop_analysis(self);
  ggame_window_start_full_game_analysis(self);
}

static void ggame_window_on_force_move_action(GSimpleAction * /*action*/,
                                                  GVariant * /*parameter*/,
                                                  gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));

  ggame_window_force_move(self);
}

static void ggame_window_on_play_puzzles_action(GSimpleAction * /*action*/,
                                                    GVariant * /*parameter*/,
                                                    gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  if (!ggame_window_get_profile(self)->features.supports_puzzles) {
    return;
  }

  ggame_window_present_puzzle_dialog(self);
}

static void ggame_window_on_puzzle_next_clicked(GtkButton * /*button*/, gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (!self->puzzle_mode || !self->puzzle_finished) {
    return;
  }
  if (!ggame_window_start_next_puzzle_mode_for_ruleset(self, self->puzzle_ruleset)) {
    g_debug("Failed to load next puzzle");
  }
}

static void ggame_window_on_puzzle_analyze_clicked(GtkButton * /*button*/, gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(self->sgf_controller));

  if (!self->puzzle_mode) {
    return;
  }

  (void)ggame_window_puzzle_attempt_finish_analyze(self);
  ggame_window_leave_puzzle_mode(self, TRUE);
  if (!ggame_sgf_controller_rewind_to_start(self->sgf_controller)) {
    g_debug("Failed to rewind puzzle before analysis");
  }
  self->show_analysis_drawer = TRUE;
  ggame_window_sync_drawer_ui(self);
  ggame_window_stop_analysis(self);
  ggame_window_start_full_game_analysis(self);
}

static void ggame_window_on_sgf_rewind(GSimpleAction * /*action*/,
                                           GVariant * /*parameter*/,
                                           gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(self->sgf_controller));
  if (ggame_window_is_edit_mode(self)) {
    return;
  }

  if (!ggame_sgf_controller_rewind_to_start(self->sgf_controller)) {
    g_debug("SGF rewind ignored");
  }
}

static void ggame_window_on_sgf_step_backward(GSimpleAction * /*action*/,
                                                  GVariant * /*parameter*/,
                                                  gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(self->sgf_controller));
  if (ggame_window_is_edit_mode(self)) {
    return;
  }

  if (!ggame_sgf_controller_step_backward(self->sgf_controller)) {
    g_debug("SGF step backward ignored");
  }
}

static void ggame_window_on_sgf_step_forward(GSimpleAction * /*action*/,
                                                 GVariant * /*parameter*/,
                                                 gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(self->sgf_controller));
  if (ggame_window_is_edit_mode(self)) {
    return;
  }

  if (!ggame_sgf_controller_step_forward(self->sgf_controller)) {
    g_debug("SGF step forward ignored");
  }
}

static void ggame_window_on_sgf_step_forward_to_branch(GSimpleAction * /*action*/,
                                                           GVariant * /*parameter*/,
                                                           gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(self->sgf_controller));
  if (ggame_window_is_edit_mode(self)) {
    return;
  }

  if (!ggame_sgf_controller_step_forward_to_branch(self->sgf_controller)) {
    g_debug("SGF step forward to branch ignored");
  }
}

static void ggame_window_on_sgf_step_forward_to_end(GSimpleAction * /*action*/,
                                                        GVariant * /*parameter*/,
                                                        gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(self->sgf_controller));
  if (ggame_window_is_edit_mode(self)) {
    return;
  }

  if (!ggame_sgf_controller_step_forward_to_end(self->sgf_controller)) {
    g_debug("SGF step forward to end ignored");
  }
}

static GtkWidget *ggame_window_new_toolbar_action_button(const char *icon_name,
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

void ggame_window_force_move(GGameWindow *self) {
  const GameBackend *backend = NULL;
  gconstpointer position = NULL;
  guint configured_depth = 0;
  g_autofree guint8 *move = NULL;

  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (self->puzzle_mode) {
    g_debug("Ignoring forced move in puzzle mode");
    return;
  }

  if (ggame_window_is_edit_mode(self)) {
    return;
  }

  backend = ggame_window_get_game_backend(self);
  position = ggame_window_get_game_position(self);
  if (backend == NULL || position == NULL) {
    return;
  }
  g_return_if_fail(backend->position_outcome != NULL);

  if (backend->position_outcome(position) != GAME_BACKEND_OUTCOME_ONGOING) {
    g_debug("Ignoring forced move after game end\n");
    return;
  }
  if (ggame_sgf_controller_is_replaying(self->sgf_controller)) {
    g_debug("Ignoring forced move while replaying SGF\n");
    return;
  }

  g_return_if_fail(self->controls_panel != NULL);
  configured_depth = player_controls_panel_get_computer_depth(self->controls_panel);
  move = g_malloc0(backend->move_size);
  g_return_if_fail(move != NULL);
  if (!ggame_sgf_controller_step_ai_move(self->sgf_controller, configured_depth, move)) {
    g_debug("Failed to choose a forced move for the active backend");
  }
}

const GameBackendVariant *ggame_window_get_variant(GGameWindow *self) {
  const GameBackendVariant *variant = NULL;

  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);

  variant = self->game_model != NULL ? ggame_model_peek_variant(self->game_model) : NULL;
  if (variant != NULL) {
    return variant;
  }

  if (ggame_window_get_profile(self)->kind == GGAME_APP_KIND_CHECKERS) {
    return ggame_window_variant_for_ruleset(self->applied_ruleset);
  }

  return NULL;
}

void ggame_window_apply_new_game_settings(GGameWindow *self,
                                          const GameBackendVariant *variant,
                                              PlayerControlMode white_mode,
                                              PlayerControlMode black_mode,
                                              guint computer_depth) {
  const GameBackend *backend = NULL;

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(self->controls_panel != NULL);
  backend = ggame_window_get_game_backend(self);
  g_return_if_fail(backend != NULL);
  if (backend->variant_count > 0) {
    g_return_if_fail(variant != NULL);
  }

  player_controls_panel_set_mode(self->controls_panel, 0, white_mode);
  player_controls_panel_set_mode(self->controls_panel, 1, black_mode);
  player_controls_panel_set_computer_depth(self->controls_panel, computer_depth);

  ggame_window_set_board_bottom_color(self, CHECKERS_COLOR_WHITE);
  if (white_mode == PLAYER_CONTROL_MODE_USER && black_mode == PLAYER_CONTROL_MODE_USER) {
    ggame_window_set_board_orientation_mode(self, GGAME_WINDOW_BOARD_ORIENTATION_FOLLOW_TURN);
  } else if (white_mode != black_mode) {
    ggame_window_set_board_orientation_mode(self, GGAME_WINDOW_BOARD_ORIENTATION_FOLLOW_PLAYER);
  } else {
    ggame_window_set_board_orientation_mode(self, GGAME_WINDOW_BOARD_ORIENTATION_FIXED);
  }

  if (backend->variant_count > 0 && ggame_window_get_profile(self)->kind == GGAME_APP_KIND_CHECKERS) {
    PlayerRuleset ruleset = PLAYER_RULESET_INTERNATIONAL;
    if (!ggame_window_ruleset_from_variant(variant, &ruleset)) {
      return;
    }
    ggame_window_set_ruleset(self, ruleset);
  }
  ggame_window_start_new_game(self);
}

void ggame_window_set_board_orientation_mode(GGameWindow *self,
                                                 GGameWindowBoardOrientationMode mode) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(ggame_window_board_orientation_mode_valid(mode));

  if (self->board_orientation_mode == mode) {
    return;
  }

  self->board_orientation_mode = mode;
  ggame_window_sync_board_orientation(self);
}

void ggame_window_set_board_bottom_color(GGameWindow *self, CheckersColor bottom_color) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(bottom_color == CHECKERS_COLOR_WHITE || bottom_color == CHECKERS_COLOR_BLACK);

  self->board_bottom_color = bottom_color;
  ggame_window_sync_board_orientation(self);
}

CheckersColor ggame_window_get_board_bottom_color(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), CHECKERS_COLOR_WHITE);

  return self->board_bottom_color;
}

static void ggame_window_set_model(GGameWindow *self, GGameModel *model) {
  const GameBackendVariant *variant = NULL;

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_MODEL(model));

  g_clear_handle_id(&self->auto_move_source_id, g_source_remove);

  if (self->game_model != NULL && self->state_handler_id != 0) {
    g_signal_handler_disconnect(self->game_model, self->state_handler_id);
    self->state_handler_id = 0;
  }
  g_clear_object(&self->game_model);

  self->game_model = g_object_ref(model);
  self->state_handler_id = g_signal_connect(self->game_model,
                                            "state-changed",
                                            G_CALLBACK(ggame_window_on_state_changed),
                                            self);
  board_view_set_model(self->board_view, self->game_model);
  ggame_sgf_controller_set_game_model(self->sgf_controller, self->game_model);
  ggame_window_rebuild_board_host(self);
  variant = ggame_model_peek_variant(self->game_model);
  if (variant != NULL && self->profile != NULL && self->profile->kind == GGAME_APP_KIND_CHECKERS) {
    (void)ggame_window_ruleset_from_variant(variant, &self->applied_ruleset);
  }
  ggame_window_sync_board_orientation(self);
  ggame_window_update_status(self);
  ggame_window_update_control_state(self);
  ggame_window_refresh_analysis_graph(self);
}

static void ggame_window_rebuild_board_host(GGameWindow *self) {
  GtkWidget *host = NULL;
  GtkWidget *board_widget = NULL;

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(self->board_view != NULL);
  g_return_if_fail(self->board_host_box != NULL);
  g_return_if_fail(GGAME_IS_MODEL(self->game_model));

  board_widget = board_view_get_widget(self->board_view);
  g_return_if_fail(GTK_IS_WIDGET(board_widget));

  if (self->board_host != NULL) {
    ggame_widget_remove_from_parent(self->board_host);
    self->board_host = NULL;
  }
  if (gtk_widget_get_parent(board_widget) != NULL) {
    ggame_widget_remove_from_parent(board_widget);
  }

  if (self->profile != NULL && self->profile->ui.create_board_host != NULL) {
    host = self->profile->ui.create_board_host(self->game_model, self->board_view);
    g_return_if_fail(GTK_IS_WIDGET(host));
  } else {
    host = gtk_aspect_frame_new(0.5f, 0.5f, 1.0f, FALSE);
    gtk_widget_set_hexpand(host, TRUE);
    gtk_widget_set_vexpand(host, TRUE);
    gtk_aspect_frame_set_child(GTK_ASPECT_FRAME(host), board_widget);
  }

  self->board_host = host;
  gtk_box_append(GTK_BOX(self->board_host_box), host);
}

static gboolean ggame_window_unparent_controls_panel(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  if (!self->controls_panel) {
    return TRUE;
  }

  GtkWidget *panel_widget = GTK_WIDGET(self->controls_panel);
  gboolean removed = ggame_widget_remove_from_parent(panel_widget);
  if (!removed && gtk_widget_get_parent(panel_widget)) {
    g_debug("Failed to remove controls panel from parent during dispose\n");
    return FALSE;
  }

  return TRUE;
}

static void ggame_window_dispose(GObject *object) {
  GGameWindow *self = GGAME_WINDOW(object);

  self->edit_mode_enabled = FALSE;
  ggame_window_sync_mode_ui(self);

  if (self->game_model != NULL && self->state_handler_id != 0) {
    g_signal_handler_disconnect(self->game_model, self->state_handler_id);
    self->state_handler_id = 0;
  }

  gboolean panel_removed = ggame_window_unparent_controls_panel(self);
  g_clear_handle_id(&self->auto_move_source_id, g_source_remove);
  g_clear_handle_id(&self->puzzle_wrong_move_source_id, g_source_remove);
  if (self->puzzle_attempt_started && !ggame_window_puzzle_attempt_is_terminal(self)) {
    (void)ggame_window_puzzle_attempt_finish_failure(self, FALSE, NULL);
  }

  ggame_window_unparent_controls_panel(self);
  self->analysis_status_label = NULL;
  self->analysis_buffer = NULL;

  ggame_window_stop_analysis(self);
  if (self->paned_tick_id != 0 && self->main_paned) {
    gtk_widget_remove_tick_callback(self->main_paned, self->paned_tick_id);
    self->paned_tick_id = 0;
  }
  g_clear_object(&self->sgf_controller);
  g_clear_object(&self->analysis_graph);
  if (panel_removed) {
    g_clear_object(&self->controls_panel);
  } else {
    self->controls_panel = NULL;
  }
  if (self->navigation_panel != NULL) {
    ggame_widget_remove_from_parent(self->navigation_panel);
    g_clear_object(&self->navigation_panel);
  }
  if (self->analysis_panel != NULL) {
    ggame_widget_remove_from_parent(self->analysis_panel);
    g_clear_object(&self->analysis_panel);
  }
  g_clear_pointer(&self->loaded_source_name, g_free);
  if (self->puzzle_steps != NULL) {
    g_array_unref(self->puzzle_steps);
    self->puzzle_steps = NULL;
  }
  if (self->drawer_split != NULL) {
    ggame_widget_remove_from_parent(self->drawer_split);
    g_clear_object(&self->drawer_split);
  }
  if (self->drawer_host != NULL) {
    ggame_widget_remove_from_parent(self->drawer_host);
    g_clear_object(&self->drawer_host);
  }
  if (self->board_host != NULL) {
    ggame_widget_remove_from_parent(self->board_host);
    self->board_host = NULL;
  }
  g_clear_object(&self->board_view);
  g_clear_object(&self->game_model);
  ggame_window_puzzle_attempt_reset(self);
  g_clear_pointer(&self->puzzle_path, g_free);
  self->puzzle_progress_store = NULL;
  self->main_paned = NULL;
  self->board_panel = NULL;
  self->board_host_box = NULL;
  self->puzzle_panel = NULL;
  self->puzzle_message_label = NULL;
  self->puzzle_next_button = NULL;
  self->puzzle_analyze_button = NULL;
  self->analysis_depth_scale = NULL;
  self->analysis_buffer = NULL;
  self->sgf_mode_control = NULL;
  G_OBJECT_CLASS(ggame_window_parent_class)->dispose(object);
}

static void ggame_window_finalize(GObject *object) {
  GGameWindow *self = GGAME_WINDOW(object);

  if (self->analysis_report_queue != NULL) {
    g_queue_free_full(self->analysis_report_queue, ggame_window_analysis_event_free);
    self->analysis_report_queue = NULL;
  }
  g_mutex_clear(&self->analysis_report_mutex);

  G_OBJECT_CLASS(ggame_window_parent_class)->finalize(object);
}

static void ggame_window_class_init(GGameWindowClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = ggame_window_dispose;
  object_class->finalize = ggame_window_finalize;
}

static void ggame_window_init(GGameWindow *self) {
  const GGameAppLayout *layout = NULL;

  self->profile = ggame_active_app_profile();
  layout = self->profile != NULL ? &self->profile->layout : NULL;
  self->auto_move_source_id = 0;
  self->paned_tick_id = 0;
  self->analysis_mode = GGAME_WINDOW_ANALYSIS_MODE_NONE;
  self->analysis_generation = 1;
  self->puzzle_wrong_move_source_id = 0;
  self->syncing_layout_default_size = FALSE;
  g_mutex_init(&self->analysis_report_mutex);
  self->analysis_report_queue = g_queue_new();
  ggame_window_analysis_reset_runtime_state(self);
  self->applied_ruleset = PLAYER_RULESET_INTERNATIONAL;

  static const GActionEntry window_actions[] = {
      {
          .name = "game-force-move",
          .activate = ggame_window_on_force_move_action,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "puzzle-play",
          .activate = ggame_window_on_play_puzzles_action,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "navigation-rewind",
          .activate = ggame_window_on_sgf_rewind,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "navigation-step-backward",
          .activate = ggame_window_on_sgf_step_backward,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "navigation-step-forward",
          .activate = ggame_window_on_sgf_step_forward,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "navigation-step-forward-to-branch",
          .activate = ggame_window_on_sgf_step_forward_to_branch,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "navigation-step-forward-to-end",
          .activate = ggame_window_on_sgf_step_forward_to_end,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "view-show-navigation-drawer",
          .activate = NULL,
          .parameter_type = NULL,
          .state = "true",
          .change_state = ggame_window_on_show_navigation_drawer_change_state,
          .padding = {0},
      },
      {
          .name = "view-show-analysis-drawer",
          .activate = NULL,
          .parameter_type = NULL,
          .state = "true",
          .change_state = ggame_window_on_show_analysis_drawer_change_state,
          .padding = {0},
      },
      {
          .name = "analysis-current-position",
          .activate = ggame_window_on_analyze_current_position_action,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "analysis-whole-game",
          .activate = ggame_window_on_analyze_full_game_action,
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
  ggame_window_install_sgf_file_actions(self);
  ggame_window_sync_title(self);
  gtk_window_set_default_size(GTK_WINDOW(self), 1100, 700);

  ggame_style_init();

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
      ggame_window_new_toolbar_action_button("document-new-symbolic", "New game...", "app.new-game");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), new_game_button);

  GtkWidget *force_move_button =
      ggame_window_new_toolbar_action_button("media-playback-start-symbolic",
                                                 "Force move",
                                                 "win.game-force-move");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), force_move_button);

  GtkWidget *toolbar_separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), toolbar_separator);

  GtkWidget *rewind_button = ggame_window_new_toolbar_action_button("media-skip-backward-symbolic",
                                                                         "Rewind to start",
                                                                         "win.navigation-rewind");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), rewind_button);

  GtkWidget *step_backward_button =
      ggame_window_new_toolbar_action_button("go-previous-symbolic",
                                                 "Back one move",
                                                 "win.navigation-step-backward");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), step_backward_button);

  GtkWidget *step_forward_button = ggame_window_new_toolbar_action_button("go-next-symbolic",
                                                                               "Forward one move",
                                                                               "win.navigation-step-forward");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), step_forward_button);

  GtkWidget *step_to_branch_button =
      ggame_window_new_toolbar_action_button("media-seek-forward-symbolic",
                                                 "Forward to next branch point",
                                                 "win.navigation-step-forward-to-branch");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), step_to_branch_button);

  GtkWidget *step_to_end_button = ggame_window_new_toolbar_action_button("media-skip-forward-symbolic",
                                                                              "Forward to main line end",
                                                                              "win.navigation-step-forward-to-end");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), step_to_end_button);
  gtk_box_append(GTK_BOX(content), toolbar);

  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_hexpand(paned, TRUE);
  gtk_widget_set_vexpand(paned, TRUE);
  gtk_box_append(GTK_BOX(content), paned);
  self->main_paned = paned;
  self->paned_tick_id = gtk_widget_add_tick_callback(paned,
                                                      ggame_window_constrain_main_split_cb,
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
  self->board_panel = left_panel;
  g_object_set_data(G_OBJECT(self), "board-panel", left_panel);

  self->controls_panel = g_object_ref_sink(player_controls_panel_new());
  ggame_window_sync_side_labels(self);
  gtk_box_append(GTK_BOX(left_panel), GTK_WIDGET(self->controls_panel));
  g_signal_connect(self->controls_panel,
                   "control-changed",
                   G_CALLBACK(ggame_window_on_control_changed),
                   self);

  GtkWidget *puzzle_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_bottom(puzzle_panel, 8);
  gtk_widget_set_visible(puzzle_panel, FALSE);
  gtk_box_append(GTK_BOX(left_panel), puzzle_panel);
  self->puzzle_panel = puzzle_panel;
  g_object_set_data(G_OBJECT(self), "puzzle-panel", puzzle_panel);

  GtkWidget *puzzle_title = gtk_label_new("Puzzle mode");
  gtk_widget_set_halign(puzzle_title, GTK_ALIGN_START);
  gtk_widget_add_css_class(puzzle_title, "title-4");
  gtk_box_append(GTK_BOX(puzzle_panel), puzzle_title);

  GtkWidget *puzzle_message = gtk_label_new("");
  gtk_label_set_wrap(GTK_LABEL(puzzle_message), TRUE);
  gtk_widget_set_halign(puzzle_message, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(puzzle_panel), puzzle_message);
  self->puzzle_message_label = GTK_LABEL(puzzle_message);
  g_object_set_data(G_OBJECT(self), "puzzle-message-label", puzzle_message);

  GtkWidget *puzzle_button_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append(GTK_BOX(puzzle_panel), puzzle_button_row);

  GtkWidget *puzzle_next_button = gtk_button_new_with_label("Next puzzle");
  g_signal_connect(puzzle_next_button,
                   "clicked",
                   G_CALLBACK(ggame_window_on_puzzle_next_clicked),
                   self);
  gtk_box_append(GTK_BOX(puzzle_button_row), puzzle_next_button);
  self->puzzle_next_button = GTK_BUTTON(puzzle_next_button);
  g_object_set_data(G_OBJECT(self), "puzzle-next-button", puzzle_next_button);

  GtkWidget *puzzle_analyze_button = gtk_button_new_with_label("Analyze");
  g_signal_connect(puzzle_analyze_button,
                   "clicked",
                   G_CALLBACK(ggame_window_on_puzzle_analyze_clicked),
                   self);
  gtk_box_append(GTK_BOX(puzzle_button_row), puzzle_analyze_button);
  self->puzzle_analyze_button = GTK_BUTTON(puzzle_analyze_button);
  g_object_set_data(G_OBJECT(self), "puzzle-analyze-button", puzzle_analyze_button);

  self->board_view = board_view_new();
  GtkWidget *board_host_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(board_host_box, TRUE);
  gtk_widget_set_vexpand(board_host_box, TRUE);
  gtk_box_append(GTK_BOX(left_panel), board_host_box);
  self->board_host_box = board_host_box;
  g_object_set_data(G_OBJECT(self), "board-host-box", board_host_box);

  GtkWidget *right_split = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  g_object_ref_sink(right_split);
  gtk_widget_set_hexpand(right_split, TRUE);
  gtk_widget_set_vexpand(right_split, TRUE);
  self->drawer_split = right_split;
  g_object_set_data(G_OBJECT(self), "drawer-split", right_split);

  GtkWidget *drawer_host = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  g_object_ref_sink(drawer_host);
  gtk_widget_set_hexpand(drawer_host, TRUE);
  gtk_widget_set_vexpand(drawer_host, TRUE);
  gtk_paned_set_end_child(GTK_PANED(paned), drawer_host);
  gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
  self->drawer_host = drawer_host;
  g_object_set_data(G_OBJECT(self), "drawer-host", drawer_host);

  GtkWidget *middle_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  g_object_ref_sink(middle_panel);
  gtk_widget_set_hexpand(middle_panel, TRUE);
  gtk_widget_set_vexpand(middle_panel, TRUE);
  gtk_widget_set_margin_top(middle_panel, 8);
  gtk_widget_set_margin_bottom(middle_panel, 8);
  gtk_widget_set_margin_start(middle_panel, 8);
  gtk_widget_set_margin_end(middle_panel, 8);
  gtk_paned_set_start_child(GTK_PANED(right_split), middle_panel);
  gtk_paned_set_shrink_start_child(GTK_PANED(right_split), FALSE);
  self->navigation_panel = middle_panel;
  g_object_set_data(G_OBJECT(self), "navigation-panel", middle_panel);

  GtkWidget *analysis_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  g_object_ref_sink(analysis_panel);
  gtk_widget_set_hexpand(analysis_panel, TRUE);
  gtk_widget_set_vexpand(analysis_panel, TRUE);
  gtk_widget_set_margin_top(analysis_panel, 8);
  gtk_widget_set_margin_bottom(analysis_panel, 8);
  gtk_widget_set_margin_start(analysis_panel, 8);
  gtk_widget_set_margin_end(analysis_panel, 8);
  gtk_paned_set_end_child(GTK_PANED(right_split), analysis_panel);
  gtk_paned_set_shrink_end_child(GTK_PANED(right_split), FALSE);
  self->analysis_panel = analysis_panel;
  g_object_set_data(G_OBJECT(self), "analysis-panel", analysis_panel);
  gtk_paned_set_position(GTK_PANED(paned), 500);
  gtk_paned_set_position(GTK_PANED(right_split), 300);

  GtkWidget *sgf_mode_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *sgf_mode_label = gtk_label_new("Mode");
  gtk_widget_set_halign(sgf_mode_label, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(sgf_mode_row), sgf_mode_label);
  self->sgf_mode_control = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(
      (const char *[]){"Play", "Edit", NULL}));
  gtk_drop_down_set_selected(self->sgf_mode_control, 0);
  g_signal_connect(self->sgf_mode_control,
                   "notify::selected",
                   G_CALLBACK(ggame_window_on_mode_selected_notify),
                   self);
  g_signal_connect(self,
                   "notify::default-width",
                   G_CALLBACK(ggame_window_on_default_size_notify),
                   self);
  gtk_box_append(GTK_BOX(sgf_mode_row), GTK_WIDGET(self->sgf_mode_control));
  gtk_box_append(GTK_BOX(middle_panel), sgf_mode_row);

  self->sgf_controller = ggame_sgf_controller_new(self->board_view);
  board_view_set_sgf_controller(self->board_view, self->sgf_controller);
  board_view_set_move_handler(self->board_view, ggame_window_apply_player_move, self);
  board_view_set_square_handler(self->board_view, ggame_window_on_board_square_action, self);
  self->analysis_graph = analysis_graph_new();
  GtkWidget *sgf_widget = ggame_sgf_controller_get_widget(self->sgf_controller);
  g_return_if_fail(sgf_widget != NULL);
  g_signal_connect(self->sgf_controller,
                   "manual-requested",
                   G_CALLBACK(ggame_window_on_manual_requested),
                   self);
  g_signal_connect(self->sgf_controller,
                   "node-changed",
                   G_CALLBACK(ggame_window_on_sgf_node_changed),
                   self);
  gtk_widget_add_css_class(sgf_widget, "sgf-panel");
  gtk_box_append(GTK_BOX(middle_panel), sgf_widget);

  GtkWidget *analysis_depth_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_box_append(GTK_BOX(analysis_panel), analysis_depth_box);

  GtkWidget *analysis_depth_label = gtk_label_new("Analysis depth");
  gtk_widget_set_halign(analysis_depth_label, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(analysis_depth_box), analysis_depth_label);

  self->analysis_depth_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                                  GGAME_WINDOW_ANALYSIS_DEPTH_MIN,
                                                                  GGAME_WINDOW_ANALYSIS_DEPTH_MAX,
                                                                  1));
  gtk_scale_set_digits(self->analysis_depth_scale, 0);
  gtk_scale_set_draw_value(self->analysis_depth_scale, TRUE);
  gtk_widget_set_hexpand(GTK_WIDGET(self->analysis_depth_scale), TRUE);
  gtk_widget_set_size_request(GTK_WIDGET(self->analysis_depth_scale), 100, -1);
  gtk_box_append(GTK_BOX(analysis_depth_box), GTK_WIDGET(self->analysis_depth_scale));
  g_object_set_data(G_OBJECT(self), "analysis-depth-scale", self->analysis_depth_scale);
  ggame_window_set_analysis_depth(self, GGAME_WINDOW_ANALYSIS_DEPTH_DEFAULT);

  GtkWidget *graph_widget = analysis_graph_get_widget(self->analysis_graph);
  g_return_if_fail(graph_widget != NULL);
  g_signal_connect(self->analysis_graph,
                   "node-activated",
                   G_CALLBACK(ggame_window_on_analysis_graph_node_activated),
                   self);
  g_object_set_data(G_OBJECT(self), "analysis-graph", self->analysis_graph);
  gtk_box_append(GTK_BOX(analysis_panel), graph_widget);

  GtkWidget *analysis_status = gtk_label_new("");
  gtk_widget_add_css_class(analysis_status, "analysis-status");
  gtk_widget_set_halign(analysis_status, GTK_ALIGN_START);
  gtk_label_set_wrap(GTK_LABEL(analysis_status), TRUE);
  gtk_label_set_xalign(GTK_LABEL(analysis_status), 0.0f);
  gtk_box_append(GTK_BOX(analysis_panel), analysis_status);
  self->analysis_status_label = GTK_LABEL(analysis_status);
  g_object_set_data(G_OBJECT(self), "analysis-status-label", analysis_status);

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
  self->edit_mode_enabled = FALSE;
  self->show_navigation_drawer =
      layout != NULL ? layout->show_navigation_drawer_by_default : TRUE;
  self->show_analysis_drawer =
      layout != NULL ? layout->show_analysis_drawer_by_default : TRUE;
  self->layout_mode = GGAME_WINDOW_LAYOUT_MODE_NORMAL;
  self->puzzle_saved_show_navigation_drawer = self->show_navigation_drawer;
  self->puzzle_saved_show_analysis_drawer = self->show_analysis_drawer;
  self->board_panel_width =
      layout != NULL && layout->default_board_panel_width > 0 ? layout->default_board_panel_width
                                                              : GGAME_WINDOW_DEFAULT_BOARD_PANEL_WIDTH;
  self->navigation_panel_width =
      layout != NULL && layout->default_navigation_panel_width > 0 ? layout->default_navigation_panel_width
                                                                   : GGAME_WINDOW_DEFAULT_NAVIGATION_PANEL_WIDTH;
  self->analysis_panel_width =
      layout != NULL && layout->default_analysis_panel_width > 0 ? layout->default_analysis_panel_width
                                                                 : GGAME_WINDOW_DEFAULT_ANALYSIS_PANEL_WIDTH;
  self->extra_width = 0;
  self->puzzle_board_panel_width = self->board_panel_width;
  self->puzzle_navigation_panel_width = self->navigation_panel_width;
  self->puzzle_analysis_panel_width = self->analysis_panel_width;
  self->puzzle_extra_width = 0;
  self->board_orientation_mode = GGAME_WINDOW_BOARD_ORIENTATION_FIXED;
  self->board_bottom_color = CHECKERS_COLOR_WHITE;
  self->puzzle_ruleset = PLAYER_RULESET_INTERNATIONAL;
  self->puzzle_attacker = CHECKERS_COLOR_WHITE;
  self->puzzle_number = 0;
  self->puzzle_attempt_started = FALSE;
  self->puzzle_attempt_made_player_move = FALSE;
  ggame_window_sync_drawer_action_states(self);
  ggame_window_sync_drawer_ui(self);
  ggame_window_sync_puzzle_ui(self);
  ggame_window_sync_mode_ui(self);
  ggame_window_analysis_sync_ui(self);
  ggame_window_show_analysis_for_current_node(self);
  ggame_window_refresh_analysis_graph(self);
}

PlayerControlsPanel *ggame_window_get_controls_panel(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);

  if (!self->controls_panel) {
    g_debug("Missing controls panel\n");
    return NULL;
  }

  return self->controls_panel;
}

GGameSgfController *ggame_window_get_sgf_controller(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);

  if (!self->sgf_controller) {
    g_debug("Missing SGF controller\n");
    return NULL;
  }

  return self->sgf_controller;
}

void ggame_window_set_loaded_source_path(GGameWindow *self, const char *path) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  g_clear_pointer(&self->loaded_source_name, g_free);
  if (path != NULL && *path != '\0') {
    self->loaded_source_name = g_path_get_basename(path);
  }
  ggame_window_sync_title(self);
}

GGameWindow *ggame_window_new(GtkApplication *app, GGameModel *model) {
  g_return_val_if_fail(GTK_IS_APPLICATION(app), NULL);
  g_return_val_if_fail(GGAME_IS_MODEL(model), NULL);

  GGameWindow *window = g_object_new(GGAME_TYPE_WINDOW, "application", app, NULL);
  if (GGAME_IS_APPLICATION(app) && window->profile != NULL && window->profile->features.supports_puzzles) {
    window->puzzle_progress_store =
        ggame_application_get_puzzle_progress_store(GGAME_APPLICATION(app));
  }
  ggame_window_set_model(window, model);
  return window;
}
