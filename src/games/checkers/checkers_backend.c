#include "checkers_backend.h"

#include "../../ai_zobrist.h"
#include "../../board.h"
#include "../../game.h"
#include "../../ruleset.h"
#include "../../rulesets.h"

#include <string.h>

static const GameBackendVariant checkers_backend_variants[] = {
    [PLAYER_RULESET_AMERICAN] =
        {
            .id = "checkers-american",
            .name = "American (8x8)",
            .short_name = "american",
            .summary = "8x8 board, mandatory captures, short kings, and no backward captures for men.",
        },
    [PLAYER_RULESET_INTERNATIONAL] =
        {
            .id = "checkers-international",
            .name = "International (10x10)",
            .short_name = "international",
            .summary = "10x10 board, mandatory longest captures, flying kings, and backward captures for men.",
        },
    [PLAYER_RULESET_RUSSIAN] =
        {
            .id = "checkers-russian",
            .name = "Russian (8x8)",
            .short_name = "russian",
            .summary = "8x8 board, mandatory longest captures, flying kings, and backward captures for men.",
        },
};

static const char *checkers_backend_side_label(guint side) {
  switch (side) {
    case 0:
      return "White";
    case 1:
      return "Black";
    default:
      return NULL;
  }
}

static const char *checkers_backend_outcome_banner_text(GameBackendOutcome outcome) {
  switch (outcome) {
    case GAME_BACKEND_OUTCOME_SIDE_0_WIN:
      return "White wins!";
    case GAME_BACKEND_OUTCOME_SIDE_1_WIN:
      return "Black wins!";
    case GAME_BACKEND_OUTCOME_DRAW:
      return "Draw!";
    case GAME_BACKEND_OUTCOME_ONGOING:
    default:
      return NULL;
  }
}

static const GameBackendVariant *checkers_backend_variant_at(guint index) {
  if (index >= G_N_ELEMENTS(checkers_backend_variants)) {
    return NULL;
  }

  return &checkers_backend_variants[index];
}

static const GameBackendVariant *checkers_backend_variant_by_short_name(const char *short_name) {
  g_return_val_if_fail(short_name != NULL, NULL);

  PlayerRuleset ruleset = PLAYER_RULESET_AMERICAN;
  if (!checkers_ruleset_find_by_short_name(short_name, &ruleset)) {
    return NULL;
  }

  return checkers_backend_variant_at((guint)ruleset);
}

static PlayerRuleset checkers_backend_variant_to_ruleset(const GameBackendVariant *variant) {
  if (variant == NULL || variant->short_name == NULL) {
    return PLAYER_RULESET_AMERICAN;
  }

  PlayerRuleset ruleset = PLAYER_RULESET_AMERICAN;
  if (checkers_ruleset_find_by_short_name(variant->short_name, &ruleset)) {
    return ruleset;
  }

  return PLAYER_RULESET_AMERICAN;
}

static void checkers_backend_position_init(gpointer position, const GameBackendVariant *variant_or_null) {
  g_return_if_fail(position != NULL);

  Game *game = position;
  const CheckersRules *rules =
      checkers_ruleset_get_rules(checkers_backend_variant_to_ruleset(variant_or_null));
  g_return_if_fail(rules != NULL);

  game_init_with_rules(game, rules);
}

static void checkers_backend_position_clear(gpointer position) {
  g_return_if_fail(position != NULL);

  Game *game = position;
  game_destroy(game);
}

static void checkers_backend_position_copy(gpointer dest, gconstpointer src) {
  g_return_if_fail(dest != NULL);
  g_return_if_fail(src != NULL);

  memcpy(dest, src, sizeof(Game));
}

static GameBackendOutcome checkers_backend_position_outcome(gconstpointer position) {
  g_return_val_if_fail(position != NULL, GAME_BACKEND_OUTCOME_ONGOING);

  const Game *game = position;
  switch (game->state.winner) {
    case CHECKERS_WINNER_WHITE:
      return GAME_BACKEND_OUTCOME_SIDE_0_WIN;
    case CHECKERS_WINNER_BLACK:
      return GAME_BACKEND_OUTCOME_SIDE_1_WIN;
    case CHECKERS_WINNER_DRAW:
      return GAME_BACKEND_OUTCOME_DRAW;
    case CHECKERS_WINNER_NONE:
    default:
      return GAME_BACKEND_OUTCOME_ONGOING;
  }
}

static guint checkers_backend_position_turn(gconstpointer position) {
  g_return_val_if_fail(position != NULL, 0);

  const Game *game = position;
  return game->state.turn == CHECKERS_COLOR_BLACK ? 1u : 0u;
}

static GameBackendMoveList checkers_backend_list_moves(gconstpointer position) {
  GameBackendMoveList empty = {0};

  g_return_val_if_fail(position != NULL, empty);

  const Game *game = position;
  MoveList moves = game_list_available_moves(game);
  return (GameBackendMoveList){
      .moves = moves.moves,
      .count = moves.count,
  };
}

static void checkers_backend_move_list_free(GameBackendMoveList *moves) {
  g_return_if_fail(moves != NULL);

  MoveList native_moves = {
      .moves = moves->moves,
      .count = moves->count,
  };
  movelist_free(&native_moves);
  moves->moves = NULL;
  moves->count = 0;
}

static const void *checkers_backend_move_list_get(const GameBackendMoveList *moves, gsize index) {
  g_return_val_if_fail(moves != NULL, NULL);
  g_return_val_if_fail(index < moves->count, NULL);

  const CheckersMove *all_moves = moves->moves;
  return &all_moves[index];
}

static gboolean checkers_backend_moves_equal(gconstpointer left, gconstpointer right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  const CheckersMove *left_move = left;
  const CheckersMove *right_move = right;

  if (left_move->length != right_move->length || left_move->captures != right_move->captures) {
    return FALSE;
  }
  if (left_move->length == 0) {
    return TRUE;
  }

  return memcmp(left_move->path, right_move->path, left_move->length * sizeof(left_move->path[0])) == 0;
}

static gboolean checkers_backend_apply_move(gpointer position, gconstpointer move) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  Game *game = position;
  const CheckersMove *checkers_move = move;
  return game_apply_move(game, checkers_move) == 0;
}

static gint checkers_backend_man_advancement_bonus(CheckersPiece piece, int row, guint8 board_size) {
  if (piece != CHECKERS_PIECE_WHITE_MAN && piece != CHECKERS_PIECE_BLACK_MAN) {
    return 0;
  }

  gint distance_to_king_row = piece == CHECKERS_PIECE_WHITE_MAN ? row : (gint)board_size - 1 - row;
  if (distance_to_king_row <= 0 || distance_to_king_row > 3) {
    return 0;
  }

  return 4 - distance_to_king_row;
}

static gint checkers_backend_evaluate_static(gconstpointer position) {
  g_return_val_if_fail(position != NULL, 0);

  const Game *game = position;
  const CheckersBoard *board = &game->state.board;
  guint8 squares = board_playable_squares(board->board_size);
  gint score = 0;

  for (guint8 i = 0; i < squares; ++i) {
    CheckersPiece piece = board_get(board, i);
    gint value = 0;

    switch (piece) {
      case CHECKERS_PIECE_WHITE_MAN:
      case CHECKERS_PIECE_BLACK_MAN:
        value = 100;
        break;
      case CHECKERS_PIECE_WHITE_KING:
      case CHECKERS_PIECE_BLACK_KING:
        value = 200;
        break;
      case CHECKERS_PIECE_EMPTY:
      default:
        value = 0;
        break;
    }

    if (value == 0) {
      continue;
    }

    int row = 0;
    int col = 0;
    board_coord_from_index(i, &row, &col, board->board_size);
    value += checkers_backend_man_advancement_bonus(piece, row, board->board_size);
    (void) col;

    score += board_piece_color(piece) == CHECKERS_COLOR_WHITE ? value : -value;
  }

  return score;
}

static gint checkers_backend_terminal_score(GameBackendOutcome outcome, guint ply_depth) {
  gint win_score = 3000 - (gint) ply_depth;

  switch (outcome) {
    case GAME_BACKEND_OUTCOME_SIDE_0_WIN:
      return win_score;
    case GAME_BACKEND_OUTCOME_SIDE_1_WIN:
      return -win_score;
    case GAME_BACKEND_OUTCOME_DRAW:
    case GAME_BACKEND_OUTCOME_ONGOING:
    default:
      return 0;
  }
}

static guint64 checkers_backend_hash_position(gconstpointer position) {
  g_return_val_if_fail(position != NULL, 0);

  return checkers_ai_zobrist_key(position);
}

static guint checkers_backend_square_grid_rows(gconstpointer position) {
  g_return_val_if_fail(position != NULL, 0);

  const Game *game = position;
  return game->state.board.board_size;
}

static guint checkers_backend_square_grid_cols(gconstpointer position) {
  return checkers_backend_square_grid_rows(position);
}

static gboolean checkers_backend_square_grid_square_playable(gconstpointer position, guint row, guint col) {
  g_return_val_if_fail(position != NULL, FALSE);

  const Game *game = position;
  guint board_size = game->state.board.board_size;
  if (row >= board_size || col >= board_size) {
    return FALSE;
  }

  return ((row + col) % 2) != 0;
}

static gboolean checkers_backend_square_grid_square_index(gconstpointer position,
                                                          guint row,
                                                          guint col,
                                                          guint *out_index) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(out_index != NULL, FALSE);

  const Game *game = position;
  int8_t index = board_index_from_coord((int) row, (int) col, game->state.board.board_size);
  if (index < 0) {
    return FALSE;
  }

  *out_index = (guint) index;
  return TRUE;
}

static gboolean checkers_backend_square_grid_index_coord(gconstpointer position,
                                                         guint index,
                                                         guint *out_row,
                                                         guint *out_col) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(out_row != NULL, FALSE);
  g_return_val_if_fail(out_col != NULL, FALSE);

  const Game *game = position;
  guint max_index = board_playable_squares(game->state.board.board_size);
  if (index >= max_index) {
    return FALSE;
  }

  int row = 0;
  int col = 0;
  board_coord_from_index((guint8) index, &row, &col, game->state.board.board_size);
  *out_row = (guint) row;
  *out_col = (guint) col;
  return TRUE;
}

static gboolean checkers_backend_square_grid_piece_view(gconstpointer position,
                                                        guint index,
                                                        GameBackendSquarePieceView *out_view) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(out_view != NULL, FALSE);

  const Game *game = position;
  guint max_index = board_playable_squares(game->state.board.board_size);
  if (index >= max_index) {
    return FALSE;
  }

  memset(out_view, 0, sizeof(*out_view));

  CheckersPiece piece = board_get(&game->state.board, (guint8) index);
  switch (piece) {
    case CHECKERS_PIECE_WHITE_MAN:
      out_view->side = 0;
      out_view->kind = GAME_BACKEND_SQUARE_PIECE_KIND_MAN;
      out_view->symbol = "⛀";
      return TRUE;
    case CHECKERS_PIECE_WHITE_KING:
      out_view->side = 0;
      out_view->kind = GAME_BACKEND_SQUARE_PIECE_KIND_KING;
      out_view->symbol = "⛁";
      return TRUE;
    case CHECKERS_PIECE_BLACK_MAN:
      out_view->side = 1;
      out_view->kind = GAME_BACKEND_SQUARE_PIECE_KIND_MAN;
      out_view->symbol = "⛂";
      return TRUE;
    case CHECKERS_PIECE_BLACK_KING:
      out_view->side = 1;
      out_view->kind = GAME_BACKEND_SQUARE_PIECE_KIND_KING;
      out_view->symbol = "⛃";
      return TRUE;
    case CHECKERS_PIECE_EMPTY:
    default:
      out_view->is_empty = TRUE;
      out_view->kind = GAME_BACKEND_SQUARE_PIECE_KIND_NONE;
      out_view->symbol = "·";
      return TRUE;
  }
}

static gboolean checkers_backend_square_grid_move_get_path(gconstpointer move,
                                                           guint *out_length,
                                                           guint *out_indices,
                                                           gsize max_indices) {
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(out_length != NULL, FALSE);

  const CheckersMove *checkers_move = move;
  if (out_indices != NULL && checkers_move->length > max_indices) {
    return FALSE;
  }

  *out_length = checkers_move->length;
  if (out_indices == NULL) {
    return TRUE;
  }

  for (guint i = 0; i < checkers_move->length; ++i) {
    out_indices[i] = checkers_move->path[i];
  }
  return TRUE;
}

static void checkers_backend_square_grid_moves_collect_starts(const GameBackendMoveList *moves,
                                                              gboolean *out_starts,
                                                              gsize out_count) {
  g_return_if_fail(moves != NULL);
  g_return_if_fail(out_starts != NULL);

  memset(out_starts, 0, sizeof(*out_starts) * out_count);

  const CheckersMove *all_moves = moves->moves;
  for (gsize i = 0; i < moves->count; ++i) {
    if (all_moves[i].length == 0 || all_moves[i].path[0] >= out_count) {
      continue;
    }
    out_starts[all_moves[i].path[0]] = TRUE;
  }
}

static gboolean checkers_backend_square_grid_move_has_prefix(gconstpointer move,
                                                             const guint *path,
                                                             guint path_length) {
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  const CheckersMove *checkers_move = move;
  if (checkers_move->length < path_length) {
    return FALSE;
  }

  for (guint i = 0; i < path_length; ++i) {
    if (checkers_move->path[i] != path[i]) {
      return FALSE;
    }
  }

  return TRUE;
}

static void checkers_backend_square_grid_moves_collect_next_destinations(const GameBackendMoveList *moves,
                                                                         const guint *path,
                                                                         guint path_length,
                                                                         gboolean *out_destinations,
                                                                         gsize out_count) {
  g_return_if_fail(moves != NULL);
  g_return_if_fail(path != NULL || path_length == 0);
  g_return_if_fail(out_destinations != NULL);

  memset(out_destinations, 0, sizeof(*out_destinations) * out_count);

  const CheckersMove *all_moves = moves->moves;
  for (gsize i = 0; i < moves->count; ++i) {
    if (!checkers_backend_square_grid_move_has_prefix(&all_moves[i], path, path_length)) {
      continue;
    }
    if (all_moves[i].length <= path_length || all_moves[i].path[path_length] >= out_count) {
      continue;
    }
    out_destinations[all_moves[i].path[path_length]] = TRUE;
  }
}

static gboolean checkers_backend_format_move(gconstpointer move, char *buffer, gsize size) {
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(size > 0, FALSE);

  return game_format_move_notation(move, buffer, size);
}

const GameBackend checkers_game_backend = {
    .id = "checkers",
    .display_name = "Checkers",
    .variant_count = G_N_ELEMENTS(checkers_backend_variants),
    .position_size = sizeof(Game),
    .move_size = sizeof(CheckersMove),
    .variant_at = checkers_backend_variant_at,
    .variant_by_short_name = checkers_backend_variant_by_short_name,
    .side_label = checkers_backend_side_label,
    .outcome_banner_text = checkers_backend_outcome_banner_text,
    .position_init = checkers_backend_position_init,
    .position_clear = checkers_backend_position_clear,
    .position_copy = checkers_backend_position_copy,
    .position_outcome = checkers_backend_position_outcome,
    .position_turn = checkers_backend_position_turn,
    .list_moves = checkers_backend_list_moves,
    .move_list_free = checkers_backend_move_list_free,
    .move_list_get = checkers_backend_move_list_get,
    .moves_equal = checkers_backend_moves_equal,
    .apply_move = checkers_backend_apply_move,
    .evaluate_static = checkers_backend_evaluate_static,
    .terminal_score = checkers_backend_terminal_score,
    .hash_position = checkers_backend_hash_position,
    .format_move = checkers_backend_format_move,
    .supports_square_grid_board = TRUE,
    .square_grid_rows = checkers_backend_square_grid_rows,
    .square_grid_cols = checkers_backend_square_grid_cols,
    .square_grid_square_playable = checkers_backend_square_grid_square_playable,
    .square_grid_square_index = checkers_backend_square_grid_square_index,
    .square_grid_index_coord = checkers_backend_square_grid_index_coord,
    .square_grid_piece_view = checkers_backend_square_grid_piece_view,
    .square_grid_move_get_path = checkers_backend_square_grid_move_get_path,
    .square_grid_moves_collect_starts = checkers_backend_square_grid_moves_collect_starts,
    .square_grid_moves_collect_next_destinations =
        checkers_backend_square_grid_moves_collect_next_destinations,
    .square_grid_move_has_prefix = checkers_backend_square_grid_move_has_prefix,
};
