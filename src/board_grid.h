#ifndef BOARD_GRID_H
#define BOARD_GRID_H

#include <gtk/gtk.h>

#include "game_backend.h"
#include "board_square.h"

G_BEGIN_DECLS

#define BOARD_TYPE_GRID (board_grid_get_type())

G_DECLARE_FINAL_TYPE(BoardGrid, board_grid, BOARD, GRID, GObject)

typedef void (*BoardGridPrimaryClickHandler)(GtkButton *button, gpointer user_data);
typedef void (*BoardGridSecondaryPressHandler)(GtkGestureClick *gesture,
                                               gint n_press,
                                               gdouble x,
                                               gdouble y,
                                               gpointer user_data);

BoardGrid *board_grid_new(guint square_size);
GtkWidget *board_grid_get_widget(BoardGrid *self);
void board_grid_build(BoardGrid *self,
                      const GameBackend *backend,
                      gconstpointer position,
                      guint bottom_side,
                      BoardGridPrimaryClickHandler primary_clicked,
                      BoardGridSecondaryPressHandler secondary_pressed,
                      gpointer user_data);
BoardSquare *board_grid_get_square(BoardGrid *self, guint index);
void board_grid_clear(BoardGrid *self);

guint board_grid_get_board_size(BoardGrid *self);

G_END_DECLS

#endif
