#ifndef SGF_VIEW_H
#define SGF_VIEW_H

#include "sgf_tree.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SGF_TYPE_VIEW (sgf_view_get_type())

G_DECLARE_FINAL_TYPE(SgfView, sgf_view, SGF, VIEW, GObject)

typedef enum {
  SGF_VIEW_NAVIGATE_PARENT,
  SGF_VIEW_NAVIGATE_CHILD,
  SGF_VIEW_NAVIGATE_PREVIOUS_SIBLING,
  SGF_VIEW_NAVIGATE_NEXT_SIBLING
} SgfViewNavigation;

SgfView *sgf_view_new(void);
GtkWidget *sgf_view_get_widget(SgfView *self);
void sgf_view_set_tree(SgfView *self, SgfTree *tree);
void sgf_view_set_selected(SgfView *self, const SgfNode *node);
const SgfNode *sgf_view_get_selected(SgfView *self);
gboolean sgf_view_navigate(SgfView *self, SgfViewNavigation navigation);
void sgf_view_refresh(SgfView *self);
void sgf_view_force_layout_sync(SgfView *self);
gboolean sgf_view_has_horizontal_position_inconsistency(double scroll_window_position,
                                                        double content_view_effective_position);

G_END_DECLS

#endif
