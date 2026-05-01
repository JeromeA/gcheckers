#include "boop_sgf_position.h"

#include "boop_game.h"

#include <string.h>

typedef struct {
  const char *ident;
  guint side;
  BoopPieceRank rank;
} BoopSgfBoardProp;

static const BoopSgfBoardProp boop_sgf_board_props[] = {
  {.ident = "GBK", .side = 0, .rank = BOOP_PIECE_RANK_KITTEN},
  {.ident = "GBC", .side = 0, .rank = BOOP_PIECE_RANK_CAT},
  {.ident = "GWK", .side = 1, .rank = BOOP_PIECE_RANK_KITTEN},
  {.ident = "GWC", .side = 1, .rank = BOOP_PIECE_RANK_CAT},
};

static GQuark boop_sgf_position_error_quark(void) {
  return g_quark_from_static_string("boop-sgf-position-error");
}

static gboolean boop_sgf_position_square_to_point(guint square, char out_point[3]) {
  guint row = 0;
  guint col = 0;

  g_return_val_if_fail(out_point != NULL, FALSE);

  if (!boop_square_to_coord(square, &row, &col)) {
    return FALSE;
  }
  if (row >= 26 || col >= 26) {
    g_debug("Unsupported boop SGF coordinate for square %u", square);
    return FALSE;
  }

  out_point[0] = (char)('a' + col);
  out_point[1] = (char)('a' + row);
  out_point[2] = '\0';
  return TRUE;
}

static gboolean boop_sgf_position_parse_point(const char *value, guint *out_square, GError **error) {
  gint row = 0;
  gint col = 0;

  g_return_val_if_fail(value != NULL, FALSE);
  g_return_val_if_fail(out_square != NULL, FALSE);

  if (strlen(value) != 2 || !g_ascii_isalpha(value[0]) || !g_ascii_isalpha(value[1])) {
    g_set_error(error, boop_sgf_position_error_quark(), 1, "Invalid boop SGF point: %s", value);
    return FALSE;
  }

  col = g_ascii_tolower(value[0]) - 'a';
  row = g_ascii_tolower(value[1]) - 'a';
  if (col < 0 || row < 0) {
    g_set_error(error, boop_sgf_position_error_quark(), 2, "Invalid boop SGF point: %s", value);
    return FALSE;
  }
  if (!boop_coord_to_square((guint)row, (guint)col, out_square)) {
    g_set_error(error, boop_sgf_position_error_quark(), 3, "Out-of-range boop SGF point: %s", value);
    return FALSE;
  }

  return TRUE;
}

static gboolean boop_sgf_position_parse_uint_prop(const SgfNode *node,
                                                  const char *ident,
                                                  guint8 *out_value,
                                                  GError **error) {
  const char *text = NULL;
  char *end_ptr = NULL;
  guint64 value = 0;

  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(ident != NULL, FALSE);
  g_return_val_if_fail(out_value != NULL, FALSE);

  text = sgf_node_get_property_first(node, ident);
  if (text == NULL) {
    g_set_error(error, boop_sgf_position_error_quark(), 4, "Missing boop SGF property %s", ident);
    return FALSE;
  }

  value = g_ascii_strtoull(text, &end_ptr, 10);
  if (end_ptr == text || (end_ptr != NULL && *end_ptr != '\0') || value > BOOP_SUPPLY_COUNT) {
    g_set_error(error, boop_sgf_position_error_quark(), 5, "Invalid boop SGF integer for %s: %s", ident, text);
    return FALSE;
  }

  *out_value = (guint8)value;
  return TRUE;
}

static gboolean boop_sgf_position_add_property_value(SgfNode *node,
                                                     const char *ident,
                                                     const char *value,
                                                     GError **error) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(ident != NULL, FALSE);
  g_return_val_if_fail(value != NULL, FALSE);

  if (!sgf_node_add_property(node, ident, value)) {
    g_set_error(error, boop_sgf_position_error_quark(), 6, "Failed to write boop SGF %s property", ident);
    return FALSE;
  }

  return TRUE;
}

static gboolean boop_sgf_position_apply_board_values(BoopPosition *position,
                                                     const SgfNode *node,
                                                     const BoopSgfBoardProp *prop,
                                                     gboolean *out_any_snapshot_prop,
                                                     GError **error) {
  const GPtrArray *values = NULL;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(prop != NULL, FALSE);
  g_return_val_if_fail(out_any_snapshot_prop != NULL, FALSE);

  values = sgf_node_get_property_values(node, prop->ident);
  if (values == NULL) {
    return TRUE;
  }

  *out_any_snapshot_prop = TRUE;
  for (guint i = 0; i < values->len; ++i) {
    const char *value = g_ptr_array_index((GPtrArray *)values, i);
    guint square = 0;

    g_return_val_if_fail(value != NULL, FALSE);

    if (!boop_sgf_position_parse_point(value, &square, error)) {
      return FALSE;
    }
    if (position->board[square].rank != BOOP_PIECE_RANK_NONE) {
      g_set_error(error,
                  boop_sgf_position_error_quark(),
                  7,
                  "Duplicate boop SGF square assignment for %s",
                  value);
      return FALSE;
    }

    position->board[square] = (BoopPiece){
      .side = (guint8)prop->side,
      .rank = (guint8)prop->rank,
    };
  }

  return TRUE;
}

static gboolean boop_sgf_position_apply_turn(BoopPosition *position, const SgfNode *node, GError **error) {
  const char *pl = NULL;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  pl = sgf_node_get_property_first(node, "PL");
  if (pl == NULL) {
    return TRUE;
  }
  if (g_strcmp0(pl, "B") == 0) {
    position->turn = 0;
    return TRUE;
  }
  if (g_strcmp0(pl, "W") == 0) {
    position->turn = 1;
    return TRUE;
  }

  g_set_error(error, boop_sgf_position_error_quark(), 8, "Invalid boop SGF PL value: %s", pl);
  return FALSE;
}

gboolean boop_sgf_position_apply_setup_node(gpointer position, const SgfNode *node, GError **error) {
  BoopPosition *boop_position = position;
  gboolean any_snapshot_prop = FALSE;

  g_return_val_if_fail(boop_position != NULL, FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  for (guint i = 0; i < G_N_ELEMENTS(boop_sgf_board_props); ++i) {
    if (sgf_node_get_property_values(node, boop_sgf_board_props[i].ident) != NULL) {
      any_snapshot_prop = TRUE;
    }
  }

  if (sgf_node_get_property_first(node, "GBKS") != NULL ||
      sgf_node_get_property_first(node, "GBCS") != NULL ||
      sgf_node_get_property_first(node, "GWKS") != NULL ||
      sgf_node_get_property_first(node, "GWCS") != NULL) {
    any_snapshot_prop = TRUE;
  }

  if (any_snapshot_prop) {
    boop_position_init(boop_position);
    memset(boop_position->board, 0, sizeof(boop_position->board));

    for (guint i = 0; i < G_N_ELEMENTS(boop_sgf_board_props); ++i) {
      if (!boop_sgf_position_apply_board_values(boop_position,
                                                node,
                                                &boop_sgf_board_props[i],
                                                &any_snapshot_prop,
                                                error)) {
        return FALSE;
      }
    }
    if (!boop_sgf_position_parse_uint_prop(node, "GBKS", &boop_position->kittens_in_supply[0], error) ||
        !boop_sgf_position_parse_uint_prop(node, "GBCS", &boop_position->cats_in_supply[0], error) ||
        !boop_sgf_position_parse_uint_prop(node, "GWKS", &boop_position->kittens_in_supply[1], error) ||
        !boop_sgf_position_parse_uint_prop(node, "GWCS", &boop_position->cats_in_supply[1], error)) {
      return FALSE;
    }
  }

  if (!boop_sgf_position_apply_turn(boop_position, node, error)) {
    return FALSE;
  }

  if (!any_snapshot_prop && sgf_node_get_property_first(node, "PL") == NULL) {
    return TRUE;
  }

  return boop_position_normalize(boop_position, error);
}

gboolean boop_sgf_position_write_position_node(gconstpointer position, SgfNode *node, GError **error) {
  const BoopPosition *boop_position = position;

  g_return_val_if_fail(boop_position != NULL, FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  sgf_node_clear_property(node, "GBK");
  sgf_node_clear_property(node, "GBC");
  sgf_node_clear_property(node, "GWK");
  sgf_node_clear_property(node, "GWC");
  sgf_node_clear_property(node, "GBKS");
  sgf_node_clear_property(node, "GBCS");
  sgf_node_clear_property(node, "GWKS");
  sgf_node_clear_property(node, "GWCS");
  sgf_node_clear_property(node, "PL");

  for (guint square = 0; square < BOOP_SQUARE_COUNT; ++square) {
    BoopPiece piece = boop_position->board[square];
    char point[3] = {0};
    const char *ident = NULL;

    if (piece.rank == BOOP_PIECE_RANK_NONE) {
      continue;
    }
    if (!boop_sgf_position_square_to_point(square, point)) {
      g_set_error(error,
                  boop_sgf_position_error_quark(),
                  9,
                  "Unable to encode boop SGF point for square %u",
                  square);
      return FALSE;
    }

    if (piece.side == 0 && piece.rank == BOOP_PIECE_RANK_KITTEN) {
      ident = "GBK";
    } else if (piece.side == 0 && piece.rank == BOOP_PIECE_RANK_CAT) {
      ident = "GBC";
    } else if (piece.side == 1 && piece.rank == BOOP_PIECE_RANK_KITTEN) {
      ident = "GWK";
    } else if (piece.side == 1 && piece.rank == BOOP_PIECE_RANK_CAT) {
      ident = "GWC";
    } else {
      g_set_error(error,
                  boop_sgf_position_error_quark(),
                  10,
                  "Unable to encode boop piece rank %u for side %u",
                  piece.rank,
                  piece.side);
      return FALSE;
    }

    if (!boop_sgf_position_add_property_value(node, ident, point, error)) {
      return FALSE;
    }
  }

  char supply_value[4] = {0};
  g_snprintf(supply_value, sizeof(supply_value), "%u", boop_position->kittens_in_supply[0]);
  if (!boop_sgf_position_add_property_value(node, "GBKS", supply_value, error)) {
    return FALSE;
  }
  g_snprintf(supply_value, sizeof(supply_value), "%u", boop_position->cats_in_supply[0]);
  if (!boop_sgf_position_add_property_value(node, "GBCS", supply_value, error)) {
    return FALSE;
  }
  g_snprintf(supply_value, sizeof(supply_value), "%u", boop_position->kittens_in_supply[1]);
  if (!boop_sgf_position_add_property_value(node, "GWKS", supply_value, error)) {
    return FALSE;
  }
  g_snprintf(supply_value, sizeof(supply_value), "%u", boop_position->cats_in_supply[1]);
  if (!boop_sgf_position_add_property_value(node, "GWCS", supply_value, error)) {
    return FALSE;
  }

  return boop_sgf_position_add_property_value(node, "PL", boop_position->turn == 0 ? "B" : "W", error);
}
