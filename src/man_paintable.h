#ifndef GGAME_MAN_PAINTABLE_H
#define GGAME_MAN_PAINTABLE_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

void ggame_man_paintable_draw(cairo_t *cr,
                                  double width,
                                  double height,
                                  const GdkRGBA *fill_color,
                                  const GdkRGBA *stroke_color,
                                  guint layer_count);

GdkPaintable *ggame_man_paintable_new(const GdkRGBA *fill_color,
                                          const GdkRGBA *stroke_color,
                                          guint layer_count);

G_END_DECLS

#endif
