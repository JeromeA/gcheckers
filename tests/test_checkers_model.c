#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include <glib.h>

#include "../src/ai_alpha_beta.h"
#include "../src/checkers_model.h"

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

  CheckersMove depth1 = {0};
  CheckersMove depth4 = {0};
  CheckersMove depth8 = {0};
  bool selected_1 = gcheckers_model_choose_best_move(model, 1, &depth1);
  bool selected_4 = gcheckers_model_choose_best_move(model, 4, &depth4);
  bool selected_8 = gcheckers_model_choose_best_move(model, 8, &depth8);
  assert(selected_1);
  assert(selected_4);
  assert(selected_8);
  assert(depth1.length >= 2);
  assert(depth4.length >= 2);
  assert(depth8.length >= 2);
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

static void test_model_analyze_moves_text(void) {
  GCheckersModel *model = gcheckers_model_new();

  char *analysis = gcheckers_model_analyze_moves_text(model, 4);
  assert(analysis != NULL);
  const char *nodes_line = strstr(analysis, "Nodes: ");
  assert(nodes_line != NULL);
  guint64 nodes = g_ascii_strtoull(nodes_line + strlen("Nodes: "), NULL, 10);
  assert(nodes > 0);
  assert(strstr(analysis, "Best to worst:") != NULL);
  g_free(analysis);

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

static void test_model_set_rules(void) {
  GCheckersModel *model = gcheckers_model_new();
  const GameState *state = gcheckers_model_peek_state(model);
  assert(state != NULL);
  assert(state->board.board_size == 8);

  CheckersRules international = game_rules_international_draughts();
  gcheckers_model_set_rules(model, &international);

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
  test_model_analyze_moves_text();
  test_model_analyze_moves_progress_callback();
  test_model_analyze_moves_reuses_tt_across_depths();
  test_model_analyze_moves_stats_can_accumulate_across_calls();
  test_model_analyze_moves_stats_can_be_per_call();
  test_model_set_rules();
  test_model_peek_last_move();
  test_model_reset_clears_last_move();

  return 0;
}
