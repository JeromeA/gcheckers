#include "ai_alpha_beta.h"
#include "window.h"

#include "analysis_graph.h"
#include "board_view.h"
#include "rulesets.h"
#include "sgf_file_actions.h"
#include "sgf_controller.h"
#include "sgf_move_props.h"
#include "style.h"
#include "player_controls_panel.h"
#include "widget_utils.h"

#include <string.h>

struct _GCheckersWindow {
  GtkApplicationWindow parent_instance;
  GCheckersModel *model;
  GtkWidget *main_paned;
  GtkWidget *board_panel;
  GtkWidget *drawer_host;
  GtkWidget *drawer_split;
  GtkWidget *navigation_panel;
  GtkWidget *analysis_panel;
  GtkScale *analysis_depth_scale;
  GtkTextBuffer *analysis_buffer;
  BoardView *board_view;
  PlayerControlsPanel *controls_panel;
  GtkDropDown *sgf_mode_control;
  GCheckersSgfController *sgf_controller;
  AnalysisGraph *analysis_graph;
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
  gint board_panel_width;
  gint navigation_panel_width;
  gint analysis_panel_width;
  GCheckersWindowBoardOrientationMode board_orientation_mode;
  GCheckersWindowBoardOrientationMode puzzle_saved_board_orientation_mode;
  CheckersColor board_bottom_color;
  CheckersColor puzzle_saved_board_bottom_color;
  CheckersColor puzzle_attacker;
  guint puzzle_expected_step;
  guint puzzle_wrong_move_source_id;
  GArray *puzzle_steps;
  GtkWidget *puzzle_panel;
  GtkLabel *puzzle_message_label;
  GtkButton *puzzle_next_button;
  GtkButton *puzzle_analyze_button;
};

G_DEFINE_TYPE(GCheckersWindow, gcheckers_window, GTK_TYPE_APPLICATION_WINDOW)

typedef struct {
  GCheckersWindow *self;
  Game game;
  gint generation;
  CheckersAiTranspositionTable *tt;
  guint current_depth;
  guint target_depth;
  gint64 last_progress_publish_us;
  CheckersAiSearchStats cumulative_stats;
  guint last_completed_depth;
  SgfNodeAnalysis *last_completed_analysis;
  const SgfNode *target_node;
} GCheckersWindowAnalysisTask;

typedef enum {
  GCHECKERS_WINDOW_ANALYSIS_MODE_NONE = 0,
  GCHECKERS_WINDOW_ANALYSIS_MODE_CURRENT,
  GCHECKERS_WINDOW_ANALYSIS_MODE_FULL_GAME
} GCheckersWindowAnalysisMode;

typedef struct {
  gint generation;
  GCheckersWindowAnalysisMode mode;
  gboolean done;
  gboolean canceled;
  gboolean is_status;
  gboolean is_payload;
  char *status_text;
  SgfNodeAnalysis *analysis;
  const SgfNode *node;
} GCheckersWindowAnalysisEvent;

typedef struct {
  const SgfNode *node;
  GArray *moves;
} GCheckersWindowFullNodeJob;

typedef struct {
  CheckersColor color;
  CheckersMove move;
} GCheckersWindowPuzzleStep;

typedef struct {
  GCheckersWindow *self;
  gint generation;
  const CheckersRules *rules;
  guint depth;
  CheckersAiTranspositionTable *tt;
  GPtrArray *jobs;
} GCheckersWindowFullAnalysisTask;

static void gcheckers_window_analysis_sync_ui(GCheckersWindow *self);
static void gcheckers_window_analysis_reset_runtime_state(GCheckersWindow *self);
static void gcheckers_window_analysis_begin_session(GCheckersWindow *self,
                                                    GCheckersWindowAnalysisMode mode,
                                                    guint expected_nodes);
static void gcheckers_window_analysis_finish_session(GCheckersWindow *self);
static gboolean gcheckers_window_is_edit_mode(GCheckersWindow *self);
static void gcheckers_window_set_action_enabled(GActionMap *map, const char *name, gboolean enabled);
static void gcheckers_window_sync_mode_ui(GCheckersWindow *self);
static void gcheckers_window_sync_drawer_ui(GCheckersWindow *self);
static void gcheckers_window_capture_panel_widths(GCheckersWindow *self);
static gint gcheckers_window_current_extra_width(GCheckersWindow *self);
static void gcheckers_window_apply_saved_panel_widths(GCheckersWindow *self, gint extra_width);
static gboolean gcheckers_window_format_setup_point(uint8_t index, uint8_t board_size, char out_point[3]);
static gboolean gcheckers_window_update_node_setup_piece(SgfNode *node, const char *point, CheckersPiece piece);
static gboolean gcheckers_window_on_board_square_action(guint8 index, guint button, gpointer user_data);
static void gcheckers_window_sync_board_orientation(GCheckersWindow *self);
static void gcheckers_window_sync_puzzle_ui(GCheckersWindow *self);
static void gcheckers_window_leave_puzzle_mode(GCheckersWindow *self, gboolean restore_drawers);
static gboolean gcheckers_window_start_random_puzzle_mode(GCheckersWindow *self);
static void gcheckers_window_set_analysis_current_action_state(GCheckersWindow *self, gboolean active);
static void gcheckers_window_stop_analysis(GCheckersWindow *self);

enum {
  GCHECKERS_WINDOW_DEFAULT_BOARD_PANEL_WIDTH = 500,
  GCHECKERS_WINDOW_DEFAULT_NAVIGATION_PANEL_WIDTH = 300,
  GCHECKERS_WINDOW_DEFAULT_ANALYSIS_PANEL_WIDTH = 300,
  GCHECKERS_WINDOW_DEFAULT_HEIGHT = 700,
  GCHECKERS_WINDOW_ANALYSIS_PROGRESS_INTERVAL_MS = 100,
  GCHECKERS_WINDOW_ANALYSIS_TT_SIZE_MB = 256,
  GCHECKERS_WINDOW_ANALYSIS_DEPTH_MIN = 1,
  GCHECKERS_WINDOW_ANALYSIS_DEPTH_MAX = 16,
  GCHECKERS_WINDOW_ANALYSIS_DEPTH_DEFAULT = 8,
  GCHECKERS_WINDOW_PUZZLE_WRONG_MOVE_DELAY_MS = 1000,
};

static void gcheckers_window_refresh_analysis_graph(GCheckersWindow *self);

static gboolean gcheckers_window_moves_equal(const CheckersMove *left, const CheckersMove *right) {
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

static const char *gcheckers_window_color_name(CheckersColor color) {
  return color == CHECKERS_COLOR_BLACK ? "Black" : "White";
}

static const char *gcheckers_window_puzzles_dir(void) {
  const char *override = g_getenv("GCHECKERS_PUZZLES_DIR");
  return override != NULL && *override != '\0' ? override : "puzzles";
}

static gboolean gcheckers_window_analysis_depth_valid(guint depth) {
  return depth >= GCHECKERS_WINDOW_ANALYSIS_DEPTH_MIN && depth <= GCHECKERS_WINDOW_ANALYSIS_DEPTH_MAX;
}

static gboolean gcheckers_window_board_orientation_mode_valid(GCheckersWindowBoardOrientationMode mode) {
  return mode == GCHECKERS_WINDOW_BOARD_ORIENTATION_FIXED ||
         mode == GCHECKERS_WINDOW_BOARD_ORIENTATION_FOLLOW_PLAYER ||
         mode == GCHECKERS_WINDOW_BOARD_ORIENTATION_FOLLOW_TURN;
}

static gboolean gcheckers_window_try_resolve_follow_player_bottom_color(GCheckersWindow *self,
                                                                        CheckersColor *out_color) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(out_color != NULL, FALSE);
  g_return_val_if_fail(self->controls_panel != NULL, FALSE);

  gboolean white_is_user = player_controls_panel_is_user_control(self->controls_panel, CHECKERS_COLOR_WHITE);
  gboolean black_is_user = player_controls_panel_is_user_control(self->controls_panel, CHECKERS_COLOR_BLACK);
  if (white_is_user == black_is_user) {
    return FALSE;
  }

  *out_color = white_is_user ? CHECKERS_COLOR_WHITE : CHECKERS_COLOR_BLACK;
  return TRUE;
}

static CheckersColor gcheckers_window_resolve_board_bottom_color(GCheckersWindow *self) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), CHECKERS_COLOR_WHITE);

  switch (self->board_orientation_mode) {
    case GCHECKERS_WINDOW_BOARD_ORIENTATION_FIXED:
      return self->board_bottom_color;
    case GCHECKERS_WINDOW_BOARD_ORIENTATION_FOLLOW_PLAYER: {
      CheckersColor bottom_color = self->board_bottom_color;
      if (gcheckers_window_try_resolve_follow_player_bottom_color(self, &bottom_color)) {
        return bottom_color;
      }
      return self->board_bottom_color;
    }
    case GCHECKERS_WINDOW_BOARD_ORIENTATION_FOLLOW_TURN: {
      if (self->model != NULL) {
        const GameState *state = gcheckers_model_peek_state(self->model);
        if (state != NULL) {
          return state->turn;
        }
      }
      return self->board_bottom_color;
    }
    default:
      g_debug("Unexpected board orientation mode %d", self->board_orientation_mode);
      return self->board_bottom_color;
  }
}

static void gcheckers_window_sync_board_orientation(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(self->board_view != NULL);

  CheckersColor bottom_color = gcheckers_window_resolve_board_bottom_color(self);
  self->board_bottom_color = bottom_color;
  board_view_set_bottom_color(self->board_view, bottom_color);
}

static gboolean gcheckers_window_puzzle_name_matches(const char *name) {
  g_return_val_if_fail(name != NULL, FALSE);

  return (g_str_has_prefix(name, "puzzle-") || g_str_has_prefix(name, "puzzles-")) &&
         g_str_has_suffix(name, ".sgf");
}

static gboolean gcheckers_window_collect_puzzle_paths(const char *dir_path, GPtrArray *out_paths) {
  g_return_val_if_fail(dir_path != NULL, FALSE);
  g_return_val_if_fail(out_paths != NULL, FALSE);

  g_autoptr(GError) error = NULL;
  GDir *dir = g_dir_open(dir_path, 0, &error);
  if (dir == NULL) {
    g_debug("Failed to open puzzle dir %s: %s", dir_path, error != NULL ? error->message : "unknown error");
    return FALSE;
  }

  const char *name = NULL;
  while ((name = g_dir_read_name(dir)) != NULL) {
    if (!gcheckers_window_puzzle_name_matches(name)) {
      continue;
    }

    g_ptr_array_add(out_paths, g_build_filename(dir_path, name, NULL));
  }

  g_dir_close(dir);
  return TRUE;
}

static gboolean gcheckers_window_sgf_color_to_checkers(SgfColor color, CheckersColor *out_color) {
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

static gboolean gcheckers_window_load_puzzle_steps_from_tree(SgfTree *tree, GArray *out_steps) {
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
    if (!gcheckers_window_sgf_color_to_checkers(sgf_color, &color)) {
      g_debug("Puzzle main line node had unexpected color");
      return FALSE;
    }

    GCheckersWindowPuzzleStep step = {
      .color = color,
      .move = move,
    };
    g_array_append_val(out_steps, step);
  }
}

static void gcheckers_window_set_puzzle_message(GCheckersWindow *self, const char *message) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(message != NULL);

  if (self->puzzle_message_label != NULL) {
    gtk_label_set_text(self->puzzle_message_label, message);
  }
}

static void gcheckers_window_set_default_puzzle_message(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  g_autofree char *message =
      g_strdup_printf("Find the best sequence for %s.", gcheckers_window_color_name(self->puzzle_attacker));
  gcheckers_window_set_puzzle_message(self, message);
}

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

static void gcheckers_window_analysis_sync_ui(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  gboolean full_game_active = self->analysis_mode == GCHECKERS_WINDOW_ANALYSIS_MODE_FULL_GAME;
  GAction *action = g_action_map_lookup_action(G_ACTION_MAP(self), "analysis-whole-game");
  if (action != NULL && G_IS_SIMPLE_ACTION(action)) {
    g_simple_action_set_enabled(G_SIMPLE_ACTION(action), !full_game_active && !self->puzzle_mode);
  }

  action = g_action_map_lookup_action(G_ACTION_MAP(self), "analysis-current-position");
  if (action != NULL && G_IS_SIMPLE_ACTION(action)) {
    g_simple_action_set_enabled(G_SIMPLE_ACTION(action), !self->puzzle_mode);
  }

  if (!full_game_active && self->analysis_graph != NULL) {
    analysis_graph_clear_progress_node(self->analysis_graph);
  }
}

static void gcheckers_window_capture_panel_widths(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  if (self->board_panel != NULL && gtk_widget_get_visible(self->board_panel)) {
    gint width = gtk_widget_get_width(self->board_panel);
    if (width > 0) {
      self->board_panel_width = width;
    }
  }

  if (self->navigation_panel != NULL && gtk_widget_get_parent(self->navigation_panel) != NULL) {
    gint width = gtk_widget_get_width(self->navigation_panel);
    if (width > 0) {
      self->navigation_panel_width = width;
    }
  }

  if (self->analysis_panel != NULL && gtk_widget_get_parent(self->analysis_panel) != NULL) {
    gint width = gtk_widget_get_width(self->analysis_panel);
    if (width > 0) {
      self->analysis_panel_width = width;
    }
  }
}

static gint gcheckers_window_current_extra_width(GCheckersWindow *self) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), 0);

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

static void gcheckers_window_apply_saved_panel_widths(GCheckersWindow *self, gint extra_width) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  gint board_width = MAX(1, self->board_panel_width);
  gint navigation_width = MAX(1, self->navigation_panel_width);
  gint analysis_width = MAX(1, self->analysis_panel_width);
  gint drawer_width = 0;

  if (self->show_navigation_drawer) {
    drawer_width += navigation_width;
  }
  if (self->show_analysis_drawer) {
    drawer_width += analysis_width;
  }

  gint current_height = gtk_widget_get_height(GTK_WIDGET(self));
  if (current_height <= 0) {
    current_height = GCHECKERS_WINDOW_DEFAULT_HEIGHT;
  }

  gtk_window_set_default_size(GTK_WINDOW(self), board_width + drawer_width + MAX(0, extra_width), current_height);

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

static void gcheckers_window_sync_drawer_ui(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  gcheckers_window_capture_panel_widths(self);
  gint extra_width = gcheckers_window_current_extra_width(self);

  if (self->navigation_panel != NULL) {
    gcheckers_widget_remove_from_parent(self->navigation_panel);
  }
  if (self->analysis_panel != NULL) {
    gcheckers_widget_remove_from_parent(self->analysis_panel);
  }
  if (self->drawer_host != NULL) {
    gcheckers_widget_remove_from_parent(self->drawer_host);
  }
  if (self->drawer_split != NULL) {
    gcheckers_widget_remove_from_parent(self->drawer_split);
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

  gcheckers_window_apply_saved_panel_widths(self, extra_width);
  gtk_widget_queue_allocate(GTK_WIDGET(self));
}

static void gcheckers_window_sync_puzzle_ui(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

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

static void gcheckers_window_on_show_navigation_drawer_change_state(GSimpleAction *action,
                                                                    GVariant *value,
                                                                    gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(G_IS_SIMPLE_ACTION(action));
  g_return_if_fail(value != NULL);

  self->show_navigation_drawer = g_variant_get_boolean(value);
  g_simple_action_set_state(action, value);
  gcheckers_window_sync_drawer_ui(self);
}

static void gcheckers_window_on_show_analysis_drawer_change_state(GSimpleAction *action,
                                                                  GVariant *value,
                                                                  gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(G_IS_SIMPLE_ACTION(action));
  g_return_if_fail(value != NULL);

  self->show_analysis_drawer = g_variant_get_boolean(value);
  g_simple_action_set_state(action, value);
  gcheckers_window_sync_drawer_ui(self);
}

static void gcheckers_window_analysis_reset_runtime_state(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  self->analysis_expected_nodes = 0;
  self->analysis_attached_nodes = 0;
  self->analysis_processed_nodes = 0;
  self->analysis_last_updated_node = NULL;
  self->analysis_done_received = FALSE;
  self->analysis_canceled = FALSE;
}

static void gcheckers_window_analysis_begin_session(GCheckersWindow *self,
                                                    GCheckersWindowAnalysisMode mode,
                                                    guint expected_nodes) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(mode == GCHECKERS_WINDOW_ANALYSIS_MODE_CURRENT ||
                   mode == GCHECKERS_WINDOW_ANALYSIS_MODE_FULL_GAME);

  self->analysis_mode = mode;
  gcheckers_window_analysis_reset_runtime_state(self);
  self->analysis_expected_nodes = expected_nodes;
  gcheckers_window_analysis_sync_ui(self);
}

static void gcheckers_window_analysis_finish_session(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  self->analysis_mode = GCHECKERS_WINDOW_ANALYSIS_MODE_NONE;
  gcheckers_window_analysis_reset_runtime_state(self);
  gcheckers_window_analysis_sync_ui(self);
}

static gboolean gcheckers_window_is_edit_mode(GCheckersWindow *self) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), FALSE);

  return self->edit_mode_enabled;
}

static void gcheckers_window_set_action_enabled(GActionMap *map, const char *name, gboolean enabled) {
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

static void gcheckers_window_sync_mode_ui(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  gboolean allow_navigation = !self->edit_mode_enabled && !self->puzzle_mode;
  gboolean allow_sgf_file_actions = !self->puzzle_mode;
  gboolean allow_view_actions = !self->puzzle_mode;
  gboolean allow_edit_mode = !self->puzzle_mode;

  gcheckers_window_set_action_enabled(G_ACTION_MAP(self), "game-force-move", allow_navigation);
  gcheckers_window_set_action_enabled(G_ACTION_MAP(self), "navigation-rewind", allow_navigation);
  gcheckers_window_set_action_enabled(G_ACTION_MAP(self), "navigation-step-backward", allow_navigation);
  gcheckers_window_set_action_enabled(G_ACTION_MAP(self), "navigation-step-forward", allow_navigation);
  gcheckers_window_set_action_enabled(G_ACTION_MAP(self), "navigation-step-forward-to-branch", allow_navigation);
  gcheckers_window_set_action_enabled(G_ACTION_MAP(self), "navigation-step-forward-to-end", allow_navigation);
  gcheckers_window_set_action_enabled(G_ACTION_MAP(self), "sgf-load", allow_sgf_file_actions);
  gcheckers_window_set_action_enabled(G_ACTION_MAP(self), "sgf-save-as", allow_sgf_file_actions);
  gcheckers_window_set_action_enabled(G_ACTION_MAP(self), "sgf-save-position", allow_sgf_file_actions);
  gcheckers_window_set_action_enabled(G_ACTION_MAP(self), "view-show-navigation-drawer", allow_view_actions);
  gcheckers_window_set_action_enabled(G_ACTION_MAP(self), "view-show-analysis-drawer", allow_view_actions);

  if (self->analysis_graph != NULL) {
    GtkWidget *graph_widget = analysis_graph_get_widget(self->analysis_graph);
    if (graph_widget != NULL) {
      gtk_widget_set_sensitive(graph_widget, allow_navigation);
    }
  }

  if (self->sgf_controller != NULL) {
    GtkWidget *sgf_widget = gcheckers_sgf_controller_get_widget(self->sgf_controller);
    if (sgf_widget != NULL) {
      gtk_widget_set_sensitive(sgf_widget, allow_navigation);
    }
  }

  if (self->sgf_mode_control != NULL) {
    gtk_widget_set_sensitive(GTK_WIDGET(self->sgf_mode_control), allow_edit_mode);
  }
}

static gboolean gcheckers_window_format_setup_point(uint8_t index, uint8_t board_size, char out_point[3]) {
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

static const char *gcheckers_window_piece_label(CheckersPiece piece) {
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

static gboolean gcheckers_window_node_set_prop_has_point(SgfNode *node,
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

static gboolean gcheckers_window_update_node_setup_piece(SgfNode *node, const char *point, CheckersPiece piece) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(point != NULL, FALSE);

  gboolean is_empty = piece == CHECKERS_PIECE_EMPTY;
  gboolean is_black = piece == CHECKERS_PIECE_BLACK_MAN || piece == CHECKERS_PIECE_BLACK_KING;
  gboolean is_white = piece == CHECKERS_PIECE_WHITE_MAN || piece == CHECKERS_PIECE_WHITE_KING;
  gboolean is_black_king = piece == CHECKERS_PIECE_BLACK_KING;
  gboolean is_white_king = piece == CHECKERS_PIECE_WHITE_KING;

  if (!gcheckers_window_node_set_prop_has_point(node, "AE", point, is_empty) ||
      !gcheckers_window_node_set_prop_has_point(node, "AB", point, is_black) ||
      !gcheckers_window_node_set_prop_has_point(node, "AW", point, is_white) ||
      !gcheckers_window_node_set_prop_has_point(node, "ABK", point, is_black_king) ||
      !gcheckers_window_node_set_prop_has_point(node, "AWK", point, is_white_king)) {
    g_debug("Edit update failed while setting SGF setup properties at point=%s target=%s",
            point,
            gcheckers_window_piece_label(piece));
    return FALSE;
  }

  sgf_node_clear_analysis(node);
  g_debug("Edit update wrote SGF setup properties at point=%s target=%s",
          point,
          gcheckers_window_piece_label(piece));
  return TRUE;
}

static gboolean gcheckers_window_on_board_square_action(guint8 index, guint button, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), FALSE);
  if (button != GDK_BUTTON_PRIMARY && button != GDK_BUTTON_SECONDARY) {
    return FALSE;
  }

  if (!gcheckers_window_is_edit_mode(self)) {
    return FALSE;
  }

  g_return_val_if_fail(GCHECKERS_IS_MODEL(self->model), FALSE);
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self->sgf_controller), FALSE);

  const GameState *state = gcheckers_model_peek_state(self->model);
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

  SgfTree *tree = gcheckers_sgf_controller_get_tree(self->sgf_controller);
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
  if (!gcheckers_window_format_setup_point(index, state->board.board_size, point)) {
    g_debug("Edit click failed formatting setup point: index=%u board_size=%u", index, state->board.board_size);
    return TRUE;
  }
  if (!gcheckers_window_update_node_setup_piece(current_node, point, next)) {
    g_debug("Edit click failed SGF setup update: index=%u point=%s", index, point);
    return TRUE;
  }
  if (!gcheckers_sgf_controller_refresh_current_node(self->sgf_controller)) {
    g_debug("Failed to refresh model from edited SGF current node");
    return TRUE;
  }

  const GameState *after = gcheckers_model_peek_state(self->model);
  if (after == NULL) {
    g_debug("Edit click missing post-refresh game state: index=%u point=%s", index, point);
    return TRUE;
  }
  (void)after;

  return TRUE;
}

static void gcheckers_window_start_new_game(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));

  gcheckers_window_leave_puzzle_mode(self, TRUE);
  gcheckers_model_reset(self->model);
  board_view_clear_selection(self->board_view);
  gcheckers_sgf_controller_new_game(self->sgf_controller);
}

static gboolean gcheckers_window_revert_wrong_puzzle_move_cb(gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), G_SOURCE_REMOVE);

  self->puzzle_wrong_move_source_id = 0;
  self->puzzle_feedback_locked = FALSE;
  if (!gcheckers_sgf_controller_refresh_current_node(self->sgf_controller)) {
    g_debug("Failed to restore puzzle position after wrong move");
  }
  board_view_clear_selection(self->board_view);
  gcheckers_window_set_default_puzzle_message(self);
  gcheckers_window_sync_puzzle_ui(self);
  g_object_unref(self);
  return G_SOURCE_REMOVE;
}

static gboolean gcheckers_window_play_next_puzzle_step_if_needed(GCheckersWindow *self) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), FALSE);

  if (!self->puzzle_mode || self->puzzle_steps == NULL) {
    return FALSE;
  }

  while (self->puzzle_expected_step < self->puzzle_steps->len) {
    GCheckersWindowPuzzleStep *step =
        &g_array_index(self->puzzle_steps, GCheckersWindowPuzzleStep, self->puzzle_expected_step);
    if (step->color == self->puzzle_attacker) {
      break;
    }
    if (!gcheckers_sgf_controller_apply_move(self->sgf_controller, &step->move)) {
      g_debug("Failed to apply puzzle defense move at step %u", self->puzzle_expected_step);
      return FALSE;
    }
    self->puzzle_expected_step++;
  }

  if (self->puzzle_expected_step >= self->puzzle_steps->len) {
    self->puzzle_finished = TRUE;
    gcheckers_window_set_puzzle_message(self, "Puzzle solved. Next puzzle is ready.");
  } else {
    gcheckers_window_set_default_puzzle_message(self);
  }
  gcheckers_window_sync_puzzle_ui(self);
  return TRUE;
}

static void gcheckers_window_leave_puzzle_mode(GCheckersWindow *self, gboolean restore_drawers) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  if (!self->puzzle_mode) {
    return;
  }

  g_clear_handle_id(&self->puzzle_wrong_move_source_id, g_source_remove);
  self->puzzle_feedback_locked = FALSE;
  self->puzzle_mode = FALSE;
  self->puzzle_finished = FALSE;
  self->puzzle_expected_step = 0;
  self->puzzle_attacker = CHECKERS_COLOR_WHITE;
  if (self->puzzle_steps != NULL) {
    g_array_unref(self->puzzle_steps);
    self->puzzle_steps = NULL;
  }

  self->board_orientation_mode = self->puzzle_saved_board_orientation_mode;
  self->board_bottom_color = self->puzzle_saved_board_bottom_color;
  if (restore_drawers) {
    self->show_navigation_drawer = self->puzzle_saved_show_navigation_drawer;
    self->show_analysis_drawer = self->puzzle_saved_show_analysis_drawer;
  }
  gcheckers_window_sync_board_orientation(self);
  gcheckers_window_sync_drawer_ui(self);
  gcheckers_window_sync_puzzle_ui(self);
  gcheckers_window_sync_mode_ui(self);
  gcheckers_window_analysis_sync_ui(self);
}

static gboolean gcheckers_window_enter_puzzle_mode_with_path(GCheckersWindow *self, const char *path) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  gcheckers_window_set_analysis_current_action_state(self, FALSE);
  gcheckers_window_stop_analysis(self);

  g_autoptr(GError) error = NULL;
  if (!gcheckers_sgf_controller_load_file(self->sgf_controller, path, &error)) {
    g_debug("Failed to load puzzle file %s: %s", path, error != NULL ? error->message : "unknown error");
    return FALSE;
  }

  SgfTree *tree = gcheckers_sgf_controller_get_tree(self->sgf_controller);
  if (tree == NULL) {
    g_debug("Puzzle file load did not produce an SGF tree");
    return FALSE;
  }

  g_autoptr(GArray) steps = g_array_new(FALSE, FALSE, sizeof(GCheckersWindowPuzzleStep));
  if (!gcheckers_window_load_puzzle_steps_from_tree(tree, steps)) {
    g_debug("Puzzle file %s did not contain a valid main-line solution", path);
    return FALSE;
  }

  const GameState *state = gcheckers_model_peek_state(self->model);
  if (state == NULL) {
    g_debug("Missing model state after puzzle load");
    return FALSE;
  }

  GCheckersWindowPuzzleStep *first_step = &g_array_index(steps, GCheckersWindowPuzzleStep, 0);
  if (first_step->color != state->turn) {
    g_debug("Puzzle file %s first move does not match side to move", path);
    return FALSE;
  }

  if (!self->puzzle_mode) {
    self->puzzle_saved_show_navigation_drawer = self->show_navigation_drawer;
    self->puzzle_saved_show_analysis_drawer = self->show_analysis_drawer;
    self->puzzle_saved_board_orientation_mode = self->board_orientation_mode;
    self->puzzle_saved_board_bottom_color = self->board_bottom_color;
  } else if (self->puzzle_steps != NULL) {
    g_array_unref(self->puzzle_steps);
    self->puzzle_steps = NULL;
  }

  g_clear_handle_id(&self->puzzle_wrong_move_source_id, g_source_remove);
  self->puzzle_feedback_locked = FALSE;
  self->puzzle_mode = TRUE;
  self->puzzle_finished = FALSE;
  self->puzzle_attacker = state->turn;
  self->puzzle_expected_step = 0;
  self->puzzle_steps = g_steal_pointer(&steps);
  self->show_navigation_drawer = FALSE;
  self->show_analysis_drawer = FALSE;
  self->edit_mode_enabled = FALSE;
  if (self->sgf_mode_control != NULL) {
    gtk_drop_down_set_selected(self->sgf_mode_control, 0);
  }
  board_view_clear_selection(self->board_view);
  gcheckers_window_set_board_orientation_mode(self, GCHECKERS_WINDOW_BOARD_ORIENTATION_FIXED);
  gcheckers_window_set_board_bottom_color(self, self->puzzle_attacker);
  gcheckers_window_set_default_puzzle_message(self);
  gcheckers_window_sync_drawer_ui(self);
  gcheckers_window_sync_puzzle_ui(self);
  gcheckers_window_sync_mode_ui(self);
  gcheckers_window_analysis_sync_ui(self);
  return TRUE;
}

static gboolean gcheckers_window_start_random_puzzle_mode(GCheckersWindow *self) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), FALSE);

  g_autoptr(GPtrArray) puzzle_paths = g_ptr_array_new_with_free_func(g_free);
  const char *dir_path = gcheckers_window_puzzles_dir();
  if (!gcheckers_window_collect_puzzle_paths(dir_path, puzzle_paths)) {
    return FALSE;
  }
  if (puzzle_paths->len == 0) {
    g_debug("No puzzle files found in %s", dir_path);
    return FALSE;
  }

  guint index = g_random_int_range(0, (gint)puzzle_paths->len);
  const char *path = g_ptr_array_index(puzzle_paths, index);
  g_return_val_if_fail(path != NULL, FALSE);
  return gcheckers_window_enter_puzzle_mode_with_path(self, path);
}

static gboolean gcheckers_window_apply_player_move(const CheckersMove *move, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self->sgf_controller), FALSE);

  if (self->puzzle_mode) {
    if (self->puzzle_feedback_locked || self->puzzle_finished || self->puzzle_steps == NULL) {
      return FALSE;
    }
    if (self->puzzle_expected_step >= self->puzzle_steps->len) {
      g_debug("Puzzle move attempted after the solution was already complete");
      return FALSE;
    }

    GCheckersWindowPuzzleStep *expected =
        &g_array_index(self->puzzle_steps, GCheckersWindowPuzzleStep, self->puzzle_expected_step);
    if (!gcheckers_window_moves_equal(move, &expected->move)) {
      self->puzzle_feedback_locked = TRUE;
      gcheckers_window_set_puzzle_message(self, "Wrong move.");
      if (!gcheckers_model_apply_move(self->model, move)) {
        self->puzzle_feedback_locked = FALSE;
        gcheckers_window_set_default_puzzle_message(self);
        return FALSE;
      }
      board_view_clear_selection(self->board_view);
      self->puzzle_wrong_move_source_id = g_timeout_add_full(G_PRIORITY_DEFAULT,
                                                             GCHECKERS_WINDOW_PUZZLE_WRONG_MOVE_DELAY_MS,
                                                             gcheckers_window_revert_wrong_puzzle_move_cb,
                                                             g_object_ref(self),
                                                             NULL);
      return TRUE;
    }

    if (!gcheckers_sgf_controller_apply_move(self->sgf_controller, move)) {
      return FALSE;
    }

    self->puzzle_expected_step++;
    return gcheckers_window_play_next_puzzle_step_if_needed(self);
  }

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

guint gcheckers_window_get_analysis_depth(GCheckersWindow *self) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), GCHECKERS_WINDOW_ANALYSIS_DEPTH_DEFAULT);
  g_return_val_if_fail(self->analysis_depth_scale != NULL, GCHECKERS_WINDOW_ANALYSIS_DEPTH_DEFAULT);

  guint depth = (guint)gtk_range_get_value(GTK_RANGE(self->analysis_depth_scale));
  if (!gcheckers_window_analysis_depth_valid(depth)) {
    g_debug("Unexpected analysis depth value");
    return GCHECKERS_WINDOW_ANALYSIS_DEPTH_DEFAULT;
  }

  return depth;
}

void gcheckers_window_set_analysis_depth(GCheckersWindow *self, guint depth) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(gcheckers_window_analysis_depth_valid(depth));
  g_return_if_fail(self->analysis_depth_scale != NULL);

  gtk_range_set_value(GTK_RANGE(self->analysis_depth_scale), (gdouble)depth);
}

static gboolean gcheckers_window_choose_computer_move(GCheckersWindow *self, CheckersMove *move) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(self->controls_panel != NULL, FALSE);
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self->sgf_controller), FALSE);

  guint configured_depth = player_controls_panel_get_computer_depth(self->controls_panel);
  return gcheckers_sgf_controller_step_ai_move(self->sgf_controller, configured_depth, move);
}

static void gcheckers_window_set_ruleset(GCheckersWindow *self, PlayerRuleset ruleset) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self->sgf_controller));

  const CheckersRules *rules = checkers_ruleset_get_rules(ruleset);
  if (rules == NULL) {
    return;
  }

  if (ruleset == self->applied_ruleset) {
    const GameState *state = gcheckers_model_peek_state(self->model);
    if (state != NULL) {
      if (state->board.board_size == rules->board_size) {
        return;
      }
    }
  }

  gcheckers_model_set_rules(self->model, rules);
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

static gboolean gcheckers_window_should_cancel_full_analysis(gpointer user_data) {
  GCheckersWindowFullAnalysisTask *task = user_data;
  g_return_val_if_fail(task != NULL, TRUE);
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(task->self), TRUE);

  return g_atomic_int_get(&task->self->analysis_generation) != task->generation;
}

static SgfNodeAnalysis *gcheckers_window_analysis_from_scored_moves(const CheckersScoredMoveList *moves,
                                                                    guint depth,
                                                                    const CheckersAiSearchStats *stats) {
  g_return_val_if_fail(moves != NULL, NULL);
  g_return_val_if_fail(stats != NULL, NULL);

  SgfNodeAnalysis *analysis = sgf_node_analysis_new();
  if (analysis == NULL) {
    return NULL;
  }

  analysis->depth = depth;
  analysis->nodes = stats->nodes;
  analysis->tt_probes = stats->tt_probes;
  analysis->tt_hits = stats->tt_hits;
  analysis->tt_cutoffs = stats->tt_cutoffs;

  for (guint i = 0; i < moves->count; ++i) {
    if (!sgf_node_analysis_add_scored_move(analysis,
                                           &moves->moves[i].move,
                                           moves->moves[i].score,
                                           moves->moves[i].nodes)) {
      sgf_node_analysis_free(analysis);
      return NULL;
    }
  }

  return analysis;
}

static gboolean gcheckers_window_node_first_score(const SgfNode *node, gint *out_score) {
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

static void gcheckers_window_analysis_event_free(gpointer data) {
  GCheckersWindowAnalysisEvent *event = data;
  if (event == NULL) {
    return;
  }

  g_clear_pointer(&event->status_text, g_free);
  sgf_node_analysis_free(event->analysis);
  g_free(event);
}

static void gcheckers_window_maybe_finish_full_analysis(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  if (self->analysis_mode != GCHECKERS_WINDOW_ANALYSIS_MODE_FULL_GAME) {
    return;
  }
  if (!self->analysis_done_received) {
    return;
  }
  if (!self->analysis_canceled && self->analysis_processed_nodes < self->analysis_expected_nodes) {
    return;
  }

  gcheckers_window_analysis_finish_session(self);
}

static gboolean gcheckers_window_analysis_dispatch_cb(gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), G_SOURCE_REMOVE);

  while (TRUE) {
    GCheckersWindowAnalysisEvent *event = NULL;

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
              event->is_status);
      gcheckers_window_analysis_event_free(event);
      continue;
    }

    if (event->is_status && event->status_text != NULL) {
      gcheckers_window_set_analysis_text(self, event->status_text);
      if (event->mode == GCHECKERS_WINDOW_ANALYSIS_MODE_FULL_GAME && !event->done) {
        self->analysis_processed_nodes++;
      }
    }

    if (event->is_payload && event->analysis != NULL && event->node != NULL) {
      if (!sgf_node_set_analysis((SgfNode *)event->node, event->analysis)) {
        g_debug("Failed to attach analysis to selected SGF node");
      } else {
        if (event->mode == GCHECKERS_WINDOW_ANALYSIS_MODE_FULL_GAME) {
          self->analysis_attached_nodes++;
          self->analysis_last_updated_node = event->node;
        }
        gcheckers_window_refresh_analysis_graph(self);
      }
    }

    if (event->done && event->mode == GCHECKERS_WINDOW_ANALYSIS_MODE_FULL_GAME) {
      self->analysis_done_received = TRUE;
      self->analysis_canceled = event->canceled;
    }

    gcheckers_window_analysis_event_free(event);
  }

  gcheckers_window_maybe_finish_full_analysis(self);
  g_object_unref(self);
  return G_SOURCE_REMOVE;
}

static void gcheckers_window_analysis_enqueue_event(GCheckersWindow *self, GCheckersWindowAnalysisEvent *event) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(event != NULL);

  g_mutex_lock(&self->analysis_report_mutex);
  g_return_if_fail(self->analysis_report_queue != NULL);
  g_queue_push_tail(self->analysis_report_queue, event);
  g_mutex_unlock(&self->analysis_report_mutex);

  g_main_context_invoke(NULL, gcheckers_window_analysis_dispatch_cb, g_object_ref(self));
}

static void gcheckers_window_analysis_publish_status(GCheckersWindow *self,
                                                     gint generation,
                                                     GCheckersWindowAnalysisMode mode,
                                                     gboolean done,
                                                     gboolean canceled,
                                                     const char *text) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(text != NULL);

  GCheckersWindowAnalysisEvent *event = g_new0(GCheckersWindowAnalysisEvent, 1);
  event->generation = generation;
  event->mode = mode;
  event->done = done;
  event->canceled = canceled;
  event->is_status = TRUE;
  event->status_text = g_strdup(text);
  gcheckers_window_analysis_enqueue_event(self, event);
}

static void gcheckers_window_analysis_publish_payload(GCheckersWindow *self,
                                                      gint generation,
                                                      GCheckersWindowAnalysisMode mode,
                                                      const SgfNodeAnalysis *analysis,
                                                      const SgfNode *node) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(analysis != NULL);
  g_return_if_fail(node != NULL);

  GCheckersWindowAnalysisEvent *event = g_new0(GCheckersWindowAnalysisEvent, 1);
  event->generation = generation;
  event->mode = mode;
  event->is_payload = TRUE;
  event->analysis = sgf_node_analysis_copy(analysis);
  event->node = node;
  if (event->analysis == NULL) {
    g_debug("Failed to copy payload analysis event");
    gcheckers_window_analysis_event_free(event);
    return;
  }
  gcheckers_window_analysis_enqueue_event(self, event);
}

static void gcheckers_window_refresh_analysis_graph(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(ANALYSIS_IS_GRAPH(self->analysis_graph));

  SgfTree *tree = gcheckers_sgf_controller_get_tree(self->sgf_controller);
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
    gboolean has_score = gcheckers_window_node_first_score(node, &score);
    (void)score;
    (void)current;
    if (has_score) {
      analyzed_count++;
    }
  }
  (void)analyzed_count;

  analysis_graph_set_nodes(self->analysis_graph, branch, selected_index);
  if (self->analysis_mode != GCHECKERS_WINDOW_ANALYSIS_MODE_FULL_GAME || self->analysis_last_updated_node == NULL) {
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

char *gcheckers_window_format_analysis_score(gint score) {
  gint abs_score = ABS(score);
  if (abs_score >= 2900 && abs_score <= 3000) {
    gint distance = 3000 - abs_score;
    return g_strdup_printf("%s win in %d", score > 0 ? "White" : "Black", distance);
  }

  return g_strdup_printf("%+d", score);
}

static void gcheckers_window_analysis_append_scored_moves(GString *text, const SgfNodeAnalysis *analysis) {
  g_return_if_fail(text != NULL);
  g_return_if_fail(analysis != NULL);
  g_return_if_fail(analysis->moves != NULL);

  g_string_append(text, "Best to worst:\n");
  for (guint i = 0; i < analysis->moves->len; ++i) {
    const SgfNodeScoredMove *entry = g_ptr_array_index(analysis->moves, i);
    if (entry == NULL) {
      continue;
    }

    char notation[128];
    if (!game_format_move_notation(&entry->move, notation, sizeof(notation))) {
      g_strlcpy(notation, "?", sizeof(notation));
    }

    g_autofree char *score_text = gcheckers_window_format_analysis_score(entry->score);
    if (score_text == NULL) {
      g_debug("Failed to format analysis score");
      continue;
    }
    g_string_append_printf(text,
                           "%u. %s : %s (%" G_GUINT64_FORMAT " nodes)\n",
                           i + 1,
                           notation,
                           score_text,
                           entry->nodes);
  }
}

char *gcheckers_window_format_analysis_report(const SgfNodeAnalysis *analysis) {
  g_return_val_if_fail(analysis != NULL, NULL);
  g_return_val_if_fail(analysis->moves != NULL, NULL);

  GString *text = g_string_new(NULL);
  g_string_append_printf(text, "Analysis depth: %u\n", analysis->depth);
  g_string_append_printf(text, "Nodes: %" G_GUINT64_FORMAT "\n", analysis->nodes);
  gcheckers_window_analysis_append_scored_moves(text, analysis);
  return g_string_free(text, FALSE);
}

static char *gcheckers_window_analysis_format_complete(const SgfNodeAnalysis *analysis) {
  return gcheckers_window_format_analysis_report(analysis);
}

static char *gcheckers_window_analysis_format_progress(const GCheckersWindowAnalysisTask *task,
                                                       const CheckersAiSearchStats *stats) {
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
  gcheckers_window_analysis_append_scored_moves(text, task->last_completed_analysis);
  return g_string_free(text, FALSE);
}

static void gcheckers_window_analysis_on_progress(const CheckersAiSearchStats *stats, gpointer user_data) {
  GCheckersWindowAnalysisTask *task = user_data;
  g_return_if_fail(stats != NULL);
  g_return_if_fail(task != NULL);
  g_return_if_fail(GCHECKERS_IS_WINDOW(task->self));

  const gint64 interval_us = (gint64)GCHECKERS_WINDOW_ANALYSIS_PROGRESS_INTERVAL_MS * 1000;
  gint64 now = g_get_monotonic_time();
  if (task->last_progress_publish_us != 0 && now - task->last_progress_publish_us < interval_us) {
    return;
  }

  task->last_progress_publish_us = now;
  g_autofree char *text = gcheckers_window_analysis_format_progress(task, stats);
  if (text == NULL) {
    g_debug("Failed to format analysis progress text");
    return;
  }

  gcheckers_window_analysis_publish_status(task->self,
                                           task->generation,
                                           GCHECKERS_WINDOW_ANALYSIS_MODE_CURRENT,
                                           FALSE,
                                           FALSE,
                                           text);
}

static gpointer gcheckers_window_analysis_thread(gpointer user_data) {
  GCheckersWindowAnalysisTask *task = user_data;
  g_return_val_if_fail(task != NULL, NULL);

  for (guint depth = GCHECKERS_WINDOW_ANALYSIS_DEPTH_MIN;
       depth <= task->target_depth && !gcheckers_window_should_cancel_analysis(task);
       ++depth) {
    task->current_depth = depth;
    task->last_progress_publish_us = 0;
    gcheckers_window_analysis_on_progress(&task->cumulative_stats, task);

    CheckersScoredMoveList moves = {0};
    gboolean ok = checkers_ai_alpha_beta_analyze_moves_cancellable_with_tt(
        &task->game,
        depth,
        &moves,
        gcheckers_window_should_cancel_analysis,
        task,
        gcheckers_window_analysis_on_progress,
        task,
        task->tt,
        &task->cumulative_stats);
    if (!ok) {
      if (!gcheckers_window_should_cancel_analysis(task)) {
        gcheckers_window_analysis_publish_status(task->self,
                                                 task->generation,
                                                 GCHECKERS_WINDOW_ANALYSIS_MODE_CURRENT,
                                                 TRUE,
                                                 FALSE,
                                                 "No legal moves to analyze.");
      }
      break;
    }

    g_autoptr(SgfNodeAnalysis) analysis = gcheckers_window_analysis_from_scored_moves(&moves,
                                                                                       depth,
                                                                                       &task->cumulative_stats);
    checkers_scored_move_list_free(&moves);
    if (analysis == NULL) {
      g_debug("Failed to build SGF node analysis payload");
      break;
    }

    g_autofree char *text = gcheckers_window_analysis_format_complete(analysis);
    if (text == NULL) {
      break;
    }

    sgf_node_analysis_free(task->last_completed_analysis);
    task->last_completed_analysis = sgf_node_analysis_copy(analysis);
    task->last_completed_depth = depth;
    gcheckers_window_analysis_publish_payload(task->self,
                                              task->generation,
                                              GCHECKERS_WINDOW_ANALYSIS_MODE_CURRENT,
                                              analysis,
                                              task->target_node);
    gcheckers_window_analysis_publish_status(task->self,
                                             task->generation,
                                             GCHECKERS_WINDOW_ANALYSIS_MODE_CURRENT,
                                             FALSE,
                                             FALSE,
                                             text);
  }

  gcheckers_window_analysis_publish_status(task->self,
                                           task->generation,
                                           GCHECKERS_WINDOW_ANALYSIS_MODE_CURRENT,
                                           TRUE,
                                           gcheckers_window_should_cancel_analysis(task),
                                           "Analysis stopped.");

  g_object_unref(task->self);
  checkers_ai_tt_free(task->tt);
  sgf_node_analysis_free(task->last_completed_analysis);
  g_free(task);
  return NULL;
}

static void gcheckers_window_full_node_job_free(gpointer data) {
  GCheckersWindowFullNodeJob *job = data;
  if (job == NULL) {
    return;
  }

  g_clear_pointer(&job->moves, g_array_unref);
  g_free(job);
}

static GPtrArray *gcheckers_window_build_full_analysis_jobs(SgfTree *tree) {
  g_return_val_if_fail(SGF_IS_TREE(tree), NULL);

  g_autoptr(GPtrArray) nodes = sgf_tree_collect_nodes_preorder(tree);
  if (nodes == NULL) {
    g_debug("Failed to collect SGF nodes for full analysis");
    return NULL;
  }

  GPtrArray *jobs = g_ptr_array_new_with_free_func(gcheckers_window_full_node_job_free);
  for (guint i = 0; i < nodes->len; ++i) {
    const SgfNode *node = g_ptr_array_index(nodes, i);
    g_return_val_if_fail(node != NULL, NULL);

    g_autoptr(GPtrArray) path = sgf_tree_build_path_to_node(tree, node);
    if (path == NULL) {
      g_debug("Failed to build SGF path for full analysis node");
      g_ptr_array_unref(jobs);
      return NULL;
    }

    GCheckersWindowFullNodeJob *job = g_new0(GCheckersWindowFullNodeJob, 1);
    job->node = node;
    job->moves = g_array_new(FALSE, FALSE, sizeof(CheckersMove));

    gboolean path_ok = TRUE;
    for (guint step_i = 1; step_i < path->len; ++step_i) {
      const SgfNode *step = g_ptr_array_index(path, step_i);
      g_return_val_if_fail(step != NULL, NULL);

      CheckersMove move = {0};
      SgfColor color = SGF_COLOR_NONE;
      gboolean has_move = FALSE;
      g_autoptr(GError) error = NULL;
      if (!sgf_move_props_try_parse_node(step, &color, &move, &has_move, &error)) {
        g_debug("Failed to parse SGF path move for full analysis node %u: %s",
                sgf_node_get_move_number(step),
                error != NULL ? error->message : "unknown error");
        path_ok = FALSE;
        break;
      }
      if (!has_move) {
        continue;
      }
      g_array_append_val(job->moves, move);
    }

    if (!path_ok) {
      gcheckers_window_full_node_job_free(job);
      g_ptr_array_unref(jobs);
      return NULL;
    }

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

static gpointer gcheckers_window_full_analysis_thread(gpointer user_data) {
  GCheckersWindowFullAnalysisTask *task = user_data;
  g_return_val_if_fail(task != NULL, NULL);
  g_return_val_if_fail(task->jobs != NULL, NULL);

  for (guint i = 0; i < task->jobs->len; ++i) {
    if (gcheckers_window_should_cancel_full_analysis(task)) {
      break;
    }

    GCheckersWindowFullNodeJob *job = g_ptr_array_index(task->jobs, i);
    g_return_val_if_fail(job != NULL, NULL);

    Game game = {0};
    game_init_with_rules(&game, task->rules);
    gboolean replay_ok = TRUE;
    for (guint move_i = 0; move_i < job->moves->len; ++move_i) {
      const CheckersMove *move = &g_array_index(job->moves, CheckersMove, move_i);
      if (game_apply_move(&game, move) != 0) {
        replay_ok = FALSE;
        break;
      }
    }

    if (!replay_ok) {
      game_destroy(&game);
      g_debug("Skipping full analysis for SGF node with invalid replay path");
      g_autofree char *text = g_strdup_printf("Full-game analysis: %u/%u nodes processed (replay skipped).",
                                              i + 1,
                                              task->jobs->len);
      gcheckers_window_analysis_publish_status(task->self,
                                               task->generation,
                                               GCHECKERS_WINDOW_ANALYSIS_MODE_FULL_GAME,
                                               FALSE,
                                               FALSE,
                                               text);
      continue;
    }

    CheckersScoredMoveList moves = {0};
    CheckersAiSearchStats stats = {0};
    gboolean ok = checkers_ai_alpha_beta_analyze_moves_cancellable_with_tt(
        &game,
        task->depth,
        &moves,
        gcheckers_window_should_cancel_full_analysis,
        task,
        NULL,
        NULL,
        task->tt,
        &stats);
    game_destroy(&game);
    if (!ok) {
      checkers_scored_move_list_free(&moves);
      g_autofree char *text = g_strdup_printf("Full-game analysis: %u/%u nodes processed (no legal moves).",
                                              i + 1,
                                              task->jobs->len);
      gcheckers_window_analysis_publish_status(task->self,
                                               task->generation,
                                               GCHECKERS_WINDOW_ANALYSIS_MODE_FULL_GAME,
                                               FALSE,
                                               FALSE,
                                               text);
      continue;
    }

    g_autoptr(SgfNodeAnalysis) analysis = gcheckers_window_analysis_from_scored_moves(&moves, task->depth, &stats);
    checkers_scored_move_list_free(&moves);
    if (analysis == NULL) {
      g_autofree char *text =
          g_strdup_printf("Full-game analysis: %u/%u nodes processed (analysis payload failed).",
                          i + 1,
                          task->jobs->len);
      gcheckers_window_analysis_publish_status(task->self,
                                               task->generation,
                                               GCHECKERS_WINDOW_ANALYSIS_MODE_FULL_GAME,
                                               FALSE,
                                               FALSE,
                                               text);
      continue;
    }

    g_autofree char *text = g_strdup_printf("Full-game analysis: %u/%u nodes analyzed.",
                                            i + 1,
                                            task->jobs->len);
    gint first_score = 0;
    gboolean has_first_score = FALSE;
    if (analysis->moves != NULL && analysis->moves->len > 0) {
      const SgfNodeScoredMove *entry = g_ptr_array_index(analysis->moves, 0);
      if (entry != NULL) {
        first_score = entry->score;
        has_first_score = TRUE;
      }
    }
    gcheckers_window_analysis_publish_payload(task->self,
                                              task->generation,
                                              GCHECKERS_WINDOW_ANALYSIS_MODE_FULL_GAME,
                                              analysis,
                                              job->node);
    gcheckers_window_analysis_publish_status(task->self,
                                             task->generation,
                                             GCHECKERS_WINDOW_ANALYSIS_MODE_FULL_GAME,
                                             FALSE,
                                             FALSE,
                                             text);
    (void)first_score;
    (void)has_first_score;
  }

  const gboolean canceled = gcheckers_window_should_cancel_full_analysis(task);
  gcheckers_window_analysis_publish_status(task->self,
                                           task->generation,
                                           GCHECKERS_WINDOW_ANALYSIS_MODE_FULL_GAME,
                                           TRUE,
                                           canceled,
                                           canceled ? "Full-game analysis canceled."
                                                    : "Full-game analysis complete.");

  g_ptr_array_unref(task->jobs);
  checkers_ai_tt_free(task->tt);
  g_object_unref(task->self);
  g_free(task);
  return NULL;
}

static void gcheckers_window_stop_analysis(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  if (self->analysis_mode == GCHECKERS_WINDOW_ANALYSIS_MODE_NONE) {
    gcheckers_window_analysis_finish_session(self);
    return;
  }

  g_atomic_int_inc(&self->analysis_generation);

  g_mutex_lock(&self->analysis_report_mutex);
  if (self->analysis_report_queue != NULL) {
    g_queue_clear_full(self->analysis_report_queue, gcheckers_window_analysis_event_free);
  }
  g_mutex_unlock(&self->analysis_report_mutex);
  gcheckers_window_analysis_finish_session(self);
}

static void gcheckers_window_start_analysis(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self->sgf_controller));

  Game game = {0};
  if (!gcheckers_model_copy_game(self->model, &game)) {
    g_debug("Failed to snapshot game for threaded analysis");
    return;
  }

  SgfTree *tree = gcheckers_sgf_controller_get_tree(self->sgf_controller);
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
  gcheckers_window_analysis_begin_session(self, GCHECKERS_WINDOW_ANALYSIS_MODE_CURRENT, 0);
  gcheckers_window_set_analysis_text(self, "Analyzing...");

  GCheckersWindowAnalysisTask *task = g_new0(GCheckersWindowAnalysisTask, 1);
  task->self = g_object_ref(self);
  task->game = game;
  task->generation = generation;
  task->target_node = target_node;
  task->target_depth = gcheckers_window_get_analysis_depth(self);
  task->tt = checkers_ai_tt_new(GCHECKERS_WINDOW_ANALYSIS_TT_SIZE_MB);
  if (task->tt == NULL) {
    g_debug("Failed to allocate analysis TT, continuing without TT caching");
  }
  GThread *thread = g_thread_new("analysis-thread", gcheckers_window_analysis_thread, task);
  g_thread_unref(thread);
}

static void gcheckers_window_start_full_game_analysis(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self->sgf_controller));
  g_return_if_fail(self->controls_panel != NULL);

  SgfTree *tree = gcheckers_sgf_controller_get_tree(self->sgf_controller);
  if (tree == NULL) {
    g_debug("Missing SGF tree for full-game analysis");
    return;
  }

  g_autoptr(GPtrArray) jobs = gcheckers_window_build_full_analysis_jobs(tree);
  if (jobs == NULL) {
    g_debug("Failed to build full-game analysis jobs");
    return;
  }

  Game game = {0};
  if (!gcheckers_model_copy_game(self->model, &game)) {
    g_debug("Failed to copy model game for full analysis setup");
    return;
  }
  const CheckersRules *rules = game.rules;
  if (rules == NULL) {
    g_debug("Missing rules for full-game analysis");
    return;
  }

  guint depth = gcheckers_window_get_analysis_depth(self);
  gint generation = g_atomic_int_add(&self->analysis_generation, 1) + 1;
  gcheckers_window_analysis_begin_session(self, GCHECKERS_WINDOW_ANALYSIS_MODE_FULL_GAME, jobs->len);
  gcheckers_window_set_analysis_text(self, "Analyzing full game...");

  GCheckersWindowFullAnalysisTask *task = g_new0(GCheckersWindowFullAnalysisTask, 1);
  task->self = g_object_ref(self);
  task->generation = generation;
  task->rules = rules;
  task->depth = depth;
  task->jobs = g_steal_pointer(&jobs);
  task->tt = checkers_ai_tt_new(GCHECKERS_WINDOW_ANALYSIS_TT_SIZE_MB);
  if (task->tt == NULL) {
    g_debug("Failed to allocate full analysis TT, continuing without TT caching");
  }

  GThread *thread = g_thread_new("full-analysis-thread", gcheckers_window_full_analysis_thread, task);
  g_thread_unref(thread);
}

static void gcheckers_window_restart_analysis_if_active(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  GAction *action = g_action_map_lookup_action(G_ACTION_MAP(self), "analysis-current-position");
  if (action == NULL || !G_IS_SIMPLE_ACTION(action)) {
    return;
  }

  GVariant *state = g_action_get_state(action);
  gboolean active = state != NULL && g_variant_get_boolean(state);
  g_clear_pointer(&state, g_variant_unref);
  if (!active) {
    return;
  }
  if (self->analysis_mode == GCHECKERS_WINDOW_ANALYSIS_MODE_FULL_GAME) {
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

  if (gcheckers_window_is_edit_mode(self)) {
    return;
  }
  if (self->puzzle_mode) {
    return;
  }

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

  gcheckers_window_sync_board_orientation(self);
  gcheckers_window_update_status(self);
  gcheckers_window_update_control_state(self);
  gcheckers_window_maybe_trigger_auto_move(self);
  gcheckers_window_restart_analysis_if_active(self);
  gcheckers_window_refresh_analysis_graph(self);
}

static void gcheckers_window_on_control_changed(PlayerControlsPanel * /*panel*/, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  gcheckers_window_sync_board_orientation(self);
  gcheckers_window_update_control_state(self);
}

static void gcheckers_window_on_mode_selected_notify(GObject * /*object*/,
                                                     GParamSpec * /*pspec*/,
                                                     gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GTK_IS_DROP_DOWN(self->sgf_mode_control));

  if (self->puzzle_mode) {
    gtk_drop_down_set_selected(self->sgf_mode_control, 0);
    return;
  }

  self->edit_mode_enabled = gtk_drop_down_get_selected(self->sgf_mode_control) == 1;
  board_view_clear_selection(self->board_view);
  gcheckers_window_sync_mode_ui(self);
}

static void gcheckers_window_on_manual_requested(GCheckersSgfController * /*controller*/,
                                                 gpointer /*node*/,
                                                 gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(self->controls_panel != NULL);

  gcheckers_window_set_board_orientation_mode(self, GCHECKERS_WINDOW_BOARD_ORIENTATION_FIXED);
  player_controls_panel_set_all_user(self->controls_panel);
  gcheckers_window_update_control_state(self);

  SgfTree *tree = gcheckers_sgf_controller_get_tree(self->sgf_controller);
  const SgfNode *node = tree != NULL ? sgf_tree_get_current(tree) : NULL;
  if (node != NULL) {
    g_autoptr(SgfNodeAnalysis) analysis = sgf_node_get_analysis(node);
    if (analysis != NULL) {
      g_autofree char *text = gcheckers_window_analysis_format_complete(analysis);
      if (text != NULL) {
        gcheckers_window_set_analysis_text(self, text);
      }
    } else {
      gcheckers_window_set_analysis_text(self, "No analysis saved for this node.");
    }
  }
  gcheckers_window_refresh_analysis_graph(self);

  gcheckers_window_restart_analysis_if_active(self);
}

static void gcheckers_window_on_sgf_node_changed(GCheckersSgfController * /*controller*/,
                                                 const SgfNode *node,
                                                 gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(node != NULL);

  gcheckers_window_refresh_analysis_graph(self);
}

static void gcheckers_window_on_analysis_graph_node_activated(AnalysisGraph * /*graph*/,
                                                              const SgfNode *node,
                                                              gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(node != NULL);

  if (gcheckers_window_is_edit_mode(self)) {
    return;
  }

  if (!gcheckers_sgf_controller_select_node(self->sgf_controller, node)) {
    g_debug("Failed to select SGF node from analysis graph");
  }
}

static void gcheckers_window_set_analysis_current_action_state(GCheckersWindow *self, gboolean active) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  GAction *action = g_action_map_lookup_action(G_ACTION_MAP(self), "analysis-current-position");
  if (action == NULL || !G_IS_SIMPLE_ACTION(action)) {
    g_debug("Missing analysis-current-position action");
    return;
  }

  g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(active));
}

static void gcheckers_window_on_analysis_current_change_state(GSimpleAction *action,
                                                              GVariant *value,
                                                              gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(G_IS_SIMPLE_ACTION(action));
  g_return_if_fail(value != NULL);
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));

  gboolean active = g_variant_get_boolean(value);
  g_simple_action_set_state(action, value);
  if (active) {
    gcheckers_window_stop_analysis(self);
    gcheckers_window_start_analysis(self);
    return;
  }

  gcheckers_window_stop_analysis(self);
}

static void gcheckers_window_on_analyze_full_game_action(GSimpleAction * /*action*/,
                                                         GVariant * /*parameter*/,
                                                         gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  gcheckers_window_set_analysis_current_action_state(self, FALSE);
  gcheckers_window_stop_analysis(self);
  gcheckers_window_start_full_game_analysis(self);
}

static void gcheckers_window_on_force_move_action(GSimpleAction * /*action*/,
                                                  GVariant * /*parameter*/,
                                                  gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  gcheckers_window_force_move(self);
}

static void gcheckers_window_on_play_puzzles_action(GSimpleAction * /*action*/,
                                                    GVariant * /*parameter*/,
                                                    gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  if (!gcheckers_window_start_random_puzzle_mode(self)) {
    g_debug("Failed to start puzzle mode");
  }
}

static void gcheckers_window_on_puzzle_next_clicked(GtkButton * /*button*/, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  if (!self->puzzle_mode || !self->puzzle_finished) {
    return;
  }
  if (!gcheckers_window_start_random_puzzle_mode(self)) {
    g_debug("Failed to load next puzzle");
  }
}

static void gcheckers_window_on_puzzle_analyze_clicked(GtkButton * /*button*/, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  if (!self->puzzle_mode) {
    return;
  }

  gcheckers_window_leave_puzzle_mode(self, TRUE);
  self->show_analysis_drawer = TRUE;
  gcheckers_window_sync_drawer_ui(self);
  gcheckers_window_set_analysis_current_action_state(self, TRUE);
}

static void gcheckers_window_on_sgf_rewind(GSimpleAction * /*action*/,
                                           GVariant * /*parameter*/,
                                           gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self->sgf_controller));
  if (gcheckers_window_is_edit_mode(self)) {
    return;
  }

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
  if (gcheckers_window_is_edit_mode(self)) {
    return;
  }

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
  if (gcheckers_window_is_edit_mode(self)) {
    return;
  }

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
  if (gcheckers_window_is_edit_mode(self)) {
    return;
  }

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
  if (gcheckers_window_is_edit_mode(self)) {
    return;
  }

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

  if (self->puzzle_mode) {
    g_debug("Ignoring forced move in puzzle mode");
    return;
  }

  if (gcheckers_window_is_edit_mode(self)) {
    return;
  }

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

  gcheckers_window_set_board_bottom_color(self, CHECKERS_COLOR_WHITE);
  if (white_mode == PLAYER_CONTROL_MODE_USER && black_mode == PLAYER_CONTROL_MODE_USER) {
    gcheckers_window_set_board_orientation_mode(self, GCHECKERS_WINDOW_BOARD_ORIENTATION_FOLLOW_TURN);
  } else if (white_mode != black_mode) {
    gcheckers_window_set_board_orientation_mode(self, GCHECKERS_WINDOW_BOARD_ORIENTATION_FOLLOW_PLAYER);
  } else {
    gcheckers_window_set_board_orientation_mode(self, GCHECKERS_WINDOW_BOARD_ORIENTATION_FIXED);
  }

  gcheckers_window_set_ruleset(self, ruleset);
  gcheckers_window_start_new_game(self);
}

void gcheckers_window_set_board_orientation_mode(GCheckersWindow *self,
                                                 GCheckersWindowBoardOrientationMode mode) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(gcheckers_window_board_orientation_mode_valid(mode));

  if (self->board_orientation_mode == mode) {
    return;
  }

  self->board_orientation_mode = mode;
  gcheckers_window_sync_board_orientation(self);
}

void gcheckers_window_set_board_bottom_color(GCheckersWindow *self, CheckersColor bottom_color) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(bottom_color == CHECKERS_COLOR_WHITE || bottom_color == CHECKERS_COLOR_BLACK);

  self->board_bottom_color = bottom_color;
  gcheckers_window_sync_board_orientation(self);
}

CheckersColor gcheckers_window_get_board_bottom_color(GCheckersWindow *self) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), CHECKERS_COLOR_WHITE);

  return self->board_bottom_color;
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
  gcheckers_window_sync_board_orientation(self);
  gcheckers_window_update_status(self);
  gcheckers_window_update_control_state(self);
  gcheckers_window_refresh_analysis_graph(self);
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

  self->edit_mode_enabled = FALSE;
  gcheckers_window_sync_mode_ui(self);

  if (self->model && self->state_handler_id != 0) {
    g_signal_handler_disconnect(self->model, self->state_handler_id);
    self->state_handler_id = 0;
  }

  gboolean panel_removed = gcheckers_window_unparent_controls_panel(self);
  g_clear_handle_id(&self->auto_move_source_id, g_source_remove);
  g_clear_handle_id(&self->puzzle_wrong_move_source_id, g_source_remove);

  gcheckers_window_unparent_controls_panel(self);

  gcheckers_window_stop_analysis(self);
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
    gcheckers_widget_remove_from_parent(self->navigation_panel);
    g_clear_object(&self->navigation_panel);
  }
  if (self->analysis_panel != NULL) {
    gcheckers_widget_remove_from_parent(self->analysis_panel);
    g_clear_object(&self->analysis_panel);
  }
  if (self->puzzle_steps != NULL) {
    g_array_unref(self->puzzle_steps);
    self->puzzle_steps = NULL;
  }
  if (self->drawer_split != NULL) {
    gcheckers_widget_remove_from_parent(self->drawer_split);
    g_clear_object(&self->drawer_split);
  }
  if (self->drawer_host != NULL) {
    gcheckers_widget_remove_from_parent(self->drawer_host);
    g_clear_object(&self->drawer_host);
  }
  g_clear_object(&self->board_view);
  g_clear_object(&self->model);
  self->main_paned = NULL;
  self->board_panel = NULL;
  self->puzzle_panel = NULL;
  self->puzzle_message_label = NULL;
  self->puzzle_next_button = NULL;
  self->puzzle_analyze_button = NULL;
  self->analysis_depth_scale = NULL;
  self->analysis_buffer = NULL;
  self->sgf_mode_control = NULL;
  G_OBJECT_CLASS(gcheckers_window_parent_class)->dispose(object);
}

static void gcheckers_window_finalize(GObject *object) {
  GCheckersWindow *self = GCHECKERS_WINDOW(object);

  if (self->analysis_report_queue != NULL) {
    g_queue_free_full(self->analysis_report_queue, gcheckers_window_analysis_event_free);
    self->analysis_report_queue = NULL;
  }
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
  self->analysis_mode = GCHECKERS_WINDOW_ANALYSIS_MODE_NONE;
  self->analysis_generation = 1;
  self->puzzle_wrong_move_source_id = 0;
  g_mutex_init(&self->analysis_report_mutex);
  self->analysis_report_queue = g_queue_new();
  gcheckers_window_analysis_reset_runtime_state(self);
  self->applied_ruleset = PLAYER_RULESET_INTERNATIONAL;

  static const GActionEntry window_actions[] = {
      {
          .name = "game-force-move",
          .activate = gcheckers_window_on_force_move_action,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "puzzle-play",
          .activate = gcheckers_window_on_play_puzzles_action,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "navigation-rewind",
          .activate = gcheckers_window_on_sgf_rewind,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "navigation-step-backward",
          .activate = gcheckers_window_on_sgf_step_backward,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "navigation-step-forward",
          .activate = gcheckers_window_on_sgf_step_forward,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "navigation-step-forward-to-branch",
          .activate = gcheckers_window_on_sgf_step_forward_to_branch,
          .parameter_type = NULL,
          .state = NULL,
          .change_state = NULL,
          .padding = {0},
      },
      {
          .name = "navigation-step-forward-to-end",
          .activate = gcheckers_window_on_sgf_step_forward_to_end,
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
          .change_state = gcheckers_window_on_show_navigation_drawer_change_state,
          .padding = {0},
      },
      {
          .name = "view-show-analysis-drawer",
          .activate = NULL,
          .parameter_type = NULL,
          .state = "true",
          .change_state = gcheckers_window_on_show_analysis_drawer_change_state,
          .padding = {0},
      },
      {
          .name = "analysis-current-position",
          .activate = NULL,
          .parameter_type = NULL,
          .state = "false",
          .change_state = gcheckers_window_on_analysis_current_change_state,
          .padding = {0},
      },
      {
          .name = "analysis-whole-game",
          .activate = gcheckers_window_on_analyze_full_game_action,
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
                                                 "win.game-force-move");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), force_move_button);

  GtkWidget *toolbar_separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), toolbar_separator);

  GtkWidget *rewind_button = gcheckers_window_new_toolbar_action_button("media-skip-backward-symbolic",
                                                                         "Rewind to start",
                                                                         "win.navigation-rewind");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), rewind_button);

  GtkWidget *step_backward_button =
      gcheckers_window_new_toolbar_action_button("go-previous-symbolic",
                                                 "Back one move",
                                                 "win.navigation-step-backward");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), step_backward_button);

  GtkWidget *step_forward_button = gcheckers_window_new_toolbar_action_button("go-next-symbolic",
                                                                               "Forward one move",
                                                                               "win.navigation-step-forward");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), step_forward_button);

  GtkWidget *step_to_branch_button =
      gcheckers_window_new_toolbar_action_button("media-seek-forward-symbolic",
                                                 "Forward to next branch point",
                                                 "win.navigation-step-forward-to-branch");
  gtk_action_bar_pack_start(GTK_ACTION_BAR(toolbar), step_to_branch_button);

  GtkWidget *step_to_end_button = gcheckers_window_new_toolbar_action_button("media-skip-forward-symbolic",
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
  self->board_panel = left_panel;
  g_object_set_data(G_OBJECT(self), "board-panel", left_panel);

  self->controls_panel = g_object_ref_sink(player_controls_panel_new());
  gtk_box_append(GTK_BOX(left_panel), GTK_WIDGET(self->controls_panel));
  g_signal_connect(self->controls_panel,
                   "control-changed",
                   G_CALLBACK(gcheckers_window_on_control_changed),
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
                   G_CALLBACK(gcheckers_window_on_puzzle_next_clicked),
                   self);
  gtk_box_append(GTK_BOX(puzzle_button_row), puzzle_next_button);
  self->puzzle_next_button = GTK_BUTTON(puzzle_next_button);
  g_object_set_data(G_OBJECT(self), "puzzle-next-button", puzzle_next_button);

  GtkWidget *puzzle_analyze_button = gtk_button_new_with_label("Analyze");
  g_signal_connect(puzzle_analyze_button,
                   "clicked",
                   G_CALLBACK(gcheckers_window_on_puzzle_analyze_clicked),
                   self);
  gtk_box_append(GTK_BOX(puzzle_button_row), puzzle_analyze_button);
  self->puzzle_analyze_button = GTK_BUTTON(puzzle_analyze_button);
  g_object_set_data(G_OBJECT(self), "puzzle-analyze-button", puzzle_analyze_button);

  self->board_view = board_view_new();
  GtkWidget *board_aspect = gtk_aspect_frame_new(0.5f, 0.5f, 1.0f, FALSE);
  gtk_widget_set_hexpand(board_aspect, TRUE);
  gtk_widget_set_vexpand(board_aspect, TRUE);
  gtk_box_append(GTK_BOX(left_panel), board_aspect);
  gtk_aspect_frame_set_child(GTK_ASPECT_FRAME(board_aspect), board_view_get_widget(self->board_view));

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
                   G_CALLBACK(gcheckers_window_on_mode_selected_notify),
                   self);
  gtk_box_append(GTK_BOX(sgf_mode_row), GTK_WIDGET(self->sgf_mode_control));
  gtk_box_append(GTK_BOX(middle_panel), sgf_mode_row);

  self->sgf_controller = gcheckers_sgf_controller_new(self->board_view);
  board_view_set_sgf_controller(self->board_view, self->sgf_controller);
  board_view_set_move_handler(self->board_view, gcheckers_window_apply_player_move, self);
  board_view_set_square_handler(self->board_view, gcheckers_window_on_board_square_action, self);
  self->analysis_graph = analysis_graph_new();
  GtkWidget *sgf_widget = gcheckers_sgf_controller_get_widget(self->sgf_controller);
  g_return_if_fail(sgf_widget != NULL);
  g_signal_connect(self->sgf_controller,
                   "manual-requested",
                   G_CALLBACK(gcheckers_window_on_manual_requested),
                   self);
  g_signal_connect(self->sgf_controller,
                   "node-changed",
                   G_CALLBACK(gcheckers_window_on_sgf_node_changed),
                   self);
  gtk_widget_add_css_class(sgf_widget, "sgf-panel");
  gtk_box_append(GTK_BOX(middle_panel), sgf_widget);

  GtkWidget *analysis_depth_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_box_append(GTK_BOX(analysis_panel), analysis_depth_box);

  GtkWidget *analysis_depth_label = gtk_label_new("Analysis depth");
  gtk_widget_set_halign(analysis_depth_label, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(analysis_depth_box), analysis_depth_label);

  self->analysis_depth_scale = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                                  GCHECKERS_WINDOW_ANALYSIS_DEPTH_MIN,
                                                                  GCHECKERS_WINDOW_ANALYSIS_DEPTH_MAX,
                                                                  1));
  gtk_scale_set_digits(self->analysis_depth_scale, 0);
  gtk_scale_set_draw_value(self->analysis_depth_scale, TRUE);
  gtk_widget_set_hexpand(GTK_WIDGET(self->analysis_depth_scale), TRUE);
  gtk_widget_set_size_request(GTK_WIDGET(self->analysis_depth_scale), 100, -1);
  gtk_box_append(GTK_BOX(analysis_depth_box), GTK_WIDGET(self->analysis_depth_scale));
  g_object_set_data(G_OBJECT(self), "analysis-depth-scale", self->analysis_depth_scale);
  gcheckers_window_set_analysis_depth(self, GCHECKERS_WINDOW_ANALYSIS_DEPTH_DEFAULT);

  GtkWidget *graph_widget = analysis_graph_get_widget(self->analysis_graph);
  g_return_if_fail(graph_widget != NULL);
  g_signal_connect(self->analysis_graph,
                   "node-activated",
                   G_CALLBACK(gcheckers_window_on_analysis_graph_node_activated),
                   self);
  g_object_set_data(G_OBJECT(self), "analysis-graph", self->analysis_graph);
  gtk_box_append(GTK_BOX(analysis_panel), graph_widget);

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
  self->show_navigation_drawer = TRUE;
  self->show_analysis_drawer = TRUE;
  self->puzzle_saved_show_navigation_drawer = TRUE;
  self->puzzle_saved_show_analysis_drawer = TRUE;
  self->board_panel_width = GCHECKERS_WINDOW_DEFAULT_BOARD_PANEL_WIDTH;
  self->navigation_panel_width = GCHECKERS_WINDOW_DEFAULT_NAVIGATION_PANEL_WIDTH;
  self->analysis_panel_width = GCHECKERS_WINDOW_DEFAULT_ANALYSIS_PANEL_WIDTH;
  self->board_orientation_mode = GCHECKERS_WINDOW_BOARD_ORIENTATION_FIXED;
  self->puzzle_saved_board_orientation_mode = GCHECKERS_WINDOW_BOARD_ORIENTATION_FIXED;
  self->board_bottom_color = CHECKERS_COLOR_WHITE;
  self->puzzle_saved_board_bottom_color = CHECKERS_COLOR_WHITE;
  self->puzzle_attacker = CHECKERS_COLOR_WHITE;
  gcheckers_window_sync_drawer_ui(self);
  gcheckers_window_sync_puzzle_ui(self);
  gcheckers_window_sync_mode_ui(self);
  gcheckers_window_analysis_sync_ui(self);
  gcheckers_window_set_analysis_text(self, "Use the Analysis menu to analyze this move or the whole game.");
  gcheckers_window_refresh_analysis_graph(self);
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
