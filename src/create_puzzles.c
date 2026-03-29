#include "ai_alpha_beta.h"
#include "puzzle_generation.h"
#include "rulesets.h"
#include "sgf_io.h"
#include "sgf_move_props.h"
#include "sgf_tree.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

enum {
  CHECKERS_PUZZLE_SELF_PLAY_DEPTH = 0,
  CHECKERS_PUZZLE_ANALYSIS_DEPTH = 8,
  CHECKERS_PUZZLE_MISTAKE_THRESHOLD = 100,
  CHECKERS_PUZZLE_MIN_LEGAL_MOVES = 4,
  CHECKERS_PUZZLE_MAX_TACTICAL_PLIES = 200,
};

typedef struct {
  CheckersMove move;
  CheckersColor color;
} CheckersPuzzleLineMove;

typedef struct {
  GameState start_state;
  GArray *line;
  gint mistake_delta;
  gint target_score;
  CheckersMove solution_move;
} CheckersPuzzleCandidate;

static void checkers_puzzle_set_winner_for_no_moves(Game *game) {
  g_return_if_fail(game != NULL);

  game->state.winner =
      game->state.turn == CHECKERS_COLOR_WHITE ? CHECKERS_WINNER_BLACK : CHECKERS_WINNER_WHITE;
}

static gboolean checkers_puzzle_moves_equal(const CheckersMove *left, const CheckersMove *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  return memcmp(left, right, sizeof(*left)) == 0;
}

static gboolean checkers_puzzle_score_better_for_side(CheckersColor side, gint candidate, gint baseline) {
  g_return_val_if_fail(side == CHECKERS_COLOR_WHITE || side == CHECKERS_COLOR_BLACK, FALSE);
  return side == CHECKERS_COLOR_WHITE ? candidate > baseline : candidate < baseline;
}

static gboolean checkers_puzzle_score_reaches_target_for_side(CheckersColor side, gint score, gint target) {
  g_return_val_if_fail(side == CHECKERS_COLOR_WHITE || side == CHECKERS_COLOR_BLACK, FALSE);
  return side == CHECKERS_COLOR_WHITE ? score >= target : score <= target;
}

static gboolean checkers_puzzle_find_move_score(const CheckersScoredMoveList *moves,
                                                const CheckersMove *move,
                                                gint *out_score) {
  g_return_val_if_fail(moves != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(out_score != NULL, FALSE);

  for (guint i = 0; i < moves->count; ++i) {
    if (checkers_puzzle_moves_equal(&moves->moves[i].move, move)) {
      *out_score = moves->moves[i].score;
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean checkers_puzzle_format_setup_point(uint8_t index, uint8_t board_size, char out_point[3]) {
  g_return_val_if_fail(out_point != NULL, FALSE);
  g_return_val_if_fail(board_size > 0, FALSE);

  gint row = 0;
  gint col = 0;
  board_coord_from_index(index, &row, &col, board_size);
  if (row < 0 || col < 0 || row >= 26 || col >= 26) {
    g_debug("Unsupported SGF setup coordinate for board size %u", board_size);
    return FALSE;
  }

  out_point[0] = (char)('a' + col);
  out_point[1] = (char)('a' + row);
  out_point[2] = '\0';
  return TRUE;
}

static gboolean checkers_puzzle_add_setup_properties(SgfNode *node, const GameState *state) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(state != NULL, FALSE);

  guint8 squares = board_playable_squares(state->board.board_size);
  for (guint8 i = 0; i < squares; ++i) {
    char point[3] = {0};
    if (!checkers_puzzle_format_setup_point(i, state->board.board_size, point)) {
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

  const char *pl = state->turn == CHECKERS_COLOR_BLACK ? "B" : "W";
  if (!sgf_node_add_property(node, "PL", pl)) {
    return FALSE;
  }
  return TRUE;
}

static gboolean checkers_puzzle_format_move(const CheckersMove *move, char out_text[128]) {
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(out_text != NULL, FALSE);
  return game_format_move_notation(move, out_text, 128);
}

static gboolean checkers_puzzle_evaluate_static(const Game *game, gint *out_score) {
  g_return_val_if_fail(game != NULL, FALSE);
  g_return_val_if_fail(out_score != NULL, FALSE);
  return checkers_ai_evaluate_static_material(game, out_score);
}

static gboolean checkers_puzzle_append_best_depth0_move(Game *game, GArray *line) {
  g_return_val_if_fail(game != NULL, FALSE);
  g_return_val_if_fail(line != NULL, FALSE);

  CheckersScoredMoveList best_moves = {0};
  if (!checkers_ai_alpha_beta_analyze_moves(game, CHECKERS_PUZZLE_SELF_PLAY_DEPTH, &best_moves)) {
    checkers_scored_move_list_free(&best_moves);
    return FALSE;
  }
  g_return_val_if_fail(best_moves.count > 0, FALSE);

  CheckersPuzzleLineMove step = {
      .move = best_moves.moves[0].move,
      .color = game->state.turn,
  };
  g_array_append_val(line, step);

  gboolean ok = game_apply_move(game, &step.move) == 0;
  checkers_scored_move_list_free(&best_moves);
  return ok;
}

static gboolean checkers_puzzle_build_tactical_line(const Game *start,
                                                    const CheckersMove *forced_first,
                                                    gint target_score,
                                                    GArray *out_line) {
  g_return_val_if_fail(start != NULL, FALSE);
  g_return_val_if_fail(forced_first != NULL, FALSE);
  g_return_val_if_fail(out_line != NULL, FALSE);

  Game line_game = *start;
  CheckersColor puzzle_side = start->state.turn;
  gint start_static = 0;
  if (!checkers_puzzle_evaluate_static(start, &start_static)) {
    g_debug("Failed to evaluate static score at puzzle start");
    return FALSE;
  }

  CheckersPuzzleLineMove first = {
      .move = *forced_first,
      .color = line_game.state.turn,
  };
  g_array_append_val(out_line, first);
  if (game_apply_move(&line_game, &first.move) != 0) {
    g_debug("Failed to apply forced first tactical move");
    return FALSE;
  }

  for (guint ply = 1; ply < CHECKERS_PUZZLE_MAX_TACTICAL_PLIES; ++ply) {
    gint eval0 = 0;
    if (!checkers_puzzle_evaluate_static(&line_game, &eval0)) {
      g_debug("Failed static evaluation while building tactical line at ply %u", ply);
      return FALSE;
    }
    if (checkers_puzzle_score_reaches_target_for_side(puzzle_side, eval0, target_score) ||
        checkers_puzzle_score_better_for_side(puzzle_side, eval0, start_static)) {
      return TRUE;
    }
    if (line_game.state.winner != CHECKERS_WINNER_NONE) {
      g_debug("Tactical line ended before target at ply %u (winner=%u)", ply, line_game.state.winner);
      return FALSE;
    }
    if (!checkers_puzzle_append_best_depth0_move(&line_game, out_line)) {
      g_debug("Failed to append best depth-0 move while building tactical line at ply %u", ply);
      return FALSE;
    }
  }

  gint final_eval = 0;
  if (!checkers_puzzle_evaluate_static(&line_game, &final_eval)) {
    g_debug("Failed final static evaluation after reaching tactical line max plies");
    return FALSE;
  }
  return final_eval == target_score;
}

static SgfColor checkers_puzzle_sgf_color(CheckersColor color) {
  g_return_val_if_fail(color == CHECKERS_COLOR_WHITE || color == CHECKERS_COLOR_BLACK, SGF_COLOR_NONE);
  return color == CHECKERS_COLOR_WHITE ? SGF_COLOR_WHITE : SGF_COLOR_BLACK;
}

static gboolean checkers_puzzle_save_sgf(const char *path,
                                         const GameState *start_state,
                                         const GArray *line,
                                         gint mistake_delta,
                                         gint target_score,
                                         const CheckersMove *solution_move) {
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(start_state != NULL, FALSE);
  g_return_val_if_fail(line != NULL, FALSE);
  g_return_val_if_fail(solution_move != NULL, FALSE);

  g_autoptr(SgfTree) tree = sgf_tree_new();
  SgfNode *root = (SgfNode *)sgf_tree_get_root(tree);
  g_return_val_if_fail(root != NULL, FALSE);

  if (!checkers_puzzle_add_setup_properties(root, start_state)) {
    g_debug("Failed to encode puzzle root setup properties");
    return FALSE;
  }

  char solution_text[128] = {0};
  if (!checkers_puzzle_format_move(solution_move, solution_text)) {
    g_debug("Failed to format puzzle solution move");
    return FALSE;
  }

  g_autofree char *comment = g_strdup_printf("mistake_delta_d8=%d target_d8=%d solution=%s line_plies=%u",
                                             mistake_delta,
                                             target_score,
                                             solution_text,
                                             line->len);
  if (!sgf_node_add_property(root, "C", comment)) {
    g_debug("Failed to add puzzle comment");
    return FALSE;
  }

  for (guint i = 0; i < line->len; ++i) {
    const CheckersPuzzleLineMove *entry = &g_array_index(line, CheckersPuzzleLineMove, i);
    SgfNode *node = (SgfNode *)sgf_tree_append_node(tree);
    g_return_val_if_fail(node != NULL, FALSE);

    g_autoptr(GError) move_error = NULL;
    if (!sgf_move_props_set_move(node, checkers_puzzle_sgf_color(entry->color), &entry->move, &move_error)) {
      g_debug("Failed to set SGF tactical move: %s", move_error != NULL ? move_error->message : "unknown error");
      return FALSE;
    }
  }

  g_autoptr(GError) save_error = NULL;
  if (!sgf_io_save_file(path, tree, &save_error)) {
    g_debug("Failed to save puzzle file %s: %s", path, save_error != NULL ? save_error->message : "unknown error");
    return FALSE;
  }
  return TRUE;
}

static gboolean checkers_puzzle_save_game_sgf(const char *path, const GArray *game_line) {
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(game_line != NULL, FALSE);

  g_autoptr(SgfTree) tree = sgf_tree_new();
  for (guint i = 0; i < game_line->len; ++i) {
    const CheckersPuzzleLineMove *entry = &g_array_index(game_line, CheckersPuzzleLineMove, i);
    SgfNode *node = (SgfNode *)sgf_tree_append_node(tree);
    g_return_val_if_fail(node != NULL, FALSE);

    g_autoptr(GError) move_error = NULL;
    if (!sgf_move_props_set_move(node, checkers_puzzle_sgf_color(entry->color), &entry->move, &move_error)) {
      g_debug("Failed to set SGF game move: %s", move_error != NULL ? move_error->message : "unknown error");
      return FALSE;
    }
  }

  g_autoptr(GError) save_error = NULL;
  if (!sgf_io_save_file(path, tree, &save_error)) {
    g_debug("Failed to save game file %s: %s", path, save_error != NULL ? save_error->message : "unknown error");
    return FALSE;
  }
  return TRUE;
}

static void checkers_puzzle_candidate_free(gpointer data) {
  CheckersPuzzleCandidate *candidate = data;
  if (candidate == NULL) {
    return;
  }

  if (candidate->line != NULL) {
    g_array_unref(candidate->line);
    candidate->line = NULL;
  }
  g_free(candidate);
}

static gboolean checkers_puzzle_collect_candidate_from_position(const Game *post_mistake_game,
                                                                gint mistake_delta,
                                                                GPtrArray *out_candidates) {
  g_return_val_if_fail(post_mistake_game != NULL, FALSE);
  g_return_val_if_fail(out_candidates != NULL, FALSE);

  CheckersScoredMoveList start_analysis = {0};
  if (!checkers_ai_alpha_beta_analyze_moves(post_mistake_game, CHECKERS_PUZZLE_ANALYSIS_DEPTH, &start_analysis)) {
    checkers_scored_move_list_free(&start_analysis);
    return FALSE;
  }

  gint target_score = 0;
  guint best_count = 0;
  gboolean has_unique = checkers_puzzle_has_unique_best(&start_analysis,
                                                        CHECKERS_PUZZLE_MIN_LEGAL_MOVES,
                                                        &target_score,
                                                        &best_count);
  if (!has_unique) {
    checkers_scored_move_list_free(&start_analysis);
    return FALSE;
  }

  gint static_score = 0;
  if (!checkers_puzzle_evaluate_static(post_mistake_game, &static_score)) {
    checkers_scored_move_list_free(&start_analysis);
    return FALSE;
  }
  gboolean is_better_for_side_to_move =
      post_mistake_game->state.turn == CHECKERS_COLOR_WHITE ? (target_score > static_score) : (target_score < static_score);
  if (!is_better_for_side_to_move) {
    checkers_scored_move_list_free(&start_analysis);
    return FALSE;
  }

  CheckersMove solution_move = start_analysis.moves[0].move;
  checkers_scored_move_list_free(&start_analysis);

  GArray *line = g_array_new(FALSE, FALSE, sizeof(CheckersPuzzleLineMove));
  if (!checkers_puzzle_build_tactical_line(post_mistake_game, &solution_move, target_score, line)) {
    g_array_unref(line);
    return FALSE;
  }

  CheckersPuzzleCandidate *candidate = g_new0(CheckersPuzzleCandidate, 1);
  candidate->start_state = post_mistake_game->state;
  candidate->line = line;
  candidate->mistake_delta = mistake_delta;
  candidate->target_score = target_score;
  candidate->solution_move = solution_move;
  g_ptr_array_add(out_candidates, candidate);
  return TRUE;
}

static gboolean checkers_puzzle_emit_candidate(const CheckersPuzzleCandidate *candidate,
                                               const GArray *game_line,
                                               const char *output_dir,
                                               guint *inout_index) {
  g_return_val_if_fail(candidate != NULL, FALSE);
  g_return_val_if_fail(game_line != NULL, FALSE);
  g_return_val_if_fail(output_dir != NULL, FALSE);
  g_return_val_if_fail(inout_index != NULL, FALSE);

  g_autofree char *puzzle_path = checkers_puzzle_build_indexed_path(output_dir, "puzzle", *inout_index);
  g_autofree char *game_path = checkers_puzzle_build_indexed_path(output_dir, "game", *inout_index);
  g_return_val_if_fail(puzzle_path != NULL, FALSE);
  g_return_val_if_fail(game_path != NULL, FALSE);

  if (!checkers_puzzle_save_sgf(puzzle_path,
                                &candidate->start_state,
                                candidate->line,
                                candidate->mistake_delta,
                                candidate->target_score,
                                &candidate->solution_move)) {
    return FALSE;
  }
  if (!checkers_puzzle_save_game_sgf(game_path, game_line)) {
    return FALSE;
  }

  g_print("saved %s and %s\n", puzzle_path, game_path);
  *inout_index += 1;
  return TRUE;
}

static gboolean checkers_puzzle_collect_candidates_from_line(const CheckersRules *rules,
                                                             const GArray *game_line,
                                                             GPtrArray *out_candidates) {
  g_return_val_if_fail(rules != NULL, FALSE);
  g_return_val_if_fail(game_line != NULL, FALSE);
  g_return_val_if_fail(out_candidates != NULL, FALSE);

  Game game = {0};
  game_init_with_rules(&game, rules);
  for (guint i = 0; i < game_line->len; ++i) {
    const CheckersPuzzleLineMove *played = &g_array_index(game_line, CheckersPuzzleLineMove, i);
    Game before = game;

    if (played->color != before.state.turn) {
      game_destroy(&game);
      return FALSE;
    }

    CheckersScoredMoveList deep = {0};
    if (!checkers_ai_alpha_beta_analyze_moves(&before, CHECKERS_PUZZLE_ANALYSIS_DEPTH, &deep)) {
      checkers_scored_move_list_free(&deep);
      game_destroy(&game);
      return FALSE;
    }

    gint played_score = 0;
    if (!checkers_puzzle_find_move_score(&deep, &played->move, &played_score)) {
      checkers_scored_move_list_free(&deep);
      game_destroy(&game);
      return FALSE;
    }

    gint best_score = deep.moves[0].score;
    gboolean is_mistake =
        checkers_puzzle_is_mistake(before.state.turn, best_score, played_score, CHECKERS_PUZZLE_MISTAKE_THRESHOLD);
    gint mistake_delta = checkers_puzzle_mistake_delta(before.state.turn, best_score, played_score);
    checkers_scored_move_list_free(&deep);

    if (game_apply_move(&game, &played->move) != 0) {
      game_destroy(&game);
      return FALSE;
    }

    if (is_mistake) {
      (void)checkers_puzzle_collect_candidate_from_position(&game, mistake_delta, out_candidates);
    }
  }

  game_destroy(&game);
  return TRUE;
}

static gboolean checkers_puzzle_generate_self_play_line(const CheckersRules *rules, GArray *out_line) {
  g_return_val_if_fail(rules != NULL, FALSE);
  g_return_val_if_fail(out_line != NULL, FALSE);

  Game game = {0};
  game_init_with_rules(&game, rules);
  while (game.state.winner == CHECKERS_WINNER_NONE) {
    CheckersMove played = {0};
    if (!checkers_ai_alpha_beta_choose_move(&game, CHECKERS_PUZZLE_SELF_PLAY_DEPTH, &played)) {
      checkers_puzzle_set_winner_for_no_moves(&game);
      break;
    }

    CheckersPuzzleLineMove step = {
        .move = played,
        .color = game.state.turn,
    };
    g_array_append_val(out_line, step);

    if (game_apply_move(&game, &played) != 0) {
      game_destroy(&game);
      return FALSE;
    }
  }

  game_destroy(&game);
  return TRUE;
}

static gboolean checkers_puzzle_line_replays_with_rules(const GArray *game_line, const CheckersRules *rules) {
  g_return_val_if_fail(game_line != NULL, FALSE);
  g_return_val_if_fail(rules != NULL, FALSE);

  Game game = {0};
  game_init_with_rules(&game, rules);
  for (guint i = 0; i < game_line->len; ++i) {
    const CheckersPuzzleLineMove *step = &g_array_index(game_line, CheckersPuzzleLineMove, i);
    if (step->color != game.state.turn || game_apply_move(&game, &step->move) != 0) {
      game_destroy(&game);
      return FALSE;
    }
  }
  game_destroy(&game);
  return TRUE;
}

static const CheckersRules *checkers_puzzle_find_matching_rules(const GArray *game_line) {
  g_return_val_if_fail(game_line != NULL, NULL);

  guint count = checkers_ruleset_count();
  for (guint i = 0; i < count; ++i) {
    const CheckersRules *rules = checkers_ruleset_get_rules((PlayerRuleset)i);
    if (rules != NULL && checkers_puzzle_line_replays_with_rules(game_line, rules)) {
      return rules;
    }
  }
  return NULL;
}

static gboolean checkers_puzzle_load_main_line(const char *path, GArray *out_line) {
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(out_line != NULL, FALSE);

  g_autoptr(SgfTree) tree = NULL;
  g_autoptr(GError) load_error = NULL;
  if (!sgf_io_load_file(path, &tree, &load_error)) {
    g_debug("Failed to load SGF file %s: %s", path, load_error != NULL ? load_error->message : "unknown error");
    return FALSE;
  }

  g_autoptr(GPtrArray) line = sgf_tree_build_main_line(tree);
  if (line == NULL || line->len == 0) {
    return FALSE;
  }

  for (guint i = 1; i < line->len; ++i) {
    const SgfNode *node = g_ptr_array_index(line, i);
    g_return_val_if_fail(node != NULL, FALSE);

    SgfColor color = SGF_COLOR_NONE;
    CheckersMove move = {0};
    gboolean has_move = FALSE;
    g_autoptr(GError) move_error = NULL;
    if (!sgf_move_props_try_parse_node(node, &color, &move, &has_move, &move_error)) {
      g_debug("Failed to parse SGF node move: %s", move_error != NULL ? move_error->message : "unknown error");
      return FALSE;
    }
    if (!has_move) {
      continue;
    }

    CheckersPuzzleLineMove step = {
        .move = move,
        .color = color == SGF_COLOR_WHITE ? CHECKERS_COLOR_WHITE : CHECKERS_COLOR_BLACK,
    };
    g_array_append_val(out_line, step);
  }

  return out_line->len > 0;
}

static gboolean checkers_puzzle_emit_from_line(const CheckersRules *rules,
                                               const GArray *game_line,
                                               const char *output_dir,
                                               guint *inout_index,
                                               guint limit,
                                               guint *out_emitted) {
  g_return_val_if_fail(rules != NULL, FALSE);
  g_return_val_if_fail(game_line != NULL, FALSE);
  g_return_val_if_fail(output_dir != NULL, FALSE);
  g_return_val_if_fail(inout_index != NULL, FALSE);
  g_return_val_if_fail(out_emitted != NULL, FALSE);

  *out_emitted = 0;
  g_autoptr(GPtrArray) candidates = g_ptr_array_new_with_free_func(checkers_puzzle_candidate_free);
  if (!checkers_puzzle_collect_candidates_from_line(rules, game_line, candidates)) {
    return FALSE;
  }

  for (guint i = 0; i < candidates->len && *out_emitted < limit; ++i) {
    const CheckersPuzzleCandidate *candidate = g_ptr_array_index(candidates, i);
    g_return_val_if_fail(candidate != NULL, FALSE);
    if (checkers_puzzle_emit_candidate(candidate, game_line, output_dir, inout_index)) {
      *out_emitted += 1;
    }
  }
  return TRUE;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    g_printerr("Usage: %s <puzzle-count|sgf-file>\n", argv[0]);
    return 1;
  }

  const char *arg = argv[1];
  guint wanted = 0;
  CheckersPuzzleArgType arg_type = checkers_puzzle_parse_arg(arg, &wanted);
  if (arg_type == CHECKERS_PUZZLE_ARG_INVALID) {
    g_printerr("Invalid argument: %s\n", arg);
    return 1;
  }

  const char *output_dir = "puzzles";
  if (g_mkdir_with_parents(output_dir, 0755) != 0) {
    g_printerr("Failed to create output dir %s: %s\n", output_dir, g_strerror(errno));
    return 1;
  }

  guint next_index = 0;
  g_autoptr(GError) index_error = NULL;
  if (!checkers_puzzle_find_next_index(output_dir, &next_index, &index_error)) {
    g_printerr("Failed to compute next puzzle index: %s\n",
               index_error != NULL ? index_error->message : "unknown error");
    return 1;
  }

  if (arg_type == CHECKERS_PUZZLE_ARG_FILE) {
    g_autoptr(GArray) game_line = g_array_new(FALSE, FALSE, sizeof(CheckersPuzzleLineMove));
    if (!checkers_puzzle_load_main_line(arg, game_line)) {
      g_printerr("Failed to load game line from file: %s\n", arg);
      return 1;
    }

    const CheckersRules *rules = checkers_puzzle_find_matching_rules(game_line);
    if (rules == NULL) {
      g_printerr("Could not match SGF game line to known rulesets\n");
      return 1;
    }

    guint emitted = 0;
    if (!checkers_puzzle_emit_from_line(rules, game_line, output_dir, &next_index, G_MAXUINT, &emitted)) {
      g_printerr("Failed to extract puzzles from file\n");
      return 1;
    }
    g_print("file=%s generated=%u\n", arg, emitted);
    return 0;
  }

  const CheckersRules *rules = checkers_ruleset_get_rules(PLAYER_RULESET_INTERNATIONAL);
  if (rules == NULL) {
    g_printerr("Missing ruleset\n");
    return 1;
  }

  guint generated = 0;
  guint games = 0;
  while (generated < wanted) {
    g_autoptr(GArray) game_line = g_array_new(FALSE, FALSE, sizeof(CheckersPuzzleLineMove));
    if (!checkers_puzzle_generate_self_play_line(rules, game_line)) {
      g_printerr("Self-play generation failed\n");
      return 1;
    }

    guint emitted = 0;
    if (!checkers_puzzle_emit_from_line(rules, game_line, output_dir, &next_index, wanted - generated, &emitted)) {
      g_printerr("Puzzle extraction failed\n");
      return 1;
    }

    generated += emitted;
    games++;
    g_print("games=%u generated=%u/%u\n", games, generated, wanted);
  }

  return 0;
}
