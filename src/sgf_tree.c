#include "sgf_tree.h"

#include <string.h>

struct _SgfNode {
  SgfColor color;
  guint move_number;
  SgfNode *parent;
  GPtrArray *children;
  GBytes *payload;
};

struct _SgfTree {
  GObject parent_instance;
  SgfNode *root;
  SgfNode *current;
};

G_DEFINE_TYPE(SgfTree, sgf_tree, G_TYPE_OBJECT)

static SgfNode *sgf_node_new(SgfNode *parent, SgfColor color, guint move_number, GBytes *payload) {
  SgfNode *node = g_new0(SgfNode, 1);

  node->color = color;
  node->move_number = move_number;
  node->parent = parent;
  node->children = g_ptr_array_new();
  if (payload) {
    node->payload = g_bytes_ref(payload);
  }

  return node;
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

  if (node->payload) {
    g_bytes_unref(node->payload);
    node->payload = NULL;
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

static void sgf_tree_clear_internal(SgfTree *self) {
  if (self->root) {
    sgf_node_free(self->root);
    self->root = NULL;
  }
  self->current = NULL;
}

static void sgf_tree_reset_internal(SgfTree *self) {
  sgf_tree_clear_internal(self);
  self->root = sgf_node_new(NULL, SGF_COLOR_NONE, 0, NULL);
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

const SgfNode *sgf_tree_append_move(SgfTree *self, SgfColor color, GBytes *payload) {
  g_return_val_if_fail(SGF_IS_TREE(self), NULL);
  g_return_val_if_fail(self->current != NULL, NULL);

  guint move_number = self->current->move_number + 1;
  SgfNode *node = sgf_node_new(self->current, color, move_number, payload);
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

GBytes *sgf_node_get_payload(const SgfNode *node) {
  g_return_val_if_fail(node != NULL, NULL);

  if (!node->payload) {
    return NULL;
  }

  return g_bytes_ref(node->payload);
}
