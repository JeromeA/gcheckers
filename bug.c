/*
 * This crash reproducer embeds copies of src/game_app_profile.c, src/game_model.c,
 * src/games/homeworlds/homeworlds_backend.c, and src/window.c below. Do not link those original source files again
 * or the reproducer stops proving that it is self-contained. Do not remove this comment when simplifying bug.c.
 */
#include <gtk/gtk.h>

/* Begin copied file: src/game_app_profile.c */
#include "game_app_profile.h"

#include "games/homeworlds/homeworlds_backend.h"
#include "games/homeworlds/homeworlds_view.h"
#include "player_controls_panel.h"

static const GGameAppProfile homeworlds_app_profile = {
  .kind = GGAME_APP_KIND_HOMEWORLDS,
  .id = "homeworlds",
  .app_id = "io.github.jeromea.ghomeworlds",
  .display_name = "Homeworlds",
  .window_title_name = "ghomeworlds",
  .settings_schema_id = NULL,
  .backend = &homeworlds_game_backend,
  .features =
      {
          .supports_ai_players = FALSE,
          .supports_analysis = FALSE,
          .supports_puzzles = FALSE,
          .supports_import = FALSE,
          .supports_settings = FALSE,
          .supports_save_position = FALSE,
          .supports_edit_mode = FALSE,
      },
  .import =
      {
          .board_game_arena_game_id = 1515,
          .show_site_step = FALSE,
      },
  .ui =
      {
          .create_board_host = homeworlds_view_create_board_host,
          .sync_board_host_node = homeworlds_view_sync_board_host_node,
          .set_move_report_enabled = homeworlds_view_set_board_host_move_report_enabled,
      },
  .layout =
      {
          .default_board_panel_width = 960,
          .minimum_board_panel_width = 760,
          .default_navigation_panel_width = 300,
          .default_analysis_panel_width = 300,
          .show_navigation_drawer_by_default = TRUE,
          .show_analysis_drawer_by_default = FALSE,
      },
  .default_computer_depth = PLAYER_COMPUTER_DEPTH_MIN,
};

static const GGameAppProfile *active_app_profile = &homeworlds_app_profile;

const GGameAppProfile *ggame_app_profile_lookup_by_id(const char *id) {
  g_return_val_if_fail(id != NULL, NULL);

  if (g_strcmp0(id, homeworlds_app_profile.id) == 0) {
    return &homeworlds_app_profile;
  }

  g_debug("Unknown app profile id %s", id);
  return NULL;
}

const GGameAppProfile *ggame_active_app_profile(void) {
  return active_app_profile;
}

gboolean ggame_app_profile_supports_puzzle_catalog(const GGameAppProfile *profile) {
  g_return_val_if_fail(profile != NULL, FALSE);

  return FALSE;
}
/* End copied file: src/game_app_profile.c */

/* Begin copied file: src/game_model.c */
#include "game_model.h"

#include <glib.h>

struct _GGameModel {
  GObject parent_instance;
  const GameBackend *backend;
  gpointer position;
  const GameBackendVariant *variant;
};

G_DEFINE_TYPE(GGameModel, ggame_model, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_BACKEND,
  PROP_LAST,
};

enum {
  SIGNAL_STATE_CHANGED,
  SIGNAL_LAST,
};

static guint model_signals[SIGNAL_LAST] = {0};
static GParamSpec *properties[PROP_LAST] = {0};

static const GameBackendVariant *ggame_model_pick_initial_variant(const GameBackend *backend) {
  g_return_val_if_fail(backend != NULL, NULL);

  if (backend->variant_count == 0) {
    return NULL;
  }

  g_return_val_if_fail(backend->variant_at != NULL, NULL);

  return backend->variant_at(0);
}

static void ggame_model_emit_state_changed(GGameModel *self) {
  g_return_if_fail(GGAME_IS_MODEL(self));

  g_signal_emit(self, model_signals[SIGNAL_STATE_CHANGED], 0);
}

static void ggame_model_finalize(GObject *object) {
  GGameModel *self = GGAME_MODEL(object);

  if (self->position != NULL && self->backend != NULL && self->backend->position_clear != NULL) {
    self->backend->position_clear(self->position);
  }
  g_clear_pointer(&self->position, g_free);

  G_OBJECT_CLASS(ggame_model_parent_class)->finalize(object);
}

static void ggame_model_constructed(GObject *object) {
  GGameModel *self = GGAME_MODEL(object);
  gboolean has_move_list_api = FALSE;
  gboolean has_move_builder_api = FALSE;
  gboolean has_good_move_api = FALSE;

  G_OBJECT_CLASS(ggame_model_parent_class)->constructed(object);

  g_return_if_fail(self->backend != NULL);
  g_return_if_fail(self->backend->position_size > 0);
  g_return_if_fail(self->backend->position_init != NULL);
  g_return_if_fail(self->backend->position_clear != NULL);
  g_return_if_fail(self->backend->apply_move != NULL);

  has_move_list_api = self->backend->supports_move_list &&
                      self->backend->list_moves != NULL &&
                      self->backend->move_list_free != NULL &&
                      self->backend->move_list_get != NULL &&
                      self->backend->moves_equal != NULL;
  has_move_builder_api = self->backend->supports_move_builder &&
                         self->backend->move_list_free != NULL &&
                         self->backend->move_list_get != NULL &&
                         self->backend->moves_equal != NULL &&
                         self->backend->move_builder_init != NULL &&
                         self->backend->move_builder_clear != NULL &&
                         self->backend->move_builder_list_candidates != NULL &&
                         self->backend->move_builder_step != NULL &&
                         self->backend->move_builder_is_complete != NULL &&
                         self->backend->move_builder_build_move != NULL;
  has_good_move_api = self->backend->list_good_moves != NULL &&
                      self->backend->move_list_free != NULL &&
                      self->backend->move_list_get != NULL &&
                      self->backend->moves_equal != NULL;
  g_return_if_fail(has_move_list_api || has_move_builder_api);
  if (self->backend->supports_ai_search) {
    g_return_if_fail(has_good_move_api);
    g_return_if_fail(self->backend->evaluate_static != NULL);
    g_return_if_fail(self->backend->terminal_score != NULL);
    g_return_if_fail(self->backend->hash_position != NULL);
  }

  self->position = g_malloc0(self->backend->position_size);
  g_return_if_fail(self->position != NULL);

  self->variant = ggame_model_pick_initial_variant(self->backend);
  self->backend->position_init(self->position, self->variant);
}

static void ggame_model_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
  GGameModel *self = GGAME_MODEL(object);

  switch (property_id) {
    case PROP_BACKEND:
      self->backend = g_value_get_pointer(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void ggame_model_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
  GGameModel *self = GGAME_MODEL(object);

  switch (property_id) {
    case PROP_BACKEND:
      g_value_set_pointer(value, (gpointer) self->backend);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void ggame_model_class_init(GGameModelClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->constructed = ggame_model_constructed;
  object_class->finalize = ggame_model_finalize;
  object_class->set_property = ggame_model_set_property;
  object_class->get_property = ggame_model_get_property;

  properties[PROP_BACKEND] = g_param_spec_pointer("backend",
                                                  "Backend",
                                                  "Active backend used by this model.",
                                                  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS);
  g_object_class_install_property(object_class, PROP_BACKEND, properties[PROP_BACKEND]);

  model_signals[SIGNAL_STATE_CHANGED] = g_signal_new("state-changed",
                                                     G_TYPE_FROM_CLASS(klass),
                                                     G_SIGNAL_RUN_LAST,
                                                     0,
                                                     NULL,
                                                     NULL,
                                                     NULL,
                                                     G_TYPE_NONE,
                                                     0);
}

static void ggame_model_init(GGameModel *self) {
  self->backend = NULL;
  self->position = NULL;
  self->variant = NULL;
}

GGameModel *ggame_model_new(const GameBackend *backend) {
  g_return_val_if_fail(backend != NULL, NULL);

  return g_object_new(GGAME_TYPE_MODEL, "backend", backend, NULL);
}

void ggame_model_reset(GGameModel *self, const GameBackendVariant *variant_or_null) {
  g_return_if_fail(GGAME_IS_MODEL(self));
  g_return_if_fail(self->backend != NULL);
  g_return_if_fail(self->position != NULL);

  self->backend->position_clear(self->position);
  self->variant = variant_or_null != NULL ? variant_or_null : ggame_model_pick_initial_variant(self->backend);
  self->backend->position_init(self->position, self->variant);
  ggame_model_emit_state_changed(self);
}

gboolean ggame_model_set_position(GGameModel *self, gconstpointer position) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), FALSE);

  return ggame_model_set_position_variant(self, position, self->variant);
}

gboolean ggame_model_set_position_variant(GGameModel *self,
                                          gconstpointer position,
                                          const GameBackendVariant *variant_or_null) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), FALSE);
  g_return_val_if_fail(self->backend != NULL, FALSE);
  g_return_val_if_fail(self->position != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(self->backend->position_copy != NULL, FALSE);

  self->variant = variant_or_null != NULL ? variant_or_null : ggame_model_pick_initial_variant(self->backend);
  self->backend->position_copy(self->position, position);
  ggame_model_emit_state_changed(self);
  return TRUE;
}

GameBackendMoveList ggame_model_list_moves(GGameModel *self) {
  GameBackendMoveList empty = {0};

  g_return_val_if_fail(GGAME_IS_MODEL(self), empty);
  g_return_val_if_fail(self->backend != NULL, empty);
  g_return_val_if_fail(self->position != NULL, empty);
  if (!self->backend->supports_move_list || self->backend->list_moves == NULL) {
    g_debug("Move listing is not available for this backend");
    return empty;
  }

  return self->backend->list_moves(self->position);
}

gboolean ggame_model_apply_move(GGameModel *self, gconstpointer move) {
  GameBackendMoveList moves = {0};
  gboolean applied = FALSE;

  g_return_val_if_fail(GGAME_IS_MODEL(self), FALSE);
  g_return_val_if_fail(self->backend != NULL, FALSE);
  g_return_val_if_fail(self->position != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  if (!self->backend->supports_move_list) {
    applied = self->backend->apply_move(self->position, move);
    if (!applied) {
      g_debug("Backend rejected move application without move-list validation");
      return FALSE;
    }

    ggame_model_emit_state_changed(self);
    return TRUE;
  }

  moves = self->backend->list_moves(self->position);
  for (gsize i = 0; i < moves.count; ++i) {
    const void *candidate = self->backend->move_list_get(&moves, i);

    if (candidate == NULL || !self->backend->moves_equal(candidate, move)) {
      continue;
    }

    applied = self->backend->apply_move(self->position, candidate);
    break;
  }

  self->backend->move_list_free(&moves);

  if (!applied) {
    g_debug("Attempted to apply a move that is not in the available move list");
    return FALSE;
  }

  ggame_model_emit_state_changed(self);
  return TRUE;
}

gconstpointer ggame_model_peek_position(GGameModel *self) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), NULL);

  return self->position;
}

const GameBackend *ggame_model_peek_backend(GGameModel *self) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), NULL);

  return self->backend;
}

const GameBackendVariant *ggame_model_peek_variant(GGameModel *self) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), NULL);

  return self->variant;
}

char *ggame_model_format_status(GGameModel *self) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), NULL);
  g_return_val_if_fail(self->backend != NULL, NULL);

  return g_strdup(self->backend->display_name != NULL ? self->backend->display_name : "Game");
}
/* End copied file: src/game_model.c */

/* Begin copied file: src/games/homeworlds/homeworlds_backend.c */
#include "homeworlds_backend.h"

#include "homeworlds_game.h"
#include "homeworlds_move_builder.h"
#include "homeworlds_position_text.h"
#include "homeworlds_sgf_position.h"

#include <stdlib.h>
#include <string.h>

static const char *homeworlds_backend_side_label(guint side) {
  switch (side) {
    case 0:
      return "Player 1";
    case 1:
      return "Player 2";
    default:
      g_debug("Unsupported Homeworlds side index");
      return "Player";
  }
}

static const char *homeworlds_backend_outcome_banner_text(GameBackendOutcome outcome) {
  switch (outcome) {
    case GAME_BACKEND_OUTCOME_SIDE_0_WIN:
      return "Player 1 wins";
    case GAME_BACKEND_OUTCOME_SIDE_1_WIN:
      return "Player 2 wins";
    case GAME_BACKEND_OUTCOME_DRAW:
      return "Draw";
    case GAME_BACKEND_OUTCOME_ONGOING:
    default:
      return NULL;
  }
}

static SgfColor homeworlds_backend_sgf_color_for_side(guint side) {
  switch (side) {
    case 0:
      return SGF_COLOR_BLACK;
    case 1:
      return SGF_COLOR_WHITE;
    default:
      g_debug("Unsupported Homeworlds side index for SGF color");
      return SGF_COLOR_NONE;
  }
}

static void homeworlds_backend_position_init(gpointer position, const GameBackendVariant * /*variant_or_null*/) {
  HomeworldsPosition *homeworlds_position = position;

  g_return_if_fail(homeworlds_position != NULL);

  homeworlds_position_init(homeworlds_position);
}

static void homeworlds_backend_position_clear(gpointer position) {
  HomeworldsPosition *homeworlds_position = position;

  g_return_if_fail(homeworlds_position != NULL);

  homeworlds_position_clear(homeworlds_position);
}

static void homeworlds_backend_position_copy(gpointer dest, gconstpointer src) {
  HomeworldsPosition *dest_position = dest;
  const HomeworldsPosition *src_position = src;

  g_return_if_fail(dest_position != NULL);
  g_return_if_fail(src_position != NULL);

  homeworlds_position_copy(dest_position, src_position);
}

static GameBackendOutcome homeworlds_backend_position_outcome(gconstpointer position) {
  const HomeworldsPosition *homeworlds_position = position;

  g_return_val_if_fail(homeworlds_position != NULL, GAME_BACKEND_OUTCOME_ONGOING);

  return homeworlds_position_outcome(homeworlds_position);
}

static guint homeworlds_backend_position_turn(gconstpointer position) {
  const HomeworldsPosition *homeworlds_position = position;

  g_return_val_if_fail(homeworlds_position != NULL, 0);

  return homeworlds_position_turn(homeworlds_position);
}

static void homeworlds_backend_move_list_free(GameBackendMoveList *moves) {
  g_return_if_fail(moves != NULL);

  homeworlds_move_list_free(moves);
}

static const void *homeworlds_backend_move_list_get(const GameBackendMoveList *moves, gsize index) {
  g_return_val_if_fail(moves != NULL, NULL);

  return homeworlds_move_list_get(moves, index);
}

static gboolean homeworlds_backend_moves_equal(gconstpointer left, gconstpointer right) {
  const HomeworldsMove *left_move = left;
  const HomeworldsMove *right_move = right;

  g_return_val_if_fail(left_move != NULL, FALSE);
  g_return_val_if_fail(right_move != NULL, FALSE);

  return homeworlds_moves_equal(left_move, right_move);
}

static gboolean homeworlds_backend_apply_move(gpointer position, gconstpointer move) {
  HomeworldsPosition *homeworlds_position = position;
  const HomeworldsMove *homeworlds_move = move;

  g_return_val_if_fail(homeworlds_position != NULL, FALSE);
  g_return_val_if_fail(homeworlds_move != NULL, FALSE);

  return homeworlds_position_apply_move(homeworlds_position, homeworlds_move);
}

static gboolean homeworlds_backend_format_move(gconstpointer move, char *buffer, gsize size) {
  const HomeworldsMove *homeworlds_move = move;

  g_return_val_if_fail(homeworlds_move != NULL, FALSE);

  return homeworlds_move_format(homeworlds_move, buffer, size);
}

static gboolean homeworlds_backend_parse_move(const char *notation, gpointer out_move) {
  HomeworldsMove *homeworlds_move = out_move;

  g_return_val_if_fail(notation != NULL, FALSE);
  g_return_val_if_fail(homeworlds_move != NULL, FALSE);

  return homeworlds_move_parse(notation, homeworlds_move);
}

const GameBackend homeworlds_game_backend = {
  .id = "homeworlds",
  .display_name = "Homeworlds",
  .variant_count = 0,
  .position_size = sizeof(HomeworldsPosition),
  .move_size = sizeof(HomeworldsMove),
  .supports_move_list = FALSE,
  .supports_move_builder = TRUE,
  .side_label = homeworlds_backend_side_label,
  .sgf_color_for_side = homeworlds_backend_sgf_color_for_side,
  .outcome_banner_text = homeworlds_backend_outcome_banner_text,
  .position_init = homeworlds_backend_position_init,
  .position_clear = homeworlds_backend_position_clear,
  .position_copy = homeworlds_backend_position_copy,
  .position_outcome = homeworlds_backend_position_outcome,
  .position_turn = homeworlds_backend_position_turn,
  .move_list_free = homeworlds_backend_move_list_free,
  .move_list_get = homeworlds_backend_move_list_get,
  .moves_equal = homeworlds_backend_moves_equal,
  .move_builder_init = (gboolean (*)(gconstpointer, GameBackendMoveBuilder *)) homeworlds_move_builder_init,
  .move_builder_clear = homeworlds_move_builder_clear,
  .move_builder_list_candidates = (GameBackendMoveList (*)(const GameBackendMoveBuilder *))
      homeworlds_move_builder_list_candidates,
  .move_builder_step = (gboolean (*)(GameBackendMoveBuilder *, gconstpointer)) homeworlds_move_builder_step,
  .move_builder_is_complete = (gboolean (*)(const GameBackendMoveBuilder *)) homeworlds_move_builder_is_complete,
  .move_builder_build_move = (gboolean (*)(const GameBackendMoveBuilder *, gpointer))
      homeworlds_move_builder_build_move,
  .apply_move = homeworlds_backend_apply_move,
  .format_move = homeworlds_backend_format_move,
  .parse_move = homeworlds_backend_parse_move,
  .sgf_apply_setup_node = homeworlds_sgf_position_apply_setup_node,
  .sgf_write_position_node = homeworlds_sgf_position_write_position_node,
  .supports_square_grid_board = FALSE,
};
/* End copied file: src/games/homeworlds/homeworlds_backend.c */

/* Begin copied file: src/window.c */
#include "application.h"
#include "active_game_backend.h"
#include "game_app_profile.h"
#include "window.h"

#include "ai_search.h"
#include "app_paths.h"
#include "analysis_graph.h"
#include "board_view.h"
#include "common_settings.h"
#include "puzzle_catalog.h"
#include "games/checkers/rulesets.h"
#include "sgf_file_actions.h"
#include "sgf_controller.h"
#include "sgf_io.h"
#include "sgf_metadata.h"
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
  gboolean puzzle_mode;
  gboolean puzzle_finished;
  gboolean puzzle_feedback_locked;
  gboolean edit_mode_enabled;
  gboolean puzzle_saved_show_navigation_drawer;
  gboolean puzzle_saved_show_analysis_drawer;
  gboolean show_navigation_drawer;
  gboolean show_analysis_drawer;
  gboolean show_move_report;
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
  const GameBackendVariant *puzzle_variant;
  char *puzzle_variant_key;
  guint puzzle_attacker_side;
  guint puzzle_number;
  guint puzzle_expected_step;
  guint puzzle_wrong_move_source_id;
  GPtrArray *puzzle_steps;
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

static void ggame_window_sync_mode_ui(GGameWindow *self);
static void ggame_window_sync_move_report_ui(GGameWindow *self);
static void ggame_window_capture_panel_widths(GGameWindow *self);
static gint ggame_window_current_extra_width(GGameWindow *self);
static void ggame_window_apply_saved_panel_widths(GGameWindow *self);
static gint ggame_window_expected_default_width(GGameWindow *self);
static gboolean ggame_window_apply_player_move(gconstpointer move, gpointer user_data);
static gboolean ggame_window_on_board_square_action(guint8 index, guint button, gpointer user_data);
static void ggame_window_sync_board_orientation(GGameWindow *self);
static void ggame_window_sync_puzzle_ui(GGameWindow *self);
static void ggame_window_sync_drawer_ui_with_capture(GGameWindow *self, gboolean capture_current_layout);
static void ggame_window_sync_title(GGameWindow *self);
static gboolean ggame_window_puzzle_attempt_finish_failure(GGameWindow *self,
                                                               gboolean failure_on_first_move,
                                                               gconstpointer failed_first_move);
static void ggame_window_puzzle_attempt_reset(GGameWindow *self);
static void ggame_window_rebuild_board_host(GGameWindow *self);
static void ggame_window_load_default_size(gint *out_width, gint *out_height);
static void ggame_window_save_default_size(GGameWindow *self);
static gboolean ggame_window_on_close_request(GtkWindow *window, gpointer user_data);

enum {
  GGAME_WINDOW_DEFAULT_BOARD_PANEL_WIDTH = 500,
  GGAME_WINDOW_DEFAULT_NAVIGATION_PANEL_WIDTH = 300,
  GGAME_WINDOW_DEFAULT_ANALYSIS_PANEL_WIDTH = 300,
  GGAME_WINDOW_DEFAULT_WIDTH = 1100,
  GGAME_WINDOW_DEFAULT_HEIGHT = 700,
  GGAME_WINDOW_ANALYSIS_PROGRESS_INTERVAL_MS = 100,
  GGAME_WINDOW_ANALYSIS_TT_SIZE_MB = 256,
  GGAME_WINDOW_ANALYSIS_DEPTH_MIN = 1,
  GGAME_WINDOW_ANALYSIS_DEPTH_MAX = 16,
  GGAME_WINDOW_ANALYSIS_DEPTH_DEFAULT = 8,
  GGAME_WINDOW_PUZZLE_WRONG_MOVE_DELAY_MS = 1000,
};


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

static gboolean ggame_window_uses_square_board(GGameWindow *self) {
  const GameBackend *backend = NULL;

  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  backend = ggame_window_get_game_backend(self);
  return backend != NULL && backend->supports_square_grid_board;
}

static void ggame_window_clear_board_selection(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (self->board_view != NULL && ggame_window_uses_square_board(self)) {
    board_view_clear_selection(self->board_view);
  }
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

static gboolean ggame_window_puzzle_attempt_is_terminal(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);

  return FALSE;
}

static gboolean ggame_window_puzzle_attempt_finish_failure(GGameWindow *self,
                                                               gboolean failure_on_first_move,
                                                               gconstpointer failed_first_move) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);
  (void)failure_on_first_move;
  (void)failed_first_move;
  return FALSE;
}

static void ggame_window_puzzle_attempt_reset(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  self->puzzle_attempt_started = FALSE;
  self->puzzle_attempt_made_player_move = FALSE;
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

static CheckersColor ggame_window_resolve_board_bottom_color(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), CHECKERS_COLOR_WHITE);

  return self->board_bottom_color;
}

static void ggame_window_sync_board_orientation(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(self->board_view != NULL);

  if (!ggame_window_uses_square_board(self)) {
    return;
  }

  CheckersColor bottom_color = ggame_window_resolve_board_bottom_color(self);
  self->board_bottom_color = bottom_color;
  board_view_set_bottom_side(self->board_view, bottom_color);
}

static gboolean ggame_window_constrain_main_split_cb(GtkWidget * /*widget*/,
                                                         GdkFrameClock * /*frame_clock*/,
                                                         gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_val_if_fail(GGAME_IS_WINDOW(self), G_SOURCE_CONTINUE);

  if (!self->main_paned || !GTK_IS_PANED(self->main_paned)) {
    return G_SOURCE_CONTINUE;
  }
  if (self->profile == NULL ||
      self->profile->backend == NULL ||
      !self->profile->backend->supports_square_grid_board) {
    return G_SOURCE_CONTINUE;
  }

  int height = gtk_widget_get_height(self->main_paned);
  int position = gtk_paned_get_position(GTK_PANED(self->main_paned));
  if (height > 0 && position > height) {
    gtk_paned_set_position(GTK_PANED(self->main_paned), height);
  }

  return G_SOURCE_CONTINUE;
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

  gboolean drawer_attached = self->drawer_host != NULL && gtk_widget_get_parent(self->drawer_host) != NULL;
  gboolean drawer_split_attached = self->drawer_split != NULL && gtk_widget_get_parent(self->drawer_split) != NULL;

  if (drawer_attached && self->main_paned != NULL) {
    gint position = gtk_paned_get_position(GTK_PANED(self->main_paned));
    if (position > 0) {
      *board_panel_width = position;
    }
  } else if (drawer_attached && self->board_panel != NULL && gtk_widget_get_visible(self->board_panel)) {
    gint width = gtk_widget_get_width(self->board_panel);
    if (width > 0) {
      *board_panel_width = width;
    }
  }

  if (drawer_split_attached && self->drawer_split != NULL) {
    gint position = gtk_paned_get_position(GTK_PANED(self->drawer_split));
    if (position > 0) {
      *navigation_panel_width = position;
    }
  } else if (self->navigation_panel != NULL && gtk_widget_get_parent(self->navigation_panel) != NULL) {
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
  gint board_min_width = board_width;
  gint navigation_width = MAX(1, *navigation_panel_width);
  gint analysis_width = MAX(1, *analysis_panel_width);
  gint drawer_width = 0;

  if (self->profile != NULL && self->profile->layout.minimum_board_panel_width > 0) {
    board_min_width = MIN(board_width, self->profile->layout.minimum_board_panel_width);
  }

  if (self->show_navigation_drawer) {
    drawer_width += navigation_width;
  }
  if (self->show_analysis_drawer) {
    drawer_width += analysis_width;
  }

  gint current_height = gtk_widget_get_height(GTK_WIDGET(self));
  if (current_height <= 0) {
    gint default_width = 0;
    gint default_height = 0;
    gtk_window_get_default_size(GTK_WINDOW(self), &default_width, &default_height);
    current_height = default_height > 0 ? default_height : GGAME_WINDOW_DEFAULT_HEIGHT;
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
    gtk_widget_set_size_request(self->board_panel, board_min_width, -1);
  }
  if (self->drawer_host != NULL) {
    gtk_widget_set_size_request(self->drawer_host, -1, -1);
  }
  if (self->drawer_split != NULL) {
    gtk_widget_set_size_request(self->drawer_split, -1, -1);
  }
}

static void ggame_window_sync_move_report_ui(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (self->profile == NULL || self->profile->ui.set_move_report_enabled == NULL || self->board_host == NULL) {
    return;
  }

  self->profile->ui.set_move_report_enabled(self->board_host, self->show_move_report);
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

static void ggame_window_sync_mode_ui(GGameWindow *self) {
  const GGameAppProfile *profile = ggame_window_get_profile(self);
  gboolean supports_analysis = FALSE;
  gboolean supports_edit_mode = FALSE;

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(profile != NULL);

  supports_analysis = profile->features.supports_analysis;
  supports_edit_mode = profile->features.supports_edit_mode;

  gboolean allow_navigation = !self->edit_mode_enabled && !self->puzzle_mode;
  gboolean allow_edit_mode_selection = supports_edit_mode && !self->puzzle_mode;

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

static gboolean ggame_window_on_board_square_action(guint8 index, guint button, gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);

  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);
  (void)index;
  (void)button;
  return FALSE;
}

static void ggame_window_start_new_game(GGameWindow *self) {
  const GameBackendVariant *variant = NULL;

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_MODEL(self->game_model));

  variant = ggame_window_get_variant(self);
  ggame_model_reset(self->game_model, variant);
  ggame_window_clear_board_selection(self);
  ggame_sgf_controller_new_game(self->sgf_controller);
  g_clear_pointer(&self->loaded_source_name, g_free);
  ggame_window_sync_title(self);
}

static void ggame_window_sync_board_host_node(GGameWindow *self, const SgfNode *node) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(node != NULL);

  if (self->profile != NULL && self->profile->ui.sync_board_host_node != NULL && self->board_host != NULL) {
    self->profile->ui.sync_board_host_node(self->board_host, node);
  }
}

static const char *ggame_window_player_name_property_for_side(GGameWindow *self, guint side) {
  const GameBackend *backend = NULL;
  SgfColor color = SGF_COLOR_NONE;

  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);
  g_return_val_if_fail(side < 2, NULL);

  backend = self->profile != NULL ? self->profile->backend : NULL;
  if (backend != NULL && backend->sgf_color_for_side != NULL) {
    color = backend->sgf_color_for_side(side);
  }

  if (color == SGF_COLOR_BLACK) {
    return "PB";
  }
  if (color == SGF_COLOR_WHITE) {
    return "PW";
  }

  g_debug("Falling back to player name SGF property for side %u", side);
  return side == 0 ? "PB" : "PW";
}

static void ggame_window_set_root_text_property(SgfNode *root, const char *property, const char *value) {
  g_return_if_fail(root != NULL);
  g_return_if_fail(property != NULL);
  g_return_if_fail(value != NULL);

  sgf_node_clear_property(root, property);
  if (value[0] != '\0' && !sgf_node_add_property(root, property, value)) {
    g_debug("Failed to set SGF root property %s", property);
  }
}

static void ggame_window_set_new_game_computer_player_names(GGameWindow *self,
                                                            PlayerControlMode side0_mode,
                                                            PlayerControlMode side1_mode) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(self->sgf_controller));

  SgfTree *tree = ggame_sgf_controller_get_tree(self->sgf_controller);
  if (tree == NULL) {
    g_debug("Unable to set computer player names without an SGF tree");
    return;
  }

  SgfNode *root = (SgfNode *)sgf_tree_get_root(tree);
  if (root == NULL) {
    g_debug("Unable to set computer player names without an SGF root node");
    return;
  }

  PlayerControlMode modes[2] = {side0_mode, side1_mode};
  for (guint side = 0; side < 2; side++) {
    if (modes[side] != PLAYER_CONTROL_MODE_COMPUTER) {
      continue;
    }

    const char *property = ggame_window_player_name_property_for_side(self, side);
    if (property == NULL) {
      g_debug("Unable to set computer player name for side %u", side);
      continue;
    }
    ggame_window_set_root_text_property(root, property, "Computer");
  }
}

static void ggame_window_set_current_computer_player_names(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(self->controls_panel != NULL);

  ggame_window_set_new_game_computer_player_names(self,
                                                  player_controls_panel_get_mode(self->controls_panel, 0),
                                                  player_controls_panel_get_mode(self->controls_panel, 1));
}

static gboolean ggame_window_apply_player_move(gconstpointer move, gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;

  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(backend->moves_equal != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self->sgf_controller), FALSE);

  if (!ggame_sgf_controller_apply_move(self->sgf_controller, move)) {
    return FALSE;
  }

  return TRUE;
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

void ggame_window_set_loaded_variant(GGameWindow *self, const GameBackendVariant *variant) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(variant != NULL);
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
  if (ggame_window_uses_square_board(self)) {
    board_view_set_input_enabled(self->board_view, input_enabled);
  }
}

static void ggame_window_update_status(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(GGAME_IS_MODEL(self->game_model));

  if (ggame_window_uses_square_board(self)) {
    board_view_update(self->board_view);
  }
}

static void ggame_window_on_state_changed(GGameModel *model, gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);

  g_return_if_fail(GGAME_IS_MODEL(model));
  g_return_if_fail(GGAME_IS_WINDOW(self));

  ggame_window_sync_board_orientation(self);
  ggame_window_update_status(self);
  ggame_window_update_control_state(self);
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
  ggame_window_clear_board_selection(self);
  ggame_window_sync_mode_ui(self);
}

static void ggame_window_load_default_size(gint *out_width, gint *out_height) {
  g_return_if_fail(out_width != NULL);
  g_return_if_fail(out_height != NULL);

  *out_width = GGAME_WINDOW_DEFAULT_WIDTH;
  *out_height = GGAME_WINDOW_DEFAULT_HEIGHT;

  g_autoptr(GSettings) settings = ggame_common_settings_create();
  if (!G_IS_SETTINGS(settings)) {
    return;
  }

  gint saved_width = g_settings_get_int(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_WIDTH);
  gint saved_height = g_settings_get_int(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_HEIGHT);
  if (saved_width > 0 && saved_height > 0) {
    *out_width = saved_width;
    *out_height = saved_height;
  }
}

static void ggame_window_load_saved_layout(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  g_autoptr(GSettings) settings = ggame_common_settings_create();
  if (!G_IS_SETTINGS(settings)) {
    return;
  }

  gint board_panel_width = g_settings_get_int(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_BOARD_PANEL_WIDTH);
  if (board_panel_width > 0) {
    self->board_panel_width = board_panel_width;
  }

  gint navigation_panel_width = g_settings_get_int(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_NAVIGATION_PANEL_WIDTH);
  if (navigation_panel_width > 0) {
    self->navigation_panel_width = navigation_panel_width;
  }

  gint analysis_panel_width = g_settings_get_int(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_ANALYSIS_PANEL_WIDTH);
  if (analysis_panel_width > 0) {
    self->analysis_panel_width = analysis_panel_width;
  }

  if (!g_settings_get_boolean(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_LAYOUT_SAVED)) {
    return;
  }

  self->show_navigation_drawer =
      g_settings_get_boolean(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_SHOW_NAVIGATION_DRAWER);
  self->show_analysis_drawer =
      g_settings_get_boolean(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_SHOW_ANALYSIS_DRAWER);
  self->show_move_report = g_settings_get_boolean(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_SHOW_MOVE_REPORT);
}

static void ggame_window_save_default_size(GGameWindow *self) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  if (self->layout_mode == GGAME_WINDOW_LAYOUT_MODE_NORMAL &&
      (self->show_navigation_drawer ||
       self->show_analysis_drawer ||
       (self->drawer_host != NULL && gtk_widget_get_parent(self->drawer_host) != NULL))) {
    ggame_window_capture_panel_widths(self);
  }

  gint width = gtk_widget_get_width(GTK_WIDGET(self));
  gint height = gtk_widget_get_height(GTK_WIDGET(self));
  if (width <= 0 || height <= 0) {
    gtk_window_get_default_size(GTK_WINDOW(self), &width, &height);
  }
  if (width <= 0 || height <= 0) {
    return;
  }

  g_autoptr(GSettings) settings = ggame_common_settings_create();
  if (!G_IS_SETTINGS(settings)) {
    return;
  }

  g_settings_set_int(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_WIDTH, width);
  g_settings_set_int(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_HEIGHT, height);
  g_settings_set_boolean(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_LAYOUT_SAVED, TRUE);
  g_settings_set_int(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_BOARD_PANEL_WIDTH, self->board_panel_width);
  g_settings_set_int(settings,
                     GGAME_COMMON_SETTINGS_KEY_WINDOW_NAVIGATION_PANEL_WIDTH,
                     self->navigation_panel_width);
  g_settings_set_int(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_ANALYSIS_PANEL_WIDTH, self->analysis_panel_width);

  gboolean show_navigation_drawer = self->show_navigation_drawer;
  gboolean show_analysis_drawer = self->show_analysis_drawer;
  if (self->puzzle_mode) {
    show_navigation_drawer = self->puzzle_saved_show_navigation_drawer;
    show_analysis_drawer = self->puzzle_saved_show_analysis_drawer;
  }

  g_settings_set_boolean(settings,
                         GGAME_COMMON_SETTINGS_KEY_WINDOW_SHOW_NAVIGATION_DRAWER,
                         show_navigation_drawer);
  g_settings_set_boolean(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_SHOW_ANALYSIS_DRAWER, show_analysis_drawer);
  g_settings_set_boolean(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_SHOW_MOVE_REPORT, self->show_move_report);
}

static gboolean ggame_window_on_close_request(GtkWindow *window, gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_val_if_fail(GGAME_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(GTK_IS_WINDOW(window), FALSE);

  ggame_window_save_default_size(self);
  return FALSE;
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

  g_clear_handle_id(&self->auto_move_source_id, g_source_remove);
  ggame_window_set_board_orientation_mode(self, GGAME_WINDOW_BOARD_ORIENTATION_FIXED);
  ggame_window_update_control_state(self);
}

static void ggame_window_on_sgf_node_changed(GGameSgfController * /*controller*/,
                                                 const SgfNode *node,
                                                 gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(node != NULL);

  ggame_window_sync_board_host_node(self, node);
}

static void ggame_window_on_puzzle_next_clicked(GtkButton * /*button*/, gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
}

static void ggame_window_on_puzzle_analyze_clicked(GtkButton * /*button*/, gpointer user_data) {
  GGameWindow *self = GGAME_WINDOW(user_data);
  g_return_if_fail(GGAME_IS_WINDOW(self));
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

const GameBackendVariant *ggame_window_get_variant(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);

  return self->game_model != NULL ? ggame_model_peek_variant(self->game_model) : NULL;
}

void ggame_window_apply_new_game_settings(GGameWindow *self,
                                          const GameBackendVariant *variant,
                                              PlayerControlMode white_mode,
                                              PlayerControlMode black_mode,
                                              guint computer_depth) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(self->controls_panel != NULL);

  (void)variant;
  player_controls_panel_set_mode(self->controls_panel, 0, white_mode);
  player_controls_panel_set_mode(self->controls_panel, 1, black_mode);
  player_controls_panel_set_computer_depth(self->controls_panel, computer_depth);
  ggame_window_start_new_game(self);
  ggame_window_set_current_computer_player_names(self);
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
  if (ggame_window_uses_square_board(self)) {
    board_view_set_model(self->board_view, self->game_model);
  }
  ggame_sgf_controller_set_game_model(self->sgf_controller, self->game_model);
  ggame_window_rebuild_board_host(self);
  ggame_window_sync_board_orientation(self);
  ggame_window_update_status(self);
  ggame_window_update_control_state(self);
  ggame_window_set_current_computer_player_names(self);
}

static void ggame_window_rebuild_board_host(GGameWindow *self) {
  GtkWidget *host = NULL;
  GtkWidget *board_widget = NULL;
  GGameAppBoardHostOptions host_options = {
    .move_report_enabled = self->show_move_report,
  };

  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(self->board_view != NULL);
  g_return_if_fail(self->board_host_box != NULL);
  g_return_if_fail(GGAME_IS_MODEL(self->game_model));

  if (self->board_host != NULL) {
    ggame_widget_remove_from_parent(self->board_host);
    self->board_host = NULL;
  }

  if (self->profile != NULL && self->profile->ui.create_board_host != NULL) {
    host = self->profile->ui.create_board_host(self->game_model,
                                               self->board_view,
                                               ggame_window_apply_player_move,
                                               self,
                                               &host_options);
    g_return_if_fail(GTK_IS_WIDGET(host));
  } else {
    g_return_if_fail(ggame_window_uses_square_board(self));
    board_widget = board_view_get_widget(self->board_view);
    g_return_if_fail(GTK_IS_WIDGET(board_widget));
    if (gtk_widget_get_parent(board_widget) != NULL) {
      ggame_widget_remove_from_parent(board_widget);
    }

    host = gtk_aspect_frame_new(0.5f, 0.5f, 1.0f, FALSE);
    gtk_widget_set_hexpand(host, TRUE);
    gtk_widget_set_vexpand(host, TRUE);
    gtk_aspect_frame_set_child(GTK_ASPECT_FRAME(host), board_widget);
  }

  self->board_host = host;
  gtk_box_append(GTK_BOX(self->board_host_box), host);
  ggame_window_sync_move_report_ui(self);
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
    g_ptr_array_unref(self->puzzle_steps);
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
  g_clear_pointer(&self->puzzle_variant_key, g_free);
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

static void ggame_window_class_init(GGameWindowClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = ggame_window_dispose;
}

static void ggame_window_init(GGameWindow *self) {
  const GGameAppLayout *layout = NULL;

  self->profile = ggame_active_app_profile();
  layout = self->profile != NULL ? &self->profile->layout : NULL;
  self->auto_move_source_id = 0;
  self->paned_tick_id = 0;
  self->puzzle_wrong_move_source_id = 0;
  self->syncing_layout_default_size = FALSE;
  self->applied_ruleset = PLAYER_RULESET_INTERNATIONAL;

  ggame_window_sync_title(self);
  gint default_width = 0;
  gint default_height = 0;
  ggame_window_load_default_size(&default_width, &default_height);
  gtk_window_set_default_size(GTK_WINDOW(self), default_width, default_height);
  g_signal_connect(self, "close-request", G_CALLBACK(ggame_window_on_close_request), self);

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
  g_object_set_data(G_OBJECT(self), "main-paned", paned);
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
  if (self->profile != NULL) {
    player_controls_panel_set_computer_depth(self->controls_panel, self->profile->default_computer_depth);
  }
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

  gboolean square_board = self->profile != NULL && self->profile->backend != NULL &&
                          self->profile->backend->supports_square_grid_board;
  self->sgf_controller = ggame_sgf_controller_new(square_board ? self->board_view : NULL);
  if (square_board) {
    board_view_set_sgf_controller(self->board_view, self->sgf_controller);
    board_view_set_move_handler(self->board_view, ggame_window_apply_player_move, self);
    board_view_set_square_handler(self->board_view, ggame_window_on_board_square_action, self);
  }
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
  self->show_move_report = TRUE;
  self->layout_mode = GGAME_WINDOW_LAYOUT_MODE_NORMAL;
  self->board_panel_width =
      layout != NULL && layout->default_board_panel_width > 0 ? layout->default_board_panel_width
                                                              : GGAME_WINDOW_DEFAULT_BOARD_PANEL_WIDTH;
  self->navigation_panel_width =
      layout != NULL && layout->default_navigation_panel_width > 0 ? layout->default_navigation_panel_width
                                                                   : GGAME_WINDOW_DEFAULT_NAVIGATION_PANEL_WIDTH;
  self->analysis_panel_width =
      layout != NULL && layout->default_analysis_panel_width > 0 ? layout->default_analysis_panel_width
                                                                 : GGAME_WINDOW_DEFAULT_ANALYSIS_PANEL_WIDTH;
  ggame_window_load_saved_layout(self);
  self->puzzle_saved_show_navigation_drawer = self->show_navigation_drawer;
  self->puzzle_saved_show_analysis_drawer = self->show_analysis_drawer;
  self->extra_width = MAX(0, default_width - ggame_window_expected_default_width(self));
  self->puzzle_board_panel_width = self->board_panel_width;
  self->puzzle_navigation_panel_width = self->navigation_panel_width;
  self->puzzle_analysis_panel_width = self->analysis_panel_width;
  self->puzzle_extra_width = 0;
  self->board_orientation_mode = GGAME_WINDOW_BOARD_ORIENTATION_FIXED;
  self->board_bottom_color = CHECKERS_COLOR_WHITE;
  self->puzzle_variant = NULL;
  self->puzzle_variant_key = NULL;
  self->puzzle_attacker_side = 0;
  self->puzzle_number = 0;
  self->puzzle_attempt_started = FALSE;
  self->puzzle_attempt_made_player_move = FALSE;
  ggame_window_sync_drawer_ui_with_capture(self, FALSE);
  ggame_window_sync_puzzle_ui(self);
  ggame_window_sync_mode_ui(self);
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
/* End copied file: src/window.c */

static GtkApplication *test_homeworlds_app = NULL;

static void drain_main_context(void) {
  for (guint i = 0; i < 256; ++i) {
    g_main_context_iteration(NULL, FALSE);
  }
}

static GGameWindow *create_window(GtkApplication **out_app, GGameModel **out_model) {
  if (test_homeworlds_app == NULL) {
    test_homeworlds_app = gtk_application_new("io.github.jeromea.ghomeworlds.test",
                                              G_APPLICATION_DEFAULT_FLAGS | G_APPLICATION_NON_UNIQUE);
    g_application_register(G_APPLICATION(test_homeworlds_app), NULL, NULL);
  }

  GtkApplication *app = g_object_ref(test_homeworlds_app);
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  GGameWindow *window = ggame_window_new(app, model);

  *out_app = app;
  *out_model = model;
  return window;
}

int main(void) {
  GtkApplication *app = NULL;
  GGameModel *model = NULL;
  GGameWindow *window = NULL;

  g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL);

  gtk_init();

  window = create_window(&app, &model);
  gtk_window_present(GTK_WINDOW(window));
  drain_main_context();
  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);

  window = create_window(&app, &model);
  gtk_window_set_default_size(GTK_WINDOW(window), 2000, 700);
  gtk_window_present(GTK_WINDOW(window));
  drain_main_context();
  gtk_paned_set_position(GTK_PANED(g_object_get_data(G_OBJECT(window), "main-paned")), 1400);
  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);
}
