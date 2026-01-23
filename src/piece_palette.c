#include "piece_palette.h"

#include "gcheckers_man_paintable.h"

struct _PiecePalette {
  GObject parent_instance;
  GdkPaintable *white_man_paintable;
  GdkPaintable *black_man_paintable;
  GdkPaintable *white_king_paintable;
  GdkPaintable *black_king_paintable;
};

G_DEFINE_TYPE(PiecePalette, piece_palette, G_TYPE_OBJECT)

static const char *piece_palette_symbol(CheckersPiece piece) {
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
      g_debug("piece_palette_symbol received unknown piece %d\n", piece);
      return "?";
  }
}

static GdkPaintable *piece_palette_build_paintable(const char *fill_color,
                                                   const char *stroke_color,
                                                   guint layer_count) {
  g_return_val_if_fail(fill_color != NULL, NULL);
  g_return_val_if_fail(stroke_color != NULL, NULL);

  GdkRGBA fill;
  GdkRGBA stroke;

  if (!gdk_rgba_parse(&fill, fill_color)) {
    g_debug("Failed to parse fill color for piece: %s\n", fill_color);
    return NULL;
  }
  if (!gdk_rgba_parse(&stroke, stroke_color)) {
    g_debug("Failed to parse stroke color for piece: %s\n", stroke_color);
    return NULL;
  }

  return gcheckers_man_paintable_new(&fill, &stroke, layer_count);
}

static void piece_palette_dispose(GObject *object) {
  PiecePalette *self = PIECE_PALETTE(object);

  g_clear_object(&self->white_man_paintable);
  g_clear_object(&self->black_man_paintable);
  g_clear_object(&self->white_king_paintable);
  g_clear_object(&self->black_king_paintable);

  G_OBJECT_CLASS(piece_palette_parent_class)->dispose(object);
}

static void piece_palette_class_init(PiecePaletteClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = piece_palette_dispose;
}

static void piece_palette_init(PiecePalette *self) {
  self->white_man_paintable = piece_palette_build_paintable("#ffffff", "#111111", 1);
  self->black_man_paintable = piece_palette_build_paintable("#111111", "#ffffff", 1);
  self->white_king_paintable = piece_palette_build_paintable("#ffffff", "#111111", 2);
  self->black_king_paintable = piece_palette_build_paintable("#111111", "#ffffff", 2);
}

PiecePalette *piece_palette_new_default(void) {
  return g_object_new(PIECE_TYPE_PALETTE, NULL);
}

gboolean piece_palette_lookup(PiecePalette *self,
                              CheckersPiece piece,
                              GdkPaintable **paintable,
                              const char **symbol,
                              gboolean *is_empty) {
  g_return_val_if_fail(PIECE_IS_PALETTE(self), FALSE);
  g_return_val_if_fail(paintable != NULL, FALSE);
  g_return_val_if_fail(symbol != NULL, FALSE);
  g_return_val_if_fail(is_empty != NULL, FALSE);

  *paintable = NULL;
  *symbol = "";
  *is_empty = FALSE;

  switch (piece) {
    case CHECKERS_PIECE_WHITE_MAN:
      *paintable = self->white_man_paintable;
      *symbol = piece_palette_symbol(piece);
      return TRUE;
    case CHECKERS_PIECE_BLACK_MAN:
      *paintable = self->black_man_paintable;
      *symbol = piece_palette_symbol(piece);
      return TRUE;
    case CHECKERS_PIECE_WHITE_KING:
      *paintable = self->white_king_paintable;
      *symbol = piece_palette_symbol(piece);
      return TRUE;
    case CHECKERS_PIECE_BLACK_KING:
      *paintable = self->black_king_paintable;
      *symbol = piece_palette_symbol(piece);
      return TRUE;
    case CHECKERS_PIECE_EMPTY:
      *symbol = piece_palette_symbol(piece);
      *is_empty = TRUE;
      return TRUE;
    default:
      g_debug("piece_palette_lookup received unknown piece %d\n", piece);
      *symbol = "?";
      return TRUE;
  }
}
