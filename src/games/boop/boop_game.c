#include "boop_game.h"

#include <string.h>

typedef struct {
  guint64 masks[BOOP_PROMOTION_OPTION_MAX];
  guint count;
  gboolean mandatory;
} BoopPromotionChoices;

static const gint boop_line_dirs[][2] = {
  {0, 1},
  {1, 0},
  {1, 1},
  {1, -1},
};

static GQuark boop_position_error_quark(void) {
  return g_quark_from_static_string("boop-position-error");
}

static guint64 boop_square_mask(guint square) {
  g_return_val_if_fail(square < BOOP_SQUARE_COUNT, 0);

  return G_GUINT64_CONSTANT(1) << square;
}

static guint boop_mask_popcount(guint64 mask) {
  guint count = 0;

  while (mask != 0) {
    count += (guint)(mask & 1u);
    mask >>= 1;
  }
  return count;
}

static gboolean boop_piece_is_empty(BoopPiece piece) {
  return piece.rank == BOOP_PIECE_RANK_NONE;
}

static BoopPiece boop_piece_empty(void) {
  return (BoopPiece){0};
}

static gboolean boop_piece_rank_valid(guint rank) {
  return rank == BOOP_PIECE_RANK_KITTEN || rank == BOOP_PIECE_RANK_CAT;
}

static gboolean boop_side_valid(guint side) {
  return side < 2;
}

static gboolean boop_square_valid(guint square) {
  return square < BOOP_SQUARE_COUNT;
}

static gboolean boop_coord_valid(gint row, gint col) {
  return row >= 0 && row < BOOP_BOARD_SIZE && col >= 0 && col < BOOP_BOARD_SIZE;
}

static gboolean boop_coord_to_square_signed(gint row, gint col, guint *out_square) {
  g_return_val_if_fail(out_square != NULL, FALSE);

  if (!boop_coord_valid(row, col)) {
    return FALSE;
  }

  *out_square = (guint)(row * BOOP_BOARD_SIZE + col);
  return TRUE;
}

static void boop_return_piece_to_supply(BoopPosition *position, BoopPiece piece) {
  g_return_if_fail(position != NULL);

  if (boop_piece_is_empty(piece)) {
    return;
  }

  g_return_if_fail(boop_side_valid(piece.side));
  switch ((BoopPieceRank)piece.rank) {
    case BOOP_PIECE_RANK_KITTEN:
      position->kittens_in_supply[piece.side]++;
      break;
    case BOOP_PIECE_RANK_CAT:
      position->cats_in_supply[piece.side]++;
      break;
    case BOOP_PIECE_RANK_NONE:
    default:
      g_debug("Unsupported boop piece rank while returning to supply");
      break;
  }
}

static gboolean boop_position_has_supply_for_rank(const BoopPosition *position, guint side, guint rank) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(boop_side_valid(side), FALSE);

  switch ((BoopPieceRank)rank) {
    case BOOP_PIECE_RANK_KITTEN:
      return position->kittens_in_supply[side] > 0;
    case BOOP_PIECE_RANK_CAT:
      return position->cats_in_supply[side] > 0;
    case BOOP_PIECE_RANK_NONE:
    default:
      return FALSE;
  }
}

static guint boop_position_count_on_board(const BoopPosition *position, guint side) {
  guint count = 0;

  g_return_val_if_fail(position != NULL, 0);
  g_return_val_if_fail(boop_side_valid(side), 0);

  for (guint square = 0; square < BOOP_SQUARE_COUNT; ++square) {
    BoopPiece piece = position->board[square];
    if (!boop_piece_is_empty(piece) && piece.side == side) {
      count++;
    }
  }
  return count;
}

static gboolean boop_position_add_mask(guint64 *masks, guint *count, guint64 mask) {
  g_return_val_if_fail(masks != NULL, FALSE);
  g_return_val_if_fail(count != NULL, FALSE);

  if (mask == 0) {
    return TRUE;
  }

  for (guint i = 0; i < *count; ++i) {
    if (masks[i] == mask) {
      return TRUE;
    }
  }

  if (*count >= BOOP_PROMOTION_OPTION_MAX) {
    g_debug("Too many boop promotion choices");
    return FALSE;
  }

  masks[*count] = mask;
  (*count)++;
  return TRUE;
}

static gboolean boop_position_collect_line_promotion_choices(const BoopPosition *position,
                                                             guint side,
                                                             BoopPromotionChoices *choices) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(boop_side_valid(side), FALSE);
  g_return_val_if_fail(choices != NULL, FALSE);

  memset(choices, 0, sizeof(*choices));
  choices->mandatory = TRUE;

  for (guint dir = 0; dir < G_N_ELEMENTS(boop_line_dirs); ++dir) {
    gint row_step = boop_line_dirs[dir][0];
    gint col_step = boop_line_dirs[dir][1];

    for (gint row = 0; row < BOOP_BOARD_SIZE; ++row) {
      for (gint col = 0; col < BOOP_BOARD_SIZE; ++col) {
        guint64 mask = 0;
        gboolean all_side = TRUE;
        gboolean has_kitten = FALSE;

        for (gint offset = 0; offset < 3; ++offset) {
          guint square = 0;
          if (!boop_coord_to_square_signed(row + (row_step * offset), col + (col_step * offset), &square)) {
            all_side = FALSE;
            break;
          }

          BoopPiece piece = position->board[square];
          if (boop_piece_is_empty(piece) || piece.side != side) {
            all_side = FALSE;
            break;
          }

          if (piece.rank == BOOP_PIECE_RANK_KITTEN) {
            has_kitten = TRUE;
          }
          mask |= boop_square_mask(square);
        }

        if (all_side && has_kitten && !boop_position_add_mask(choices->masks, &choices->count, mask)) {
          return FALSE;
        }
      }
    }
  }

  return TRUE;
}

static gboolean boop_position_has_cat_line(const BoopPosition *position, guint side) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(boop_side_valid(side), FALSE);

  for (guint dir = 0; dir < G_N_ELEMENTS(boop_line_dirs); ++dir) {
    gint row_step = boop_line_dirs[dir][0];
    gint col_step = boop_line_dirs[dir][1];

    for (gint row = 0; row < BOOP_BOARD_SIZE; ++row) {
      for (gint col = 0; col < BOOP_BOARD_SIZE; ++col) {
        gboolean all_cats = TRUE;

        for (gint offset = 0; offset < 3; ++offset) {
          guint square = 0;
          if (!boop_coord_to_square_signed(row + (row_step * offset), col + (col_step * offset), &square)) {
            all_cats = FALSE;
            break;
          }

          BoopPiece piece = position->board[square];
          if (boop_piece_is_empty(piece) || piece.side != side || piece.rank != BOOP_PIECE_RANK_CAT) {
            all_cats = FALSE;
            break;
          }
        }

        if (all_cats) {
          return TRUE;
        }
      }
    }
  }

  return FALSE;
}

static gboolean boop_position_collect_graduation_choices(const BoopPosition *position,
                                                         guint side,
                                                         BoopPromotionChoices *choices) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(boop_side_valid(side), FALSE);
  g_return_val_if_fail(choices != NULL, FALSE);

  memset(choices, 0, sizeof(*choices));
  choices->mandatory = FALSE;
  if (boop_position_count_on_board(position, side) != BOOP_SUPPLY_COUNT) {
    return TRUE;
  }

  for (guint square = 0; square < BOOP_SQUARE_COUNT; ++square) {
    BoopPiece piece = position->board[square];
    if (!boop_piece_is_empty(piece) && piece.side == side && piece.rank == BOOP_PIECE_RANK_KITTEN &&
        !boop_position_add_mask(choices->masks, &choices->count, boop_square_mask(square))) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean boop_position_collect_turn_choices(const BoopPosition *position,
                                                   guint side,
                                                   BoopPromotionChoices *choices) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(boop_side_valid(side), FALSE);
  g_return_val_if_fail(choices != NULL, FALSE);

  memset(choices, 0, sizeof(*choices));
  if (boop_position_has_cat_line(position, side)) {
    return TRUE;
  }

  if (!boop_position_collect_line_promotion_choices(position, side, choices)) {
    return FALSE;
  }
  if (choices->count > 0) {
    return TRUE;
  }

  return boop_position_collect_graduation_choices(position, side, choices);
}

static gboolean boop_position_mask_is_choice(const BoopPromotionChoices *choices, guint64 mask) {
  g_return_val_if_fail(choices != NULL, FALSE);

  for (guint i = 0; i < choices->count; ++i) {
    if (choices->masks[i] == mask) {
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean boop_move_overlay_append_arrow(BoopMoveOverlayInfo *info,
                                               guint from_square,
                                               guint to_square,
                                               gint row_delta,
                                               gint col_delta,
                                               gboolean leaves_board) {
  g_return_val_if_fail(info != NULL, FALSE);
  g_return_val_if_fail(boop_square_valid(from_square), FALSE);
  g_return_val_if_fail(leaves_board || boop_square_valid(to_square), FALSE);
  g_return_val_if_fail(row_delta >= G_MININT8 && row_delta <= G_MAXINT8, FALSE);
  g_return_val_if_fail(col_delta >= G_MININT8 && col_delta <= G_MAXINT8, FALSE);

  if (info->arrow_count >= G_N_ELEMENTS(info->arrows)) {
    g_debug("Too many boop overlay arrows");
    return FALSE;
  }

  info->arrows[info->arrow_count] = (BoopMoveOverlayArrow){
    .from_square = (guint8)from_square,
    .to_square = leaves_board ? BOOP_INVALID_SQUARE : (guint8)to_square,
    .row_delta = (gint8)row_delta,
    .col_delta = (gint8)col_delta,
    .leaves_board = leaves_board,
  };
  info->arrow_count++;
  return TRUE;
}

static gboolean boop_move_overlay_append_removed_square(BoopMoveOverlayInfo *info, guint square) {
  g_return_val_if_fail(info != NULL, FALSE);
  g_return_val_if_fail(boop_square_valid(square), FALSE);

  for (guint i = 0; i < info->removed_square_count; ++i) {
    if (info->removed_squares[i] == square) {
      return TRUE;
    }
  }

  if (info->removed_square_count >= G_N_ELEMENTS(info->removed_squares)) {
    g_debug("Too many boop overlay removed squares");
    return FALSE;
  }

  info->removed_squares[info->removed_square_count] = (guint8) square;
  info->removed_square_count++;
  return TRUE;
}

static gboolean boop_position_apply_boop_effects(BoopPosition *position,
                                                 guint placed_square,
                                                 guint rank,
                                                 BoopMoveOverlayInfo *overlay_info) {
  BoopPiece before[BOOP_SQUARE_COUNT];

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(boop_square_valid(placed_square), FALSE);
  g_return_val_if_fail(boop_piece_rank_valid(rank), FALSE);

  memcpy(before, position->board, sizeof(before));

  guint placed_row = 0;
  guint placed_col = 0;
  if (!boop_square_to_coord(placed_square, &placed_row, &placed_col)) {
    return FALSE;
  }

  for (gint row_delta = -1; row_delta <= 1; ++row_delta) {
    for (gint col_delta = -1; col_delta <= 1; ++col_delta) {
      if (row_delta == 0 && col_delta == 0) {
        continue;
      }

      gint adjacent_row = (gint)placed_row + row_delta;
      gint adjacent_col = (gint)placed_col + col_delta;
      guint adjacent_square = 0;
      if (!boop_coord_to_square_signed(adjacent_row, adjacent_col, &adjacent_square)) {
        continue;
      }

      BoopPiece target = before[adjacent_square];
      if (boop_piece_is_empty(target)) {
        continue;
      }
      if (target.rank == BOOP_PIECE_RANK_CAT && rank != BOOP_PIECE_RANK_CAT) {
        continue;
      }

      guint destination_square = 0;
      gint destination_row = adjacent_row + row_delta;
      gint destination_col = adjacent_col + col_delta;
      if (!boop_coord_to_square_signed(destination_row, destination_col, &destination_square)) {
        position->board[adjacent_square] = boop_piece_empty();
        boop_return_piece_to_supply(position, target);
        if (overlay_info != NULL && !boop_move_overlay_append_removed_square(overlay_info, adjacent_square)) {
          return FALSE;
        }
        if (overlay_info != NULL &&
            !boop_move_overlay_append_arrow(overlay_info,
                                            adjacent_square,
                                            BOOP_INVALID_SQUARE,
                                            row_delta,
                                            col_delta,
                                            TRUE)) {
          return FALSE;
        }
        continue;
      }

      if (!boop_piece_is_empty(before[destination_square])) {
        continue;
      }

      position->board[adjacent_square] = boop_piece_empty();
      position->board[destination_square] = target;
      if (overlay_info != NULL &&
          !boop_move_overlay_append_arrow(overlay_info,
                                          adjacent_square,
                                          destination_square,
                                          row_delta,
                                          col_delta,
                                          FALSE)) {
        return FALSE;
      }
    }
  }

  return TRUE;
}

static gboolean boop_position_resolve_placement(const BoopPosition *position,
                                                guint side,
                                                guint rank,
                                                guint square,
                                                BoopPosition *out_position,
                                                BoopMoveOverlayInfo *overlay_info) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(out_position != NULL, FALSE);
  g_return_val_if_fail(boop_side_valid(side), FALSE);
  g_return_val_if_fail(boop_piece_rank_valid(rank), FALSE);
  g_return_val_if_fail(boop_square_valid(square), FALSE);
  g_return_val_if_fail(boop_piece_is_empty(position->board[square]), FALSE);
  g_return_val_if_fail(boop_position_has_supply_for_rank(position, side, rank), FALSE);

  *out_position = *position;
  switch ((BoopPieceRank)rank) {
    case BOOP_PIECE_RANK_KITTEN:
      out_position->kittens_in_supply[side]--;
      break;
    case BOOP_PIECE_RANK_CAT:
      out_position->cats_in_supply[side]--;
      break;
    case BOOP_PIECE_RANK_NONE:
    default:
      return FALSE;
  }
  out_position->board[square] = (BoopPiece){
    .side = (guint8)side,
    .rank = (guint8)rank,
  };
  return boop_position_apply_boop_effects(out_position, square, rank, overlay_info);
}

static gboolean boop_position_apply_promotion_mask(BoopPosition *position,
                                                   guint side,
                                                   guint64 mask,
                                                   BoopMoveOverlayInfo *overlay_info) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(boop_side_valid(side), FALSE);

  for (guint square = 0; square < BOOP_SQUARE_COUNT; ++square) {
    if ((mask & boop_square_mask(square)) == 0) {
      continue;
    }

    BoopPiece piece = position->board[square];
    if (boop_piece_is_empty(piece) || piece.side != side) {
      g_debug("Ignoring invalid boop promotion square %u", square);
      continue;
    }

    if (piece.rank == BOOP_PIECE_RANK_KITTEN) {
      position->promoted_count[side]++;
    }
    position->cats_in_supply[side]++;
    position->board[square] = boop_piece_empty();
    if (overlay_info != NULL && !boop_move_overlay_append_removed_square(overlay_info, square)) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean boop_move_array_append(GArray *moves, BoopMove move) {
  g_return_val_if_fail(moves != NULL, FALSE);

  g_array_append_val(moves, move);
  return TRUE;
}

static gboolean boop_position_append_resolved_moves_for_placement(const BoopPosition *position,
                                                                  guint side,
                                                                  guint rank,
                                                                  guint square,
                                                                  GArray *moves) {
  BoopPosition after_placement = {0};
  BoopPromotionChoices choices = {0};

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(moves != NULL, FALSE);

  if (!boop_position_resolve_placement(position, side, rank, square, &after_placement, NULL) ||
      !boop_position_collect_turn_choices(&after_placement, side, &choices)) {
    return FALSE;
  }

  BoopMove move = {
    .square = (guint8)square,
    .rank = (guint8)rank,
  };
  if (choices.count == 0) {
    return boop_move_array_append(moves, move);
  }
  if (!choices.mandatory) {
    if (!boop_move_array_append(moves, move)) {
      return FALSE;
    }
  }

  for (guint i = 0; i < choices.count; ++i) {
    move.promotion_mask = choices.masks[i];
    if (!boop_move_array_append(moves, move)) {
      return FALSE;
    }
  }
  return TRUE;
}

static gboolean boop_move_path_append_square(BoopMove *move, guint square) {
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(boop_square_valid(square), FALSE);

  if (move->path_length >= BOOP_MOVE_PATH_MAX) {
    g_debug("Boop move path exceeds maximum length");
    return FALSE;
  }

  move->path[move->path_length] = (guint8)square;
  move->path_length++;
  return TRUE;
}

static gboolean boop_move_set_path_from_mask(BoopMove *move, guint64 mask) {
  g_return_val_if_fail(move != NULL, FALSE);

  move->path_length = 0;
  memset(move->path, 0, sizeof(move->path));
  if (!boop_move_path_append_square(move, move->square)) {
    return FALSE;
  }

  for (guint square = 0; square < BOOP_SQUARE_COUNT; ++square) {
    if ((mask & boop_square_mask(square)) != 0 && !boop_move_path_append_square(move, square)) {
      return FALSE;
    }
  }
  return TRUE;
}

static gboolean boop_builder_candidate_exists(const BoopMove *candidates, gsize count, const BoopMove *candidate) {
  g_return_val_if_fail(candidate != NULL, FALSE);

  for (gsize i = 0; i < count; ++i) {
    if (boop_moves_equal(&candidates[i], candidate) && candidates[i].path_length == candidate->path_length &&
        memcmp(candidates[i].path, candidate->path, candidate->path_length * sizeof(candidate->path[0])) == 0) {
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean boop_builder_append_candidate(BoopMove **candidates,
                                              gsize *count,
                                              gsize *capacity,
                                              const BoopMove *candidate) {
  g_return_val_if_fail(candidates != NULL, FALSE);
  g_return_val_if_fail(count != NULL, FALSE);
  g_return_val_if_fail(capacity != NULL, FALSE);
  g_return_val_if_fail(candidate != NULL, FALSE);

  if (boop_builder_candidate_exists(*candidates, *count, candidate)) {
    return TRUE;
  }

  if (*count == *capacity) {
    gsize next_capacity = *capacity == 0 ? 16 : *capacity * 2;
    BoopMove *next_candidates = g_realloc_n(*candidates, next_capacity, sizeof(*next_candidates));
    g_return_val_if_fail(next_candidates != NULL, FALSE);
    *candidates = next_candidates;
    *capacity = next_capacity;
  }

  (*candidates)[*count] = *candidate;
  (*count)++;
  return TRUE;
}

static gboolean boop_builder_selected_mask_can_continue(const BoopMoveBuilderState *state, guint64 mask) {
  g_return_val_if_fail(state != NULL, FALSE);

  if (!state->promotion_mandatory) {
    if (mask == 0) {
      return TRUE;
    }
    for (guint i = 0; i < state->promotion_option_count; ++i) {
      if (state->promotion_options[i] == mask) {
        return TRUE;
      }
    }
    return FALSE;
  }

  for (guint i = 0; i < state->promotion_option_count; ++i) {
    if ((state->promotion_options[i] & mask) == mask) {
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean boop_builder_selected_mask_is_complete(const BoopMoveBuilderState *state, guint64 mask) {
  g_return_val_if_fail(state != NULL, FALSE);

  if (!state->promotion_mandatory) {
    if (mask == 0) {
      return TRUE;
    }
    for (guint i = 0; i < state->promotion_option_count; ++i) {
      if (state->promotion_options[i] == mask) {
        return TRUE;
      }
    }
    return FALSE;
  }

  for (guint i = 0; i < state->promotion_option_count; ++i) {
    if (state->promotion_options[i] == mask) {
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean boop_builder_list_promotion_candidates(const BoopMoveBuilderState *state,
                                                       BoopMove **candidates,
                                                       gsize *count,
                                                       gsize *capacity) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(candidates != NULL, FALSE);
  g_return_val_if_fail(count != NULL, FALSE);
  g_return_val_if_fail(capacity != NULL, FALSE);

  for (guint i = 0; i < state->promotion_option_count; ++i) {
    guint64 option = state->promotion_options[i];
    if ((option & state->selected_mask) != state->selected_mask) {
      continue;
    }

    for (guint square = 0; square < BOOP_SQUARE_COUNT; ++square) {
      guint64 square_mask = boop_square_mask(square);
      if ((option & square_mask) == 0 || (state->selected_mask & square_mask) != 0) {
        continue;
      }

      BoopMove candidate = state->move;
      candidate.promotion_mask = state->selected_mask | square_mask;
      candidate.path_length = state->selection_path_length;
      memcpy(candidate.path, state->selection_path, sizeof(candidate.path));
      if (!boop_move_path_append_square(&candidate, square) ||
          !boop_builder_append_candidate(candidates, count, capacity, &candidate)) {
        return FALSE;
      }
    }
  }
  return TRUE;
}

static gboolean boop_builder_choice_copy_options(BoopMoveBuilderState *state, const BoopPromotionChoices *choices) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(choices != NULL, FALSE);
  g_return_val_if_fail(choices->count <= G_N_ELEMENTS(state->promotion_options), FALSE);

  memcpy(state->promotion_options, choices->masks, choices->count * sizeof(choices->masks[0]));
  state->promotion_option_count = choices->count;
  state->promotion_mandatory = choices->mandatory;
  state->selection_path_length = 0;
  memset(state->selection_path, 0, sizeof(state->selection_path));
  return TRUE;
}

void boop_position_init(BoopPosition *position) {
  g_return_if_fail(position != NULL);

  memset(position, 0, sizeof(*position));
  position->kittens_in_supply[0] = BOOP_SUPPLY_COUNT;
  position->kittens_in_supply[1] = BOOP_SUPPLY_COUNT;
  position->turn = 0;
  position->outcome = GAME_BACKEND_OUTCOME_ONGOING;
}

void boop_position_clear(BoopPosition *position) {
  g_return_if_fail(position != NULL);

  memset(position, 0, sizeof(*position));
}

void boop_position_copy(BoopPosition *dest, const BoopPosition *src) {
  g_return_if_fail(dest != NULL);
  g_return_if_fail(src != NULL);

  *dest = *src;
}

gboolean boop_position_normalize(BoopPosition *position, GError **error) {
  guint kittens_on_board[2] = {0};
  guint cats_on_board[2] = {0};

  g_return_val_if_fail(position != NULL, FALSE);

  if (!boop_side_valid(position->turn)) {
    g_set_error(error, boop_position_error_quark(), 1, "Invalid boop side to move: %u", position->turn);
    return FALSE;
  }

  for (guint square = 0; square < BOOP_SQUARE_COUNT; ++square) {
    BoopPiece piece = position->board[square];

    if (boop_piece_is_empty(piece)) {
      continue;
    }
    if (!boop_side_valid(piece.side)) {
      g_set_error(error,
                  boop_position_error_quark(),
                  2,
                  "Invalid boop side %u on square %u",
                  piece.side,
                  square);
      return FALSE;
    }
    if (!boop_piece_rank_valid(piece.rank)) {
      g_set_error(error,
                  boop_position_error_quark(),
                  3,
                  "Invalid boop rank %u on square %u",
                  piece.rank,
                  square);
      return FALSE;
    }

    switch ((BoopPieceRank)piece.rank) {
      case BOOP_PIECE_RANK_KITTEN:
        kittens_on_board[piece.side]++;
        break;
      case BOOP_PIECE_RANK_CAT:
        cats_on_board[piece.side]++;
        break;
      case BOOP_PIECE_RANK_NONE:
      default:
        g_set_error(error,
                    boop_position_error_quark(),
                    4,
                    "Unsupported boop rank %u on square %u",
                    piece.rank,
                    square);
        return FALSE;
    }
  }

  for (guint side = 0; side < 2; ++side) {
    guint total_pieces = kittens_on_board[side] + cats_on_board[side] +
                         position->kittens_in_supply[side] + position->cats_in_supply[side];

    if (total_pieces != BOOP_SUPPLY_COUNT) {
      g_set_error(error,
                  boop_position_error_quark(),
                  5,
                  "Invalid boop piece total for side %u: %u",
                  side,
                  total_pieces);
      return FALSE;
    }

    position->promoted_count[side] = (guint8)(cats_on_board[side] + position->cats_in_supply[side]);
  }

  gboolean side_0_wins = boop_position_has_cat_line(position, 0) ||
                         position->promoted_count[0] >= BOOP_SUPPLY_COUNT;
  gboolean side_1_wins = boop_position_has_cat_line(position, 1) ||
                         position->promoted_count[1] >= BOOP_SUPPLY_COUNT;

  if (side_0_wins && side_1_wins) {
    g_set_error_literal(error,
                        boop_position_error_quark(),
                        6,
                        "Invalid boop position: both sides satisfy win conditions");
    return FALSE;
  }

  if (side_0_wins) {
    position->outcome = GAME_BACKEND_OUTCOME_SIDE_0_WIN;
  } else if (side_1_wins) {
    position->outcome = GAME_BACKEND_OUTCOME_SIDE_1_WIN;
  } else {
    position->outcome = GAME_BACKEND_OUTCOME_ONGOING;
  }

  return TRUE;
}

GameBackendOutcome boop_position_outcome(const BoopPosition *position) {
  g_return_val_if_fail(position != NULL, GAME_BACKEND_OUTCOME_ONGOING);

  return (GameBackendOutcome)position->outcome;
}

guint boop_position_turn(const BoopPosition *position) {
  g_return_val_if_fail(position != NULL, 0);

  return position->turn;
}

GameBackendMoveList boop_position_list_moves(const BoopPosition *position) {
  g_return_val_if_fail(position != NULL, (GameBackendMoveList){0});

  if (position->outcome != GAME_BACKEND_OUTCOME_ONGOING) {
    return (GameBackendMoveList){0};
  }

  guint side = position->turn;
  GArray *moves = g_array_sized_new(FALSE, FALSE, sizeof(BoopMove), BOOP_SQUARE_COUNT);
  g_return_val_if_fail(moves != NULL, (GameBackendMoveList){0});

  for (guint square = 0; square < BOOP_SQUARE_COUNT; ++square) {
    if (!boop_piece_is_empty(position->board[square])) {
      continue;
    }

    if (position->kittens_in_supply[side] > 0 &&
        !boop_position_append_resolved_moves_for_placement(position,
                                                           side,
                                                           BOOP_PIECE_RANK_KITTEN,
                                                           square,
                                                           moves)) {
      g_array_free(moves, TRUE);
      return (GameBackendMoveList){0};
    }
    if (position->cats_in_supply[side] > 0 &&
        !boop_position_append_resolved_moves_for_placement(position, side, BOOP_PIECE_RANK_CAT, square, moves)) {
      g_array_free(moves, TRUE);
      return (GameBackendMoveList){0};
    }
  }

  gsize count = moves->len;
  gpointer data = g_array_free(moves, FALSE);
  return (GameBackendMoveList){
    .moves = data,
    .count = count,
  };
}

GameBackendMoveList boop_position_list_good_moves(const BoopPosition *position,
                                                  guint max_count,
                                                  guint /*depth_hint*/) {
  GameBackendMoveList all_moves = boop_position_list_moves(position);
  if (max_count == 0 || all_moves.count <= max_count) {
    return all_moves;
  }

  BoopMove *moves = g_new0(BoopMove, max_count);
  g_return_val_if_fail(moves != NULL, all_moves);
  memcpy(moves, all_moves.moves, max_count * sizeof(*moves));
  boop_move_list_free(&all_moves);
  return (GameBackendMoveList){
    .moves = moves,
    .count = max_count,
  };
}

void boop_move_list_free(GameBackendMoveList *moves) {
  g_return_if_fail(moves != NULL);

  g_clear_pointer(&moves->moves, g_free);
  moves->count = 0;
}

const BoopMove *boop_move_list_get(const GameBackendMoveList *moves, gsize index) {
  g_return_val_if_fail(moves != NULL, NULL);
  g_return_val_if_fail(index < moves->count, NULL);
  g_return_val_if_fail(moves->moves != NULL, NULL);

  const BoopMove *all_moves = moves->moves;
  return &all_moves[index];
}

gboolean boop_moves_equal(const BoopMove *left, const BoopMove *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  return left->square == right->square && left->rank == right->rank &&
         left->promotion_mask == right->promotion_mask;
}

gboolean boop_position_apply_move(BoopPosition *position, const BoopMove *move) {
  BoopPosition after_placement = {0};
  BoopPromotionChoices choices = {0};
  guint side = 0;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(position->outcome == GAME_BACKEND_OUTCOME_ONGOING, FALSE);
  g_return_val_if_fail(boop_side_valid(position->turn), FALSE);
  g_return_val_if_fail(boop_square_valid(move->square), FALSE);
  g_return_val_if_fail(boop_piece_rank_valid(move->rank), FALSE);

  side = position->turn;
  if (!boop_position_resolve_placement(position, side, move->rank, move->square, &after_placement, NULL) ||
      !boop_position_collect_turn_choices(&after_placement, side, &choices)) {
    return FALSE;
  }

  if (choices.count == 0) {
    if (move->promotion_mask != 0) {
      return FALSE;
    }
  } else if (choices.mandatory) {
    if (!boop_position_mask_is_choice(&choices, move->promotion_mask)) {
      return FALSE;
    }
  } else if (move->promotion_mask != 0 && !boop_position_mask_is_choice(&choices, move->promotion_mask)) {
    return FALSE;
  }

  if (!boop_position_apply_promotion_mask(&after_placement, side, move->promotion_mask, NULL)) {
    return FALSE;
  }
  if (boop_position_has_cat_line(&after_placement, side) || after_placement.promoted_count[side] >= BOOP_SUPPLY_COUNT) {
    after_placement.outcome = side == 0 ? GAME_BACKEND_OUTCOME_SIDE_0_WIN : GAME_BACKEND_OUTCOME_SIDE_1_WIN;
  } else {
    after_placement.turn = side == 0 ? 1 : 0;
    after_placement.outcome = GAME_BACKEND_OUTCOME_ONGOING;
  }

  *position = after_placement;
  return TRUE;
}

gint boop_position_evaluate_static(const BoopPosition *position) {
  gint score = 0;

  g_return_val_if_fail(position != NULL, 0);

  for (guint side = 0; side < 2; ++side) {
    gint side_score = ((gint)position->promoted_count[side] * 300) +
                      ((gint)position->cats_in_supply[side] * 180) +
                      ((gint)position->kittens_in_supply[side] * 20);
    for (guint square = 0; square < BOOP_SQUARE_COUNT; ++square) {
      BoopPiece piece = position->board[square];
      if (boop_piece_is_empty(piece) || piece.side != side) {
        continue;
      }

      guint row = 0;
      guint col = 0;
      (void)boop_square_to_coord(square, &row, &col);
      gint center_bonus = 6 - ABS((gint)row - 2) - ABS((gint)col - 2);
      side_score += piece.rank == BOOP_PIECE_RANK_CAT ? 240 : 100;
      side_score += MAX(center_bonus, 0);
    }
    if (boop_position_has_cat_line(position, side)) {
      side_score += 10000;
    }

    score += side == 0 ? side_score : -side_score;
  }

  return score;
}

gint boop_position_terminal_score(GameBackendOutcome outcome, guint ply_depth) {
  gint win_score = 100000 - (gint)ply_depth;

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

guint64 boop_position_hash(const BoopPosition *position) {
  guint64 hash = G_GUINT64_CONSTANT(1469598103934665603);

  g_return_val_if_fail(position != NULL, 0);

  for (guint square = 0; square < BOOP_SQUARE_COUNT; ++square) {
    guint8 token = (guint8)(position->board[square].rank | (position->board[square].side << 2));
    hash ^= token + 1;
    hash *= G_GUINT64_CONSTANT(1099511628211);
  }

  for (guint side = 0; side < 2; ++side) {
    hash ^= position->kittens_in_supply[side] + 17u;
    hash *= G_GUINT64_CONSTANT(1099511628211);
    hash ^= position->cats_in_supply[side] + 31u;
    hash *= G_GUINT64_CONSTANT(1099511628211);
    hash ^= position->promoted_count[side] + 47u;
    hash *= G_GUINT64_CONSTANT(1099511628211);
  }
  hash ^= position->turn + 61u;
  hash *= G_GUINT64_CONSTANT(1099511628211);
  hash ^= position->outcome + 67u;
  return hash;
}

gboolean boop_square_to_coord(guint square, guint *out_row, guint *out_col) {
  g_return_val_if_fail(out_row != NULL, FALSE);
  g_return_val_if_fail(out_col != NULL, FALSE);

  if (!boop_square_valid(square)) {
    return FALSE;
  }

  *out_row = square / BOOP_BOARD_SIZE;
  *out_col = square % BOOP_BOARD_SIZE;
  return TRUE;
}

gboolean boop_coord_to_square(guint row, guint col, guint *out_square) {
  g_return_val_if_fail(out_square != NULL, FALSE);

  if (row >= BOOP_BOARD_SIZE || col >= BOOP_BOARD_SIZE) {
    return FALSE;
  }

  *out_square = row * BOOP_BOARD_SIZE + col;
  return TRUE;
}

static gboolean boop_square_format(guint square, char out_coord[3]) {
  guint row = 0;
  guint col = 0;

  g_return_val_if_fail(out_coord != NULL, FALSE);

  if (!boop_square_to_coord(square, &row, &col)) {
    return FALSE;
  }

  out_coord[0] = (char)('a' + col);
  out_coord[1] = (char)('1' + row);
  out_coord[2] = '\0';
  return TRUE;
}

static gboolean boop_square_parse(const char *text, guint *out_square) {
  g_return_val_if_fail(text != NULL, FALSE);
  g_return_val_if_fail(out_square != NULL, FALSE);

  if (!g_ascii_isalpha(text[0]) || !g_ascii_isdigit(text[1])) {
    return FALSE;
  }

  gint col = g_ascii_tolower(text[0]) - 'a';
  gint row = text[1] - '1';
  return boop_coord_to_square_signed(row, col, out_square);
}

gboolean boop_move_format(const BoopMove *move, char *buffer, gsize size) {
  char coord[3] = {0};

  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(size > 0, FALSE);
  g_return_val_if_fail(boop_square_valid(move->square), FALSE);
  g_return_val_if_fail(boop_piece_rank_valid(move->rank), FALSE);

  if (!boop_square_format(move->square, coord)) {
    return FALSE;
  }

  GString *notation = g_string_new(NULL);
  g_return_val_if_fail(notation != NULL, FALSE);
  g_string_append_printf(notation, "%c@%s", move->rank == BOOP_PIECE_RANK_CAT ? 'C' : 'K', coord);

  if (move->promotion_mask != 0) {
    gboolean first = TRUE;
    g_string_append_c(notation, '+');
    for (guint square = 0; square < BOOP_SQUARE_COUNT; ++square) {
      if ((move->promotion_mask & boop_square_mask(square)) == 0) {
        continue;
      }

      if (!first) {
        g_string_append_c(notation, ',');
      }
      first = FALSE;
      if (!boop_square_format(square, coord)) {
        g_string_free(notation, TRUE);
        return FALSE;
      }
      g_string_append(notation, coord);
    }
  }

  gboolean fits = notation->len < size;
  if (fits) {
    memcpy(buffer, notation->str, notation->len + 1);
  }
  g_string_free(notation, TRUE);
  return fits;
}

gboolean boop_move_parse(const char *notation, BoopMove *out_move) {
  g_return_val_if_fail(notation != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  if ((notation[0] != 'K' && notation[0] != 'C') || notation[1] != '@') {
    return FALSE;
  }

  BoopMove move = {
    .rank = notation[0] == 'C' ? BOOP_PIECE_RANK_CAT : BOOP_PIECE_RANK_KITTEN,
  };
  guint square = 0;
  if (!boop_square_parse(notation + 2, &square)) {
    return FALSE;
  }
  move.square = (guint8)square;

  const char *cursor = notation + 4;
  if (*cursor == '\0') {
    *out_move = move;
    return TRUE;
  }
  if (*cursor != '+') {
    return FALSE;
  }
  cursor++;

  while (*cursor != '\0') {
    guint square = 0;
    if (!boop_square_parse(cursor, &square)) {
      return FALSE;
    }
    move.promotion_mask |= boop_square_mask(square);
    cursor += 2;
    if (*cursor == ',') {
      cursor++;
      continue;
    }
    if (*cursor != '\0') {
      return FALSE;
    }
  }

  *out_move = move;
  return TRUE;
}

gboolean boop_move_get_path(const BoopMove *move, guint *out_length, guint *out_indices, gsize max_indices) {
  guint length = 0;

  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(out_length != NULL, FALSE);

  length = move->path_length > 0 ? move->path_length : 1 + boop_mask_popcount(move->promotion_mask);
  if (out_indices != NULL && length > max_indices) {
    return FALSE;
  }

  *out_length = length;
  if (out_indices == NULL) {
    return TRUE;
  }

  if (move->path_length > 0) {
    for (guint i = 0; i < move->path_length; ++i) {
      out_indices[i] = move->path[i];
    }
    return TRUE;
  }

  out_indices[0] = move->square;
  guint path_index = 1;
  for (guint square = 0; square < BOOP_SQUARE_COUNT; ++square) {
    if ((move->promotion_mask & boop_square_mask(square)) != 0) {
      out_indices[path_index++] = square;
    }
  }
  return TRUE;
}

gboolean boop_move_describe_overlay(const BoopPosition *position,
                                    const BoopMove *move,
                                    BoopMoveOverlayInfo *out_info) {
  BoopPosition after_placement = {0};
  BoopPromotionChoices choices = {0};

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(out_info != NULL, FALSE);
  g_return_val_if_fail(position->outcome == GAME_BACKEND_OUTCOME_ONGOING, FALSE);
  g_return_val_if_fail(boop_side_valid(position->turn), FALSE);
  g_return_val_if_fail(boop_square_valid(move->square), FALSE);
  g_return_val_if_fail(boop_piece_rank_valid(move->rank), FALSE);

  memset(out_info, 0, sizeof(*out_info));
  out_info->placed_square = move->square;
  if (!boop_position_resolve_placement(position,
                                       position->turn,
                                       move->rank,
                                       move->square,
                                       &after_placement,
                                       out_info)) {
    return FALSE;
  }
  if (!boop_position_collect_turn_choices(&after_placement, position->turn, &choices)) {
    return FALSE;
  }
  if (choices.count == 0) {
    return move->promotion_mask == 0;
  }
  if (choices.mandatory) {
    if (!boop_position_mask_is_choice(&choices, move->promotion_mask)) {
      return FALSE;
    }
  } else if (move->promotion_mask != 0 && !boop_position_mask_is_choice(&choices, move->promotion_mask)) {
    return FALSE;
  }

  return boop_position_apply_promotion_mask(&after_placement, position->turn, move->promotion_mask, out_info);
}

gboolean boop_move_builder_init(const BoopPosition *position, GameBackendMoveBuilder *out_builder) {
  BoopMoveBuilderState *state = NULL;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(out_builder != NULL, FALSE);

  if (position->outcome != GAME_BACKEND_OUTCOME_ONGOING) {
    return FALSE;
  }

  state = g_new0(BoopMoveBuilderState, 1);
  g_return_val_if_fail(state != NULL, FALSE);

  state->position = *position;
  state->stage = BOOP_MOVE_BUILDER_STAGE_PLACEMENT;
  out_builder->builder_state = state;
  out_builder->builder_state_size = sizeof(*state);
  return TRUE;
}

void boop_move_builder_clear(GameBackendMoveBuilder *builder) {
  g_return_if_fail(builder != NULL);

  g_clear_pointer(&builder->builder_state, g_free);
  builder->builder_state_size = 0;
}

GameBackendMoveList boop_move_builder_list_candidates(const GameBackendMoveBuilder *builder) {
  const BoopMoveBuilderState *state = NULL;
  BoopMove *candidates = NULL;
  gsize count = 0;
  gsize capacity = 0;

  g_return_val_if_fail(builder != NULL, (GameBackendMoveList){0});

  state = builder->builder_state;
  g_return_val_if_fail(state != NULL, (GameBackendMoveList){0});

  if (state->stage == BOOP_MOVE_BUILDER_STAGE_COMPLETE) {
    return (GameBackendMoveList){0};
  }

  if (state->stage == BOOP_MOVE_BUILDER_STAGE_PROMOTION) {
    if (!boop_builder_list_promotion_candidates(state, &candidates, &count, &capacity)) {
      g_free(candidates);
      return (GameBackendMoveList){0};
    }
    return (GameBackendMoveList){
      .moves = candidates,
      .count = count,
    };
  }

  guint side = state->position.turn;
  for (guint square = 0; square < BOOP_SQUARE_COUNT; ++square) {
    if (!boop_piece_is_empty(state->position.board[square])) {
      continue;
    }

    for (guint rank = BOOP_PIECE_RANK_KITTEN; rank <= BOOP_PIECE_RANK_CAT; ++rank) {
      if (!boop_position_has_supply_for_rank(&state->position, side, rank)) {
        continue;
      }

      BoopMove candidate = {
        .square = (guint8)square,
        .rank = (guint8)rank,
      };
      if (!boop_move_path_append_square(&candidate, square) ||
          !boop_builder_append_candidate(&candidates, &count, &capacity, &candidate)) {
        g_free(candidates);
        return (GameBackendMoveList){0};
      }
    }
  }

  return (GameBackendMoveList){
    .moves = candidates,
    .count = count,
  };
}

gboolean boop_move_builder_step(GameBackendMoveBuilder *builder, const BoopMove *candidate) {
  BoopMoveBuilderState *state = NULL;
  BoopPromotionChoices choices = {0};
  guint side = 0;

  g_return_val_if_fail(builder != NULL, FALSE);
  g_return_val_if_fail(candidate != NULL, FALSE);

  state = builder->builder_state;
  g_return_val_if_fail(state != NULL, FALSE);

  if (state->stage == BOOP_MOVE_BUILDER_STAGE_COMPLETE) {
    return FALSE;
  }

  if (state->stage == BOOP_MOVE_BUILDER_STAGE_PROMOTION) {
    if (!boop_builder_selected_mask_can_continue(state, candidate->promotion_mask)) {
      return FALSE;
    }
    if (candidate->path_length > G_N_ELEMENTS(state->selection_path)) {
      return FALSE;
    }

    state->selected_mask = candidate->promotion_mask;
    state->selection_path_length = candidate->path_length;
    memcpy(state->selection_path, candidate->path, sizeof(state->selection_path));
    state->move.promotion_mask = state->selected_mask;
    if (!boop_move_set_path_from_mask(&state->move, state->move.promotion_mask)) {
      return FALSE;
    }

    if (boop_builder_selected_mask_is_complete(state, state->selected_mask)) {
      state->stage = BOOP_MOVE_BUILDER_STAGE_COMPLETE;
    }
    return TRUE;
  }

  side = state->position.turn;
  if (candidate->path_length != 1 || !boop_square_valid(candidate->square) ||
      !boop_position_has_supply_for_rank(&state->position, side, candidate->rank)) {
    return FALSE;
  }

  if (!boop_position_resolve_placement(&state->position,
                                       side,
                                       candidate->rank,
                                       candidate->square,
                                       &state->after_placement,
                                       NULL) ||
      !boop_position_collect_turn_choices(&state->after_placement, side, &choices)) {
    return FALSE;
  }

  state->move = *candidate;
  state->selected_mask = 0;
  state->selection_path_length = 0;
  memset(state->selection_path, 0, sizeof(state->selection_path));
  if (choices.count == 0) {
    state->stage = BOOP_MOVE_BUILDER_STAGE_COMPLETE;
    return TRUE;
  }

  if (choices.mandatory && choices.count == 1) {
    state->move.promotion_mask = choices.masks[0];
    if (!boop_move_set_path_from_mask(&state->move, choices.masks[0])) {
      return FALSE;
    }
    state->stage = BOOP_MOVE_BUILDER_STAGE_COMPLETE;
    return TRUE;
  }

  if (!boop_builder_choice_copy_options(state, &choices)) {
    return FALSE;
  }
  state->stage = BOOP_MOVE_BUILDER_STAGE_PROMOTION;
  return TRUE;
}

gboolean boop_move_builder_is_complete(const GameBackendMoveBuilder *builder) {
  const BoopMoveBuilderState *state = NULL;

  g_return_val_if_fail(builder != NULL, FALSE);

  state = builder->builder_state;
  g_return_val_if_fail(state != NULL, FALSE);
  return state->stage == BOOP_MOVE_BUILDER_STAGE_COMPLETE;
}

gboolean boop_move_builder_build_move(const GameBackendMoveBuilder *builder, BoopMove *out_move) {
  const BoopMoveBuilderState *state = NULL;

  g_return_val_if_fail(builder != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  state = builder->builder_state;
  g_return_val_if_fail(state != NULL, FALSE);
  if (state->stage != BOOP_MOVE_BUILDER_STAGE_COMPLETE) {
    return FALSE;
  }

  *out_move = state->move;
  return TRUE;
}
