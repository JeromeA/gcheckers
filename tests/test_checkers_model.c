#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include <glib.h>

#include "../src/ai_alpha_beta.h"
#include "../src/checkers_model.h"
#include "../src/rulesets.h"

static gboolean test_checkers_model_move_in_list(const MoveList *moves, const CheckersMove *move) {
  g_return_val_if_fail(moves != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  for (size_t i = 0; i < moves->count; ++i) {
    const CheckersMove *candidate = &moves->moves[i];
    if (candidate->length != move->length || candidate->captures != move->captures) {
      continue;
    }

    if (memcmp(candidate->path, move->path, move->length * sizeof(move->path[0])) == 0) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean test_checkers_model_move_equals(const CheckersMove *left, const CheckersMove *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  if (left->length != right->length || left->captures != right->captures) {
    return FALSE;
  }

  return memcmp(left->path, right->path, left->length * sizeof(left->path[0])) == 0;
}

static gint test_checkers_model_material_score(const Game *game) {
  g_return_val_if_fail(game != NULL, 0);

  const CheckersBoard *board = &game->state.board;
  uint8_t squares = board_playable_squares(board->board_size);
  gint score = 0;
  for (uint8_t i = 0; i < squares; ++i) {
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

    score += board_piece_color(piece) == CHECKERS_COLOR_WHITE ? value : -value;
  }
  return score;
}

static gboolean test_model_find_forced_root_candidate_recursive(const Game *position,
                                                                guint remaining_plies,
                                                                Game *out_forced_position) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(out_forced_position != NULL, FALSE);

  MoveList moves = position->available_moves(position);
  if (moves.count == 1) {
    Game after_forced = *position;
    if (game_apply_move(&after_forced, &moves.moves[0]) == 0) {
      MoveList after_moves = after_forced.available_moves(&after_forced);
      if (after_moves.count > 1) {
        CheckersScoredMoveList depth0 = {0};
        CheckersScoredMoveList depth1 = {0};
        gboolean depth0_ok = checkers_ai_alpha_beta_analyze_moves(position, 0, &depth0);
        gboolean depth1_ok = checkers_ai_alpha_beta_analyze_moves(position, 1, &depth1);
        if (depth0_ok && depth1_ok && depth0.count == 1 && depth1.count == 1 &&
            depth0.moves[0].score != depth1.moves[0].score) {
          checkers_scored_move_list_free(&depth0);
          checkers_scored_move_list_free(&depth1);
          *out_forced_position = *position;
          movelist_free(&after_moves);
          movelist_free(&moves);
          return TRUE;
        }
        checkers_scored_move_list_free(&depth0);
        checkers_scored_move_list_free(&depth1);
      }
      movelist_free(&after_moves);
    }
  }

  if (remaining_plies == 0) {
    movelist_free(&moves);
    return FALSE;
  }

  for (size_t i = 0; i < moves.count; ++i) {
    Game child = *position;
    if (game_apply_move(&child, &moves.moves[i]) != 0) {
      continue;
    }
    if (test_model_find_forced_root_candidate_recursive(&child, remaining_plies - 1, out_forced_position)) {
      movelist_free(&moves);
      return TRUE;
    }
  }

  movelist_free(&moves);
  return FALSE;
}

static gboolean test_model_find_forced_root_candidate(Game *out_forced_position) {
  g_return_val_if_fail(out_forced_position != NULL, FALSE);

  const CheckersRules *rules = checkers_ruleset_get_rules(PLAYER_RULESET_AMERICAN);
  g_return_val_if_fail(rules != NULL, FALSE);

  Game root = {0};
  game_init_with_rules(&root, rules);
  gboolean found = test_model_find_forced_root_candidate_recursive(&root, 14, out_forced_position);
  game_destroy(&root);
  return found;
}

typedef struct {
  guint calls;
  guint64 last_nodes;
} ProgressProbe;

static void test_model_analysis_progress_probe(const CheckersAiSearchStats *stats, gpointer user_data) {
  ProgressProbe *probe = user_data;
  g_return_if_fail(stats != NULL);
  g_return_if_fail(probe != NULL);

  assert(stats->nodes >= probe->last_nodes);
  probe->last_nodes = stats->nodes;
  probe->calls++;
}

static void test_model_reset_and_moves(void) {
  GCheckersModel *model = gcheckers_model_new();

  const GameState *state = gcheckers_model_peek_state(model);
  assert(state->winner == CHECKERS_WINNER_NONE);
  assert(state->turn == CHECKERS_COLOR_WHITE);

  MoveList moves = gcheckers_model_list_moves(model);
  assert(moves.count > 0);

  CheckersMove first_move = moves.moves[0];
  movelist_free(&moves);

  bool moved = gcheckers_model_apply_move(model, &first_move);
  assert(moved);

  const GameState *after_move = gcheckers_model_peek_state(model);
  assert(after_move->turn == CHECKERS_COLOR_BLACK);

  char *status = gcheckers_model_format_status(model);
  assert(status != NULL);
  g_free(status);

  gcheckers_model_reset(model);
  const GameState *after_reset = gcheckers_model_peek_state(model);
  assert(after_reset->winner == CHECKERS_WINNER_NONE);
  assert(after_reset->turn == CHECKERS_COLOR_WHITE);

  g_object_unref(model);
}

static void test_model_rejects_invalid_move(void) {
  GCheckersModel *model = gcheckers_model_new();

  CheckersMove invalid_move;
  memset(&invalid_move, 0, sizeof(invalid_move));
  invalid_move.length = 2;
  invalid_move.path[0] = 0;
  invalid_move.path[1] = 1;

  const GameState *before = gcheckers_model_peek_state(model);
  assert(before->turn == CHECKERS_COLOR_WHITE);

  bool moved = gcheckers_model_apply_move(model, &invalid_move);
  assert(!moved);

  const GameState *after = gcheckers_model_peek_state(model);
  assert(after->turn == CHECKERS_COLOR_WHITE);

  g_object_unref(model);
}

static void test_model_choose_best_move_returns_legal_move(void) {
  GCheckersModel *model = gcheckers_model_new();

  MoveList moves = gcheckers_model_list_moves(model);
  assert(moves.count > 0);

  CheckersMove depth0 = {0};
  CheckersMove depth1 = {0};
  CheckersMove depth4 = {0};
  CheckersMove depth8 = {0};
  bool selected_0 = gcheckers_model_choose_best_move(model, 0, &depth0);
  bool selected_1 = gcheckers_model_choose_best_move(model, 1, &depth1);
  bool selected_4 = gcheckers_model_choose_best_move(model, 4, &depth4);
  bool selected_8 = gcheckers_model_choose_best_move(model, 8, &depth8);
  assert(selected_0);
  assert(selected_1);
  assert(selected_4);
  assert(selected_8);
  assert(depth0.length >= 2);
  assert(depth1.length >= 2);
  assert(depth4.length >= 2);
  assert(depth8.length >= 2);
  assert(test_checkers_model_move_in_list(&moves, &depth0));
  assert(test_checkers_model_move_in_list(&moves, &depth1));
  assert(test_checkers_model_move_in_list(&moves, &depth4));
  assert(test_checkers_model_move_in_list(&moves, &depth8));

  movelist_free(&moves);
  g_object_unref(model);
}

static void test_model_choose_best_move_randomized_within_best_ties(void) {
  GCheckersModel *model = gcheckers_model_new();

  Game game = {0};
  bool copied = gcheckers_model_copy_game(model, &game);
  assert(copied);

  CheckersScoredMoveList scored_moves = {0};
  bool analyzed = checkers_ai_alpha_beta_analyze_moves(&game, 1, &scored_moves);
  assert(analyzed);
  assert(scored_moves.count > 1);

  gint best_score = scored_moves.moves[0].score;
  size_t best_count = 1;
  while (best_count < scored_moves.count && scored_moves.moves[best_count].score == best_score) {
    best_count++;
  }
  assert(best_count > 1);

  const guint seed = 0xC0FFEEu;
  g_random_set_seed(seed);
  guint expected_index = g_random_int_range(0, (gint)best_count);

  CheckersMove selected = {0};
  g_random_set_seed(seed);
  bool chosen = gcheckers_model_choose_best_move(model, 1, &selected);
  assert(chosen);
  assert(test_checkers_model_move_equals(&selected, &scored_moves.moves[expected_index].move));

  checkers_scored_move_list_free(&scored_moves);
  g_object_unref(model);
}

static gboolean test_model_find_black_position_with_non_equal_scores(Game *out_game, guint depth) {
  g_return_val_if_fail(out_game != NULL, FALSE);
  g_return_val_if_fail(depth > 0, FALSE);

  const CheckersRules *rules = checkers_ruleset_get_rules(PLAYER_RULESET_AMERICAN);
  g_return_val_if_fail(rules != NULL, FALSE);

  Game root = {0};
  game_init_with_rules(&root, rules);

  MoveList root_moves = root.available_moves(&root);
  for (size_t i = 0; i < root_moves.count; ++i) {
    Game child = root;
    if (game_apply_move(&child, &root_moves.moves[i]) != 0) {
      continue;
    }
    if (child.state.turn != CHECKERS_COLOR_BLACK) {
      continue;
    }

    CheckersScoredMoveList scored_moves = {0};
    gboolean ok = checkers_ai_alpha_beta_analyze_moves(&child, depth, &scored_moves);
    if (!ok || scored_moves.count < 2) {
      checkers_scored_move_list_free(&scored_moves);
      continue;
    }

    gboolean has_non_equal_scores = FALSE;
    gint first_score = scored_moves.moves[0].score;
    for (size_t j = 1; j < scored_moves.count; ++j) {
      if (scored_moves.moves[j].score != first_score) {
        has_non_equal_scores = TRUE;
        break;
      }
    }
    checkers_scored_move_list_free(&scored_moves);

    if (!has_non_equal_scores) {
      continue;
    }

    *out_game = child;
    movelist_free(&root_moves);
    game_destroy(&root);
    return TRUE;
  }

  movelist_free(&root_moves);
  game_destroy(&root);
  return FALSE;
}

static void test_model_evaluate_position_uses_white_perspective_signs(void) {
  const CheckersRules *rules = checkers_ruleset_get_rules(PLAYER_RULESET_AMERICAN);
  assert(rules != NULL);

  Game game = {0};
  game_init_with_rules(&game, rules);

  game.state.turn = CHECKERS_COLOR_BLACK;
  game.state.winner = CHECKERS_WINNER_WHITE;
  gint white_win_score = 0;
  gboolean ok = checkers_ai_alpha_beta_evaluate_position(&game, 2, &white_win_score);
  assert(ok);
  assert(white_win_score > 0);

  game.state.turn = CHECKERS_COLOR_WHITE;
  game.state.winner = CHECKERS_WINNER_BLACK;
  gint black_win_score = 0;
  ok = checkers_ai_alpha_beta_evaluate_position(&game, 2, &black_win_score);
  assert(ok);
  assert(black_win_score < 0);

  game_destroy(&game);
}

static void test_model_evaluate_position_depth0_allowed(void) {
  const CheckersRules *rules = checkers_ruleset_get_rules(PLAYER_RULESET_AMERICAN);
  assert(rules != NULL);

  Game game = {0};
  game_init_with_rules(&game, rules);

  gint score = 0;
  gboolean ok = checkers_ai_alpha_beta_evaluate_position(&game, 0, &score);
  assert(ok);

  game_destroy(&game);
}

static void test_model_evaluate_static_material_matches_board(void) {
  const CheckersRules *rules = checkers_ruleset_get_rules(PLAYER_RULESET_AMERICAN);
  assert(rules != NULL);

  Game game = {0};
  game_init_with_rules(&game, rules);

  gint static_score = 0;
  gboolean ok = checkers_ai_evaluate_static_material(&game, &static_score);
  assert(ok);
  assert(static_score == test_checkers_model_material_score(&game));

  MoveList moves = game.available_moves(&game);
  assert(moves.count > 0);
  int rc = game_apply_move(&game, &moves.moves[0]);
  movelist_free(&moves);
  assert(rc == 0);

  static_score = 0;
  ok = checkers_ai_evaluate_static_material(&game, &static_score);
  assert(ok);
  assert(static_score == test_checkers_model_material_score(&game));

  game_destroy(&game);
}

static void test_model_search_static_bonus_uses_both_king_rows(void) {
  const CheckersRules *rules = checkers_ruleset_get_rules(PLAYER_RULESET_AMERICAN);
  assert(rules != NULL);

  Game game = {0};
  game_init_with_rules(&game, rules);

  uint8_t squares = board_playable_squares(game.state.board.board_size);
  for (uint8_t i = 0; i < squares; ++i) {
    board_set(&game.state.board, i, CHECKERS_PIECE_EMPTY);
  }

  int8_t white_index = board_index_from_coord(4, 3, game.state.board.board_size);
  int8_t black_index = board_index_from_coord(6, 3, game.state.board.board_size);
  assert(white_index >= 0);
  assert(black_index >= 0);
  board_set(&game.state.board, (uint8_t)white_index, CHECKERS_PIECE_WHITE_MAN);
  board_set(&game.state.board, (uint8_t)black_index, CHECKERS_PIECE_BLACK_MAN);

  gint static_score = 0;
  gboolean ok = checkers_ai_evaluate_static_material(&game, &static_score);
  assert(ok);
  assert(static_score == 0);

  game.state.turn = CHECKERS_COLOR_WHITE;
  MoveList white_moves = game.available_moves(&game);
  assert(white_moves.count > 1);
  movelist_free(&white_moves);

  gint white_score = 0;
  ok = checkers_ai_alpha_beta_evaluate_position(&game, 0, &white_score);
  assert(ok);
  assert(white_score == 3);

  game.state.turn = CHECKERS_COLOR_BLACK;
  MoveList black_moves = game.available_moves(&game);
  assert(black_moves.count > 1);
  movelist_free(&black_moves);

  gint black_score = 0;
  ok = checkers_ai_alpha_beta_evaluate_position(&game, 0, &black_score);
  assert(ok);
  assert(black_score == -3);

  game_destroy(&game);
}

static void test_model_analyze_moves_black_turn_sorts_low_to_high(void) {
  Game game = {0};
  gboolean found = test_model_find_black_position_with_non_equal_scores(&game, 4);
  assert(found);

  CheckersScoredMoveList scored_moves = {0};
  gboolean ok = checkers_ai_alpha_beta_analyze_moves(&game, 4, &scored_moves);
  assert(ok);
  assert(scored_moves.count > 1);

  gboolean has_strict_increase = FALSE;
  for (size_t i = 1; i < scored_moves.count; ++i) {
    assert(scored_moves.moves[i - 1].score <= scored_moves.moves[i].score);
    if (scored_moves.moves[i - 1].score < scored_moves.moves[i].score) {
      has_strict_increase = TRUE;
    }
  }
  assert(has_strict_increase);

  checkers_scored_move_list_free(&scored_moves);
  game_destroy(&game);
}

static void test_model_analyze_moves_structured(void) {
  GCheckersModel *model = gcheckers_model_new();

  CheckersScoredMoveList moves_depth0 = {0};
  CheckersAiSearchStats stats_depth0 = {0};
  gboolean ok0 = gcheckers_model_analyze_moves(model, 0, &moves_depth0, &stats_depth0);
  assert(ok0);
  assert(moves_depth0.count > 0);
  assert(stats_depth0.nodes > 0);
  assert(stats_depth0.tt_probes >= stats_depth0.tt_hits);
  checkers_scored_move_list_free(&moves_depth0);

  CheckersScoredMoveList moves = {0};
  CheckersAiSearchStats stats = {0};
  gboolean ok = gcheckers_model_analyze_moves(model, 4, &moves, &stats);
  assert(ok);
  assert(moves.count > 0);
  assert(stats.nodes > 0);
  assert(stats.tt_probes >= stats.tt_hits);
  checkers_scored_move_list_free(&moves);

  g_object_unref(model);
}

static void test_model_analyze_moves_progress_callback(void) {
  GCheckersModel *model = gcheckers_model_new();
  Game game = {0};
  bool copied = gcheckers_model_copy_game(model, &game);
  assert(copied);

  ProgressProbe probe = {0};
  CheckersScoredMoveList scored_moves = {0};
  CheckersAiSearchStats stats = {0};
  checkers_ai_search_stats_clear(&stats);
  gboolean analyzed = checkers_ai_alpha_beta_analyze_moves_cancellable_with_tt(&game,
                                                                                5,
                                                                                &scored_moves,
                                                                                NULL,
                                                                                NULL,
                                                                                test_model_analysis_progress_probe,
                                                                                &probe,
                                                                                NULL,
                                                                                &stats);
  assert(analyzed);
  assert(scored_moves.count > 0);
  assert(stats.nodes > 0);
  assert(probe.calls > 0);
  assert(probe.last_nodes > 0);

  checkers_scored_move_list_free(&scored_moves);
  g_object_unref(model);
}

static void test_model_analyze_moves_reuses_tt_across_depths(void) {
  GCheckersModel *model = gcheckers_model_new();
  Game game = {0};
  bool copied = gcheckers_model_copy_game(model, &game);
  assert(copied);

  CheckersAiTranspositionTable *tt = checkers_ai_tt_new(16);
  assert(tt != NULL);

  CheckersScoredMoveList first = {0};
  CheckersAiSearchStats stats_first = {0};
  checkers_ai_search_stats_clear(&stats_first);
  gboolean ok = checkers_ai_alpha_beta_analyze_moves_cancellable_with_tt(&game,
                                                                          5,
                                                                          &first,
                                                                          NULL,
                                                                          NULL,
                                                                          NULL,
                                                                          NULL,
                                                                          tt,
                                                                          &stats_first);
  assert(ok);
  assert(first.count > 0);
  assert(stats_first.nodes > 0);
  checkers_scored_move_list_free(&first);

  CheckersScoredMoveList second = {0};
  CheckersAiSearchStats stats_second = {0};
  checkers_ai_search_stats_clear(&stats_second);
  ok = checkers_ai_alpha_beta_analyze_moves_cancellable_with_tt(&game,
                                                                 6,
                                                                 &second,
                                                                 NULL,
                                                                 NULL,
                                                                 NULL,
                                                                 NULL,
                                                                 tt,
                                                                 &stats_second);
  assert(ok);
  assert(second.count > 0);
  assert(stats_second.nodes > 0);
  assert(stats_second.tt_hits > 0);
  checkers_scored_move_list_free(&second);

  checkers_ai_tt_free(tt);
  g_object_unref(model);
}

static void test_model_analyze_moves_stats_can_accumulate_across_calls(void) {
  GCheckersModel *model = gcheckers_model_new();
  Game game = {0};
  bool copied = gcheckers_model_copy_game(model, &game);
  assert(copied);

  CheckersAiTranspositionTable *tt = checkers_ai_tt_new(16);
  assert(tt != NULL);

  CheckersAiSearchStats stats = {0};
  checkers_ai_search_stats_clear(&stats);
  CheckersScoredMoveList first = {0};
  gboolean ok = checkers_ai_alpha_beta_analyze_moves_cancellable_with_tt(&game,
                                                                          5,
                                                                          &first,
                                                                          NULL,
                                                                          NULL,
                                                                          NULL,
                                                                          NULL,
                                                                          tt,
                                                                          &stats);
  assert(ok);
  assert(first.count > 0);
  assert(stats.nodes > 0);
  guint64 nodes_after_first = stats.nodes;
  guint64 probes_after_first = stats.tt_probes;
  guint64 hits_after_first = stats.tt_hits;
  checkers_scored_move_list_free(&first);

  CheckersScoredMoveList second = {0};
  ok = checkers_ai_alpha_beta_analyze_moves_cancellable_with_tt(&game,
                                                                 6,
                                                                 &second,
                                                                 NULL,
                                                                 NULL,
                                                                 NULL,
                                                                 NULL,
                                                                 tt,
                                                                 &stats);
  assert(ok);
  assert(second.count > 0);
  assert(stats.nodes > nodes_after_first);
  assert(stats.tt_probes > probes_after_first);
  assert(stats.tt_hits >= hits_after_first);
  checkers_scored_move_list_free(&second);

  checkers_ai_tt_free(tt);
  g_object_unref(model);
}

static void test_model_analyze_moves_stats_can_be_per_call(void) {
  GCheckersModel *model = gcheckers_model_new();
  Game game = {0};
  bool copied = gcheckers_model_copy_game(model, &game);
  assert(copied);

  CheckersAiTranspositionTable *tt = checkers_ai_tt_new(16);
  assert(tt != NULL);

  CheckersAiSearchStats first_stats = {0};
  checkers_ai_search_stats_clear(&first_stats);
  CheckersScoredMoveList first = {0};
  gboolean ok = checkers_ai_alpha_beta_analyze_moves_cancellable_with_tt(&game,
                                                                          5,
                                                                          &first,
                                                                          NULL,
                                                                          NULL,
                                                                          NULL,
                                                                          NULL,
                                                                          tt,
                                                                          &first_stats);
  assert(ok);
  assert(first.count > 0);
  assert(first_stats.nodes > 0);
  checkers_scored_move_list_free(&first);

  CheckersAiSearchStats second_stats = {0};
  checkers_ai_search_stats_clear(&second_stats);
  CheckersScoredMoveList second = {0};
  ok = checkers_ai_alpha_beta_analyze_moves_cancellable_with_tt(&game,
                                                                 6,
                                                                 &second,
                                                                 NULL,
                                                                 NULL,
                                                                 NULL,
                                                                 NULL,
                                                                 tt,
                                                                 &second_stats);
  assert(ok);
  assert(second.count > 0);
  assert(second_stats.nodes > 0);
  assert(second_stats.tt_probes >= second_stats.tt_hits);
  checkers_scored_move_list_free(&second);

  checkers_ai_tt_free(tt);
  g_object_unref(model);
}

static void test_model_forced_move_does_not_consume_depth(void) {
  Game forced_position = {0};
  gboolean found = test_model_find_forced_root_candidate(&forced_position);
  assert(found);

  CheckersScoredMoveList depth0 = {0};
  CheckersScoredMoveList depth1 = {0};
  gboolean ok = checkers_ai_alpha_beta_analyze_moves(&forced_position, 0, &depth0);
  assert(ok);
  assert(depth0.count == 1);

  ok = checkers_ai_alpha_beta_analyze_moves(&forced_position, 1, &depth1);
  assert(ok);
  assert(depth1.count == 1);

  assert(depth0.moves[0].score != depth1.moves[0].score);

  checkers_scored_move_list_free(&depth0);
  checkers_scored_move_list_free(&depth1);
}

static void test_model_set_rules(void) {
  GCheckersModel *model = gcheckers_model_new();
  const GameState *state = gcheckers_model_peek_state(model);
  assert(state != NULL);
  assert(state->board.board_size == 8);

  const CheckersRules *international = checkers_ruleset_get_rules(PLAYER_RULESET_INTERNATIONAL);
  assert(international != NULL);
  gcheckers_model_set_rules(model, international);

  state = gcheckers_model_peek_state(model);
  assert(state != NULL);
  assert(state->board.board_size == 10);
  assert(state->turn == CHECKERS_COLOR_WHITE);
  assert(state->winner == CHECKERS_WINNER_NONE);

  g_object_unref(model);
}

static void test_model_peek_last_move(void) {
  GCheckersModel *model = gcheckers_model_new();

  const CheckersMove *last_move = gcheckers_model_peek_last_move(model);
  assert(last_move == NULL);

  MoveList moves = gcheckers_model_list_moves(model);
  assert(moves.count > 0);
  CheckersMove first_move = moves.moves[0];
  movelist_free(&moves);

  bool moved = gcheckers_model_apply_move(model, &first_move);
  assert(moved);

  last_move = gcheckers_model_peek_last_move(model);
  assert(last_move != NULL);
  assert(last_move->length == first_move.length);
  assert(last_move->captures == first_move.captures);
  assert(memcmp(last_move->path,
                first_move.path,
                first_move.length * sizeof(first_move.path[0])) == 0);

  g_object_unref(model);
}

static void test_model_reset_clears_last_move(void) {
  GCheckersModel *model = gcheckers_model_new();

  const CheckersMove *last_move = gcheckers_model_peek_last_move(model);
  assert(last_move == NULL);

  MoveList moves = gcheckers_model_list_moves(model);
  assert(moves.count > 0);
  CheckersMove first_move = moves.moves[0];
  movelist_free(&moves);

  bool moved = gcheckers_model_apply_move(model, &first_move);
  assert(moved);
  last_move = gcheckers_model_peek_last_move(model);
  assert(last_move != NULL);

  gcheckers_model_reset(model);
  last_move = gcheckers_model_peek_last_move(model);
  assert(last_move == NULL);

  g_object_unref(model);
}

int main(void) {
  test_model_reset_and_moves();
  test_model_rejects_invalid_move();
  test_model_choose_best_move_returns_legal_move();
  test_model_choose_best_move_randomized_within_best_ties();
  test_model_evaluate_position_depth0_allowed();
  test_model_evaluate_static_material_matches_board();
  test_model_search_static_bonus_uses_both_king_rows();
  test_model_evaluate_position_uses_white_perspective_signs();
  test_model_analyze_moves_black_turn_sorts_low_to_high();
  test_model_analyze_moves_structured();
  test_model_analyze_moves_progress_callback();
  test_model_analyze_moves_reuses_tt_across_depths();
  test_model_analyze_moves_stats_can_accumulate_across_calls();
  test_model_analyze_moves_stats_can_be_per_call();
  test_model_forced_move_does_not_consume_depth();
  test_model_set_rules();
  test_model_peek_last_move();
  test_model_reset_clears_last_move();

  return 0;
}
