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
};
