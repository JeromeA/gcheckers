#ifndef SGF_VIEW_SCROLLER_H
#define SGF_VIEW_SCROLLER_H

#include "sgf_tree.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SGF_TYPE_VIEW_SCROLLER (sgf_view_scroller_get_type())

G_DECLARE_FINAL_TYPE(SgfViewScroller, sgf_view_scroller, SGF, VIEW_SCROLLER, GObject)

SgfViewScroller *sgf_view_scroller_new(void);
void sgf_view_scroller_request_scroll(SgfViewScroller *self,
                                      GtkScrolledWindow *root,
                                      GHashTable *node_widgets,
                                      const SgfNode *selected);
void sgf_view_scroller_on_layout_changed(SgfViewScroller *self,
                                         GtkScrolledWindow *root,
                                         GHashTable *node_widgets);

G_END_DECLS

#endif
