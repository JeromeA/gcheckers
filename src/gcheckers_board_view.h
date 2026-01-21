#ifndef GCHECKERS_BOARD_VIEW_H
#define GCHECKERS_BOARD_VIEW_H

#include <gtk/gtk.h>

#include "checkers_model.h"

G_BEGIN_DECLS

#define GCHECKERS_TYPE_BOARD_VIEW (gcheckers_board_view_get_type())

G_DECLARE_FINAL_TYPE(GCheckersBoardView, gcheckers_board_view, GCHECKERS, BOARD_VIEW, GObject)

GCheckersBoardView *gcheckers_board_view_new(void);
GtkWidget *gcheckers_board_view_get_widget(GCheckersBoardView *self);
void gcheckers_board_view_set_model(GCheckersBoardView *self, GCheckersModel *model);
void gcheckers_board_view_update(GCheckersBoardView *self);
void gcheckers_board_view_clear_selection(GCheckersBoardView *self);

G_END_DECLS

#endif
