#ifndef SGF_TREE_H
#define SGF_TREE_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
  SGF_COLOR_NONE = 0,
  SGF_COLOR_BLACK,
  SGF_COLOR_WHITE
} SgfColor;

typedef struct _SgfNode SgfNode;

#define SGF_TYPE_TREE (sgf_tree_get_type())

G_DECLARE_FINAL_TYPE(SgfTree, sgf_tree, SGF, TREE, GObject)

SgfTree *sgf_tree_new(void);
void sgf_tree_reset(SgfTree *self);
const SgfNode *sgf_tree_get_root(SgfTree *self);
const SgfNode *sgf_tree_get_current(SgfTree *self);
const SgfNode *sgf_tree_append_move(SgfTree *self, SgfColor color, const char *move_value);
gboolean sgf_tree_set_current(SgfTree *self, const SgfNode *node);
GPtrArray *sgf_tree_build_main_line(SgfTree *self);

SgfColor sgf_node_get_color(const SgfNode *node);
guint sgf_node_get_move_number(const SgfNode *node);
const SgfNode *sgf_node_get_parent(const SgfNode *node);
const GPtrArray *sgf_node_get_children(const SgfNode *node);
gboolean sgf_node_add_property(SgfNode *node, const char *ident, const char *value);
gboolean sgf_node_clear_property(SgfNode *node, const char *ident);
const GPtrArray *sgf_node_get_property_values(const SgfNode *node, const char *ident);
const char *sgf_node_get_property_first(const SgfNode *node, const char *ident);
GPtrArray *sgf_node_copy_property_idents(const SgfNode *node);

G_END_DECLS

#endif
