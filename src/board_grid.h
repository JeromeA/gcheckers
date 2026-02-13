#ifndef BOARD_GRID_H
#define BOARD_GRID_H

#include <gtk/gtk.h>

#include "board_square.h"

G_BEGIN_DECLS

#define BOARD_TYPE_GRID (board_grid_get_type())

G_DECLARE_FINAL_TYPE(BoardGrid, board_grid, BOARD, GRID, GObject)

BoardGrid *board_grid_new(guint square_size);
GtkWidget *board_grid_get_widget(BoardGrid *self);
void board_grid_build(BoardGrid *self, guint board_size, GCallback clicked, gpointer user_data);
BoardSquare *board_grid_get_square(BoardGrid *self, uint8_t index);
void board_grid_clear(BoardGrid *self);


G_END_DECLS

#endif
