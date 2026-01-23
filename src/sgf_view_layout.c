#include "sgf_view_layout.h"

struct _SgfViewLayout {
  GObject parent_instance;
};

G_DEFINE_TYPE(SgfViewLayout, sgf_view_layout, G_TYPE_OBJECT)

static void sgf_view_layout_clear_container(GtkWidget *container) {
  GtkWidget *child = gtk_widget_get_first_child(container);
  while (child) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_widget_unparent(child);
    child = next;
  }
}

static GtkWidget *sgf_view_layout_build_disc(SgfViewLayout *self,
                                             SgfViewDiscFactory *disc_factory,
                                             const SgfNode *node,
                                             const SgfNode *selected,
                                             GHashTable *node_widgets,
                                             int disc_stride) {
  g_return_val_if_fail(SGF_IS_VIEW_LAYOUT(self), NULL);
  g_return_val_if_fail(SGF_IS_VIEW_DISC_FACTORY(disc_factory), NULL);
  g_return_val_if_fail(node != NULL, NULL);

  return sgf_view_disc_factory_build(disc_factory, node, selected, node_widgets, disc_stride);
}

static void sgf_view_layout_attach_disc(SgfViewLayout *self,
                                        SgfViewDiscFactory *disc_factory,
                                        GtkGrid *grid,
                                        const SgfNode *node,
                                        const SgfNode *selected,
                                        GHashTable *node_widgets,
                                        guint row,
                                        guint column,
                                        int disc_stride) {
  g_return_if_fail(SGF_IS_VIEW_LAYOUT(self));
  g_return_if_fail(SGF_IS_VIEW_DISC_FACTORY(disc_factory));
  g_return_if_fail(GTK_IS_GRID(grid));
  g_return_if_fail(node != NULL);

  GtkWidget *disc = sgf_view_layout_build_disc(self, disc_factory, node, selected, node_widgets, disc_stride);
  gtk_grid_attach(grid, disc, (int)column, (int)row, 1, 1);
}

static guint sgf_view_layout_append_branch(SgfViewLayout *self,
                                           SgfViewDiscFactory *disc_factory,
                                           GtkGrid *grid,
                                           const SgfNode *parent,
                                           const SgfNode *selected,
                                           GHashTable *node_widgets,
                                           guint row,
                                           guint depth,
                                           int disc_stride) {
  const GPtrArray *children = sgf_node_get_children(parent);
  if (!children || children->len == 0) {
    return row;
  }

  guint current_row = row;
  for (guint i = 0; i < children->len; ++i) {
    const SgfNode *child = g_ptr_array_index(children, i);
    if (i == 0) {
      sgf_view_layout_attach_disc(self,
                                  disc_factory,
                                  grid,
                                  child,
                                  selected,
                                  node_widgets,
                                  row,
                                  depth,
                                  disc_stride);
      current_row = sgf_view_layout_append_branch(self,
                                                  disc_factory,
                                                  grid,
                                                  child,
                                                  selected,
                                                  node_widgets,
                                                  row,
                                                  depth + 1,
                                                  disc_stride);
    } else {
      guint branch_row = current_row + 1;
      sgf_view_layout_attach_disc(self,
                                  disc_factory,
                                  grid,
                                  child,
                                  selected,
                                  node_widgets,
                                  branch_row,
                                  depth,
                                  disc_stride);
      current_row = sgf_view_layout_append_branch(self,
                                                  disc_factory,
                                                  grid,
                                                  child,
                                                  selected,
                                                  node_widgets,
                                                  branch_row,
                                                  depth + 1,
                                                  disc_stride);
    }
  }
  return current_row;
}

static void sgf_view_layout_class_init(SgfViewLayoutClass *klass) {
  (void)klass;
}

static void sgf_view_layout_init(SgfViewLayout *self) {
  (void)self;
}

SgfViewLayout *sgf_view_layout_new(void) {
  return g_object_new(SGF_TYPE_VIEW_LAYOUT, NULL);
}

void sgf_view_layout_build(SgfViewLayout *self,
                           GtkGrid *grid,
                           SgfTree *tree,
                           GHashTable *node_widgets,
                           SgfViewDiscFactory *disc_factory,
                           const SgfNode *selected,
                           int disc_stride) {
  g_return_if_fail(SGF_IS_VIEW_LAYOUT(self));
  g_return_if_fail(GTK_IS_GRID(grid));
  g_return_if_fail(SGF_IS_VIEW_DISC_FACTORY(disc_factory));

  sgf_view_layout_clear_container(GTK_WIDGET(grid));

  if (!tree) {
    return;
  }

  const SgfNode *root = sgf_tree_get_root(tree);
  if (!root) {
    return;
  }

  sgf_view_layout_append_branch(self, disc_factory, grid, root, selected, node_widgets, 0, 0, disc_stride);
}
