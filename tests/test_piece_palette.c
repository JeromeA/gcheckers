#include <gtk/gtk.h>

#include "games/checkers/board.h"
#include "piece_palette.h"

static GameBackendSquarePieceView test_piece_palette_piece_view(CheckersPiece piece) {
  switch (piece) {
    case CHECKERS_PIECE_WHITE_MAN:
      return (GameBackendSquarePieceView){
        .is_empty = FALSE,
        .side = 0,
        .kind = GAME_BACKEND_SQUARE_PIECE_KIND_MAN,
        .symbol = NULL,
      };
    case CHECKERS_PIECE_WHITE_KING:
      return (GameBackendSquarePieceView){
        .is_empty = FALSE,
        .side = 0,
        .kind = GAME_BACKEND_SQUARE_PIECE_KIND_KING,
        .symbol = NULL,
      };
    case CHECKERS_PIECE_BLACK_MAN:
      return (GameBackendSquarePieceView){
        .is_empty = FALSE,
        .side = 1,
        .kind = GAME_BACKEND_SQUARE_PIECE_KIND_MAN,
        .symbol = NULL,
      };
    case CHECKERS_PIECE_BLACK_KING:
      return (GameBackendSquarePieceView){
        .is_empty = FALSE,
        .side = 1,
        .kind = GAME_BACKEND_SQUARE_PIECE_KIND_KING,
        .symbol = NULL,
      };
    case CHECKERS_PIECE_EMPTY:
    default:
      return (GameBackendSquarePieceView){
        .is_empty = TRUE,
        .side = 0,
        .kind = GAME_BACKEND_SQUARE_PIECE_KIND_NONE,
        .symbol = NULL,
      };
  }
}

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

static guint8 test_piece_palette_alpha_at(cairo_surface_t *surface, int x, int y) {
  g_return_val_if_fail(surface != NULL, 0);

  cairo_surface_flush(surface);

  int width = cairo_image_surface_get_width(surface);
  int height = cairo_image_surface_get_height(surface);
  g_return_val_if_fail(x >= 0 && x < width, 0);
  g_return_val_if_fail(y >= 0 && y < height, 0);

  int stride = cairo_image_surface_get_stride(surface);
  unsigned char *data = cairo_image_surface_get_data(surface);
  guint32 pixel = *(guint32 *)(data + y * stride + x * 4);
  return (pixel >> 24) & 0xff;
}

static gboolean test_piece_palette_row_has_alpha(cairo_surface_t *surface, int y) {
  g_return_val_if_fail(surface != NULL, FALSE);

  cairo_surface_flush(surface);

  int width = cairo_image_surface_get_width(surface);
  int height = cairo_image_surface_get_height(surface);
  g_return_val_if_fail(y >= 0 && y < height, FALSE);

  int stride = cairo_image_surface_get_stride(surface);
  unsigned char *data = cairo_image_surface_get_data(surface);
  for (int x = 0; x < width; ++x) {
    guint32 pixel = *(guint32 *)(data + y * stride + x * 4);
    guint8 alpha = (pixel >> 24) & 0xff;
    if (alpha > 0) {
      return TRUE;
    }
  }

  return FALSE;
}

static void test_piece_palette_find_y_bounds(cairo_surface_t *surface, int *min_y, int *max_y) {
  g_return_if_fail(surface != NULL);
  g_return_if_fail(min_y != NULL);
  g_return_if_fail(max_y != NULL);

  int height = cairo_image_surface_get_height(surface);
  *min_y = -1;
  *max_y = -1;

  for (int y = 0; y < height; ++y) {
    if (!test_piece_palette_row_has_alpha(surface, y)) {
      continue;
    }
    if (*min_y < 0) {
      *min_y = y;
    }
    *max_y = y;
  }
}

static cairo_surface_t *test_piece_palette_draw_piece(CheckersPiece piece, int size) {
  PiecePalette *palette = piece_palette_new_default();
  GameBackendSquarePieceView piece_view = test_piece_palette_piece_view(piece);
  g_assert_nonnull(palette);

  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
  g_assert_nonnull(surface);

  cairo_t *cr = cairo_create(surface);
  g_assert_nonnull(cr);

  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
  cairo_paint(cr);

  g_assert_true(piece_palette_draw(palette, &piece_view, cr, (double)size, (double)size));

  cairo_destroy(cr);
  g_clear_object(&palette);
  return surface;
}

static void test_piece_palette_draw_uses_partial_alpha_edges(void) {
  cairo_surface_t *surface = test_piece_palette_draw_piece(CHECKERS_PIECE_WHITE_MAN, 31);

  g_assert_cmpuint(test_piece_palette_count_partial_alpha(surface), >, 0);

  cairo_surface_destroy(surface);
}

static void test_piece_palette_draw_uses_taller_top_ellipse(void) {
  cairo_surface_t *surface = test_piece_palette_draw_piece(CHECKERS_PIECE_WHITE_MAN, 64);

  g_assert_cmpuint(test_piece_palette_alpha_at(surface, 32, 22), >, 0);

  cairo_surface_destroy(surface);
}

static void test_piece_palette_draw_centers_man_vertically(void) {
  cairo_surface_t *surface = test_piece_palette_draw_piece(CHECKERS_PIECE_WHITE_MAN, 64);
  int min_y = -1;
  int max_y = -1;

  test_piece_palette_find_y_bounds(surface, &min_y, &max_y);

  g_assert_cmpint(min_y, >=, 0);
  g_assert_cmpint(max_y, >=, 0);
  g_assert_cmpint(abs((min_y + max_y) - 63), <=, 1);

  cairo_surface_destroy(surface);
}

static void test_piece_palette_draw_centers_king_vertically(void) {
  cairo_surface_t *surface = test_piece_palette_draw_piece(CHECKERS_PIECE_WHITE_KING, 64);
  int min_y = -1;
  int max_y = -1;

  test_piece_palette_find_y_bounds(surface, &min_y, &max_y);

  g_assert_cmpint(min_y, >=, 0);
  g_assert_cmpint(max_y, >=, 0);
  g_assert_cmpint(abs((min_y + max_y) - 63), <=, 1);

  cairo_surface_destroy(surface);
}

static void test_piece_palette_draw_rejects_empty_piece(void) {
  PiecePalette *palette = piece_palette_new_default();
  GameBackendSquarePieceView piece = test_piece_palette_piece_view(CHECKERS_PIECE_EMPTY);
  g_assert_nonnull(palette);

  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 31, 31);
  g_assert_nonnull(surface);

  cairo_t *cr = cairo_create(surface);
  g_assert_nonnull(cr);

  g_assert_false(piece_palette_draw(palette, &piece, cr, 31.0, 31.0));

  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  g_clear_object(&palette);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/piece-palette/draw-uses-partial-alpha-edges",
                  test_piece_palette_draw_uses_partial_alpha_edges);
  g_test_add_func("/piece-palette/draw-uses-taller-top-ellipse",
                  test_piece_palette_draw_uses_taller_top_ellipse);
  g_test_add_func("/piece-palette/draw-centers-man-vertically",
                  test_piece_palette_draw_centers_man_vertically);
  g_test_add_func("/piece-palette/draw-centers-king-vertically",
                  test_piece_palette_draw_centers_king_vertically);
  g_test_add_func("/piece-palette/draw-rejects-empty-piece",
                  test_piece_palette_draw_rejects_empty_piece);
  return g_test_run();
}
