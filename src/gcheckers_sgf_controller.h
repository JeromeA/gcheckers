#ifndef GCHECKERS_SGF_CONTROLLER_H
#define GCHECKERS_SGF_CONTROLLER_H

#include "board_view.h"
#include "checkers_model.h"
#include "sgf_tree.h"
#include "sgf_view.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GCHECKERS_TYPE_SGF_CONTROLLER (gcheckers_sgf_controller_get_type())

G_DECLARE_FINAL_TYPE(GCheckersSgfController,
                     gcheckers_sgf_controller,
                     GCHECKERS,
                     SGF_CONTROLLER,
                     GObject)

GCheckersSgfController *gcheckers_sgf_controller_new(BoardView *board_view);
void gcheckers_sgf_controller_set_model(GCheckersSgfController *self, GCheckersModel *model);
void gcheckers_sgf_controller_reset(GCheckersSgfController *self);
GtkWidget *gcheckers_sgf_controller_get_widget(GCheckersSgfController *self);
SgfTree *gcheckers_sgf_controller_get_tree(GCheckersSgfController *self);
SgfView *gcheckers_sgf_controller_get_view(GCheckersSgfController *self);
gboolean gcheckers_sgf_controller_is_replaying(GCheckersSgfController *self);

G_END_DECLS

#endif
