#ifndef SGF_VIEW_DISC_FACTORY_H
#define SGF_VIEW_DISC_FACTORY_H

#include "sgf_tree.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SGF_TYPE_VIEW_DISC_FACTORY (sgf_view_disc_factory_get_type())

G_DECLARE_FINAL_TYPE(SgfViewDiscFactory, sgf_view_disc_factory, SGF, VIEW_DISC_FACTORY, GObject)

SgfViewDiscFactory *sgf_view_disc_factory_new(void);
GtkWidget *sgf_view_disc_factory_build(SgfViewDiscFactory *self,
                                       const SgfNode *node,
                                       const SgfNode *selected,
                                       GHashTable *node_widgets,
                                       int disc_stride);

G_END_DECLS

#endif
