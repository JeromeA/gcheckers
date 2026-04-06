#include "../src/puzzle_generation.h"

#include <glib.h>
#include <glib/gstdio.h>

static void test_puzzle_mistake_delta_white_and_black(void) {
  g_assert_cmpint(checkers_puzzle_mistake_delta(CHECKERS_COLOR_WHITE, 300, 200), ==, 100);
  g_assert_cmpint(checkers_puzzle_mistake_delta(CHECKERS_COLOR_BLACK, -300, -200), ==, 100);
  g_assert_cmpint(checkers_puzzle_score_gap_to_next_best(CHECKERS_COLOR_WHITE, 300, 250), ==, 50);
  g_assert_cmpint(checkers_puzzle_score_gap_to_next_best(CHECKERS_COLOR_BLACK, -300, -250), ==, 50);
  g_assert_true(checkers_puzzle_is_mistake(CHECKERS_COLOR_WHITE, 300, 250, 50));
  g_assert_true(checkers_puzzle_is_mistake(CHECKERS_COLOR_BLACK, -300, -250, 50));
  g_assert_false(checkers_puzzle_is_mistake(CHECKERS_COLOR_WHITE, 300, 251, 50));
}

static void test_puzzle_unique_best_rules(void) {
  CheckersScoredMove unique_moves[] = {
      {.score = 200},
      {.score = 100},
      {.score = 0},
      {.score = -100},
  };
  CheckersScoredMoveList unique_list = {
      .moves = unique_moves,
      .count = G_N_ELEMENTS(unique_moves),
  };
  gint best_score = 0;
  gint second_score = 0;
  g_assert_true(checkers_puzzle_side_to_move_has_enough_choice(&unique_list, 4));
  g_assert_true(
      checkers_puzzle_side_to_move_has_a_single_correct_move(&unique_list,
                                                             CHECKERS_COLOR_WHITE,
                                                             50,
                                                             &best_score,
                                                             &second_score));
  g_assert_cmpint(best_score, ==, 200);
  g_assert_cmpint(second_score, ==, 100);
  g_assert_true(
      checkers_puzzle_has_unique_best(&unique_list, 4, CHECKERS_COLOR_WHITE, 50, &best_score, &second_score));
  g_assert_cmpint(best_score, ==, 200);
  g_assert_cmpint(second_score, ==, 100);

  CheckersScoredMove close_moves[] = {
      {.score = 200},
      {.score = 151},
      {.score = 100},
      {.score = 0},
  };
  CheckersScoredMoveList close_list = {
      .moves = close_moves,
      .count = G_N_ELEMENTS(close_moves),
  };
  g_assert_true(checkers_puzzle_side_to_move_has_enough_choice(&close_list, 4));
  g_assert_false(checkers_puzzle_side_to_move_has_a_single_correct_move(&close_list,
                                                                        CHECKERS_COLOR_WHITE,
                                                                        50,
                                                                        &best_score,
                                                                        &second_score));
  g_assert_cmpint(best_score, ==, 200);
  g_assert_cmpint(second_score, ==, 151);
  g_assert_false(
      checkers_puzzle_has_unique_best(&close_list, 4, CHECKERS_COLOR_WHITE, 50, &best_score, &second_score));
  g_assert_cmpint(best_score, ==, 200);
  g_assert_cmpint(second_score, ==, 151);

  CheckersScoredMove boundary_moves[] = {
      {.score = 200},
      {.score = 150},
      {.score = 100},
      {.score = 0},
  };
  CheckersScoredMoveList boundary_list = {
      .moves = boundary_moves,
      .count = G_N_ELEMENTS(boundary_moves),
  };
  g_assert_true(checkers_puzzle_side_to_move_has_enough_choice(&boundary_list, 4));
  g_assert_true(checkers_puzzle_side_to_move_has_a_single_correct_move(&boundary_list,
                                                                       CHECKERS_COLOR_WHITE,
                                                                       50,
                                                                       &best_score,
                                                                       &second_score));
  g_assert_cmpint(best_score, ==, 200);
  g_assert_cmpint(second_score, ==, 150);
  g_assert_true(
      checkers_puzzle_has_unique_best(&boundary_list, 4, CHECKERS_COLOR_WHITE, 50, &best_score, &second_score));
  g_assert_cmpint(best_score, ==, 200);
  g_assert_cmpint(second_score, ==, 150);

  CheckersScoredMove too_few_moves[] = {
      {.score = 200},
      {.score = 100},
      {.score = 0},
  };
  CheckersScoredMoveList too_few_list = {
      .moves = too_few_moves,
      .count = G_N_ELEMENTS(too_few_moves),
  };
  g_assert_false(checkers_puzzle_side_to_move_has_enough_choice(&too_few_list, 4));
  g_assert_true(checkers_puzzle_side_to_move_has_a_single_correct_move(&too_few_list,
                                                                       CHECKERS_COLOR_WHITE,
                                                                       50,
                                                                       &best_score,
                                                                       &second_score));
  g_assert_cmpint(best_score, ==, 200);
  g_assert_cmpint(second_score, ==, 100);
  g_assert_false(
      checkers_puzzle_has_unique_best(&too_few_list, 4, CHECKERS_COLOR_WHITE, 50, &best_score, &second_score));

  CheckersScoredMove black_boundary_moves[] = {
      {.score = -200},
      {.score = -150},
      {.score = -100},
      {.score = 0},
  };
  CheckersScoredMoveList black_boundary_list = {
      .moves = black_boundary_moves,
      .count = G_N_ELEMENTS(black_boundary_moves),
  };
  g_assert_true(checkers_puzzle_side_to_move_has_enough_choice(&black_boundary_list, 4));
  g_assert_true(checkers_puzzle_side_to_move_has_a_single_correct_move(&black_boundary_list,
                                                                       CHECKERS_COLOR_BLACK,
                                                                       50,
                                                                       &best_score,
                                                                       &second_score));
  g_assert_cmpint(best_score, ==, -200);
  g_assert_cmpint(second_score, ==, -150);

  CheckersScoredMove black_close_moves[] = {
      {.score = -200},
      {.score = -151},
      {.score = -100},
      {.score = 0},
  };
  CheckersScoredMoveList black_close_list = {
      .moves = black_close_moves,
      .count = G_N_ELEMENTS(black_close_moves),
  };
  g_assert_false(checkers_puzzle_side_to_move_has_a_single_correct_move(&black_close_list,
                                                                        CHECKERS_COLOR_BLACK,
                                                                        50,
                                                                        &best_score,
                                                                        &second_score));
  g_assert_cmpint(best_score, ==, -200);
  g_assert_cmpint(second_score, ==, -151);
}

static void test_puzzle_attacker_move_clarity_is_asymmetric(void) {
  CheckersScoredMove defender_choice_moves[] = {
      {.score = 200},
      {.score = 151},
      {.score = 100},
      {.score = 0},
  };
  CheckersScoredMoveList defender_choice_list = {
      .moves = defender_choice_moves,
      .count = G_N_ELEMENTS(defender_choice_moves),
  };
  gint best_score = 0;
  gint second_score = 0;

  g_assert_true(checkers_puzzle_turn_keeps_attacker_on_a_single_good_move(&defender_choice_list,
                                                                          CHECKERS_COLOR_WHITE,
                                                                          CHECKERS_COLOR_BLACK,
                                                                          50,
                                                                          &best_score,
                                                                          &second_score));
  g_assert_cmpint(best_score, ==, 200);
  g_assert_cmpint(second_score, ==, 151);

  g_assert_false(checkers_puzzle_turn_keeps_attacker_on_a_single_good_move(&defender_choice_list,
                                                                           CHECKERS_COLOR_WHITE,
                                                                           CHECKERS_COLOR_WHITE,
                                                                           50,
                                                                           &best_score,
                                                                           &second_score));
  g_assert_cmpint(best_score, ==, 200);
  g_assert_cmpint(second_score, ==, 151);

  CheckersScoredMove black_defender_choice_moves[] = {
      {.score = -200},
      {.score = -151},
      {.score = -100},
      {.score = 0},
  };
  CheckersScoredMoveList black_defender_choice_list = {
      .moves = black_defender_choice_moves,
      .count = G_N_ELEMENTS(black_defender_choice_moves),
  };
  g_assert_true(checkers_puzzle_turn_keeps_attacker_on_a_single_good_move(&black_defender_choice_list,
                                                                          CHECKERS_COLOR_BLACK,
                                                                          CHECKERS_COLOR_WHITE,
                                                                          50,
                                                                          &best_score,
                                                                          &second_score));
  g_assert_false(checkers_puzzle_turn_keeps_attacker_on_a_single_good_move(&black_defender_choice_list,
                                                                           CHECKERS_COLOR_BLACK,
                                                                           CHECKERS_COLOR_BLACK,
                                                                           50,
                                                                           &best_score,
                                                                           &second_score));
}

static void test_puzzle_find_next_index(void) {
  guint next = G_MAXUINT;
  g_assert_true(checkers_puzzle_find_next_index("/tmp/does-not-exist-gcheckers-puzzles", &next, NULL));
  g_assert_cmpuint(next, ==, 0);

  g_autoptr(GError) error = NULL;
  g_autofree char *dir_path = g_dir_make_tmp("gcheckers-puzzles-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_nonnull(dir_path);

  g_autofree char *p0 = g_build_filename(dir_path, "puzzle-0000.sgf", NULL);
  g_autofree char *p9 = g_build_filename(dir_path, "puzzle-0009.sgf", NULL);
  g_autofree char *junk = g_build_filename(dir_path, "notes.txt", NULL);
  g_assert_true(g_file_set_contents(p0, "(;)", -1, &error));
  g_assert_no_error(error);
  g_assert_true(g_file_set_contents(p9, "(;)", -1, &error));
  g_assert_no_error(error);
  g_assert_true(g_file_set_contents(junk, "ignore", -1, &error));
  g_assert_no_error(error);

  next = G_MAXUINT;
  g_assert_true(checkers_puzzle_find_next_index(dir_path, &next, &error));
  g_assert_no_error(error);
  g_assert_cmpuint(next, ==, 10);

  g_assert_cmpint(g_remove(p0), ==, 0);
  g_assert_cmpint(g_remove(p9), ==, 0);
  g_assert_cmpint(g_remove(junk), ==, 0);
  g_assert_cmpint(g_rmdir(dir_path), ==, 0);
}

static void test_puzzle_build_indexed_path(void) {
  g_autofree char *puzzle_path = checkers_puzzle_build_indexed_path("puzzles", "puzzle", 7);
  g_assert_cmpstr(puzzle_path, ==, "puzzles/puzzle-0007.sgf");

  g_autofree char *game_path = checkers_puzzle_build_indexed_path("puzzles", "game", 42);
  g_assert_cmpstr(game_path, ==, "puzzles/game-0042.sgf");
}

static void test_puzzle_parse_arg(void) {
  guint count = 0;
  g_assert_cmpuint(checkers_puzzle_parse_arg("12", &count), ==, CHECKERS_PUZZLE_ARG_COUNT);
  g_assert_cmpuint(count, ==, 12);
  g_assert_cmpuint(checkers_puzzle_parse_arg("0", &count), ==, CHECKERS_PUZZLE_ARG_INVALID);
  g_assert_cmpuint(checkers_puzzle_parse_arg("not-a-number", &count), ==, CHECKERS_PUZZLE_ARG_INVALID);

  g_autoptr(GError) error = NULL;
  g_autofree char *dir_path = g_dir_make_tmp("gcheckers-puzzles-arg-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_nonnull(dir_path);

  g_autofree char *file_path = g_build_filename(dir_path, "input.sgf", NULL);
  g_assert_true(g_file_set_contents(file_path, "(;)", -1, &error));
  g_assert_no_error(error);
  g_assert_cmpuint(checkers_puzzle_parse_arg(file_path, &count), ==, CHECKERS_PUZZLE_ARG_FILE);

  g_assert_cmpint(g_remove(file_path), ==, 0);
  g_assert_cmpint(g_rmdir(dir_path), ==, 0);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/puzzle-generation/mistake-delta", test_puzzle_mistake_delta_white_and_black);
  g_test_add_func("/puzzle-generation/unique-best", test_puzzle_unique_best_rules);
  g_test_add_func("/puzzle-generation/attacker-move-clarity",
                  test_puzzle_attacker_move_clarity_is_asymmetric);
  g_test_add_func("/puzzle-generation/find-next-index", test_puzzle_find_next_index);
  g_test_add_func("/puzzle-generation/build-indexed-path", test_puzzle_build_indexed_path);
  g_test_add_func("/puzzle-generation/parse-arg", test_puzzle_parse_arg);
  return g_test_run();
}
