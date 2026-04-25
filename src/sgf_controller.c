#include "sgf_controller.h"
#include "active_game_backend.h"
#include "sgf_io.h"
#include "sgf_move_props.h"
#include "games/checkers/rulesets.h"

#include <string.h>

struct _GCheckersSgfController {
  GObject parent_instance;
  BoardView *board_view;
  GCheckersModel *model;
  SgfTree *sgf_tree;
  SgfView *sgf_view;
  gboolean is_replaying;
};

static gboolean gcheckers_sgf_controller_sync_tree_ruleset_from_model(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);

  if (!GCHECKERS_IS_MODEL(self->model) || !SGF_IS_TREE(self->sgf_tree)) {
    return TRUE;
  }

  const CheckersRules *rules = gcheckers_model_peek_rules(self->model);
  if (rules == NULL) {
    g_debug("Missing model rules while syncing SGF RU");
    return FALSE;
  }

  PlayerRuleset ruleset = PLAYER_RULESET_INTERNATIONAL;
  if (!checkers_ruleset_find_by_rules(rules, &ruleset)) {
    g_debug("Unable to map model rules to a known SGF RU value");
    return FALSE;
  }

  const char *short_name = checkers_ruleset_short_name(ruleset);
  const GameBackendVariant *variant =
      short_name != NULL ? GGAME_ACTIVE_GAME_BACKEND->variant_by_short_name(short_name) : NULL;
  if (variant == NULL) {
    g_debug("Unable to map model ruleset to a backend variant");
    return FALSE;
  }

  return sgf_io_tree_set_variant(self->sgf_tree, variant);
}

G_DEFINE_TYPE(GCheckersSgfController, gcheckers_sgf_controller, G_TYPE_OBJECT)

enum {
  SIGNAL_NODE_CHANGED,
  SIGNAL_MANUAL_REQUESTED,
  SIGNAL_LAST
};

static guint controller_signals[SIGNAL_LAST] = {0};

static void gcheckers_sgf_controller_emit_node_changed(GCheckersSgfController *self, const SgfNode *node) {
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self));
  g_return_if_fail(node != NULL);

  g_signal_emit(self, controller_signals[SIGNAL_NODE_CHANGED], 0, node);
}

static gboolean gcheckers_sgf_controller_moves_equal(const CheckersMove *left, const CheckersMove *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  if (left->length != right->length || left->captures != right->captures) {
    return FALSE;
  }

  if (left->length == 0) {
    return TRUE;
  }

  return memcmp(left->path, right->path, left->length * sizeof(left->path[0])) == 0;
}

static SgfColor gcheckers_sgf_controller_color_from_turn(CheckersColor color) {
  switch (color) {
    case CHECKERS_COLOR_BLACK:
      return SGF_COLOR_BLACK;
    case CHECKERS_COLOR_WHITE:
      return SGF_COLOR_WHITE;
    default:
      return SGF_COLOR_NONE;
  }
}

static void gcheckers_sgf_controller_disconnect_model(GCheckersSgfController *self) {
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self));

  g_clear_object(&self->model);
}

static gboolean gcheckers_sgf_controller_parse_setup_point(const char *value,
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
      g_set_error(error, g_quark_from_static_string("gcheckers-sgf-controller-error"), 2,
                  "Out-of-range SGF setup point: %s", value);
      return FALSE;
    }
    gint8 square = board_index_from_coord(row, col, board_size);
    if (square < 0) {
      g_set_error(error, g_quark_from_static_string("gcheckers-sgf-controller-error"), 3,
                  "Non-playable SGF setup point: %s", value);
      return FALSE;
    }
    *out_square = (guint8)square;
    return TRUE;
  }

  char *end_ptr = NULL;
  guint64 square_1based = g_ascii_strtoull(value, &end_ptr, 10);
  if (end_ptr == value || (end_ptr != NULL && *end_ptr != '\0')) {
    g_set_error(error, g_quark_from_static_string("gcheckers-sgf-controller-error"), 4,
                "Invalid SGF setup point: %s", value);
    return FALSE;
  }
  guint8 max_square = board_playable_squares(board_size);
  if (square_1based == 0 || square_1based > max_square) {
    g_set_error(error, g_quark_from_static_string("gcheckers-sgf-controller-error"), 5,
                "Out-of-range SGF setup square: %s", value);
    return FALSE;
  }
  *out_square = (guint8)(square_1based - 1);
  return TRUE;
}

typedef gboolean (*GCheckersSgfSetupSquareFunc)(GameState *state, guint8 square, gpointer user_data, GError **error);

static gboolean gcheckers_sgf_controller_for_each_setup_square(GameState *state,
                                                               const char *value,
                                                               GCheckersSgfSetupSquareFunc func,
                                                               gpointer user_data,
                                                               GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(value != NULL, FALSE);
  g_return_val_if_fail(func != NULL, FALSE);

  const char *range_sep = strchr(value, ':');
  if (range_sep == NULL) {
    guint8 square = 0;
    if (!gcheckers_sgf_controller_parse_setup_point(value, state->board.board_size, &square, error)) {
      return FALSE;
    }
    return func(state, square, user_data, error);
  }

  g_autofree char *start_text = g_strndup(value, (gsize)(range_sep - value));
  g_autofree char *end_text = g_strdup(range_sep + 1);
  guint8 start_square = 0;
  guint8 end_square = 0;
  if (!gcheckers_sgf_controller_parse_setup_point(start_text, state->board.board_size, &start_square, error) ||
      !gcheckers_sgf_controller_parse_setup_point(end_text, state->board.board_size, &end_square, error)) {
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

static gboolean gcheckers_sgf_controller_set_square_piece(GameState *state,
                                                          guint8 square,
                                                          gpointer user_data,
                                                          GError ** /*error*/) {
  CheckersPiece piece = GPOINTER_TO_INT(user_data);
  board_set(&state->board, square, piece);
  return TRUE;
}

static gboolean gcheckers_sgf_controller_apply_setup_values(GameState *state,
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
    if (!gcheckers_sgf_controller_for_each_setup_square(state,
                                                        value,
                                                        gcheckers_sgf_controller_set_square_piece,
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
} GCheckersSgfKingSetupContext;

static gboolean gcheckers_sgf_controller_set_square_king(GameState *state,
                                                         guint8 square,
                                                         gpointer user_data,
                                                         GError **error) {
  GCheckersSgfKingSetupContext *context = user_data;
  g_return_val_if_fail(context != NULL, FALSE);

  CheckersPiece piece = board_get(&state->board, square);
  if (piece == CHECKERS_PIECE_EMPTY || board_piece_color(piece) != context->color) {
    g_set_error(error,
                g_quark_from_static_string("gcheckers-sgf-controller-error"),
                7,
                "SGF %s square must also be present in %s",
                context->ident,
                context->color == CHECKERS_COLOR_BLACK ? "AB" : "AW");
    return FALSE;
  }

  board_set(&state->board, square, context->king_piece);
  return TRUE;
}

static gboolean gcheckers_sgf_controller_apply_king_setup_values(GameState *state,
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

  GCheckersSgfKingSetupContext context = {
    .color = color,
    .king_piece = king_piece,
    .ident = ident,
  };
  for (guint i = 0; i < values->len; ++i) {
    const char *value = g_ptr_array_index((GPtrArray *)values, i);
    g_return_val_if_fail(value != NULL, FALSE);
    if (!gcheckers_sgf_controller_for_each_setup_square(state,
                                                        value,
                                                        gcheckers_sgf_controller_set_square_king,
                                                        &context,
                                                        error)) {
      return FALSE;
    }
  }
  return TRUE;
}

static gboolean gcheckers_sgf_controller_apply_setup_node(GameState *state, const SgfNode *node, GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  const GPtrArray *ae_values = sgf_node_get_property_values(node, "AE");
  const GPtrArray *ab_values = sgf_node_get_property_values(node, "AB");
  const GPtrArray *aw_values = sgf_node_get_property_values(node, "AW");
  const GPtrArray *abk_values = sgf_node_get_property_values(node, "ABK");
  const GPtrArray *awk_values = sgf_node_get_property_values(node, "AWK");
  const char *pl = sgf_node_get_property_first(node, "PL");

  if (!gcheckers_sgf_controller_apply_setup_values(state, ae_values, CHECKERS_PIECE_EMPTY, error) ||
      !gcheckers_sgf_controller_apply_setup_values(state, ab_values, CHECKERS_PIECE_BLACK_MAN, error) ||
      !gcheckers_sgf_controller_apply_setup_values(state, aw_values, CHECKERS_PIECE_WHITE_MAN, error) ||
      !gcheckers_sgf_controller_apply_king_setup_values(state,
                                                        abk_values,
                                                        CHECKERS_COLOR_BLACK,
                                                        CHECKERS_PIECE_BLACK_KING,
                                                        "ABK",
                                                        error) ||
      !gcheckers_sgf_controller_apply_king_setup_values(state,
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
      g_set_error(error, g_quark_from_static_string("gcheckers-sgf-controller-error"), 6,
                  "Invalid SGF PL value: %s", pl);
      return FALSE;
    }
  }

  if (ae_values != NULL || ab_values != NULL || aw_values != NULL || abk_values != NULL || awk_values != NULL ||
      pl != NULL) {
    state->winner = CHECKERS_WINNER_NONE;
  }

  return TRUE;
}

static gboolean gcheckers_sgf_controller_format_setup_point(uint8_t index,
                                                            uint8_t board_size,
                                                            char out_point[3]) {
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

static char *gcheckers_sgf_controller_escape_prop_value(const char *value) {
  g_return_val_if_fail(value != NULL, NULL);

  GString *escaped = g_string_new(NULL);
  for (const char *cursor = value; *cursor != '\0'; ++cursor) {
    if (*cursor == '\\' || *cursor == ']') {
      g_string_append_c(escaped, '\\');
    }
    g_string_append_c(escaped, *cursor);
  }
  return g_string_free(escaped, FALSE);
}

static gboolean gcheckers_sgf_controller_append_prop_values(GString *content,
                                                            const char *ident,
                                                            GPtrArray *values,
                                                            GError **error) {
  g_return_val_if_fail(content != NULL, FALSE);
  g_return_val_if_fail(ident != NULL, FALSE);
  g_return_val_if_fail(values != NULL, FALSE);

  if (values->len == 0) {
    return TRUE;
  }

  g_string_append(content, ident);
  for (guint i = 0; i < values->len; ++i) {
    const char *value = g_ptr_array_index(values, i);
    g_return_val_if_fail(value != NULL, FALSE);
    g_autofree char *escaped = gcheckers_sgf_controller_escape_prop_value(value);
    if (escaped == NULL) {
      g_set_error_literal(error,
                          g_quark_from_static_string("gcheckers-sgf-controller-error"),
                          8,
                          "Unable to escape SGF property value");
      return FALSE;
    }
    g_string_append_printf(content, "[%s]", escaped);
  }

  return TRUE;
}

static gboolean gcheckers_sgf_controller_extract_node_move(const SgfNode *node,
                                                           CheckersMove *move,
                                                           gboolean *has_move) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(has_move != NULL, FALSE);

  SgfColor color = SGF_COLOR_NONE;
  g_autoptr(GError) error = NULL;
  if (!sgf_move_props_try_parse_node(node, &color, move, has_move, &error)) {
    g_debug("Failed to extract SGF move for node %u: %s",
            sgf_node_get_move_number(node),
            error != NULL ? error->message : "unknown error");
    return FALSE;
  }

  return TRUE;
}

static GPtrArray *gcheckers_sgf_controller_build_node_path(const SgfNode *node) {
  g_return_val_if_fail(node != NULL, NULL);

  GPtrArray *path = g_ptr_array_new();
  const SgfNode *cursor = node;
  while (cursor) {
    g_ptr_array_add(path, (gpointer)cursor);
    cursor = sgf_node_get_parent(cursor);
  }

  for (guint i = 0; i < path->len / 2; ++i) {
    gpointer tmp = g_ptr_array_index(path, i);
    g_ptr_array_index(path, i) = g_ptr_array_index(path, path->len - 1 - i);
    g_ptr_array_index(path, path->len - 1 - i) = tmp;
  }

  return path;
}

gboolean gcheckers_sgf_controller_replay_node_into_game(const SgfNode *node, Game *game, GError **error) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(game != NULL, FALSE);

  g_autoptr(GPtrArray) path = gcheckers_sgf_controller_build_node_path(node);
  if (path == NULL) {
    g_set_error_literal(error,
                        g_quark_from_static_string("gcheckers-sgf-controller-error"),
                        12,
                        "Unable to build SGF node path");
    return FALSE;
  }

  for (guint i = 0; i < path->len; ++i) {
    const SgfNode *step = g_ptr_array_index(path, i);
    g_autoptr(GError) setup_error = NULL;
    if (!gcheckers_sgf_controller_apply_setup_node(&game->state, step, &setup_error)) {
      g_propagate_error(error, g_steal_pointer(&setup_error));
      return FALSE;
    }

    CheckersMove move = {0};
    gboolean has_move = FALSE;
    if (!gcheckers_sgf_controller_extract_node_move(step, &move, &has_move)) {
      g_set_error(error,
                  g_quark_from_static_string("gcheckers-sgf-controller-error"),
                  13,
                  "Unable to extract SGF move for node %u",
                  sgf_node_get_move_number(step));
      return FALSE;
    }
    if (!has_move) {
      continue;
    }

    if (game_apply_move(game, &move) != 0) {
      g_set_error(error,
                  g_quark_from_static_string("gcheckers-sgf-controller-error"),
                  14,
                  "Unable to replay SGF move %u",
                  sgf_node_get_move_number(step));
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean gcheckers_sgf_controller_replay_to_node(GCheckersSgfController *self, const SgfNode *node) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self->model), FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  self->is_replaying = TRUE;
  gcheckers_model_reset(self->model);
  board_view_clear_selection(self->board_view);

  gboolean success = TRUE;
  Game game = {0};
  if (!gcheckers_model_copy_game(self->model, &game)) {
    g_debug("Failed to copy model game for SGF replay");
    self->is_replaying = FALSE;
    return FALSE;
  }

  g_autoptr(GError) replay_error = NULL;
  if (!gcheckers_sgf_controller_replay_node_into_game(node, &game, &replay_error)) {
    g_debug("Failed to replay SGF node %u: %s",
            sgf_node_get_move_number(node),
            replay_error != NULL ? replay_error->message : "unknown error");
    success = FALSE;
  }

  if (success && !gcheckers_model_set_state(self->model, &game.state)) {
    g_debug("Failed to publish replayed SGF state into model");
    success = FALSE;
  }

  game_destroy(&game);
  self->is_replaying = FALSE;
  return success;
}

static gboolean gcheckers_sgf_controller_sync_model_for_transition(GCheckersSgfController *self,
                                                                   const SgfNode *previous,
                                                                   const SgfNode *target) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self->model), FALSE);
  g_return_val_if_fail(target != NULL, FALSE);

  if (previous == target) {
    return TRUE;
  }

  if (previous && sgf_node_get_parent(target) == previous) {
    CheckersMove move = {0};
    gboolean has_move = FALSE;
    if (!gcheckers_sgf_controller_extract_node_move(target, &move, &has_move)) {
      return FALSE;
    }
    if (!has_move) {
      return gcheckers_sgf_controller_replay_to_node(self, target);
    }

    if (!gcheckers_model_apply_move(self->model, &move)) {
      g_debug("Failed to apply SGF transition move %u", sgf_node_get_move_number(target));
      return FALSE;
    }

    return TRUE;
  }

  return gcheckers_sgf_controller_replay_to_node(self, target);
}

static gboolean gcheckers_sgf_controller_move_current(GCheckersSgfController *self, const SgfNode *node) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  const SgfNode *previous = sgf_tree_get_current(self->sgf_tree);
  if (!sgf_tree_set_current(self->sgf_tree, node)) {
    g_debug("Failed to select SGF node");
    return FALSE;
  }

  if (!self->model) {
    if (previous != node) {
      gcheckers_sgf_controller_emit_node_changed(self, node);
    }
    return TRUE;
  }

  gboolean ok = gcheckers_sgf_controller_sync_model_for_transition(self, previous, node);
  if (ok && previous != node) {
    gcheckers_sgf_controller_emit_node_changed(self, node);
  }
  return ok;
}

static gboolean gcheckers_sgf_controller_navigate_to(GCheckersSgfController *self, const SgfNode *node) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  if (!gcheckers_sgf_controller_move_current(self, node)) {
    return FALSE;
  }

  g_signal_emit(self, controller_signals[SIGNAL_MANUAL_REQUESTED], 0, node);
  sgf_view_set_selected(self->sgf_view, node);
  return TRUE;
}

static void gcheckers_sgf_controller_on_node_selected(SgfView * /*view*/,
                                                      const SgfNode *node,
                                                      gpointer user_data) {
  GCheckersSgfController *self = GCHECKERS_SGF_CONTROLLER(user_data);

  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self));
  g_return_if_fail(node != NULL);

  if (!gcheckers_sgf_controller_move_current(self, node)) {
    return;
  }

  g_signal_emit(self, controller_signals[SIGNAL_MANUAL_REQUESTED], 0, node);
  sgf_view_set_selected(self->sgf_view, node);
}

static void gcheckers_sgf_controller_dispose(GObject *object) {
  GCheckersSgfController *self = GCHECKERS_SGF_CONTROLLER(object);

  gcheckers_sgf_controller_disconnect_model(self);
  g_clear_object(&self->board_view);
  g_clear_object(&self->sgf_view);
  g_clear_object(&self->sgf_tree);

  G_OBJECT_CLASS(gcheckers_sgf_controller_parent_class)->dispose(object);
}

static void gcheckers_sgf_controller_class_init(GCheckersSgfControllerClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = gcheckers_sgf_controller_dispose;

  controller_signals[SIGNAL_NODE_CHANGED] = g_signal_new("node-changed",
                                                         G_TYPE_FROM_CLASS(klass),
                                                         G_SIGNAL_RUN_LAST,
                                                         0,
                                                         NULL,
                                                         NULL,
                                                         NULL,
                                                         G_TYPE_NONE,
                                                         1,
                                                         G_TYPE_POINTER);

  controller_signals[SIGNAL_MANUAL_REQUESTED] = g_signal_new("manual-requested",
                                                               G_TYPE_FROM_CLASS(klass),
                                                               G_SIGNAL_RUN_LAST,
                                                               0,
                                                               NULL,
                                                               NULL,
                                                               NULL,
                                                               G_TYPE_NONE,
                                                               1,
                                                               G_TYPE_POINTER);
}

static void gcheckers_sgf_controller_init(GCheckersSgfController *self) {
  self->sgf_tree = sgf_tree_new();
  self->sgf_view = sgf_view_new();
  sgf_view_set_tree(self->sgf_view, self->sgf_tree);
  g_signal_connect(self->sgf_view,
                   "node-selected",
                   G_CALLBACK(gcheckers_sgf_controller_on_node_selected),
                   self);

  self->is_replaying = FALSE;
}

GCheckersSgfController *gcheckers_sgf_controller_new(BoardView *board_view) {
  g_return_val_if_fail(BOARD_IS_VIEW(board_view), NULL);

  GCheckersSgfController *self = g_object_new(GCHECKERS_TYPE_SGF_CONTROLLER, NULL);
  self->board_view = g_object_ref(board_view);
  return self;
}

void gcheckers_sgf_controller_set_model(GCheckersSgfController *self, GCheckersModel *model) {
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(model));

  if (self->model == model) {
    return;
  }

  gcheckers_sgf_controller_disconnect_model(self);
  self->model = g_object_ref(model);
}

void gcheckers_sgf_controller_new_game(GCheckersSgfController *self) {
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self));

  sgf_tree_reset(self->sgf_tree);
  if (!gcheckers_sgf_controller_sync_tree_ruleset_from_model(self)) {
    g_debug("Failed to stamp SGF RU on new game tree");
  }
  sgf_view_set_tree(self->sgf_view, self->sgf_tree);
  board_view_clear_selection(self->board_view);
  self->is_replaying = FALSE;

  const SgfNode *root = sgf_tree_get_root(self->sgf_tree);
  if (root != NULL) {
    gcheckers_sgf_controller_emit_node_changed(self, root);
  }
}

gboolean gcheckers_sgf_controller_apply_move(GCheckersSgfController *self, const CheckersMove *move) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self->model), FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  MoveList moves = gcheckers_model_list_moves(self->model);
  gboolean found = FALSE;
  for (guint i = 0; i < moves.count; ++i) {
    if (gcheckers_sgf_controller_moves_equal(&moves.moves[i], move)) {
      found = TRUE;
      break;
    }
  }

  if (!found) {
    g_debug("Attempted to apply move not present in current model move list");
    movelist_free(&moves);
    return FALSE;
  }

  const GameState *state = gcheckers_model_peek_state(self->model);
  if (!state) {
    g_debug("Failed to fetch game state for SGF append");
    movelist_free(&moves);
    return FALSE;
  }

  const SgfNode *previous = sgf_tree_get_current(self->sgf_tree);
  char notation[128] = {0};
  g_autoptr(GError) error = NULL;
  if (!sgf_move_props_format_notation(move, notation, sizeof(notation), &error)) {
    g_debug("Failed to format SGF move notation: %s", error != NULL ? error->message : "unknown error");
    movelist_free(&moves);
    return FALSE;
  }

  const SgfNode *node = sgf_tree_append_move(self->sgf_tree,
                                             gcheckers_sgf_controller_color_from_turn(state->turn),
                                             notation);
  movelist_free(&moves);

  if (!node) {
    g_debug("Failed to append SGF move");
    return FALSE;
  }

  if (!sgf_tree_set_current(self->sgf_tree, node)) {
    g_debug("Failed to move SGF current to appended node");
    return FALSE;
  }

  if (!gcheckers_sgf_controller_sync_model_for_transition(self, previous, node)) {
    return FALSE;
  }

  gcheckers_sgf_controller_emit_node_changed(self, node);
  sgf_view_refresh(self->sgf_view);
  return TRUE;
}

gboolean gcheckers_sgf_controller_step_ai_move(GCheckersSgfController *self, guint depth, CheckersMove *out_move) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self->model), FALSE);

  CheckersMove move = {0};
  if (!gcheckers_model_choose_best_move(self->model, depth, &move)) {
    g_debug("Failed to choose alpha-beta SGF move from model");
    return FALSE;
  }

  gboolean applied = gcheckers_sgf_controller_apply_move(self, &move);
  if (applied && out_move) {
    *out_move = move;
  }

  return applied;
}

gboolean gcheckers_sgf_controller_rewind_to_start(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);

  const SgfNode *root = sgf_tree_get_root(self->sgf_tree);
  if (!root) {
    g_debug("Missing SGF root node");
    return FALSE;
  }

  if (sgf_tree_get_current(self->sgf_tree) == root) {
    return FALSE;
  }

  return gcheckers_sgf_controller_navigate_to(self, root);
}

gboolean gcheckers_sgf_controller_step_backward(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);

  const SgfNode *current = sgf_tree_get_current(self->sgf_tree);
  if (!current) {
    g_debug("Missing SGF current node");
    return FALSE;
  }

  const SgfNode *target = sgf_node_get_parent(current);
  if (!target) {
    return FALSE;
  }

  return gcheckers_sgf_controller_navigate_to(self, target);
}

gboolean gcheckers_sgf_controller_step_forward(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);

  const SgfNode *current = sgf_tree_get_current(self->sgf_tree);
  if (!current) {
    g_debug("Missing SGF current node");
    return FALSE;
  }

  const GPtrArray *children = sgf_node_get_children(current);
  if (!children || children->len == 0) {
    return FALSE;
  }

  const SgfNode *target = g_ptr_array_index((GPtrArray *)children, 0);
  g_return_val_if_fail(target != NULL, FALSE);
  return gcheckers_sgf_controller_navigate_to(self, target);
}

gboolean gcheckers_sgf_controller_step_forward_to_branch(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);

  const SgfNode *cursor = sgf_tree_get_current(self->sgf_tree);
  if (!cursor) {
    g_debug("Missing SGF current node");
    return FALSE;
  }

  gboolean moved = FALSE;
  while (TRUE) {
    const GPtrArray *children = sgf_node_get_children(cursor);
    if (!children || children->len == 0 || children->len >= 2) {
      break;
    }
    cursor = g_ptr_array_index((GPtrArray *)children, 0);
    g_return_val_if_fail(cursor != NULL, FALSE);
    moved = TRUE;
  }

  if (!moved) {
    return FALSE;
  }

  return gcheckers_sgf_controller_navigate_to(self, cursor);
}

gboolean gcheckers_sgf_controller_step_forward_to_end(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);

  const SgfNode *cursor = sgf_tree_get_current(self->sgf_tree);
  if (!cursor) {
    g_debug("Missing SGF current node");
    return FALSE;
  }

  gboolean moved = FALSE;
  while (TRUE) {
    const GPtrArray *children = sgf_node_get_children(cursor);
    if (!children || children->len == 0) {
      break;
    }
    cursor = g_ptr_array_index((GPtrArray *)children, 0);
    g_return_val_if_fail(cursor != NULL, FALSE);
    moved = TRUE;
  }

  if (!moved) {
    return FALSE;
  }

  return gcheckers_sgf_controller_navigate_to(self, cursor);
}

gboolean gcheckers_sgf_controller_load_file(GCheckersSgfController *self, const char *path, GError **error) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  g_autoptr(SgfTree) loaded = NULL;
  if (!sgf_io_load_file(path, &loaded, error)) {
    return FALSE;
  }

  g_clear_object(&self->sgf_tree);
  self->sgf_tree = g_steal_pointer(&loaded);
  sgf_view_set_tree(self->sgf_view, self->sgf_tree);
  board_view_clear_selection(self->board_view);

  const SgfNode *selected = sgf_tree_get_current(self->sgf_tree);
  if (self->model != NULL) {
    const GameBackendVariant *loaded_variant = NULL;
    g_autoptr(GError) ruleset_error = NULL;
    if (!sgf_io_tree_get_variant(self->sgf_tree, &loaded_variant, &ruleset_error) || loaded_variant == NULL) {
      g_propagate_error(error, g_steal_pointer(&ruleset_error));
      return FALSE;
    }

    PlayerRuleset loaded_ruleset = PLAYER_RULESET_INTERNATIONAL;
    if (!checkers_ruleset_find_by_short_name(loaded_variant->short_name, &loaded_ruleset)) {
      g_set_error_literal(error,
                          g_quark_from_static_string("gcheckers-sgf-controller-error"),
                          3,
                          "Loaded SGF references an unknown ruleset");
      return FALSE;
    }

    const CheckersRules *rules = checkers_ruleset_get_rules(loaded_ruleset);
    if (rules == NULL) {
      g_set_error_literal(error,
                          g_quark_from_static_string("gcheckers-sgf-controller-error"),
                          3,
                          "Loaded SGF references an unknown ruleset");
      return FALSE;
    }
    gcheckers_model_set_rules(self->model, rules);

    if (selected == NULL || !gcheckers_sgf_controller_replay_to_node(self, selected)) {
      g_set_error_literal(error, g_quark_from_static_string("gcheckers-sgf-controller-error"), 1,
                          "Unable to replay loaded SGF tree");
      return FALSE;
    }
  }

  if (selected != NULL) {
    gcheckers_sgf_controller_emit_node_changed(self, selected);
    g_signal_emit(self, controller_signals[SIGNAL_MANUAL_REQUESTED], 0, selected);
  }
  return TRUE;
}

gboolean gcheckers_sgf_controller_select_node(GCheckersSgfController *self, const SgfNode *node) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  return gcheckers_sgf_controller_navigate_to(self, node);
}

gboolean gcheckers_sgf_controller_refresh_current_node(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self->model), FALSE);

  const SgfNode *current = sgf_tree_get_current(self->sgf_tree);
  if (current == NULL) {
    g_debug("Missing SGF current node");
    return FALSE;
  }

  if (!gcheckers_sgf_controller_replay_to_node(self, current)) {
    return FALSE;
  }

  sgf_view_refresh(self->sgf_view);
  return TRUE;
}

gboolean gcheckers_sgf_controller_get_current_node_move(GCheckersSgfController *self, CheckersMove *out_move) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  const SgfNode *current = sgf_tree_get_current(self->sgf_tree);
  if (current == NULL || sgf_node_get_parent(current) == NULL) {
    return FALSE;
  }

  gboolean has_move = FALSE;
  if (!gcheckers_sgf_controller_extract_node_move(current, out_move, &has_move)) {
    return FALSE;
  }

  return has_move;
}

gboolean gcheckers_sgf_controller_save_file(GCheckersSgfController *self, const char *path, GError **error) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);

  if (!gcheckers_sgf_controller_sync_tree_ruleset_from_model(self)) {
    g_set_error_literal(error,
                        g_quark_from_static_string("gcheckers-sgf-controller-error"),
                        5,
                        "Unable to determine SGF ruleset metadata");
    return FALSE;
  }

  return sgf_io_save_file(path, self->sgf_tree, error);
}

gboolean gcheckers_sgf_controller_save_position_file(GCheckersSgfController *self, const char *path, GError **error) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self->model), FALSE);

  const GameState *state = gcheckers_model_peek_state(self->model);
  if (state == NULL) {
    g_set_error_literal(error,
                        g_quark_from_static_string("gcheckers-sgf-controller-error"),
                        2,
                        "Missing game state for SGF position save");
    return FALSE;
  }

  const CheckersRules *rules = gcheckers_model_peek_rules(self->model);
  if (rules == NULL) {
    g_set_error_literal(error,
                        g_quark_from_static_string("gcheckers-sgf-controller-error"),
                        6,
                        "Missing game rules for SGF position save");
    return FALSE;
  }

  PlayerRuleset ruleset = PLAYER_RULESET_INTERNATIONAL;
  if (!checkers_ruleset_find_by_rules(rules, &ruleset)) {
    g_set_error_literal(error,
                        g_quark_from_static_string("gcheckers-sgf-controller-error"),
                        7,
                        "Unable to determine SGF RU value for current rules");
    return FALSE;
  }

  const char *ru_value = checkers_ruleset_short_name(ruleset);
  if (ru_value == NULL) {
    g_set_error_literal(error,
                        g_quark_from_static_string("gcheckers-sgf-controller-error"),
                        8,
                        "Unable to encode SGF RU value");
    return FALSE;
  }

  g_autoptr(GPtrArray) ae_values = g_ptr_array_new_with_free_func(g_free);
  g_autoptr(GPtrArray) ab_values = g_ptr_array_new_with_free_func(g_free);
  g_autoptr(GPtrArray) aw_values = g_ptr_array_new_with_free_func(g_free);
  g_autoptr(GPtrArray) abk_values = g_ptr_array_new_with_free_func(g_free);
  g_autoptr(GPtrArray) awk_values = g_ptr_array_new_with_free_func(g_free);
  guint8 squares = board_playable_squares(state->board.board_size);
  for (guint8 i = 0; i < squares; ++i) {
    CheckersPiece piece = board_get(&state->board, i);
    char point[3] = {0};
    if (!gcheckers_sgf_controller_format_setup_point(i, state->board.board_size, point)) {
      g_set_error_literal(error,
                          g_quark_from_static_string("gcheckers-sgf-controller-error"),
                          4,
                          "Unable to encode SGF setup point");
      return FALSE;
    }

    if (piece == CHECKERS_PIECE_EMPTY) {
      g_ptr_array_add(ae_values, g_strdup(point));
      continue;
    }
    if (piece == CHECKERS_PIECE_BLACK_MAN) {
      g_ptr_array_add(ab_values, g_strdup(point));
      continue;
    }
    if (piece == CHECKERS_PIECE_WHITE_MAN) {
      g_ptr_array_add(aw_values, g_strdup(point));
      continue;
    }
    if (piece == CHECKERS_PIECE_BLACK_KING) {
      g_ptr_array_add(ab_values, g_strdup(point));
      g_ptr_array_add(abk_values, g_strdup(point));
      continue;
    }
    if (piece == CHECKERS_PIECE_WHITE_KING) {
      g_ptr_array_add(aw_values, g_strdup(point));
      g_ptr_array_add(awk_values, g_strdup(point));
      continue;
    }

    g_debug("Skipping unknown piece while saving SGF position");
  }

  GString *content = g_string_new("(;");
  g_string_append_printf(content, "FF[4]CA[UTF-8]AP[gcheckers]GM[40]RU[%s]", ru_value);
  if (!gcheckers_sgf_controller_append_prop_values(content, "AE", ae_values, error) ||
      !gcheckers_sgf_controller_append_prop_values(content, "AB", ab_values, error) ||
      !gcheckers_sgf_controller_append_prop_values(content, "AW", aw_values, error) ||
      !gcheckers_sgf_controller_append_prop_values(content, "ABK", abk_values, error) ||
      !gcheckers_sgf_controller_append_prop_values(content, "AWK", awk_values, error)) {
    g_string_free(content, TRUE);
    return FALSE;
  }

  const char *pl_value = state->turn == CHECKERS_COLOR_BLACK ? "B" : "W";
  g_string_append_printf(content, "PL[%s])", pl_value);

  gboolean written = g_file_set_contents(path, content->str, (gssize)content->len, error);
  g_string_free(content, TRUE);
  return written;
}

GtkWidget *gcheckers_sgf_controller_get_widget(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), NULL);

  GtkWidget *widget = sgf_view_get_widget(self->sgf_view);
  if (!widget) {
    g_debug("Missing SGF view widget");
    return NULL;
  }

  return widget;
}

SgfTree *gcheckers_sgf_controller_get_tree(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), NULL);

  if (!self->sgf_tree) {
    g_debug("Missing SGF tree");
    return NULL;
  }

  return self->sgf_tree;
}

SgfView *gcheckers_sgf_controller_get_view(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), NULL);

  if (!self->sgf_view) {
    g_debug("Missing SGF view");
    return NULL;
  }

  return self->sgf_view;
}

gboolean gcheckers_sgf_controller_is_replaying(GCheckersSgfController *self) {
  g_return_val_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self), FALSE);

  return self->is_replaying;
}

void gcheckers_sgf_controller_force_layout_resync(GCheckersSgfController *self) {
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(self));

  if (!self->sgf_view) {
    g_debug("Missing SGF view for layout resync");
    return;
  }

  sgf_view_force_layout_sync(self->sgf_view);
}
