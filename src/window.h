#ifndef GCHECKERS_WINDOW_H
#define GCHECKERS_WINDOW_H

#include "checkers_model.h"
#include "sgf_controller.h"
#include "player_controls_panel.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GCHECKERS_TYPE_WINDOW (gcheckers_window_get_type())

G_DECLARE_FINAL_TYPE(GCheckersWindow, gcheckers_window, GCHECKERS, WINDOW, GtkApplicationWindow)

GCheckersWindow *gcheckers_window_new(GtkApplication *app, GCheckersModel *model);
void gcheckers_window_present_new_game_dialog(GCheckersWindow *self);
void gcheckers_window_present_import_dialog(GCheckersWindow *self);
void gcheckers_window_force_move(GCheckersWindow *self);
PlayerRuleset gcheckers_window_get_ruleset(GCheckersWindow *self);
void gcheckers_window_apply_new_game_settings(GCheckersWindow *self,
                                              PlayerRuleset ruleset,
                                              PlayerControlMode white_mode,
                                              PlayerControlMode black_mode,
                                              guint computer_depth);
PlayerControlsPanel *gcheckers_window_get_controls_panel(GCheckersWindow *self);
GCheckersSgfController *gcheckers_window_get_sgf_controller(GCheckersWindow *self);

G_END_DECLS

#endif
