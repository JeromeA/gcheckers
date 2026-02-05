#ifndef SGF_VIEW_SCROLLER_H
#define SGF_VIEW_SCROLLER_H

#include "sgf_tree.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SGF_TYPE_VIEW_SCROLLER (sgf_view_scroller_get_type())
#define SGF_VIEW_SCROLLER_VISIBILITY_PADDING 3

G_DECLARE_FINAL_TYPE(SgfViewScroller, sgf_view_scroller, SGF, VIEW_SCROLLER, GObject)

SgfViewScroller *sgf_view_scroller_new(void);
void sgf_view_scroller_request_scroll(SgfViewScroller *self,
                                      GtkScrolledWindow *root,
                                      GHashTable *node_widgets,
                                      GArray *column_widths,
                                      GArray *row_heights,
                                      const SgfNode *selected);
void sgf_view_scroller_on_layout_changed(SgfViewScroller *self,
                                         GtkScrolledWindow *root,
                                         GHashTable *node_widgets,
                                         GArray *column_widths,
                                         GArray *row_heights);

G_END_DECLS

#endif
