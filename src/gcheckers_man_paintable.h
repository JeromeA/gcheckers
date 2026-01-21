#ifndef GCHECKERS_MAN_PAINTABLE_H
#define GCHECKERS_MAN_PAINTABLE_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

GdkPaintable *gcheckers_man_paintable_new(const GdkRGBA *fill_color,
                                          const GdkRGBA *stroke_color,
                                          guint layer_count);

G_END_DECLS

#endif
