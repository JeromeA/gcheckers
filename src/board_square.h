#ifndef BOARD_SQUARE_H
#define BOARD_SQUARE_H

#include <gtk/gtk.h>

#include "board.h"

G_BEGIN_DECLS

#define BOARD_TYPE_SQUARE (board_square_get_type())

G_DECLARE_FINAL_TYPE(BoardSquare, board_square, BOARD, SQUARE, GObject)

BoardSquare *board_square_new(guint square_size);
GtkWidget *board_square_get_widget(BoardSquare *self);
void board_square_set_index(BoardSquare *self, guint index);
void board_square_set_piece(BoardSquare *self, CheckersPiece piece);
void board_square_set_highlight(BoardSquare *self,
                                gboolean is_selected,
                                gboolean is_selectable,
                                gboolean is_destination);

G_END_DECLS

#endif
