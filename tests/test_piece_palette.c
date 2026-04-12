#include <gtk/gtk.h>

#include "piece_palette.h"

static guint test_piece_palette_count_partial_alpha(cairo_surface_t *surface) {
  g_return_val_if_fail(surface != NULL, 0);

  cairo_surface_flush(surface);

  int width = cairo_image_surface_get_width(surface);
  int height = cairo_image_surface_get_height(surface);
  int stride = cairo_image_surface_get_stride(surface);
  unsigned char *data = cairo_image_surface_get_data(surface);
  guint count = 0;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      guint32 pixel = *(guint32 *)(data + y * stride + x * 4);
      guint8 alpha = (pixel >> 24) & 0xff;
      if (alpha > 0 && alpha < 255) {
        ++count;
      }
    }
  }

  return count;
}

static void test_piece_palette_draw_uses_partial_alpha_edges(void) {
  PiecePalette *palette = piece_palette_new_default();
  g_assert_nonnull(palette);

  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 31, 31);
  g_assert_nonnull(surface);

  cairo_t *cr = cairo_create(surface);
  g_assert_nonnull(cr);

  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
  cairo_paint(cr);

  g_assert_true(piece_palette_draw(palette, CHECKERS_PIECE_WHITE_MAN, cr, 31.0, 31.0));

  cairo_destroy(cr);

  g_assert_cmpuint(test_piece_palette_count_partial_alpha(surface), >, 0);

  cairo_surface_destroy(surface);
  g_clear_object(&palette);
}

static void test_piece_palette_draw_rejects_empty_piece(void) {
  PiecePalette *palette = piece_palette_new_default();
  g_assert_nonnull(palette);

  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 31, 31);
  g_assert_nonnull(surface);

  cairo_t *cr = cairo_create(surface);
  g_assert_nonnull(cr);

  g_assert_false(piece_palette_draw(palette, CHECKERS_PIECE_EMPTY, cr, 31.0, 31.0));

  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  g_clear_object(&palette);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/piece-palette/draw-uses-partial-alpha-edges",
                  test_piece_palette_draw_uses_partial_alpha_edges);
  g_test_add_func("/piece-palette/draw-rejects-empty-piece",
                  test_piece_palette_draw_rejects_empty_piece);
  return g_test_run();
}
