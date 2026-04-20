#ifndef GCHECKERS_WINDOW_H
#define GCHECKERS_WINDOW_H

#include "checkers_model.h"
#include "sgf_controller.h"
#include "player_controls_panel.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GCHECKERS_TYPE_WINDOW (gcheckers_window_get_type())

G_DECLARE_FINAL_TYPE(GCheckersWindow, gcheckers_window, GCHECKERS, WINDOW, GtkApplicationWindow)

typedef enum {
  GCHECKERS_WINDOW_BOARD_ORIENTATION_FIXED = 0,
  GCHECKERS_WINDOW_BOARD_ORIENTATION_FOLLOW_PLAYER,
  GCHECKERS_WINDOW_BOARD_ORIENTATION_FOLLOW_TURN
} GCheckersWindowBoardOrientationMode;

GCheckersWindow *gcheckers_window_new(GtkApplication *app, GCheckersModel *model);
void gcheckers_window_present_new_game_dialog(GCheckersWindow *self);
void gcheckers_window_present_puzzle_dialog(GCheckersWindow *self);
void gcheckers_window_present_import_dialog(GCheckersWindow *self);
void gcheckers_window_present_settings_dialog(GCheckersWindow *self);
void gcheckers_window_set_loaded_source_path(GCheckersWindow *self, const char *path);
void gcheckers_window_set_loaded_ruleset(GCheckersWindow *self, PlayerRuleset ruleset);
void gcheckers_window_force_move(GCheckersWindow *self);
char *gcheckers_window_format_analysis_score(gint score);
char *gcheckers_window_format_analysis_report(const SgfNodeAnalysis *analysis);
PlayerRuleset gcheckers_window_get_ruleset(GCheckersWindow *self);
gboolean gcheckers_window_start_puzzle_mode_for_path(GCheckersWindow *self,
                                                     PlayerRuleset ruleset,
                                                     const char *path);
void gcheckers_window_apply_new_game_settings(GCheckersWindow *self,
                                              PlayerRuleset ruleset,
                                              PlayerControlMode white_mode,
                                              PlayerControlMode black_mode,
                                              guint computer_depth);
void gcheckers_window_set_board_orientation_mode(GCheckersWindow *self,
                                                 GCheckersWindowBoardOrientationMode mode);
void gcheckers_window_set_board_bottom_color(GCheckersWindow *self, CheckersColor bottom_color);
CheckersColor gcheckers_window_get_board_bottom_color(GCheckersWindow *self);
void gcheckers_window_set_analysis_depth(GCheckersWindow *self, guint depth);
guint gcheckers_window_get_analysis_depth(GCheckersWindow *self);
PlayerControlsPanel *gcheckers_window_get_controls_panel(GCheckersWindow *self);
GCheckersSgfController *gcheckers_window_get_sgf_controller(GCheckersWindow *self);

G_END_DECLS

#endif
