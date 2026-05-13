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

static void assert_square_empty(const BoopPosition *position, guint row, guint col) {
  assert(position != NULL);
  assert(position->board[square_at(row, col)] == BOOP_PIECE_EMPTY);
}

static void assert_square_rank(const BoopPosition *position, guint row, guint col, guint rank) {
  assert(position != NULL);
  assert(boop_piece_rank(position->board[square_at(row, col)]) == rank);
}

static gboolean square_at_signed(gint row, gint col, guint *out_square) {
  assert(out_square != NULL);

  if (row < 0 || col < 0 || row >= BOOP_BOARD_SIZE || col >= BOOP_BOARD_SIZE) {
    return FALSE;
  }

  *out_square = (guint)(row * BOOP_BOARD_SIZE + col);
  return TRUE;
}

static void setup_piece(BoopPosition *position, guint side, guint rank, guint row, guint col) {
  guint square = square_at(row, col);

  assert(position != NULL);
  assert(side < 2);
  assert(rank == BOOP_PIECE_RANK_KITTEN || rank == BOOP_PIECE_RANK_CAT);
  assert(position->board[square] == BOOP_PIECE_EMPTY);

  position->board[square] = boop_piece_make(side, rank);
  if (rank == BOOP_PIECE_RANK_KITTEN) {
    assert(position->kittens_in_supply[side] > 0);
    position->kittens_in_supply[side]--;
  } else {
    assert(position->cats_in_supply[side] > 0);
    position->cats_in_supply[side]--;
  }
}

static void test_piece_encoding_is_byte_sized(void) {
  assert(sizeof(BoopPiece) == 1);
  assert(BOOP_PIECE_EMPTY == 4);
  assert(boop_piece_make(0, BOOP_PIECE_RANK_KITTEN) == BOOP_PIECE_SIDE_0_KITTEN);
  assert(boop_piece_make(0, BOOP_PIECE_RANK_CAT) == BOOP_PIECE_SIDE_0_CAT);
  assert(boop_piece_make(1, BOOP_PIECE_RANK_KITTEN) == BOOP_PIECE_SIDE_1_KITTEN);
  assert(boop_piece_make(1, BOOP_PIECE_RANK_CAT) == BOOP_PIECE_SIDE_1_CAT);
  assert(boop_piece_make(0, BOOP_PIECE_RANK_NONE) == BOOP_PIECE_EMPTY);
  assert(boop_piece_side(BOOP_PIECE_SIDE_1_CAT) == 1);
  assert(boop_piece_side(BOOP_PIECE_EMPTY) == 2);
  assert(boop_piece_rank(BOOP_PIECE_SIDE_1_CAT) == BOOP_PIECE_RANK_CAT);
  assert(boop_piece_rank(BOOP_PIECE_EMPTY) == BOOP_PIECE_RANK_NONE);
}

typedef struct {
  guint64 masks[80];
  guint count;
} TestLineWindows;

static void test_line_windows_foreach(void (*callback)(guint a, guint b, guint c, void *user_data),
                                      void *user_data) {
  static const gint dirs[][2] = {
    {0, 1},
    {1, 0},
    {1, 1},
    {1, -1},
  };

  assert(callback != NULL);

  for (guint dir = 0; dir < G_N_ELEMENTS(dirs); ++dir) {
    gint row_step = dirs[dir][0];
    gint col_step = dirs[dir][1];
    for (gint row = 0; row < BOOP_BOARD_SIZE; ++row) {
      for (gint col = 0; col < BOOP_BOARD_SIZE; ++col) {
        guint a = 0;
        guint b = 0;
        guint c = 0;
        if (!square_at_signed(row, col, &a) ||
            !square_at_signed(row + row_step, col + col_step, &b) ||
            !square_at_signed(row + (2 * row_step), col + (2 * col_step), &c)) {
          continue;
        }

        callback(a, b, c, user_data);
      }
    }
  }
}

static void test_record_unique_line_window(guint a, guint b, guint c, void *user_data) {
  TestLineWindows *windows = user_data;
  guint64 mask = (G_GUINT64_CONSTANT(1) << a) | (G_GUINT64_CONSTANT(1) << b) | (G_GUINT64_CONSTANT(1) << c);

  assert(windows != NULL);
  assert(windows->count < G_N_ELEMENTS(windows->masks));
  for (guint i = 0; i < windows->count; ++i) {
    assert(windows->masks[i] != mask);
  }

  windows->masks[windows->count++] = mask;
}

static void test_assert_cat_line_window_detected(guint a, guint b, guint c, void *user_data) {
  guint *count = user_data;
  BoopPosition position = {0};
  GError *error = NULL;

  assert(count != NULL);
  boop_position_init(&position);
  position.kittens_in_supply[0] = BOOP_SUPPLY_COUNT - 3;
  position.board[a] = boop_piece_make(0, BOOP_PIECE_RANK_CAT);
  position.board[b] = boop_piece_make(0, BOOP_PIECE_RANK_CAT);
  position.board[c] = boop_piece_make(0, BOOP_PIECE_RANK_CAT);

  assert(boop_position_normalize(&position, &error));
  assert(error == NULL);
  assert(boop_position_outcome(&position) == GAME_BACKEND_OUTCOME_SIDE_0_WIN);
  (*count)++;
}

static void test_assert_line_window_promotion_accepted(guint a, guint b, guint c, void *user_data) {
  guint *count = user_data;
  BoopPosition position = {0};
  BoopMove move = {
    .square = (guint8)a,
    .rank = BOOP_PIECE_RANK_KITTEN,
    .promotion_mask = (G_GUINT64_CONSTANT(1) << a) | (G_GUINT64_CONSTANT(1) << b) |
                      (G_GUINT64_CONSTANT(1) << c),
  };

  assert(count != NULL);
  boop_position_init(&position);
  position.board[b] = boop_piece_make(0, BOOP_PIECE_RANK_KITTEN);
  position.board[c] = boop_piece_make(0, BOOP_PIECE_RANK_KITTEN);
  position.kittens_in_supply[0] -= 2;

  assert(boop_position_apply_move(&position, &move));
  assert(position.promoted_count[0] == 3);
  (*count)++;
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

static void test_all_line_windows_are_detected(void) {
  TestLineWindows windows = {0};
  guint detected_count = 0;
  guint promoted_count = 0;

  test_line_windows_foreach(test_record_unique_line_window, &windows);
  assert(windows.count == 80);
  test_line_windows_foreach(test_assert_cat_line_window_detected, &detected_count);
  test_line_windows_foreach(test_assert_line_window_promotion_accepted, &promoted_count);
  assert(detected_count == windows.count);
  assert(promoted_count == windows.count);
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
  assert_square_empty(&position, 1, 2);
  assert_square_rank(&position, 0, 2, BOOP_PIECE_RANK_KITTEN);
  assert_square_rank(&position, 2, 3, BOOP_PIECE_RANK_CAT);
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
  assert_square_empty(&position, 0, 0);
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
  assert_square_empty(&position, 0, 0);
  assert_square_empty(&position, 0, 1);
  assert_square_empty(&position, 0, 2);
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
  assert_square_empty(&position, 5, 5);
  assert(position.cats_in_supply[0] == 1);
  assert(position.promoted_count[0] == 1);
}

static void setup_line_and_full_board_position(BoopPosition *position) {
  g_return_if_fail(position != NULL);

  boop_position_init(position);
  setup_piece(position, 0, BOOP_PIECE_RANK_KITTEN, 0, 1);
  setup_piece(position, 0, BOOP_PIECE_RANK_KITTEN, 0, 2);
  setup_piece(position, 0, BOOP_PIECE_RANK_KITTEN, 1, 4);
  setup_piece(position, 0, BOOP_PIECE_RANK_KITTEN, 2, 0);
  setup_piece(position, 0, BOOP_PIECE_RANK_KITTEN, 2, 5);
  setup_piece(position, 0, BOOP_PIECE_RANK_KITTEN, 4, 1);
  setup_piece(position, 0, BOOP_PIECE_RANK_KITTEN, 4, 4);
}

static void test_line_and_full_board_can_choose_line_or_single_kitten(void) {
  guint64 line_mask = square_mask(0, 0) | square_mask(0, 1) | square_mask(0, 2);
  guint64 single_mask = square_mask(4, 4);
  guint matching_moves = 0;
  gboolean found_line = FALSE;
  gboolean found_single = FALSE;
  gboolean found_skip = FALSE;
  BoopPosition position = {0};
  BoopMove line_move = {
    .square = (guint8)square_at(0, 0),
    .rank = BOOP_PIECE_RANK_KITTEN,
    .promotion_mask = line_mask,
  };
  BoopMove single_move = {
    .square = (guint8)square_at(0, 0),
    .rank = BOOP_PIECE_RANK_KITTEN,
    .promotion_mask = single_mask,
  };
  BoopMove skipped_move = {
    .square = (guint8)square_at(0, 0),
    .rank = BOOP_PIECE_RANK_KITTEN,
  };

  setup_line_and_full_board_position(&position);
  GameBackendMoveList moves = boop_position_list_moves(&position);
  for (gsize i = 0; i < moves.count; ++i) {
    const BoopMove *move = boop_move_list_get(&moves, i);
    assert(move != NULL);
    if (move->square != square_at(0, 0)) {
      continue;
    }

    matching_moves++;
    found_line = found_line || move->promotion_mask == line_mask;
    found_single = found_single || move->promotion_mask == single_mask;
    found_skip = found_skip || move->promotion_mask == 0;
  }

  assert(matching_moves == 9);
  assert(found_line);
  assert(found_single);
  assert(!found_skip);
  boop_move_list_free(&moves);

  setup_line_and_full_board_position(&position);
  assert(boop_position_apply_move(&position, &line_move));
  assert(position.promoted_count[0] == 3);
  assert_square_empty(&position, 0, 0);
  assert_square_empty(&position, 0, 1);
  assert_square_empty(&position, 0, 2);

  setup_line_and_full_board_position(&position);
  assert(boop_position_apply_move(&position, &single_move));
  assert(position.promoted_count[0] == 1);
  assert_square_rank(&position, 0, 0, BOOP_PIECE_RANK_KITTEN);
  assert_square_rank(&position, 0, 1, BOOP_PIECE_RANK_KITTEN);
  assert_square_rank(&position, 0, 2, BOOP_PIECE_RANK_KITTEN);
  assert_square_empty(&position, 4, 4);

  setup_line_and_full_board_position(&position);
  assert(!boop_position_apply_move(&position, &skipped_move));
}

static const BoopMove *test_find_builder_candidate_with_mask(const GameBackendMoveList *moves, guint64 mask) {
  g_return_val_if_fail(moves != NULL, NULL);

  for (gsize i = 0; i < moves->count; ++i) {
    const BoopMove *move = boop_move_list_get(moves, i);
    assert(move != NULL);
    if (move->promotion_mask == mask) {
      return move;
    }
  }
  return NULL;
}

static void test_builder_can_continue_single_graduation_into_line_choice(void) {
  guint64 first_square = square_mask(0, 0);
  guint64 first_two_squares = first_square | square_mask(0, 1);
  guint64 line_mask = first_two_squares | square_mask(0, 2);
  guint64 single_mask = square_mask(4, 4);
  BoopPosition position = {0};
  GameBackendMoveBuilder builder = {0};
  BoopMove move = {0};
  const BoopMove *candidate = NULL;

  setup_line_and_full_board_position(&position);
  assert(boop_move_builder_init(&position, &builder));
  GameBackendMoveList candidates = boop_move_builder_list_candidates(&builder);
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

  candidates = boop_move_builder_list_candidates(&builder);
  candidate = test_find_builder_candidate_with_mask(&candidates, first_square);
  assert(candidate != NULL);
  assert(boop_move_builder_step(&builder, candidate));
  assert(boop_move_builder_is_complete(&builder));
  boop_move_list_free(&candidates);

  candidates = boop_move_builder_list_candidates(&builder);
  candidate = test_find_builder_candidate_with_mask(&candidates, first_two_squares);
  assert(candidate != NULL);
  assert(boop_move_builder_step(&builder, candidate));
  assert(!boop_move_builder_is_complete(&builder));
  boop_move_list_free(&candidates);

  candidates = boop_move_builder_list_candidates(&builder);
  candidate = test_find_builder_candidate_with_mask(&candidates, line_mask);
  assert(candidate != NULL);
  assert(boop_move_builder_step(&builder, candidate));
  assert(boop_move_builder_is_complete(&builder));
  boop_move_list_free(&candidates);

  assert(boop_move_builder_build_move(&builder, &move));
  assert(move.promotion_mask == line_mask);
  boop_move_builder_clear(&builder);

  setup_line_and_full_board_position(&position);
  assert(boop_move_builder_init(&position, &builder));
  candidates = boop_move_builder_list_candidates(&builder);
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

  candidates = boop_move_builder_list_candidates(&builder);
  candidate = test_find_builder_candidate_with_mask(&candidates, single_mask);
  assert(candidate != NULL);
  assert(boop_move_builder_step(&builder, candidate));
  assert(boop_move_builder_is_complete(&builder));
  assert(boop_move_builder_build_move(&builder, &move));
  assert(move.promotion_mask == single_mask);
  boop_move_list_free(&candidates);
  boop_move_builder_clear(&builder);
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

static void test_all_boop_rays_match_square_geometry(void) {
  static const gint dirs[][2] = {
    {-1, -1},
    {-1,  0},
    {-1,  1},
    { 0, -1},
    { 0,  1},
    { 1, -1},
    { 1,  0},
    { 1,  1},
  };

  for (guint placed_square = 0; placed_square < BOOP_SQUARE_COUNT; ++placed_square) {
    gint placed_row = (gint)(placed_square / BOOP_BOARD_SIZE);
    gint placed_col = (gint)(placed_square % BOOP_BOARD_SIZE);

    for (guint dir = 0; dir < G_N_ELEMENTS(dirs); ++dir) {
      gint row_delta = dirs[dir][0];
      gint col_delta = dirs[dir][1];
      guint adjacent_square = 0;
      guint destination_square = 0;
      gboolean has_adjacent =
          square_at_signed(placed_row + row_delta, placed_col + col_delta, &adjacent_square);
      gboolean has_destination =
          square_at_signed(placed_row + (2 * row_delta), placed_col + (2 * col_delta), &destination_square);
      BoopPosition position = {0};
      BoopMove move = {
        .square = (guint8)placed_square,
        .rank = BOOP_PIECE_RANK_CAT,
      };
      BoopMoveOverlayInfo overlay = {0};

      boop_position_init(&position);
      position.kittens_in_supply[0] = BOOP_SUPPLY_COUNT - 1;
      position.cats_in_supply[0] = 1;
      position.promoted_count[0] = 1;
      if (has_adjacent) {
        position.board[adjacent_square] = boop_piece_make(1, BOOP_PIECE_RANK_KITTEN);
        position.kittens_in_supply[1]--;
      }

      assert(boop_move_describe_overlay(&position, &move, &overlay));
      if (!has_adjacent) {
        assert(overlay.arrow_count == 0);
        assert(overlay.removed_square_count == 0);
        continue;
      }

      assert(overlay.arrow_count == 1);
      assert(overlay.arrows[0].from_square == adjacent_square);
      assert(overlay.arrows[0].row_delta == row_delta);
      assert(overlay.arrows[0].col_delta == col_delta);
      if (has_destination) {
        assert(overlay.arrows[0].to_square == destination_square);
        assert(!overlay.arrows[0].leaves_board);
        assert(overlay.removed_square_count == 0);
      } else {
        assert(overlay.arrows[0].to_square == BOOP_INVALID_SQUARE);
        assert(overlay.arrows[0].leaves_board);
        assert(overlay.removed_square_count == 1);
        assert(overlay.removed_squares[0] == adjacent_square);
      }
    }
  }
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

static void test_static_evaluation_ignores_supply_counts(void) {
  BoopPosition position = {0};

  boop_position_init(&position);
  position.cats_in_supply[0] = 1;
  setup_piece(&position, 0, BOOP_PIECE_RANK_KITTEN, 2, 2);
  setup_piece(&position, 0, BOOP_PIECE_RANK_CAT, 3, 3);
  setup_piece(&position, 1, BOOP_PIECE_RANK_KITTEN, 0, 0);
  position.promoted_count[0] = 2;
  position.promoted_count[1] = 1;

  gint baseline = boop_position_evaluate_static(&position);

  position.kittens_in_supply[0] = 0;
  position.cats_in_supply[0] = 8;
  position.kittens_in_supply[1] = 8;
  position.cats_in_supply[1] = 0;
  assert(boop_position_evaluate_static(&position) == baseline);
}

static void test_static_evaluation_scores_cats_like_kittens(void) {
  BoopPosition kitten_position = {0};
  BoopPosition cat_position = {0};
  guint square = square_at(2, 2);

  boop_position_init(&kitten_position);
  boop_position_init(&cat_position);
  kitten_position.board[square] = boop_piece_make(0, BOOP_PIECE_RANK_KITTEN);
  cat_position.board[square] = boop_piece_make(0, BOOP_PIECE_RANK_CAT);

  assert(boop_position_evaluate_static(&cat_position) == boop_position_evaluate_static(&kitten_position));
}

static void test_static_evaluation_does_not_bonus_cat_lines(void) {
  BoopPosition position = {0};

  boop_position_init(&position);
  position.board[square_at(0, 0)] = boop_piece_make(0, BOOP_PIECE_RANK_CAT);
  position.board[square_at(0, 1)] = boop_piece_make(0, BOOP_PIECE_RANK_CAT);
  position.board[square_at(0, 2)] = boop_piece_make(0, BOOP_PIECE_RANK_CAT);

  assert(boop_position_evaluate_static(&position) == 309);
}

static void test_terminal_score_uses_win_scale(void) {
  assert(boop_position_terminal_score(GAME_BACKEND_OUTCOME_SIDE_0_WIN, 0) == 10000);
  assert(boop_position_terminal_score(GAME_BACKEND_OUTCOME_SIDE_0_WIN, 7) == 9993);
  assert(boop_position_terminal_score(GAME_BACKEND_OUTCOME_SIDE_1_WIN, 7) == -9993);
  assert(boop_position_terminal_score(GAME_BACKEND_OUTCOME_DRAW, 7) == 0);
  assert(boop_position_terminal_score(GAME_BACKEND_OUTCOME_ONGOING, 7) == 0);
}

int main(void) {
  test_piece_encoding_is_byte_sized();
  test_initial_move_list_and_notation();
  test_kitten_boops_kittens_not_cats();
  test_booped_off_piece_returns_to_supply();
  test_kitten_line_promotes_to_cats();
  test_overlong_line_has_multiple_promotion_moves();
  test_all_line_windows_are_detected();
  test_graduation_can_promote_one_kitten();
  test_line_and_full_board_can_choose_line_or_single_kitten();
  test_builder_can_continue_single_graduation_into_line_choice();
  test_three_cats_win();
  test_builder_selects_promotion_squares();
  test_overlay_describes_on_board_boop();
  test_overlay_describes_off_board_boop();
  test_all_boop_rays_match_square_geometry();
  test_overlay_describes_promoted_kittens_as_removed();
  test_static_evaluation_ignores_supply_counts();
  test_static_evaluation_scores_cats_like_kittens();
  test_static_evaluation_does_not_bonus_cat_lines();
  test_terminal_score_uses_win_scale();

  return 0;
}
