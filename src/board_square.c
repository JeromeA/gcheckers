#include "board_square.h"

#include "widget_utils.h"

struct _BoardSquare {
  GObject parent_instance;
  GtkWidget *button;
  GtkWidget *piece_stack;
  GtkWidget *piece_area;
  GtkWidget *piece_label;
  GtkWidget *index_label;
  PiecePalette *piece_palette;
  GameBackendSquarePieceView piece;
  guint square_size;
};

G_DEFINE_TYPE(BoardSquare, board_square, G_TYPE_OBJECT)

static void board_square_draw_piece(GtkDrawingArea * /*area*/,
                                    cairo_t *cr,
                                    int width,
                                    int height,
                                    gpointer user_data) {
  BoardSquare *self = BOARD_SQUARE(user_data);
  g_return_if_fail(BOARD_IS_SQUARE(self));
  g_return_if_fail(cr != NULL);
  g_return_if_fail(width > 0);
  g_return_if_fail(height > 0);

  if (!self->piece_palette) {
    return;
  }

  if (!piece_palette_draw(self->piece_palette, &self->piece, cr, (double)width, (double)height)) {
    g_debug("Failed to draw board piece");
  }
}

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

  GtkWidget *piece_area = gtk_drawing_area_new();
  gtk_widget_set_size_request(piece_area, self->square_size, self->square_size);
  gtk_widget_set_hexpand(piece_area, TRUE);
  gtk_widget_set_vexpand(piece_area, TRUE);
  gtk_widget_add_css_class(piece_area, "piece-picture");
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(piece_area), board_square_draw_piece, self, NULL);
  gtk_stack_add_named(GTK_STACK(piece_stack), piece_area, "picture");

  GtkWidget *piece_label = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(piece_label), 0.5f);
  gtk_widget_add_css_class(piece_label, "piece-label");
  gtk_stack_add_named(GTK_STACK(piece_stack), piece_label, "label");

  GtkWidget *index_label = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(index_label), 0.5f);
  gtk_widget_set_halign(index_label, GTK_ALIGN_START);
  gtk_widget_set_valign(index_label, GTK_ALIGN_END);
  gtk_widget_set_margin_bottom(index_label, 0);
  gtk_widget_add_css_class(index_label, "square-index");

  gtk_overlay_set_child(GTK_OVERLAY(container), piece_stack);
  gtk_overlay_add_overlay(GTK_OVERLAY(container), index_label);

  self->button = gtk_button_new();
  g_object_ref_sink(self->button);
  gtk_widget_set_size_request(self->button, self->square_size, self->square_size);
  gtk_widget_set_hexpand(self->button, TRUE);
  gtk_widget_set_vexpand(self->button, TRUE);
  gtk_button_set_child(GTK_BUTTON(self->button), container);

  self->piece_stack = piece_stack;
  self->piece_area = piece_area;
  self->piece_label = piece_label;
  self->index_label = index_label;
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
  self->piece_area = NULL;
  self->piece_label = NULL;
  self->index_label = NULL;
  g_clear_object(&self->piece_palette);

  G_OBJECT_CLASS(board_square_parent_class)->dispose(object);
}

static void board_square_class_init(BoardSquareClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = board_square_dispose;
}

static void board_square_init(BoardSquare *self) {
  memset(&self->piece, 0, sizeof(self->piece));
  self->piece.is_empty = TRUE;
  self->piece.kind = GAME_BACKEND_SQUARE_PIECE_KIND_NONE;
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

void board_square_set_index(BoardSquare *self, guint index) {
  g_return_if_fail(BOARD_IS_SQUARE(self));
  g_return_if_fail(GTK_IS_LABEL(self->index_label));

  char label[8];
  g_snprintf(label, sizeof(label), "%u", index + 1);
  gtk_label_set_text(GTK_LABEL(self->index_label), label);
  g_object_set_data(G_OBJECT(self->button), "board-index", GINT_TO_POINTER(index + 1));
}

void board_square_set_piece(BoardSquare *self, const GameBackendSquarePieceView *piece, PiecePalette *palette) {
  g_return_if_fail(BOARD_IS_SQUARE(self));
  g_return_if_fail(piece != NULL);
  g_return_if_fail(PIECE_IS_PALETTE(palette));

  const char *symbol = "";
  gboolean is_empty = FALSE;
  if (!piece_palette_lookup(palette, piece, &symbol, &is_empty)) {
    g_debug("Failed to lookup palette data for board piece");
  }

  self->piece = *piece;
  g_set_object(&self->piece_palette, palette);

  if (is_empty) {
    gtk_widget_set_visible(self->piece_stack, FALSE);
    return;
  }

  if (piece_palette_can_draw(palette, piece)) {
    gtk_widget_queue_draw(self->piece_area);
    gtk_stack_set_visible_child(GTK_STACK(self->piece_stack), self->piece_area);
  } else {
    gtk_label_set_text(GTK_LABEL(self->piece_label), symbol);
    gtk_stack_set_visible_child(GTK_STACK(self->piece_stack), self->piece_label);
  }
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
