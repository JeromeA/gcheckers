#include <assert.h>
#include <string.h>

#include "../src/position_predicate.h"
#include "../src/position_search.h"

static void test_eval_best_score_non_zero_for_material_advantage(void) {
  Game game;
  game_init(&game);

  memset(game.state.board.data, 0, sizeof(game.state.board.data));
  game.state.turn = CHECKERS_COLOR_WHITE;
  game.state.winner = CHECKERS_WINNER_NONE;

  board_set(&game.state.board, 20, CHECKERS_PIECE_WHITE_MAN);
  board_set(&game.state.board, 11, CHECKERS_PIECE_BLACK_MAN);
  board_set(&game.state.board, 13, CHECKERS_PIECE_BLACK_MAN);

  gint score = 0;
  gboolean ok = checkers_position_eval_best_score(&game, 4, &score);
  assert(ok);
  assert(score != 0);

  game_destroy(&game);
}

static void test_eval_non_zero_predicate_caches_score(void) {
  Game game;
  game_init(&game);

  memset(game.state.board.data, 0, sizeof(game.state.board.data));
  game.state.turn = CHECKERS_COLOR_WHITE;
  game.state.winner = CHECKERS_WINNER_NONE;

  board_set(&game.state.board, 20, CHECKERS_PIECE_WHITE_MAN);
  board_set(&game.state.board, 11, CHECKERS_PIECE_BLACK_MAN);
  board_set(&game.state.board, 13, CHECKERS_PIECE_BLACK_MAN);

  CheckersEvalNonZeroPredicate predicate = {0};
  checkers_eval_non_zero_predicate_init(&predicate, 4);

  gboolean matched = checkers_position_predicate_eval_non_zero(&game, NULL, 0, &predicate);
  assert(matched);

  gint cached_score = 0;
  gboolean have_score = checkers_eval_non_zero_predicate_get_last_score(&predicate, &cached_score);
  assert(have_score);
  assert(cached_score != 0);

  game_destroy(&game);
}

static void test_eval_non_zero_predicate_false_on_balanced_start(void) {
  Game game;
  game_init(&game);

  CheckersEvalNonZeroPredicate predicate = {0};
  checkers_eval_non_zero_predicate_init(&predicate, 2);

  gboolean matched = checkers_position_predicate_eval_non_zero(&game, NULL, 0, &predicate);
  assert(!matched);

  gint cached_score = 12345;
  gboolean have_score = checkers_eval_non_zero_predicate_get_last_score(&predicate, &cached_score);
  assert(have_score);
  assert(cached_score == 0);

  game_destroy(&game);
}

static void count_profile_match(const Game */*position*/,
                                const CheckersMove */*line*/,
                                guint /*line_length*/,
                                gpointer user_data) {
  assert(user_data != NULL);
  guint *count = user_data;
  (*count)++;
}

static void test_eval_profile_predicate_finds_matches_in_four_plies(void) {
  Game game;
  game_init(&game);

  CheckersEvalProfilePredicate predicate = {0};
  checkers_eval_profile_predicate_init(&predicate, 2, 4, 10);

  CheckersPositionSearchOptions options = {
      .min_ply = 4,
      .max_ply = 4,
      .deduplicate_positions = TRUE,
  };
  CheckersPositionSearchStats stats = {0};
  guint match_count = 0;
  gboolean ok = checkers_position_search(&game,
                                         &options,
                                         checkers_position_predicate_eval_profile,
                                         &predicate,
                                         count_profile_match,
                                         &match_count,
                                         &stats);
  assert(ok);
  assert(stats.evaluated_positions > 0);
  assert(match_count > 0);
  assert(stats.matched_positions == match_count);

  gint last_score = 0;
  gboolean has_score = checkers_eval_profile_predicate_get_last_non_zero_score(&predicate, &last_score);
  assert(has_score);
  assert(last_score != 0);

  game_destroy(&game);
}

static void test_eval_profile_predicate_rejects_imbalanced_position(void) {
  Game game;
  game_init(&game);

  memset(game.state.board.data, 0, sizeof(game.state.board.data));
  game.state.turn = CHECKERS_COLOR_WHITE;
  game.state.winner = CHECKERS_WINNER_NONE;
  board_set(&game.state.board, 20, CHECKERS_PIECE_WHITE_MAN);

  CheckersEvalProfilePredicate predicate = {0};
  checkers_eval_profile_predicate_init(&predicate, 2, 4, 10);
  gboolean matched = checkers_position_predicate_eval_profile(&game, NULL, 0, &predicate);
  assert(!matched);

  game_destroy(&game);
}

int main(void) {
  test_eval_best_score_non_zero_for_material_advantage();
  test_eval_non_zero_predicate_caches_score();
  test_eval_non_zero_predicate_false_on_balanced_start();
  test_eval_profile_predicate_finds_matches_in_four_plies();
  test_eval_profile_predicate_rejects_imbalanced_position();

  return 0;
}
