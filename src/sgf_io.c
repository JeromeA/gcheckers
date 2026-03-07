#include "sgf_io.h"

#include "game.h"
#include "sgf_move_props.h"

#include <string.h>

/*
 * SGF properties used by gcheckers SGF IO.
 *
 * Standard metadata:
 * - FF[4]       : SGF file format version.
 * - CA[UTF-8]   : character encoding.
 * - AP[gcheckers]
 *               : application identifier.
 * - GM[40]      : game id marker used by gcheckers for checkers/draughts.
 *
 * Move properties:
 * - B[move]     : black move in gcheckers move notation, e.g. 12-16 or 23x18.
 * - W[move]     : white move in gcheckers move notation, e.g. 32-28 or 30x23.
 *
 * Variations are represented using standard SGF game-tree nesting with '(' and ')'.
 */

typedef struct {
  const char *text;
  gsize len;
  gsize pos;
} SgfIoParser;

typedef struct {
  GString *text;
} SgfIoWriter;

typedef struct {
  GHashTable *props;
  GPtrArray *idents;
} SgfIoPropertyBag;

static GQuark sgf_io_error_quark(void) {
  return g_quark_from_static_string("sgf-io-error");
}

static void sgf_io_destroy_property_values(gpointer data) {
  GPtrArray *values = data;
  if (values != NULL) {
    g_ptr_array_unref(values);
  }
}

static gint sgf_io_sort_strings(gconstpointer left, gconstpointer right) {
  return g_strcmp0((const char *)left, (const char *)right);
}

static SgfIoPropertyBag *sgf_io_property_bag_new(void) {
  SgfIoPropertyBag *bag = g_new0(SgfIoPropertyBag, 1);
  bag->props = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, sgf_io_destroy_property_values);
  bag->idents = g_ptr_array_new_with_free_func(g_free);
  return bag;
}

static void sgf_io_property_bag_free(SgfIoPropertyBag *bag) {
  if (bag == NULL) {
    return;
  }

  if (bag->props != NULL) {
    g_hash_table_unref(bag->props);
    bag->props = NULL;
  }
  if (bag->idents != NULL) {
    g_ptr_array_unref(bag->idents);
    bag->idents = NULL;
  }
  g_free(bag);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(SgfIoPropertyBag, sgf_io_property_bag_free)

static gboolean sgf_io_is_upper_alpha(char c) {
  return c >= 'A' && c <= 'Z';
}

static void sgf_io_skip_ws(SgfIoParser *p) {
  g_return_if_fail(p != NULL);

  while (p->pos < p->len) {
    char c = p->text[p->pos];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      p->pos++;
      continue;
    }
    break;
  }
}

static gboolean sgf_io_peek_char(SgfIoParser *p, char *out_char) {
  g_return_val_if_fail(p != NULL, FALSE);
  g_return_val_if_fail(out_char != NULL, FALSE);

  sgf_io_skip_ws(p);
  if (p->pos >= p->len) {
    return FALSE;
  }

  *out_char = p->text[p->pos];
  return TRUE;
}

static gboolean sgf_io_consume_char(SgfIoParser *p, char expected, GError **error) {
  g_return_val_if_fail(p != NULL, FALSE);

  sgf_io_skip_ws(p);
  if (p->pos >= p->len || p->text[p->pos] != expected) {
    g_set_error(error, sgf_io_error_quark(), 1, "Expected '%c' at offset %zu", expected, p->pos);
    return FALSE;
  }

  p->pos++;
  return TRUE;
}

static char *sgf_io_parse_prop_ident(SgfIoParser *p, GError **error) {
  g_return_val_if_fail(p != NULL, NULL);

  sgf_io_skip_ws(p);
  gsize start = p->pos;
  while (p->pos < p->len && sgf_io_is_upper_alpha(p->text[p->pos])) {
    p->pos++;
  }

  if (p->pos == start) {
    g_set_error(error, sgf_io_error_quark(), 2, "Expected SGF property identifier at offset %zu", p->pos);
    return NULL;
  }

  return g_strndup(p->text + start, p->pos - start);
}

static char *sgf_io_parse_prop_value(SgfIoParser *p, GError **error) {
  g_return_val_if_fail(p != NULL, NULL);

  if (!sgf_io_consume_char(p, '[', error)) {
    return NULL;
  }

  g_autoptr(GString) value = g_string_new(NULL);
  while (p->pos < p->len) {
    char c = p->text[p->pos++];
    if (c == ']') {
      return g_string_free(g_steal_pointer(&value), FALSE);
    }
    if (c == '\\') {
      if (p->pos >= p->len) {
        break;
      }
      c = p->text[p->pos++];
    }
    g_string_append_c(value, c);
  }

  g_set_error_literal(error, sgf_io_error_quark(), 3, "Unterminated SGF property value");
  return NULL;
}

static gboolean sgf_io_parse_node_properties(SgfIoParser *p, SgfIoPropertyBag *bag, GError **error) {
  g_return_val_if_fail(p != NULL, FALSE);
  g_return_val_if_fail(bag != NULL, FALSE);
  g_return_val_if_fail(bag->props != NULL, FALSE);
  g_return_val_if_fail(bag->idents != NULL, FALSE);

  while (TRUE) {
    char next = '\0';
    if (!sgf_io_peek_char(p, &next) || !sgf_io_is_upper_alpha(next)) {
      return TRUE;
    }

    g_autofree char *ident = sgf_io_parse_prop_ident(p, error);
    if (ident == NULL) {
      return FALSE;
    }

    GPtrArray *values = g_hash_table_lookup(bag->props, ident);
    if (values == NULL) {
      values = g_ptr_array_new_with_free_func(g_free);
      g_hash_table_insert(bag->props, g_strdup(ident), values);
      g_ptr_array_add(bag->idents, g_strdup(ident));
    }

    g_autofree char *first_value = sgf_io_parse_prop_value(p, error);
    if (first_value == NULL) {
      return FALSE;
    }
    g_ptr_array_add(values, g_strdup(first_value));

    while (TRUE) {
      char maybe_more = '\0';
      if (!sgf_io_peek_char(p, &maybe_more) || maybe_more != '[') {
        break;
      }
      g_autofree char *next_value = sgf_io_parse_prop_value(p, error);
      if (next_value == NULL) {
        return FALSE;
      }
      g_ptr_array_add(values, g_strdup(next_value));
    }
  }
}

static char *sgf_io_escape_prop_value(const char *value) {
  g_return_val_if_fail(value != NULL, NULL);

  g_autoptr(GString) escaped = g_string_new(NULL);
  for (const char *c = value; *c != '\0'; c++) {
    if (*c == '\\' || *c == ']') {
      g_string_append_c(escaped, '\\');
    }
    g_string_append_c(escaped, *c);
  }

  return g_string_free(g_steal_pointer(&escaped), FALSE);
}

static const GPtrArray *sgf_io_property_bag_get_values(const SgfIoPropertyBag *bag, const char *ident) {
  g_return_val_if_fail(bag != NULL, NULL);
  g_return_val_if_fail(bag->props != NULL, NULL);
  g_return_val_if_fail(ident != NULL, NULL);
  return g_hash_table_lookup(bag->props, ident);
}

static const char *sgf_io_property_bag_get_first(const SgfIoPropertyBag *bag, const char *ident) {
  const GPtrArray *values = sgf_io_property_bag_get_values(bag, ident);
  if (values == NULL || values->len == 0) {
    return NULL;
  }

  return g_ptr_array_index((GPtrArray *)values, 0);
}

static gboolean sgf_io_node_apply_property_values(SgfNode *node, const char *ident, const GPtrArray *values) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(ident != NULL, FALSE);
  g_return_val_if_fail(values != NULL, FALSE);

  sgf_node_clear_property(node, ident);
  for (guint i = 0; i < values->len; ++i) {
    const char *value = g_ptr_array_index((GPtrArray *)values, i);
    if (!sgf_node_add_property(node, ident, value)) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean sgf_io_append_node_properties(SgfIoWriter *writer, const SgfNode *node, GError **error) {
  g_return_val_if_fail(writer != NULL, FALSE);
  g_return_val_if_fail(writer->text != NULL, FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  g_autoptr(GPtrArray) idents = sgf_node_copy_property_idents(node);
  if (idents == NULL) {
    g_set_error_literal(error, sgf_io_error_quark(), 11, "Missing SGF node property map");
    return FALSE;
  }

  for (guint i = 0; i < idents->len; ++i) {
    const char *ident = g_ptr_array_index(idents, i);
    const GPtrArray *values = sgf_node_get_property_values(node, ident);
    if (values == NULL || values->len == 0) {
      continue;
    }

    g_string_append(writer->text, ident);
    for (guint j = 0; j < values->len; ++j) {
      const char *value = g_ptr_array_index((GPtrArray *)values, j);
      g_autofree char *escaped_value = sgf_io_escape_prop_value(value);
      if (escaped_value == NULL) {
        g_set_error_literal(error, sgf_io_error_quark(), 12, "Unable to escape SGF property value");
        return FALSE;
      }
      g_string_append_printf(writer->text, "[%s]", escaped_value);
    }
  }

  return TRUE;
}

static gboolean sgf_io_append_node(SgfIoWriter *writer,
                                   const SgfNode *node,
                                   gboolean /*is_root*/,
                                   GError **error) {
  g_return_val_if_fail(writer != NULL, FALSE);
  g_return_val_if_fail(writer->text != NULL, FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  g_string_append_c(writer->text, ';');
  if (!sgf_io_append_node_properties(writer, node, error)) {
    return FALSE;
  }

  const GPtrArray *children = sgf_node_get_children(node);
  if (children == NULL || children->len == 0) {
    return TRUE;
  }

  if (children->len == 1) {
    const SgfNode *child = g_ptr_array_index((GPtrArray *)children, 0);
    g_return_val_if_fail(child != NULL, FALSE);
    return sgf_io_append_node(writer, child, FALSE, error);
  }

  for (guint i = 0; i < children->len; ++i) {
    const SgfNode *child = g_ptr_array_index((GPtrArray *)children, i);
    g_return_val_if_fail(child != NULL, FALSE);

    g_string_append_c(writer->text, '(');
    if (!sgf_io_append_node(writer, child, FALSE, error)) {
      return FALSE;
    }
    g_string_append_c(writer->text, ')');
  }

  return TRUE;
}

char *sgf_io_save_data(SgfTree *tree, GError **error) {
  g_return_val_if_fail(SGF_IS_TREE(tree), NULL);

  const SgfNode *root_const = sgf_tree_get_root(tree);
  SgfNode *root = (SgfNode *)root_const;
  if (root == NULL) {
    g_set_error_literal(error, sgf_io_error_quark(), 13, "Missing SGF root node");
    return NULL;
  }

  if (sgf_node_get_property_first(root, "FF") == NULL) {
    sgf_node_add_property(root, "FF", "4");
  }
  if (sgf_node_get_property_first(root, "CA") == NULL) {
    sgf_node_add_property(root, "CA", "UTF-8");
  }
  if (sgf_node_get_property_first(root, "AP") == NULL) {
    sgf_node_add_property(root, "AP", "gcheckers");
  }
  if (sgf_node_get_property_first(root, "GM") == NULL) {
    sgf_node_add_property(root, "GM", "40");
  }

  SgfIoWriter writer = {
    .text = g_string_new(NULL),
  };

  g_string_append_c(writer.text, '(');
  if (!sgf_io_append_node(&writer, root_const, TRUE, error)) {
    g_string_free(writer.text, TRUE);
    return NULL;
  }
  g_string_append_c(writer.text, ')');

  return g_string_free(writer.text, FALSE);
}

gboolean sgf_io_save_file(const char *path, SgfTree *tree, GError **error) {
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(SGF_IS_TREE(tree), FALSE);

  g_autofree char *content = sgf_io_save_data(tree, error);
  if (content == NULL) {
    return FALSE;
  }

  return g_file_set_contents(path, content, -1, error);
}

static gboolean sgf_io_parse_tree(SgfIoParser *p,
                                  SgfTree *tree,
                                  const SgfNode *start_parent,
                                  gboolean *seen_root,
                                  GError **error) {
  g_return_val_if_fail(p != NULL, FALSE);
  g_return_val_if_fail(SGF_IS_TREE(tree), FALSE);
  g_return_val_if_fail(seen_root != NULL, FALSE);

  if (!sgf_io_consume_char(p, '(', error)) {
    return FALSE;
  }

  const SgfNode *cursor = start_parent;
  gboolean has_sequence_node = FALSE;

  while (TRUE) {
    char next = '\0';
    if (!sgf_io_peek_char(p, &next)) {
      g_set_error_literal(error, sgf_io_error_quark(), 17, "Unexpected EOF while parsing SGF tree");
      return FALSE;
    }

    if (next == ';') {
      p->pos++;
      has_sequence_node = TRUE;

      g_autoptr(SgfIoPropertyBag) props = sgf_io_property_bag_new();
      if (!sgf_io_parse_node_properties(p, props, error)) {
        return FALSE;
      }

      if (!(*seen_root)) {
        *seen_root = TRUE;
        cursor = sgf_tree_get_root(tree);
        SgfNode *root_node = (SgfNode *)cursor;
        g_ptr_array_sort(props->idents, sgf_io_sort_strings);
        for (guint i = 0; i < props->idents->len; ++i) {
          const char *ident = g_ptr_array_index(props->idents, i);
          const GPtrArray *values = sgf_io_property_bag_get_values(props, ident);
          g_return_val_if_fail(values != NULL, FALSE);
          if (!sgf_io_node_apply_property_values(root_node, ident, values)) {
            g_set_error_literal(error, sgf_io_error_quark(), 14, "Unable to apply SGF root property");
            return FALSE;
          }
        }
        continue;
      }

      const char *black_move = sgf_io_property_bag_get_first(props, "B");
      const char *white_move = sgf_io_property_bag_get_first(props, "W");
      if ((black_move == NULL) == (white_move == NULL)) {
        g_set_error_literal(error, sgf_io_error_quark(), 15, "SGF move node must contain exactly one of B[] or W[]");
        return FALSE;
      }

      CheckersMove move = {0};
      if (!sgf_move_props_parse_notation(black_move != NULL ? black_move : white_move, &move, error)) {
        return FALSE;
      }

      if (!sgf_tree_set_current(tree, cursor)) {
        g_set_error_literal(error, sgf_io_error_quark(), 16, "Unable to select SGF parent node");
        return FALSE;
      }

      char move_text[128] = {0};
      if (!sgf_move_props_format_notation(&move, move_text, sizeof(move_text), error)) {
        return FALSE;
      }
      SgfColor color = black_move != NULL ? SGF_COLOR_BLACK : SGF_COLOR_WHITE;
      const SgfNode *node = sgf_tree_append_move(tree, color, move_text);
      if (node == NULL) {
        g_set_error_literal(error, sgf_io_error_quark(), 17, "Unable to append SGF move node");
        return FALSE;
      }

      g_ptr_array_sort(props->idents, sgf_io_sort_strings);
      SgfNode *node_mut = (SgfNode *)node;
      for (guint i = 0; i < props->idents->len; ++i) {
        const char *ident = g_ptr_array_index(props->idents, i);
        const GPtrArray *values = sgf_io_property_bag_get_values(props, ident);
        g_return_val_if_fail(values != NULL, FALSE);
        if (!sgf_io_node_apply_property_values(node_mut, ident, values)) {
          g_set_error_literal(error, sgf_io_error_quark(), 18, "Unable to apply SGF move-node property");
          return FALSE;
        }
      }

      cursor = node;
      continue;
    }

    if (next == '(') {
      if (!has_sequence_node) {
        g_set_error_literal(error, sgf_io_error_quark(), 19, "SGF variation without node sequence");
        return FALSE;
      }
      if (!sgf_io_parse_tree(p, tree, cursor, seen_root, error)) {
        return FALSE;
      }
      continue;
    }

    if (next == ')') {
      p->pos++;
      return TRUE;
    }

    g_set_error(error, sgf_io_error_quark(), 20, "Unexpected SGF token '%c' at offset %zu", next, p->pos);
    return FALSE;
  }
}

gboolean sgf_io_load_data(const char *content, SgfTree **out_tree, GError **error) {
  g_return_val_if_fail(content != NULL, FALSE);
  g_return_val_if_fail(out_tree != NULL, FALSE);

  g_autoptr(SgfTree) tree = sgf_tree_new();
  const SgfNode *root = sgf_tree_get_root(tree);
  if (root == NULL) {
    g_set_error_literal(error, sgf_io_error_quark(), 21, "Unable to initialize SGF tree");
    return FALSE;
  }

  SgfIoParser parser = {
    .text = content,
    .len = strlen(content),
    .pos = 0,
  };

  gboolean seen_root = FALSE;
  if (!sgf_io_parse_tree(&parser, tree, root, &seen_root, error)) {
    return FALSE;
  }
  if (!seen_root) {
    g_set_error_literal(error, sgf_io_error_quark(), 22, "Missing SGF root node");
    return FALSE;
  }

  if (!sgf_tree_set_current(tree, root)) {
    g_set_error_literal(error, sgf_io_error_quark(), 23, "Unable to select SGF root after load");
    return FALSE;
  }

  g_clear_object(out_tree);
  *out_tree = g_steal_pointer(&tree);
  return TRUE;
}

gboolean sgf_io_load_file(const char *path, SgfTree **out_tree, GError **error) {
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(out_tree != NULL, FALSE);

  g_autofree char *content = NULL;
  gsize len = 0;
  if (!g_file_get_contents(path, &content, &len, error)) {
    return FALSE;
  }
  if (len == 0) {
    g_set_error(error, sgf_io_error_quark(), 24, "Empty SGF file: %s", path);
    return FALSE;
  }

  return sgf_io_load_data(content, out_tree, error);
}
