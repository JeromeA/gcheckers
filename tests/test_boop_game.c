#include <assert.h>
#include <string.h>

#include "../src/games/boop/boop_game.h"

static guint square_at(guint row, guint col) {
  guint square = 0;

  assert(boop_coord_to_square(row, col, &square));
  return square;
}

static guint64 square_mask(guint row, guint col) {
  return G_GUINT64_CONSTANT(1) << square_at(row, col);
}

static void setup_piece(BoopPosition *position, guint side, guint rank, guint row, guint col) {
  guint square = square_at(row, col);

  assert(position != NULL);
  assert(side < 2);
  assert(rank == BOOP_PIECE_RANK_KITTEN || rank == BOOP_PIECE_RANK_CAT);
  assert(position->board[square].rank == BOOP_PIECE_RANK_NONE);

  position->board[square] = (BoopPiece){
    .side = (guint8)side,
    .rank = (guint8)rank,
  };
  if (rank == BOOP_PIECE_RANK_KITTEN) {
    assert(position->kittens_in_supply[side] > 0);
    position->kittens_in_supply[side]--;
  } else {
    assert(position->cats_in_supply[side] > 0);
    position->cats_in_supply[side]--;
  }
}

static void test_initial_move_list_and_notation(void) {
  BoopPosition position = {0};
  BoopMove parsed = {0};
  char notation[32] = {0};

  boop_position_init(&position);
  assert(boop_position_turn(&position) == 0);
  assert(boop_position_outcome(&position) == GAME_BACKEND_OUTCOME_ONGOING);

  GameBackendMoveList moves = boop_position_list_moves(&position);
  assert(moves.count == BOOP_SQUARE_COUNT);
  const BoopMove *move = boop_move_list_get(&moves, 0);
  assert(move != NULL);
  assert(move->rank == BOOP_PIECE_RANK_KITTEN);
  assert(boop_move_format(move, notation, sizeof(notation)));
  assert(strcmp(notation, "K@a1") == 0);
  assert(boop_move_parse(notation, &parsed));
  assert(boop_moves_equal(move, &parsed));
  boop_move_list_free(&moves);
}

static void test_kitten_boops_kittens_not_cats(void) {
  BoopPosition position = {0};
  BoopMove move = {
    .square = (guint8)square_at(2, 2),
    .rank = BOOP_PIECE_RANK_KITTEN,
  };

  boop_position_init(&position);
  position.cats_in_supply[1] = 1;
  setup_piece(&position, 1, BOOP_PIECE_RANK_KITTEN, 1, 2);
  setup_piece(&position, 1, BOOP_PIECE_RANK_CAT, 2, 3);

  assert(boop_position_apply_move(&position, &move));
  assert(position.board[square_at(1, 2)].rank == BOOP_PIECE_RANK_NONE);
  assert(position.board[square_at(0, 2)].rank == BOOP_PIECE_RANK_KITTEN);
  assert(position.board[square_at(2, 3)].rank == BOOP_PIECE_RANK_CAT);
  assert(position.turn == 1);
}

static void test_booped_off_piece_returns_to_supply(void) {
  BoopPosition position = {0};
  BoopMove move = {
    .square = (guint8)square_at(1, 1),
    .rank = BOOP_PIECE_RANK_KITTEN,
  };

  boop_position_init(&position);
  setup_piece(&position, 1, BOOP_PIECE_RANK_KITTEN, 0, 0);
  assert(position.kittens_in_supply[1] == 7);

  assert(boop_position_apply_move(&position, &move));
  assert(position.board[square_at(0, 0)].rank == BOOP_PIECE_RANK_NONE);
  assert(position.kittens_in_supply[1] == 8);
}

static void test_kitten_line_promotes_to_cats(void) {
  BoopPosition position = {0};
  BoopMove move = {
    .square = (guint8)square_at(0, 0),
    .rank = BOOP_PIECE_RANK_KITTEN,
    .promotion_mask = square_mask(0, 0) | square_mask(0, 1) | square_mask(0, 2),
  };

  boop_position_init(&position);
  setup_piece(&position, 0, BOOP_PIECE_RANK_KITTEN, 0, 1);
  setup_piece(&position, 0, BOOP_PIECE_RANK_KITTEN, 0, 2);

  assert(boop_position_apply_move(&position, &move));
  assert(position.board[square_at(0, 0)].rank == BOOP_PIECE_RANK_NONE);
  assert(position.board[square_at(0, 1)].rank == BOOP_PIECE_RANK_NONE);
  assert(position.board[square_at(0, 2)].rank == BOOP_PIECE_RANK_NONE);
  assert(position.cats_in_supply[0] == 3);
  assert(position.promoted_count[0] == 3);
}

static void test_overlong_line_has_multiple_promotion_moves(void) {
  BoopPosition position = {0};
  guint64 first_mask = square_mask(0, 0) | square_mask(0, 1) | square_mask(0, 2);
  guint64 second_mask = square_mask(0, 1) | square_mask(0, 2) | square_mask(0, 3);
  gboolean found_first = FALSE;
  gboolean found_second = FALSE;

  boop_position_init(&position);
  setup_piece(&position, 0, BOOP_PIECE_RANK_KITTEN, 0, 1);
  setup_piece(&position, 0, BOOP_PIECE_RANK_KITTEN, 0, 2);
  setup_piece(&position, 0, BOOP_PIECE_RANK_KITTEN, 0, 3);

  GameBackendMoveList moves = boop_position_list_moves(&position);
  for (gsize i = 0; i < moves.count; ++i) {
    const BoopMove *move = boop_move_list_get(&moves, i);
    assert(move != NULL);
    if (move->square != square_at(0, 0)) {
      continue;
    }
    found_first = found_first || move->promotion_mask == first_mask;
    found_second = found_second || move->promotion_mask == second_mask;
  }

  assert(found_first);
  assert(found_second);
  boop_move_list_free(&moves);
}

static void test_graduation_can_promote_one_kitten(void) {
  BoopPosition position = {0};
  BoopMove move = {
    .square = (guint8)square_at(5, 5),
    .rank = BOOP_PIECE_RANK_KITTEN,
    .promotion_mask = square_mask(5, 5),
  };

  boop_position_init(&position);
  setup_piece(&position, 0, BOOP_PIECE_RANK_KITTEN, 0, 0);
  setup_piece(&position, 0, BOOP_PIECE_RANK_KITTEN, 0, 3);
  setup_piece(&position, 0, BOOP_PIECE_RANK_KITTEN, 1, 5);
  setup_piece(&position, 0, BOOP_PIECE_RANK_KITTEN, 2, 1);
  setup_piece(&position, 0, BOOP_PIECE_RANK_KITTEN, 3, 3);
  setup_piece(&position, 0, BOOP_PIECE_RANK_KITTEN, 4, 0);
  setup_piece(&position, 0, BOOP_PIECE_RANK_KITTEN, 5, 2);

  assert(boop_position_apply_move(&position, &move));
  assert(position.board[square_at(5, 5)].rank == BOOP_PIECE_RANK_NONE);
  assert(position.cats_in_supply[0] == 1);
  assert(position.promoted_count[0] == 1);
}

static void test_three_cats_win(void) {
  BoopPosition position = {0};
  BoopMove move = {
    .square = (guint8)square_at(0, 0),
    .rank = BOOP_PIECE_RANK_CAT,
  };

  boop_position_init(&position);
  position.promoted_count[0] = 3;
  position.cats_in_supply[0] = 3;
  setup_piece(&position, 0, BOOP_PIECE_RANK_CAT, 0, 1);
  setup_piece(&position, 0, BOOP_PIECE_RANK_CAT, 0, 2);

  assert(boop_position_apply_move(&position, &move));
  assert(boop_position_outcome(&position) == GAME_BACKEND_OUTCOME_SIDE_0_WIN);
}

static void test_builder_selects_promotion_squares(void) {
  BoopPosition position = {0};
  GameBackendMoveBuilder builder = {0};
  BoopMove move = {0};

  boop_position_init(&position);
  setup_piece(&position, 0, BOOP_PIECE_RANK_KITTEN, 0, 1);
  setup_piece(&position, 0, BOOP_PIECE_RANK_KITTEN, 0, 2);
  setup_piece(&position, 0, BOOP_PIECE_RANK_KITTEN, 0, 3);

  assert(boop_move_builder_init(&position, &builder));
  GameBackendMoveList candidates = boop_move_builder_list_candidates(&builder);
  const BoopMove *candidate = NULL;
  for (gsize i = 0; i < candidates.count; ++i) {
    const BoopMove *current = boop_move_list_get(&candidates, i);
    assert(current != NULL);
    if (current->square == square_at(0, 0)) {
      candidate = current;
      break;
    }
  }
  assert(candidate != NULL);
  assert(boop_move_builder_step(&builder, candidate));
  boop_move_list_free(&candidates);
  assert(!boop_move_builder_is_complete(&builder));

  while (!boop_move_builder_is_complete(&builder)) {
    candidates = boop_move_builder_list_candidates(&builder);
    assert(candidates.count > 0);
    candidate = boop_move_list_get(&candidates, 0);
    assert(candidate != NULL);
    assert(boop_move_builder_step(&builder, candidate));
    boop_move_list_free(&candidates);
  }

  assert(boop_move_builder_build_move(&builder, &move));
  assert(move.square == square_at(0, 0));
  assert(boop_move_builder_is_complete(&builder));
  assert((move.promotion_mask & square_mask(0, 0)) != 0);
  assert(boop_position_apply_move(&position, &move));
  boop_move_builder_clear(&builder);
}

static void test_overlay_describes_on_board_boop(void) {
  BoopPosition position = {0};
  BoopMove move = {
    .square = (guint8)square_at(2, 2),
    .rank = BOOP_PIECE_RANK_KITTEN,
  };
  BoopMoveOverlayInfo overlay = {0};

  boop_position_init(&position);
  position.cats_in_supply[1] = 1;
  setup_piece(&position, 1, BOOP_PIECE_RANK_KITTEN, 1, 2);
  setup_piece(&position, 1, BOOP_PIECE_RANK_CAT, 2, 3);

  assert(boop_move_describe_overlay(&position, &move, &overlay));
  assert(overlay.placed_square == square_at(2, 2));
  assert(overlay.arrow_count == 1);
  assert(overlay.arrows[0].from_square == square_at(1, 2));
  assert(overlay.arrows[0].to_square == square_at(0, 2));
  assert(overlay.arrows[0].row_delta == -1);
  assert(overlay.arrows[0].col_delta == 0);
  assert(!overlay.arrows[0].leaves_board);
}

static void test_overlay_describes_off_board_boop(void) {
  BoopPosition position = {0};
  BoopMove move = {
    .square = (guint8)square_at(1, 1),
    .rank = BOOP_PIECE_RANK_KITTEN,
  };
  BoopMoveOverlayInfo overlay = {0};

  boop_position_init(&position);
  setup_piece(&position, 1, BOOP_PIECE_RANK_KITTEN, 0, 0);

  assert(boop_move_describe_overlay(&position, &move, &overlay));
  assert(overlay.placed_square == square_at(1, 1));
  assert(overlay.arrow_count == 1);
  assert(overlay.arrows[0].from_square == square_at(0, 0));
  assert(overlay.arrows[0].to_square == BOOP_INVALID_SQUARE);
  assert(overlay.arrows[0].row_delta == -1);
  assert(overlay.arrows[0].col_delta == -1);
  assert(overlay.arrows[0].leaves_board);
  assert(overlay.removed_square_count == 1);
  assert(overlay.removed_squares[0] == square_at(0, 0));
}

static void test_overlay_describes_promoted_kittens_as_removed(void) {
  BoopPosition position = {0};
  BoopMove move = {
    .square = (guint8) square_at(0, 0),
    .rank = BOOP_PIECE_RANK_KITTEN,
    .promotion_mask = square_mask(0, 0) | square_mask(0, 1) | square_mask(0, 2),
  };
  BoopMoveOverlayInfo overlay = {0};

  boop_position_init(&position);
  setup_piece(&position, 0, BOOP_PIECE_RANK_KITTEN, 0, 1);
  setup_piece(&position, 0, BOOP_PIECE_RANK_KITTEN, 0, 2);

  assert(boop_move_describe_overlay(&position, &move, &overlay));
  assert(overlay.removed_square_count == 3);
  assert(overlay.removed_squares[0] == square_at(0, 0));
  assert(overlay.removed_squares[1] == square_at(0, 1));
  assert(overlay.removed_squares[2] == square_at(0, 2));
}

int main(void) {
  test_initial_move_list_and_notation();
  test_kitten_boops_kittens_not_cats();
  test_booped_off_piece_returns_to_supply();
  test_kitten_line_promotes_to_cats();
  test_overlong_line_has_multiple_promotion_moves();
  test_graduation_can_promote_one_kitten();
  test_three_cats_win();
  test_builder_selects_promotion_squares();
  test_overlay_describes_on_board_boop();
  test_overlay_describes_off_board_boop();
  test_overlay_describes_promoted_kittens_as_removed();

  return 0;
}
