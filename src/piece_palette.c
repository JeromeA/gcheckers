#include "piece_palette.h"

#include "man_paintable.h"

struct _PiecePalette {
  GObject parent_instance;
  GdkRGBA white_fill;
  GdkRGBA white_stroke;
  GdkRGBA black_fill;
  GdkRGBA black_stroke;
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

static gboolean piece_palette_parse_color(const char *color_name, GdkRGBA *color) {
  g_return_val_if_fail(color_name != NULL, FALSE);
  g_return_val_if_fail(color != NULL, FALSE);

  if (gdk_rgba_parse(color, color_name)) {
    return TRUE;
  }

  g_debug("Failed to parse piece color: %s\n", color_name);
  return FALSE;
}

static gboolean piece_palette_get_style(PiecePalette *self,
                                        CheckersPiece piece,
                                        const GdkRGBA **fill_color,
                                        const GdkRGBA **stroke_color,
                                        guint *layer_count) {
  g_return_val_if_fail(PIECE_IS_PALETTE(self), FALSE);
  g_return_val_if_fail(fill_color != NULL, FALSE);
  g_return_val_if_fail(stroke_color != NULL, FALSE);
  g_return_val_if_fail(layer_count != NULL, FALSE);

  switch (piece) {
    case CHECKERS_PIECE_WHITE_MAN:
      *fill_color = &self->white_fill;
      *stroke_color = &self->white_stroke;
      *layer_count = 1;
      return TRUE;
    case CHECKERS_PIECE_BLACK_MAN:
      *fill_color = &self->black_fill;
      *stroke_color = &self->black_stroke;
      *layer_count = 1;
      return TRUE;
    case CHECKERS_PIECE_WHITE_KING:
      *fill_color = &self->white_fill;
      *stroke_color = &self->white_stroke;
      *layer_count = 2;
      return TRUE;
    case CHECKERS_PIECE_BLACK_KING:
      *fill_color = &self->black_fill;
      *stroke_color = &self->black_stroke;
      *layer_count = 2;
      return TRUE;
    default:
      return FALSE;
  }
}

static void piece_palette_class_init(PiecePaletteClass * /*klass*/) {
}

static void piece_palette_init(PiecePalette *self) {
  if (!piece_palette_parse_color("#ffffff", &self->white_fill)) {
    self->white_fill.red = 1.0;
    self->white_fill.green = 1.0;
    self->white_fill.blue = 1.0;
    self->white_fill.alpha = 1.0;
  }
  if (!piece_palette_parse_color("#111111", &self->white_stroke)) {
    self->white_stroke.red = 0.0667;
    self->white_stroke.green = 0.0667;
    self->white_stroke.blue = 0.0667;
    self->white_stroke.alpha = 1.0;
  }
  if (!piece_palette_parse_color("#111111", &self->black_fill)) {
    self->black_fill = self->white_stroke;
  }
  if (!piece_palette_parse_color("#ffffff", &self->black_stroke)) {
    self->black_stroke = self->white_fill;
  }
}

PiecePalette *piece_palette_new_default(void) {
  return g_object_new(PIECE_TYPE_PALETTE, NULL);
}

gboolean piece_palette_lookup(PiecePalette *self,
                              CheckersPiece piece,
                              const char **symbol,
                              gboolean *is_empty) {
  g_return_val_if_fail(PIECE_IS_PALETTE(self), FALSE);
  g_return_val_if_fail(symbol != NULL, FALSE);
  g_return_val_if_fail(is_empty != NULL, FALSE);

  *symbol = "";
  *is_empty = FALSE;

  switch (piece) {
    case CHECKERS_PIECE_WHITE_MAN:
      *symbol = piece_palette_symbol(piece);
      return TRUE;
    case CHECKERS_PIECE_BLACK_MAN:
      *symbol = piece_palette_symbol(piece);
      return TRUE;
    case CHECKERS_PIECE_WHITE_KING:
      *symbol = piece_palette_symbol(piece);
      return TRUE;
    case CHECKERS_PIECE_BLACK_KING:
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

gboolean piece_palette_can_draw(PiecePalette *self, CheckersPiece piece) {
  const GdkRGBA *fill_color = NULL;
  const GdkRGBA *stroke_color = NULL;
  guint layer_count = 0;

  return piece_palette_get_style(self, piece, &fill_color, &stroke_color, &layer_count);
}

gboolean piece_palette_draw(PiecePalette *self,
                            CheckersPiece piece,
                            cairo_t *cr,
                            double width,
                            double height) {
  g_return_val_if_fail(PIECE_IS_PALETTE(self), FALSE);
  g_return_val_if_fail(cr != NULL, FALSE);
  g_return_val_if_fail(width > 0.0, FALSE);
  g_return_val_if_fail(height > 0.0, FALSE);

  const GdkRGBA *fill_color = NULL;
  const GdkRGBA *stroke_color = NULL;
  guint layer_count = 0;
  if (!piece_palette_get_style(self, piece, &fill_color, &stroke_color, &layer_count)) {
    return FALSE;
  }

  gcheckers_man_paintable_draw(cr, width, height, fill_color, stroke_color, layer_count);
  return TRUE;
}
