#include "ai_alpha_beta.h"
#include "create_puzzles_cli.h"
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

G_DEFINE_AUTOPTR_CLEANUP_FUNC(CheckersAiTranspositionTable, checkers_ai_tt_free)

enum {
  CHECKERS_PUZZLE_SELF_PLAY_DEPTH = 0,
  CHECKERS_PUZZLE_DEFAULT_BEST_MOVE_DEPTH = 8,
  CHECKERS_PUZZLE_ANALYSIS_TT_SIZE_MB = 256,
  CHECKERS_PUZZLE_MISTAKE_THRESHOLD = 50,
  CHECKERS_PUZZLE_FORCED_MISTAKE_THRESHOLD = 100,
  CHECKERS_PUZZLE_SINGLE_MOVE_MARGIN = 50,
  CHECKERS_PUZZLE_MIN_LEGAL_MOVES = 4,
  CHECKERS_PUZZLE_MAX_TACTICAL_PLIES = 200,
};

typedef struct {
  CheckersMove move;
  CheckersColor color;
} CheckersPuzzleLineMove;

typedef struct {
  CheckersScoredMoveList moves;
  gint best_score;
  gint second_score;
  gint static_score;
  CheckersMove best_move;
} CheckersPuzzlePositionAnalysis;

typedef struct {
  GArray *line;
  gint start_static;
  gint final_static;
  CheckersMove solution_move;
  Game final_game;
} CheckersPuzzleValidatedCandidate;

typedef enum {
  CHECKERS_PUZZLE_REJECTION_ANALYZE_AFTER_SOLUTION = 0,
  CHECKERS_PUZZLE_REJECTION_IMMEDIATE_RECAPTURE,
  CHECKERS_PUZZLE_REJECTION_START_STATIC_EVAL,
  CHECKERS_PUZZLE_REJECTION_LINE_STATIC_EVAL,
  CHECKERS_PUZZLE_REJECTION_LINE_ENDED_BEFORE_IMPROVEMENT,
  CHECKERS_PUZZLE_REJECTION_LINE_ANALYZE_BEST_MOVES,
  CHECKERS_PUZZLE_REJECTION_ATTACKER_LOST_CLARITY,
  CHECKERS_PUZZLE_REJECTION_APPEND_BEST_MOVE,
  CHECKERS_PUZZLE_REJECTION_FINAL_STATIC_EVAL,
  CHECKERS_PUZZLE_REJECTION_LINE_MAX_PLIES,
  CHECKERS_PUZZLE_REJECTION_NO_LEGAL_MOVES,
  CHECKERS_PUZZLE_REJECTION_DUPLICATE_SOLUTION,
  CHECKERS_PUZZLE_REJECTION_RESULT_ANALYSIS_FAILED,
  CHECKERS_PUZZLE_REJECTION_ATTACKER_TOO_FEW_MOVES,
  CHECKERS_PUZZLE_REJECTION_ATTACKER_MARGIN_TOO_SMALL,
  CHECKERS_PUZZLE_REJECTION_INVALID_PUZZLE_POSITION,
  CHECKERS_PUZZLE_REJECTION_SINGLE_MOVE_SOLUTION,
  CHECKERS_PUZZLE_REJECTION_MOVE_MOVE_JUMP_SOLUTION,
  CHECKERS_PUZZLE_REJECTION_UNINTERESTING_SOLUTION,
  CHECKERS_PUZZLE_REJECTION_INSUFFICIENT_COMEBACK,
  CHECKERS_PUZZLE_REJECTION_SAVED_LINE_MISMATCH,
  CHECKERS_PUZZLE_REJECTION_PUZZLE_LOAD_FAILED,
  CHECKERS_PUZZLE_REJECTION_MOVE_COLOR_MISMATCH,
  CHECKERS_PUZZLE_REJECTION_MOVE_APPLY_FAILED,
  CHECKERS_PUZZLE_REJECTION_NOT_SERIOUS_MISTAKE,
  CHECKERS_PUZZLE_REJECTION_COUNT,
} CheckersPuzzleRejectionReason;

typedef struct {
  guint games_processed;
  guint moves_analyzed;
  guint puzzles_generated;
  guint existing_puzzles_checked;
  guint existing_puzzles_removed;
  guint existing_puzzles_would_remove;
  guint rejections[CHECKERS_PUZZLE_REJECTION_COUNT];
} CheckersPuzzleRunStats;

static gboolean checkers_puzzle_analyze_resulting_position(const Game *game,
                                                           guint best_move_depth,
                                                           CheckersAiTranspositionTable *tt,
                                                           CheckersPuzzlePositionAnalysis *out_analysis);
static gboolean checkers_puzzle_load_existing_solution_keys(const char *output_dir, GHashTable *out_keys);
static gboolean checkers_puzzle_try_forced_mistake_candidates(const Game *before_mistake_game,
                                                              guint best_move_depth,
                                                              CheckersAiTranspositionTable *tt,
                                                              const CheckersMove *exclude_move,
                                                              const GArray *game_line,
                                                              guint prefix_len,
                                                              gboolean save_game_sgf,
                                                              const char *output_dir,
                                                              GHashTable *existing_solution_keys,
                                                              guint *inout_index,
                                                              guint limit,
                                                              guint *out_emitted);

static void checkers_puzzle_log_progress(const char *format, ...) G_GNUC_PRINTF(1, 2);
static void checkers_puzzle_log_rejection(CheckersPuzzleRejectionReason reason,
                                          const char *format,
                                          ...) G_GNUC_PRINTF(2, 3);
static char *checkers_puzzle_build_ruleset_output_dir(PlayerRuleset ruleset);

static CheckersPuzzleRunStats checkers_puzzle_run_stats = {0};

static const char *checkers_puzzle_rejection_reason_label(CheckersPuzzleRejectionReason reason) {
  switch (reason) {
    case CHECKERS_PUZZLE_REJECTION_ANALYZE_AFTER_SOLUTION:
      return "failed to analyze the move immediately after the solution";
    case CHECKERS_PUZZLE_REJECTION_IMMEDIATE_RECAPTURE:
      return "move immediately after the solution is a recapture";
    case CHECKERS_PUZZLE_REJECTION_START_STATIC_EVAL:
      return "failed to evaluate static score at puzzle start";
    case CHECKERS_PUZZLE_REJECTION_LINE_STATIC_EVAL:
      return "failed static evaluation while building the line";
    case CHECKERS_PUZZLE_REJECTION_LINE_ENDED_BEFORE_IMPROVEMENT:
      return "solution line ended before static improvement";
    case CHECKERS_PUZZLE_REJECTION_LINE_ANALYZE_BEST_MOVES:
      return "failed to analyze best moves while building the line";
    case CHECKERS_PUZZLE_REJECTION_ATTACKER_LOST_CLARITY:
      return "attacker lost move clarity";
    case CHECKERS_PUZZLE_REJECTION_APPEND_BEST_MOVE:
      return "failed to append best move while building the line";
    case CHECKERS_PUZZLE_REJECTION_FINAL_STATIC_EVAL:
      return "failed final static evaluation";
    case CHECKERS_PUZZLE_REJECTION_LINE_MAX_PLIES:
      return "line reached max plies without static improvement";
    case CHECKERS_PUZZLE_REJECTION_NO_LEGAL_MOVES:
      return "resulting position has no legal moves";
    case CHECKERS_PUZZLE_REJECTION_DUPLICATE_SOLUTION:
      return "solution matches an existing puzzle";
    case CHECKERS_PUZZLE_REJECTION_RESULT_ANALYSIS_FAILED:
      return "failed to analyze the resulting position";
    case CHECKERS_PUZZLE_REJECTION_ATTACKER_TOO_FEW_MOVES:
      return "attacker has too few legal moves";
    case CHECKERS_PUZZLE_REJECTION_ATTACKER_MARGIN_TOO_SMALL:
      return "attacker best move margin is too small";
    case CHECKERS_PUZZLE_REJECTION_INVALID_PUZZLE_POSITION:
      return "resulting position is not a valid puzzle";
    case CHECKERS_PUZZLE_REJECTION_SINGLE_MOVE_SOLUTION:
      return "solution is only one move";
    case CHECKERS_PUZZLE_REJECTION_MOVE_MOVE_JUMP_SOLUTION:
      return "solution is move, move, jump";
    case CHECKERS_PUZZLE_REJECTION_UNINTERESTING_SOLUTION:
      return "solution shape is not interesting";
    case CHECKERS_PUZZLE_REJECTION_INSUFFICIENT_COMEBACK:
      return "solution does not improve a losing position enough";
    case CHECKERS_PUZZLE_REJECTION_SAVED_LINE_MISMATCH:
      return "saved puzzle line no longer matches the validated line";
    case CHECKERS_PUZZLE_REJECTION_PUZZLE_LOAD_FAILED:
      return "failed to load puzzle file";
    case CHECKERS_PUZZLE_REJECTION_MOVE_COLOR_MISMATCH:
      return "move color does not match side to move";
    case CHECKERS_PUZZLE_REJECTION_MOVE_APPLY_FAILED:
      return "move could not be applied to the game";
    case CHECKERS_PUZZLE_REJECTION_NOT_SERIOUS_MISTAKE:
      return "not a serious mistake";
    case CHECKERS_PUZZLE_REJECTION_COUNT:
    default:
      return "unknown rejection";
  }
}

static void checkers_puzzle_log_progress(const char *format, ...) {
  g_return_if_fail(format != NULL);

  va_list args;
  va_start(args, format);
  g_autofree char *message = g_strdup_vprintf(format, args);
  va_end(args);

  g_print("%s\n", message);
}

static void checkers_puzzle_log_rejection(CheckersPuzzleRejectionReason reason, const char *format, ...) {
  g_return_if_fail(reason < CHECKERS_PUZZLE_REJECTION_COUNT);
  g_return_if_fail(format != NULL);

  checkers_puzzle_run_stats.rejections[reason]++;

  va_list args;
  va_start(args, format);
  g_autofree char *message = g_strdup_vprintf(format, args);
  va_end(args);

  checkers_puzzle_log_progress("  -> %s", message);
}

static void checkers_puzzle_print_final_report(void) {
  guint rejected_total = 0;
  for (guint i = 0; i < CHECKERS_PUZZLE_REJECTION_COUNT; ++i) {
    rejected_total += checkers_puzzle_run_stats.rejections[i];
  }

  g_print("Report:\n");
  g_print("  games processed: %u\n", checkers_puzzle_run_stats.games_processed);
  g_print("  moves analyzed: %u\n", checkers_puzzle_run_stats.moves_analyzed);
  if (checkers_puzzle_run_stats.existing_puzzles_checked > 0) {
    g_print("  existing puzzles checked: %u\n", checkers_puzzle_run_stats.existing_puzzles_checked);
    if (checkers_puzzle_run_stats.existing_puzzles_removed > 0) {
      g_print("  existing puzzles removed: %u\n", checkers_puzzle_run_stats.existing_puzzles_removed);
    }
    if (checkers_puzzle_run_stats.existing_puzzles_would_remove > 0) {
      g_print("  existing puzzles that would be removed: %u\n",
              checkers_puzzle_run_stats.existing_puzzles_would_remove);
    }
  }
  g_print("  moves rejected: %u\n", rejected_total);
  for (guint i = 0; i < CHECKERS_PUZZLE_REJECTION_COUNT; ++i) {
    guint count = checkers_puzzle_run_stats.rejections[i];
    if (count == 0) {
      continue;
    }
    g_print("    %u: %s\n", count, checkers_puzzle_rejection_reason_label((CheckersPuzzleRejectionReason)i));
  }
  g_print("  puzzles generated: %u\n", checkers_puzzle_run_stats.puzzles_generated);
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

static gboolean checkers_puzzle_analyze_moves_with_shared_tt(const Game *game,
                                                             guint depth,
                                                             CheckersAiTranspositionTable *tt,
                                                             CheckersScoredMoveList *out_moves) {
  g_return_val_if_fail(game != NULL, FALSE);
  g_return_val_if_fail(depth > 0, FALSE);
  g_return_val_if_fail(out_moves != NULL, FALSE);

  CheckersAiSearchStats stats = {0};
  checkers_ai_search_stats_clear(&stats);
  return checkers_ai_alpha_beta_analyze_moves_cancellable_with_tt(game,
                                                                   depth,
                                                                   out_moves,
                                                                   NULL,
                                                                   NULL,
                                                                   NULL,
                                                                   NULL,
                                                                   tt,
                                                                   &stats);
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

static gboolean checkers_puzzle_parse_setup_point(const char *value,
                                                  guint8 board_size,
                                                  guint8 *out_square,
                                                  GError **error) {
  g_return_val_if_fail(value != NULL, FALSE);
  g_return_val_if_fail(board_size > 0, FALSE);
  g_return_val_if_fail(out_square != NULL, FALSE);

  gsize len = strlen(value);
  if (len == 2 && g_ascii_isalpha(value[0]) && g_ascii_isalpha(value[1])) {
    gint col = g_ascii_tolower(value[0]) - 'a';
    gint row = g_ascii_tolower(value[1]) - 'a';
    if (row < 0 || col < 0 || row >= board_size || col >= board_size) {
      g_set_error(error,
                  g_quark_from_static_string("checkers-puzzle-load-error"),
                  1,
                  "Out-of-range SGF setup point: %s",
                  value);
      return FALSE;
    }
    gint8 square = board_index_from_coord(row, col, board_size);
    if (square < 0) {
      g_set_error(error,
                  g_quark_from_static_string("checkers-puzzle-load-error"),
                  2,
                  "Non-playable SGF setup point: %s",
                  value);
      return FALSE;
    }
    *out_square = (guint8)square;
    return TRUE;
  }

  gchar *end = NULL;
  guint64 square_1based = g_ascii_strtoull(value, &end, 10);
  if (end == value || end == NULL || *end != '\0') {
    g_set_error(error,
                g_quark_from_static_string("checkers-puzzle-load-error"),
                3,
                "Invalid SGF setup point: %s",
                value);
    return FALSE;
  }
  guint8 max_square = board_playable_squares(board_size);
  if (square_1based == 0 || square_1based > max_square) {
    g_set_error(error,
                g_quark_from_static_string("checkers-puzzle-load-error"),
                4,
                "Out-of-range SGF setup square: %s",
                value);
    return FALSE;
  }
  *out_square = (guint8)(square_1based - 1);
  return TRUE;
}

static gboolean checkers_puzzle_apply_setup_points(GameState *state,
                                                   const GPtrArray *values,
                                                   CheckersPiece piece,
                                                   GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);

  if (values == NULL) {
    return TRUE;
  }

  for (guint i = 0; i < values->len; ++i) {
    const char *value = g_ptr_array_index((GPtrArray *)values, i);
    guint8 square = 0;
    if (!checkers_puzzle_parse_setup_point(value, state->board.board_size, &square, error)) {
      return FALSE;
    }
    board_set(&state->board, square, piece);
  }
  return TRUE;
}

static gboolean checkers_puzzle_apply_setup_properties(GameState *state, const SgfNode *node, GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  guint8 squares = board_playable_squares(state->board.board_size);
  for (guint8 i = 0; i < squares; ++i) {
    board_set(&state->board, i, CHECKERS_PIECE_EMPTY);
  }

  if (!checkers_puzzle_apply_setup_points(state, sgf_node_get_property_values(node, "AE"), CHECKERS_PIECE_EMPTY,
                                          error) ||
      !checkers_puzzle_apply_setup_points(state,
                                          sgf_node_get_property_values(node, "AB"),
                                          CHECKERS_PIECE_BLACK_MAN,
                                          error) ||
      !checkers_puzzle_apply_setup_points(state,
                                          sgf_node_get_property_values(node, "AW"),
                                          CHECKERS_PIECE_WHITE_MAN,
                                          error) ||
      !checkers_puzzle_apply_setup_points(state,
                                          sgf_node_get_property_values(node, "ABK"),
                                          CHECKERS_PIECE_BLACK_KING,
                                          error) ||
      !checkers_puzzle_apply_setup_points(state,
                                          sgf_node_get_property_values(node, "AWK"),
                                          CHECKERS_PIECE_WHITE_KING,
                                          error)) {
    return FALSE;
  }

  const char *pl = sgf_node_get_property_first(node, "PL");
  if (g_strcmp0(pl, "B") == 0) {
    state->turn = CHECKERS_COLOR_BLACK;
  } else if (g_strcmp0(pl, "W") == 0 || pl == NULL) {
    state->turn = CHECKERS_COLOR_WHITE;
  } else {
    g_set_error(error,
                g_quark_from_static_string("checkers-puzzle-load-error"),
                5,
                "Invalid SGF PL value: %s",
                pl);
    return FALSE;
  }

  state->winner = CHECKERS_WINNER_NONE;
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

static void checkers_puzzle_validated_candidate_clear(CheckersPuzzleValidatedCandidate *candidate) {
  g_return_if_fail(candidate != NULL);

  if (candidate->line != NULL) {
    g_array_unref(candidate->line);
  }
  memset(candidate, 0, sizeof(*candidate));
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

static GArray *checkers_puzzle_build_game_prefix_with_mistake(const GArray *game_line,
                                                              guint prefix_len,
                                                              CheckersColor color,
                                                              const CheckersMove *move) {
  g_return_val_if_fail(game_line != NULL, NULL);
  g_return_val_if_fail(prefix_len <= game_line->len, NULL);
  g_return_val_if_fail(color == CHECKERS_COLOR_WHITE || color == CHECKERS_COLOR_BLACK, NULL);
  g_return_val_if_fail(move != NULL, NULL);

  GArray *candidate_line = g_array_new(FALSE, FALSE, sizeof(CheckersPuzzleLineMove));
  for (guint i = 0; i < prefix_len; ++i) {
    CheckersPuzzleLineMove step = g_array_index(game_line, CheckersPuzzleLineMove, i);
    g_array_append_val(candidate_line, step);
  }

  CheckersPuzzleLineMove mistake_step = {
      .move = *move,
      .color = color,
  };
  g_array_append_val(candidate_line, mistake_step);
  return candidate_line;
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
                                                                            guint best_move_depth,
                                                                            CheckersAiTranspositionTable *tt) {
  g_return_val_if_fail(after_solution != NULL, FALSE);
  g_return_val_if_fail(line != NULL, FALSE);
  g_return_val_if_fail(best_move_depth > 0, FALSE);

  if (after_solution->state.winner != CHECKERS_WINNER_NONE) {
    return TRUE;
  }

  CheckersPuzzlePositionAnalysis analysis = {0};
  if (!checkers_puzzle_analyze_resulting_position(after_solution, best_move_depth, tt, &analysis)) {
    checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_ANALYZE_AFTER_SOLUTION,
                                  "failed to analyze the move immediately after the solution");
    return FALSE;
  }

  g_autofree CheckersMove *moves = g_new(CheckersMove, line->len);
  for (guint i = 0; i < line->len; ++i) {
    const CheckersPuzzleLineMove *entry = &g_array_index(line, CheckersPuzzleLineMove, i);
    moves[i] = entry->move;
  }
  gboolean stable = checkers_puzzle_solution_has_no_immediate_recapture(moves, line->len, &analysis.best_move);
  if (!stable) {
    checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_IMMEDIATE_RECAPTURE,
                                  "move immediately after the solution is a recapture");
  }
  checkers_puzzle_position_analysis_clear(&analysis);
  return stable;
}

static char *checkers_puzzle_build_line_solution_key(const GArray *line) {
  g_return_val_if_fail(line != NULL, NULL);

  g_autofree CheckersMove *moves = NULL;
  if (line->len > 0) {
    moves = g_new(CheckersMove, line->len);
    for (guint i = 0; i < line->len; ++i) {
      const CheckersPuzzleLineMove *entry = &g_array_index(line, CheckersPuzzleLineMove, i);
      moves[i] = entry->move;
    }
  }

  return checkers_puzzle_build_solution_key(moves, line->len);
}

static gboolean checkers_puzzle_solution_line_of_best_depth_moves_improves_static_evaluation(
    const Game *start,
    guint best_move_depth,
    CheckersAiTranspositionTable *tt,
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
    checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_START_STATIC_EVAL,
                                  "failed to evaluate static score at puzzle start");
    return FALSE;
  }

  for (guint ply = 0; ply < CHECKERS_PUZZLE_MAX_TACTICAL_PLIES; ++ply) {
    gint current_static = 0;
    if (!checkers_puzzle_evaluate_static(&line_game, &current_static)) {
      checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_LINE_STATIC_EVAL,
                                    "failed static evaluation while building the line at ply %u",
                                    ply);
      return FALSE;
    }

    if (checkers_puzzle_score_better_for_side(attacker, current_static, start_static)) {
      *out_final_static = current_static;
      *out_final_game = line_game;
      return TRUE;
    }

    if (line_game.state.winner != CHECKERS_WINNER_NONE) {
      checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_LINE_ENDED_BEFORE_IMPROVEMENT,
                                    "solution line ended before static improvement at ply %u (winner=%s)",
                                    ply,
                                    game_winner_label(line_game.state.winner));
      return FALSE;
    }

    CheckersPuzzlePositionAnalysis analysis = {0};
    if (!checkers_puzzle_analyze_resulting_position(&line_game, best_move_depth, tt, &analysis)) {
      checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_LINE_ANALYZE_BEST_MOVES,
                                    "failed to analyze best depth-%u moves while building the line at ply %u",
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
      checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_ATTACKER_LOST_CLARITY,
                                    "attacker lost move clarity at ply %u (%d vs %d)",
                                    ply,
                                    analysis.best_score,
                                    analysis.second_score);
      checkers_puzzle_position_analysis_clear(&analysis);
      return FALSE;
    }

    if (!checkers_puzzle_append_best_depth_move(&line_game, &analysis, out_line)) {
      checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_APPEND_BEST_MOVE,
                                    "failed to append best depth-%u move while building the line at ply %u",
                                    best_move_depth,
                                    ply);
      checkers_puzzle_position_analysis_clear(&analysis);
      return FALSE;
    }
    checkers_puzzle_position_analysis_clear(&analysis);
  }

  gint final_eval = 0;
  if (!checkers_puzzle_evaluate_static(&line_game, &final_eval)) {
    checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_FINAL_STATIC_EVAL,
                                  "failed final static evaluation after reaching the line max plies");
    return FALSE;
  }
  *out_final_static = final_eval;
  *out_final_game = line_game;
  checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_LINE_MAX_PLIES,
                                "line reached max plies without static improvement (%d vs %d)",
                                final_eval,
                                start_static);
  return checkers_puzzle_score_better_for_side(attacker, final_eval, start_static);
}

static gboolean checkers_puzzle_position_follows_a_serious_mistake(const Game *before_mistake_game,
                                                                   guint best_move_depth,
                                                                   CheckersAiTranspositionTable *tt,
                                                                   const CheckersMove *played_move,
                                                                   gint *out_mistake_delta) {
  g_return_val_if_fail(before_mistake_game != NULL, FALSE);
  g_return_val_if_fail(best_move_depth > 0, FALSE);
  g_return_val_if_fail(played_move != NULL, FALSE);
  g_return_val_if_fail(out_mistake_delta != NULL, FALSE);

  CheckersScoredMoveList analysis = {0};
  if (!checkers_puzzle_analyze_moves_with_shared_tt(before_mistake_game, best_move_depth, tt, &analysis)) {
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
                                                           CheckersAiTranspositionTable *tt,
                                                           CheckersPuzzlePositionAnalysis *out_analysis) {
  g_return_val_if_fail(game != NULL, FALSE);
  g_return_val_if_fail(best_move_depth > 0, FALSE);
  g_return_val_if_fail(out_analysis != NULL, FALSE);

  *out_analysis = (CheckersPuzzlePositionAnalysis) {0};
  if (!checkers_puzzle_analyze_moves_with_shared_tt(game, best_move_depth, tt, &out_analysis->moves)) {
    checkers_scored_move_list_free(&out_analysis->moves);
    return FALSE;
  }
  if (!checkers_puzzle_evaluate_static(game, &out_analysis->static_score)) {
    checkers_puzzle_position_analysis_clear(out_analysis);
    return FALSE;
  }
  if (out_analysis->moves.count == 0) {
    checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_NO_LEGAL_MOVES,
                                  "resulting position has no legal moves");
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

static gboolean checkers_puzzle_emit_validated_candidate(const GameState *start_state,
                                                         gint mistake_delta,
                                                         guint solution_depth,
                                                         const CheckersPuzzleValidatedCandidate *candidate,
                                                         const GArray *game_line,
                                                         gboolean save_game_sgf,
                                                         const char *output_dir,
                                                         GHashTable *existing_solution_keys,
                                                         guint *inout_index,
                                                         char **out_puzzle_path,
                                                         char **out_game_path) {
  g_return_val_if_fail(start_state != NULL, FALSE);
  g_return_val_if_fail(candidate != NULL, FALSE);
  g_return_val_if_fail(candidate->line != NULL, FALSE);
  g_return_val_if_fail(candidate->solution_move.length >= 2, FALSE);
  g_return_val_if_fail(game_line != NULL, FALSE);
  g_return_val_if_fail(output_dir != NULL, FALSE);
  g_return_val_if_fail(existing_solution_keys != NULL, FALSE);
  g_return_val_if_fail(inout_index != NULL, FALSE);
  g_return_val_if_fail(out_puzzle_path != NULL, FALSE);
  g_return_val_if_fail(out_game_path != NULL, FALSE);

  g_autofree char *solution_key = checkers_puzzle_build_line_solution_key(candidate->line);
  g_return_val_if_fail(solution_key != NULL, FALSE);
  if (g_hash_table_contains(existing_solution_keys, solution_key)) {
    checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_DUPLICATE_SOLUTION,
                                  "solution matches an existing puzzle");
    return FALSE;
  }

  g_autofree char *puzzle_path = checkers_puzzle_build_indexed_path(output_dir, "puzzle", *inout_index);
  g_return_val_if_fail(puzzle_path != NULL, FALSE);

  if (!checkers_puzzle_save_sgf(puzzle_path,
                                start_state,
                                candidate->line,
                                mistake_delta,
                                candidate->start_static,
                                candidate->final_static,
                                solution_depth,
                                &candidate->solution_move)) {
    return FALSE;
  }

  g_autofree char *game_path = NULL;
  if (save_game_sgf) {
    game_path = checkers_puzzle_build_indexed_path(output_dir, "game", *inout_index);
    g_return_val_if_fail(game_path != NULL, FALSE);
    if (!checkers_puzzle_save_game_sgf(game_path, game_line)) {
      return FALSE;
    }
  }

  g_hash_table_add(existing_solution_keys, g_steal_pointer(&solution_key));
  *out_puzzle_path = g_steal_pointer(&puzzle_path);
  *out_game_path = g_steal_pointer(&game_path);
  *inout_index += 1;
  return TRUE;
}

static gboolean checkers_puzzle_validate_candidate_from_resulting_position(
    const Game *post_mistake_game,
    guint best_move_depth,
    CheckersAiTranspositionTable *tt,
    CheckersPuzzleValidatedCandidate *out_candidate) {
  g_return_val_if_fail(post_mistake_game != NULL, FALSE);
  g_return_val_if_fail(best_move_depth > 0, FALSE);
  g_return_val_if_fail(out_candidate != NULL, FALSE);

  *out_candidate = (CheckersPuzzleValidatedCandidate) {0};

  CheckersPuzzlePositionAnalysis analysis = {0};
  if (!checkers_puzzle_analyze_resulting_position(post_mistake_game, best_move_depth, tt, &analysis)) {
    checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_RESULT_ANALYSIS_FAILED,
                                  "failed to analyze the resulting position");
    return FALSE;
  }
  if (!checkers_puzzle_attacker_has_enough_choice(&analysis)) {
    checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_ATTACKER_TOO_FEW_MOVES,
                                  "attacker has only %zu legal moves",
                                  analysis.moves.count);
    checkers_puzzle_position_analysis_clear(&analysis);
    return FALSE;
  }
  if (!checkers_puzzle_attacker_has_a_single_good_move(&analysis, post_mistake_game->state.turn)) {
    checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_ATTACKER_MARGIN_TOO_SMALL,
                                  "attacker best move is only %d points ahead of second-best (%d vs %d)",
                                  checkers_puzzle_score_gap_to_next_best(post_mistake_game->state.turn,
                                                                         analysis.best_score,
                                                                         analysis.second_score),
                                  analysis.best_score,
                                  analysis.second_score);
    checkers_puzzle_position_analysis_clear(&analysis);
    return FALSE;
  }
  if (!checkers_puzzle_position_is_valid(&analysis, post_mistake_game->state.turn)) {
    checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_INVALID_PUZZLE_POSITION,
                                  "resulting position is not a valid puzzle");
    checkers_puzzle_position_analysis_clear(&analysis);
    return FALSE;
  }

  GArray *line = g_array_new(FALSE, FALSE, sizeof(CheckersPuzzleLineMove));
  gint final_static = analysis.static_score;
  Game final_game = {0};
  if (!checkers_puzzle_solution_line_of_best_depth_moves_improves_static_evaluation(post_mistake_game,
                                                                                    best_move_depth,
                                                                                    tt,
                                                                                    line,
                                                                                    &final_static,
                                                                                    &final_game)) {
    checkers_puzzle_position_analysis_clear(&analysis);
    g_array_unref(line);
    return FALSE;
  }
  if (checkers_puzzle_solution_is_a_single_move(line)) {
    checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_SINGLE_MOVE_SOLUTION,
                                  "solution is only one move");
    checkers_puzzle_position_analysis_clear(&analysis);
    g_array_unref(line);
    return FALSE;
  }
  if (checkers_puzzle_solution_is_move_move_jump(line)) {
    checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_MOVE_MOVE_JUMP_SOLUTION,
                                  "solution is move, move, jump");
    checkers_puzzle_position_analysis_clear(&analysis);
    g_array_unref(line);
    return FALSE;
  }
  if (!checkers_puzzle_solution_is_interesting(line)) {
    checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_UNINTERESTING_SOLUTION,
                                  "solution shape is not interesting");
    checkers_puzzle_position_analysis_clear(&analysis);
    g_array_unref(line);
    return FALSE;
  }
  if (!checkers_puzzle_solution_evaluation_swing_is_interesting(post_mistake_game->state.turn,
                                                                analysis.static_score,
                                                                final_static)) {
    checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_INSUFFICIENT_COMEBACK,
                                  "attacker was too far behind and only improved from %d to %d",
                                  analysis.static_score,
                                  final_static);
    checkers_puzzle_position_analysis_clear(&analysis);
    g_array_unref(line);
    return FALSE;
  }
  if (!checkers_puzzle_solution_ends_before_an_immediate_recapture(&final_game,
                                                                   line,
                                                                   best_move_depth,
                                                                   tt)) {
    checkers_puzzle_position_analysis_clear(&analysis);
    g_array_unref(line);
    return FALSE;
  }

  out_candidate->line = line;
  out_candidate->start_static = analysis.static_score;
  out_candidate->final_static = final_static;
  out_candidate->solution_move = analysis.best_move;
  out_candidate->final_game = final_game;
  checkers_puzzle_position_analysis_clear(&analysis);
  return TRUE;
}

static gboolean checkers_puzzle_emit_candidate_if_valid(const Game *post_mistake_game,
                                                        guint best_move_depth,
                                                        CheckersAiTranspositionTable *tt,
                                                        gint mistake_delta,
                                                        const GArray *game_line,
                                                        gboolean save_game_sgf,
                                                        const char *output_dir,
                                                        GHashTable *existing_solution_keys,
                                                        guint *inout_index,
                                                        guint *out_emitted) {
  g_return_val_if_fail(post_mistake_game != NULL, FALSE);
  g_return_val_if_fail(best_move_depth > 0, FALSE);
  g_return_val_if_fail(game_line != NULL, FALSE);
  g_return_val_if_fail(output_dir != NULL, FALSE);
  g_return_val_if_fail(existing_solution_keys != NULL, FALSE);
  g_return_val_if_fail(inout_index != NULL, FALSE);
  g_return_val_if_fail(out_emitted != NULL, FALSE);

  CheckersPuzzleValidatedCandidate validated = {0};
  if (!checkers_puzzle_validate_candidate_from_resulting_position(post_mistake_game,
                                                                  best_move_depth,
                                                                  tt,
                                                                  &validated)) {
    return FALSE;
  }

  g_autofree char *puzzle_path = NULL;
  g_autofree char *game_path = NULL;
  if (!checkers_puzzle_emit_validated_candidate(&post_mistake_game->state,
                                                mistake_delta,
                                                best_move_depth,
                                                &validated,
                                                game_line,
                                                save_game_sgf,
                                                output_dir,
                                                existing_solution_keys,
                                                inout_index,
                                                &puzzle_path,
                                                &game_path)) {
    checkers_puzzle_validated_candidate_clear(&validated);
    return FALSE;
  }

  (*out_emitted)++;
  checkers_puzzle_run_stats.puzzles_generated++;
  checkers_puzzle_log_progress("  -> kept: mistake_delta=%d start_static=%d final_static=%d line_plies=%u",
                               mistake_delta,
                               validated.start_static,
                               validated.final_static,
                               validated.line->len);
  if (game_path != NULL) {
    checkers_puzzle_log_progress("  -> saved %s and %s", puzzle_path, game_path);
  } else {
    checkers_puzzle_log_progress("  -> saved %s", puzzle_path);
  }
  checkers_puzzle_validated_candidate_clear(&validated);
  return TRUE;
}

static gboolean checkers_puzzle_try_forced_mistake_candidates(const Game *before_mistake_game,
                                                              guint best_move_depth,
                                                              CheckersAiTranspositionTable *tt,
                                                              const CheckersMove *exclude_move,
                                                              const GArray *game_line,
                                                              guint prefix_len,
                                                              gboolean save_game_sgf,
                                                              const char *output_dir,
                                                              GHashTable *existing_solution_keys,
                                                              guint *inout_index,
                                                              guint limit,
                                                              guint *out_emitted) {
  g_return_val_if_fail(before_mistake_game != NULL, FALSE);
  g_return_val_if_fail(best_move_depth > 0, FALSE);
  g_return_val_if_fail(game_line != NULL, FALSE);
  g_return_val_if_fail(output_dir != NULL, FALSE);
  g_return_val_if_fail(existing_solution_keys != NULL, FALSE);
  g_return_val_if_fail(inout_index != NULL, FALSE);
  g_return_val_if_fail(out_emitted != NULL, FALSE);

  if (*out_emitted >= limit) {
    return TRUE;
  }

  CheckersScoredMoveList analysis = {0};
  if (!checkers_puzzle_analyze_moves_with_shared_tt(before_mistake_game, best_move_depth, tt, &analysis)) {
    return FALSE;
  }

  g_autoptr(GArray) candidates = g_array_new(FALSE, FALSE, sizeof(CheckersPuzzleMistakeCandidate));
  if (!checkers_puzzle_collect_mistake_candidates(&analysis,
                                                  before_mistake_game->state.turn,
                                                  CHECKERS_PUZZLE_FORCED_MISTAKE_THRESHOLD,
                                                  exclude_move,
                                                  candidates)) {
    checkers_scored_move_list_free(&analysis);
    return FALSE;
  }
  checkers_scored_move_list_free(&analysis);

  for (guint i = 0; i < candidates->len && *out_emitted < limit; ++i) {
    const CheckersPuzzleMistakeCandidate *candidate =
        &g_array_index(candidates, CheckersPuzzleMistakeCandidate, i);
    checkers_puzzle_run_stats.moves_analyzed++;

    Game post_mistake_game = *before_mistake_game;
    if (game_apply_move(&post_mistake_game, &candidate->move) != 0) {
      checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_MOVE_APPLY_FAILED,
                                    "forced mistake move could not be applied to the game");
      return FALSE;
    }

    g_autoptr(GArray) candidate_game_line =
        checkers_puzzle_build_game_prefix_with_mistake(game_line,
                                                       prefix_len,
                                                       before_mistake_game->state.turn,
                                                       &candidate->move);
    if (candidate_game_line == NULL) {
      return FALSE;
    }

    char move_text[128] = {0};
    if (!checkers_puzzle_format_move(&candidate->move, move_text)) {
      g_strlcpy(move_text, "?", sizeof(move_text));
    }
    checkers_puzzle_log_progress("  -> trying forced mistake %s (delta=%d depth=%u)",
                                 move_text,
                                 candidate->mistake_delta,
                                 best_move_depth);

    (void)checkers_puzzle_emit_candidate_if_valid(&post_mistake_game,
                                                  best_move_depth,
                                                  tt,
                                                  candidate->mistake_delta,
                                                  candidate_game_line,
                                                  save_game_sgf,
                                                  output_dir,
                                                  existing_solution_keys,
                                                  inout_index,
                                                  out_emitted);
  }

  return TRUE;
}

static gboolean checkers_puzzle_emit_from_line(const CheckersRules *rules,
                                               guint best_move_depth,
                                               CheckersAiTranspositionTable *tt,
                                               gboolean try_forced_mistakes,
                                               gboolean save_game_sgf,
                                               const GArray *game_line,
                                               const char *output_dir,
                                               GHashTable *existing_solution_keys,
                                               guint *inout_index,
                                               guint limit,
                                               guint *out_emitted) {
  g_return_val_if_fail(rules != NULL, FALSE);
  g_return_val_if_fail(best_move_depth > 0, FALSE);
  g_return_val_if_fail(tt != NULL, FALSE);
  g_return_val_if_fail(game_line != NULL, FALSE);
  g_return_val_if_fail(output_dir != NULL, FALSE);
  g_return_val_if_fail(existing_solution_keys != NULL, FALSE);
  g_return_val_if_fail(inout_index != NULL, FALSE);
  g_return_val_if_fail(out_emitted != NULL, FALSE);

  *out_emitted = 0;
  Game game = {0};
  game_init_with_rules(&game, rules);
  for (guint i = 0; i < game_line->len && *out_emitted < limit; ++i) {
    const CheckersPuzzleLineMove *played = &g_array_index(game_line, CheckersPuzzleLineMove, i);
    Game before = game;
    char move_text[128] = {0};
    gboolean has_move_text = checkers_puzzle_format_move(&played->move, move_text);

    checkers_puzzle_log_progress("Considering move #%u%s%s",
            i + 1,
            has_move_text ? " " : "",
            has_move_text ? move_text : "");
    checkers_puzzle_run_stats.moves_analyzed++;

    if (try_forced_mistakes) {
      if (!checkers_puzzle_try_forced_mistake_candidates(&before,
                                                         best_move_depth,
                                                         tt,
                                                         &played->move,
                                                         game_line,
                                                         i,
                                                         save_game_sgf,
                                                         output_dir,
                                                         existing_solution_keys,
                                                         inout_index,
                                                         limit,
                                                         out_emitted)) {
        game_destroy(&game);
        return FALSE;
      }
      if (*out_emitted >= limit) {
        break;
      }
    }

    if (played->color != before.state.turn) {
      checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_MOVE_COLOR_MISMATCH,
                                    "move color does not match side to move");
      game_destroy(&game);
      return FALSE;
    }

    gint mistake_delta = 0;
    gboolean is_mistake = checkers_puzzle_position_follows_a_serious_mistake(&before,
                                                                             best_move_depth,
                                                                             tt,
                                                                             &played->move,
                                                                             &mistake_delta);

    if (game_apply_move(&game, &played->move) != 0) {
      checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_MOVE_APPLY_FAILED,
                                    "move could not be applied to the game");
      game_destroy(&game);
      return FALSE;
    }

    if (is_mistake) {
      (void)checkers_puzzle_emit_candidate_if_valid(&game,
                                                    best_move_depth,
                                                    tt,
                                                    mistake_delta,
                                                    game_line,
                                                    save_game_sgf,
                                                    output_dir,
                                                    existing_solution_keys,
                                                    inout_index,
                                                    out_emitted);
    } else {
      checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_NOT_SERIOUS_MISTAKE,
                                    "not a serious mistake");
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

  checkers_puzzle_log_progress("Played game ended after %u moves (winner=%s)",
                               out_line->len,
                               game_winner_label(game.state.winner));
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

static gboolean checkers_puzzle_line_replays_from_game(const GArray *game_line, Game *game) {
  g_return_val_if_fail(game_line != NULL, FALSE);
  g_return_val_if_fail(game != NULL, FALSE);

  for (guint i = 0; i < game_line->len; ++i) {
    const CheckersPuzzleLineMove *step = &g_array_index(game_line, CheckersPuzzleLineMove, i);
    if (step->color != game->state.turn || game_apply_move(game, &step->move) != 0) {
      return FALSE;
    }
  }
  return TRUE;
}

static const CheckersRules *checkers_puzzle_find_matching_rules_for_setup(const SgfNode *root, const GArray *line) {
  g_return_val_if_fail(root != NULL, NULL);
  g_return_val_if_fail(line != NULL, NULL);

  guint count = checkers_ruleset_count();
  for (guint i = 0; i < count; ++i) {
    const CheckersRules *rules = checkers_ruleset_get_rules((PlayerRuleset)i);
    if (rules == NULL) {
      continue;
    }

    Game game = {0};
    game_init_with_rules(&game, rules);
    g_autoptr(GError) setup_error = NULL;
    if (!checkers_puzzle_apply_setup_properties(&game.state, root, &setup_error)) {
      game_destroy(&game);
      continue;
    }
    gboolean matches = checkers_puzzle_line_replays_from_game(line, &game);
    game_destroy(&game);
    if (matches) {
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

static gboolean checkers_puzzle_lines_equal(const GArray *left, const GArray *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  if (left->len != right->len) {
    return FALSE;
  }

  for (guint i = 0; i < left->len; ++i) {
    const CheckersPuzzleLineMove *a = &g_array_index(left, CheckersPuzzleLineMove, i);
    const CheckersPuzzleLineMove *b = &g_array_index(right, CheckersPuzzleLineMove, i);
    if (a->color != b->color || !checkers_puzzle_moves_equal(&a->move, &b->move)) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean checkers_puzzle_load_puzzle_file(const char *path, Game *out_game, GArray *out_line) {
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(out_game != NULL, FALSE);
  g_return_val_if_fail(out_line != NULL, FALSE);

  g_autoptr(SgfTree) tree = NULL;
  g_autoptr(GError) load_error = NULL;
  if (!sgf_io_load_file(path, &tree, &load_error)) {
    g_debug("Failed to load puzzle SGF %s: %s", path, load_error != NULL ? load_error->message : "unknown error");
    return FALSE;
  }

  const SgfNode *root = sgf_tree_get_root(tree);
  g_return_val_if_fail(root != NULL, FALSE);

  g_autoptr(GPtrArray) line_nodes = sgf_tree_build_main_line(tree);
  if (line_nodes == NULL || line_nodes->len == 0) {
    g_debug("Puzzle SGF has no main line: %s", path);
    return FALSE;
  }

  for (guint i = 1; i < line_nodes->len; ++i) {
    const SgfNode *node = g_ptr_array_index(line_nodes, i);
    g_return_val_if_fail(node != NULL, FALSE);

    SgfColor color = SGF_COLOR_NONE;
    CheckersMove move = {0};
    gboolean has_move = FALSE;
    g_autoptr(GError) move_error = NULL;
    if (!sgf_move_props_try_parse_node(node, &color, &move, &has_move, &move_error)) {
      g_debug("Failed to parse puzzle SGF move in %s: %s",
              path,
              move_error != NULL ? move_error->message : "unknown error");
      return FALSE;
    }
    if (!has_move) {
      continue;
    }

    CheckersPuzzleLineMove step = {
        .move = move,
        .color = color == SGF_COLOR_BLACK ? CHECKERS_COLOR_BLACK : CHECKERS_COLOR_WHITE,
    };
    g_array_append_val(out_line, step);
  }

  if (out_line->len == 0) {
    g_debug("Puzzle SGF has no tactical moves: %s", path);
    return FALSE;
  }

  const CheckersRules *rules = checkers_puzzle_find_matching_rules_for_setup(root, out_line);
  if (rules == NULL) {
    g_debug("Could not match puzzle SGF to known rules: %s", path);
    return FALSE;
  }

  game_init_with_rules(out_game, rules);
  g_autoptr(GError) setup_error = NULL;
  if (!checkers_puzzle_apply_setup_properties(&out_game->state, root, &setup_error)) {
    g_debug("Failed to apply puzzle setup from %s: %s",
            path,
            setup_error != NULL ? setup_error->message : "unknown error");
    game_destroy(out_game);
    return FALSE;
  }

  return TRUE;
}

static gboolean checkers_puzzle_name_is_puzzle_sgf(const char *name) {
  g_return_val_if_fail(name != NULL, FALSE);

  return g_str_has_prefix(name, "puzzle-") && g_str_has_suffix(name, ".sgf");
}

static char *checkers_puzzle_build_matching_game_path(const char *puzzle_path) {
  g_return_val_if_fail(puzzle_path != NULL, NULL);

  g_autofree char *dir = g_path_get_dirname(puzzle_path);
  g_autofree char *base = g_path_get_basename(puzzle_path);
  if (!checkers_puzzle_name_is_puzzle_sgf(base)) {
    return NULL;
  }

  g_autofree char *game_name = g_strdup_printf("game-%s", base + strlen("puzzle-"));
  return g_build_filename(dir, game_name, NULL);
}

static gboolean checkers_puzzle_delete_file_if_present(const char *path) {
  g_return_val_if_fail(path != NULL, FALSE);

  if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
    return TRUE;
  }
  if (g_remove(path) != 0) {
    g_debug("Failed to delete %s: %s", path, g_strerror(errno));
    return FALSE;
  }
  return TRUE;
}

static gboolean checkers_puzzle_check_existing_file(const char *path,
                                                    guint best_move_depth,
                                                    CheckersAiTranspositionTable *tt,
                                                    gboolean dry_run) {
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(best_move_depth > 0, FALSE);
  g_return_val_if_fail(tt != NULL, FALSE);

  checkers_puzzle_log_progress("Checking %s...", path);
  checkers_puzzle_run_stats.existing_puzzles_checked++;

  Game start_game = {0};
  g_autoptr(GArray) saved_line = g_array_new(FALSE, FALSE, sizeof(CheckersPuzzleLineMove));
  if (!checkers_puzzle_load_puzzle_file(path, &start_game, saved_line)) {
    checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_PUZZLE_LOAD_FAILED,
                                  "failed to load puzzle file %s",
                                  path);
    if (dry_run) {
      checkers_puzzle_run_stats.existing_puzzles_would_remove++;
      checkers_puzzle_log_progress("  -> invalid");
      return TRUE;
    }
    checkers_puzzle_log_progress("  -> invalid, deleting");
    if (!checkers_puzzle_delete_file_if_present(path)) {
      return FALSE;
    }
    checkers_puzzle_run_stats.existing_puzzles_removed++;
    return TRUE;
  }

  CheckersPuzzleValidatedCandidate validated = {0};
  gboolean valid = checkers_puzzle_validate_candidate_from_resulting_position(&start_game,
                                                                              best_move_depth,
                                                                              tt,
                                                                              &validated);
  if (valid && !checkers_puzzle_lines_equal(saved_line, validated.line)) {
    valid = FALSE;
    checkers_puzzle_log_rejection(CHECKERS_PUZZLE_REJECTION_SAVED_LINE_MISMATCH,
                                  "saved line no longer matches validated line for %s",
                                  path);
  }
  game_destroy(&start_game);

  if (valid) {
    checkers_puzzle_validated_candidate_clear(&validated);
    checkers_puzzle_log_progress("  -> kept");
    return TRUE;
  }

  checkers_puzzle_validated_candidate_clear(&validated);
  g_autofree char *game_path = checkers_puzzle_build_matching_game_path(path);
  if (dry_run) {
    checkers_puzzle_run_stats.existing_puzzles_would_remove++;
    checkers_puzzle_log_progress("  -> invalid");
    if (game_path != NULL && g_file_test(game_path, G_FILE_TEST_EXISTS)) {
      checkers_puzzle_log_progress("     would delete %s", game_path);
    }
    return TRUE;
  }

  checkers_puzzle_log_progress("  -> invalid, deleting");
  if (!checkers_puzzle_delete_file_if_present(path)) {
    return FALSE;
  }
  checkers_puzzle_log_progress("     deleted %s", path);
  if (game_path != NULL && g_file_test(game_path, G_FILE_TEST_EXISTS)) {
    if (!checkers_puzzle_delete_file_if_present(game_path)) {
      return FALSE;
    }
    checkers_puzzle_log_progress("     deleted %s", game_path);
  }
  checkers_puzzle_run_stats.existing_puzzles_removed++;
  return TRUE;
}

static gboolean checkers_puzzle_check_existing_dir(const char *dir_path,
                                                   guint best_move_depth,
                                                   CheckersAiTranspositionTable *tt,
                                                   gboolean dry_run) {
  g_return_val_if_fail(dir_path != NULL, FALSE);
  g_return_val_if_fail(best_move_depth > 0, FALSE);
  g_return_val_if_fail(tt != NULL, FALSE);

  if (!g_file_test(dir_path, G_FILE_TEST_IS_DIR)) {
    g_debug("Puzzle directory does not exist: %s", dir_path);
    return TRUE;
  }

  g_autoptr(GDir) dir = g_dir_open(dir_path, 0, NULL);
  if (dir == NULL) {
    g_debug("Failed to open puzzle directory %s", dir_path);
    return FALSE;
  }

  const char *name = NULL;
  while ((name = g_dir_read_name(dir)) != NULL) {
    if (!checkers_puzzle_name_is_puzzle_sgf(name)) {
      continue;
    }

    g_autofree char *path = g_build_filename(dir_path, name, NULL);
    if (!checkers_puzzle_check_existing_file(path, best_move_depth, tt, dry_run)) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean checkers_puzzle_load_existing_solution_keys(const char *output_dir, GHashTable *out_keys) {
  g_return_val_if_fail(output_dir != NULL, FALSE);
  g_return_val_if_fail(out_keys != NULL, FALSE);

  if (!g_file_test(output_dir, G_FILE_TEST_IS_DIR)) {
    return TRUE;
  }

  g_autoptr(GError) dir_error = NULL;
  g_autoptr(GDir) dir = g_dir_open(output_dir, 0, &dir_error);
  if (dir == NULL) {
    g_debug("Failed to open puzzle output dir %s: %s",
            output_dir,
            dir_error != NULL ? dir_error->message : "unknown error");
    return FALSE;
  }

  guint loaded = 0;
  const char *name = NULL;
  while ((name = g_dir_read_name(dir)) != NULL) {
    if (!checkers_puzzle_name_is_puzzle_sgf(name)) {
      continue;
    }

    g_autofree char *path = g_build_filename(output_dir, name, NULL);
    g_autoptr(GArray) line = g_array_new(FALSE, FALSE, sizeof(CheckersPuzzleLineMove));
    if (!checkers_puzzle_load_main_line(path, line)) {
      g_debug("Failed to load existing puzzle line from %s", path);
      return FALSE;
    }

    g_autofree char *key = checkers_puzzle_build_line_solution_key(line);
    if (key == NULL) {
      g_debug("Failed to build solution key for existing puzzle %s", path);
      return FALSE;
    }

    g_hash_table_add(out_keys, g_steal_pointer(&key));
    loaded++;
  }

  checkers_puzzle_log_progress("Loaded %u existing puzzle solution keys", loaded);
  return TRUE;
}

static char *checkers_puzzle_build_ruleset_output_dir(PlayerRuleset ruleset) {
  const char *short_name = checkers_ruleset_short_name(ruleset);
  if (short_name == NULL) {
    g_debug("Missing short name for ruleset %d", (gint)ruleset);
    return NULL;
  }

  return g_build_filename("puzzles", short_name, NULL);
}

int main(int argc, char **argv) {
  CheckersCreatePuzzlesCliOptions options = {0};
  g_autofree char *parse_error = NULL;
  if (!checkers_create_puzzles_cli_parse(argc,
                                         argv,
                                         CHECKERS_PUZZLE_DEFAULT_BEST_MOVE_DEPTH,
                                         &options,
                                         &parse_error)) {
    g_printerr("%s\n", parse_error != NULL ? parse_error : "Invalid arguments");
    g_printerr("Usage: %s --ruleset <short-name> [--depth N] [--synthetic-candidates] [--save-games] ",
               argv[0]);
    g_printerr("<puzzle-count|sgf-file>\n");
    g_printerr("   or: %s --ruleset <short-name> [--depth N] --check-existing [--dry-run] [puzzle-dir]\n", argv[0]);
    return 1;
  }

  g_autofree char *ruleset_dir = checkers_puzzle_build_ruleset_output_dir(options.ruleset);
  if (ruleset_dir == NULL) {
    g_printerr("Missing ruleset output directory\n");
    return 1;
  }

  if (options.mode == CHECKERS_CREATE_PUZZLES_MODE_CHECK_EXISTING) {
    const char *dir_path = options.arg != NULL ? options.arg : ruleset_dir;
    g_autoptr(CheckersAiTranspositionTable) analysis_tt =
        checkers_ai_tt_new(CHECKERS_PUZZLE_ANALYSIS_TT_SIZE_MB);
    if (analysis_tt == NULL) {
      g_printerr("Failed to allocate shared analysis TT\n");
      return 1;
    }
    if (!checkers_puzzle_check_existing_dir(dir_path, options.depth, analysis_tt, options.dry_run)) {
      g_printerr("Failed to check existing puzzles in %s\n", dir_path);
      return 1;
    }
    checkers_puzzle_print_final_report();
    return 0;
  }

  guint wanted = 0;
  CheckersPuzzleArgType arg_type = checkers_puzzle_parse_arg(options.arg, &wanted);
  if (arg_type == CHECKERS_PUZZLE_ARG_INVALID) {
    g_printerr("Invalid argument: %s\n", options.arg);
    return 1;
  }

  const char *output_dir = ruleset_dir;
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

  g_autoptr(GHashTable) existing_solution_keys = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  if (!checkers_puzzle_load_existing_solution_keys(output_dir, existing_solution_keys)) {
    g_printerr("Failed to load existing puzzles from %s\n", output_dir);
    return 1;
  }

  g_autoptr(CheckersAiTranspositionTable) analysis_tt =
      checkers_ai_tt_new(CHECKERS_PUZZLE_ANALYSIS_TT_SIZE_MB);
  if (analysis_tt == NULL) {
    g_printerr("Failed to allocate shared analysis TT\n");
    return 1;
  }

  if (arg_type == CHECKERS_PUZZLE_ARG_FILE) {
    checkers_puzzle_run_stats.games_processed = 1;
    g_autoptr(GArray) game_line = g_array_new(FALSE, FALSE, sizeof(CheckersPuzzleLineMove));
    if (!checkers_puzzle_load_main_line(options.arg, game_line)) {
      g_printerr("Failed to load game line from file: %s\n", options.arg);
      return 1;
    }

    const CheckersRules *rules = checkers_ruleset_get_rules(options.ruleset);
    if (rules == NULL) {
      g_printerr("Missing ruleset\n");
      return 1;
    }
    if (!checkers_puzzle_line_replays_with_rules(game_line, rules)) {
      g_printerr("SGF game line is not compatible with ruleset %s\n", checkers_ruleset_short_name(options.ruleset));
      return 1;
    }

    guint emitted = 0;
    if (!checkers_puzzle_emit_from_line(rules,
                                        options.depth,
                                        analysis_tt,
                                        options.try_forced_mistakes,
                                        options.save_games,
                                        game_line,
                                        output_dir,
                                        existing_solution_keys,
                                        &next_index,
                                        G_MAXUINT,
                                        &emitted)) {
      g_printerr("Failed to extract puzzles from file\n");
      return 1;
    }
    g_print("file=%s\n", options.arg);
    checkers_puzzle_print_final_report();
    return 0;
  }

  const CheckersRules *rules = checkers_ruleset_get_rules(options.ruleset);
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
                                        options.depth,
                                        analysis_tt,
                                        options.try_forced_mistakes,
                                        options.save_games,
                                        game_line,
                                        output_dir,
                                        existing_solution_keys,
                                        &next_index,
                                        wanted - generated,
                                        &emitted)) {
      g_printerr("Puzzle extraction failed\n");
      return 1;
    }

    generated += emitted;
    games++;
    checkers_puzzle_run_stats.games_processed = games;
  }

  checkers_puzzle_print_final_report();
  return 0;
}
