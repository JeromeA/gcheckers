#ifndef GCHECKERS_WINDOW_H
#define GCHECKERS_WINDOW_H

#include "checkers_model.h"
#include "gcheckers_sgf_controller.h"
#include "player_controls_panel.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GCHECKERS_TYPE_WINDOW (gcheckers_window_get_type())

G_DECLARE_FINAL_TYPE(GCheckersWindow, gcheckers_window, GCHECKERS, WINDOW, GtkApplicationWindow)

GCheckersWindow *gcheckers_window_new(GtkApplication *app, GCheckersModel *model);
PlayerControlsPanel *gcheckers_window_get_controls_panel(GCheckersWindow *self);
GCheckersSgfController *gcheckers_window_get_sgf_controller(GCheckersWindow *self);

G_END_DECLS

#endif
