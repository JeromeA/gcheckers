#ifndef SGF_VIEW_SELECTION_CONTROLLER_H
#define SGF_VIEW_SELECTION_CONTROLLER_H

#include "sgf_tree.h"
#include "sgf_view.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SGF_TYPE_VIEW_SELECTION_CONTROLLER (sgf_view_selection_controller_get_type())

G_DECLARE_FINAL_TYPE(SgfViewSelectionController,
                     sgf_view_selection_controller,
                     SGF,
                     VIEW_SELECTION_CONTROLLER,
                     GObject)

SgfViewSelectionController *sgf_view_selection_controller_new(void);
const SgfNode *sgf_view_selection_controller_get_selected(SgfViewSelectionController *self);
void sgf_view_selection_controller_set_selected_raw(SgfViewSelectionController *self, const SgfNode *node);
gboolean sgf_view_selection_controller_set_selected(SgfViewSelectionController *self,
                                                    const SgfNode *node,
                                                    GHashTable *node_widgets);
const SgfNode *sgf_view_selection_controller_next(SgfViewSelectionController *self,
                                                  SgfViewNavigation navigation);

G_END_DECLS

#endif
