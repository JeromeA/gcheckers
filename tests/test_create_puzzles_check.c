#include "../src/active_game_backend.h"
#include "../src/games/checkers/game.h"
#include "../src/games/checkers/rulesets.h"
#include "../src/sgf_io.h"
#include "../src/sgf_move_props.h"
#include "../src/sgf_tree.h"
#include "test_profile_utils.h"

#include <glib.h>
#include <glib/gstdio.h>

#ifndef GCHECKERS_CREATE_PUZZLES_PATH
#define GCHECKERS_CREATE_PUZZLES_PATH "./checkers_create_puzzles"
#endif

typedef struct {
  CheckersMove move;
  CheckersColor color;
} TestPuzzleLineMove;

static gboolean test_create_puzzles_check_format_setup_point(uint8_t index, uint8_t board_size, char out_point[3]) {
  g_return_val_if_fail(out_point != NULL, FALSE);
  g_return_val_if_fail(board_size > 0, FALSE);

  gint row = 0;
  gint col = 0;
  board_coord_from_index(index, &row, &col, board_size);
  if (row < 0 || col < 0 || row >= 26 || col >= 26) {
    return FALSE;
  }

  out_point[0] = (char)('a' + col);
  out_point[1] = (char)('a' + row);
  out_point[2] = '\0';
  return TRUE;
}

static gboolean test_create_puzzles_check_add_setup_properties(SgfNode *node, const GameState *state) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(state != NULL, FALSE);

  guint8 squares = board_playable_squares(state->board.board_size);
  for (guint8 i = 0; i < squares; ++i) {
    char point[3] = {0};
    if (!test_create_puzzles_check_format_setup_point(i, state->board.board_size, point)) {
      return FALSE;
    }

    CheckersPiece piece = board_get(&state->board, i);
    if (piece == CHECKERS_PIECE_EMPTY) {
      if (!sgf_node_add_property(node, "AE", point)) {
        return FALSE;
      }
      continue;
    }
    if (piece == CHECKERS_PIECE_BLACK_MAN) {
      if (!sgf_node_add_property(node, "AB", point)) {
        return FALSE;
      }
      continue;
    }
    if (piece == CHECKERS_PIECE_WHITE_MAN) {
      if (!sgf_node_add_property(node, "AW", point)) {
        return FALSE;
      }
      continue;
    }
    if (piece == CHECKERS_PIECE_BLACK_KING) {
      if (!sgf_node_add_property(node, "AB", point) || !sgf_node_add_property(node, "ABK", point)) {
        return FALSE;
      }
      continue;
    }
    if (piece == CHECKERS_PIECE_WHITE_KING) {
      if (!sgf_node_add_property(node, "AW", point) || !sgf_node_add_property(node, "AWK", point)) {
        return FALSE;
      }
      continue;
    }
  }

  return sgf_node_add_property(node, "PL", state->turn == CHECKERS_COLOR_BLACK ? "B" : "W");
}

static gboolean test_create_puzzles_check_write_invalid_single_move_puzzle(const char *dir_path) {
  g_return_val_if_fail(dir_path != NULL, FALSE);

  const CheckersRules *rules = checkers_ruleset_get_rules(PLAYER_RULESET_INTERNATIONAL);
  g_assert_nonnull(rules);

  Game game = {0};
  game_init_with_rules(&game, rules);

  GameState state = game.state;
  guint8 squares = board_playable_squares(state.board.board_size);
  for (guint8 i = 0; i < squares; ++i) {
    board_set(&state.board, i, CHECKERS_PIECE_EMPTY);
  }

  gint8 white_square_i = board_index_from_coord(5, 0, state.board.board_size);
  gint8 black_square_i = board_index_from_coord(2, 5, state.board.board_size);
  g_assert_cmpint(white_square_i, >=, 0);
  g_assert_cmpint(black_square_i, >=, 0);

  board_set(&state.board, (guint8)white_square_i, CHECKERS_PIECE_WHITE_KING);
  board_set(&state.board, (guint8)black_square_i, CHECKERS_PIECE_BLACK_KING);
  state.turn = CHECKERS_COLOR_WHITE;
  state.winner = CHECKERS_WINNER_NONE;
  game.state = state;

  MoveList moves = game.available_moves(&game);
  g_assert_cmpuint(moves.count, >, 0);

  CheckersMove chosen = {0};
  gboolean found = FALSE;
  for (guint i = 0; i < moves.count; ++i) {
    if (moves.moves[i].captures == 0 && moves.moves[i].length == 2) {
      chosen = moves.moves[i];
      found = TRUE;
      break;
    }
  }
  g_assert_true(found);
  movelist_free(&moves);

  g_autofree char *puzzle_path = g_build_filename(dir_path, "puzzle-0000.sgf", NULL);
  g_autofree char *game_path = g_build_filename(dir_path, "game-0000.sgf", NULL);

  g_autoptr(SgfTree) puzzle_tree = sgf_tree_new();
  SgfNode *root = (SgfNode *)sgf_tree_get_root(puzzle_tree);
  g_assert_nonnull(root);
  const GameBackendVariant *variant = GGAME_ACTIVE_GAME_BACKEND->variant_by_short_name("international");
  g_assert_nonnull(variant);
  g_assert_true(sgf_io_tree_set_variant(puzzle_tree, variant));
  g_assert_true(test_create_puzzles_check_add_setup_properties(root, &state));
  SgfNode *puzzle_move_node = (SgfNode *)sgf_tree_append_node(puzzle_tree);
  g_assert_nonnull(puzzle_move_node);
  g_assert_true(sgf_move_props_set_move(puzzle_move_node, SGF_COLOR_WHITE, &chosen, NULL));
  g_assert_true(sgf_io_save_file(puzzle_path, puzzle_tree, NULL));

  g_autoptr(SgfTree) game_tree = sgf_tree_new();
  g_assert_true(sgf_io_tree_set_variant(game_tree, variant));
  SgfNode *game_move_node = (SgfNode *)sgf_tree_append_node(game_tree);
  g_assert_nonnull(game_move_node);
  g_assert_true(sgf_move_props_set_move(game_move_node, SGF_COLOR_WHITE, &chosen, NULL));
  g_assert_true(sgf_io_save_file(game_path, game_tree, NULL));

  game_destroy(&game);
  return TRUE;
}

static void test_create_puzzles_check_mode_dry_run_and_delete(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *dir_path = g_dir_make_tmp("gcheckers-check-puzzles-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_nonnull(dir_path);
  g_autofree char *ruleset_dir = g_build_filename(dir_path, "international", NULL);
  g_assert_cmpint(g_mkdir_with_parents(ruleset_dir, 0755), ==, 0);
  g_assert_true(test_create_puzzles_check_write_invalid_single_move_puzzle(ruleset_dir));

  g_autofree char *puzzle_path = g_build_filename(ruleset_dir, "puzzle-0000.sgf", NULL);
  g_autofree char *game_path = g_build_filename(ruleset_dir, "game-0000.sgf", NULL);
  g_assert_true(g_file_test(puzzle_path, G_FILE_TEST_EXISTS));
  g_assert_true(g_file_test(game_path, G_FILE_TEST_EXISTS));

  g_autofree char *cwd = g_get_current_dir();
  gchar *dry_run_argv[] = {
      (gchar *)GCHECKERS_CREATE_PUZZLES_PATH,
      (gchar *)"--ruleset",
      (gchar *)"international",
      (gchar *)"--check-existing",
      (gchar *)"--dry-run",
      ruleset_dir,
      NULL,
  };
  gchar *stdout_text = NULL;
  gchar *stderr_text = NULL;
  gint wait_status = 0;
  g_assert_true(g_spawn_sync(cwd,
                             dry_run_argv,
                             NULL,
                             G_SPAWN_SEARCH_PATH,
                             NULL,
                             NULL,
                             &stdout_text,
                             &stderr_text,
                             &wait_status,
                             &error));
  g_assert_no_error(error);
  g_assert_true(g_spawn_check_wait_status(wait_status, &error));
  g_assert_no_error(error);
  g_assert_nonnull(stdout_text);
  g_assert_nonnull(strstr(stdout_text, "Checking"));
  g_assert_nonnull(strstr(stdout_text, "  -> invalid"));
  g_assert_nonnull(strstr(stdout_text, "would delete"));
  g_assert_true(g_file_test(puzzle_path, G_FILE_TEST_EXISTS));
  g_assert_true(g_file_test(game_path, G_FILE_TEST_EXISTS));
  g_free(stdout_text);
  g_free(stderr_text);

  gchar *delete_argv[] = {
      (gchar *)GCHECKERS_CREATE_PUZZLES_PATH,
      (gchar *)"--ruleset",
      (gchar *)"international",
      (gchar *)"--check-existing",
      ruleset_dir,
      NULL,
  };
  stdout_text = NULL;
  stderr_text = NULL;
  g_assert_true(g_spawn_sync(cwd,
                             delete_argv,
                             NULL,
                             G_SPAWN_SEARCH_PATH,
                             NULL,
                             NULL,
                             &stdout_text,
                             &stderr_text,
                             &wait_status,
                             &error));
  g_assert_no_error(error);
  g_assert_true(g_spawn_check_wait_status(wait_status, &error));
  g_assert_no_error(error);
  g_assert_nonnull(stdout_text);
  g_assert_nonnull(strstr(stdout_text, "Checking"));
  g_assert_nonnull(strstr(stdout_text, "  -> invalid, deleting"));
  g_assert_nonnull(strstr(stdout_text, "deleted"));
  g_assert_false(g_file_test(puzzle_path, G_FILE_TEST_EXISTS));
  g_assert_false(g_file_test(game_path, G_FILE_TEST_EXISTS));
  g_free(stdout_text);
  g_free(stderr_text);

  g_assert_cmpint(g_rmdir(ruleset_dir), ==, 0);
  g_assert_cmpint(g_rmdir(dir_path), ==, 0);
}

static void test_create_puzzles_check_mode_rejects_missing_ru(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *dir_path = g_dir_make_tmp("gcheckers-check-puzzles-missing-ru-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_nonnull(dir_path);
  g_autofree char *ruleset_dir = g_build_filename(dir_path, "international", NULL);
  g_assert_cmpint(g_mkdir_with_parents(ruleset_dir, 0755), ==, 0);

  g_autofree char *puzzle_path = g_build_filename(ruleset_dir, "puzzle-0000.sgf", NULL);
  g_assert_true(g_file_set_contents(puzzle_path,
                                    "(;FF[4]CA[UTF-8]AP[gcheckers]GM[40]AE[1:50]AW[31]AWK[31]AB[8]ABK[8]PL[W];W[31-27])",
                                    -1,
                                    &error));
  g_assert_no_error(error);

  g_autofree char *cwd = g_get_current_dir();
  gchar *argv[] = {
      (gchar *)GCHECKERS_CREATE_PUZZLES_PATH,
      (gchar *)"--ruleset",
      (gchar *)"international",
      (gchar *)"--check-existing",
      (gchar *)"--dry-run",
      ruleset_dir,
      NULL,
  };
  gchar *stdout_text = NULL;
  gchar *stderr_text = NULL;
  gint wait_status = 0;
  g_assert_true(g_spawn_sync(cwd,
                             argv,
                             NULL,
                             G_SPAWN_SEARCH_PATH,
                             NULL,
                             NULL,
                             &stdout_text,
                             &stderr_text,
                             &wait_status,
                             &error));
  g_assert_no_error(error);
  g_assert_true(g_spawn_check_wait_status(wait_status, &error));
  g_assert_no_error(error);
  g_assert_nonnull(stdout_text);
  g_assert_nonnull(strstr(stdout_text, "Checking"));
  g_assert_nonnull(strstr(stdout_text, "failed to load puzzle file"));
  g_assert_nonnull(strstr(stdout_text, "  -> invalid"));
  g_free(stdout_text);
  g_free(stderr_text);

  g_assert_cmpint(g_remove(puzzle_path), ==, 0);
  g_assert_cmpint(g_rmdir(ruleset_dir), ==, 0);
  g_assert_cmpint(g_rmdir(dir_path), ==, 0);
}

int main(int argc, char **argv) {
  ggame_test_init_profile(&argc, &argv, "checkers");
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/create-puzzles-check/dry-run-and-delete", test_create_puzzles_check_mode_dry_run_and_delete);
  g_test_add_func("/create-puzzles-check/rejects-missing-ru", test_create_puzzles_check_mode_rejects_missing_ru);
  return g_test_run();
}
