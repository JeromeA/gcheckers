#include "piece_palette.h"

#include "man_paintable.h"

struct _PiecePalette {
  GObject parent_instance;
  GdkRGBA side0_fill;
  GdkRGBA side0_stroke;
  GdkRGBA side1_fill;
  GdkRGBA side1_stroke;
};

G_DEFINE_TYPE(PiecePalette, piece_palette, G_TYPE_OBJECT)

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
                                        const GameBackendSquarePieceView *piece,
                                        const GdkRGBA **fill_color,
                                        const GdkRGBA **stroke_color,
                                        guint *layer_count) {
  g_return_val_if_fail(PIECE_IS_PALETTE(self), FALSE);
  g_return_val_if_fail(piece != NULL, FALSE);
  g_return_val_if_fail(fill_color != NULL, FALSE);
  g_return_val_if_fail(stroke_color != NULL, FALSE);
  g_return_val_if_fail(layer_count != NULL, FALSE);

  if (piece->is_empty) {
    return FALSE;
  }

  switch (piece->side) {
    case 0:
      *fill_color = &self->side0_fill;
      *stroke_color = &self->side0_stroke;
      break;
    case 1:
      *fill_color = &self->side1_fill;
      *stroke_color = &self->side1_stroke;
      break;
    default:
      g_debug("Unsupported side index %u for palette lookup", piece->side);
      return FALSE;
  }

  switch (piece->kind) {
    case GAME_BACKEND_SQUARE_PIECE_KIND_MAN:
      *layer_count = 1;
      return TRUE;
    case GAME_BACKEND_SQUARE_PIECE_KIND_KING:
      *layer_count = 2;
      return TRUE;
    case GAME_BACKEND_SQUARE_PIECE_KIND_NONE:
    case GAME_BACKEND_SQUARE_PIECE_KIND_SYMBOL_ONLY:
    default:
      return FALSE;
  }
}

static void piece_palette_class_init(PiecePaletteClass * /*klass*/) {
}

static void piece_palette_init(PiecePalette *self) {
  if (!piece_palette_parse_color("#ffffff", &self->side0_fill)) {
    self->side0_fill.red = 1.0;
    self->side0_fill.green = 1.0;
    self->side0_fill.blue = 1.0;
    self->side0_fill.alpha = 1.0;
  }
  if (!piece_palette_parse_color("#111111", &self->side0_stroke)) {
    self->side0_stroke.red = 0.0667;
    self->side0_stroke.green = 0.0667;
    self->side0_stroke.blue = 0.0667;
    self->side0_stroke.alpha = 1.0;
  }
  if (!piece_palette_parse_color("#111111", &self->side1_fill)) {
    self->side1_fill = self->side0_stroke;
  }
  if (!piece_palette_parse_color("#ffffff", &self->side1_stroke)) {
    self->side1_stroke = self->side0_fill;
  }
}

PiecePalette *piece_palette_new_default(void) {
  return g_object_new(PIECE_TYPE_PALETTE, NULL);
}

gboolean piece_palette_lookup(PiecePalette *self,
                              const GameBackendSquarePieceView *piece,
                              const char **symbol,
                              gboolean *is_empty) {
  g_return_val_if_fail(PIECE_IS_PALETTE(self), FALSE);
  g_return_val_if_fail(piece != NULL, FALSE);
  g_return_val_if_fail(symbol != NULL, FALSE);
  g_return_val_if_fail(is_empty != NULL, FALSE);

  *symbol = piece->symbol != NULL ? piece->symbol : "";
  *is_empty = piece->is_empty;
  return TRUE;
}

gboolean piece_palette_can_draw(PiecePalette *self, const GameBackendSquarePieceView *piece) {
  const GdkRGBA *fill_color = NULL;
  const GdkRGBA *stroke_color = NULL;
  guint layer_count = 0;

  return piece_palette_get_style(self, piece, &fill_color, &stroke_color, &layer_count);
}

gboolean piece_palette_draw(PiecePalette *self,
                            const GameBackendSquarePieceView *piece,
                            cairo_t *cr,
                            double width,
                            double height) {
  g_return_val_if_fail(PIECE_IS_PALETTE(self), FALSE);
  g_return_val_if_fail(piece != NULL, FALSE);
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
