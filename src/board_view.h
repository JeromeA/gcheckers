#ifndef BOARD_VIEW_H
#define BOARD_VIEW_H

#include <gtk/gtk.h>

#include "checkers_model.h"

G_BEGIN_DECLS

typedef struct _GCheckersSgfController GCheckersSgfController;

#define BOARD_TYPE_VIEW (board_view_get_type())

G_DECLARE_FINAL_TYPE(BoardView, board_view, BOARD, VIEW, GObject)

typedef gboolean (*BoardViewMoveHandler)(const CheckersMove *move, gpointer user_data);
typedef gboolean (*BoardViewSquareHandler)(guint8 index, guint button, gpointer user_data);

BoardView *board_view_new(void);
GtkWidget *board_view_get_widget(BoardView *self);
void board_view_set_model(BoardView *self, GCheckersModel *model);
void board_view_set_sgf_controller(BoardView *self, GCheckersSgfController *controller);
void board_view_set_move_handler(BoardView *self, BoardViewMoveHandler handler, gpointer user_data);
void board_view_set_square_handler(BoardView *self, BoardViewSquareHandler handler, gpointer user_data);
void board_view_update(BoardView *self);
void board_view_clear_selection(BoardView *self);
void board_view_set_input_enabled(BoardView *self, gboolean enabled);

G_END_DECLS

#endif
