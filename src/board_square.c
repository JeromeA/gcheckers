#include "board_square.h"

#include "widget_utils.h"

struct _BoardSquare {
  GObject parent_instance;
  GtkWidget *button;
  GtkWidget *piece_stack;
  GtkWidget *piece_label;
  guint square_size;
};

G_DEFINE_TYPE(BoardSquare, board_square, G_TYPE_OBJECT)

static void board_square_build(BoardSquare *self) {
  g_return_if_fail(BOARD_IS_SQUARE(self));

  GtkWidget *container = gtk_overlay_new();
  gtk_widget_set_hexpand(container, TRUE);
  gtk_widget_set_vexpand(container, TRUE);

  GtkWidget *piece_stack = gtk_stack_new();
  gtk_stack_set_hhomogeneous(GTK_STACK(piece_stack), TRUE);
  gtk_stack_set_vhomogeneous(GTK_STACK(piece_stack), TRUE);
  gtk_widget_set_hexpand(piece_stack, TRUE);
  gtk_widget_set_vexpand(piece_stack, TRUE);

  GtkWidget *piece_label = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(piece_label), 0.5f);
  gtk_widget_add_css_class(piece_label, "piece-label");
  gtk_stack_add_named(GTK_STACK(piece_stack), piece_label, "label");

  gtk_overlay_set_child(GTK_OVERLAY(container), piece_stack);

  self->button = gtk_button_new();
  g_object_ref_sink(self->button);
  gtk_widget_set_size_request(self->button, self->square_size, self->square_size);
  gtk_widget_set_hexpand(self->button, TRUE);
  gtk_widget_set_vexpand(self->button, TRUE);
  gtk_button_set_child(GTK_BUTTON(self->button), container);

  self->piece_stack = piece_stack;
  self->piece_label = piece_label;
}

static void board_square_dispose(GObject *object) {
  BoardSquare *self = BOARD_SQUARE(object);

  gboolean button_removed = TRUE;
  if (self->button) {
    button_removed = gcheckers_widget_remove_from_parent(self->button);
    if (!button_removed && gtk_widget_get_parent(self->button)) {
      g_debug("Failed to remove board square button from parent during dispose\n");
    }
  }

  if (button_removed) {
    g_clear_object(&self->button);
  } else {
    self->button = NULL;
  }
  self->piece_stack = NULL;
  self->piece_label = NULL;

  G_OBJECT_CLASS(board_square_parent_class)->dispose(object);
}

static void board_square_class_init(BoardSquareClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = board_square_dispose;
}

static void board_square_init(BoardSquare *self) {
  self->square_size = 0;
}

BoardSquare *board_square_new(guint square_size) {
  g_return_val_if_fail(square_size > 0, NULL);

  BoardSquare *self = g_object_new(BOARD_TYPE_SQUARE, NULL);
  self->square_size = square_size;
  board_square_build(self);
  return self;
}

GtkWidget *board_square_get_widget(BoardSquare *self) {
  g_return_val_if_fail(BOARD_IS_SQUARE(self), NULL);

  return self->button;
}


static const char *board_square_piece_symbol(CheckersPiece piece) {
  switch (piece) {
    case CHECKERS_PIECE_WHITE_MAN:
      return "⛀";
    case CHECKERS_PIECE_WHITE_KING:
      return "⛁";
    case CHECKERS_PIECE_BLACK_MAN:
      return "⛂";
    case CHECKERS_PIECE_BLACK_KING:
      return "⛃";
    case CHECKERS_PIECE_EMPTY:
      return "·";
    default:
      g_debug("board_square_piece_symbol received unknown piece %d\n", piece);
      return "?";
  }
}

void board_square_set_piece(BoardSquare *self, CheckersPiece piece) {
  g_return_if_fail(BOARD_IS_SQUARE(self));

  if (piece == CHECKERS_PIECE_EMPTY) {
    gtk_widget_set_visible(self->piece_stack, FALSE);
    return;
  }

  gtk_label_set_text(GTK_LABEL(self->piece_label), board_square_piece_symbol(piece));
  gtk_stack_set_visible_child(GTK_STACK(self->piece_stack), self->piece_label);
  gtk_widget_set_visible(self->piece_stack, TRUE);
}

void board_square_set_highlight(BoardSquare *self,
                                gboolean is_selected,
                                gboolean is_selectable,
                                gboolean is_destination) {
  g_return_if_fail(BOARD_IS_SQUARE(self));

  if (is_selected) {
    gtk_widget_add_css_class(self->button, "board-halo-selected");
    gtk_widget_remove_css_class(self->button, "board-halo");
    return;
  }

  if (is_selectable || is_destination) {
    gtk_widget_add_css_class(self->button, "board-halo");
    gtk_widget_remove_css_class(self->button, "board-halo-selected");
    return;
  }

  gtk_widget_remove_css_class(self->button, "board-halo");
  gtk_widget_remove_css_class(self->button, "board-halo-selected");
}
