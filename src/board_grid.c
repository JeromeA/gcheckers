#include "board_grid.h"

#include "widget_utils.h"

#include <string.h>

struct _BoardGrid {
  GObject parent_instance;
  GtkWidget *grid;
  BoardSquare *squares[128];
  guint board_size;
  guint square_size;
};

G_DEFINE_TYPE(BoardGrid, board_grid, G_TYPE_OBJECT)

static void board_grid_transform_for_bottom_side(guint *row, guint *col, guint rows, guint cols, guint bottom_side) {
  g_return_if_fail(row != NULL);
  g_return_if_fail(col != NULL);

  if (bottom_side == 0) {
    return;
  }

  *row = rows - 1 - *row;
  *col = cols - 1 - *col;
}

static void board_grid_reset_squares(BoardGrid *self) {
  g_return_if_fail(BOARD_IS_GRID(self));

  for (guint i = 0; i < G_N_ELEMENTS(self->squares); ++i) {
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
    grid_removed = ggame_widget_remove_from_parent(self->grid);
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

void board_grid_build(BoardGrid *self,
                      const GameBackend *backend,
                      gconstpointer position,
                      guint bottom_side,
                      BoardGridPrimaryClickHandler primary_clicked,
                      BoardGridSecondaryPressHandler secondary_pressed,
                      gpointer user_data) {
  g_return_if_fail(BOARD_IS_GRID(self));
  g_return_if_fail(backend != NULL);
  g_return_if_fail(position != NULL);
  g_return_if_fail(backend->supports_square_grid_board);
  g_return_if_fail(backend->square_grid_rows != NULL);
  g_return_if_fail(backend->square_grid_cols != NULL);
  g_return_if_fail(backend->square_grid_square_playable != NULL);
  g_return_if_fail(backend->square_grid_square_index != NULL);

  guint rows = backend->square_grid_rows(position);
  guint cols = backend->square_grid_cols(position);
  g_return_if_fail(rows > 0);
  g_return_if_fail(cols > 0);

  board_grid_clear(self);
  board_grid_reset_squares(self);
  self->board_size = rows;
  memset(self->squares, 0, sizeof(self->squares));

  gtk_grid_set_row_homogeneous(GTK_GRID(self->grid), TRUE);
  gtk_grid_set_column_homogeneous(GTK_GRID(self->grid), TRUE);
  gtk_grid_set_row_spacing(GTK_GRID(self->grid), 1);
  gtk_grid_set_column_spacing(GTK_GRID(self->grid), 1);

  for (guint row = 0; row < rows; ++row) {
    for (guint col = 0; col < cols; ++col) {
      GtkWidget *square = NULL;
      if (!backend->square_grid_square_playable(position, row, col)) {
        GtkWidget *label = gtk_label_new(" ");
        gtk_widget_set_size_request(label, self->square_size, self->square_size);
        gtk_widget_set_hexpand(label, TRUE);
        gtk_widget_set_vexpand(label, TRUE);
        gtk_widget_add_css_class(label, "board-light");
        square = label;
      } else {
        guint index = 0;
        BoardSquare *board_square = board_square_new(self->square_size);
        GtkWidget *button = board_square_get_widget(board_square);
        gtk_widget_add_css_class(button, "board-dark");
        gtk_widget_add_css_class(button, "board-square");
        if (!backend->square_grid_square_index(position, row, col, &index)) {
          g_debug("Invalid board index while building board grid\n");
        } else {
          board_square_set_index(board_square, index);
        }
        if (primary_clicked != NULL) {
          g_signal_connect(button, "clicked", G_CALLBACK(primary_clicked), user_data);
        }
        if (secondary_pressed != NULL) {
          GtkGesture *gesture = gtk_gesture_click_new();
          gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
          gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(gesture));
          g_signal_connect(gesture, "pressed", G_CALLBACK(secondary_pressed), user_data);
        }
        square = button;
        if (index < G_N_ELEMENTS(self->squares)) {
          self->squares[index] = board_square;
        }
      }
      guint display_row = row;
      guint display_col = col;
      board_grid_transform_for_bottom_side(&display_row, &display_col, rows, cols, bottom_side);
      gtk_grid_attach(GTK_GRID(self->grid), square, (int) display_col, (int) display_row, 1, 1);
    }
  }
}

BoardSquare *board_grid_get_square(BoardGrid *self, guint index) {
  g_return_val_if_fail(BOARD_IS_GRID(self), NULL);

  if (index >= G_N_ELEMENTS(self->squares)) {
    return NULL;
  }

  return self->squares[index];
}

guint board_grid_get_board_size(BoardGrid *self) {
  g_return_val_if_fail(BOARD_IS_GRID(self), 0);

  return self->board_size;
}
