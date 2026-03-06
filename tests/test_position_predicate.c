#include <assert.h>
#include <string.h>

#include "../src/ai_alpha_beta.h"
#include "../src/position_predicate.h"
#include "../src/position_search.h"
#include "../src/rulesets.h"

static void test_init_game_with_ruleset(Game *game, PlayerRuleset ruleset) {
  assert(game != NULL);

  const CheckersRules *rules = checkers_ruleset_get_rules(ruleset);
  assert(rules != NULL);
  game_init_with_rules(game, rules);
}

static void test_eval_best_score_non_zero_for_material_advantage(void) {
  Game game;
  test_init_game_with_ruleset(&game, PLAYER_RULESET_AMERICAN);

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
  test_init_game_with_ruleset(&game, PLAYER_RULESET_AMERICAN);

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
  test_init_game_with_ruleset(&game, PLAYER_RULESET_AMERICAN);

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
  test_init_game_with_ruleset(&game, PLAYER_RULESET_AMERICAN);

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
  test_init_game_with_ruleset(&game, PLAYER_RULESET_AMERICAN);

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

static void test_mistake_predicate_rejects_best_line(void) {
  Game game;
  test_init_game_with_ruleset(&game, PLAYER_RULESET_AMERICAN);

  CheckersScoredMoveList first_scored = {0};
  gboolean ok = checkers_ai_alpha_beta_analyze_moves(&game, 4, &first_scored);
  assert(ok);
  assert(first_scored.count > 0);
  CheckersMove line[2] = {0};
  line[0] = first_scored.moves[0].move;
  checkers_scored_move_list_free(&first_scored);

  int rc = game_apply_move(&game, &line[0]);
  assert(rc == 0);

  CheckersScoredMoveList second_scored = {0};
  ok = checkers_ai_alpha_beta_analyze_moves(&game, 4, &second_scored);
  assert(ok);
  assert(second_scored.count > 0);
  line[1] = second_scored.moves[0].move;
  checkers_scored_move_list_free(&second_scored);

  Game root;
  test_init_game_with_ruleset(&root, PLAYER_RULESET_AMERICAN);
  CheckersMistakePredicate predicate = {0};
  checkers_mistake_predicate_init(&predicate, &root, 2, 4);
  gboolean matched = checkers_position_predicate_has_mistake(&root, line, 2, &predicate);
  assert(!matched);

  game_destroy(&root);
  game_destroy(&game);
}

typedef struct {
  gboolean found;
  CheckersMove line[2];
} MistakeLineCapture;

static void capture_first_mistake_line(const Game */*position*/,
                                       const CheckersMove *line,
                                       guint line_length,
                                       gpointer user_data) {
  assert(user_data != NULL);
  assert(line != NULL);

  MistakeLineCapture *capture = user_data;
  if (capture->found) {
    return;
  }
  assert(line_length == 2);
  capture->line[0] = line[0];
  capture->line[1] = line[1];
  capture->found = TRUE;
}

static void test_mistake_predicate_detects_mistake_line(void) {
  Game root;
  test_init_game_with_ruleset(&root, PLAYER_RULESET_AMERICAN);

  CheckersMistakePredicate predicate = {0};
  checkers_mistake_predicate_init(&predicate, &root, 2, 4);

  CheckersPositionSearchOptions options = {
      .min_ply = 2,
      .max_ply = 2,
      .deduplicate_positions = TRUE,
  };
  CheckersPositionSearchStats stats = {0};
  MistakeLineCapture capture = {0};
  gboolean ok = checkers_position_search(&root,
                                         &options,
                                         checkers_position_predicate_has_mistake,
                                         &predicate,
                                         capture_first_mistake_line,
                                         &capture,
                                         &stats);
  assert(ok);
  assert(stats.matched_positions > 0);
  assert(capture.found);

  checkers_mistake_predicate_init(&predicate, &root, 2, 4);
  gboolean matched = checkers_position_predicate_has_mistake(&root, capture.line, 2, &predicate);
  assert(matched);

  guint mistake_ply = 0;
  gint best_score = 0;
  gint played_score = 0;
  gboolean has_details = checkers_mistake_predicate_get_last_details(&predicate,
                                                                      &mistake_ply,
                                                                      &best_score,
                                                                      &played_score);
  assert(has_details);
  assert(mistake_ply == 1 || mistake_ply == 2);
  assert(played_score < best_score);

  game_destroy(&root);
}

static void test_deep_mistake_predicate_same_depth_never_matches(void) {
  Game root;
  test_init_game_with_ruleset(&root, PLAYER_RULESET_AMERICAN);

  CheckersDeepMistakePredicate predicate = {0};
  checkers_deep_mistake_predicate_init(&predicate, &root, 2, 4, 4);

  CheckersPositionSearchOptions options = {
      .min_ply = 2,
      .max_ply = 2,
      .deduplicate_positions = TRUE,
  };
  CheckersPositionSearchStats stats = {0};
  guint match_count = 0;
  gboolean ok = checkers_position_search(&root,
                                         &options,
                                         checkers_position_predicate_has_deep_mistake,
                                         &predicate,
                                         count_profile_match,
                                         &match_count,
                                         &stats);
  assert(ok);
  assert(stats.evaluated_positions > 0);
  assert(match_count == 0);
  assert(stats.matched_positions == 0);

  game_destroy(&root);
}

int main(void) {
  test_eval_best_score_non_zero_for_material_advantage();
  test_eval_non_zero_predicate_caches_score();
  test_eval_non_zero_predicate_false_on_balanced_start();
  test_eval_profile_predicate_finds_matches_in_four_plies();
  test_eval_profile_predicate_rejects_imbalanced_position();
  test_mistake_predicate_rejects_best_line();
  test_mistake_predicate_detects_mistake_line();
  test_deep_mistake_predicate_same_depth_never_matches();

  return 0;
}
