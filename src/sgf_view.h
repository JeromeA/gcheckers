#ifndef SGF_VIEW_H
#define SGF_VIEW_H

#include "sgf_tree.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SGF_TYPE_VIEW (sgf_view_get_type())

G_DECLARE_FINAL_TYPE(SgfView, sgf_view, SGF, VIEW, GObject)

SgfView *sgf_view_new(void);
GtkWidget *sgf_view_get_widget(SgfView *self);
void sgf_view_set_tree(SgfView *self, SgfTree *tree);
void sgf_view_refresh(SgfView *self);

G_END_DECLS

#endif
