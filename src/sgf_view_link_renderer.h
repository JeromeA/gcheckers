#ifndef SGF_VIEW_LINK_RENDERER_H
#define SGF_VIEW_LINK_RENDERER_H

#include "sgf_tree.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SGF_TYPE_VIEW_LINK_RENDERER (sgf_view_link_renderer_get_type())

G_DECLARE_FINAL_TYPE(SgfViewLinkRenderer, sgf_view_link_renderer, SGF, VIEW_LINK_RENDERER, GObject)

SgfViewLinkRenderer *sgf_view_link_renderer_new(void);
void sgf_view_link_renderer_draw(SgfViewLinkRenderer *self,
                                 GtkWidget *lines_area,
                                 GHashTable *node_widgets,
                                 SgfTree *tree,
                                 cairo_t *cr,
                                 int width,
                                 int height);

G_END_DECLS

#endif
