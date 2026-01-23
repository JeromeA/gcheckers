#ifndef BOARD_MOVE_OVERLAY_H
#define BOARD_MOVE_OVERLAY_H

#include <gtk/gtk.h>

#include "checkers_model.h"

G_BEGIN_DECLS

#define BOARD_TYPE_MOVE_OVERLAY (board_move_overlay_get_type())

G_DECLARE_FINAL_TYPE(BoardMoveOverlay, board_move_overlay, BOARD, MOVE_OVERLAY, GObject)

BoardMoveOverlay *board_move_overlay_new(void);
GtkWidget *board_move_overlay_get_widget(BoardMoveOverlay *self);
void board_move_overlay_set_model(BoardMoveOverlay *self, GCheckersModel *model);
void board_move_overlay_queue_draw(BoardMoveOverlay *self);

G_END_DECLS

#endif
