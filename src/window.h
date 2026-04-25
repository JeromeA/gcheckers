#ifndef GGAME_WINDOW_H
#define GGAME_WINDOW_H

#include "games/checkers/checkers_model.h"
#include "game_backend.h"
#include "sgf_controller.h"
#include "player_controls_panel.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GGAME_TYPE_WINDOW (ggame_window_get_type())

G_DECLARE_FINAL_TYPE(GGameWindow, ggame_window, GGAME, WINDOW, GtkApplicationWindow)

typedef enum {
  GGAME_WINDOW_BOARD_ORIENTATION_FIXED = 0,
  GGAME_WINDOW_BOARD_ORIENTATION_FOLLOW_PLAYER,
  GGAME_WINDOW_BOARD_ORIENTATION_FOLLOW_TURN
} GGameWindowBoardOrientationMode;

GGameWindow *ggame_window_new(GtkApplication *app, GCheckersModel *model);
void ggame_window_present_new_game_dialog(GGameWindow *self);
void ggame_window_present_puzzle_dialog(GGameWindow *self);
void ggame_window_present_import_dialog(GGameWindow *self);
void ggame_window_present_settings_dialog(GGameWindow *self);
void ggame_window_set_loaded_source_path(GGameWindow *self, const char *path);
void ggame_window_set_loaded_variant(GGameWindow *self, const GameBackendVariant *variant);
void ggame_window_force_move(GGameWindow *self);
char *ggame_window_format_analysis_score(gint score);
char *ggame_window_format_analysis_report(const SgfNodeAnalysis *analysis);
const GameBackendVariant *ggame_window_get_variant(GGameWindow *self);
gboolean ggame_window_start_puzzle_mode_for_path(GGameWindow *self,
                                                 const GameBackendVariant *variant,
                                                     const char *path);
void ggame_window_apply_new_game_settings(GGameWindow *self,
                                          const GameBackendVariant *variant,
                                              PlayerControlMode white_mode,
                                              PlayerControlMode black_mode,
                                              guint computer_depth);
void ggame_window_set_board_orientation_mode(GGameWindow *self,
                                                 GGameWindowBoardOrientationMode mode);
void ggame_window_set_board_bottom_color(GGameWindow *self, CheckersColor bottom_color);
CheckersColor ggame_window_get_board_bottom_color(GGameWindow *self);
void ggame_window_set_analysis_depth(GGameWindow *self, guint depth);
guint ggame_window_get_analysis_depth(GGameWindow *self);
PlayerControlsPanel *ggame_window_get_controls_panel(GGameWindow *self);
GGameSgfController *ggame_window_get_sgf_controller(GGameWindow *self);

G_END_DECLS

#endif
