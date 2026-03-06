#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include <glib.h>

#include "../src/position_search.h"
#include "../src/rulesets.h"

typedef struct {
  guint matches;
  guint expected_line_length;
} MatchCounter;

typedef struct {
  uint8_t board_size;
  uint8_t turn;
  uint8_t winner;
  uint8_t board_data[CHECKERS_MAX_SQUARES];
} PositionKey;

static void test_init_game_with_ruleset(Game *game, PlayerRuleset ruleset) {
  assert(game != NULL);

  const CheckersRules *rules = checkers_ruleset_get_rules(ruleset);
  assert(rules != NULL);
  game_init_with_rules(game, rules);
}

static gboolean predicate_true(const Game */*position*/,
                               const CheckersMove */*line*/,
                               guint /*line_length*/,
                               gpointer /*user_data*/) {
  return TRUE;
}

static void count_match(const Game */*position*/, const CheckersMove */*line*/, guint line_length, gpointer user_data) {
  assert(user_data != NULL);

  MatchCounter *counter = user_data;
  assert(line_length == counter->expected_line_length);
  counter->matches++;
}

static PositionKey position_key_from_game(const Game *game) {
  assert(game != NULL);

  PositionKey key = {0};
  key.board_size = game->state.board.board_size;
  key.turn = (uint8_t)game->state.turn;
  key.winner = (uint8_t)game->state.winner;
  memcpy(key.board_data, game->state.board.data, sizeof(key.board_data));
  return key;
}

static guint expected_paths_after_two_plies(const Game *root) {
  assert(root != NULL);

  guint count = 0;
  MoveList first_moves = root->available_moves(root);
  for (size_t i = 0; i < first_moves.count; ++i) {
    Game after_first = *root;
    int rc = game_apply_move(&after_first, &first_moves.moves[i]);
    assert(rc == 0);

    MoveList second_moves = after_first.available_moves(&after_first);
    count += (guint)second_moves.count;
    movelist_free(&second_moves);
  }
  movelist_free(&first_moves);
  return count;
}

static guint expected_unique_positions_after_two_plies(const Game *root) {
  assert(root != NULL);

  GHashTable *unique = g_hash_table_new_full(g_bytes_hash,
                                             g_bytes_equal,
                                             (GDestroyNotify)g_bytes_unref,
                                             NULL);

  MoveList first_moves = root->available_moves(root);
  for (size_t i = 0; i < first_moves.count; ++i) {
    Game after_first = *root;
    int rc = game_apply_move(&after_first, &first_moves.moves[i]);
    assert(rc == 0);

    MoveList second_moves = after_first.available_moves(&after_first);
    for (size_t j = 0; j < second_moves.count; ++j) {
      Game after_second = after_first;
      rc = game_apply_move(&after_second, &second_moves.moves[j]);
      assert(rc == 0);

      PositionKey key = position_key_from_game(&after_second);
      GBytes *bytes = g_bytes_new(&key, sizeof(key));
      g_hash_table_add(unique, bytes);
    }

    movelist_free(&second_moves);
  }

  movelist_free(&first_moves);
  guint unique_count = (guint)g_hash_table_size(unique);
  g_hash_table_destroy(unique);
  return unique_count;
}

static void test_search_counts_two_plies_paths(void) {
  Game game;
  test_init_game_with_ruleset(&game, PLAYER_RULESET_AMERICAN);

  guint expected = expected_paths_after_two_plies(&game);
  CheckersPositionSearchOptions options = {
      .min_ply = 2,
      .max_ply = 2,
      .deduplicate_positions = FALSE,
  };

  MatchCounter counter = {
      .matches = 0,
      .expected_line_length = 2,
  };
  CheckersPositionSearchStats stats = {0};
  gboolean ok = checkers_position_search(&game,
                                         &options,
                                         predicate_true,
                                         NULL,
                                         count_match,
                                         &counter,
                                         &stats);

  assert(ok);
  assert(counter.matches == expected);
  assert(stats.evaluated_positions == expected);
  assert(stats.matched_positions == expected);

  game_destroy(&game);
}

static void test_search_counts_two_plies_unique_positions(void) {
  Game game;
  test_init_game_with_ruleset(&game, PLAYER_RULESET_AMERICAN);

  guint expected_unique = expected_unique_positions_after_two_plies(&game);
  CheckersPositionSearchOptions options = {
      .min_ply = 2,
      .max_ply = 2,
      .deduplicate_positions = TRUE,
  };

  MatchCounter counter = {
      .matches = 0,
      .expected_line_length = 2,
  };
  CheckersPositionSearchStats stats = {0};
  gboolean ok = checkers_position_search(&game,
                                         &options,
                                         predicate_true,
                                         NULL,
                                         count_match,
                                         &counter,
                                         &stats);

  assert(ok);
  assert(counter.matches == expected_unique);
  assert(stats.evaluated_positions == expected_unique);
  assert(stats.matched_positions == expected_unique);

  game_destroy(&game);
}

static void test_search_counts_one_ply(void) {
  Game game;
  test_init_game_with_ruleset(&game, PLAYER_RULESET_AMERICAN);

  MoveList moves = game.available_moves(&game);
  guint expected = (guint)moves.count;
  movelist_free(&moves);

  CheckersPositionSearchOptions options = {
      .min_ply = 1,
      .max_ply = 1,
      .deduplicate_positions = FALSE,
  };

  MatchCounter counter = {
      .matches = 0,
      .expected_line_length = 1,
  };
  CheckersPositionSearchStats stats = {0};
  gboolean ok = checkers_position_search(&game,
                                         &options,
                                         predicate_true,
                                         NULL,
                                         count_match,
                                         &counter,
                                         &stats);

  assert(ok);
  assert(counter.matches == expected);
  assert(stats.evaluated_positions == expected);
  assert(stats.matched_positions == expected);

  game_destroy(&game);
}

int main(void) {
  test_search_counts_two_plies_paths();
  test_search_counts_two_plies_unique_positions();
  test_search_counts_one_ply();

  return 0;
}
