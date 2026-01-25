#include "sgf_view_layout.h"

struct _SgfViewLayout {
  GObject parent_instance;
};

G_DEFINE_TYPE(SgfViewLayout, sgf_view_layout, G_TYPE_OBJECT)

static void sgf_view_layout_clear_container(GtkWidget *container) {
  g_return_if_fail(GTK_IS_GRID(container));

  GtkWidget *child = gtk_widget_get_first_child(container);
  while (child) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_grid_remove(GTK_GRID(container), child);
    child = next;
  }
}

static void sgf_view_layout_update_extent(GArray *extents, guint index, int value) {
  if (!extents) {
    return;
  }

  if (extents->len <= index) {
    g_array_set_size(extents, index + 1);
  }

  int current = g_array_index(extents, int, index);
  if (value > current) {
    g_array_index(extents, int, index) = value;
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
                                        int disc_stride,
                                        GArray *column_widths,
                                        GArray *row_heights,
                                        guint *max_row,
                                        guint *max_column) {
  g_return_if_fail(SGF_IS_VIEW_LAYOUT(self));
  g_return_if_fail(SGF_IS_VIEW_DISC_FACTORY(disc_factory));
  g_return_if_fail(GTK_IS_GRID(grid));
  g_return_if_fail(node != NULL);

  GtkWidget *disc = sgf_view_layout_build_disc(self,
                                               disc_factory,
                                               node,
                                               selected,
                                               node_widgets,
                                               disc_stride);

  int min_width = 0;
  int natural_width = 0;
  int min_height = 0;
  int natural_height = 0;
  gtk_widget_measure(disc,
                     GTK_ORIENTATION_HORIZONTAL,
                     -1,
                     &min_width,
                     &natural_width,
                     NULL,
                     NULL);
  gtk_widget_measure(disc,
                     GTK_ORIENTATION_VERTICAL,
                     -1,
                     &min_height,
                     &natural_height,
                     NULL,
                     NULL);
  int measured_width = MAX(natural_width, min_width);
  int measured_height = MAX(natural_height, min_height);
  measured_width = MAX(measured_width, disc_stride);
  measured_height = MAX(measured_height, disc_stride);
  sgf_view_layout_update_extent(column_widths, column, measured_width);
  sgf_view_layout_update_extent(row_heights, row, measured_height);

  gtk_grid_attach(grid, disc, (int)column, (int)row, 1, 1);

  if (max_row) {
    *max_row = MAX(*max_row, row);
  }
  if (max_column) {
    *max_column = MAX(*max_column, column);
  }
}

static guint sgf_view_layout_append_branch(SgfViewLayout *self,
                                           SgfViewDiscFactory *disc_factory,
                                           GtkGrid *grid,
                                           const SgfNode *parent,
                                           const SgfNode *selected,
                                           GHashTable *node_widgets,
                                           guint row,
                                           guint depth,
                                           int disc_stride,
                                           GArray *column_widths,
                                           GArray *row_heights,
                                           guint *max_row,
                                           guint *max_column) {
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
                                  disc_stride,
                                  column_widths,
                                  row_heights,
                                  max_row,
                                  max_column);
      current_row = sgf_view_layout_append_branch(self,
                                                  disc_factory,
                                                  grid,
                                                  child,
                                                  selected,
                                                  node_widgets,
                                                  row,
                                                  depth + 1,
                                                  disc_stride,
                                                  column_widths,
                                                  row_heights,
                                                  max_row,
                                                  max_column);
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
                                  disc_stride,
                                  column_widths,
                                  row_heights,
                                  max_row,
                                  max_column);
      current_row = sgf_view_layout_append_branch(self,
                                                  disc_factory,
                                                  grid,
                                                  child,
                                                  selected,
                                                  node_widgets,
                                                  branch_row,
                                                  depth + 1,
                                                  disc_stride,
                                                  column_widths,
                                                  row_heights,
                                                  max_row,
                                                  max_column);
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
                           int disc_stride,
                           GArray *column_widths,
                           GArray *row_heights,
                           guint *out_max_row,
                           guint *out_max_column) {
  g_return_if_fail(SGF_IS_VIEW_LAYOUT(self));
  g_return_if_fail(GTK_IS_GRID(grid));
  g_return_if_fail(SGF_IS_VIEW_DISC_FACTORY(disc_factory));

  if (out_max_row) {
    *out_max_row = 0;
  }
  if (out_max_column) {
    *out_max_column = 0;
  }

  if (column_widths) {
    g_array_set_size(column_widths, 0);
  }
  if (row_heights) {
    g_array_set_size(row_heights, 0);
  }

  sgf_view_layout_clear_container(GTK_WIDGET(grid));

  if (!tree) {
    return;
  }

  const SgfNode *root = sgf_tree_get_root(tree);
  if (!root) {
    return;
  }

  guint max_row = 0;
  guint max_column = 0;
  sgf_view_layout_attach_disc(self,
                              disc_factory,
                              grid,
                              root,
                              selected,
                              node_widgets,
                              0,
                              0,
                              disc_stride,
                              column_widths,
                              row_heights,
                              &max_row,
                              &max_column);

  const GPtrArray *children = sgf_node_get_children(root);
  if (!children || children->len == 0) {
    if (out_max_row) {
      *out_max_row = max_row;
    }
    if (out_max_column) {
      *out_max_column = max_column;
    }
    return;
  }

  sgf_view_layout_append_branch(self,
                                disc_factory,
                                grid,
                                root,
                                selected,
                                node_widgets,
                                0,
                                1,
                                disc_stride,
                                column_widths,
                                row_heights,
                                &max_row,
                                &max_column);

  if (out_max_row) {
    *out_max_row = max_row;
  }
  if (out_max_column) {
    *out_max_column = max_column;
  }
}
