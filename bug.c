/*
 * This crash reproducer embeds copies of src/game_app_profile.c, src/game_model.c,
 * src/games/homeworlds/homeworlds_backend.c, and src/window.c below. Do not link those original source files again
 * or the reproducer stops proving that it is self-contained. Do not remove this comment when simplifying bug.c.
 */
#include <gtk/gtk.h>

/* Begin copied file: src/game_app_profile.c */
#include "game_app_profile.h"

#include "games/homeworlds/homeworlds_backend.h"

static const GGameAppProfile homeworlds_app_profile = {
  .kind = GGAME_APP_KIND_HOMEWORLDS,
  .id = "homeworlds",
  .app_id = "io.github.jeromea.ghomeworlds",
  .display_name = "Homeworlds",
  .window_title_name = "ghomeworlds",
  .backend = &homeworlds_game_backend,
  .layout =
      {
          .default_board_panel_width = 960,
          .minimum_board_panel_width = 760,
          .default_navigation_panel_width = 300,
          .default_analysis_panel_width = 300,
          .show_navigation_drawer_by_default = TRUE,
          .show_analysis_drawer_by_default = FALSE,
      },
};

const GGameAppProfile *ggame_app_profile_lookup_by_id(const char *id) {
  g_return_val_if_fail(id != NULL, NULL);

  if (g_strcmp0(id, homeworlds_app_profile.id) == 0) {
    return &homeworlds_app_profile;
  }

  g_debug("Unknown app profile id %s", id);
  return NULL;
}

const GGameAppProfile *ggame_active_app_profile(void) {
  return &homeworlds_app_profile;
}

gboolean ggame_app_profile_supports_puzzle_catalog(const GGameAppProfile *profile) {
  g_return_val_if_fail(profile != NULL, FALSE);

  return FALSE;
}
/* End copied file: src/game_app_profile.c */

/* Begin copied file: src/game_model.c */
#include "game_model.h"

struct _GGameModel {
  GObject parent_instance;
};

G_DEFINE_TYPE(GGameModel, ggame_model, G_TYPE_OBJECT)

static void ggame_model_class_init(GGameModelClass * /*klass*/) {}
static void ggame_model_init(GGameModel * /*self*/) {}

GGameModel *ggame_model_new(const GameBackend *backend) {
  g_return_val_if_fail(backend != NULL, NULL);

  return g_object_new(GGAME_TYPE_MODEL, NULL);
}

void ggame_model_reset(GGameModel *self, const GameBackendVariant * /*variant_or_null*/) {
  g_return_if_fail(GGAME_IS_MODEL(self));
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

  return NULL;
}

const GameBackend *ggame_model_peek_backend(GGameModel *self) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), NULL);

  return &homeworlds_game_backend;
}

const GameBackendVariant *ggame_model_peek_variant(GGameModel *self) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), NULL);

  return NULL;
}

char *ggame_model_format_status(GGameModel *self) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), NULL);

  return g_strdup("Homeworlds");
}
/* End copied file: src/game_model.c */

/* Begin copied file: src/games/homeworlds/homeworlds_backend.c */
#include "homeworlds_backend.h"

const GameBackend homeworlds_game_backend = {
  .id = "homeworlds",
  .display_name = "Homeworlds",
};
/* End copied file: src/games/homeworlds/homeworlds_backend.c */

/* Begin copied file: src/window.c */
#include "game_app_profile.h"
#include "window.h"

#include "widget_utils.h"

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
};

G_DEFINE_TYPE(GGameWindow, ggame_window, GTK_TYPE_APPLICATION_WINDOW)

enum {
  GGAME_WINDOW_DEFAULT_HEIGHT = 700,
  GGAME_WINDOW_ANALYSIS_DEPTH_DEFAULT = 8,
};

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
  self->profile = ggame_active_app_profile();
  self->paned_tick_id = 0;

  gtk_window_set_default_size(GTK_WINDOW(self), 1260, GGAME_WINDOW_DEFAULT_HEIGHT);

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

  ggame_widget_remove_from_parent(middle_panel);
  ggame_widget_remove_from_parent(analysis_panel);
  ggame_widget_remove_from_parent(drawer_host);
  ggame_widget_remove_from_parent(right_split);
  gtk_box_append(GTK_BOX(drawer_host), middle_panel);
  gtk_paned_set_end_child(GTK_PANED(paned), drawer_host);
  gtk_widget_set_size_request(left_panel, 760, -1);
  gtk_paned_set_position(GTK_PANED(paned), 960);
  gtk_paned_set_position(GTK_PANED(right_split), 300);
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

  (void)model;
  return g_object_new(GGAME_TYPE_WINDOW, "application", app, NULL);
}
/* End copied file: src/window.c */

static GtkApplication *test_homeworlds_app = NULL;

static void drain_main_context(void) {
  for (guint i = 0; i < 256; ++i) {
    g_main_context_iteration(NULL, FALSE);
  }
}

static GGameWindow *create_window(GtkApplication **out_app) {
  if (test_homeworlds_app == NULL) {
    test_homeworlds_app = gtk_application_new("io.github.jeromea.ghomeworlds.test",
                                              G_APPLICATION_DEFAULT_FLAGS | G_APPLICATION_NON_UNIQUE);
    g_application_register(G_APPLICATION(test_homeworlds_app), NULL, NULL);
  }

  GtkApplication *app = g_object_ref(test_homeworlds_app);
  GGameWindow *window = ggame_window_new(app, NULL);

  *out_app = app;
  return window;
}

int main(void) {
  GtkApplication *app = NULL;
  GGameWindow *window = NULL;

  g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL);

  gtk_init();

  window = create_window(&app);
  gtk_window_present(GTK_WINDOW(window));
  drain_main_context();
  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(app);

  window = create_window(&app);
  gtk_window_set_default_size(GTK_WINDOW(window), 2000, 700);
  gtk_window_present(GTK_WINDOW(window));
  drain_main_context();
  gtk_paned_set_position(GTK_PANED(g_object_get_data(G_OBJECT(window), "main-paned")), 1400);
  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(app);
}
