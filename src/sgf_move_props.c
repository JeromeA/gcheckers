#include "sgf_move_props.h"

#include "active_game_backend.h"

#include <string.h>

static GQuark sgf_move_props_error_quark(void) {
  return g_quark_from_static_string("sgf-move-props-error");
}

gboolean sgf_move_props_parse_notation(const char *notation, gpointer out_move, GError **error) {
  g_return_val_if_fail(notation != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);
  g_return_val_if_fail(GGAME_ACTIVE_GAME_BACKEND != NULL, FALSE);

  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  if (backend->parse_move == NULL) {
    g_set_error(error,
                sgf_move_props_error_quark(),
                1,
                "%s does not support SGF move parsing",
                backend->display_name);
    return FALSE;
  }

  memset(out_move, 0, backend->move_size);
  if (!backend->parse_move(notation, out_move)) {
    g_set_error(error, sgf_move_props_error_quark(), 2, "Invalid SGF move value: %s", notation);
    return FALSE;
  }

  return TRUE;
}

gboolean sgf_move_props_format_notation(gconstpointer move, char *buffer, size_t size, GError **error) {
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(size > 0, FALSE);
  g_return_val_if_fail(GGAME_ACTIVE_GAME_BACKEND != NULL, FALSE);

  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  if (backend->format_move == NULL || !backend->format_move(move, buffer, size)) {
    g_set_error_literal(error, sgf_move_props_error_quark(), 3, "Unable to format SGF move notation");
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
                        4,
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
  g_return_val_if_fail(GGAME_ACTIVE_GAME_BACKEND != NULL, FALSE);

  const char *black_move = sgf_node_get_property_first(node, "B");
  const char *white_move = sgf_node_get_property_first(node, "W");

  if (black_move != NULL && white_move != NULL) {
    g_set_error_literal(error,
                        sgf_move_props_error_quark(),
                        5,
                        "SGF move node must contain exactly one of B[] or W[]");
    return FALSE;
  }
  if (black_move == NULL && white_move == NULL) {
    *out_has_move = FALSE;
    *out_color = SGF_COLOR_NONE;
    memset(out_move, 0, GGAME_ACTIVE_GAME_BACKEND->move_size);
    return TRUE;
  }

  const char *notation = black_move != NULL ? black_move : white_move;
  *out_color = black_move != NULL ? SGF_COLOR_BLACK : SGF_COLOR_WHITE;
  if (!sgf_move_props_parse_notation(notation, out_move, error)) {
    return FALSE;
  }

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
    g_set_error_literal(error, sgf_move_props_error_quark(), 6, "Failed to assign SGF move property");
    return FALSE;
  }

  return TRUE;
}
