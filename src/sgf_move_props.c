#include "sgf_move_props.h"

#include "games/checkers/game.h"

#include <string.h>

static GQuark sgf_move_props_error_quark(void) {
  return g_quark_from_static_string("sgf-move-props-error");
}

gboolean sgf_move_props_parse_notation(const char *notation, gpointer out_move, GError **error) {
  g_return_val_if_fail(notation != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  gsize len = strlen(notation);
  if (len == 0) {
    g_set_error_literal(error, sgf_move_props_error_quark(), 1, "Empty SGF move value");
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
      g_set_error(error, sgf_move_props_error_quark(), 2, "Invalid SGF move token near: %s", notation + pos);
      return FALSE;
    }

    g_autofree char *square_text = g_strndup(notation + start, pos - start);
    char *end_ptr = NULL;
    guint64 square_1based = g_ascii_strtoull(square_text, &end_ptr, 10);
    if (end_ptr == square_text || (end_ptr != NULL && *end_ptr != '\0') || square_1based == 0 ||
        square_1based > CHECKERS_MAX_SQUARES) {
      g_set_error(error, sgf_move_props_error_quark(), 3, "Invalid SGF move square: %.*s", (int)(pos - start),
                  notation + start);
      return FALSE;
    }

    if (move.length >= CHECKERS_MAX_MOVE_LENGTH) {
      g_set_error_literal(error, sgf_move_props_error_quark(), 4, "SGF move exceeds max path length");
      return FALSE;
    }

    move.path[move.length++] = (uint8_t)(square_1based - 1);

    if (pos >= len) {
      break;
    }

    if (notation[pos] != '-' && notation[pos] != 'x') {
      g_set_error(error, sgf_move_props_error_quark(), 5, "Invalid SGF move separator: %c", notation[pos]);
      return FALSE;
    }

    if (separator == '\0') {
      separator = notation[pos];
      captures = separator == 'x';
    } else if (separator != notation[pos]) {
      g_set_error_literal(error, sgf_move_props_error_quark(), 6, "Mixed SGF move separators are not supported");
      return FALSE;
    }
    pos++;
  }

  if (move.length < 2) {
    g_set_error_literal(error, sgf_move_props_error_quark(), 7, "SGF move needs at least 2 squares");
    return FALSE;
  }

  move.captures = captures ? (uint8_t)(move.length - 1) : 0;
  *(CheckersMove *) out_move = move;
  return TRUE;
}

gboolean sgf_move_props_format_notation(gconstpointer move, char *buffer, size_t size, GError **error) {
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(size > 0, FALSE);

  if (!game_format_move_notation(move, buffer, size)) {
    g_set_error_literal(error, sgf_move_props_error_quark(), 8, "Unable to format SGF move notation");
    return FALSE;
  }

  return TRUE;
}

gboolean sgf_move_props_parse_node(const SgfNode *node, SgfColor *out_color, gpointer out_move, GError **error) {
  gboolean has_move = FALSE;
  if (!sgf_move_props_try_parse_node(node, out_color, out_move, &has_move, error)) {
    return FALSE;
  }

  if (!has_move) {
    g_set_error_literal(error,
                        sgf_move_props_error_quark(),
                        9,
                        "SGF move node must contain exactly one of B[] or W[]");
    return FALSE;
  }

  return TRUE;
}

gboolean sgf_move_props_try_parse_node(const SgfNode *node,
                                       SgfColor *out_color,
                                       gpointer out_move,
                                       gboolean *out_has_move,
                                       GError **error) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(out_color != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);
  g_return_val_if_fail(out_has_move != NULL, FALSE);

  const char *black_move = sgf_node_get_property_first(node, "B");
  const char *white_move = sgf_node_get_property_first(node, "W");

  if (black_move != NULL && white_move != NULL) {
    g_set_error_literal(error,
                        sgf_move_props_error_quark(),
                        9,
                        "SGF move node must contain exactly one of B[] or W[]");
    return FALSE;
  }
  if (black_move == NULL && white_move == NULL) {
    *out_has_move = FALSE;
    *out_color = SGF_COLOR_NONE;
    *(CheckersMove *) out_move = (CheckersMove){0};
    return TRUE;
  }

  const char *notation = black_move != NULL ? black_move : white_move;
  SgfColor color = black_move != NULL ? SGF_COLOR_BLACK : SGF_COLOR_WHITE;
  CheckersMove move = {0};
  if (!sgf_move_props_parse_notation(notation, &move, error)) {
    return FALSE;
  }

  *out_color = color;
  *(CheckersMove *) out_move = move;
  *out_has_move = TRUE;
  return TRUE;
}

gboolean sgf_move_props_set_move(SgfNode *node, SgfColor color, gconstpointer move, GError **error) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(color == SGF_COLOR_BLACK || color == SGF_COLOR_WHITE, FALSE);

  char notation[128] = {0};
  if (!sgf_move_props_format_notation(move, notation, sizeof(notation), error)) {
    return FALSE;
  }

  const char *ident = color == SGF_COLOR_BLACK ? "B" : "W";
  const char *other_ident = color == SGF_COLOR_BLACK ? "W" : "B";
  sgf_node_clear_property(node, ident);
  sgf_node_clear_property(node, other_ident);
  if (!sgf_node_add_property(node, ident, notation)) {
    g_set_error_literal(error, sgf_move_props_error_quark(), 10, "Failed to assign SGF move property");
    return FALSE;
  }

  return TRUE;
}
