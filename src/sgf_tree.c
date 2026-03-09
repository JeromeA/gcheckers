#include "sgf_tree.h"

struct _SgfNode {
  SgfColor color;
  guint move_number;
  SgfNode *parent;
  GPtrArray *children;
  GHashTable *properties;
  SgfNodeAnalysis *analysis;
};

struct _SgfTree {
  GObject parent_instance;
  SgfNode *root;
  SgfNode *current;
};

G_DEFINE_TYPE(SgfTree, sgf_tree, G_TYPE_OBJECT)

static gint sgf_tree_sort_strings(gconstpointer left, gconstpointer right) {
  g_return_val_if_fail(left != NULL, 0);
  g_return_val_if_fail(right != NULL, 0);
  return g_strcmp0((const char *)left, (const char *)right);
}

static const char *sgf_tree_move_ident_for_color(SgfColor color) {
  if (color == SGF_COLOR_BLACK) {
    return "B";
  }

  if (color == SGF_COLOR_WHITE) {
    return "W";
  }

  return NULL;
}

static SgfNode *sgf_node_new(SgfNode *parent, SgfColor color, guint move_number) {
  SgfNode *node = g_new0(SgfNode, 1);

  node->color = color;
  node->move_number = move_number;
  node->parent = parent;
  node->children = g_ptr_array_new();
  node->properties = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_ptr_array_unref);
  node->analysis = NULL;

  return node;
}

static void sgf_node_scored_move_free(gpointer data) {
  SgfNodeScoredMove *scored = data;
  g_free(scored);
}

SgfNodeAnalysis *sgf_node_analysis_new(void) {
  SgfNodeAnalysis *analysis = g_new0(SgfNodeAnalysis, 1);
  analysis->moves = g_ptr_array_new_with_free_func(sgf_node_scored_move_free);
  return analysis;
}

void sgf_node_analysis_free(SgfNodeAnalysis *analysis) {
  if (analysis == NULL) {
    return;
  }

  if (analysis->moves != NULL) {
    g_ptr_array_unref(analysis->moves);
    analysis->moves = NULL;
  }

  g_free(analysis);
}

gboolean sgf_node_analysis_add_scored_move(SgfNodeAnalysis *analysis, const CheckersMove *move, gint score) {
  g_return_val_if_fail(analysis != NULL, FALSE);
  g_return_val_if_fail(analysis->moves != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(move->length >= 2, FALSE);

  SgfNodeScoredMove *entry = g_new0(SgfNodeScoredMove, 1);
  entry->move = *move;
  entry->score = score;
  g_ptr_array_add(analysis->moves, entry);
  return TRUE;
}

SgfNodeAnalysis *sgf_node_analysis_copy(const SgfNodeAnalysis *analysis) {
  g_return_val_if_fail(analysis != NULL, NULL);
  g_return_val_if_fail(analysis->moves != NULL, NULL);

  SgfNodeAnalysis *copy = sgf_node_analysis_new();
  copy->depth = analysis->depth;
  copy->nodes = analysis->nodes;
  copy->tt_probes = analysis->tt_probes;
  copy->tt_hits = analysis->tt_hits;
  copy->tt_cutoffs = analysis->tt_cutoffs;
  for (guint i = 0; i < analysis->moves->len; ++i) {
    const SgfNodeScoredMove *entry = g_ptr_array_index(analysis->moves, i);
    if (entry == NULL) {
      g_debug("Missing scored move entry while copying SGF analysis");
      sgf_node_analysis_free(copy);
      return NULL;
    }
    if (!sgf_node_analysis_add_scored_move(copy, &entry->move, entry->score)) {
      sgf_node_analysis_free(copy);
      return NULL;
    }
  }
  return copy;
}

static void sgf_node_free(SgfNode *node) {
  if (!node) {
    return;
  }

  if (node->children) {
    for (guint i = 0; i < node->children->len; ++i) {
      sgf_node_free(g_ptr_array_index(node->children, i));
    }
    g_ptr_array_free(node->children, TRUE);
    node->children = NULL;
  }

  if (node->properties) {
    g_hash_table_unref(node->properties);
    node->properties = NULL;
  }
  if (node->analysis != NULL) {
    sgf_node_analysis_free(node->analysis);
    node->analysis = NULL;
  }

  g_free(node);
}

static gboolean sgf_node_is_descendant(const SgfNode *node, const SgfNode *root) {
  const SgfNode *cursor = node;

  while (cursor) {
    if (cursor == root) {
      return TRUE;
    }
    cursor = cursor->parent;
  }

  return FALSE;
}

static gboolean sgf_node_matches_move(const SgfNode *node, SgfColor color, const char *move_value) {
  if (!node) {
    return FALSE;
  }

  if (node->color != color) {
    return FALSE;
  }

  const char *ident = sgf_tree_move_ident_for_color(color);
  if (ident == NULL) {
    return move_value == NULL;
  }

  const char *candidate_value = sgf_node_get_property_first(node, ident);
  return g_strcmp0(candidate_value, move_value) == 0;
}

static void sgf_tree_clear_internal(SgfTree *self) {
  if (self->root) {
    sgf_node_free(self->root);
    self->root = NULL;
  }
  self->current = NULL;
}

static void sgf_tree_reset_internal(SgfTree *self) {
  sgf_tree_clear_internal(self);
  self->root = sgf_node_new(NULL, SGF_COLOR_NONE, 0);
  self->current = self->root;
}

static void sgf_tree_dispose(GObject *object) {
  SgfTree *self = SGF_TREE(object);

  sgf_tree_clear_internal(self);

  G_OBJECT_CLASS(sgf_tree_parent_class)->dispose(object);
}

static void sgf_tree_class_init(SgfTreeClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = sgf_tree_dispose;
}

static void sgf_tree_init(SgfTree *self) {
  sgf_tree_reset_internal(self);
}

SgfTree *sgf_tree_new(void) {
  return g_object_new(SGF_TYPE_TREE, NULL);
}

void sgf_tree_reset(SgfTree *self) {
  g_return_if_fail(SGF_IS_TREE(self));

  sgf_tree_reset_internal(self);
}

const SgfNode *sgf_tree_get_root(SgfTree *self) {
  g_return_val_if_fail(SGF_IS_TREE(self), NULL);

  return self->root;
}

const SgfNode *sgf_tree_get_current(SgfTree *self) {
  g_return_val_if_fail(SGF_IS_TREE(self), NULL);

  return self->current;
}

const SgfNode *sgf_tree_append_node(SgfTree *self) {
  g_return_val_if_fail(SGF_IS_TREE(self), NULL);
  g_return_val_if_fail(self->current != NULL, NULL);

  guint move_number = self->current->move_number;
  SgfNode *node = sgf_node_new(self->current, SGF_COLOR_NONE, move_number);
  g_ptr_array_add(self->current->children, node);
  self->current = node;

  return node;
}

const SgfNode *sgf_tree_append_move(SgfTree *self, SgfColor color, const char *move_value) {
  g_return_val_if_fail(SGF_IS_TREE(self), NULL);
  g_return_val_if_fail(self->current != NULL, NULL);
  g_return_val_if_fail(color == SGF_COLOR_BLACK || color == SGF_COLOR_WHITE, NULL);

  if (self->current->children) {
    for (guint i = 0; i < self->current->children->len; ++i) {
      SgfNode *candidate = g_ptr_array_index(self->current->children, i);
      if (sgf_node_matches_move(candidate, color, move_value)) {
        self->current = candidate;
        return candidate;
      }
    }
  }

  guint move_number = self->current->move_number + 1;
  SgfNode *node = sgf_node_new(self->current, color, move_number);
  if (move_value != NULL) {
    const char *ident = sgf_tree_move_ident_for_color(color);
    if (!sgf_node_add_property(node, ident, move_value)) {
      sgf_node_free(node);
      return NULL;
    }
  }
  g_ptr_array_add(self->current->children, node);
  self->current = node;

  return node;
}

gboolean sgf_tree_set_current(SgfTree *self, const SgfNode *node) {
  g_return_val_if_fail(SGF_IS_TREE(self), FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  if (!sgf_node_is_descendant(node, self->root)) {
    g_debug("Attempted to select SGF node not in tree\n");
    return FALSE;
  }

  self->current = (SgfNode *)node;
  return TRUE;
}

GPtrArray *sgf_tree_build_main_line(SgfTree *self) {
  g_return_val_if_fail(SGF_IS_TREE(self), NULL);

  GPtrArray *line = g_ptr_array_new();
  const SgfNode *cursor = self->root;

  while (cursor) {
    g_ptr_array_add(line, (gpointer)cursor);
    if (!cursor->children || cursor->children->len == 0) {
      break;
    }
    cursor = g_ptr_array_index(cursor->children, 0);
  }

  return line;
}

GPtrArray *sgf_tree_build_path_to_node(SgfTree *self, const SgfNode *node) {
  g_return_val_if_fail(SGF_IS_TREE(self), NULL);
  g_return_val_if_fail(node != NULL, NULL);

  if (!sgf_node_is_descendant(node, self->root)) {
    g_debug("Attempted to build path for SGF node not in tree");
    return NULL;
  }

  GPtrArray *path = g_ptr_array_new();
  const SgfNode *cursor = node;
  while (cursor != NULL) {
    g_ptr_array_add(path, (gpointer)cursor);
    cursor = cursor->parent;
  }

  for (guint i = 0; i < path->len / 2; ++i) {
    gpointer tmp = g_ptr_array_index(path, i);
    g_ptr_array_index(path, i) = g_ptr_array_index(path, path->len - 1 - i);
    g_ptr_array_index(path, path->len - 1 - i) = tmp;
  }

  return path;
}

GPtrArray *sgf_tree_build_main_line_from_node(const SgfNode *node) {
  g_return_val_if_fail(node != NULL, NULL);

  GPtrArray *line = g_ptr_array_new();
  const SgfNode *cursor = node;
  while (cursor != NULL) {
    g_ptr_array_add(line, (gpointer)cursor);
    const GPtrArray *children = cursor->children;
    if (children == NULL || children->len == 0) {
      break;
    }
    cursor = g_ptr_array_index((GPtrArray *)children, 0);
  }

  return line;
}

GPtrArray *sgf_tree_build_current_branch(SgfTree *self) {
  g_return_val_if_fail(SGF_IS_TREE(self), NULL);
  g_return_val_if_fail(self->current != NULL, NULL);

  g_autoptr(GPtrArray) to_current = sgf_tree_build_path_to_node(self, self->current);
  if (to_current == NULL) {
    return NULL;
  }

  g_autoptr(GPtrArray) main_line = sgf_tree_build_main_line_from_node(self->current);
  if (main_line == NULL) {
    return NULL;
  }

  GPtrArray *branch = g_ptr_array_new();
  for (guint i = 0; i < to_current->len; ++i) {
    g_ptr_array_add(branch, g_ptr_array_index(to_current, i));
  }
  for (guint i = 1; i < main_line->len; ++i) {
    g_ptr_array_add(branch, g_ptr_array_index(main_line, i));
  }

  return branch;
}

static void sgf_tree_collect_nodes_preorder_from(const SgfNode *node, GPtrArray *nodes) {
  g_return_if_fail(node != NULL);
  g_return_if_fail(nodes != NULL);

  g_ptr_array_add(nodes, (gpointer)node);
  const GPtrArray *children = node->children;
  if (children == NULL || children->len == 0) {
    return;
  }

  for (guint i = 0; i < children->len; ++i) {
    const SgfNode *child = g_ptr_array_index((GPtrArray *)children, i);
    g_return_if_fail(child != NULL);
    sgf_tree_collect_nodes_preorder_from(child, nodes);
  }
}

GPtrArray *sgf_tree_collect_nodes_preorder(SgfTree *self) {
  g_return_val_if_fail(SGF_IS_TREE(self), NULL);
  g_return_val_if_fail(self->root != NULL, NULL);

  GPtrArray *nodes = g_ptr_array_new();
  sgf_tree_collect_nodes_preorder_from(self->root, nodes);
  return nodes;
}

SgfColor sgf_node_get_color(const SgfNode *node) {
  g_return_val_if_fail(node != NULL, SGF_COLOR_NONE);

  return node->color;
}

guint sgf_node_get_move_number(const SgfNode *node) {
  g_return_val_if_fail(node != NULL, 0);

  return node->move_number;
}

const SgfNode *sgf_node_get_parent(const SgfNode *node) {
  g_return_val_if_fail(node != NULL, NULL);

  return node->parent;
}

const GPtrArray *sgf_node_get_children(const SgfNode *node) {
  g_return_val_if_fail(node != NULL, NULL);

  return node->children;
}

gboolean sgf_node_add_property(SgfNode *node, const char *ident, const char *value) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(node->properties != NULL, FALSE);
  g_return_val_if_fail(ident != NULL, FALSE);
  g_return_val_if_fail(value != NULL, FALSE);

  GPtrArray *values = g_hash_table_lookup(node->properties, ident);
  if (values == NULL) {
    values = g_ptr_array_new_with_free_func(g_free);
    g_hash_table_insert(node->properties, g_strdup(ident), values);
  }

  g_ptr_array_add(values, g_strdup(value));
  return TRUE;
}

gboolean sgf_node_clear_property(SgfNode *node, const char *ident) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(node->properties != NULL, FALSE);
  g_return_val_if_fail(ident != NULL, FALSE);

  return g_hash_table_remove(node->properties, ident);
}

const GPtrArray *sgf_node_get_property_values(const SgfNode *node, const char *ident) {
  g_return_val_if_fail(node != NULL, NULL);
  g_return_val_if_fail(node->properties != NULL, NULL);
  g_return_val_if_fail(ident != NULL, NULL);

  return g_hash_table_lookup(node->properties, ident);
}

const char *sgf_node_get_property_first(const SgfNode *node, const char *ident) {
  g_return_val_if_fail(node != NULL, NULL);
  g_return_val_if_fail(node->properties != NULL, NULL);
  g_return_val_if_fail(ident != NULL, NULL);

  const GPtrArray *values = sgf_node_get_property_values(node, ident);
  if (values == NULL || values->len == 0) {
    return NULL;
  }

  return g_ptr_array_index((GPtrArray *)values, 0);
}

GPtrArray *sgf_node_copy_property_idents(const SgfNode *node) {
  g_return_val_if_fail(node != NULL, NULL);
  g_return_val_if_fail(node->properties != NULL, NULL);

  GPtrArray *idents = g_ptr_array_new_with_free_func(g_free);
  GList *keys = g_hash_table_get_keys(node->properties);
  if (keys != NULL) {
    keys = g_list_sort(keys, sgf_tree_sort_strings);
    for (GList *cursor = keys; cursor != NULL; cursor = cursor->next) {
      g_ptr_array_add(idents, g_strdup((const char *)cursor->data));
    }
    g_list_free(keys);
  }

  return idents;
}

gboolean sgf_node_set_analysis(SgfNode *node, const SgfNodeAnalysis *analysis) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(analysis != NULL, FALSE);
  g_return_val_if_fail(analysis->moves != NULL, FALSE);

  SgfNodeAnalysis *copy = sgf_node_analysis_copy(analysis);
  if (copy == NULL) {
    g_debug("Failed to copy SGF node analysis");
    return FALSE;
  }

  sgf_node_analysis_free(node->analysis);
  node->analysis = copy;
  return TRUE;
}

SgfNodeAnalysis *sgf_node_get_analysis(const SgfNode *node) {
  g_return_val_if_fail(node != NULL, NULL);

  if (node->analysis == NULL) {
    return NULL;
  }

  return sgf_node_analysis_copy(node->analysis);
}

gboolean sgf_node_clear_analysis(SgfNode *node) {
  g_return_val_if_fail(node != NULL, FALSE);

  if (node->analysis == NULL) {
    return FALSE;
  }

  sgf_node_analysis_free(node->analysis);
  node->analysis = NULL;
  return TRUE;
}
