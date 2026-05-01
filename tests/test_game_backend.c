#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/active_game_backend.h"
#include "../src/board_selection_controller.h"
#include "../src/game_app_profile.h"
#include "../src/games/boop/boop_types.h"
#include "test_profile_utils.h"

static void test_app_profile_metadata(void) {
  const GGameAppProfile *profile = ggame_active_app_profile();
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;

  assert(profile != NULL);
  assert(backend != NULL);
  assert(profile->backend == backend);
  assert(profile->app_id != NULL);
  assert(profile->display_name != NULL);
  assert(profile->window_title_name != NULL);

  switch (profile->kind) {
    case GGAME_APP_KIND_CHECKERS:
      assert(profile->features.supports_shared_shell);
      assert(profile->features.supports_sgf_files);
      assert(profile->features.supports_ai_players);
      assert(profile->features.supports_puzzles);
      assert(profile->features.supports_import);
      assert(profile->features.supports_settings);
      assert(profile->features.supports_save_position);
      assert(profile->features.supports_edit_mode);
      assert(profile->features.supports_analysis);
      assert(profile->ui.create_window == NULL);
      assert(profile->ui.create_board_host == NULL);
      break;
    case GGAME_APP_KIND_HOMEWORLDS:
      assert(!profile->features.supports_shared_shell);
      assert(!profile->features.supports_puzzles);
      assert(!profile->features.supports_import);
      assert(!profile->features.supports_settings);
      assert(profile->ui.create_window != NULL);
      assert(profile->ui.create_board_host == NULL);
      break;
    case GGAME_APP_KIND_BOOP:
      assert(profile->features.supports_shared_shell);
      assert(profile->features.supports_sgf_files);
      assert(profile->features.supports_ai_players);
      assert(!profile->features.supports_puzzles);
      assert(!profile->features.supports_import);
      assert(profile->features.supports_settings);
      assert(profile->features.supports_save_position);
      assert(!profile->features.supports_edit_mode);
      assert(profile->features.supports_analysis);
      assert(profile->ui.create_window == NULL);
      assert(profile->ui.create_board_host != NULL);
      break;
    default:
      assert(!"Unhandled app profile kind");
  }
}

static void test_backend_metadata(void) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  assert(backend != NULL);

  switch (ggame_active_app_profile()->kind) {
    case GGAME_APP_KIND_CHECKERS: {
      assert(strcmp(backend->id, "checkers") == 0);
      assert(strcmp(backend->display_name, "Checkers") == 0);
      assert(backend->variant_count == 3);
      assert(backend->supports_move_list);
      assert(backend->supports_move_builder);
      assert(backend->supports_ai_search);
      assert(backend->list_good_moves != NULL);
      assert(backend->sgf_apply_setup_node != NULL);
      assert(backend->sgf_write_position_node != NULL);

      const GameBackendVariant *american = backend->variant_at(0);
      assert(american != NULL);
      assert(strcmp(american->short_name, "american") == 0);

      const GameBackendVariant *russian = backend->variant_by_short_name("russian");
      assert(russian != NULL);
      assert(strcmp(russian->name, "Russian (8x8)") == 0);
      break;
    }
    case GGAME_APP_KIND_HOMEWORLDS:
      assert(strcmp(backend->id, "homeworlds") == 0);
      assert(strcmp(backend->display_name, "Homeworlds") == 0);
      assert(backend->variant_count == 0);
      assert(!backend->supports_move_list);
      assert(backend->supports_move_builder);
      assert(backend->supports_ai_search);
      assert(backend->list_good_moves != NULL);
      assert(backend->sgf_apply_setup_node == NULL);
      assert(backend->sgf_write_position_node == NULL);
      assert(strcmp(backend->side_label(0), "Player 1") == 0);
      assert(strcmp(backend->side_label(1), "Player 2") == 0);
      break;
    case GGAME_APP_KIND_BOOP:
      assert(strcmp(backend->id, "boop") == 0);
      assert(strcmp(backend->display_name, "Boop") == 0);
      assert(backend->variant_count == 0);
      assert(backend->supports_move_list);
      assert(backend->supports_move_builder);
      assert(backend->supports_ai_search);
      assert(backend->list_good_moves != NULL);
      assert(backend->supports_square_grid_board);
      assert(backend->sgf_apply_setup_node != NULL);
      assert(backend->sgf_write_position_node != NULL);
      assert(strcmp(backend->side_label(0), "Player 1") == 0);
      assert(strcmp(backend->side_label(1), "Player 2") == 0);
      break;
    default:
      assert(!"Unhandled backend profile kind");
  }
}

static void test_backend_position_and_move_flow(void) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  assert(backend != NULL);

  switch (ggame_active_app_profile()->kind) {
    case GGAME_APP_KIND_CHECKERS: {
      assert(backend->supports_move_list);
      assert(backend->supports_move_builder);

      gpointer position = g_malloc0(backend->position_size);
      assert(position != NULL);

      const GameBackendVariant *american = backend->variant_by_short_name("american");
      assert(american != NULL);
      backend->position_init(position, american);

      GameBackendMoveList moves = backend->list_moves(position);
      assert(moves.count > 0);

      const void *first_move = backend->move_list_get(&moves, 0);
      assert(first_move != NULL);

      char notation[32] = {0};
      assert(backend->format_move(first_move, notation, sizeof(notation)));
      assert(notation[0] != '\0');

      gpointer move_copy = g_malloc0(backend->move_size);
      assert(move_copy != NULL);
      memcpy(move_copy, first_move, backend->move_size);
      assert(backend->moves_equal(first_move, move_copy));
      assert(backend->apply_move(position, move_copy));

      backend->move_list_free(&moves);

      backend->position_clear(position);
      backend->position_init(position, american);
      GameBackendMoveBuilder builder = {0};
      assert(backend->move_builder_init(position, &builder));
      GameBackendMoveList candidates = backend->move_builder_list_candidates(&builder);
      assert(candidates.count > 0);
      const void *first_candidate = backend->move_list_get(&candidates, 0);
      assert(first_candidate != NULL);
      assert(backend->move_builder_step(&builder, first_candidate));
      backend->move_list_free(&candidates);
      while (!backend->move_builder_is_complete(&builder)) {
        candidates = backend->move_builder_list_candidates(&builder);
        assert(candidates.count > 0);
        first_candidate = backend->move_list_get(&candidates, 0);
        assert(first_candidate != NULL);
        assert(backend->move_builder_step(&builder, first_candidate));
        backend->move_list_free(&candidates);
      }
      memset(move_copy, 0, backend->move_size);
      assert(backend->move_builder_build_move(&builder, move_copy));
      assert(backend->apply_move(position, move_copy));
      backend->move_builder_clear(&builder);

      backend->position_clear(position);
      g_free(move_copy);
      g_free(position);
      break;
    }
    case GGAME_APP_KIND_HOMEWORLDS: {
      assert(!backend->supports_move_list);
      assert(backend->supports_move_builder);
      assert(backend->move_builder_init != NULL);
      assert(backend->move_builder_clear != NULL);
      assert(backend->move_builder_list_candidates != NULL);
      assert(backend->move_builder_step != NULL);
      assert(backend->move_builder_is_complete != NULL);
      assert(backend->move_builder_build_move != NULL);

      gpointer position = g_malloc0(backend->position_size);
      assert(position != NULL);
      backend->position_init(position, NULL);
      assert(backend->position_outcome(position) == GAME_BACKEND_OUTCOME_ONGOING);
      assert(backend->position_turn(position) == 0);

      GameBackendMoveBuilder builder = {0};
      assert(backend->move_builder_init(position, &builder));
      GameBackendMoveList candidates = backend->move_builder_list_candidates(&builder);
      assert(candidates.count > 0);
      backend->move_list_free(&candidates);
      assert(!backend->move_builder_is_complete(&builder));
      backend->move_builder_clear(&builder);

      GameBackendMoveList good_moves = backend->list_good_moves(position, 4, 1);
      assert(good_moves.count > 0);
      backend->move_list_free(&good_moves);
      backend->position_clear(position);
      g_free(position);
      break;
    }
    case GGAME_APP_KIND_BOOP: {
      assert(backend->supports_move_list);
      assert(backend->supports_move_builder);
      assert(backend->move_builder_init != NULL);
      assert(backend->move_builder_clear != NULL);
      assert(backend->move_builder_list_candidates != NULL);
      assert(backend->move_builder_step != NULL);
      assert(backend->move_builder_is_complete != NULL);
      assert(backend->move_builder_build_move != NULL);

      gpointer position = g_malloc0(backend->position_size);
      assert(position != NULL);
      backend->position_init(position, NULL);
      assert(backend->position_outcome(position) == GAME_BACKEND_OUTCOME_ONGOING);
      assert(backend->position_turn(position) == 0);

      GameBackendMoveList moves = backend->list_moves(position);
      assert(moves.count == 36);
      const void *first_move = backend->move_list_get(&moves, 0);
      assert(first_move != NULL);

      gpointer move_copy = g_malloc0(backend->move_size);
      assert(move_copy != NULL);
      memcpy(move_copy, first_move, backend->move_size);
      assert(backend->moves_equal(first_move, move_copy));
      backend->move_list_free(&moves);

      GameBackendMoveBuilder builder = {0};
      assert(backend->move_builder_init(position, &builder));
      GameBackendMoveList candidates = backend->move_builder_list_candidates(&builder);
      assert(candidates.count > 0);

      const void *first_candidate = backend->move_list_get(&candidates, 0);
      assert(first_candidate != NULL);
      assert(backend->move_builder_step(&builder, first_candidate));
      assert(backend->move_builder_is_complete(&builder));

      gpointer move = g_malloc0(backend->move_size);
      assert(move != NULL);
      assert(backend->move_builder_build_move(&builder, move));

      char notation[32] = {0};
      assert(backend->format_move(move, notation, sizeof(notation)));
      assert(notation[0] != '\0');
      assert(backend->apply_move(position, move));
      assert(backend->position_turn(position) == 1);

      GameBackendMoveList good_moves = backend->list_good_moves(position, 4, 1);
      assert(good_moves.count > 0);
      assert(good_moves.count <= 4);
      backend->move_list_free(&good_moves);

      backend->move_list_free(&candidates);
      backend->move_builder_clear(&builder);
      backend->position_clear(position);
      g_free(move);
      g_free(move_copy);
      g_free(position);
      break;
    }
    default:
      assert(!"Unhandled move flow profile kind");
  }
}

static void test_backend_move_path_length_only_query(void) {
  if (ggame_active_app_profile()->kind != GGAME_APP_KIND_CHECKERS) {
    return;
  }

  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  assert(backend != NULL);
  assert(backend->square_grid_move_get_path != NULL);

  gpointer position = g_malloc0(backend->position_size);
  assert(position != NULL);

  const GameBackendVariant *american = backend->variant_by_short_name("american");
  assert(american != NULL);
  backend->position_init(position, american);

  GameBackendMoveList moves = backend->list_moves(position);
  assert(moves.count > 0);

  gboolean found_simple_move = FALSE;
  for (gsize i = 0; i < moves.count; ++i) {
    const void *move = backend->move_list_get(&moves, i);
    assert(move != NULL);

    guint move_length = 0;
    assert(backend->square_grid_move_get_path(move, &move_length, NULL, 0));
    if (move_length != 2) {
      continue;
    }

    guint path[2] = {0};
    assert(backend->square_grid_move_get_path(move, &move_length, path, G_N_ELEMENTS(path)));
    assert(move_length == 2);
    assert(path[0] != path[1]);
    found_simple_move = TRUE;
    break;
  }

  assert(found_simple_move);

  backend->move_list_free(&moves);
  backend->position_clear(position);
  g_free(position);
}

typedef struct {
  GGameModel *model;
} TestBackendSelectionData;

static gboolean test_backend_apply_model_move(gconstpointer move, gpointer user_data) {
  TestBackendSelectionData *data = user_data;

  assert(data != NULL);
  assert(data->model != NULL);
  assert(move != NULL);

  return ggame_model_apply_move(data->model, move);
}

static void test_backend_collect_model_starts(GGameModel *model, gboolean *out_starts, gsize out_count) {
  const GameBackend *backend = ggame_model_peek_backend(model);

  assert(backend != NULL);
  assert(out_starts != NULL);
  assert(backend->square_grid_moves_collect_starts != NULL);

  memset(out_starts, 0, sizeof(*out_starts) * out_count);
  GameBackendMoveList moves = ggame_model_list_moves(model);
  assert(moves.count > 0);
  backend->square_grid_moves_collect_starts(&moves, out_starts, out_count);
  backend->move_list_free(&moves);
}

static gboolean test_backend_bool_arrays_equal(const gboolean *left, const gboolean *right, gsize count) {
  assert(left != NULL);
  assert(right != NULL);

  for (gsize i = 0; i < count; ++i) {
    if (left[i] != right[i]) {
      return FALSE;
    }
  }

  return TRUE;
}

static void test_backend_selection_controller_restarts_from_non_continuation_click(void) {
  if (ggame_active_app_profile()->kind != GGAME_APP_KIND_CHECKERS) {
    return;
  }

  enum { TEST_BACKEND_SELECTION_SQUARE_CAPACITY = 128 };
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  assert(backend != NULL);
  assert(backend->supports_move_builder);
  assert(backend->square_grid_move_get_path != NULL);

  GGameModel *model = ggame_model_new(backend);
  assert(model != NULL);
  BoardSelectionController *controller = board_selection_controller_new();
  assert(controller != NULL);

  TestBackendSelectionData data = {.model = model};
  board_selection_controller_set_model(controller, model);
  board_selection_controller_set_move_handler(controller, test_backend_apply_model_move, &data);

  GameBackendMoveList moves = ggame_model_list_moves(model);
  assert(moves.count > 1);
  const void *first_move = backend->move_list_get(&moves, 0);
  assert(first_move != NULL);

  guint first_path[TEST_BACKEND_SELECTION_SQUARE_CAPACITY] = {0};
  guint first_path_length = 0;
  assert(backend->square_grid_move_get_path(first_move,
                                            &first_path_length,
                                            first_path,
                                            G_N_ELEMENTS(first_path)));
  assert(first_path_length >= 2);
  assert(board_selection_controller_handle_click(controller, first_path[0]));

  gboolean selectable[TEST_BACKEND_SELECTION_SQUARE_CAPACITY] = {FALSE};
  gboolean destinations[TEST_BACKEND_SELECTION_SQUARE_CAPACITY] = {FALSE};
  assert(board_selection_controller_collect_highlights(controller,
                                                       selectable,
                                                       destinations,
                                                       G_N_ELEMENTS(selectable)));

  gboolean found_restart_start = FALSE;
  guint restart_start = 0;
  for (gsize i = 1; i < moves.count; ++i) {
    const void *move = backend->move_list_get(&moves, i);
    assert(move != NULL);

    guint path[TEST_BACKEND_SELECTION_SQUARE_CAPACITY] = {0};
    guint path_length = 0;
    assert(backend->square_grid_move_get_path(move, &path_length, path, G_N_ELEMENTS(path)));
    if (path_length == 0 || path[0] == first_path[0] ||
        (path[0] < G_N_ELEMENTS(destinations) && destinations[path[0]])) {
      continue;
    }

    restart_start = path[0];
    found_restart_start = TRUE;
    break;
  }
  assert(found_restart_start);

  assert(board_selection_controller_handle_click(controller, restart_start));

  guint selected_length = 0;
  const guint *selected_path = board_selection_controller_peek_path(controller, &selected_length);
  assert(selected_path != NULL);
  assert(selected_length == 1);
  assert(selected_path[0] == restart_start);

  gconstpointer position = ggame_model_peek_position(model);
  assert(position != NULL);
  assert(backend->position_turn(position) == 0);

  backend->move_list_free(&moves);
  g_clear_object(&controller);
  g_clear_object(&model);
}

static gboolean test_backend_prefer_boop_cat(gconstpointer move, gpointer /*user_data*/) {
  const BoopMove *boop_move = move;

  assert(boop_move != NULL);
  return boop_move->rank == BOOP_PIECE_RANK_CAT;
}

static gboolean test_backend_confirm_boop_promotion(const GameBackendMoveBuilder *builder,
                                                    gconstpointer move,
                                                    gpointer /*user_data*/) {
  const BoopMove *boop_move = move;

  assert(builder != NULL);
  assert(builder->builder_state != NULL);
  assert(builder->builder_state_size == sizeof(BoopMoveBuilderState));
  assert(boop_move != NULL);

  const BoopMoveBuilderState *builder_state = builder->builder_state;
  return builder_state->promotion_option_count > 0 && boop_move->promotion_mask != 0;
}

static void test_backend_selection_controller_prefers_boop_rank(void) {
  if (ggame_active_app_profile()->kind != GGAME_APP_KIND_BOOP) {
    return;
  }

  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  assert(backend != NULL);
  assert(backend->supports_move_builder);

  GGameModel *model = ggame_model_new(backend);
  assert(model != NULL);

  BoopPosition position = {0};
  backend->position_init(&position, NULL);
  position.cats_in_supply[0] = 1;
  assert(ggame_model_set_position(model, &position));

  BoardSelectionController *controller = board_selection_controller_new();
  assert(controller != NULL);

  TestBackendSelectionData data = {.model = model};
  board_selection_controller_set_model(controller, model);
  board_selection_controller_set_move_handler(controller, test_backend_apply_model_move, &data);
  board_selection_controller_set_candidate_preference(controller, test_backend_prefer_boop_cat, NULL);

  assert(board_selection_controller_handle_click(controller, 0));

  const BoopPosition *applied = ggame_model_peek_position(model);
  assert(applied != NULL);
  assert(applied->board[0].rank == BOOP_PIECE_RANK_CAT);
  assert(applied->cats_in_supply[0] == 0);

  backend->position_clear(&position);
  g_clear_object(&controller);
  g_clear_object(&model);
}

static void test_backend_selection_controller_confirms_boop_promotion(void) {
  if (ggame_active_app_profile()->kind != GGAME_APP_KIND_BOOP) {
    return;
  }

  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  assert(backend != NULL);
  assert(backend->supports_move_builder);

  GGameModel *model = ggame_model_new(backend);
  assert(model != NULL);

  BoopPosition position = {0};
  backend->position_init(&position, NULL);
  position.kittens_in_supply[0] = 1;
  const guint kitten_squares[] = {0, 2, 4, 13, 15, 17, 24};
  for (guint i = 0; i < G_N_ELEMENTS(kitten_squares); ++i) {
    position.board[kitten_squares[i]] = (BoopPiece){
      .side = 0,
      .rank = BOOP_PIECE_RANK_KITTEN,
    };
  }
  assert(ggame_model_set_position(model, &position));

  BoardSelectionController *controller = board_selection_controller_new();
  assert(controller != NULL);

  TestBackendSelectionData data = {.model = model};
  board_selection_controller_set_model(controller, model);
  board_selection_controller_set_move_handler(controller, test_backend_apply_model_move, &data);
  board_selection_controller_set_completion_confirmation(controller, test_backend_confirm_boop_promotion, NULL);

  assert(board_selection_controller_handle_click(controller, 35));
  assert(!board_selection_controller_completion_pending(controller));

  const GameBackendMoveBuilder *builder = board_selection_controller_peek_builder(controller);
  assert(builder != NULL);
  assert(builder->builder_state != NULL);
  const BoopMoveBuilderState *builder_state = builder->builder_state;
  assert(builder_state->stage == BOOP_MOVE_BUILDER_STAGE_PROMOTION);
  assert(builder_state->promotion_option_count == 8);

  const BoopPosition *current = ggame_model_peek_position(model);
  assert(current != NULL);
  assert(current->turn == 0);
  assert(current->board[35].rank == BOOP_PIECE_RANK_NONE);

  assert(board_selection_controller_handle_click(controller, 0));
  assert(board_selection_controller_completion_pending(controller));

  current = ggame_model_peek_position(model);
  assert(current != NULL);
  assert(current->turn == 0);
  assert(current->board[35].rank == BOOP_PIECE_RANK_NONE);

  assert(board_selection_controller_confirm(controller));
  current = ggame_model_peek_position(model);
  assert(current != NULL);
  assert(current->turn == 1);
  assert(current->board[0].rank == BOOP_PIECE_RANK_NONE);
  assert(current->board[35].rank == BOOP_PIECE_RANK_KITTEN);
  assert(current->cats_in_supply[0] == 1);
  assert(current->promoted_count[0] == 1);
  assert(!board_selection_controller_completion_pending(controller));

  backend->position_clear(&position);
  g_clear_object(&controller);
  g_clear_object(&model);
}

static void test_backend_selection_controller_auto_applies_single_boop_line_promotion(void) {
  if (ggame_active_app_profile()->kind != GGAME_APP_KIND_BOOP) {
    return;
  }

  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  assert(backend != NULL);
  assert(backend->supports_move_builder);

  GGameModel *model = ggame_model_new(backend);
  assert(model != NULL);

  BoopPosition position = {0};
  backend->position_init(&position, NULL);
  position.kittens_in_supply[0] = 6;
  position.board[0] = (BoopPiece){
    .side = 0,
    .rank = BOOP_PIECE_RANK_KITTEN,
  };
  position.board[1] = (BoopPiece){
    .side = 0,
    .rank = BOOP_PIECE_RANK_KITTEN,
  };
  assert(ggame_model_set_position(model, &position));

  BoardSelectionController *controller = board_selection_controller_new();
  assert(controller != NULL);

  TestBackendSelectionData data = {.model = model};
  board_selection_controller_set_model(controller, model);
  board_selection_controller_set_move_handler(controller, test_backend_apply_model_move, &data);
  board_selection_controller_set_completion_confirmation(controller, test_backend_confirm_boop_promotion, NULL);

  assert(board_selection_controller_handle_click(controller, 2));
  assert(!board_selection_controller_completion_pending(controller));

  const BoopPosition *current = ggame_model_peek_position(model);
  assert(current != NULL);
  assert(current->turn == 1);
  assert(current->board[0].rank == BOOP_PIECE_RANK_NONE);
  assert(current->board[1].rank == BOOP_PIECE_RANK_NONE);
  assert(current->board[2].rank == BOOP_PIECE_RANK_NONE);
  assert(current->cats_in_supply[0] == 3);
  assert(current->promoted_count[0] == 3);

  backend->position_clear(&position);
  g_clear_object(&controller);
  g_clear_object(&model);
}

static void test_backend_selection_controller_confirms_boop_line_choice_only_from_button(void) {
  if (ggame_active_app_profile()->kind != GGAME_APP_KIND_BOOP) {
    return;
  }

  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  assert(backend != NULL);
  assert(backend->supports_move_builder);

  GGameModel *model = ggame_model_new(backend);
  assert(model != NULL);

  BoopPosition position = {0};
  backend->position_init(&position, NULL);
  position.board[1] = (BoopPiece){
    .side = 0,
    .rank = BOOP_PIECE_RANK_CAT,
  };
  position.board[6] = (BoopPiece){
    .side = 0,
    .rank = BOOP_PIECE_RANK_CAT,
  };
  position.board[8] = (BoopPiece){
    .side = 0,
    .rank = BOOP_PIECE_RANK_CAT,
  };
  position.board[13] = (BoopPiece){
    .side = 0,
    .rank = BOOP_PIECE_RANK_CAT,
  };
  assert(ggame_model_set_position(model, &position));

  BoardSelectionController *controller = board_selection_controller_new();
  assert(controller != NULL);

  TestBackendSelectionData data = {.model = model};
  board_selection_controller_set_model(controller, model);
  board_selection_controller_set_move_handler(controller, test_backend_apply_model_move, &data);
  board_selection_controller_set_completion_confirmation(controller, test_backend_confirm_boop_promotion, NULL);

  assert(board_selection_controller_handle_click(controller, 7));

  const GameBackendMoveBuilder *builder = board_selection_controller_peek_builder(controller);
  assert(builder != NULL);
  assert(builder->builder_state != NULL);
  const BoopMoveBuilderState *builder_state = builder->builder_state;
  assert(builder_state->stage == BOOP_MOVE_BUILDER_STAGE_PROMOTION);
  assert(builder_state->promotion_option_count == 2);

  guint selected_length = 0;
  const guint *selected_path = board_selection_controller_peek_path(controller, &selected_length);
  assert(selected_path != NULL);
  assert(selected_length == 0);
  assert(!board_selection_controller_contains(controller, 7));

  gboolean selectable[BOOP_SQUARE_COUNT] = {FALSE};
  gboolean destinations[BOOP_SQUARE_COUNT] = {FALSE};
  assert(board_selection_controller_collect_highlights(controller,
                                                       selectable,
                                                       destinations,
                                                       G_N_ELEMENTS(selectable)));
  assert(selectable[1]);
  assert(selectable[6]);
  assert(selectable[7]);
  assert(selectable[8]);
  assert(selectable[13]);

  assert(board_selection_controller_handle_click(controller, 7));
  selected_path = board_selection_controller_peek_path(controller, &selected_length);
  assert(selected_path != NULL);
  assert(selected_length == 1);
  assert(selected_path[0] == 7);

  assert(board_selection_controller_handle_click(controller, 1));
  selected_path = board_selection_controller_peek_path(controller, &selected_length);
  assert(selected_path != NULL);
  assert(selected_length == 2);
  assert(selected_path[0] == 7);
  assert(selected_path[1] == 1);

  assert(board_selection_controller_handle_click(controller, 13));
  assert(board_selection_controller_completion_pending(controller));

  const BoopPosition *current = ggame_model_peek_position(model);
  assert(current != NULL);
  assert(current->turn == 0);
  assert(current->board[7].rank == BOOP_PIECE_RANK_NONE);

  assert(board_selection_controller_handle_click(controller, 35));
  assert(!board_selection_controller_completion_pending(controller));
  current = ggame_model_peek_position(model);
  assert(current != NULL);
  assert(current->turn == 0);
  assert(current->board[7].rank == BOOP_PIECE_RANK_NONE);

  assert(board_selection_controller_handle_click(controller, 7));
  assert(board_selection_controller_handle_click(controller, 6));
  assert(board_selection_controller_handle_click(controller, 8));
  assert(board_selection_controller_completion_pending(controller));
  assert(board_selection_controller_confirm(controller));

  current = ggame_model_peek_position(model);
  assert(current != NULL);
  assert(current->turn == 1);
  assert(current->board[6].rank == BOOP_PIECE_RANK_NONE);
  assert(current->board[7].rank == BOOP_PIECE_RANK_NONE);
  assert(current->board[8].rank == BOOP_PIECE_RANK_NONE);
  assert(current->cats_in_supply[0] == 3);
  assert(current->promoted_count[0] == 1);

  backend->position_clear(&position);
  g_clear_object(&controller);
  g_clear_object(&model);
}

static void test_backend_selection_controller_resets_on_model_change(void) {
  if (ggame_active_app_profile()->kind != GGAME_APP_KIND_CHECKERS) {
    return;
  }

  enum { TEST_BACKEND_SELECTION_SQUARE_CAPACITY = 128 };
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  assert(backend != NULL);
  assert(backend->supports_move_builder);
  assert(backend->square_grid_move_get_path != NULL);

  GGameModel *model = ggame_model_new(backend);
  assert(model != NULL);
  BoardSelectionController *controller = board_selection_controller_new();
  assert(controller != NULL);

  TestBackendSelectionData data = {.model = model};
  board_selection_controller_set_model(controller, model);
  board_selection_controller_set_move_handler(controller, test_backend_apply_model_move, &data);

  GameBackendMoveList white_moves = ggame_model_list_moves(model);
  assert(white_moves.count > 0);
  const void *white_move = backend->move_list_get(&white_moves, 0);
  assert(white_move != NULL);

  guint path[TEST_BACKEND_SELECTION_SQUARE_CAPACITY] = {0};
  guint path_length = 0;
  assert(backend->square_grid_move_get_path(white_move, &path_length, path, G_N_ELEMENTS(path)));
  assert(path_length >= 2);
  for (guint i = 0; i < path_length; ++i) {
    assert(board_selection_controller_handle_click(controller, path[i]));
  }
  backend->move_list_free(&white_moves);

  gboolean black_selectable[TEST_BACKEND_SELECTION_SQUARE_CAPACITY] = {FALSE};
  gboolean black_destinations[TEST_BACKEND_SELECTION_SQUARE_CAPACITY] = {FALSE};
  assert(board_selection_controller_collect_highlights(controller,
                                                       black_selectable,
                                                       black_destinations,
                                                       G_N_ELEMENTS(black_selectable)));

  GameBackendMoveList black_moves = ggame_model_list_moves(model);
  assert(black_moves.count > 0);
  const void *black_move = backend->move_list_get(&black_moves, 0);
  assert(black_move != NULL);
  assert(ggame_model_apply_move(model, black_move));
  backend->move_list_free(&black_moves);

  gboolean expected_white_selectable[TEST_BACKEND_SELECTION_SQUARE_CAPACITY] = {FALSE};
  gboolean actual_white_selectable[TEST_BACKEND_SELECTION_SQUARE_CAPACITY] = {FALSE};
  gboolean actual_destinations[TEST_BACKEND_SELECTION_SQUARE_CAPACITY] = {FALSE};
  test_backend_collect_model_starts(model, expected_white_selectable, G_N_ELEMENTS(expected_white_selectable));
  assert(board_selection_controller_collect_highlights(controller,
                                                       actual_white_selectable,
                                                       actual_destinations,
                                                       G_N_ELEMENTS(actual_white_selectable)));
  assert(test_backend_bool_arrays_equal(actual_white_selectable,
                                        expected_white_selectable,
                                        G_N_ELEMENTS(actual_white_selectable)));

  g_clear_object(&controller);
  g_clear_object(&model);
}

int main(int argc, char **argv) {
  ggame_test_init_profile(&argc, &argv, "checkers");
  test_app_profile_metadata();
  test_backend_metadata();
  test_backend_position_and_move_flow();
  test_backend_move_path_length_only_query();
  test_backend_selection_controller_restarts_from_non_continuation_click();
  test_backend_selection_controller_prefers_boop_rank();
  test_backend_selection_controller_confirms_boop_promotion();
  test_backend_selection_controller_auto_applies_single_boop_line_promotion();
  test_backend_selection_controller_confirms_boop_line_choice_only_from_button();
  test_backend_selection_controller_resets_on_model_change();

  printf("All tests passed.\n");
  return 0;
}
