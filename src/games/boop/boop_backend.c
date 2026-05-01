#include "boop_backend.h"

#include "boop_game.h"
#include "boop_sgf_position.h"

#include <string.h>

static const char *boop_backend_side_label(guint side) {
  switch (side) {
    case 0:
      return "Player 1";
    case 1:
      return "Player 2";
    default:
      g_debug("Unsupported boop side index");
      return "Player";
  }
}

static const char *boop_backend_outcome_banner_text(GameBackendOutcome outcome) {
  switch (outcome) {
    case GAME_BACKEND_OUTCOME_SIDE_0_WIN:
      return "Player 1 wins";
    case GAME_BACKEND_OUTCOME_SIDE_1_WIN:
      return "Player 2 wins";
    case GAME_BACKEND_OUTCOME_DRAW:
      return "Draw";
    case GAME_BACKEND_OUTCOME_ONGOING:
    default:
      return NULL;
  }
}

static void boop_backend_position_init(gpointer position, const GameBackendVariant * /*variant_or_null*/) {
  BoopPosition *boop_position = position;

  g_return_if_fail(boop_position != NULL);

  boop_position_init(boop_position);
}

static void boop_backend_position_clear(gpointer position) {
  BoopPosition *boop_position = position;

  g_return_if_fail(boop_position != NULL);

  boop_position_clear(boop_position);
}

static void boop_backend_position_copy(gpointer dest, gconstpointer src) {
  BoopPosition *dest_position = dest;
  const BoopPosition *src_position = src;

  g_return_if_fail(dest_position != NULL);
  g_return_if_fail(src_position != NULL);

  boop_position_copy(dest_position, src_position);
}

static GameBackendOutcome boop_backend_position_outcome(gconstpointer position) {
  const BoopPosition *boop_position = position;

  g_return_val_if_fail(boop_position != NULL, GAME_BACKEND_OUTCOME_ONGOING);

  return boop_position_outcome(boop_position);
}

static guint boop_backend_position_turn(gconstpointer position) {
  const BoopPosition *boop_position = position;

  g_return_val_if_fail(boop_position != NULL, 0);

  return boop_position_turn(boop_position);
}

static GameBackendMoveList boop_backend_list_moves(gconstpointer position) {
  const BoopPosition *boop_position = position;

  g_return_val_if_fail(boop_position != NULL, (GameBackendMoveList){0});

  return boop_position_list_moves(boop_position);
}

static GameBackendMoveList boop_backend_list_good_moves(gconstpointer position, guint max_count, guint depth_hint) {
  const BoopPosition *boop_position = position;

  g_return_val_if_fail(boop_position != NULL, (GameBackendMoveList){0});

  return boop_position_list_good_moves(boop_position, max_count, depth_hint);
}

static void boop_backend_move_list_free(GameBackendMoveList *moves) {
  boop_move_list_free(moves);
}

static const void *boop_backend_move_list_get(const GameBackendMoveList *moves, gsize index) {
  return boop_move_list_get(moves, index);
}

static gboolean boop_backend_moves_equal(gconstpointer left, gconstpointer right) {
  const BoopMove *left_move = left;
  const BoopMove *right_move = right;

  g_return_val_if_fail(left_move != NULL, FALSE);
  g_return_val_if_fail(right_move != NULL, FALSE);

  return boop_moves_equal(left_move, right_move);
}

static gconstpointer boop_backend_move_builder_preview_position(const GameBackendMoveBuilder *builder) {
  g_return_val_if_fail(builder != NULL, NULL);
  g_return_val_if_fail(builder->builder_state != NULL, NULL);
  g_return_val_if_fail(builder->builder_state_size == sizeof(BoopMoveBuilderState), NULL);

  const BoopMoveBuilderState *state = builder->builder_state;
  if (state->stage == BOOP_MOVE_BUILDER_STAGE_PLACEMENT) {
    return NULL;
  }

  return &state->after_placement;
}

static gboolean boop_backend_move_builder_get_selection_path(const GameBackendMoveBuilder *builder,
                                                             guint *out_length,
                                                             guint *out_indices,
                                                             gsize max_indices) {
  g_return_val_if_fail(builder != NULL, FALSE);
  g_return_val_if_fail(builder->builder_state != NULL, FALSE);
  g_return_val_if_fail(builder->builder_state_size == sizeof(BoopMoveBuilderState), FALSE);
  g_return_val_if_fail(out_length != NULL, FALSE);

  const BoopMoveBuilderState *state = builder->builder_state;
  if (state->stage == BOOP_MOVE_BUILDER_STAGE_PLACEMENT || state->promotion_option_count == 0) {
    *out_length = 0;
    return TRUE;
  }

  if (out_indices != NULL) {
    if (state->selection_path_length > max_indices) {
      return FALSE;
    }
    for (guint i = 0; i < state->selection_path_length; ++i) {
      out_indices[i] = state->selection_path[i];
    }
  }
  *out_length = state->selection_path_length;
  return TRUE;
}

static gboolean boop_backend_move_builder_reset_selection(GameBackendMoveBuilder *builder) {
  g_return_val_if_fail(builder != NULL, FALSE);
  g_return_val_if_fail(builder->builder_state != NULL, FALSE);
  g_return_val_if_fail(builder->builder_state_size == sizeof(BoopMoveBuilderState), FALSE);

  BoopMoveBuilderState *state = builder->builder_state;
  if (state->stage != BOOP_MOVE_BUILDER_STAGE_PROMOTION &&
      (state->stage != BOOP_MOVE_BUILDER_STAGE_COMPLETE || state->promotion_option_count == 0)) {
    return FALSE;
  }

  state->stage = BOOP_MOVE_BUILDER_STAGE_PROMOTION;
  state->selected_mask = 0;
  state->selection_path_length = 0;
  memset(state->selection_path, 0, sizeof(state->selection_path));
  state->move.promotion_mask = 0;
  state->move.path_length = 0;
  memset(state->move.path, 0, sizeof(state->move.path));
  return TRUE;
}

static gboolean boop_backend_apply_move(gpointer position, gconstpointer move) {
  BoopPosition *boop_position = position;
  const BoopMove *boop_move = move;

  g_return_val_if_fail(boop_position != NULL, FALSE);
  g_return_val_if_fail(boop_move != NULL, FALSE);

  return boop_position_apply_move(boop_position, boop_move);
}

static gint boop_backend_evaluate_static(gconstpointer position) {
  const BoopPosition *boop_position = position;

  g_return_val_if_fail(boop_position != NULL, 0);

  return boop_position_evaluate_static(boop_position);
}

static gint boop_backend_terminal_score(GameBackendOutcome outcome, guint ply_depth) {
  return boop_position_terminal_score(outcome, ply_depth);
}

static guint64 boop_backend_hash_position(gconstpointer position) {
  const BoopPosition *boop_position = position;

  g_return_val_if_fail(boop_position != NULL, 0);

  return boop_position_hash(boop_position);
}

static gboolean boop_backend_format_move(gconstpointer move, char *buffer, gsize size) {
  const BoopMove *boop_move = move;

  g_return_val_if_fail(boop_move != NULL, FALSE);

  return boop_move_format(boop_move, buffer, size);
}

static gboolean boop_backend_parse_move(const char *notation, gpointer out_move) {
  BoopMove *boop_move = out_move;

  g_return_val_if_fail(boop_move != NULL, FALSE);

  return boop_move_parse(notation, boop_move);
}

static guint boop_backend_square_grid_rows(gconstpointer position) {
  g_return_val_if_fail(position != NULL, 0);

  return BOOP_BOARD_SIZE;
}

static guint boop_backend_square_grid_cols(gconstpointer position) {
  g_return_val_if_fail(position != NULL, 0);

  return BOOP_BOARD_SIZE;
}

static gboolean boop_backend_square_grid_square_playable(gconstpointer position, guint row, guint col) {
  g_return_val_if_fail(position != NULL, FALSE);

  return row < BOOP_BOARD_SIZE && col < BOOP_BOARD_SIZE;
}

static gboolean boop_backend_square_grid_square_index(gconstpointer position,
                                                      guint row,
                                                      guint col,
                                                      guint *out_index) {
  g_return_val_if_fail(position != NULL, FALSE);

  return boop_coord_to_square(row, col, out_index);
}

static gboolean boop_backend_square_grid_index_coord(gconstpointer position,
                                                     guint index,
                                                     guint *out_row,
                                                     guint *out_col) {
  g_return_val_if_fail(position != NULL, FALSE);

  return boop_square_to_coord(index, out_row, out_col);
}

static gboolean boop_backend_square_grid_piece_view(gconstpointer position,
                                                    guint index,
                                                    GameBackendSquarePieceView *out_view) {
  const BoopPosition *boop_position = position;

  g_return_val_if_fail(boop_position != NULL, FALSE);
  g_return_val_if_fail(out_view != NULL, FALSE);
  g_return_val_if_fail(index < BOOP_SQUARE_COUNT, FALSE);

  memset(out_view, 0, sizeof(*out_view));

  BoopPiece piece = boop_position->board[index];
  if (piece.rank == BOOP_PIECE_RANK_NONE) {
    out_view->is_empty = TRUE;
    out_view->kind = GAME_BACKEND_SQUARE_PIECE_KIND_NONE;
    out_view->symbol = ".";
    return TRUE;
  }

  out_view->side = piece.side;
  out_view->kind = GAME_BACKEND_SQUARE_PIECE_KIND_SYMBOL_ONLY;
  if (piece.side == 0) {
    out_view->symbol = piece.rank == BOOP_PIECE_RANK_CAT ? "c" : "k";
  } else {
    out_view->symbol = piece.rank == BOOP_PIECE_RANK_CAT ? "C" : "K";
  }
  return TRUE;
}

static gboolean boop_backend_square_grid_move_get_path(gconstpointer move,
                                                       guint *out_length,
                                                       guint *out_indices,
                                                       gsize max_indices) {
  const BoopMove *boop_move = move;

  g_return_val_if_fail(boop_move != NULL, FALSE);

  return boop_move_get_path(boop_move, out_length, out_indices, max_indices);
}

static void boop_backend_square_grid_moves_collect_starts(const GameBackendMoveList *moves,
                                                          gboolean *out_starts,
                                                          gsize out_count) {
  g_return_if_fail(moves != NULL);
  g_return_if_fail(out_starts != NULL);

  memset(out_starts, 0, sizeof(*out_starts) * out_count);

  const BoopMove *all_moves = moves->moves;
  for (gsize i = 0; i < moves->count; ++i) {
    guint length = 0;
    guint path[BOOP_MOVE_PATH_MAX] = {0};
    if (!boop_move_get_path(&all_moves[i], &length, path, G_N_ELEMENTS(path)) || length == 0 ||
        path[0] >= out_count) {
      continue;
    }
    out_starts[path[0]] = TRUE;
  }
}

static gboolean boop_backend_square_grid_move_has_prefix(gconstpointer move,
                                                         const guint *path,
                                                         guint path_length) {
  const BoopMove *boop_move = move;
  guint move_length = 0;
  guint move_path[BOOP_MOVE_PATH_MAX] = {0};

  g_return_val_if_fail(boop_move != NULL, FALSE);
  g_return_val_if_fail(path != NULL || path_length == 0, FALSE);

  if (!boop_move_get_path(boop_move, &move_length, move_path, G_N_ELEMENTS(move_path)) ||
      move_length < path_length) {
    return FALSE;
  }

  for (guint i = 0; i < path_length; ++i) {
    if (move_path[i] != path[i]) {
      return FALSE;
    }
  }
  return TRUE;
}

static void boop_backend_square_grid_moves_collect_next_destinations(const GameBackendMoveList *moves,
                                                                     const guint *path,
                                                                     guint path_length,
                                                                     gboolean *out_destinations,
                                                                     gsize out_count) {
  g_return_if_fail(moves != NULL);
  g_return_if_fail(path != NULL || path_length == 0);
  g_return_if_fail(out_destinations != NULL);

  memset(out_destinations, 0, sizeof(*out_destinations) * out_count);

  const BoopMove *all_moves = moves->moves;
  for (gsize i = 0; i < moves->count; ++i) {
    guint move_length = 0;
    guint move_path[BOOP_MOVE_PATH_MAX] = {0};
    if (!boop_backend_square_grid_move_has_prefix(&all_moves[i], path, path_length) ||
        !boop_move_get_path(&all_moves[i], &move_length, move_path, G_N_ELEMENTS(move_path)) ||
        move_length <= path_length || move_path[path_length] >= out_count) {
      continue;
    }
    out_destinations[move_path[path_length]] = TRUE;
  }
}

const GameBackend boop_game_backend = {
  .id = "boop",
  .display_name = "Boop",
  .variant_count = 0,
  .position_size = sizeof(BoopPosition),
  .move_size = sizeof(BoopMove),
  .supports_move_list = TRUE,
  .supports_move_builder = TRUE,
  .supports_ai_search = TRUE,
  .side_label = boop_backend_side_label,
  .outcome_banner_text = boop_backend_outcome_banner_text,
  .position_init = boop_backend_position_init,
  .position_clear = boop_backend_position_clear,
  .position_copy = boop_backend_position_copy,
  .position_outcome = boop_backend_position_outcome,
  .position_turn = boop_backend_position_turn,
  .list_moves = boop_backend_list_moves,
  .list_good_moves = boop_backend_list_good_moves,
  .move_list_free = boop_backend_move_list_free,
  .move_list_get = boop_backend_move_list_get,
  .moves_equal = boop_backend_moves_equal,
  .move_builder_init = (gboolean (*)(gconstpointer, GameBackendMoveBuilder *))boop_move_builder_init,
  .move_builder_clear = boop_move_builder_clear,
  .move_builder_list_candidates =
      (GameBackendMoveList (*)(const GameBackendMoveBuilder *))boop_move_builder_list_candidates,
  .move_builder_step = (gboolean (*)(GameBackendMoveBuilder *, gconstpointer))boop_move_builder_step,
  .move_builder_is_complete = (gboolean (*)(const GameBackendMoveBuilder *))boop_move_builder_is_complete,
  .move_builder_build_move = (gboolean (*)(const GameBackendMoveBuilder *, gpointer))boop_move_builder_build_move,
  .move_builder_preview_position = boop_backend_move_builder_preview_position,
  .move_builder_get_selection_path = boop_backend_move_builder_get_selection_path,
  .move_builder_reset_selection = boop_backend_move_builder_reset_selection,
  .apply_move = boop_backend_apply_move,
  .evaluate_static = boop_backend_evaluate_static,
  .terminal_score = boop_backend_terminal_score,
  .hash_position = boop_backend_hash_position,
  .format_move = boop_backend_format_move,
  .parse_move = boop_backend_parse_move,
  .sgf_apply_setup_node = boop_sgf_position_apply_setup_node,
  .sgf_write_position_node = boop_sgf_position_write_position_node,
  .supports_square_grid_board = TRUE,
  .square_grid_rows = boop_backend_square_grid_rows,
  .square_grid_cols = boop_backend_square_grid_cols,
  .square_grid_square_playable = boop_backend_square_grid_square_playable,
  .square_grid_square_index = boop_backend_square_grid_square_index,
  .square_grid_index_coord = boop_backend_square_grid_index_coord,
  .square_grid_piece_view = boop_backend_square_grid_piece_view,
  .square_grid_move_get_path = boop_backend_square_grid_move_get_path,
  .square_grid_moves_collect_starts = boop_backend_square_grid_moves_collect_starts,
  .square_grid_moves_collect_next_destinations = boop_backend_square_grid_moves_collect_next_destinations,
  .square_grid_move_has_prefix = boop_backend_square_grid_move_has_prefix,
};
