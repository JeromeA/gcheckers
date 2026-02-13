#include "board_grid.h"

#include "board.h"
#include "widget_utils.h"

#include <string.h>

struct _BoardGrid {
  GObject parent_instance;
  GtkWidget *grid;
  BoardSquare *squares[CHECKERS_MAX_SQUARES];
  guint board_size;
  guint square_size;
};

G_DEFINE_TYPE(BoardGrid, board_grid, G_TYPE_OBJECT)

static void board_grid_reset_squares(BoardGrid *self) {
  g_return_if_fail(BOARD_IS_GRID(self));

  for (int i = 0; i < CHECKERS_MAX_SQUARES; ++i) {
    if (self->squares[i]) {
      g_clear_object(&self->squares[i]);
    }
  }
}

static void board_grid_dispose(GObject *object) {
  BoardGrid *self = BOARD_GRID(object);

  board_grid_clear(self);
  board_grid_reset_squares(self);
  gboolean grid_removed = TRUE;
  if (self->grid) {
    grid_removed = gcheckers_widget_remove_from_parent(self->grid);
    if (!grid_removed && gtk_widget_get_parent(self->grid)) {
      g_debug("Failed to remove board grid widget from parent during dispose\n");
    }
  }
  if (grid_removed) {
    g_clear_object(&self->grid);
  } else {
    self->grid = NULL;
  }

  G_OBJECT_CLASS(board_grid_parent_class)->dispose(object);
}

static void board_grid_class_init(BoardGridClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = board_grid_dispose;
}

static void board_grid_init(BoardGrid *self) {
  self->grid = gtk_grid_new();
  g_object_ref_sink(self->grid);
  gtk_widget_add_css_class(self->grid, "board");
  gtk_widget_set_hexpand(self->grid, TRUE);
  gtk_widget_set_vexpand(self->grid, TRUE);
  self->board_size = 0;
  self->square_size = 0;
}

BoardGrid *board_grid_new(guint square_size) {
  g_return_val_if_fail(square_size > 0, NULL);

  BoardGrid *self = g_object_new(BOARD_TYPE_GRID, NULL);
  self->square_size = square_size;
  return self;
}

GtkWidget *board_grid_get_widget(BoardGrid *self) {
  g_return_val_if_fail(BOARD_IS_GRID(self), NULL);

  return self->grid;
}

void board_grid_clear(BoardGrid *self) {
  g_return_if_fail(BOARD_IS_GRID(self));
  g_return_if_fail(GTK_IS_GRID(self->grid));

  GtkWidget *child = gtk_widget_get_first_child(self->grid);
  while (child) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_grid_remove(GTK_GRID(self->grid), child);
    child = next;
  }
}

void board_grid_build(BoardGrid *self, guint board_size, GCallback clicked, gpointer user_data) {
  g_return_if_fail(BOARD_IS_GRID(self));
  g_return_if_fail(board_size > 0);

  if (board_size == 0) {
    g_debug("Board size was zero while building board grid\n");
    return;
  }

  board_grid_clear(self);
  board_grid_reset_squares(self);
  self->board_size = board_size;
  memset(self->squares, 0, sizeof(self->squares));

  gtk_grid_set_row_homogeneous(GTK_GRID(self->grid), TRUE);
  gtk_grid_set_column_homogeneous(GTK_GRID(self->grid), TRUE);
  gtk_grid_set_row_spacing(GTK_GRID(self->grid), 1);
  gtk_grid_set_column_spacing(GTK_GRID(self->grid), 1);

  for (int row = 0; row < (int)board_size; ++row) {
    for (int col = 0; col < (int)board_size; ++col) {
      GtkWidget *square = NULL;
      if ((row + col) % 2 == 0) {
        GtkWidget *label = gtk_label_new(" ");
        gtk_widget_set_size_request(label, self->square_size, self->square_size);
        gtk_widget_set_hexpand(label, TRUE);
        gtk_widget_set_vexpand(label, TRUE);
        gtk_widget_add_css_class(label, "board-light");
        square = label;
      } else {
        int8_t index = board_index_from_coord(row, col, board_size);
        BoardSquare *board_square = board_square_new(self->square_size);
        GtkWidget *button = board_square_get_widget(board_square);
        gtk_widget_add_css_class(button, "board-dark");
        gtk_widget_add_css_class(button, "board-square");
        if (index < 0) {
          g_debug("Invalid board index while building board grid\n");
        } else {
          board_square_set_index(board_square, (guint)index);
        }
        if (clicked != NULL) {
          g_signal_connect(button, "clicked", clicked, user_data);
        }
        square = button;
        if (index >= 0 && index < CHECKERS_MAX_SQUARES) {
          self->squares[index] = board_square;
        }
      }
      gtk_grid_attach(GTK_GRID(self->grid), square, col, row, 1, 1);
    }
  }
}

BoardSquare *board_grid_get_square(BoardGrid *self, uint8_t index) {
  g_return_val_if_fail(BOARD_IS_GRID(self), NULL);

  if (index >= CHECKERS_MAX_SQUARES) {
    return NULL;
  }

  return self->squares[index];
}
