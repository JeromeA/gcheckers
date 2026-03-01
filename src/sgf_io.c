#include "sgf_io.h"

#include "game.h"

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

static GQuark sgf_io_error_quark(void) {
  return g_quark_from_static_string("sgf-io-error");
}

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

static gboolean sgf_io_parse_node_properties(SgfIoParser *p, GHashTable *props, GError **error) {
  g_return_val_if_fail(p != NULL, FALSE);
  g_return_val_if_fail(props != NULL, FALSE);

  while (TRUE) {
    char next = '\0';
    if (!sgf_io_peek_char(p, &next) || !sgf_io_is_upper_alpha(next)) {
      return TRUE;
    }

    g_autofree char *ident = sgf_io_parse_prop_ident(p, error);
    if (ident == NULL) {
      return FALSE;
    }

    g_autofree char *first_value = sgf_io_parse_prop_value(p, error);
    if (first_value == NULL) {
      return FALSE;
    }
    g_hash_table_insert(props, g_strdup(ident), g_strdup(first_value));

    while (TRUE) {
      char maybe_more = '\0';
      if (!sgf_io_peek_char(p, &maybe_more) || maybe_more != '[') {
        break;
      }
      g_autofree char *ignored = sgf_io_parse_prop_value(p, error);
      if (ignored == NULL) {
        return FALSE;
      }
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

static gboolean sgf_io_parse_move_notation(const char *notation, CheckersMove *out_move, GError **error) {
  g_return_val_if_fail(notation != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  gsize len = strlen(notation);
  if (len == 0) {
    g_set_error_literal(error, sgf_io_error_quark(), 4, "Empty SGF move value");
    return FALSE;
  }

  CheckersMove move = {0};
  gboolean captures = FALSE;
  char separator = '\0';
  gsize pos = 0;

  while (pos < len) {
    gsize start = pos;
    while (pos < len && g_ascii_isdigit(notation[pos])) {
      pos++;
    }
    if (start == pos) {
      g_set_error(error, sgf_io_error_quark(), 5, "Invalid SGF move token near: %s", notation + pos);
      return FALSE;
    }

    g_autofree char *square_text = g_strndup(notation + start, pos - start);
    char *end_ptr = NULL;
    guint64 square_1based = g_ascii_strtoull(square_text, &end_ptr, 10);
    if (end_ptr == square_text || (end_ptr != NULL && *end_ptr != '\0') || square_1based == 0 ||
        square_1based > CHECKERS_MAX_SQUARES) {
      g_set_error(error, sgf_io_error_quark(), 6, "Invalid SGF move square: %.*s", (int)(pos - start),
                  notation + start);
      return FALSE;
    }
    if (move.length >= CHECKERS_MAX_MOVE_LENGTH) {
      g_set_error(error, sgf_io_error_quark(), 7, "SGF move exceeds max path length");
      return FALSE;
    }
    move.path[move.length++] = (uint8_t)(square_1based - 1);

    if (pos >= len) {
      break;
    }
    if (notation[pos] != '-' && notation[pos] != 'x') {
      g_set_error(error, sgf_io_error_quark(), 8, "Invalid SGF move separator: %c", notation[pos]);
      return FALSE;
    }
    if (separator == '\0') {
      separator = notation[pos];
      captures = separator == 'x';
    } else if (separator != notation[pos]) {
      g_set_error_literal(error, sgf_io_error_quark(), 9, "Mixed SGF move separators are not supported");
      return FALSE;
    }
    pos++;
  }

  if (move.length < 2) {
    g_set_error_literal(error, sgf_io_error_quark(), 10, "SGF move needs at least 2 squares");
    return FALSE;
  }

  move.captures = captures ? (uint8_t)(move.length - 1) : 0;
  *out_move = move;
  return TRUE;
}

static gboolean sgf_io_move_notation_from_node(const SgfNode *node, char *buffer, size_t size, GError **error) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(size > 0, FALSE);

  g_autoptr(GBytes) payload = sgf_node_get_payload(node);
  if (payload == NULL) {
    g_set_error_literal(error, sgf_io_error_quark(), 11, "Missing move payload for SGF node");
    return FALSE;
  }

  gsize payload_size = 0;
  const void *payload_data = g_bytes_get_data(payload, &payload_size);
  if (payload_data == NULL || payload_size != sizeof(CheckersMove)) {
    g_set_error(error, sgf_io_error_quark(), 12, "Unexpected SGF node payload size: %zu", payload_size);
    return FALSE;
  }

  CheckersMove move = {0};
  memcpy(&move, payload_data, sizeof(move));
  if (!game_format_move_notation(&move, buffer, size)) {
    g_set_error_literal(error, sgf_io_error_quark(), 13, "Unable to format SGF move notation");
    return FALSE;
  }

  return TRUE;
}

static gboolean sgf_io_append_node(SgfIoWriter *writer,
                                   const SgfNode *node,
                                   gboolean is_root,
                                   GError **error) {
  g_return_val_if_fail(writer != NULL, FALSE);
  g_return_val_if_fail(writer->text != NULL, FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  g_string_append_c(writer->text, ';');
  if (is_root) {
    g_string_append(writer->text, "FF[4]CA[UTF-8]AP[gcheckers]GM[40]");
  } else {
    char move_text[128] = {0};
    if (!sgf_io_move_notation_from_node(node, move_text, sizeof(move_text), error)) {
      return FALSE;
    }

    g_autofree char *escaped_move = sgf_io_escape_prop_value(move_text);
    if (escaped_move == NULL) {
      g_set_error_literal(error, sgf_io_error_quark(), 14, "Unable to escape SGF move text");
      return FALSE;
    }

    SgfColor color = sgf_node_get_color(node);
    if (color == SGF_COLOR_BLACK) {
      g_string_append_printf(writer->text, "B[%s]", escaped_move);
    } else if (color == SGF_COLOR_WHITE) {
      g_string_append_printf(writer->text, "W[%s]", escaped_move);
    } else {
      g_set_error_literal(error, sgf_io_error_quark(), 15, "Only black/white nodes can be serialized as SGF moves");
      return FALSE;
    }
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

  const SgfNode *root = sgf_tree_get_root(tree);
  if (root == NULL) {
    g_set_error_literal(error, sgf_io_error_quark(), 16, "Missing SGF root node");
    return NULL;
  }

  SgfIoWriter writer = {
    .text = g_string_new(NULL),
  };

  g_string_append_c(writer.text, '(');
  if (!sgf_io_append_node(&writer, root, TRUE, error)) {
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

      g_autoptr(GHashTable) props = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
      if (!sgf_io_parse_node_properties(p, props, error)) {
        return FALSE;
      }

      if (!(*seen_root)) {
        *seen_root = TRUE;
        cursor = sgf_tree_get_root(tree);
        continue;
      }

      const char *black_move = g_hash_table_lookup(props, "B");
      const char *white_move = g_hash_table_lookup(props, "W");
      if ((black_move == NULL) == (white_move == NULL)) {
        g_set_error_literal(error, sgf_io_error_quark(), 18, "SGF move node must contain exactly one of B[] or W[]");
        return FALSE;
      }

      CheckersMove move = {0};
      if (!sgf_io_parse_move_notation(black_move != NULL ? black_move : white_move, &move, error)) {
        return FALSE;
      }

      if (!sgf_tree_set_current(tree, cursor)) {
        g_set_error_literal(error, sgf_io_error_quark(), 19, "Unable to select SGF parent node");
        return FALSE;
      }

      g_autoptr(GBytes) payload = g_bytes_new(&move, sizeof(move));
      SgfColor color = black_move != NULL ? SGF_COLOR_BLACK : SGF_COLOR_WHITE;
      const SgfNode *node = sgf_tree_append_move(tree, color, payload);
      if (node == NULL) {
        g_set_error_literal(error, sgf_io_error_quark(), 20, "Unable to append SGF move node");
        return FALSE;
      }

      cursor = node;
      continue;
    }

    if (next == '(') {
      if (!has_sequence_node) {
        g_set_error_literal(error, sgf_io_error_quark(), 21, "SGF variation without node sequence");
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

    g_set_error(error, sgf_io_error_quark(), 22, "Unexpected SGF token '%c' at offset %zu", next, p->pos);
    return FALSE;
  }
}

gboolean sgf_io_load_data(const char *content, SgfTree **out_tree, GError **error) {
  g_return_val_if_fail(content != NULL, FALSE);
  g_return_val_if_fail(out_tree != NULL, FALSE);

  g_autoptr(SgfTree) tree = sgf_tree_new();
  const SgfNode *root = sgf_tree_get_root(tree);
  if (root == NULL) {
    g_set_error_literal(error, sgf_io_error_quark(), 23, "Unable to initialize SGF tree");
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
    g_set_error_literal(error, sgf_io_error_quark(), 24, "Missing SGF root node");
    return FALSE;
  }

  if (!sgf_tree_set_current(tree, root)) {
    g_set_error_literal(error, sgf_io_error_quark(), 25, "Unable to select SGF root after load");
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
    g_set_error(error, sgf_io_error_quark(), 26, "Empty SGF file: %s", path);
    return FALSE;
  }

  return sgf_io_load_data(content, out_tree, error);
}
