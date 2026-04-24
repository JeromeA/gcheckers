#ifndef BOARD_SELECTION_CONTROLLER_H
#define BOARD_SELECTION_CONTROLLER_H

#include "game_model.h"

G_BEGIN_DECLS

#define BOARD_TYPE_SELECTION_CONTROLLER (board_selection_controller_get_type())

G_DECLARE_FINAL_TYPE(BoardSelectionController,
                     board_selection_controller,
                     BOARD,
                     SELECTION_CONTROLLER,
                     GObject)

typedef gboolean (*BoardSelectionControllerMoveHandler)(gconstpointer move, gpointer user_data);

BoardSelectionController *board_selection_controller_new(void);
void board_selection_controller_set_model(BoardSelectionController *self, GGameModel *model);
void board_selection_controller_set_move_handler(BoardSelectionController *self,
                                                 BoardSelectionControllerMoveHandler handler,
                                                 gpointer user_data);
void board_selection_controller_clear(BoardSelectionController *self);
const guint *board_selection_controller_peek_path(BoardSelectionController *self, guint *length);

gboolean board_selection_controller_contains(BoardSelectionController *self, guint index);

gboolean board_selection_controller_handle_click(BoardSelectionController *self, guint index);

G_END_DECLS

#endif
