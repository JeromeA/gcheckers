#ifndef SGF_TREE_H
#define SGF_TREE_H

#include "game.h"

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
  SGF_COLOR_NONE = 0,
  SGF_COLOR_BLACK,
  SGF_COLOR_WHITE
} SgfColor;

typedef struct _SgfNode SgfNode;

typedef struct {
  CheckersMove move;
  gint score;
} SgfNodeScoredMove;

typedef struct {
  guint depth;
  guint64 nodes;
  guint64 tt_probes;
  guint64 tt_hits;
  guint64 tt_cutoffs;
  GPtrArray *moves;
} SgfNodeAnalysis;

#define SGF_TYPE_TREE (sgf_tree_get_type())

G_DECLARE_FINAL_TYPE(SgfTree, sgf_tree, SGF, TREE, GObject)

SgfTree *sgf_tree_new(void);
void sgf_tree_reset(SgfTree *self);
const SgfNode *sgf_tree_get_root(SgfTree *self);
const SgfNode *sgf_tree_get_current(SgfTree *self);
const SgfNode *sgf_tree_append_move(SgfTree *self, SgfColor color, const char *move_value);
gboolean sgf_tree_set_current(SgfTree *self, const SgfNode *node);
GPtrArray *sgf_tree_build_main_line(SgfTree *self);
GPtrArray *sgf_tree_build_path_to_node(SgfTree *self, const SgfNode *node);
GPtrArray *sgf_tree_build_main_line_from_node(const SgfNode *node);
GPtrArray *sgf_tree_build_current_branch(SgfTree *self);
GPtrArray *sgf_tree_collect_nodes_preorder(SgfTree *self);

SgfColor sgf_node_get_color(const SgfNode *node);
guint sgf_node_get_move_number(const SgfNode *node);
const SgfNode *sgf_node_get_parent(const SgfNode *node);
const GPtrArray *sgf_node_get_children(const SgfNode *node);
gboolean sgf_node_add_property(SgfNode *node, const char *ident, const char *value);
gboolean sgf_node_clear_property(SgfNode *node, const char *ident);
const GPtrArray *sgf_node_get_property_values(const SgfNode *node, const char *ident);
const char *sgf_node_get_property_first(const SgfNode *node, const char *ident);
GPtrArray *sgf_node_copy_property_idents(const SgfNode *node);
SgfNodeAnalysis *sgf_node_analysis_new(void);
SgfNodeAnalysis *sgf_node_analysis_copy(const SgfNodeAnalysis *analysis);
void sgf_node_analysis_free(SgfNodeAnalysis *analysis);
gboolean sgf_node_analysis_add_scored_move(SgfNodeAnalysis *analysis, const CheckersMove *move, gint score);
gboolean sgf_node_set_analysis(SgfNode *node, const SgfNodeAnalysis *analysis);
SgfNodeAnalysis *sgf_node_get_analysis(const SgfNode *node);
gboolean sgf_node_clear_analysis(SgfNode *node);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(SgfNodeAnalysis, sgf_node_analysis_free)

G_END_DECLS

#endif
