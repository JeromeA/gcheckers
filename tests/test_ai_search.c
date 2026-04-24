#include <assert.h>
#include <string.h>

#include "../src/active_game_backend.h"
#include "../src/ai_search.h"
#include "../src/game.h"
#include "../src/rulesets.h"

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

int main(void) {
  test_ai_search_analyze_and_choose_move();
  test_ai_search_evaluate_terminal_position();
  test_ai_search_tt_roundtrip_and_reuse();

  return 0;
}
