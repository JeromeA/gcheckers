#ifndef SGF_VIEW_LAYOUT_H
#define SGF_VIEW_LAYOUT_H

#include "sgf_tree.h"
#include "sgf_view_disc_factory.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SGF_TYPE_VIEW_LAYOUT (sgf_view_layout_get_type())

G_DECLARE_FINAL_TYPE(SgfViewLayout, sgf_view_layout, SGF, VIEW_LAYOUT, GObject)

SgfViewLayout *sgf_view_layout_new(void);
void sgf_view_layout_build(SgfViewLayout *self,
                           GtkGrid *grid,
                           SgfTree *tree,
                           GHashTable *node_widgets,
                           SgfViewDiscFactory *disc_factory,
                           const SgfNode *selected,
                           int disc_stride);

G_END_DECLS

#endif
