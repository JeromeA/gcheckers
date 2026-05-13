#ifndef BOOP_TYPES_H
#define BOOP_TYPES_H

#include "../../game_backend.h"

#include <glib.h>

#define BOOP_BOARD_SIZE 6
#define BOOP_SQUARE_COUNT (BOOP_BOARD_SIZE * BOOP_BOARD_SIZE)
#define BOOP_MASK_ROW_STRIDE 8u
#define BOOP_BOARD_MASK G_GUINT64_CONSTANT(0x00003f3f3f3f3f3f)
#define BOOP_SQUARE_BIT(square) ((((guint)(square) / BOOP_BOARD_SIZE) * BOOP_MASK_ROW_STRIDE) + \
                                 ((guint)(square) % BOOP_BOARD_SIZE))
#define BOOP_SQUARE_MASK(square) (G_GUINT64_CONSTANT(1) << BOOP_SQUARE_BIT(square))
#define BOOP_SUPPLY_COUNT 8
#define BOOP_INVALID_SQUARE 255
#define BOOP_MOVE_PATH_MAX 8
#define BOOP_PROMOTION_OPTION_MAX 14

typedef enum {
  BOOP_PIECE_RANK_NONE = 0,
  BOOP_PIECE_RANK_KITTEN = 1,
  BOOP_PIECE_RANK_CAT = 2,
} BoopPieceRank;

typedef guint8 BoopPiece;

typedef enum {
  BOOP_PIECE_SIDE_0_KITTEN = 0,
  BOOP_PIECE_SIDE_0_CAT = 1,
  BOOP_PIECE_SIDE_1_KITTEN = 2,
  BOOP_PIECE_SIDE_1_CAT = 3,
  BOOP_PIECE_EMPTY = 4,
} BoopPieceValue;

static inline gboolean boop_piece_valid(BoopPiece piece) {
  return piece <= BOOP_PIECE_EMPTY;
}

#define boop_piece_side(piece) ((guint)(piece) / 2u)

static inline BoopPiece boop_piece_make(guint side, guint rank) {
  if (rank == BOOP_PIECE_RANK_NONE) {
    return BOOP_PIECE_EMPTY;
  }

  g_return_val_if_fail(side < 2, BOOP_PIECE_EMPTY);
  g_return_val_if_fail(rank == BOOP_PIECE_RANK_KITTEN || rank == BOOP_PIECE_RANK_CAT, BOOP_PIECE_EMPTY);

  return (BoopPiece)((side * 2) + (rank - 1));
}

static inline guint boop_piece_rank(BoopPiece piece) {
  g_return_val_if_fail(boop_piece_valid(piece), BOOP_PIECE_RANK_NONE);

  if (piece == BOOP_PIECE_EMPTY) {
    return BOOP_PIECE_RANK_NONE;
  }

  return (piece % 2) + 1;
}

typedef struct {
  guint64 side_mask[2];
  guint64 cat_mask[2];
  guint64 occupied_mask;
  guint8 kittens_in_supply[2];
  guint8 cats_in_supply[2];
  guint8 promoted_count[2];
  guint8 turn;
  guint8 outcome;
} BoopPosition;

typedef struct {
  guint8 square;
  guint8 rank;
  guint64 promotion_mask;
  guint8 path_length;
  guint8 path[BOOP_MOVE_PATH_MAX];
} BoopMove;

typedef enum {
  BOOP_MOVE_BUILDER_STAGE_PLACEMENT = 0,
  BOOP_MOVE_BUILDER_STAGE_PROMOTION,
  BOOP_MOVE_BUILDER_STAGE_COMPLETE,
} BoopMoveBuilderStage;

typedef struct {
  BoopPosition position;
  BoopPosition after_placement;
  BoopMove move;
  guint64 selected_mask;
  guint8 selection_path_length;
  guint8 selection_path[BOOP_MOVE_PATH_MAX];
  guint64 promotion_options[BOOP_PROMOTION_OPTION_MAX];
  guint promotion_option_count;
  gboolean promotion_mandatory;
  guint8 stage;
} BoopMoveBuilderState;

#endif
