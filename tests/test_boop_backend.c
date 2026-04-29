#include <assert.h>
#include <string.h>

#include "../src/games/boop/boop_backend.h"
#include "../src/games/boop/boop_game.h"

static void test_backend_metadata(void) {
  const GameBackend *backend = &boop_game_backend;

  assert(strcmp(backend->id, "boop") == 0);
  assert(strcmp(backend->display_name, "Boop") == 0);
  assert(backend->variant_count == 0);
  assert(backend->supports_move_list);
  assert(backend->supports_move_builder);
  assert(backend->supports_ai_search);
  assert(backend->supports_square_grid_board);
  assert(backend->list_moves != NULL);
  assert(backend->list_good_moves != NULL);
  assert(backend->evaluate_static != NULL);
  assert(backend->terminal_score != NULL);
  assert(backend->hash_position != NULL);
}

static void test_backend_move_flow(void) {
  const GameBackend *backend = &boop_game_backend;
  gpointer position = NULL;
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  const BoopMove *candidate = NULL;
  BoopMove move = {0};
  char notation[32] = {0};

  position = g_malloc0(backend->position_size);
  assert(position != NULL);
  backend->position_init(position, NULL);
  assert(backend->position_turn(position) == 0);

  GameBackendMoveList moves = backend->list_moves(position);
  assert(moves.count == BOOP_SQUARE_COUNT);
  backend->move_list_free(&moves);

  assert(backend->move_builder_init(position, &builder));
  candidates = backend->move_builder_list_candidates(&builder);
  assert(candidates.count == BOOP_SQUARE_COUNT);
  candidate = backend->move_list_get(&candidates, 0);
  assert(candidate != NULL);
  assert(backend->move_builder_step(&builder, candidate));
  assert(backend->move_builder_is_complete(&builder));
  assert(backend->move_builder_build_move(&builder, &move));
  assert(backend->format_move(&move, notation, sizeof(notation)));
  assert(strcmp(notation, "K@a1") == 0);
  assert(backend->apply_move(position, &move));
  assert(backend->position_turn(position) == 1);

  backend->move_list_free(&candidates);
  backend->move_builder_clear(&builder);
  backend->position_clear(position);
  g_free(position);
}

static void test_backend_builder_lists_available_ranks(void) {
  const GameBackend *backend = &boop_game_backend;
  BoopPosition position = {0};
  GameBackendMoveBuilder builder = {0};

  boop_position_init(&position);
  position.cats_in_supply[0] = 1;

  assert(backend->move_builder_init(&position, &builder));
  GameBackendMoveList candidates = backend->move_builder_list_candidates(&builder);
  assert(candidates.count == BOOP_SQUARE_COUNT * 2);

  gboolean saw_kitten = FALSE;
  gboolean saw_cat = FALSE;
  for (gsize i = 0; i < candidates.count; ++i) {
    const BoopMove *candidate = backend->move_list_get(&candidates, i);
    assert(candidate != NULL);
    if (candidate->square != 0) {
      continue;
    }
    saw_kitten = saw_kitten || candidate->rank == BOOP_PIECE_RANK_KITTEN;
    saw_cat = saw_cat || candidate->rank == BOOP_PIECE_RANK_CAT;
  }

  assert(saw_kitten);
  assert(saw_cat);

  backend->move_list_free(&candidates);
  backend->move_builder_clear(&builder);
}

static void test_backend_builder_preview_position_after_booping(void) {
  const GameBackend *backend = &boop_game_backend;
  BoopPosition position = {0};
  GameBackendMoveBuilder builder = {0};

  boop_position_init(&position);
  position.kittens_in_supply[0] = 7;
  position.board[1] = (BoopPiece){
    .side = 0,
    .rank = BOOP_PIECE_RANK_KITTEN,
  };

  assert(backend->move_builder_init(&position, &builder));
  BoopMove candidate = {
    .square = 0,
    .rank = BOOP_PIECE_RANK_KITTEN,
    .path_length = 1,
    .path = {0},
  };
  assert(backend->move_builder_step(&builder, &candidate));

  const BoopPosition *preview = backend->move_builder_preview_position(&builder);
  assert(preview != NULL);
  assert(preview->board[0].rank == BOOP_PIECE_RANK_KITTEN);
  assert(preview->board[1].rank == BOOP_PIECE_RANK_NONE);
  assert(preview->board[2].rank == BOOP_PIECE_RANK_KITTEN);

  backend->move_builder_clear(&builder);
}

static void test_backend_square_grid(void) {
  const GameBackend *backend = &boop_game_backend;
  BoopPosition position = {0};
  guint index = 0;
  guint row = 0;
  guint col = 0;
  GameBackendSquarePieceView piece = {0};

  boop_position_init(&position);
  assert(backend->square_grid_rows(&position) == BOOP_BOARD_SIZE);
  assert(backend->square_grid_cols(&position) == BOOP_BOARD_SIZE);
  assert(backend->square_grid_square_playable(&position, 5, 5));
  assert(backend->square_grid_square_index(&position, 5, 5, &index));
  assert(index == BOOP_SQUARE_COUNT - 1);
  assert(backend->square_grid_index_coord(&position, index, &row, &col));
  assert(row == 5);
  assert(col == 5);
  assert(backend->square_grid_piece_view(&position, 0, &piece));
  assert(piece.is_empty);
}

int main(void) {
  test_backend_metadata();
  test_backend_move_flow();
  test_backend_builder_lists_available_ranks();
  test_backend_builder_preview_position_after_booping();
  test_backend_square_grid();

  return 0;
}
