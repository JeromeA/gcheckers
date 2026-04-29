#ifndef BOOP_TYPES_H
#define BOOP_TYPES_H

#include "../../game_backend.h"

#include <glib.h>

#define BOOP_BOARD_SIZE 6
#define BOOP_SQUARE_COUNT (BOOP_BOARD_SIZE * BOOP_BOARD_SIZE)
#define BOOP_SUPPLY_COUNT 8
#define BOOP_INVALID_SQUARE 255
#define BOOP_MOVE_PATH_MAX 8
#define BOOP_PROMOTION_OPTION_MAX 64

typedef enum {
  BOOP_PIECE_RANK_NONE = 0,
  BOOP_PIECE_RANK_KITTEN = 1,
  BOOP_PIECE_RANK_CAT = 2,
} BoopPieceRank;

typedef struct {
  guint8 side;
  guint8 rank;
} BoopPiece;

typedef struct {
  BoopPiece board[BOOP_SQUARE_COUNT];
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
