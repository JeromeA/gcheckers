/*
 * This crash reproducer embeds copies of src/game_app_profile.c, src/game_model.c,
 * src/games/homeworlds/homeworlds_backend.c, and src/window.c below. Do not link those original source files again
 * or the reproducer stops proving that it is self-contained. Do not remove this comment when simplifying bug.c.
 */
#include <gtk/gtk.h>

/* Begin copied file: src/game_app_profile.c */
#include "game_app_profile.h"

#include "games/homeworlds/homeworlds_backend.h"
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

  G_OBJECT_CLASS(ggame_model_parent_class)->constructed(object);

  g_return_if_fail(self->backend != NULL);
  g_return_if_fail(self->backend->position_size > 0);
  g_return_if_fail(self->backend->position_init != NULL);
  g_return_if_fail(self->backend->position_clear != NULL);

  self->position = g_malloc0(self->backend->position_size);
  g_return_if_fail(self->position != NULL);

  self->backend->position_init(self->position, NULL);
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

static void ggame_model_init(GGameModel * /*self*/) {}

GGameModel *ggame_model_new(const GameBackend *backend) {
  g_return_val_if_fail(backend != NULL, NULL);

  return g_object_new(GGAME_TYPE_MODEL, "backend", backend, NULL);
}

void ggame_model_reset(GGameModel *self, const GameBackendVariant *variant_or_null) {
  g_return_if_fail(GGAME_IS_MODEL(self));
  g_return_if_fail(self->backend != NULL);
  g_return_if_fail(self->position != NULL);

  self->backend->position_clear(self->position);
  self->variant = variant_or_null;
  self->backend->position_init(self->position, self->variant);
  g_signal_emit(self, model_signals[SIGNAL_STATE_CHANGED], 0);
}

gboolean ggame_model_set_position(GGameModel *self, gconstpointer /*position*/) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), FALSE);

  g_debug("Position replacement is not available in the reproducer");
  return FALSE;
}

gboolean ggame_model_set_position_variant(GGameModel *self,
                                          gconstpointer /*position*/,
                                          const GameBackendVariant * /*variant_or_null*/) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), FALSE);

  g_debug("Position replacement is not available in the reproducer");
  return FALSE;
}

GameBackendMoveList ggame_model_list_moves(GGameModel *self) {
  GameBackendMoveList empty = {0};

  g_return_val_if_fail(GGAME_IS_MODEL(self), empty);

  g_debug("Move listing is not available in the reproducer");
  return empty;
}

gboolean ggame_model_apply_move(GGameModel *self, gconstpointer /*move*/) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), FALSE);

  g_debug("Move application is not available in the reproducer");
  return FALSE;
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

static GameBackendOutcome homeworlds_backend_position_outcome(gconstpointer position) {
  const HomeworldsPosition *homeworlds_position = position;

  g_return_val_if_fail(homeworlds_position != NULL, GAME_BACKEND_OUTCOME_ONGOING);

  return homeworlds_position_outcome(homeworlds_position);
}

const GameBackend homeworlds_game_backend = {
  .id = "homeworlds",
  .display_name = "Homeworlds",
  .position_size = sizeof(HomeworldsPosition),
  .side_label = homeworlds_backend_side_label,
  .sgf_color_for_side = homeworlds_backend_sgf_color_for_side,
  .position_init = homeworlds_backend_position_init,
  .position_clear = homeworlds_backend_position_clear,
  .position_outcome = homeworlds_backend_position_outcome,
};
/* End copied file: src/games/homeworlds/homeworlds_backend.c */

/* Begin copied file: src/window.c */
#include "game_app_profile.h"
#include "window.h"

#include "common_settings.h"
#include "widget_utils.h"

#include <string.h>

typedef enum {
  GGAME_WINDOW_LAYOUT_MODE_NORMAL = 0,
  GGAME_WINDOW_LAYOUT_MODE_PUZZLE
} GGameWindowLayoutMode;

struct _GGameWindow {
  GtkApplicationWindow parent_instance;
  const GGameAppProfile *profile;
  GtkWidget *main_paned;
  GtkWidget *board_panel;
  GtkWidget *drawer_host;
  GtkWidget *drawer_split;
  GtkWidget *navigation_panel;
  GtkWidget *analysis_panel;
  guint paned_tick_id;
  gboolean puzzle_mode;
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
};

G_DEFINE_TYPE(GGameWindow, ggame_window, GTK_TYPE_APPLICATION_WINDOW)

static void ggame_window_capture_panel_widths(GGameWindow *self);
static gint ggame_window_current_extra_width(GGameWindow *self);
static void ggame_window_apply_saved_panel_widths(GGameWindow *self);
static gint ggame_window_expected_default_width(GGameWindow *self);
static void ggame_window_sync_drawer_ui_with_capture(GGameWindow *self, gboolean capture_current_layout);
static void ggame_window_load_default_size(gint *out_width, gint *out_height);
static void ggame_window_save_default_size(GGameWindow *self);
static gboolean ggame_window_on_close_request(GtkWindow *window, gpointer user_data);

enum {
  GGAME_WINDOW_DEFAULT_BOARD_PANEL_WIDTH = 500,
  GGAME_WINDOW_DEFAULT_NAVIGATION_PANEL_WIDTH = 300,
  GGAME_WINDOW_DEFAULT_ANALYSIS_PANEL_WIDTH = 300,
  GGAME_WINDOW_DEFAULT_WIDTH = 1100,
  GGAME_WINDOW_DEFAULT_HEIGHT = 700,
  GGAME_WINDOW_ANALYSIS_DEPTH_DEFAULT = 8,
};


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

guint ggame_window_get_analysis_depth(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), GGAME_WINDOW_ANALYSIS_DEPTH_DEFAULT);

  return GGAME_WINDOW_ANALYSIS_DEPTH_DEFAULT;
}

void ggame_window_set_analysis_depth(GGameWindow *self, guint depth) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  (void)depth;
}

void ggame_window_set_loaded_variant(GGameWindow *self, const GameBackendVariant *variant) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(variant != NULL);
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

const GameBackendVariant *ggame_window_get_variant(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);

  return NULL;
}

void ggame_window_apply_new_game_settings(GGameWindow *self,
                                          const GameBackendVariant *variant,
                                          PlayerControlMode white_mode,
                                          PlayerControlMode black_mode,
                                          guint computer_depth) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  (void)variant;
  (void)white_mode;
  (void)black_mode;
  (void)computer_depth;
}

void ggame_window_set_board_orientation_mode(GGameWindow *self,
                                                 GGameWindowBoardOrientationMode mode) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  (void)mode;
}

void ggame_window_set_board_bottom_color(GGameWindow *self, CheckersColor bottom_color) {
  g_return_if_fail(GGAME_IS_WINDOW(self));
  g_return_if_fail(bottom_color == CHECKERS_COLOR_WHITE || bottom_color == CHECKERS_COLOR_BLACK);

  (void)bottom_color;
}

CheckersColor ggame_window_get_board_bottom_color(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), CHECKERS_COLOR_WHITE);

  return CHECKERS_COLOR_WHITE;
}

static void ggame_window_dispose(GObject *object) {
  GGameWindow *self = GGAME_WINDOW(object);

  if (self->paned_tick_id != 0 && self->main_paned) {
    gtk_widget_remove_tick_callback(self->main_paned, self->paned_tick_id);
    self->paned_tick_id = 0;
  }
  if (self->navigation_panel != NULL) {
    ggame_widget_remove_from_parent(self->navigation_panel);
    g_clear_object(&self->navigation_panel);
  }
  if (self->analysis_panel != NULL) {
    ggame_widget_remove_from_parent(self->analysis_panel);
    g_clear_object(&self->analysis_panel);
  }
  if (self->drawer_split != NULL) {
    ggame_widget_remove_from_parent(self->drawer_split);
    g_clear_object(&self->drawer_split);
  }
  if (self->drawer_host != NULL) {
    ggame_widget_remove_from_parent(self->drawer_host);
    g_clear_object(&self->drawer_host);
  }
  self->main_paned = NULL;
  self->board_panel = NULL;
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
  self->paned_tick_id = 0;
  self->syncing_layout_default_size = FALSE;

  gint default_width = 0;
  gint default_height = 0;
  ggame_window_load_default_size(&default_width, &default_height);
  gtk_window_set_default_size(GTK_WINDOW(self), default_width, default_height);
  g_signal_connect(self, "close-request", G_CALLBACK(ggame_window_on_close_request), self);
  g_signal_connect(self,
                   "notify::default-width",
                   G_CALLBACK(ggame_window_on_default_size_notify),
                   self);

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_window_set_child(GTK_WINDOW(self), content);

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
  ggame_window_sync_drawer_ui_with_capture(self, FALSE);
}

PlayerControlsPanel *ggame_window_get_controls_panel(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);

  g_debug("Controls panel is not available in the reproducer");
  return NULL;
}

GGameSgfController *ggame_window_get_sgf_controller(GGameWindow *self) {
  g_return_val_if_fail(GGAME_IS_WINDOW(self), NULL);

  g_debug("SGF controller is not available in the reproducer");
  return NULL;
}

void ggame_window_set_loaded_source_path(GGameWindow *self, const char *path) {
  g_return_if_fail(GGAME_IS_WINDOW(self));

  (void)path;
}

GGameWindow *ggame_window_new(GtkApplication *app, GGameModel *model) {
  g_return_val_if_fail(GTK_IS_APPLICATION(app), NULL);
  g_return_val_if_fail(GGAME_IS_MODEL(model), NULL);

  return g_object_new(GGAME_TYPE_WINDOW, "application", app, NULL);
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
