#include "ai_alpha_beta.h"
#include "puzzle_generation.h"
#include "rulesets.h"
#include "sgf_io.h"
#include "sgf_move_props.h"
#include "sgf_tree.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

enum {
  CHECKERS_PUZZLE_SELF_PLAY_DEPTH = 0,
  CHECKERS_PUZZLE_DEFAULT_BEST_MOVE_DEPTH = 8,
  CHECKERS_PUZZLE_MISTAKE_THRESHOLD = 50,
  CHECKERS_PUZZLE_SINGLE_MOVE_MARGIN = 50,
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
  gint start_static;
  gint final_static;
  guint solution_depth;
  CheckersMove solution_move;
} CheckersPuzzleCandidate;

typedef struct {
  CheckersScoredMoveList moves;
  gint best_score;
  gint second_score;
  gint static_score;
  CheckersMove best_move;
} CheckersPuzzlePositionAnalysis;

static gboolean checkers_puzzle_analyze_resulting_position(const Game *game,
                                                           guint best_move_depth,
                                                           CheckersPuzzlePositionAnalysis *out_analysis);

static void checkers_puzzle_debug_rejection(const char *format, ...) G_GNUC_PRINTF(1, 2);

static void checkers_puzzle_debug_rejection(const char *format, ...) {
  g_return_if_fail(format != NULL);

  va_list args;
  va_start(args, format);
  g_autofree char *message = g_strdup_vprintf(format, args);
  va_end(args);

  g_debug("  -> %s", message);
}

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

static void checkers_puzzle_position_analysis_clear(CheckersPuzzlePositionAnalysis *analysis) {
  g_return_if_fail(analysis != NULL);

  checkers_scored_move_list_free(&analysis->moves);
  memset(analysis, 0, sizeof(*analysis));
}

static gboolean checkers_puzzle_append_move_to_line(Game *game, const CheckersMove *move, GArray *line) {
  g_return_val_if_fail(game != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(line != NULL, FALSE);

  CheckersPuzzleLineMove step = {
      .move = *move,
      .color = game->state.turn,
  };
  g_array_append_val(line, step);

  return game_apply_move(game, &step.move) == 0;
}

static gboolean checkers_puzzle_append_best_depth_move(Game *game,
                                                       const CheckersPuzzlePositionAnalysis *analysis,
                                                       GArray *line) {
  g_return_val_if_fail(game != NULL, FALSE);
  g_return_val_if_fail(analysis != NULL, FALSE);
  g_return_val_if_fail(line != NULL, FALSE);

  return checkers_puzzle_append_move_to_line(game, &analysis->best_move, line);
}

static gboolean checkers_puzzle_solution_is_a_single_move(const GArray *line) {
  g_return_val_if_fail(line != NULL, FALSE);

  return line->len == 1;
}

static gboolean checkers_puzzle_solution_is_move_move_jump(const GArray *line) {
  g_return_val_if_fail(line != NULL, FALSE);

  if (line->len != 3) {
    return FALSE;
  }

  const CheckersPuzzleLineMove *first = &g_array_index(line, CheckersPuzzleLineMove, 0);
  const CheckersPuzzleLineMove *second = &g_array_index(line, CheckersPuzzleLineMove, 1);
  const CheckersPuzzleLineMove *third = &g_array_index(line, CheckersPuzzleLineMove, 2);
  return first->move.captures == 0 && second->move.captures == 0 && third->move.captures > 0;
}

static gboolean checkers_puzzle_solution_is_interesting(const GArray *line) {
  g_return_val_if_fail(line != NULL, FALSE);

  if (line->len == 0) {
    return FALSE;
  }

  g_autofree CheckersMove *moves = g_new(CheckersMove, line->len);
  for (guint i = 0; i < line->len; ++i) {
    const CheckersPuzzleLineMove *entry = &g_array_index(line, CheckersPuzzleLineMove, i);
    moves[i] = entry->move;
  }
  return checkers_puzzle_solution_shape_is_interesting(moves, line->len);
}

static gboolean checkers_puzzle_solution_ends_before_an_immediate_recapture(const Game *after_solution,
                                                                            const GArray *line,
                                                                            guint best_move_depth) {
  g_return_val_if_fail(after_solution != NULL, FALSE);
  g_return_val_if_fail(line != NULL, FALSE);
  g_return_val_if_fail(best_move_depth > 0, FALSE);

  if (after_solution->state.winner != CHECKERS_WINNER_NONE) {
    return TRUE;
  }

  CheckersPuzzlePositionAnalysis analysis = {0};
  if (!checkers_puzzle_analyze_resulting_position(after_solution, best_move_depth, &analysis)) {
    checkers_puzzle_debug_rejection("failed to analyze the move immediately after the solution");
    return FALSE;
  }

  g_autofree CheckersMove *moves = g_new(CheckersMove, line->len);
  for (guint i = 0; i < line->len; ++i) {
    const CheckersPuzzleLineMove *entry = &g_array_index(line, CheckersPuzzleLineMove, i);
    moves[i] = entry->move;
  }
  gboolean stable = checkers_puzzle_solution_has_no_immediate_recapture(moves, line->len, &analysis.best_move);
  if (!stable) {
    checkers_puzzle_debug_rejection("move immediately after the solution is a recapture");
  }
  checkers_puzzle_position_analysis_clear(&analysis);
  return stable;
}

static gboolean checkers_puzzle_solution_line_of_best_depth_moves_improves_static_evaluation(
    const Game *start,
    guint best_move_depth,
    GArray *out_line,
    gint *out_final_static,
    Game *out_final_game) {
  g_return_val_if_fail(start != NULL, FALSE);
  g_return_val_if_fail(best_move_depth > 0, FALSE);
  g_return_val_if_fail(out_line != NULL, FALSE);
  g_return_val_if_fail(out_final_static != NULL, FALSE);
  g_return_val_if_fail(out_final_game != NULL, FALSE);

  Game line_game = *start;
  CheckersColor attacker = start->state.turn;
  gint start_static = 0;
  if (!checkers_puzzle_evaluate_static(start, &start_static)) {
    checkers_puzzle_debug_rejection("failed to evaluate static score at puzzle start");
    return FALSE;
  }

  for (guint ply = 0; ply < CHECKERS_PUZZLE_MAX_TACTICAL_PLIES; ++ply) {
    gint current_static = 0;
    if (!checkers_puzzle_evaluate_static(&line_game, &current_static)) {
      checkers_puzzle_debug_rejection("failed static evaluation while building the line at ply %u", ply);
      return FALSE;
    }

    if (checkers_puzzle_score_better_for_side(attacker, current_static, start_static)) {
      *out_final_static = current_static;
      *out_final_game = line_game;
      return TRUE;
    }

    if (line_game.state.winner != CHECKERS_WINNER_NONE) {
      checkers_puzzle_debug_rejection("solution line ended before static improvement at ply %u (winner=%u)",
                                      ply,
                                      line_game.state.winner);
      return FALSE;
    }

    CheckersPuzzlePositionAnalysis analysis = {0};
    if (!checkers_puzzle_analyze_resulting_position(&line_game, best_move_depth, &analysis)) {
      checkers_puzzle_debug_rejection("failed to analyze best depth-%u moves while building the line at ply %u",
                                      best_move_depth,
                                      ply);
      return FALSE;
    }

    gboolean attacker_has_a_single_good_move =
        checkers_puzzle_turn_keeps_attacker_on_a_single_good_move(&analysis.moves,
                                                                  attacker,
                                                                  line_game.state.turn,
                                                                  CHECKERS_PUZZLE_SINGLE_MOVE_MARGIN,
                                                                  &analysis.best_score,
                                                                  &analysis.second_score);
    if (!attacker_has_a_single_good_move) {
      checkers_puzzle_debug_rejection("attacker lost move clarity at ply %u (%d vs %d)",
                                      ply,
                                      analysis.best_score,
                                      analysis.second_score);
      checkers_puzzle_position_analysis_clear(&analysis);
      return FALSE;
    }

    if (!checkers_puzzle_append_best_depth_move(&line_game, &analysis, out_line)) {
      checkers_puzzle_debug_rejection("failed to append best depth-%u move while building the line at ply %u",
                                      best_move_depth,
                                      ply);
      checkers_puzzle_position_analysis_clear(&analysis);
      return FALSE;
    }
    checkers_puzzle_position_analysis_clear(&analysis);
  }

  gint final_eval = 0;
  if (!checkers_puzzle_evaluate_static(&line_game, &final_eval)) {
    checkers_puzzle_debug_rejection("failed final static evaluation after reaching the line max plies");
    return FALSE;
  }
  *out_final_static = final_eval;
  *out_final_game = line_game;
  checkers_puzzle_debug_rejection("line reached max plies without static improvement (%d vs %d)",
                                  final_eval,
                                  start_static);
  return checkers_puzzle_score_better_for_side(attacker, final_eval, start_static);
}

static gboolean checkers_puzzle_position_follows_a_serious_mistake(const Game *before_mistake_game,
                                                                   guint best_move_depth,
                                                                   const CheckersMove *played_move,
                                                                   gint *out_mistake_delta) {
  g_return_val_if_fail(before_mistake_game != NULL, FALSE);
  g_return_val_if_fail(best_move_depth > 0, FALSE);
  g_return_val_if_fail(played_move != NULL, FALSE);
  g_return_val_if_fail(out_mistake_delta != NULL, FALSE);

  CheckersScoredMoveList analysis = {0};
  if (!checkers_ai_alpha_beta_analyze_moves(before_mistake_game, best_move_depth, &analysis)) {
    checkers_scored_move_list_free(&analysis);
    return FALSE;
  }

  gint played_score = 0;
  if (!checkers_puzzle_find_move_score(&analysis, played_move, &played_score)) {
    checkers_scored_move_list_free(&analysis);
    return FALSE;
  }

  gint best_score = analysis.moves[0].score;
  gint mistake_delta =
      checkers_puzzle_mistake_delta(before_mistake_game->state.turn, best_score, played_score);
  gboolean is_mistake = checkers_puzzle_is_mistake(before_mistake_game->state.turn,
                                                   best_score,
                                                   played_score,
                                                   CHECKERS_PUZZLE_MISTAKE_THRESHOLD);
  checkers_scored_move_list_free(&analysis);

  *out_mistake_delta = mistake_delta;
  return is_mistake;
}

static gboolean checkers_puzzle_analyze_resulting_position(const Game *game,
                                                           guint best_move_depth,
                                                           CheckersPuzzlePositionAnalysis *out_analysis) {
  g_return_val_if_fail(game != NULL, FALSE);
  g_return_val_if_fail(best_move_depth > 0, FALSE);
  g_return_val_if_fail(out_analysis != NULL, FALSE);

  *out_analysis = (CheckersPuzzlePositionAnalysis) {0};
  if (!checkers_ai_alpha_beta_analyze_moves(game, best_move_depth, &out_analysis->moves)) {
    checkers_scored_move_list_free(&out_analysis->moves);
    return FALSE;
  }
  if (!checkers_puzzle_evaluate_static(game, &out_analysis->static_score)) {
    checkers_puzzle_position_analysis_clear(out_analysis);
    return FALSE;
  }
  if (out_analysis->moves.count == 0) {
    checkers_puzzle_debug_rejection("resulting position has no legal moves");
    checkers_puzzle_position_analysis_clear(out_analysis);
    return FALSE;
  }

  out_analysis->best_move = out_analysis->moves.moves[0].move;
  out_analysis->best_score = out_analysis->moves.moves[0].score;
  (void)checkers_puzzle_side_to_move_has_a_single_correct_move(&out_analysis->moves,
                                                               game->state.turn,
                                                               CHECKERS_PUZZLE_SINGLE_MOVE_MARGIN,
                                                               &out_analysis->best_score,
                                                               &out_analysis->second_score);
  return TRUE;
}

static gboolean checkers_puzzle_attacker_has_enough_choice(const CheckersPuzzlePositionAnalysis *analysis) {
  g_return_val_if_fail(analysis != NULL, FALSE);

  return checkers_puzzle_side_to_move_has_enough_choice(&analysis->moves, CHECKERS_PUZZLE_MIN_LEGAL_MOVES);
}

static gboolean checkers_puzzle_attacker_has_a_single_good_move(const CheckersPuzzlePositionAnalysis *analysis,
                                                                CheckersColor attacker) {
  g_return_val_if_fail(analysis != NULL, FALSE);
  g_return_val_if_fail(attacker == CHECKERS_COLOR_WHITE || attacker == CHECKERS_COLOR_BLACK, FALSE);

  return checkers_puzzle_side_to_move_has_a_single_correct_move(&analysis->moves,
                                                                attacker,
                                                                CHECKERS_PUZZLE_SINGLE_MOVE_MARGIN,
                                                                NULL,
                                                                NULL);
}

static gboolean checkers_puzzle_position_is_valid(const CheckersPuzzlePositionAnalysis *analysis,
                                                  CheckersColor attacker) {
  g_return_val_if_fail(analysis != NULL, FALSE);
  g_return_val_if_fail(attacker == CHECKERS_COLOR_WHITE || attacker == CHECKERS_COLOR_BLACK, FALSE);

  if (!checkers_puzzle_attacker_has_enough_choice(analysis)) {
    return FALSE;
  }
  if (!checkers_puzzle_attacker_has_a_single_good_move(analysis, attacker)) {
    return FALSE;
  }
  return TRUE;
}

static SgfColor checkers_puzzle_sgf_color(CheckersColor color) {
  g_return_val_if_fail(color == CHECKERS_COLOR_WHITE || color == CHECKERS_COLOR_BLACK, SGF_COLOR_NONE);
  return color == CHECKERS_COLOR_WHITE ? SGF_COLOR_WHITE : SGF_COLOR_BLACK;
}

static gboolean checkers_puzzle_save_sgf(const char *path,
                                         const GameState *start_state,
                                         const GArray *line,
                                         gint mistake_delta,
                                         gint start_static,
                                         gint final_static,
                                         guint solution_depth,
                                         const CheckersMove *solution_move) {
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(start_state != NULL, FALSE);
  g_return_val_if_fail(line != NULL, FALSE);
  g_return_val_if_fail(solution_depth > 0, FALSE);
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

  g_autofree char *comment = g_strdup_printf("mistake_delta=%d solution_depth=%u start_static=%d final_static=%d "
                                             "solution=%s line_plies=%u",
                                             mistake_delta,
                                             solution_depth,
                                             start_static,
                                             final_static,
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

static gboolean checkers_puzzle_try_build_candidate_from_resulting_position(const Game *post_mistake_game,
                                                                            guint best_move_depth,
                                                                            gint mistake_delta,
                                                                            GPtrArray *out_candidates) {
  g_return_val_if_fail(post_mistake_game != NULL, FALSE);
  g_return_val_if_fail(best_move_depth > 0, FALSE);
  g_return_val_if_fail(out_candidates != NULL, FALSE);

  CheckersPuzzlePositionAnalysis analysis = {0};
  if (!checkers_puzzle_analyze_resulting_position(post_mistake_game, best_move_depth, &analysis)) {
    checkers_puzzle_debug_rejection("failed to analyze the resulting position");
    return FALSE;
  }
  if (!checkers_puzzle_attacker_has_enough_choice(&analysis)) {
    checkers_puzzle_debug_rejection("attacker has only %zu legal moves", analysis.moves.count);
    checkers_puzzle_position_analysis_clear(&analysis);
    return FALSE;
  }
  if (!checkers_puzzle_attacker_has_a_single_good_move(&analysis, post_mistake_game->state.turn)) {
    checkers_puzzle_debug_rejection("attacker best move is only %d points ahead of second-best (%d vs %d)",
                                    checkers_puzzle_score_gap_to_next_best(post_mistake_game->state.turn,
                                                                           analysis.best_score,
                                                                           analysis.second_score),
                                    analysis.best_score,
                                    analysis.second_score);
    checkers_puzzle_position_analysis_clear(&analysis);
    return FALSE;
  }
  if (!checkers_puzzle_position_is_valid(&analysis, post_mistake_game->state.turn)) {
    checkers_puzzle_debug_rejection("resulting position is not a valid puzzle");
    checkers_puzzle_position_analysis_clear(&analysis);
    return FALSE;
  }

  GArray *line = g_array_new(FALSE, FALSE, sizeof(CheckersPuzzleLineMove));
  gint final_static = analysis.static_score;
  Game final_game = {0};
  if (!checkers_puzzle_solution_line_of_best_depth_moves_improves_static_evaluation(post_mistake_game,
                                                                                    best_move_depth,
                                                                                    line,
                                                                                    &final_static,
                                                                                    &final_game)) {
    checkers_puzzle_position_analysis_clear(&analysis);
    g_array_unref(line);
    return FALSE;
  }
  if (checkers_puzzle_solution_is_a_single_move(line)) {
    checkers_puzzle_debug_rejection("solution is only one move");
    checkers_puzzle_position_analysis_clear(&analysis);
    g_array_unref(line);
    return FALSE;
  }
  if (checkers_puzzle_solution_is_move_move_jump(line)) {
    checkers_puzzle_debug_rejection("solution is move, move, jump");
    checkers_puzzle_position_analysis_clear(&analysis);
    g_array_unref(line);
    return FALSE;
  }
  if (!checkers_puzzle_solution_is_interesting(line)) {
    checkers_puzzle_debug_rejection("solution shape is not interesting");
    checkers_puzzle_position_analysis_clear(&analysis);
    g_array_unref(line);
    return FALSE;
  }
  if (!checkers_puzzle_solution_ends_before_an_immediate_recapture(&final_game, line, best_move_depth)) {
    checkers_puzzle_position_analysis_clear(&analysis);
    g_array_unref(line);
    return FALSE;
  }

  CheckersPuzzleCandidate *candidate = g_new0(CheckersPuzzleCandidate, 1);
  candidate->start_state = post_mistake_game->state;
  candidate->line = line;
  candidate->mistake_delta = mistake_delta;
  candidate->start_static = analysis.static_score;
  candidate->final_static = final_static;
  candidate->solution_depth = best_move_depth;
  candidate->solution_move = analysis.best_move;
  g_ptr_array_add(out_candidates, candidate);
  g_debug("  -> kept: mistake_delta=%d start_static=%d final_static=%d line_plies=%u",
          mistake_delta,
          analysis.static_score,
          final_static,
          line->len);
  checkers_puzzle_position_analysis_clear(&analysis);
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
                                candidate->start_static,
                                candidate->final_static,
                                candidate->solution_depth,
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
                                                             guint best_move_depth,
                                                             const GArray *game_line,
                                                             GPtrArray *out_candidates) {
  g_return_val_if_fail(rules != NULL, FALSE);
  g_return_val_if_fail(best_move_depth > 0, FALSE);
  g_return_val_if_fail(game_line != NULL, FALSE);
  g_return_val_if_fail(out_candidates != NULL, FALSE);

  Game game = {0};
  game_init_with_rules(&game, rules);
  for (guint i = 0; i < game_line->len; ++i) {
    const CheckersPuzzleLineMove *played = &g_array_index(game_line, CheckersPuzzleLineMove, i);
    Game before = game;
    char move_text[128] = {0};
    gboolean has_move_text = checkers_puzzle_format_move(&played->move, move_text);

    g_debug("Considering move #%u%s%s",
            i + 1,
            has_move_text ? " " : "",
            has_move_text ? move_text : "");

    if (played->color != before.state.turn) {
      checkers_puzzle_debug_rejection("move color does not match side to move");
      game_destroy(&game);
      return FALSE;
    }

    gint mistake_delta = 0;
    gboolean is_mistake = checkers_puzzle_position_follows_a_serious_mistake(&before,
                                                                             best_move_depth,
                                                                             &played->move,
                                                                             &mistake_delta);

    if (game_apply_move(&game, &played->move) != 0) {
      checkers_puzzle_debug_rejection("move could not be applied to the game");
      game_destroy(&game);
      return FALSE;
    }

    if (is_mistake) {
      (void)checkers_puzzle_try_build_candidate_from_resulting_position(&game,
                                                                        best_move_depth,
                                                                        mistake_delta,
                                                                        out_candidates);
    } else {
      checkers_puzzle_debug_rejection("not a serious mistake");
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

  g_debug("Played game ended after %u moves (winner=%u)", out_line->len, game.state.winner);
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
                                               guint best_move_depth,
                                               const GArray *game_line,
                                               const char *output_dir,
                                               guint *inout_index,
                                               guint limit,
                                               guint *out_emitted) {
  g_return_val_if_fail(rules != NULL, FALSE);
  g_return_val_if_fail(best_move_depth > 0, FALSE);
  g_return_val_if_fail(game_line != NULL, FALSE);
  g_return_val_if_fail(output_dir != NULL, FALSE);
  g_return_val_if_fail(inout_index != NULL, FALSE);
  g_return_val_if_fail(out_emitted != NULL, FALSE);

  *out_emitted = 0;
  g_autoptr(GPtrArray) candidates = g_ptr_array_new_with_free_func(checkers_puzzle_candidate_free);
  if (!checkers_puzzle_collect_candidates_from_line(rules, best_move_depth, game_line, candidates)) {
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

static gboolean checkers_puzzle_parse_depth_option(const char *arg, guint *out_depth) {
  g_return_val_if_fail(arg != NULL, FALSE);
  g_return_val_if_fail(out_depth != NULL, FALSE);

  gchar *end = NULL;
  guint64 value = g_ascii_strtoull(arg, &end, 10);
  if (end == arg || end == NULL || *end != '\0' || value == 0 || value > G_MAXUINT) {
    return FALSE;
  }

  *out_depth = (guint)value;
  return TRUE;
}

int main(int argc, char **argv) {
  guint best_move_depth = CHECKERS_PUZZLE_DEFAULT_BEST_MOVE_DEPTH;
  const char *arg = NULL;
  for (gint i = 1; i < argc; ++i) {
    if (g_strcmp0(argv[i], "--depth") == 0) {
      if (i + 1 >= argc || !checkers_puzzle_parse_depth_option(argv[i + 1], &best_move_depth)) {
        g_printerr("Invalid --depth value\n");
        return 1;
      }
      i++;
      continue;
    }
    if (arg != NULL) {
      g_printerr("Usage: %s [--depth N] <puzzle-count|sgf-file>\n", argv[0]);
      return 1;
    }
    arg = argv[i];
  }

  if (arg == NULL) {
    g_printerr("Usage: %s [--depth N] <puzzle-count|sgf-file>\n", argv[0]);
    return 1;
  }

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
    if (!checkers_puzzle_emit_from_line(rules,
                                        best_move_depth,
                                        game_line,
                                        output_dir,
                                        &next_index,
                                        G_MAXUINT,
                                        &emitted)) {
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
    if (!checkers_puzzle_emit_from_line(rules,
                                        best_move_depth,
                                        game_line,
                                        output_dir,
                                        &next_index,
                                        wanted - generated,
                                        &emitted)) {
      g_printerr("Puzzle extraction failed\n");
      return 1;
    }

    generated += emitted;
    games++;
    g_print("games=%u generated=%u/%u\n", games, generated, wanted);
  }

  return 0;
}
