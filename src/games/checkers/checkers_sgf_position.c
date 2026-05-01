#include "checkers_sgf_position.h"

#include "board.h"
#include "game.h"

#include <string.h>

static GQuark checkers_sgf_position_error_quark(void) {
  return g_quark_from_static_string("checkers-sgf-position-error");
}

static gboolean checkers_sgf_position_parse_setup_point(const char *value,
                                                        guint8 board_size,
                                                        guint8 *out_square,
                                                        GError **error) {
  g_return_val_if_fail(value != NULL, FALSE);
  g_return_val_if_fail(board_size > 0, FALSE);
  g_return_val_if_fail(out_square != NULL, FALSE);

  gsize len = strlen(value);
  if (len == 2 && g_ascii_isalpha(value[0]) && g_ascii_isalpha(value[1])) {
    gint col = g_ascii_tolower(value[0]) - 'a';
    gint row = g_ascii_tolower(value[1]) - 'a';
    if (row < 0 || col < 0 || row >= board_size || col >= board_size) {
      g_set_error(error,
                  checkers_sgf_position_error_quark(),
                  2,
                  "Out-of-range SGF setup point: %s",
                  value);
      return FALSE;
    }

    gint8 square = board_index_from_coord(row, col, board_size);
    if (square < 0) {
      g_set_error(error,
                  checkers_sgf_position_error_quark(),
                  3,
                  "Non-playable SGF setup point: %s",
                  value);
      return FALSE;
    }

    *out_square = (guint8)square;
    return TRUE;
  }

  char *end_ptr = NULL;
  guint64 square_1based = g_ascii_strtoull(value, &end_ptr, 10);
  if (end_ptr == value || (end_ptr != NULL && *end_ptr != '\0')) {
    g_set_error(error,
                checkers_sgf_position_error_quark(),
                4,
                "Invalid SGF setup point: %s",
                value);
    return FALSE;
  }

  guint8 max_square = board_playable_squares(board_size);
  if (square_1based == 0 || square_1based > max_square) {
    g_set_error(error,
                checkers_sgf_position_error_quark(),
                5,
                "Out-of-range SGF setup square: %s",
                value);
    return FALSE;
  }

  *out_square = (guint8)(square_1based - 1);
  return TRUE;
}

typedef gboolean (*CheckersSgfSetupSquareFunc)(GameState *state, guint8 square, gpointer user_data, GError **error);

static gboolean checkers_sgf_position_for_each_setup_square(GameState *state,
                                                            const char *value,
                                                            CheckersSgfSetupSquareFunc func,
                                                            gpointer user_data,
                                                            GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(value != NULL, FALSE);
  g_return_val_if_fail(func != NULL, FALSE);

  const char *range_sep = strchr(value, ':');
  if (range_sep == NULL) {
    guint8 square = 0;
    if (!checkers_sgf_position_parse_setup_point(value, state->board.board_size, &square, error)) {
      return FALSE;
    }

    return func(state, square, user_data, error);
  }

  g_autofree char *start_text = g_strndup(value, (gsize)(range_sep - value));
  g_autofree char *end_text = g_strdup(range_sep + 1);
  guint8 start_square = 0;
  guint8 end_square = 0;
  if (!checkers_sgf_position_parse_setup_point(start_text, state->board.board_size, &start_square, error) ||
      !checkers_sgf_position_parse_setup_point(end_text, state->board.board_size, &end_square, error)) {
    return FALSE;
  }

  gint start_row = 0;
  gint start_col = 0;
  gint end_row = 0;
  gint end_col = 0;
  board_coord_from_index(start_square, &start_row, &start_col, state->board.board_size);
  board_coord_from_index(end_square, &end_row, &end_col, state->board.board_size);
  gint row_min = MIN(start_row, end_row);
  gint row_max = MAX(start_row, end_row);
  gint col_min = MIN(start_col, end_col);
  gint col_max = MAX(start_col, end_col);
  for (gint row = row_min; row <= row_max; ++row) {
    for (gint col = col_min; col <= col_max; ++col) {
      gint8 square = board_index_from_coord(row, col, state->board.board_size);
      if (square < 0) {
        continue;
      }

      if (!func(state, (guint8)square, user_data, error)) {
        return FALSE;
      }
    }
  }

  return TRUE;
}

static gboolean checkers_sgf_position_set_square_piece(GameState *state,
                                                       guint8 square,
                                                       gpointer user_data,
                                                       GError ** /*error*/) {
  CheckersPiece piece = GPOINTER_TO_INT(user_data);

  g_return_val_if_fail(state != NULL, FALSE);

  board_set(&state->board, square, piece);
  return TRUE;
}

static gboolean checkers_sgf_position_apply_setup_values(GameState *state,
                                                         const GPtrArray *values,
                                                         CheckersPiece piece,
                                                         GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);

  if (values == NULL) {
    return TRUE;
  }

  for (guint i = 0; i < values->len; ++i) {
    const char *value = g_ptr_array_index((GPtrArray *)values, i);
    g_return_val_if_fail(value != NULL, FALSE);

    if (!checkers_sgf_position_for_each_setup_square(state,
                                                     value,
                                                     checkers_sgf_position_set_square_piece,
                                                     GINT_TO_POINTER(piece),
                                                     error)) {
      return FALSE;
    }
  }

  return TRUE;
}

typedef struct {
  CheckersColor color;
  CheckersPiece king_piece;
  const char *ident;
} CheckersSgfKingSetupContext;

static gboolean checkers_sgf_position_set_square_king(GameState *state,
                                                      guint8 square,
                                                      gpointer user_data,
                                                      GError **error) {
  CheckersSgfKingSetupContext *context = user_data;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);

  CheckersPiece piece = board_get(&state->board, square);
  if (piece == CHECKERS_PIECE_EMPTY || board_piece_color(piece) != context->color) {
    g_set_error(error,
                checkers_sgf_position_error_quark(),
                7,
                "SGF %s square must also be present in %s",
                context->ident,
                context->color == CHECKERS_COLOR_BLACK ? "AB" : "AW");
    return FALSE;
  }

  board_set(&state->board, square, context->king_piece);
  return TRUE;
}

static gboolean checkers_sgf_position_apply_king_setup_values(GameState *state,
                                                              const GPtrArray *values,
                                                              CheckersColor color,
                                                              CheckersPiece king_piece,
                                                              const char *ident,
                                                              GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(ident != NULL, FALSE);

  if (values == NULL) {
    return TRUE;
  }

  CheckersSgfKingSetupContext context = {
    .color = color,
    .king_piece = king_piece,
    .ident = ident,
  };

  for (guint i = 0; i < values->len; ++i) {
    const char *value = g_ptr_array_index((GPtrArray *)values, i);
    g_return_val_if_fail(value != NULL, FALSE);

    if (!checkers_sgf_position_for_each_setup_square(state,
                                                     value,
                                                     checkers_sgf_position_set_square_king,
                                                     &context,
                                                     error)) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean checkers_sgf_position_format_setup_point(guint8 index, guint8 board_size, char out_point[3]) {
  g_return_val_if_fail(out_point != NULL, FALSE);

  gint row = 0;
  gint col = 0;
  board_coord_from_index(index, &row, &col, board_size);
  if (row < 0 || col < 0 || row >= 26 || col >= 26) {
    g_debug("Unsupported SGF setup coordinate for board size %u", board_size);
    return FALSE;
  }

  out_point[0] = (char)('a' + col);
  out_point[1] = (char)('a' + row);
  out_point[2] = '\0';
  return TRUE;
}

static gboolean checkers_sgf_position_add_property_value(SgfNode *node,
                                                         const char *ident,
                                                         const char *value,
                                                         GError **error) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(ident != NULL, FALSE);
  g_return_val_if_fail(value != NULL, FALSE);

  if (!sgf_node_add_property(node, ident, value)) {
    g_set_error(error,
                checkers_sgf_position_error_quark(),
                8,
                "Failed to write SGF %s property",
                ident);
    return FALSE;
  }

  return TRUE;
}

gboolean checkers_sgf_position_apply_setup_node(gpointer position, const SgfNode *node, GError **error) {
  Game *game = position;

  g_return_val_if_fail(game != NULL, FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  GameState *state = &game->state;
  const GPtrArray *ae_values = sgf_node_get_property_values(node, "AE");
  const GPtrArray *ab_values = sgf_node_get_property_values(node, "AB");
  const GPtrArray *aw_values = sgf_node_get_property_values(node, "AW");
  const GPtrArray *abk_values = sgf_node_get_property_values(node, "ABK");
  const GPtrArray *awk_values = sgf_node_get_property_values(node, "AWK");
  const char *pl = sgf_node_get_property_first(node, "PL");

  if (!checkers_sgf_position_apply_setup_values(state, ae_values, CHECKERS_PIECE_EMPTY, error) ||
      !checkers_sgf_position_apply_setup_values(state, ab_values, CHECKERS_PIECE_BLACK_MAN, error) ||
      !checkers_sgf_position_apply_setup_values(state, aw_values, CHECKERS_PIECE_WHITE_MAN, error) ||
      !checkers_sgf_position_apply_king_setup_values(state,
                                                     abk_values,
                                                     CHECKERS_COLOR_BLACK,
                                                     CHECKERS_PIECE_BLACK_KING,
                                                     "ABK",
                                                     error) ||
      !checkers_sgf_position_apply_king_setup_values(state,
                                                     awk_values,
                                                     CHECKERS_COLOR_WHITE,
                                                     CHECKERS_PIECE_WHITE_KING,
                                                     "AWK",
                                                     error)) {
    return FALSE;
  }

  if (pl != NULL) {
    if (g_strcmp0(pl, "B") == 0) {
      state->turn = CHECKERS_COLOR_BLACK;
    } else if (g_strcmp0(pl, "W") == 0) {
      state->turn = CHECKERS_COLOR_WHITE;
    } else {
      g_set_error(error,
                  checkers_sgf_position_error_quark(),
                  6,
                  "Invalid SGF PL value: %s",
                  pl);
      return FALSE;
    }
  }

  if (ae_values != NULL || ab_values != NULL || aw_values != NULL || abk_values != NULL || awk_values != NULL ||
      pl != NULL) {
    state->winner = CHECKERS_WINNER_NONE;
  }

  return TRUE;
}

gboolean checkers_sgf_position_write_position_node(gconstpointer position, SgfNode *node, GError **error) {
  const Game *game = position;

  g_return_val_if_fail(game != NULL, FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  sgf_node_clear_property(node, "AE");
  sgf_node_clear_property(node, "AB");
  sgf_node_clear_property(node, "AW");
  sgf_node_clear_property(node, "ABK");
  sgf_node_clear_property(node, "AWK");
  sgf_node_clear_property(node, "PL");

  const GameState *state = &game->state;
  guint8 squares = board_playable_squares(state->board.board_size);
  for (guint8 i = 0; i < squares; ++i) {
    char point[3] = {0};
    if (!checkers_sgf_position_format_setup_point(i, state->board.board_size, point)) {
      g_set_error_literal(error,
                          checkers_sgf_position_error_quark(),
                          4,
                          "Unable to encode SGF setup point");
      return FALSE;
    }

    CheckersPiece piece = board_get(&state->board, i);
    switch (piece) {
      case CHECKERS_PIECE_EMPTY:
        if (!checkers_sgf_position_add_property_value(node, "AE", point, error)) {
          return FALSE;
        }
        break;
      case CHECKERS_PIECE_BLACK_MAN:
        if (!checkers_sgf_position_add_property_value(node, "AB", point, error)) {
          return FALSE;
        }
        break;
      case CHECKERS_PIECE_WHITE_MAN:
        if (!checkers_sgf_position_add_property_value(node, "AW", point, error)) {
          return FALSE;
        }
        break;
      case CHECKERS_PIECE_BLACK_KING:
        if (!checkers_sgf_position_add_property_value(node, "AB", point, error) ||
            !checkers_sgf_position_add_property_value(node, "ABK", point, error)) {
          return FALSE;
        }
        break;
      case CHECKERS_PIECE_WHITE_KING:
        if (!checkers_sgf_position_add_property_value(node, "AW", point, error) ||
            !checkers_sgf_position_add_property_value(node, "AWK", point, error)) {
          return FALSE;
        }
        break;
      default:
        g_debug("Skipping unknown piece while writing checkers SGF position");
        break;
    }
  }

  const char *pl = state->turn == CHECKERS_COLOR_BLACK ? "B" : "W";
  return checkers_sgf_position_add_property_value(node, "PL", pl, error);
}
