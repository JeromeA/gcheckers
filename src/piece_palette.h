#ifndef PIECE_PALETTE_H
#define PIECE_PALETTE_H

#include <gtk/gtk.h>

#include "board.h"

G_BEGIN_DECLS

#define PIECE_TYPE_PALETTE (piece_palette_get_type())

G_DECLARE_FINAL_TYPE(PiecePalette, piece_palette, PIECE, PALETTE, GObject)

PiecePalette *piece_palette_new_default(void);

gboolean piece_palette_lookup(PiecePalette *self,
                              CheckersPiece piece,
                              const char **symbol,
                              gboolean *is_empty);

gboolean piece_palette_can_draw(PiecePalette *self, CheckersPiece piece);

gboolean piece_palette_draw(PiecePalette *self,
                            CheckersPiece piece,
                            cairo_t *cr,
                            double width,
                            double height);

G_END_DECLS

#endif
