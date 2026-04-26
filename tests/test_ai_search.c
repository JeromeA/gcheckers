#include <assert.h>
#include <string.h>

#include "../src/active_game_backend.h"
#include "../src/ai_search.h"
#include "../src/games/checkers/game.h"
#include "../src/games/checkers/rulesets.h"

typedef struct {
  gint total;
  guint turn;
} TestGoodMoveOnlyPosition;

typedef struct {
  gint delta;
} TestGoodMoveOnlyMove;

static void test_init_game_with_ruleset(Game *game, PlayerRuleset ruleset) {
  const CheckersRules *rules = checkers_ruleset_get_rules(ruleset);

  assert(game != NULL);
  assert(rules != NULL);
  game_init_with_rules(game, rules);
}

static gboolean test_ai_search_move_in_list(const GameBackend *backend,
                                            const GameBackendMoveList *moves,
                                            gconstpointer move) {
  assert(backend != NULL);
  assert(moves != NULL);
  assert(move != NULL);

  for (gsize i = 0; i < moves->count; ++i) {
    const void *candidate = backend->move_list_get(moves, i);
    if (candidate != NULL && backend->moves_equal(candidate, move)) {
      return TRUE;
    }
  }

  return FALSE;
}

static void test_ai_search_analyze_and_choose_move(void) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  Game game = {0};
  GameAiScoredMoveList scored_moves = {0};
  CheckersMove selected = {0};

  assert(backend != NULL);
  test_init_game_with_ruleset(&game, PLAYER_RULESET_AMERICAN);

  assert(game_ai_search_analyze_moves(backend, &game, 2, &scored_moves));
  assert(scored_moves.count > 0);
  for (gsize i = 1; i < scored_moves.count; ++i) {
    assert(scored_moves.moves[i - 1].score >= scored_moves.moves[i].score);
  }

  GameBackendMoveList legal_moves = backend->list_moves(&game);
  assert(legal_moves.count > 0);

  assert(game_ai_search_choose_move(backend, &game, 2, &selected));
  assert(test_ai_search_move_in_list(backend, &legal_moves, &selected));

  backend->move_list_free(&legal_moves);
  game_ai_scored_move_list_free(&scored_moves);
  game_destroy(&game);
}

static void test_ai_search_evaluate_terminal_position(void) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  Game game = {0};
  gint white_score = 0;
  gint black_score = 0;

  assert(backend != NULL);
  test_init_game_with_ruleset(&game, PLAYER_RULESET_AMERICAN);

  game.state.winner = CHECKERS_WINNER_WHITE;
  assert(game_ai_search_evaluate_position(backend, &game, 2, &white_score));
  assert(white_score > 0);

  game.state.winner = CHECKERS_WINNER_BLACK;
  assert(game_ai_search_evaluate_position(backend, &game, 2, &black_score));
  assert(black_score < 0);

  game_destroy(&game);
}

static void test_ai_search_tt_roundtrip_and_reuse(void) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  Game game = {0};
  GameAiTranspositionTable *tt = NULL;
  GameBackendMoveList legal_moves = {0};
  GameAiTtEntry entry = {0};
  GameAiSearchStats first_stats = {0};
  GameAiSearchStats second_stats = {0};
  GameAiScoredMoveList first_moves = {0};
  GameAiScoredMoveList second_moves = {0};
  guint64 key = 0;

  assert(backend != NULL);
  test_init_game_with_ruleset(&game, PLAYER_RULESET_AMERICAN);

  tt = game_ai_tt_new(1, backend->move_size);
  assert(tt != NULL);

  legal_moves = backend->list_moves(&game);
  assert(legal_moves.count > 0);
  const void *first_move = backend->move_list_get(&legal_moves, 0);
  assert(first_move != NULL);

  key = backend->hash_position(&game);
  game_ai_tt_store(tt, key, 4, 123, GAME_AI_TT_BOUND_EXACT, first_move);
  assert(game_ai_tt_lookup(tt, key, &entry));
  assert(entry.valid);
  assert(entry.depth == 4);
  assert(entry.score == 123);
  assert(entry.best_move != NULL);
  assert(backend->moves_equal(entry.best_move, first_move));
  game_ai_tt_entry_clear(&entry);

  game_ai_search_stats_clear(&first_stats);
  assert(game_ai_search_analyze_moves_cancellable_with_tt(backend,
                                                          &game,
                                                          4,
                                                          &first_moves,
                                                          NULL,
                                                          NULL,
                                                          NULL,
                                                          NULL,
                                                          tt,
                                                          &first_stats));
  assert(first_moves.count > 0);

  game_ai_search_stats_clear(&second_stats);
  assert(game_ai_search_analyze_moves_cancellable_with_tt(backend,
                                                          &game,
                                                          4,
                                                          &second_moves,
                                                          NULL,
                                                          NULL,
                                                          NULL,
                                                          NULL,
                                                          tt,
                                                          &second_stats));
  assert(second_moves.count > 0);
  assert(second_stats.tt_hits > 0);

  backend->move_list_free(&legal_moves);
  game_ai_scored_move_list_free(&first_moves);
  game_ai_scored_move_list_free(&second_moves);
  game_ai_tt_free(tt);
  game_destroy(&game);
}

static void test_ai_search_rejects_backend_without_ai(void) {
  static const GameBackend no_ai_backend = {
    .id = "no-ai",
    .display_name = "No AI",
    .variant_count = 0,
    .position_size = sizeof(gint),
    .move_size = sizeof(gint),
    .supports_move_list = FALSE,
    .supports_move_builder = TRUE,
    .supports_ai_search = FALSE,
  };
  gint position = 0;
  gint score = 0;
  gint move = 0;
  GameAiScoredMoveList scored_moves = {0};

  assert(!game_ai_search_analyze_moves(&no_ai_backend, &position, 1, &scored_moves));
  assert(!game_ai_search_evaluate_position(&no_ai_backend, &position, 1, &score));
  assert(!game_ai_search_choose_move(&no_ai_backend, &position, 1, &move));
  assert(!game_ai_evaluate_static(&no_ai_backend, &position, &score));
}

static void test_good_move_only_position_init(gpointer position, const GameBackendVariant * /*variant_or_null*/) {
  TestGoodMoveOnlyPosition *good_position = position;

  g_return_if_fail(good_position != NULL);

  good_position->total = 0;
  good_position->turn = 0;
}

static void test_good_move_only_position_clear(gpointer /*position*/) {
}

static void test_good_move_only_position_copy(gpointer dest, gconstpointer src) {
  TestGoodMoveOnlyPosition *dest_position = dest;
  const TestGoodMoveOnlyPosition *src_position = src;

  g_return_if_fail(dest_position != NULL);
  g_return_if_fail(src_position != NULL);

  *dest_position = *src_position;
}

static GameBackendOutcome test_good_move_only_position_outcome(gconstpointer position) {
  const TestGoodMoveOnlyPosition *good_position = position;

  g_return_val_if_fail(good_position != NULL, GAME_BACKEND_OUTCOME_ONGOING);

  if (good_position->total >= 2) {
    return GAME_BACKEND_OUTCOME_SIDE_0_WIN;
  }

  return GAME_BACKEND_OUTCOME_ONGOING;
}

static guint test_good_move_only_position_turn(gconstpointer position) {
  const TestGoodMoveOnlyPosition *good_position = position;

  g_return_val_if_fail(good_position != NULL, 0);

  return good_position->turn;
}

static GameBackendMoveList test_good_move_only_list_good_moves(gconstpointer position,
                                                              guint max_count,
                                                              guint /*depth_hint*/) {
  const TestGoodMoveOnlyPosition *good_position = position;
  TestGoodMoveOnlyMove *moves = NULL;
  gsize count = 0;

  g_return_val_if_fail(good_position != NULL, (GameBackendMoveList){0});

  if (good_position->total >= 2) {
    return (GameBackendMoveList){0};
  }

  moves = g_new0(TestGoodMoveOnlyMove, 2);
  g_return_val_if_fail(moves != NULL, (GameBackendMoveList){0});
  moves[count++].delta = 2;
  if (max_count == 0 || max_count > 1) {
    moves[count++].delta = 1;
  }

  return (GameBackendMoveList){
    .moves = moves,
    .count = count,
  };
}

static void test_good_move_only_move_list_free(GameBackendMoveList *moves) {
  g_return_if_fail(moves != NULL);

  g_clear_pointer(&moves->moves, g_free);
  moves->count = 0;
}

static const void *test_good_move_only_move_list_get(const GameBackendMoveList *moves, gsize index) {
  const TestGoodMoveOnlyMove *entries = NULL;

  g_return_val_if_fail(moves != NULL, NULL);
  g_return_val_if_fail(index < moves->count, NULL);

  entries = moves->moves;
  g_return_val_if_fail(entries != NULL, NULL);
  return &entries[index];
}

static gboolean test_good_move_only_moves_equal(gconstpointer left, gconstpointer right) {
  const TestGoodMoveOnlyMove *left_move = left;
  const TestGoodMoveOnlyMove *right_move = right;

  g_return_val_if_fail(left_move != NULL, FALSE);
  g_return_val_if_fail(right_move != NULL, FALSE);

  return left_move->delta == right_move->delta;
}

static gboolean test_good_move_only_apply_move(gpointer position, gconstpointer move) {
  TestGoodMoveOnlyPosition *good_position = position;
  const TestGoodMoveOnlyMove *good_move = move;

  g_return_val_if_fail(good_position != NULL, FALSE);
  g_return_val_if_fail(good_move != NULL, FALSE);

  good_position->total += good_move->delta;
  good_position->turn = 1 - good_position->turn;
  return TRUE;
}

static gint test_good_move_only_evaluate_static(gconstpointer position) {
  const TestGoodMoveOnlyPosition *good_position = position;

  g_return_val_if_fail(good_position != NULL, 0);

  return good_position->total * 10;
}

static gint test_good_move_only_terminal_score(GameBackendOutcome outcome, guint ply_depth) {
  return outcome == GAME_BACKEND_OUTCOME_SIDE_0_WIN ? 3000 - (gint) ply_depth : 0;
}

static guint64 test_good_move_only_hash_position(gconstpointer position) {
  const TestGoodMoveOnlyPosition *good_position = position;

  g_return_val_if_fail(good_position != NULL, 0);

  return ((guint64) (good_position->total + 7) << 8) | good_position->turn;
}

static void test_ai_search_accepts_good_move_only_backend(void) {
  static const GameBackend good_move_only_backend = {
    .id = "good-move-only",
    .display_name = "Good Move Only",
    .variant_count = 0,
    .position_size = sizeof(TestGoodMoveOnlyPosition),
    .move_size = sizeof(TestGoodMoveOnlyMove),
    .supports_move_list = FALSE,
    .supports_move_builder = FALSE,
    .supports_ai_search = TRUE,
    .position_init = test_good_move_only_position_init,
    .position_clear = test_good_move_only_position_clear,
    .position_copy = test_good_move_only_position_copy,
    .position_outcome = test_good_move_only_position_outcome,
    .position_turn = test_good_move_only_position_turn,
    .list_good_moves = test_good_move_only_list_good_moves,
    .move_list_free = test_good_move_only_move_list_free,
    .move_list_get = test_good_move_only_move_list_get,
    .moves_equal = test_good_move_only_moves_equal,
    .apply_move = test_good_move_only_apply_move,
    .evaluate_static = test_good_move_only_evaluate_static,
    .terminal_score = test_good_move_only_terminal_score,
    .hash_position = test_good_move_only_hash_position,
  };
  TestGoodMoveOnlyPosition position = {0};
  GameAiScoredMoveList scored_moves = {0};
  TestGoodMoveOnlyMove selected = {0};

  test_good_move_only_position_init(&position, NULL);
  assert(game_ai_search_analyze_moves(&good_move_only_backend, &position, 2, &scored_moves));
  assert(scored_moves.count == 2);
  assert(((TestGoodMoveOnlyMove *) scored_moves.moves[0].move)->delta == 2);
  game_ai_scored_move_list_free(&scored_moves);

  assert(game_ai_search_choose_move(&good_move_only_backend, &position, 2, &selected));
  assert(selected.delta == 2);
}

int main(void) {
  test_ai_search_analyze_and_choose_move();
  test_ai_search_evaluate_terminal_position();
  test_ai_search_tt_roundtrip_and_reuse();
  test_ai_search_rejects_backend_without_ai();
  test_ai_search_accepts_good_move_only_backend();

  return 0;
}
