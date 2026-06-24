#include "homeworlds_backend.h"

#include "homeworlds_game.h"
#include "homeworlds_move_builder.h"
#include "homeworlds_sgf_position.h"

#include <stdarg.h>
#include <string.h>

enum {
  HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_LIMIT = 512,
  HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_WINDOW = 50,
  HOMEWORLDS_GOAL_BRANCH_SMALL_LEAF_LIMIT = 50,
  HOMEWORLDS_GOAL_REPORT_MAX_LINES = 2048,
};

typedef struct {
  guint system_index;
  HomeworldsColor color;
  HomeworldsSystemRef system_ref;
} HomeworldsProfitableCatastrophe;

typedef struct {
  HomeworldsProfitableCatastrophe root_catastrophes[HOMEWORLDS_SYSTEM_SLOT_COUNT * 4];
  guint root_catastrophe_count;
  gboolean has_score_interval;
  gint score_interval_min;
  gint score_interval_max;
  gboolean defer_root_catastrophes;
  guint root_defer_step_count;
  gsize pruning_checked_branches;
  gsize pruning_window_cutoff_branches;
  gsize pruning_pruned_branches;
  gsize ordering_candidate_lists;
  gsize ordering_reordered_candidate_lists;
  gsize ordering_reordered_candidates;
  gsize ordering_single_step_passes;
  gsize ordering_single_step_moves;
  gsize goal_branches_created;
  gsize goal_branches_selected;
  gsize goal_branches_split;
  gsize goal_branches_requeued;
  gsize goal_branches_direct;
  gsize goal_branches_skipped;
  gsize goal_branches_exhausted;
  gsize goal_duplicate_states;
  gsize goal_rejected_steps;
  gsize goal_rejected_bad_moves;
  gsize goal_rejected_root_catastrophes;
  gsize goal_rejected_score_intervals;
  gsize goal_rejected_score_windows;
  gsize goal_rejected_full_buffer;
  GString *goal_report;
  gsize goal_report_lines;
  gboolean goal_report_truncated;
} HomeworldsGoodMoveContext;

typedef enum {
  HOMEWORLDS_GOOD_MOVE_CUTOFF_NONE = 0,
  HOMEWORLDS_GOOD_MOVE_CUTOFF_SCORE_WINDOW,
  HOMEWORLDS_GOOD_MOVE_CUTOFF_FULL_BUFFER,
} HomeworldsGoodMoveCutoffKind;

typedef struct {
  HomeworldsMove move;
  gint score;
  gsize original_index;
} HomeworldsScoredMove;

typedef struct {
  const HomeworldsPosition *position;
  HomeworldsScoredMove *moves;
  gsize count;
  gsize capacity;
  gsize leaves_seen;
  gsize scored_moves;
  gsize next_original_index;
  guint side;
  gint best_score;
  gboolean has_best_score;
  gboolean prune_by_score;
} HomeworldsMoveBuffer;

typedef struct {
  gsize index;
  gboolean is_pass;
  gboolean has_priority;
  gint bound;
  gint current_score;
  gint buildable_gain;
  guint catastrophe_gain;
} HomeworldsCandidateOrder;

typedef enum {
  HOMEWORLDS_GOAL_BRANCH_ROOT = 0,
  HOMEWORLDS_GOAL_BRANCH_ROOT_CATASTROPHE_NOW,
  HOMEWORLDS_GOAL_BRANCH_ROOT_CATASTROPHE_POSTPONE,
  HOMEWORLDS_GOAL_BRANCH_SINGLE_STEP,
  HOMEWORLDS_GOAL_BRANCH_SACRIFICE,
  HOMEWORLDS_GOAL_BRANCH_YELLOW_SACRIFICE,
  HOMEWORLDS_GOAL_BRANCH_GENERIC,
} HomeworldsGoalBranchKind;

typedef struct {
  gint ref_count;
  HomeworldsGenerationDedupe dedupe;
} HomeworldsGoalDedupeRef;

typedef struct {
  gsize id;
  HomeworldsGoalBranchKind kind;
  HomeworldsMoveBuilderState state;
  HomeworldsGenerationContext generation_context;
  HomeworldsGoalDedupeRef *dedupe_ref;
  gint interval_min;
  gint interval_max;
  gint parent_score;
  gint parent_delta_min;
  gint parent_delta_max;
  gsize leaf_upper_bound;
  guint root_defer_step_count;
  gboolean defer_root_catastrophes;
  gboolean allow_pass_move;
  char reason[96];
} HomeworldsGoalBranch;

typedef struct {
  GPtrArray *branches;
  gsize next_branch_id;
} HomeworldsGoalQueue;

static HomeworldsGoodMoveTraceFunc homeworlds_backend_good_move_trace_func = NULL;
static gpointer homeworlds_backend_good_move_trace_user_data = NULL;

static gboolean homeworlds_backend_score_after_move(const HomeworldsPosition *position,
                                                    const HomeworldsMove *move,
                                                    gint *out_score);
static gboolean homeworlds_backend_score_is_inside_prune_window(guint side, gint score, gint best_score);
static gboolean homeworlds_backend_move_buffer_current_cutoff(const HomeworldsMoveBuffer *buffer,
                                                              gint *out_cutoff,
                                                              HomeworldsGoodMoveCutoffKind *out_cutoff_kind);
static gint homeworlds_backend_future_catastrophe_gain_ceiling(const HomeworldsMoveBuilderState *state,
                                                               guint system_index,
                                                               HomeworldsColor color,
                                                               guint side);
static guint homeworlds_backend_collect_profitable_catastrophes(const HomeworldsMoveBuilderState *state,
                                                                HomeworldsProfitableCatastrophe *out_catastrophes,
                                                                guint max_catastrophes);

void homeworlds_backend_set_good_move_trace(HomeworldsGoodMoveTraceFunc trace_func, gpointer user_data) {
  homeworlds_backend_good_move_trace_func = trace_func;
  homeworlds_backend_good_move_trace_user_data = user_data;
}

static const char *homeworlds_backend_side_label(guint side) {
  switch (side) {
    case 0:
      return "Player 1";
    case 1:
      return "Player 2";
    default:
      g_debug("Unsupported Homeworlds side index");
      return "Player";
  }
}

static const char *homeworlds_backend_outcome_banner_text(GameBackendOutcome outcome) {
  switch (outcome) {
    case GAME_BACKEND_OUTCOME_SIDE_0_WIN:
      return "Player 1 wins";
    case GAME_BACKEND_OUTCOME_SIDE_1_WIN:
      return "Player 2 wins";
    case GAME_BACKEND_OUTCOME_DRAW:
      return "Draw";
    case GAME_BACKEND_OUTCOME_ONGOING:
    default:
      return NULL;
  }
}

static SgfColor homeworlds_backend_sgf_color_for_side(guint side) {
  switch (side) {
    case 0:
      return SGF_COLOR_BLACK;
    case 1:
      return SGF_COLOR_WHITE;
    default:
      g_debug("Unsupported Homeworlds side index for SGF color");
      return SGF_COLOR_NONE;
  }
}

static void homeworlds_backend_position_init(gpointer position, const GameBackendVariant * /*variant_or_null*/) {
  HomeworldsPosition *homeworlds_position = position;

  g_return_if_fail(homeworlds_position != NULL);

  homeworlds_position_init(homeworlds_position);
}

static void homeworlds_backend_position_clear(gpointer position) {
  HomeworldsPosition *homeworlds_position = position;

  g_return_if_fail(homeworlds_position != NULL);

  homeworlds_position_clear(homeworlds_position);
}

static void homeworlds_backend_position_copy(gpointer dest, gconstpointer src) {
  HomeworldsPosition *dest_position = dest;
  const HomeworldsPosition *src_position = src;

  g_return_if_fail(dest_position != NULL);
  g_return_if_fail(src_position != NULL);

  homeworlds_position_copy(dest_position, src_position);
}

static GameBackendOutcome homeworlds_backend_position_outcome(gconstpointer position) {
  const HomeworldsPosition *homeworlds_position = position;

  g_return_val_if_fail(homeworlds_position != NULL, GAME_BACKEND_OUTCOME_ONGOING);

  return homeworlds_position_outcome(homeworlds_position);
}

static guint homeworlds_backend_position_turn(gconstpointer position) {
  const HomeworldsPosition *homeworlds_position = position;

  g_return_val_if_fail(homeworlds_position != NULL, 0);

  return homeworlds_position_turn(homeworlds_position);
}

static void homeworlds_backend_move_list_free(GameBackendMoveList *moves) {
  g_return_if_fail(moves != NULL);

  homeworlds_move_list_free(moves);
}

static const void *homeworlds_backend_move_list_get(const GameBackendMoveList *moves, gsize index) {
  g_return_val_if_fail(moves != NULL, NULL);

  return homeworlds_move_list_get(moves, index);
}

static gboolean homeworlds_backend_system_refs_equal(const HomeworldsSystemRef *left,
                                                     const HomeworldsSystemRef *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  if (left->kind != right->kind) {
    return FALSE;
  }

  switch ((HomeworldsSystemRefKind)left->kind) {
    case HOMEWORLDS_SYSTEM_REF_HOMEWORLD:
      return left->homeworld_side == right->homeworld_side;
    case HOMEWORLDS_SYSTEM_REF_SYSTEM:
      return left->system_index == right->system_index && left->star == right->star;
    case HOMEWORLDS_SYSTEM_REF_NONE:
    default:
      return TRUE;
  }
}

static gboolean homeworlds_backend_moves_equal(gconstpointer left, gconstpointer right) {
  const HomeworldsMove *left_move = left;
  const HomeworldsMove *right_move = right;

  g_return_val_if_fail(left_move != NULL, FALSE);
  g_return_val_if_fail(right_move != NULL, FALSE);

  return homeworlds_moves_equal(left_move, right_move);
}

static gboolean homeworlds_backend_moves_equivalent(gconstpointer position,
                                                    gconstpointer left,
                                                    gconstpointer right) {
  const HomeworldsPosition *homeworlds_position = position;
  const HomeworldsMove *left_move = left;
  const HomeworldsMove *right_move = right;
  HomeworldsPosition left_child = {0};
  HomeworldsPosition right_child = {0};
  gboolean equivalent = FALSE;

  g_return_val_if_fail(homeworlds_position != NULL, FALSE);
  g_return_val_if_fail(left_move != NULL, FALSE);
  g_return_val_if_fail(right_move != NULL, FALSE);

  homeworlds_position_copy(&left_child, homeworlds_position);
  if (!homeworlds_position_apply_move(&left_child, left_move)) {
    g_debug("Skipping invalid left Homeworlds move while comparing move equivalence");
    homeworlds_position_clear(&left_child);
    return FALSE;
  }

  homeworlds_position_copy(&right_child, homeworlds_position);
  if (!homeworlds_position_apply_move(&right_child, right_move)) {
    g_debug("Skipping invalid right Homeworlds move while comparing move equivalence");
    homeworlds_position_clear(&left_child);
    homeworlds_position_clear(&right_child);
    return FALSE;
  }

  equivalent = homeworlds_positions_equal(&left_child, &right_child);
  homeworlds_position_clear(&left_child);
  homeworlds_position_clear(&right_child);
  return equivalent;
}

static gboolean homeworlds_backend_profitable_catastrophes_equal(const HomeworldsProfitableCatastrophe *left,
                                                                 const HomeworldsProfitableCatastrophe *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  return left->color == right->color &&
         homeworlds_backend_system_refs_equal(&left->system_ref, &right->system_ref);
}

static gboolean homeworlds_backend_move_has_profitable_catastrophe(
    const HomeworldsMove *move,
    const HomeworldsProfitableCatastrophe *catastrophe) {
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(catastrophe != NULL, FALSE);

  if (move->kind != HOMEWORLDS_MOVE_KIND_TURN) {
    return FALSE;
  }

  for (guint i = 0; i < move->step_count; ++i) {
    const HomeworldsTurnStep *step = &move->steps[i];

    if (step->kind == HOMEWORLDS_STEP_CATASTROPHE &&
        step->target_color == catastrophe->color &&
        homeworlds_backend_system_refs_equal(&step->target_system, &catastrophe->system_ref)) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean homeworlds_backend_move_satisfies_root_catastrophe_requirement(
    const HomeworldsMove *move,
    const HomeworldsGoodMoveContext *context) {
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);

  if (context->root_catastrophe_count == 0) {
    return TRUE;
  }

  for (guint i = 0; i < context->root_catastrophe_count; ++i) {
    if (homeworlds_backend_move_has_profitable_catastrophe(move, &context->root_catastrophes[i])) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean homeworlds_backend_catastrophe_is_root_required(
    const HomeworldsGoodMoveContext *context,
    const HomeworldsProfitableCatastrophe *catastrophe) {
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(catastrophe != NULL, FALSE);

  for (guint i = 0; i < context->root_catastrophe_count; ++i) {
    if (homeworlds_backend_profitable_catastrophes_equal(catastrophe, &context->root_catastrophes[i])) {
      return TRUE;
    }
  }

  return FALSE;
}

static void homeworlds_backend_move_buffer_clear(HomeworldsMoveBuffer *buffer) {
  g_return_if_fail(buffer != NULL);

  g_clear_pointer(&buffer->moves, g_free);
  buffer->position = NULL;
  buffer->count = 0;
  buffer->capacity = 0;
  buffer->leaves_seen = 0;
  buffer->scored_moves = 0;
  buffer->next_original_index = 0;
  buffer->side = 0;
  buffer->best_score = 0;
  buffer->has_best_score = FALSE;
  buffer->prune_by_score = FALSE;
}

static void homeworlds_backend_move_buffer_init(HomeworldsMoveBuffer *buffer, const HomeworldsPosition *position) {
  g_return_if_fail(buffer != NULL);
  g_return_if_fail(position != NULL);

  memset(buffer, 0, sizeof(*buffer));
  buffer->position = position;
  buffer->side = position->turn;
  buffer->prune_by_score = position->phase == HOMEWORLDS_PHASE_PLAY;
}

static gboolean homeworlds_backend_score_is_better(guint side, gint score, gint other_score) {
  g_return_val_if_fail(side < 2, FALSE);

  if (side == 0) {
    return score > other_score;
  }
  return score < other_score;
}

static gboolean homeworlds_backend_score_reaches_cutoff(guint side, gint score, gint cutoff) {
  g_return_val_if_fail(side < 2, FALSE);

  return side == 0 ? score >= cutoff : score <= cutoff;
}

static gboolean homeworlds_backend_score_interval_contains(guint side,
                                                           gint score,
                                                           gint interval_min,
                                                           gint interval_max) {
  g_return_val_if_fail(side < 2, FALSE);

  return score >= interval_min && score <= interval_max;
}

static gint homeworlds_backend_goal_branch_best_bound(guint side, const HomeworldsGoalBranch *branch) {
  g_return_val_if_fail(side < 2, 0);
  g_return_val_if_fail(branch != NULL, 0);

  return side == 0 ? branch->interval_max : branch->interval_min;
}

static gboolean homeworlds_backend_goal_branch_can_reach_cutoff(guint side,
                                                                const HomeworldsGoalBranch *branch,
                                                                gint cutoff) {
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(branch != NULL, FALSE);

  return homeworlds_backend_score_reaches_cutoff(side,
                                                 homeworlds_backend_goal_branch_best_bound(side, branch),
                                                 cutoff);
}

static gboolean homeworlds_backend_goal_bound_is_finite(gint bound) {
  return bound != G_MININT && bound != G_MAXINT;
}

static void homeworlds_backend_format_optional_goal_score(gboolean has_score,
                                                          gint score,
                                                          char *buffer,
                                                          gsize buffer_size) {
  g_return_if_fail(buffer != NULL);
  g_return_if_fail(buffer_size > 0);

  if (!has_score) {
    g_strlcpy(buffer, "none", buffer_size);
    return;
  }

  g_snprintf(buffer, buffer_size, "%d", score);
}

static void homeworlds_backend_format_goal_cutoff(const HomeworldsMoveBuffer *buffer,
                                                  char *buffer_text,
                                                  gsize buffer_text_size) {
  HomeworldsGoodMoveCutoffKind cutoff_kind = HOMEWORLDS_GOOD_MOVE_CUTOFF_NONE;
  gint cutoff = 0;

  g_return_if_fail(buffer != NULL);
  g_return_if_fail(buffer_text != NULL);
  g_return_if_fail(buffer_text_size > 0);

  if (!homeworlds_backend_move_buffer_current_cutoff(buffer, &cutoff, &cutoff_kind)) {
    g_strlcpy(buffer_text, "none", buffer_text_size);
    return;
  }

  switch (cutoff_kind) {
    case HOMEWORLDS_GOOD_MOVE_CUTOFF_SCORE_WINDOW:
      g_snprintf(buffer_text, buffer_text_size, "%d/window", cutoff);
      return;
    case HOMEWORLDS_GOOD_MOVE_CUTOFF_FULL_BUFFER:
      g_snprintf(buffer_text, buffer_text_size, "%d/full", cutoff);
      return;
    case HOMEWORLDS_GOOD_MOVE_CUTOFF_NONE:
    default:
      g_strlcpy(buffer_text, "none", buffer_text_size);
      return;
  }
}

static void homeworlds_backend_goal_branch_format_prefix(const HomeworldsGoalBranch *branch,
                                                         char *buffer,
                                                         gsize buffer_size) {
  g_return_if_fail(branch != NULL);
  g_return_if_fail(buffer != NULL);
  g_return_if_fail(buffer_size > 0);

  if (branch->state.move.kind != HOMEWORLDS_MOVE_KIND_TURN || branch->state.move.step_count == 0) {
    g_strlcpy(buffer, "<root>", buffer_size);
    return;
  }
  if (!homeworlds_move_format(&branch->state.move, buffer, buffer_size)) {
    g_strlcpy(buffer, "<unformatted>", buffer_size);
  }
}

static const char *homeworlds_backend_goal_branch_kind_name(HomeworldsGoalBranchKind kind) {
  switch (kind) {
    case HOMEWORLDS_GOAL_BRANCH_ROOT:
      return "root";
    case HOMEWORLDS_GOAL_BRANCH_ROOT_CATASTROPHE_NOW:
      return "root-cat-now";
    case HOMEWORLDS_GOAL_BRANCH_ROOT_CATASTROPHE_POSTPONE:
      return "root-cat-postpone";
    case HOMEWORLDS_GOAL_BRANCH_SINGLE_STEP:
      return "single-step";
    case HOMEWORLDS_GOAL_BRANCH_SACRIFICE:
      return "sacrifice";
    case HOMEWORLDS_GOAL_BRANCH_YELLOW_SACRIFICE:
      return "yellow-sacrifice";
    case HOMEWORLDS_GOAL_BRANCH_GENERIC:
    default:
      return "generic";
  }
}

static void homeworlds_backend_goal_report_append(HomeworldsGoodMoveContext *context, const char *format, ...) {
  va_list args;

  g_return_if_fail(context != NULL);
  g_return_if_fail(format != NULL);

  if (context->goal_report == NULL) {
    context->goal_report = g_string_new("");
    g_return_if_fail(context->goal_report != NULL);
  }
  if (context->goal_report_lines >= HOMEWORLDS_GOAL_REPORT_MAX_LINES) {
    if (!context->goal_report_truncated) {
      g_string_append(context->goal_report, "... goal tree report truncated ...\n");
      context->goal_report_truncated = TRUE;
    }
    return;
  }

  va_start(args, format);
  g_string_append_vprintf(context->goal_report, format, args);
  va_end(args);
  g_string_append_c(context->goal_report, '\n');
  context->goal_report_lines++;
}

static HomeworldsGoalDedupeRef *homeworlds_backend_goal_dedupe_ref_new(void) {
  HomeworldsGoalDedupeRef *ref = g_new0(HomeworldsGoalDedupeRef, 1);

  g_return_val_if_fail(ref != NULL, NULL);
  ref->ref_count = 1;
  homeworlds_generation_dedupe_init(&ref->dedupe);
  return ref;
}

static HomeworldsGoalDedupeRef *homeworlds_backend_goal_dedupe_ref_ref(HomeworldsGoalDedupeRef *ref) {
  g_return_val_if_fail(ref != NULL, NULL);

  ref->ref_count++;
  return ref;
}

static void homeworlds_backend_goal_dedupe_ref_unref(HomeworldsGoalDedupeRef *ref) {
  if (ref == NULL) {
    return;
  }

  g_return_if_fail(ref->ref_count > 0);
  ref->ref_count--;
  if (ref->ref_count > 0) {
    return;
  }

  homeworlds_generation_dedupe_clear(&ref->dedupe);
  g_free(ref);
}

static HomeworldsGoalBranch *homeworlds_backend_goal_branch_new(HomeworldsGoalQueue *queue,
                                                                HomeworldsGoalBranchKind kind,
                                                                const HomeworldsMoveBuilderState *state,
                                                                const HomeworldsGenerationContext *generation_context,
                                                                HomeworldsGoalDedupeRef *dedupe_ref,
                                                                const char *reason) {
  HomeworldsGoalBranch *branch = NULL;

  g_return_val_if_fail(queue != NULL, NULL);
  g_return_val_if_fail(state != NULL, NULL);
  g_return_val_if_fail(generation_context != NULL, NULL);

  branch = g_new0(HomeworldsGoalBranch, 1);
  g_return_val_if_fail(branch != NULL, NULL);
  branch->id = queue->next_branch_id++;
  branch->kind = kind;
  branch->state = *state;
  branch->generation_context = *generation_context;
  branch->dedupe_ref = dedupe_ref != NULL ? homeworlds_backend_goal_dedupe_ref_ref(dedupe_ref) : NULL;
  branch->interval_min = G_MININT;
  branch->interval_max = G_MAXINT;
  branch->leaf_upper_bound = G_MAXSIZE;
  if (reason != NULL) {
    g_strlcpy(branch->reason, reason, sizeof(branch->reason));
  }
  return branch;
}

static void homeworlds_backend_goal_branch_free(gpointer data) {
  HomeworldsGoalBranch *branch = data;

  if (branch == NULL) {
    return;
  }

  homeworlds_backend_goal_dedupe_ref_unref(branch->dedupe_ref);
  g_free(branch);
}

static void homeworlds_backend_goal_queue_init(HomeworldsGoalQueue *queue) {
  g_return_if_fail(queue != NULL);

  memset(queue, 0, sizeof(*queue));
  queue->branches = g_ptr_array_new_with_free_func(homeworlds_backend_goal_branch_free);
  g_return_if_fail(queue->branches != NULL);
}

static void homeworlds_backend_goal_queue_clear(HomeworldsGoalQueue *queue) {
  g_return_if_fail(queue != NULL);

  g_clear_pointer(&queue->branches, g_ptr_array_unref);
  queue->next_branch_id = 0;
}

static void homeworlds_backend_goal_queue_push(HomeworldsGoalQueue *queue,
                                               HomeworldsGoodMoveContext *context,
                                               HomeworldsGoalBranch *branch) {
  char prefix[256] = {0};

  g_return_if_fail(queue != NULL);
  g_return_if_fail(context != NULL);
  g_return_if_fail(branch != NULL);
  g_return_if_fail(queue->branches != NULL);

  context->goal_branches_created++;
  homeworlds_backend_goal_branch_format_prefix(branch, prefix, sizeof(prefix));
  homeworlds_backend_goal_report_append(context,
                                        "create #%zu %s interval=[%d,%d] parent_delta=[%d,%d] leaves<=%zu "
                                        "prefix=[%s] %s",
                                        branch->id,
                                        homeworlds_backend_goal_branch_kind_name(branch->kind),
                                        branch->interval_min,
                                        branch->interval_max,
                                        branch->parent_delta_min,
                                        branch->parent_delta_max,
                                        branch->leaf_upper_bound,
                                        prefix,
                                        branch->reason);
  g_ptr_array_add(queue->branches, branch);
}

static HomeworldsGoalBranch *homeworlds_backend_goal_branch_clone_for_interval(HomeworldsGoalQueue *queue,
                                                                               const HomeworldsGoalBranch *branch,
                                                                               gint interval_min,
                                                                               gint interval_max,
                                                                               const char *reason) {
  HomeworldsGoalBranch *clone = NULL;

  g_return_val_if_fail(queue != NULL, NULL);
  g_return_val_if_fail(branch != NULL, NULL);
  g_return_val_if_fail(interval_min <= interval_max, NULL);

  clone = homeworlds_backend_goal_branch_new(queue,
                                             branch->kind,
                                             &branch->state,
                                             &branch->generation_context,
                                             NULL,
                                             reason);
  g_return_val_if_fail(clone != NULL, NULL);
  clone->interval_min = interval_min;
  clone->interval_max = interval_max;
  clone->parent_score = branch->parent_score;
  clone->parent_delta_min = interval_min == G_MININT ? G_MININT : interval_min - branch->parent_score;
  clone->parent_delta_max = interval_max == G_MAXINT ? G_MAXINT : interval_max - branch->parent_score;
  clone->leaf_upper_bound = branch->leaf_upper_bound;
  clone->defer_root_catastrophes = branch->defer_root_catastrophes;
  clone->root_defer_step_count = branch->root_defer_step_count;
  clone->allow_pass_move = branch->allow_pass_move;

  if (branch->dedupe_ref != NULL) {
    clone->dedupe_ref = homeworlds_backend_goal_dedupe_ref_new();
    if (clone->dedupe_ref == NULL) {
      homeworlds_backend_goal_branch_free(clone);
      return NULL;
    }
    clone->generation_context.sacrifice_dedupe = &clone->dedupe_ref->dedupe;
  }

  return clone;
}

static guint homeworlds_backend_goal_queue_best_index(const HomeworldsGoalQueue *queue, guint side) {
  guint best_index = 0;

  g_return_val_if_fail(queue != NULL, 0);
  g_return_val_if_fail(queue->branches != NULL, 0);
  g_return_val_if_fail(queue->branches->len > 0, 0);
  g_return_val_if_fail(side < 2, 0);

  for (guint i = 1; i < queue->branches->len; ++i) {
    const HomeworldsGoalBranch *candidate = g_ptr_array_index(queue->branches, i);
    const HomeworldsGoalBranch *best = g_ptr_array_index(queue->branches, best_index);
    gint candidate_bound = homeworlds_backend_goal_branch_best_bound(side, candidate);
    gint best_bound = homeworlds_backend_goal_branch_best_bound(side, best);

    if (candidate_bound == best_bound) {
      if (candidate->id < best->id) {
        best_index = i;
      }
      continue;
    }
    if (homeworlds_backend_score_is_better(side, candidate_bound, best_bound)) {
      best_index = i;
    }
  }
  return best_index;
}

static HomeworldsGoalBranch *homeworlds_backend_goal_queue_pop_best(HomeworldsGoalQueue *queue, guint side) {
  guint best_index = 0;
  HomeworldsGoalBranch *branch = NULL;

  g_return_val_if_fail(queue != NULL, NULL);
  g_return_val_if_fail(queue->branches != NULL, NULL);
  g_return_val_if_fail(queue->branches->len > 0, NULL);
  g_return_val_if_fail(side < 2, NULL);

  best_index = homeworlds_backend_goal_queue_best_index(queue, side);
  branch = g_ptr_array_index(queue->branches, best_index);
  g_ptr_array_steal_index(queue->branches, best_index);
  return branch;
}

static gboolean homeworlds_backend_goal_queue_best_bound(const HomeworldsGoalQueue *queue,
                                                         guint side,
                                                         gint *out_bound) {
  g_return_val_if_fail(queue != NULL, FALSE);
  g_return_val_if_fail(queue->branches != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(out_bound != NULL, FALSE);

  if (queue->branches->len == 0) {
    return FALSE;
  }

  *out_bound = homeworlds_backend_goal_branch_best_bound(
      side,
      g_ptr_array_index(queue->branches, homeworlds_backend_goal_queue_best_index(queue, side)));
  return TRUE;
}

static gint homeworlds_backend_scored_move_order_compare(guint side,
                                                         const HomeworldsScoredMove *left,
                                                         const HomeworldsScoredMove *right) {
  g_return_val_if_fail(side < 2, 0);
  g_return_val_if_fail(left != NULL, 0);
  g_return_val_if_fail(right != NULL, 0);

  if (homeworlds_backend_score_is_better(side, left->score, right->score)) {
    return -1;
  }
  if (homeworlds_backend_score_is_better(side, right->score, left->score)) {
    return 1;
  }
  if (left->original_index < right->original_index) {
    return -1;
  }
  if (left->original_index > right->original_index) {
    return 1;
  }
  return 0;
}

static gboolean homeworlds_backend_move_buffer_reserve_slot(HomeworldsMoveBuffer *buffer) {
  gsize next_capacity = 0;

  g_return_val_if_fail(buffer != NULL, FALSE);

  if (buffer->count < buffer->capacity) {
    return TRUE;
  }

  next_capacity = buffer->capacity == 0 ? 16 : buffer->capacity * 2;
  if (buffer->prune_by_score) {
    next_capacity = MIN(next_capacity, (gsize) HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_LIMIT);
  }
  g_return_val_if_fail(next_capacity > buffer->capacity, FALSE);

  HomeworldsScoredMove *next_moves = g_realloc_n(buffer->moves, next_capacity, sizeof(*next_moves));
  g_return_val_if_fail(next_moves != NULL, FALSE);
  buffer->moves = next_moves;
  buffer->capacity = next_capacity;
  return TRUE;
}

static gboolean homeworlds_backend_move_buffer_current_cutoff(const HomeworldsMoveBuffer *buffer,
                                                              gint *out_cutoff,
                                                              HomeworldsGoodMoveCutoffKind *out_cutoff_kind) {
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(out_cutoff != NULL, FALSE);
  g_return_val_if_fail(out_cutoff_kind != NULL, FALSE);

  *out_cutoff_kind = HOMEWORLDS_GOOD_MOVE_CUTOFF_NONE;
  if (!buffer->prune_by_score) {
    return FALSE;
  }

  if (buffer->count >= HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_LIMIT) {
    *out_cutoff = buffer->moves[buffer->count - 1].score;
    *out_cutoff_kind = HOMEWORLDS_GOOD_MOVE_CUTOFF_FULL_BUFFER;
    return TRUE;
  }

  if (!buffer->has_best_score) {
    return FALSE;
  }

  *out_cutoff = buffer->side == 0
      ? buffer->best_score - HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_WINDOW
      : buffer->best_score + HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_WINDOW;
  *out_cutoff_kind = HOMEWORLDS_GOOD_MOVE_CUTOFF_SCORE_WINDOW;
  return TRUE;
}

static void homeworlds_backend_move_buffer_remove_at(HomeworldsMoveBuffer *buffer, gsize index) {
  g_return_if_fail(buffer != NULL);
  g_return_if_fail(index < buffer->count);

  if (index + 1 < buffer->count) {
    memmove(&buffer->moves[index],
            &buffer->moves[index + 1],
            (buffer->count - index - 1) * sizeof(buffer->moves[0]));
  }
  buffer->count--;
}

static void homeworlds_backend_move_buffer_prune_score_window(HomeworldsMoveBuffer *buffer) {
  gsize i = 0;

  g_return_if_fail(buffer != NULL);

  while (i < buffer->count) {
    if (homeworlds_backend_score_is_inside_prune_window(buffer->side, buffer->moves[i].score, buffer->best_score)) {
      i++;
      continue;
    }
    homeworlds_backend_move_buffer_remove_at(buffer, i);
  }
}

static gboolean homeworlds_backend_move_buffer_insert_scored(HomeworldsMoveBuffer *buffer,
                                                             const HomeworldsScoredMove *scored_move) {
  gsize index = 0;

  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(scored_move != NULL, FALSE);

  if (!homeworlds_backend_move_buffer_reserve_slot(buffer)) {
    return FALSE;
  }

  while (index < buffer->count &&
         homeworlds_backend_scored_move_order_compare(buffer->side, &buffer->moves[index], scored_move) <= 0) {
    index++;
  }
  if (index < buffer->count) {
    memmove(&buffer->moves[index + 1], &buffer->moves[index], (buffer->count - index) * sizeof(buffer->moves[0]));
  }

  buffer->moves[index] = *scored_move;
  buffer->count++;
  return TRUE;
}

static gboolean homeworlds_backend_move_buffer_append_unsorted(HomeworldsMoveBuffer *buffer,
                                                               const HomeworldsMove *move) {
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  if (!homeworlds_backend_move_buffer_reserve_slot(buffer)) {
    return FALSE;
  }

  buffer->moves[buffer->count++] = (HomeworldsScoredMove){
    .move = *move,
    .score = 0,
    .original_index = buffer->next_original_index++,
  };
  return TRUE;
}

static gboolean homeworlds_backend_move_buffer_append_scored(
    HomeworldsMoveBuffer *buffer,
    const HomeworldsMove *move,
    HomeworldsGoodMoveContext *context) {
  HomeworldsScoredMove scored_move = {0};
  gint score = 0;

  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(buffer->position != NULL, FALSE);

  if (!homeworlds_backend_score_after_move(buffer->position, move, &score)) {
    return FALSE;
  }

  buffer->scored_moves++;
  if (context->has_score_interval &&
      !homeworlds_backend_score_interval_contains(buffer->side,
                                                  score,
                                                  context->score_interval_min,
                                                  context->score_interval_max)) {
    context->goal_rejected_score_intervals++;
    return TRUE;
  }

  scored_move = (HomeworldsScoredMove){
    .move = *move,
    .score = score,
    .original_index = buffer->next_original_index++,
  };

  if (!buffer->has_best_score || homeworlds_backend_score_is_better(buffer->side, score, buffer->best_score)) {
    buffer->best_score = score;
    buffer->has_best_score = TRUE;
    homeworlds_backend_move_buffer_prune_score_window(buffer);
  } else if (!homeworlds_backend_score_is_inside_prune_window(buffer->side, score, buffer->best_score)) {
    context->goal_rejected_score_windows++;
    return TRUE;
  }

  if (buffer->count == HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_LIMIT) {
    g_return_val_if_fail(buffer->count > 0, FALSE);
    if (homeworlds_backend_scored_move_order_compare(buffer->side, &scored_move, &buffer->moves[buffer->count - 1]) >=
        0) {
      context->goal_rejected_full_buffer++;
      return TRUE;
    }
    homeworlds_backend_move_buffer_remove_at(buffer, buffer->count - 1);
  }

  return homeworlds_backend_move_buffer_insert_scored(buffer, &scored_move);
}

static gboolean homeworlds_backend_move_buffer_append(
    HomeworldsMoveBuffer *buffer,
    const HomeworldsMove *move,
    HomeworldsGoodMoveContext *context) {
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);

  buffer->leaves_seen++;
  if (buffer->prune_by_score) {
    return homeworlds_backend_move_buffer_append_scored(buffer, move, context);
  }
  return homeworlds_backend_move_buffer_append_unsorted(buffer, move);
}

static HomeworldsMove *homeworlds_backend_move_buffer_copy_moves(const HomeworldsMoveBuffer *buffer) {
  HomeworldsMove *moves = NULL;

  g_return_val_if_fail(buffer != NULL, NULL);

  if (buffer->count == 0) {
    return NULL;
  }

  moves = g_new0(HomeworldsMove, buffer->count);
  for (gsize i = 0; i < buffer->count; ++i) {
    moves[i] = buffer->moves[i].move;
  }
  return moves;
}

static guint homeworlds_backend_setup_star_size_mask(const HomeworldsMove *move) {
  guint mask = 0;

  g_return_val_if_fail(move != NULL, 0);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    HomeworldsPyramid star = move->setup_stars[i];
    if (!homeworlds_pyramid_is_valid(star)) {
      return 0;
    }

    mask |= 1u << (homeworlds_pyramid_size(star) - 1);
  }

  return mask;
}

static guint homeworlds_backend_homeworld_star_size_mask(const HomeworldsPosition *position, guint side) {
  guint mask = 0;

  g_return_val_if_fail(position != NULL, 0);
  g_return_val_if_fail(side < 2, 0);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    HomeworldsPyramid star = position->systems[side].stars[i];
    if (!homeworlds_pyramid_is_valid(star)) {
      return 0;
    }

    mask |= 1u << (homeworlds_pyramid_size(star) - 1);
  }

  return mask;
}

static gboolean homeworlds_backend_setup_colors_are_distinct(const HomeworldsMove *move) {
  gboolean seen_colors[4] = {FALSE};

  g_return_val_if_fail(move != NULL, FALSE);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    HomeworldsPyramid star = move->setup_stars[i];
    if (!homeworlds_pyramid_is_valid(star)) {
      return FALSE;
    }

    HomeworldsColor color = homeworlds_pyramid_color(star);
    if (seen_colors[color]) {
      return FALSE;
    }
    seen_colors[color] = TRUE;
  }

  if (!homeworlds_pyramid_is_valid(move->setup_ship)) {
    return FALSE;
  }

  HomeworldsColor ship_color = homeworlds_pyramid_color(move->setup_ship);
  if (seen_colors[ship_color]) {
    return FALSE;
  }

  return TRUE;
}

static gboolean homeworlds_backend_setup_has_required_colors(const HomeworldsMove *move) {
  gboolean seen_colors[4] = {FALSE};

  g_return_val_if_fail(move != NULL, FALSE);

  for (guint i = 0; i < HOMEWORLDS_STAR_SLOT_COUNT; ++i) {
    HomeworldsPyramid star = move->setup_stars[i];
    if (!homeworlds_pyramid_is_valid(star)) {
      return FALSE;
    }

    seen_colors[homeworlds_pyramid_color(star)] = TRUE;
  }

  if (!homeworlds_pyramid_is_valid(move->setup_ship)) {
    return FALSE;
  }
  seen_colors[homeworlds_pyramid_color(move->setup_ship)] = TRUE;

  return seen_colors[HOMEWORLDS_COLOR_GREEN] &&
         seen_colors[HOMEWORLDS_COLOR_BLUE] &&
         (seen_colors[HOMEWORLDS_COLOR_RED] || seen_colors[HOMEWORLDS_COLOR_YELLOW]);
}

static gboolean homeworlds_backend_setup_move_is_good(const HomeworldsMoveBuilderState *state,
                                                      const HomeworldsMove *move) {
  guint star_size_mask = 0;
  guint side = 0;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  side = state->working_position.turn;
  if (move->kind != HOMEWORLDS_MOVE_KIND_SETUP || side > 1 || !homeworlds_backend_setup_colors_are_distinct(move) ||
      homeworlds_pyramid_size(move->setup_ship) != HOMEWORLDS_SIZE_LARGE) {
    return FALSE;
  }
  if (!homeworlds_backend_setup_has_required_colors(move)) {
    return FALSE;
  }

  star_size_mask = homeworlds_backend_setup_star_size_mask(move);
  if (star_size_mask == 0 || (star_size_mask & (star_size_mask - 1)) == 0) {
    return FALSE;
  }

  if (side == 1 && star_size_mask == homeworlds_backend_homeworld_star_size_mask(&state->working_position, 0)) {
    return FALSE;
  }

  return TRUE;
}

static gboolean homeworlds_backend_position_is_initial_turn(const HomeworldsPosition *position) {
  g_return_val_if_fail(position != NULL, FALSE);

  if (position->phase != HOMEWORLDS_PHASE_PLAY || position->turn != 0 ||
      homeworlds_system_ship_count_for_side(&position->systems[0], 0) != 1 ||
      homeworlds_system_ship_count_for_side(&position->systems[1], 1) != 1) {
    return FALSE;
  }

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &position->systems[system_index];

    if (system_index != 0 && homeworlds_system_has_ships_for_side(system, 0)) {
      return FALSE;
    }
    if (system_index != 1 && homeworlds_system_has_ships_for_side(system, 1)) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean homeworlds_backend_system_has_unfavorable_catastrophe(const HomeworldsMoveBuilderState *state,
                                                                      guint system_index,
                                                                      guint side) {
  const HomeworldsSystem *system = NULL;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);
  g_return_val_if_fail(side < 2, FALSE);

  system = &state->working_position.systems[system_index];
  for (guint color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
    gint gain = 0;

    if (homeworlds_system_color_count(system, (HomeworldsColor) color) < 4) {
      continue;
    }

    gain = homeworlds_backend_future_catastrophe_gain_ceiling(state, system_index, (HomeworldsColor) color, side);
    if (gain != G_MAXINT && gain < 0) {
      return TRUE;
    }
  }

  return FALSE;
}

static guint homeworlds_backend_buildable_color_count_for_side(const HomeworldsPosition *position, guint side) {
  gboolean buildable[HOMEWORLDS_COLOR_BLUE + 1] = {FALSE};
  guint count = 0;

  g_return_val_if_fail(position != NULL, 0);
  g_return_val_if_fail(side < 2, 0);

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &position->systems[system_index];

    if (homeworlds_system_accessible_color_count(system, side, HOMEWORLDS_COLOR_GREEN) == 0) {
      continue;
    }

    for (HomeworldsColor color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
      if (system->ship_color_counts[side][color] > 0) {
        buildable[color] = TRUE;
      }
    }
  }

  for (HomeworldsColor color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
    count += buildable[color];
  }
  return count;
}

static gboolean homeworlds_backend_state_has_active_sacrifice(const HomeworldsMoveBuilderState *state,
                                                              HomeworldsColor color) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, FALSE);

  if (state->pending_actions_remaining == 0 || state->forced_action_color != color ||
      state->move.kind != HOMEWORLDS_MOVE_KIND_TURN) {
    return FALSE;
  }

  for (gint i = (gint)state->move.step_count - 1; i >= 0; --i) {
    const HomeworldsTurnStep *step = &state->move.steps[i];

    if (step->kind != HOMEWORLDS_STEP_SACRIFICE) {
      continue;
    }
    return homeworlds_pyramid_is_valid(step->actor.ship) && homeworlds_pyramid_color(step->actor.ship) == color;
  }

  return FALSE;
}

static gboolean homeworlds_backend_state_has_active_sacrifice_any_color(const HomeworldsMoveBuilderState *state) {
  g_return_val_if_fail(state != NULL, FALSE);

  for (HomeworldsColor color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
    if (homeworlds_backend_state_has_active_sacrifice(state, color)) {
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean homeworlds_backend_system_has_star_size(const HomeworldsSystem *system, HomeworldsSize size) {
  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(size >= HOMEWORLDS_SIZE_SMALL && size <= HOMEWORLDS_SIZE_LARGE, FALSE);

  for (guint slot = 0; slot < HOMEWORLDS_STAR_SLOT_COUNT; ++slot) {
    HomeworldsPyramid star = system->stars[slot];

    if (homeworlds_pyramid_is_valid(star) && homeworlds_pyramid_size(star) == size) {
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean homeworlds_backend_bank_has_bridge_star(const HomeworldsPosition *position,
                                                        const HomeworldsSystem *source,
                                                        const HomeworldsSystem *target) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(source != NULL, FALSE);
  g_return_val_if_fail(target != NULL, FALSE);

  for (guint bank_slot = 0; bank_slot < HOMEWORLDS_BANK_SLOT_COUNT; ++bank_slot) {
    HomeworldsPyramid star = position->bank[bank_slot];

    if (!homeworlds_pyramid_is_valid(star)) {
      continue;
    }

    HomeworldsSize size = homeworlds_pyramid_size(star);
    if (!homeworlds_backend_system_has_star_size(source, size) &&
        !homeworlds_backend_system_has_star_size(target, size)) {
      return TRUE;
    }
  }
  return FALSE;
}

static guint homeworlds_backend_empty_system_count(const HomeworldsPosition *position) {
  guint count = 0;

  g_return_val_if_fail(position != NULL, 0);

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    count += homeworlds_system_is_empty(&position->systems[system_index]);
  }
  return count;
}

static guint homeworlds_backend_yellow_ship_distance_to_system(const HomeworldsPosition *position,
                                                               guint source_index,
                                                               guint target_index,
                                                               guint max_distance) {
  const HomeworldsSystem *source = NULL;
  const HomeworldsSystem *target = NULL;

  g_return_val_if_fail(position != NULL, G_MAXUINT);
  g_return_val_if_fail(source_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, G_MAXUINT);
  g_return_val_if_fail(target_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, G_MAXUINT);

  if (source_index == target_index) {
    return 0;
  }
  if (max_distance == 0) {
    return G_MAXUINT;
  }

  source = &position->systems[source_index];
  target = &position->systems[target_index];
  if (homeworlds_system_is_connected(source, target)) {
    return 1;
  }
  if (max_distance < 2) {
    return G_MAXUINT;
  }

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *intermediate = &position->systems[system_index];

    if (system_index == source_index ||
        system_index == target_index ||
        homeworlds_system_is_empty(intermediate)) {
      continue;
    }
    if (homeworlds_system_is_connected(source, intermediate) &&
        homeworlds_system_is_connected(intermediate, target)) {
      return 2;
    }
  }

  if (homeworlds_backend_empty_system_count(position) > 0 &&
      homeworlds_backend_bank_has_bridge_star(position, source, target)) {
    return 2;
  }
  if (max_distance >= 3 && homeworlds_backend_empty_system_count(position) > 0) {
    return 3;
  }
  return G_MAXUINT;
}

static guint homeworlds_backend_system_star_count_for_color(const HomeworldsSystem *system, HomeworldsColor color) {
  guint count = 0;

  g_return_val_if_fail(system != NULL, 0);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, 0);

  for (guint slot = 0; slot < HOMEWORLDS_STAR_SLOT_COUNT; ++slot) {
    HomeworldsPyramid star = system->stars[slot];

    if (homeworlds_pyramid_is_valid(star) && homeworlds_pyramid_color(star) == color) {
      count++;
    }
  }
  return count;
}

static guint homeworlds_backend_system_star_count(const HomeworldsSystem *system) {
  guint count = 0;

  g_return_val_if_fail(system != NULL, 0);

  for (guint slot = 0; slot < HOMEWORLDS_STAR_SLOT_COUNT; ++slot) {
    count += homeworlds_pyramid_is_valid(system->stars[slot]);
  }
  return count;
}

static guint homeworlds_backend_ship_eval_value(HomeworldsPyramid ship, const HomeworldsEvalWeights *weights) {
  HomeworldsSize size = HOMEWORLDS_SIZE_SMALL;

  g_return_val_if_fail(homeworlds_pyramid_is_valid(ship), 0);
  g_return_val_if_fail(weights != NULL, 0);

  size = homeworlds_pyramid_size(ship);
  g_return_val_if_fail(size >= HOMEWORLDS_SIZE_SMALL && size <= HOMEWORLDS_SIZE_LARGE, 0);
  g_return_val_if_fail(weights->ship_values[size] >= 0, 0);

  return (guint)weights->ship_values[size];
}

static gboolean homeworlds_backend_ship_values_are_nonnegative(const HomeworldsEvalWeights *weights) {
  g_return_val_if_fail(weights != NULL, FALSE);

  for (HomeworldsSize size = HOMEWORLDS_SIZE_SMALL; size <= HOMEWORLDS_SIZE_LARGE; ++size) {
    if (weights->ship_values[size] < 0) {
      return FALSE;
    }
  }
  return TRUE;
}

static guint homeworlds_backend_system_ship_value_for_color(const HomeworldsSystem *system,
                                                            HomeworldsColor color,
                                                            guint side,
                                                            const HomeworldsEvalWeights *weights) {
  guint value = 0;

  g_return_val_if_fail(system != NULL, 0);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, 0);
  g_return_val_if_fail(side < 2, 0);
  g_return_val_if_fail(weights != NULL, 0);

  for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    HomeworldsPyramid ship = system->ships[side][slot];

    if (!homeworlds_pyramid_is_valid(ship)) {
      break;
    }
    if (homeworlds_pyramid_color(ship) == color) {
      value += homeworlds_backend_ship_eval_value(ship, weights);
    }
  }
  return value;
}

static guint homeworlds_backend_system_ship_value_for_side(const HomeworldsSystem *system,
                                                           guint side,
                                                           const HomeworldsEvalWeights *weights) {
  guint value = 0;

  g_return_val_if_fail(system != NULL, 0);
  g_return_val_if_fail(side < 2, 0);
  g_return_val_if_fail(weights != NULL, 0);

  for (guint color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
    value += homeworlds_backend_system_ship_value_for_color(system, (HomeworldsColor) color, side, weights);
  }
  return value;
}

static gint homeworlds_backend_homeworld_star_value_for_count(guint star_count,
                                                              const HomeworldsEvalWeights *weights) {
  g_return_val_if_fail(weights != NULL, 0);

  return star_count == 1 ? weights->single_star_homeworld_penalty : 0;
}

static gint homeworlds_backend_homeworld_star_gain(const HomeworldsSystem *system,
                                                   guint system_index,
                                                   HomeworldsColor color,
                                                   guint side,
                                                   const HomeworldsEvalWeights *weights) {
  guint opponent = 0;
  guint star_count = 0;
  guint color_star_count = 0;
  guint remaining_star_count = 0;
  gint before_value = 0;
  gint after_value = 0;
  gint owner_delta = 0;

  g_return_val_if_fail(system != NULL, 0);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, 0);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, 0);
  g_return_val_if_fail(side < 2, 0);
  g_return_val_if_fail(weights != NULL, 0);

  opponent = side == 0 ? 1 : 0;
  if (system_index != side && system_index != opponent) {
    return 0;
  }

  color_star_count = homeworlds_backend_system_star_count_for_color(system, color);
  if (color_star_count == 0) {
    return 0;
  }

  star_count = homeworlds_backend_system_star_count(system);
  remaining_star_count = star_count - color_star_count;
  if (remaining_star_count == 0) {
    return system_index == opponent ? 1000 : -1000;
  }

  before_value = homeworlds_backend_homeworld_star_value_for_count(star_count, weights);
  after_value = homeworlds_backend_homeworld_star_value_for_count(remaining_star_count, weights);
  owner_delta = after_value - before_value;
  return system_index == side ? owner_delta : -owner_delta;
}

static gboolean homeworlds_backend_catastrophe_removes_all_stars(const HomeworldsSystem *system,
                                                                 HomeworldsColor color) {
  g_return_val_if_fail(system != NULL, FALSE);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, FALSE);

  return homeworlds_backend_system_star_count_for_color(system, color) > 0 &&
         homeworlds_backend_system_star_count(system) == homeworlds_backend_system_star_count_for_color(system, color);
}

/* For a catastrophe we may create later, use the most optimistic material result the future can still arrange. */
static gint homeworlds_backend_future_catastrophe_ship_gain_ceiling(const HomeworldsSystem *system,
                                                                    HomeworldsColor color,
                                                                    guint side,
                                                                    const HomeworldsEvalWeights *weights) {
  guint opponent = 0;
  gint color_gain = 0;
  gint star_removal_gain = 0;

  g_return_val_if_fail(system != NULL, 0);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, 0);
  g_return_val_if_fail(side < 2, 0);
  g_return_val_if_fail(weights != NULL, 0);

  opponent = side == 0 ? 1 : 0;
  color_gain = (gint)homeworlds_backend_system_ship_value_for_color(system, color, opponent, weights) -
               (gint)homeworlds_backend_system_ship_value_for_color(system, color, side, weights);

  if (!homeworlds_backend_catastrophe_removes_all_stars(system, color)) {
    return color_gain;
  }

  star_removal_gain = (gint)homeworlds_backend_system_ship_value_for_side(system, opponent, weights) -
                      (gint)homeworlds_backend_system_ship_value_for_color(system, color, side, weights);
  return star_removal_gain;
}

/* For an existing catastrophe, taking it now also orphans ships when all stars disappear. */
static gint homeworlds_backend_immediate_catastrophe_gain(const HomeworldsMoveBuilderState *state,
                                                          guint system_index,
                                                          HomeworldsColor color,
                                                          guint side) {
  const HomeworldsEvalWeights *weights = NULL;
  const HomeworldsSystem *system = NULL;
  gint gain = 0;

  g_return_val_if_fail(state != NULL, 0);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, 0);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, 0);
  g_return_val_if_fail(side < 2, 0);

  weights = homeworlds_eval_weights_active();
  g_return_val_if_fail(weights != NULL, 0);
  if (!homeworlds_backend_ship_values_are_nonnegative(weights)) {
    return G_MAXINT;
  }
  system = &state->working_position.systems[system_index];
  if (homeworlds_backend_catastrophe_removes_all_stars(system, color)) {
    guint opponent = side == 0 ? 1 : 0;

    gain = (gint)homeworlds_backend_system_ship_value_for_side(system, opponent, weights) -
           (gint)homeworlds_backend_system_ship_value_for_side(system, side, weights);
  } else {
    gain = homeworlds_backend_future_catastrophe_ship_gain_ceiling(system, color, side, weights);
  }

  gain += homeworlds_backend_homeworld_star_gain(system, system_index, color, side, weights);
  return gain;
}

/* This includes tactical value from a future catastrophe, but not the cost of moving ships to create it. */
static gint homeworlds_backend_future_catastrophe_gain_ceiling(const HomeworldsMoveBuilderState *state,
                                                               guint system_index,
                                                               HomeworldsColor color,
                                                               guint side) {
  const HomeworldsEvalWeights *weights = NULL;
  const HomeworldsSystem *system = NULL;
  gint gain = 0;

  g_return_val_if_fail(state != NULL, 0);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, 0);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, 0);
  g_return_val_if_fail(side < 2, 0);

  weights = homeworlds_eval_weights_active();
  g_return_val_if_fail(weights != NULL, 0);
  if (!homeworlds_backend_ship_values_are_nonnegative(weights)) {
    return G_MAXINT;
  }
  system = &state->working_position.systems[system_index];

  gain = homeworlds_backend_future_catastrophe_ship_gain_ceiling(system, color, side, weights);
  gain += homeworlds_backend_homeworld_star_gain(system, system_index, color, side, weights);
  return gain;
}

/* A future catastrophe is relevant only if yellow moves can create it without spending its whole gain on own ships. */
static gboolean homeworlds_backend_yellow_can_make_catastrophe_profitable(
    const HomeworldsMoveBuilderState *state,
    guint target_index,
    HomeworldsColor color,
    guint side,
    guint needed_pyramids,
    guint remaining_actions,
    guint gain_to_beat) {
  guint best_added_value[HOMEWORLDS_SIZE_LARGE + 1][HOMEWORLDS_SIZE_LARGE + 1] = {{0}};
  const HomeworldsEvalWeights *weights = NULL;
  const HomeworldsPosition *position = NULL;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(target_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, FALSE);
  g_return_val_if_fail(side < 2, FALSE);

  if (needed_pyramids == 0) {
    return TRUE;
  }
  if (needed_pyramids > remaining_actions || needed_pyramids > HOMEWORLDS_SIZE_LARGE) {
    return FALSE;
  }
  if (remaining_actions > HOMEWORLDS_SIZE_LARGE) {
    return TRUE;
  }

  for (guint used = 0; used <= needed_pyramids; ++used) {
    for (guint cost = 0; cost <= remaining_actions; ++cost) {
      best_added_value[used][cost] = G_MAXUINT;
    }
  }
  best_added_value[0][0] = 0;
  weights = homeworlds_eval_weights_active();
  g_return_val_if_fail(weights != NULL, TRUE);
  position = &state->working_position;

  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &position->systems[system_index];
    guint distance = G_MAXUINT;

    if (system_index == target_index) {
      continue;
    }
    distance = homeworlds_backend_yellow_ship_distance_to_system(position,
                                                                 system_index,
                                                                 target_index,
                                                                 remaining_actions);
    if (distance == G_MAXUINT || distance > remaining_actions) {
      continue;
    }

    for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
      HomeworldsPyramid ship = system->ships[side][slot];
      guint added_value = 0;
      HomeworldsSize size = HOMEWORLDS_SIZE_SMALL;

      if (!homeworlds_pyramid_is_valid(ship)) {
        break;
      }
      if (homeworlds_pyramid_color(ship) != color) {
        continue;
      }

      size = homeworlds_pyramid_size(ship);
      g_return_val_if_fail(size >= HOMEWORLDS_SIZE_SMALL && size <= HOMEWORLDS_SIZE_LARGE, TRUE);
      if (weights->ship_values[size] < 0) {
        return TRUE;
      }
      added_value = homeworlds_backend_ship_eval_value(ship, weights);
      for (guint used = needed_pyramids; used > 0; --used) {
        for (guint cost = remaining_actions; cost >= distance; --cost) {
          guint previous = best_added_value[used - 1][cost - distance];

          if (previous == G_MAXUINT) {
            continue;
          }
          best_added_value[used][cost] = MIN(best_added_value[used][cost], previous + added_value);
        }
      }
    }
  }

  for (guint cost = needed_pyramids; cost <= remaining_actions; ++cost) {
    if (best_added_value[needed_pyramids][cost] < gain_to_beat) {
      return TRUE;
    }
  }
  return FALSE;
}

/* This is intentionally one-action optimistic: if any legal exit exists, one doomed own ship might be saved. */
static gboolean homeworlds_backend_yellow_action_can_move_ship_out(const HomeworldsPosition *position,
                                                                   guint system_index) {
  const HomeworldsSystem *source = NULL;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);

  source = &position->systems[system_index];
  for (guint target_index = 0; target_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++target_index) {
    const HomeworldsSystem *target = &position->systems[target_index];

    if (target_index == system_index || homeworlds_system_is_empty(target)) {
      continue;
    }
    if (homeworlds_system_is_connected(source, target)) {
      return TRUE;
    }
  }

  for (guint bank_slot = 0; bank_slot < HOMEWORLDS_BANK_SLOT_COUNT; ++bank_slot) {
    HomeworldsSystem discovery = {0};
    HomeworldsPyramid star = position->bank[bank_slot];

    if (!homeworlds_pyramid_is_valid(star)) {
      continue;
    }
    discovery.stars[0] = star;
    if (homeworlds_system_is_connected(source, &discovery)) {
      return TRUE;
    }
  }

  return FALSE;
}

static void homeworlds_backend_insert_descending_value(guint *values,
                                                       guint value_count,
                                                       guint value) {
  g_return_if_fail(values != NULL);

  for (guint i = 0; i < value_count; ++i) {
    if (value <= values[i]) {
      continue;
    }

    for (guint j = value_count - 1; j > i; --j) {
      values[j] = values[j - 1];
    }
    values[i] = value;
    return;
  }
}

/* Own material is avoidable only when remaining yellow actions can move those ships out before the catastrophe. */
static guint homeworlds_backend_avoidable_own_catastrophe_loss(const HomeworldsMoveBuilderState *state,
                                                               guint system_index,
                                                               HomeworldsColor color,
                                                               guint side,
                                                               guint remaining_actions) {
  const HomeworldsEvalWeights *weights = NULL;
  const HomeworldsPosition *position = NULL;
  const HomeworldsSystem *system = NULL;
  gboolean removes_all_stars = FALSE;
  guint saved_values[HOMEWORLDS_SHIP_SLOT_COUNT] = {0};
  guint saved_count = 0;
  guint saved_value = 0;

  g_return_val_if_fail(state != NULL, 0);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, 0);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, 0);
  g_return_val_if_fail(side < 2, 0);

  if (remaining_actions == 0) {
    return 0;
  }

  weights = homeworlds_eval_weights_active();
  g_return_val_if_fail(weights != NULL, 0);
  if (!homeworlds_backend_ship_values_are_nonnegative(weights)) {
    return G_MAXUINT;
  }

  position = &state->working_position;
  if (!homeworlds_backend_yellow_action_can_move_ship_out(position, system_index)) {
    return 0;
  }

  system = &position->systems[system_index];
  removes_all_stars = homeworlds_backend_catastrophe_removes_all_stars(system, color);
  for (guint slot = 0; slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++slot) {
    HomeworldsPyramid ship = system->ships[side][slot];

    if (!homeworlds_pyramid_is_valid(ship)) {
      break;
    }
    if (!removes_all_stars && homeworlds_pyramid_color(ship) != color) {
      continue;
    }

    if (saved_count < G_N_ELEMENTS(saved_values)) {
      saved_count++;
    }
    homeworlds_backend_insert_descending_value(saved_values,
                                               saved_count,
                                               homeworlds_backend_ship_eval_value(ship, weights));
  }

  saved_count = MIN(saved_count, remaining_actions);
  for (guint i = 0; i < saved_count; ++i) {
    saved_value += saved_values[i];
  }
  return saved_value;
}

/* Negative existing catastrophes can become good only by first moving doomed own ships away. */
static guint homeworlds_backend_existing_catastrophe_gain_ceiling(const HomeworldsMoveBuilderState *state,
                                                                  guint system_index,
                                                                  HomeworldsColor color,
                                                                  guint side,
                                                                  guint remaining_actions) {
  gint immediate_gain = 0;
  guint avoidable_loss = 0;

  g_return_val_if_fail(state != NULL, 0);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, 0);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, 0);
  g_return_val_if_fail(side < 2, 0);

  immediate_gain = homeworlds_backend_immediate_catastrophe_gain(state, system_index, color, side);
  if (immediate_gain == G_MAXINT) {
    return G_MAXUINT;
  }
  avoidable_loss = homeworlds_backend_avoidable_own_catastrophe_loss(state,
                                                                     system_index,
                                                                     color,
                                                                     side,
                                                                     remaining_actions);
  if (avoidable_loss == G_MAXUINT) {
    return G_MAXUINT;
  }

  return MAX(0, immediate_gain + (gint)avoidable_loss);
}

/* Sum independent optimistic gains; overestimating is safe because it only makes pruning less eager. */
static guint homeworlds_backend_yellow_sacrifice_catastrophe_gain_ceiling(
    const HomeworldsMoveBuilderState *state) {
  guint remaining_actions = 0;
  guint side = 0;
  guint total_gain = 0;

  g_return_val_if_fail(state != NULL, G_MAXUINT);

  remaining_actions = state->pending_actions_remaining;
  side = state->working_position.turn;
  g_return_val_if_fail(side < 2, G_MAXUINT);
  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &state->working_position.systems[system_index];

    for (HomeworldsColor color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
      guint current_count = homeworlds_system_color_count(system, color);
      guint needed_pyramids = 0;
      gint future_gain = homeworlds_backend_future_catastrophe_gain_ceiling(state, system_index, color, side);
      guint gain = 0;

      if (future_gain == G_MAXINT) {
        return G_MAXUINT;
      }
      if (current_count >= 4) {
        gain = homeworlds_backend_existing_catastrophe_gain_ceiling(state,
                                                                    system_index,
                                                                    color,
                                                                    side,
                                                                    remaining_actions);
        if (gain == G_MAXUINT) {
          return G_MAXUINT;
        }
        total_gain += gain;
        continue;
      }
      if (future_gain <= 0) {
        continue;
      }

      needed_pyramids = 4 - current_count;
      if (homeworlds_backend_yellow_can_make_catastrophe_profitable(state,
                                                                    system_index,
                                                                    color,
                                                                    side,
                                                                    needed_pyramids,
                                                                    remaining_actions,
                                                                    (guint)future_gain)) {
        total_gain += (guint)future_gain;
      }
    }
  }

  return total_gain;
}

gboolean homeworlds_backend_describe_yellow_sacrifice_proof(const HomeworldsMoveBuilderState *state,
                                                            guint side,
                                                            gint cutoff,
                                                            HomeworldsGoodMoveProofStatus *out_status) {
  const HomeworldsEvalWeights *weights = NULL;
  guint buildable_count = 0;
  gint max_gain = 0;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(out_status != NULL, FALSE);

  *out_status = (HomeworldsGoodMoveProofStatus){
    .result = HOMEWORLDS_GOOD_MOVE_PROOF_NOT_ACTIVE,
    .pending_actions_remaining = state->pending_actions_remaining,
    .step_count = state->move.step_count,
    .cutoff = cutoff,
    .current_score = homeworlds_position_evaluate_static(&state->working_position),
  };

  if (!homeworlds_backend_state_has_active_sacrifice(state, HOMEWORLDS_COLOR_YELLOW)) {
    return TRUE;
  }
  if (state->working_position.phase != HOMEWORLDS_PHASE_PLAY) {
    out_status->result = HOMEWORLDS_GOOD_MOVE_PROOF_NOT_PLAY;
    return TRUE;
  }
  if (state->stage == HOMEWORLDS_BUILDER_STAGE_COMPLETE) {
    out_status->result = HOMEWORLDS_GOOD_MOVE_PROOF_COMPLETE;
    return TRUE;
  }

  weights = homeworlds_eval_weights_active();
  g_return_val_if_fail(weights != NULL, FALSE);
  if (weights->buildable_color_value <= 0) {
    out_status->result = HOMEWORLDS_GOOD_MOVE_PROOF_UNSUPPORTED_WEIGHTS;
    return TRUE;
  }

  buildable_count = homeworlds_backend_buildable_color_count_for_side(&state->working_position, side);
  out_status->buildable_gain =
      (gint)(HOMEWORLDS_COLOR_BLUE + 1 - buildable_count) * weights->buildable_color_value;
  out_status->catastrophe_gain = homeworlds_backend_yellow_sacrifice_catastrophe_gain_ceiling(state);
  if (out_status->catastrophe_gain == G_MAXUINT) {
    out_status->result = HOMEWORLDS_GOOD_MOVE_PROOF_UNCERTAIN;
    return TRUE;
  }

  max_gain = out_status->buildable_gain + (gint)out_status->catastrophe_gain;
  out_status->bound = side == 0 ? out_status->current_score + max_gain : out_status->current_score - max_gain;
  out_status->result = side == 0
      ? (out_status->bound < cutoff ? HOMEWORLDS_GOOD_MOVE_PROOF_REJECT : HOMEWORLDS_GOOD_MOVE_PROOF_KEEP)
      : (out_status->bound > cutoff ? HOMEWORLDS_GOOD_MOVE_PROOF_REJECT : HOMEWORLDS_GOOD_MOVE_PROOF_KEEP);
  return TRUE;
}

static gboolean homeworlds_backend_yellow_sacrifice_bound_prunes(
    const HomeworldsMoveBuilderState *state,
    guint side,
    gint cutoff,
    gboolean *out_prune) {
  HomeworldsGoodMoveProofStatus status = {0};

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(out_prune != NULL, FALSE);

  *out_prune = FALSE;
  if (!homeworlds_backend_describe_yellow_sacrifice_proof(state, side, cutoff, &status)) {
    return FALSE;
  }

  *out_prune = status.result == HOMEWORLDS_GOOD_MOVE_PROOF_REJECT;
  return TRUE;
}

static gboolean homeworlds_backend_prepare_pruning_for_child(
    HomeworldsGoodMoveContext *context,
    const HomeworldsMoveBuffer *buffer,
    const HomeworldsMoveBuilderState *child_state,
    gboolean *out_prune_child) {
  gint cutoff = 0;
  HomeworldsGoodMoveCutoffKind cutoff_kind = HOMEWORLDS_GOOD_MOVE_CUTOFF_NONE;
  gboolean prune_child = FALSE;
  gboolean has_cutoff = FALSE;

  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(out_prune_child != NULL, FALSE);

  *out_prune_child = FALSE;
  has_cutoff = homeworlds_backend_move_buffer_current_cutoff(buffer, &cutoff, &cutoff_kind);
  if (context->has_score_interval) {
    gint interval_cutoff = buffer->side == 0 ? context->score_interval_min : context->score_interval_max;

    if (!has_cutoff || homeworlds_backend_score_is_better(buffer->side, interval_cutoff, cutoff)) {
      cutoff = interval_cutoff;
      cutoff_kind = HOMEWORLDS_GOOD_MOVE_CUTOFF_SCORE_WINDOW;
      has_cutoff = TRUE;
    }
  }
  if (!has_cutoff) {
    return TRUE;
  }

  if (!homeworlds_backend_state_has_active_sacrifice(child_state, HOMEWORLDS_COLOR_YELLOW)) {
    return TRUE;
  }

  context->pruning_checked_branches++;
  if (cutoff_kind == HOMEWORLDS_GOOD_MOVE_CUTOFF_SCORE_WINDOW) {
    context->pruning_window_cutoff_branches++;
  }
  if (!homeworlds_backend_yellow_sacrifice_bound_prunes(child_state, buffer->side, cutoff, &prune_child)) {
    return FALSE;
  }
  if (!prune_child) {
    return TRUE;
  }

  context->pruning_pruned_branches++;
  *out_prune_child = TRUE;
  return TRUE;
}

static const HomeworldsTurnStep *homeworlds_backend_appended_step(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state) {
  g_return_val_if_fail(state != NULL, NULL);
  g_return_val_if_fail(child_state != NULL, NULL);

  if (state->move.kind != HOMEWORLDS_MOVE_KIND_TURN ||
      child_state->move.kind != HOMEWORLDS_MOVE_KIND_TURN ||
      child_state->move.step_count != state->move.step_count + 1) {
    return NULL;
  }

  return &child_state->move.steps[child_state->move.step_count - 1];
}

static gboolean homeworlds_backend_resolve_actor_system(const HomeworldsMoveBuilderState *state,
                                                        const HomeworldsTurnStep *step,
                                                        guint *out_system_index) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);
  g_return_val_if_fail(out_system_index != NULL, FALSE);

  return homeworlds_position_resolve_system_ref(&state->working_position, &step->actor.system, out_system_index);
}

static gboolean homeworlds_backend_step_removes_last_homeworld_ship(const HomeworldsMoveBuilderState *state,
                                                                    const HomeworldsTurnStep *step) {
  guint side = 0;
  guint system_index = HOMEWORLDS_INVALID_INDEX;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->kind != HOMEWORLDS_STEP_MOVE &&
      step->kind != HOMEWORLDS_STEP_DISCOVER &&
      step->kind != HOMEWORLDS_STEP_SACRIFICE) {
    return FALSE;
  }
  if (!homeworlds_backend_resolve_actor_system(state, step, &system_index)) {
    return FALSE;
  }

  side = state->working_position.turn;
  return system_index == side &&
         homeworlds_system_ship_count_for_side(&state->working_position.systems[side], side) == 1;
}

static gboolean homeworlds_backend_step_is_redundant_small_sacrifice(const HomeworldsMoveBuilderState *state,
                                                                     const HomeworldsTurnStep *step) {
  guint side = 0;
  guint system_index = HOMEWORLDS_INVALID_INDEX;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->kind != HOMEWORLDS_STEP_SACRIFICE ||
      !homeworlds_pyramid_is_valid(step->actor.ship) ||
      homeworlds_pyramid_size(step->actor.ship) != HOMEWORLDS_SIZE_SMALL ||
      !homeworlds_backend_resolve_actor_system(state, step, &system_index)) {
    return FALSE;
  }

  side = state->working_position.turn;
  return homeworlds_system_has_access_to_color(&state->working_position.systems[system_index],
                                               side,
                                               homeworlds_pyramid_color(step->actor.ship));
}

static gboolean homeworlds_backend_step_creates_unfavorable_build_catastrophe(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state,
    const HomeworldsTurnStep *step) {
  guint side = 0;
  guint system_index = HOMEWORLDS_INVALID_INDEX;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->kind != HOMEWORLDS_STEP_BUILD ||
      !homeworlds_backend_resolve_actor_system(state, step, &system_index)) {
    return FALSE;
  }

  side = state->working_position.turn;
  if (homeworlds_backend_system_has_unfavorable_catastrophe(state, system_index, side)) {
    return FALSE;
  }

  return homeworlds_backend_system_has_unfavorable_catastrophe(child_state, system_index, side);
}

static gboolean homeworlds_backend_step_creates_unfavorable_trade_catastrophe(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state,
    const HomeworldsTurnStep *step) {
  guint side = 0;
  guint system_index = HOMEWORLDS_INVALID_INDEX;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->kind != HOMEWORLDS_STEP_TRADE ||
      !homeworlds_backend_resolve_actor_system(state, step, &system_index)) {
    return FALSE;
  }

  side = state->working_position.turn;
  if (homeworlds_backend_system_has_unfavorable_catastrophe(state, system_index, side)) {
    return FALSE;
  }

  return homeworlds_backend_system_has_unfavorable_catastrophe(child_state, system_index, side);
}

static gboolean homeworlds_backend_step_enters_unfavorable_catastrophe(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state,
    const HomeworldsTurnStep *step) {
  guint side = 0;
  guint target_system_index = HOMEWORLDS_INVALID_INDEX;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(step != NULL, FALSE);

  if (step->kind != HOMEWORLDS_STEP_MOVE && step->kind != HOMEWORLDS_STEP_DISCOVER) {
    return FALSE;
  }
  if (!homeworlds_position_resolve_system_ref(&child_state->working_position,
                                              &step->target_system,
                                              &target_system_index)) {
    return FALSE;
  }

  side = state->working_position.turn;
  return homeworlds_backend_system_has_unfavorable_catastrophe(child_state, target_system_index, side);
}

static gboolean homeworlds_backend_child_state_is_good_after_step(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsMoveBuilderState *child_state) {
  const HomeworldsTurnStep *step = NULL;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);

  step = homeworlds_backend_appended_step(state, child_state);
  if (step == NULL) {
    return TRUE;
  }

  return !homeworlds_backend_step_removes_last_homeworld_ship(state, step) &&
         !homeworlds_backend_step_is_redundant_small_sacrifice(state, step) &&
         !homeworlds_backend_step_creates_unfavorable_build_catastrophe(state, child_state, step) &&
         !homeworlds_backend_step_creates_unfavorable_trade_catastrophe(state, child_state, step) &&
         !homeworlds_backend_step_enters_unfavorable_catastrophe(state, child_state, step);
}

static gboolean homeworlds_backend_move_has_pass(const HomeworldsMove *move) {
  g_return_val_if_fail(move != NULL, FALSE);

  if (move->kind != HOMEWORLDS_MOVE_KIND_TURN) {
    return FALSE;
  }

  for (guint i = 0; i < move->step_count; ++i) {
    if (move->steps[i].kind == HOMEWORLDS_STEP_PASS) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean homeworlds_backend_move_has_sacrifice(const HomeworldsMove *move) {
  g_return_val_if_fail(move != NULL, FALSE);

  if (move->kind != HOMEWORLDS_MOVE_KIND_TURN) {
    return FALSE;
  }

  for (guint i = 0; i < move->step_count; ++i) {
    if (move->steps[i].kind == HOMEWORLDS_STEP_SACRIFICE) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean homeworlds_backend_move_is_good(const HomeworldsMoveBuilderState *state,
                                                const HomeworldsMove *move,
                                                gboolean allow_pass) {
  gboolean move_has_pass = FALSE;
  gboolean move_has_sacrifice = FALSE;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  if (move->kind == HOMEWORLDS_MOVE_KIND_SETUP) {
    return homeworlds_backend_setup_move_is_good(state, move);
  }

  move_has_pass = homeworlds_backend_move_has_pass(move);
  move_has_sacrifice = homeworlds_backend_move_has_sacrifice(move);
  if (homeworlds_backend_position_is_initial_turn(&state->working_position) &&
      !(allow_pass && move_has_pass) &&
      (move->step_count != 1 || move->steps[0].kind != HOMEWORLDS_STEP_BUILD)) {
    return FALSE;
  }

  if (move_has_pass && !allow_pass && !move_has_sacrifice) {
    return FALSE;
  }

  return TRUE;
}

static gboolean homeworlds_backend_candidate_is_pass(const HomeworldsMoveCandidate *candidate) {
  g_return_val_if_fail(candidate != NULL, FALSE);

  return candidate->data.kind == HOMEWORLDS_CANDIDATE_ACTION &&
         candidate->data.target_color == HOMEWORLDS_STEP_PASS;
}

static gint homeworlds_backend_candidate_order_compare_priority(guint side,
                                                                const HomeworldsCandidateOrder *left,
                                                                const HomeworldsCandidateOrder *right) {
  g_return_val_if_fail(side < 2, 0);
  g_return_val_if_fail(left != NULL, 0);
  g_return_val_if_fail(right != NULL, 0);

  if (left->is_pass != right->is_pass) {
    return left->is_pass ? 1 : -1;
  }
  if (left->has_priority != right->has_priority) {
    return left->has_priority ? -1 : 1;
  }
  if (left->has_priority) {
    if (left->bound != right->bound) {
      if (side == 0) {
        return left->bound > right->bound ? -1 : 1;
      }
      return left->bound < right->bound ? -1 : 1;
    }
    if (left->catastrophe_gain != right->catastrophe_gain) {
      return left->catastrophe_gain > right->catastrophe_gain ? -1 : 1;
    }
    if (left->buildable_gain != right->buildable_gain) {
      return left->buildable_gain > right->buildable_gain ? -1 : 1;
    }
    if (left->current_score != right->current_score) {
      if (side == 0) {
        return left->current_score > right->current_score ? -1 : 1;
      }
      return left->current_score < right->current_score ? -1 : 1;
    }
  }

  if (left->index < right->index) {
    return -1;
  }
  if (left->index > right->index) {
    return 1;
  }
  return 0;
}

static gint homeworlds_backend_candidate_order_compare(gconstpointer left,
                                                       gconstpointer right,
                                                       gpointer user_data) {
  guint side = GPOINTER_TO_UINT(user_data);

  return homeworlds_backend_candidate_order_compare_priority(side, left, right);
}

static gboolean homeworlds_backend_candidate_order_is_better(guint side,
                                                             const HomeworldsCandidateOrder *candidate,
                                                             const HomeworldsCandidateOrder *best) {
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(candidate != NULL, FALSE);
  g_return_val_if_fail(best != NULL, FALSE);

  if (!candidate->has_priority) {
    return FALSE;
  }
  if (!best->has_priority) {
    return TRUE;
  }
  return homeworlds_backend_candidate_order_compare_priority(side, candidate, best) < 0;
}

static gboolean homeworlds_backend_candidate_order_set_complete_score(const HomeworldsPosition *root_position,
                                                                      const HomeworldsMoveBuilderState *state,
                                                                      guint side,
                                                                      HomeworldsCandidateOrder *out_order) {
  GameBackendMoveBuilder builder = {
    .builder_state = (gpointer)state,
    .builder_state_size = sizeof(*state),
  };
  HomeworldsMove move = {0};
  gint score = 0;

  g_return_val_if_fail(root_position != NULL, FALSE);
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(out_order != NULL, FALSE);

  if (!homeworlds_move_builder_is_complete(&builder) ||
      !homeworlds_move_builder_build_move(&builder, &move) ||
      !homeworlds_backend_score_after_move(root_position, &move, &score)) {
    return FALSE;
  }

  out_order->has_priority = TRUE;
  out_order->bound = score;
  out_order->current_score = score;
  out_order->buildable_gain = 0;
  out_order->catastrophe_gain = 0;
  return TRUE;
}

static gboolean homeworlds_backend_candidate_order_set_proof_bound(const HomeworldsMoveBuilderState *state,
                                                                   guint side,
                                                                   HomeworldsCandidateOrder *out_order) {
  HomeworldsGoodMoveProofStatus status = {0};

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(out_order != NULL, FALSE);

  if (!homeworlds_backend_describe_yellow_sacrifice_proof(state, side, 0, &status)) {
    return FALSE;
  }

  switch (status.result) {
    case HOMEWORLDS_GOOD_MOVE_PROOF_KEEP:
    case HOMEWORLDS_GOOD_MOVE_PROOF_REJECT:
      out_order->has_priority = TRUE;
      out_order->bound = status.bound;
      out_order->current_score = status.current_score;
      out_order->buildable_gain = status.buildable_gain;
      out_order->catastrophe_gain = status.catastrophe_gain;
      return TRUE;
    case HOMEWORLDS_GOOD_MOVE_PROOF_UNCERTAIN:
      out_order->has_priority = TRUE;
      out_order->bound = side == 0 ? G_MAXINT : G_MININT;
      out_order->current_score = status.current_score;
      out_order->buildable_gain = status.buildable_gain;
      out_order->catastrophe_gain = G_MAXUINT;
      return TRUE;
    case HOMEWORLDS_GOOD_MOVE_PROOF_NOT_ACTIVE:
    case HOMEWORLDS_GOOD_MOVE_PROOF_NOT_PLAY:
    case HOMEWORLDS_GOOD_MOVE_PROOF_COMPLETE:
    case HOMEWORLDS_GOOD_MOVE_PROOF_UNSUPPORTED_WEIGHTS:
    default:
      return FALSE;
  }
}

static gboolean homeworlds_backend_candidate_order_find_priority(const HomeworldsPosition *root_position,
                                                                 const HomeworldsMoveBuilderState *state,
                                                                 guint side,
                                                                 guint base_step_count,
                                                                 guint depth_remaining,
                                                                 HomeworldsCandidateOrder *out_order) {
  GameBackendMoveBuilder builder = {
    .builder_state = (gpointer)state,
    .builder_state_size = sizeof(*state),
  };
  GameBackendMoveList candidates = {0};
  HomeworldsCandidateOrder best = {0};

  g_return_val_if_fail(root_position != NULL, FALSE);
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(out_order != NULL, FALSE);

  *out_order = (HomeworldsCandidateOrder){0};
  if (homeworlds_backend_candidate_order_set_complete_score(root_position, state, side, out_order)) {
    return TRUE;
  }
  if (state->move.kind == HOMEWORLDS_MOVE_KIND_TURN &&
      state->move.step_count > base_step_count &&
      homeworlds_backend_candidate_order_set_proof_bound(state, side, out_order)) {
    return TRUE;
  }
  if (depth_remaining == 0 || homeworlds_move_builder_is_complete(&builder)) {
    return TRUE;
  }

  candidates = homeworlds_move_builder_list_candidates(&builder);
  for (gsize i = 0; i < candidates.count; ++i) {
    const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *)candidates.moves)[i];
    HomeworldsMoveBuilderState child_state = *state;
    GameBackendMoveBuilder child = {
      .builder_state = &child_state,
      .builder_state_size = sizeof(child_state),
    };
    HomeworldsCandidateOrder candidate_order = {
      .index = i,
      .is_pass = candidate != NULL && homeworlds_backend_candidate_is_pass(candidate),
    };

    if (candidate == NULL ||
        candidate_order.is_pass ||
        !homeworlds_move_builder_step(&child, candidate)) {
      continue;
    }
    if (!homeworlds_backend_candidate_order_find_priority(root_position,
                                                          &child_state,
                                                          side,
                                                          base_step_count,
                                                          depth_remaining - 1,
                                                          &candidate_order)) {
      homeworlds_backend_move_list_free(&candidates);
      return FALSE;
    }
    if (homeworlds_backend_candidate_order_is_better(side, &candidate_order, &best)) {
      best = candidate_order;
    }
  }

  homeworlds_backend_move_list_free(&candidates);
  *out_order = best;
  return TRUE;
}

static gsize homeworlds_backend_saturating_add(gsize left, gsize right) {
  if (left > G_MAXSIZE - right) {
    return G_MAXSIZE;
  }
  return left + right;
}

static guint homeworlds_backend_bank_pyramid_count(const HomeworldsPosition *position, HomeworldsPyramid pyramid) {
  guint count = 0;

  g_return_val_if_fail(position != NULL, 0);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), 0);

  for (guint bank_slot = 0; bank_slot < HOMEWORLDS_BANK_SLOT_COUNT; ++bank_slot) {
    if (position->bank[bank_slot] == pyramid) {
      count++;
    }
  }
  return count;
}

static guint homeworlds_backend_bank_color_count(const HomeworldsPosition *position, HomeworldsColor color) {
  guint count = 0;

  g_return_val_if_fail(position != NULL, 0);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, 0);

  for (HomeworldsSize size = HOMEWORLDS_SIZE_SMALL; size <= HOMEWORLDS_SIZE_LARGE; size++) {
    count += homeworlds_backend_bank_pyramid_count(position, homeworlds_pyramid_make(color, size));
  }
  return count;
}

static guint homeworlds_backend_forced_action_color_bank_count(const HomeworldsPosition *position,
                                                               HomeworldsColor color) {
  g_return_val_if_fail(position != NULL, 0);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, 0);

  return homeworlds_backend_bank_color_count(position, color);
}

static guint homeworlds_backend_forced_action_pyramid_bank_count(const HomeworldsPosition *position,
                                                                 HomeworldsPyramid pyramid) {
  g_return_val_if_fail(position != NULL, 0);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), 0);

  return homeworlds_backend_bank_pyramid_count(position, pyramid);
}

static gsize homeworlds_backend_saturating_permutation(gsize choices, guint length) {
  gsize result = 1;

  if (choices < length) {
    return 0;
  }

  for (guint i = 0; i < length; ++i) {
    gsize factor = choices - i;

    if (factor != 0 && result > G_MAXSIZE / factor) {
      return G_MAXSIZE;
    }
    result *= factor;
  }
  return result;
}

static gsize homeworlds_backend_count_current_positive_catastrophe_steps(
    const HomeworldsMoveBuilderState *state) {
  HomeworldsProfitableCatastrophe catastrophes[HOMEWORLDS_SYSTEM_SLOT_COUNT * 4] = {0};

  g_return_val_if_fail(state != NULL, 0);

  return homeworlds_backend_collect_profitable_catastrophes(state, catastrophes, G_N_ELEMENTS(catastrophes));
}

static gboolean homeworlds_backend_future_catastrophe_can_be_positive(const HomeworldsMoveBuilderState *state,
                                                                      guint system_index,
                                                                      HomeworldsColor color,
                                                                      guint side) {
  gint gain = 0;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, FALSE);
  g_return_val_if_fail(side < 2, FALSE);

  gain = homeworlds_backend_future_catastrophe_gain_ceiling(state, system_index, color, side);
  return gain == G_MAXINT || gain > 0;
}

static guint homeworlds_backend_forced_green_build_count_for_system(
    const HomeworldsMoveBuilderState *state,
    guint system_index,
    HomeworldsColor color) {
  const HomeworldsSystem *system = NULL;

  g_return_val_if_fail(state != NULL, 0);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, 0);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, 0);

  system = &state->working_position.systems[system_index];
  if (system->ship_color_counts[state->working_position.turn][color] == 0) {
    return 0;
  }
  return homeworlds_backend_forced_action_color_bank_count(&state->working_position, color);
}

static gsize homeworlds_backend_count_forced_green_build_steps(const HomeworldsMoveBuilderState *state) {
  gsize choices = 0;
  guint side = 0;

  g_return_val_if_fail(state != NULL, 0);

  side = state->working_position.turn;
  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &state->working_position.systems[system_index];

    for (HomeworldsColor color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; color++) {
      guint count = 0;

      if (system->ship_color_counts[side][color] == 0) {
        continue;
      }
      count = homeworlds_backend_forced_green_build_count_for_system(state,
                                                                     system_index,
                                                                     color);
      choices = homeworlds_backend_saturating_add(choices, count);
      if (choices == G_MAXSIZE) {
        return G_MAXSIZE;
      }
    }
  }
  return choices;
}

static gsize homeworlds_backend_count_green_build_catastrophe_steps(
    const HomeworldsMoveBuilderState *state) {
  gsize count = 0;
  guint side = 0;

  g_return_val_if_fail(state != NULL, 0);

  side = state->working_position.turn;
  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &state->working_position.systems[system_index];

    for (HomeworldsColor color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; color++) {
      guint current_count = homeworlds_system_color_count(system, color);
      guint needed_builds = 0;
      guint build_count = 0;

      if (current_count >= 4 || current_count == 0) {
        continue;
      }
      needed_builds = 4 - current_count;
      if (needed_builds > state->pending_actions_remaining ||
          !homeworlds_backend_future_catastrophe_can_be_positive(state, system_index, color, side)) {
        continue;
      }
      build_count = homeworlds_backend_forced_green_build_count_for_system(state,
                                                                           system_index,
                                                                           color);
      if (build_count >= needed_builds) {
        count++;
      }
    }
  }
  return count;
}

static gsize homeworlds_backend_count_forced_red_attack_steps(const HomeworldsMoveBuilderState *state) {
  gsize choices = 0;
  guint side = 0;
  guint opponent = 0;

  g_return_val_if_fail(state != NULL, 0);

  side = state->working_position.turn;
  opponent = side == 0 ? 1 : 0;
  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &state->working_position.systems[system_index];

    for (guint attacker_slot = 0; attacker_slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++attacker_slot) {
      HomeworldsPyramid attacker = system->ships[side][attacker_slot];

      if (!homeworlds_pyramid_is_valid(attacker)) {
        break;
      }
      for (guint target_slot = 0; target_slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++target_slot) {
        HomeworldsPyramid target = system->ships[opponent][target_slot];

        if (!homeworlds_pyramid_is_valid(target)) {
          break;
        }
        if (homeworlds_pyramid_size(attacker) < homeworlds_pyramid_size(target)) {
          continue;
        }
        choices = homeworlds_backend_saturating_add(choices, 1);
        if (choices == G_MAXSIZE) {
          return G_MAXSIZE;
        }
      }
    }
  }
  return choices;
}

static guint homeworlds_backend_forced_blue_trade_count_for_ship(
    const HomeworldsMoveBuilderState *state,
    HomeworldsPyramid ship,
    HomeworldsColor target_color) {
  HomeworldsPyramid target = 0;

  g_return_val_if_fail(state != NULL, 0);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(ship), 0);
  g_return_val_if_fail(target_color <= HOMEWORLDS_COLOR_BLUE, 0);

  if (homeworlds_pyramid_color(ship) == target_color) {
    return 0;
  }

  target = homeworlds_pyramid_make(target_color, homeworlds_pyramid_size(ship));
  return homeworlds_backend_forced_action_pyramid_bank_count(&state->working_position, target);
}

static gsize homeworlds_backend_count_forced_blue_trade_steps(const HomeworldsMoveBuilderState *state) {
  gsize choices = 0;
  guint side = 0;

  g_return_val_if_fail(state != NULL, 0);

  side = state->working_position.turn;
  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &state->working_position.systems[system_index];

    for (guint ship_slot = 0; ship_slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++ship_slot) {
      HomeworldsPyramid ship = system->ships[side][ship_slot];

      if (!homeworlds_pyramid_is_valid(ship)) {
        break;
      }
      for (HomeworldsColor target_color = HOMEWORLDS_COLOR_RED; target_color <= HOMEWORLDS_COLOR_BLUE; target_color++) {
        guint count = homeworlds_backend_forced_blue_trade_count_for_ship(state,
                                                                          ship,
                                                                          target_color);

        choices = homeworlds_backend_saturating_add(choices, count);
        if (choices == G_MAXSIZE) {
          return G_MAXSIZE;
        }
      }
    }
  }
  return choices;
}

static guint homeworlds_backend_forced_blue_trade_count_for_system_color(
    const HomeworldsMoveBuilderState *state,
    guint system_index,
    HomeworldsColor target_color) {
  const HomeworldsSystem *system = NULL;
  guint count = 0;
  guint side = 0;

  g_return_val_if_fail(state != NULL, 0);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, 0);
  g_return_val_if_fail(target_color <= HOMEWORLDS_COLOR_BLUE, 0);

  side = state->working_position.turn;
  system = &state->working_position.systems[system_index];
  for (guint ship_slot = 0; ship_slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++ship_slot) {
    HomeworldsPyramid ship = system->ships[side][ship_slot];

    if (!homeworlds_pyramid_is_valid(ship)) {
      break;
    }
    count += homeworlds_backend_forced_blue_trade_count_for_ship(state,
                                                                 ship,
                                                                 target_color);
  }
  return count;
}

static gsize homeworlds_backend_count_blue_trade_catastrophe_steps(const HomeworldsMoveBuilderState *state) {
  gsize count = 0;
  guint side = 0;

  g_return_val_if_fail(state != NULL, 0);

  side = state->working_position.turn;
  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &state->working_position.systems[system_index];

    for (HomeworldsColor color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; color++) {
      guint current_count = homeworlds_system_color_count(system, color);
      guint needed_trades = 0;
      guint trade_count = 0;

      if (current_count >= 4 || current_count == 0) {
        continue;
      }
      needed_trades = 4 - current_count;
      if (needed_trades > state->pending_actions_remaining ||
          !homeworlds_backend_future_catastrophe_can_be_positive(state, system_index, color, side)) {
        continue;
      }
      trade_count = homeworlds_backend_forced_blue_trade_count_for_system_color(state,
                                                                                system_index,
                                                                                color);
      if (trade_count >= needed_trades) {
        count++;
      }
    }
  }
  return count;
}

static gboolean homeworlds_backend_discovery_star_is_connected(const HomeworldsSystem *source,
                                                               HomeworldsPyramid star) {
  HomeworldsSystem discovery = {0};

  g_return_val_if_fail(source != NULL, FALSE);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(star), FALSE);

  discovery.stars[0] = star;
  return homeworlds_system_is_connected(source, &discovery);
}

static gsize homeworlds_backend_count_yellow_discoveries_from_system(const HomeworldsMoveBuilderState *state,
                                                                     guint source_index) {
  const HomeworldsSystem *source = NULL;
  gsize count = 0;
  gboolean seen_discovery_stars[13] = {FALSE};

  g_return_val_if_fail(state != NULL, 0);
  g_return_val_if_fail(source_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, 0);

  source = &state->working_position.systems[source_index];
  for (guint bank_slot = 0; bank_slot < HOMEWORLDS_BANK_SLOT_COUNT; ++bank_slot) {
    HomeworldsPyramid star = state->working_position.bank[bank_slot];

    if (!homeworlds_pyramid_is_valid(star) || seen_discovery_stars[star]) {
      continue;
    }
    seen_discovery_stars[star] = TRUE;
    if (homeworlds_backend_discovery_star_is_connected(source, star)) {
      count++;
    }
  }
  return count;
}

static gsize homeworlds_backend_count_forced_yellow_move_steps(const HomeworldsMoveBuilderState *state) {
  gsize choices = 0;
  guint side = 0;

  g_return_val_if_fail(state != NULL, 0);

  side = state->working_position.turn;
  for (guint source_index = 0; source_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++source_index) {
    const HomeworldsSystem *source = &state->working_position.systems[source_index];
    gsize discovery_count = 0;
    guint connected_system_count = 0;

    if (homeworlds_system_is_empty(source)) {
      continue;
    }

    for (guint target_index = 0; target_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++target_index) {
      const HomeworldsSystem *target = &state->working_position.systems[target_index];

      if (target_index == source_index || homeworlds_system_is_empty(target)) {
        continue;
      }
      if (homeworlds_system_is_connected(source, target)) {
        connected_system_count++;
      }
    }
    discovery_count =
        homeworlds_backend_count_yellow_discoveries_from_system(state, source_index);

    for (guint ship_slot = 0; ship_slot < HOMEWORLDS_SHIP_SLOT_COUNT; ++ship_slot) {
      HomeworldsPyramid ship = source->ships[side][ship_slot];
      gsize ship_choices = 0;

      if (!homeworlds_pyramid_is_valid(ship)) {
        break;
      }

      ship_choices = homeworlds_backend_saturating_add(connected_system_count, discovery_count);
      choices = homeworlds_backend_saturating_add(choices, ship_choices);
      if (choices == G_MAXSIZE) {
        return G_MAXSIZE;
      }
    }
  }
  return choices;
}

static gsize homeworlds_backend_count_yellow_move_catastrophe_steps(const HomeworldsMoveBuilderState *state) {
  gsize count = 0;
  guint side = 0;

  g_return_val_if_fail(state != NULL, 0);

  side = state->working_position.turn;
  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &state->working_position.systems[system_index];

    for (HomeworldsColor color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; color++) {
      guint current_count = homeworlds_system_color_count(system, color);
      guint needed_pyramids = 0;

      if (current_count >= 4 || current_count == 0) {
        continue;
      }
      needed_pyramids = 4 - current_count;
      if (needed_pyramids > state->pending_actions_remaining ||
          !homeworlds_backend_future_catastrophe_can_be_positive(state, system_index, color, side)) {
        continue;
      }
      if (homeworlds_backend_yellow_can_make_catastrophe_profitable(state,
                                                                    system_index,
                                                                    color,
                                                                    side,
                                                                    needed_pyramids,
                                                                    state->pending_actions_remaining,
                                                                    G_MAXUINT)) {
        count++;
      }
    }
  }
  return count;
}

static gsize homeworlds_backend_count_forced_sacrifice_steps(const HomeworldsMoveBuilderState *state,
                                                             gboolean /*allow_catastrophe_recycling*/) {
  g_return_val_if_fail(state != NULL, G_MAXSIZE);

  switch ((HomeworldsColor) state->forced_action_color) {
    case HOMEWORLDS_COLOR_GREEN:
      return homeworlds_backend_count_forced_green_build_steps(state);
    case HOMEWORLDS_COLOR_RED:
      return homeworlds_backend_count_forced_red_attack_steps(state);
    case HOMEWORLDS_COLOR_BLUE:
      return homeworlds_backend_count_forced_blue_trade_steps(state);
    case HOMEWORLDS_COLOR_YELLOW:
      return homeworlds_backend_count_forced_yellow_move_steps(state);
    default:
      return G_MAXSIZE;
  }
}

static gsize homeworlds_backend_count_reachable_positive_catastrophe_steps(
    const HomeworldsMoveBuilderState *state,
    gboolean /*allow_catastrophe_recycling*/) {
  gsize count = 0;

  g_return_val_if_fail(state != NULL, 0);

  count = homeworlds_backend_count_current_positive_catastrophe_steps(state);
  switch ((HomeworldsColor) state->forced_action_color) {
    case HOMEWORLDS_COLOR_GREEN:
      return homeworlds_backend_saturating_add(
          count,
          homeworlds_backend_count_green_build_catastrophe_steps(state));
    case HOMEWORLDS_COLOR_BLUE:
      return homeworlds_backend_saturating_add(
          count,
          homeworlds_backend_count_blue_trade_catastrophe_steps(state));
    case HOMEWORLDS_COLOR_RED:
      return count;
    case HOMEWORLDS_COLOR_YELLOW:
      return homeworlds_backend_saturating_add(count, homeworlds_backend_count_yellow_move_catastrophe_steps(state));
    default:
      return G_MAXSIZE;
  }
}

static gsize homeworlds_backend_estimate_forced_sacrifice_leaf_upper_bound(
    const HomeworldsMoveBuilderState *state) {
  gsize catastrophe_steps = 0;
  gsize forced_steps = 0;
  gsize choices = 0;

  g_return_val_if_fail(state != NULL, G_MAXSIZE);

  if (state->working_position.phase != HOMEWORLDS_PHASE_PLAY ||
      state->pending_actions_remaining == 0 ||
      state->forced_action_color > HOMEWORLDS_COLOR_BLUE) {
    return G_MAXSIZE;
  }

  catastrophe_steps = homeworlds_backend_count_reachable_positive_catastrophe_steps(state, FALSE);
  forced_steps = homeworlds_backend_count_forced_sacrifice_steps(state, FALSE);
  choices = homeworlds_backend_saturating_add(forced_steps, catastrophe_steps);
  return homeworlds_backend_saturating_permutation(choices, state->pending_actions_remaining);
}

static gboolean homeworlds_backend_build_candidate_order(const HomeworldsPosition *root_position,
                                                         const HomeworldsMoveBuilderState *state,
                                                         HomeworldsGoodMoveContext *context,
                                                         const GameBackendMoveList *candidates,
                                                         HomeworldsCandidateOrder **out_order) {
  HomeworldsCandidateOrder *order = NULL;
  gboolean should_order = FALSE;
  gsize moved_count = 0;

  g_return_val_if_fail(root_position != NULL, FALSE);
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(candidates != NULL, FALSE);
  g_return_val_if_fail(out_order != NULL, FALSE);

  *out_order = NULL;
  if (candidates->count == 0) {
    return TRUE;
  }

  order = g_new0(HomeworldsCandidateOrder, candidates->count);
  g_return_val_if_fail(order != NULL, FALSE);
  should_order = candidates->count > 1 &&
                 homeworlds_backend_state_has_active_sacrifice(state, HOMEWORLDS_COLOR_YELLOW);
  if (should_order) {
    context->ordering_candidate_lists++;
  }

  for (gsize i = 0; i < candidates->count; ++i) {
    const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *)candidates->moves)[i];

    order[i] = (HomeworldsCandidateOrder){
      .index = i,
      .is_pass = candidate != NULL && homeworlds_backend_candidate_is_pass(candidate),
    };
    if (!should_order || candidate == NULL || order[i].is_pass) {
      continue;
    }

    HomeworldsMoveBuilderState child_state = *state;
    GameBackendMoveBuilder child = {
      .builder_state = &child_state,
      .builder_state_size = sizeof(child_state),
    };

    if (!homeworlds_move_builder_step(&child, candidate)) {
      continue;
    }
    if (!homeworlds_backend_candidate_order_find_priority(root_position,
                                                          &child_state,
                                                          state->working_position.turn,
                                                          state->move.step_count,
                                                          4,
                                                          &order[i])) {
      g_free(order);
      return FALSE;
    }
    order[i].index = i;
    order[i].is_pass = FALSE;
  }

  if (should_order) {
    g_qsort_with_data(order,
                      candidates->count,
                      sizeof(order[0]),
                      homeworlds_backend_candidate_order_compare,
                      GUINT_TO_POINTER(state->working_position.turn));
    for (gsize i = 0; i < candidates->count; ++i) {
      if (!order[i].is_pass && order[i].index != i) {
        moved_count++;
      }
    }
    if (moved_count > 0) {
      context->ordering_reordered_candidate_lists++;
      context->ordering_reordered_candidates += moved_count;
    }
  }

  *out_order = order;
  return TRUE;
}

static gsize homeworlds_backend_goal_estimate_leaf_upper_bound(const HomeworldsMoveBuilderState *state) {
  GameBackendMoveBuilder builder = {
    .builder_state = (gpointer) state,
    .builder_state_size = sizeof(*state),
  };
  gsize forced_sacrifice_bound = G_MAXSIZE;

  g_return_val_if_fail(state != NULL, G_MAXSIZE);

  if (homeworlds_move_builder_is_complete(&builder)) {
    return 1;
  }
  if (state->pending_actions_remaining == 0) {
    return HOMEWORLDS_GOAL_BRANCH_SMALL_LEAF_LIMIT;
  }
  if (state->pending_actions_remaining <= 1) {
    return HOMEWORLDS_GOAL_BRANCH_SMALL_LEAF_LIMIT;
  }

  forced_sacrifice_bound = homeworlds_backend_estimate_forced_sacrifice_leaf_upper_bound(state);
  if (forced_sacrifice_bound != G_MAXSIZE) {
    return forced_sacrifice_bound;
  }
  return G_MAXSIZE;
}

static void homeworlds_backend_goal_branch_set_interval_from_bound(HomeworldsGoalBranch *branch,
                                                                   guint side,
                                                                   gint bound) {
  g_return_if_fail(branch != NULL);
  g_return_if_fail(side < 2);

  if (side == 0) {
    branch->interval_max = bound;
  } else {
    branch->interval_min = bound;
  }
}

static gboolean homeworlds_backend_goal_branch_apply_sacrifice_pass_floor(
    const HomeworldsPosition *root_position,
    const HomeworldsGoodMoveContext *context,
    guint side,
    HomeworldsGoalBranch *branch) {
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  gboolean ok = TRUE;
  gboolean scored_pass = FALSE;
  gint pass_score = 0;

  g_return_val_if_fail(root_position != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(branch != NULL, FALSE);

  if ((branch->kind != HOMEWORLDS_GOAL_BRANCH_SACRIFICE &&
       branch->kind != HOMEWORLDS_GOAL_BRANCH_YELLOW_SACRIFICE) ||
      !homeworlds_backend_state_has_active_sacrifice_any_color(&branch->state)) {
    return TRUE;
  }

  builder.builder_state = &branch->state;
  builder.builder_state_size = sizeof(branch->state);
  candidates = homeworlds_move_builder_list_candidates(&builder);
  for (gsize i = 0; i < candidates.count; ++i) {
    const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *) candidates.moves)[i];
    HomeworldsMoveBuilderState child_state = branch->state;
    GameBackendMoveBuilder child = {
      .builder_state = &child_state,
      .builder_state_size = sizeof(child_state),
    };
    HomeworldsMove pass_move = {0};

    if (candidate == NULL ||
        !homeworlds_backend_candidate_is_pass(candidate) ||
        !homeworlds_move_builder_step(&child, candidate) ||
        !homeworlds_move_builder_is_complete(&child) ||
        !homeworlds_move_builder_build_move(&child, &pass_move)) {
      continue;
    }

    if (!homeworlds_backend_move_is_good(&child_state, &pass_move, TRUE) ||
        !homeworlds_backend_move_satisfies_root_catastrophe_requirement(&pass_move, context)) {
      break;
    }
    if (!homeworlds_backend_score_after_move(root_position, &pass_move, &pass_score)) {
      ok = FALSE;
      break;
    }
    scored_pass = TRUE;
    break;
  }
  homeworlds_backend_move_list_free(&candidates);

  if (!ok || !scored_pass) {
    return ok;
  }

  if (side == 0) {
    branch->interval_max = MAX(branch->interval_max, pass_score);
    branch->interval_min = MAX(branch->interval_min, pass_score);
  } else {
    branch->interval_min = MIN(branch->interval_min, pass_score);
    branch->interval_max = MIN(branch->interval_max, pass_score);
  }
  return TRUE;
}

static gboolean homeworlds_backend_goal_branch_update_estimate(const HomeworldsPosition *root_position,
                                                               const HomeworldsGoodMoveContext *context,
                                                               guint side,
                                                               HomeworldsGoalBranch *branch) {
  HomeworldsCandidateOrder order = {0};
  gint state_score = 0;
  guint base_step_count = 0;

  g_return_val_if_fail(root_position != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);
  g_return_val_if_fail(branch != NULL, FALSE);

  state_score = homeworlds_position_evaluate_static(&branch->state.working_position);
  branch->parent_score = state_score;
  branch->leaf_upper_bound = homeworlds_backend_goal_estimate_leaf_upper_bound(&branch->state);
  if (branch->kind == HOMEWORLDS_GOAL_BRANCH_ROOT &&
      branch->state.working_position.phase == HOMEWORLDS_PHASE_PLAY) {
    branch->leaf_upper_bound = G_MAXSIZE;
  }

  if (branch->kind == HOMEWORLDS_GOAL_BRANCH_SINGLE_STEP) {
    HomeworldsMove move = {0};
    GameBackendMoveBuilder builder = {
      .builder_state = &branch->state,
      .builder_state_size = sizeof(branch->state),
    };
    gint exact_score = 0;

    if (homeworlds_move_builder_is_complete(&builder) &&
        homeworlds_move_builder_build_move(&builder, &move) &&
        homeworlds_backend_score_after_move(root_position, &move, &exact_score)) {
      branch->interval_min = exact_score;
      branch->interval_max = exact_score;
    }
  }

  if (branch->kind != HOMEWORLDS_GOAL_BRANCH_ROOT &&
      branch->kind != HOMEWORLDS_GOAL_BRANCH_ROOT_CATASTROPHE_NOW &&
      branch->kind != HOMEWORLDS_GOAL_BRANCH_ROOT_CATASTROPHE_POSTPONE &&
      branch->interval_min == G_MININT &&
      branch->interval_max == G_MAXINT) {
    base_step_count = branch->state.move.kind == HOMEWORLDS_MOVE_KIND_TURN ? branch->state.move.step_count : 0;
    if (homeworlds_backend_candidate_order_find_priority(root_position,
                                                         &branch->state,
                                                         side,
                                                         base_step_count,
                                                         4,
                                                         &order) &&
        order.has_priority) {
      homeworlds_backend_goal_branch_set_interval_from_bound(branch, side, order.bound);
    }
  }

  if (homeworlds_backend_state_has_active_sacrifice(&branch->state, HOMEWORLDS_COLOR_YELLOW)) {
    HomeworldsGoodMoveProofStatus status = {0};

    if (!homeworlds_backend_describe_yellow_sacrifice_proof(&branch->state, side, 0, &status)) {
      return FALSE;
    }
    switch (status.result) {
      case HOMEWORLDS_GOOD_MOVE_PROOF_KEEP:
      case HOMEWORLDS_GOOD_MOVE_PROOF_REJECT:
        homeworlds_backend_goal_branch_set_interval_from_bound(branch, side, status.bound);
        break;
      case HOMEWORLDS_GOOD_MOVE_PROOF_UNCERTAIN:
        break;
      case HOMEWORLDS_GOOD_MOVE_PROOF_NOT_ACTIVE:
      case HOMEWORLDS_GOOD_MOVE_PROOF_NOT_PLAY:
      case HOMEWORLDS_GOOD_MOVE_PROOF_COMPLETE:
      case HOMEWORLDS_GOOD_MOVE_PROOF_UNSUPPORTED_WEIGHTS:
      default:
        break;
    }
  }

  if (!homeworlds_backend_goal_branch_apply_sacrifice_pass_floor(root_position, context, side, branch)) {
    return FALSE;
  }

  branch->parent_delta_min = branch->interval_min == G_MININT ? G_MININT : branch->interval_min - state_score;
  branch->parent_delta_max = branch->interval_max == G_MAXINT ? G_MAXINT : branch->interval_max - state_score;
  return TRUE;
}

static gboolean homeworlds_backend_state_can_use_pass_fallback(const HomeworldsMoveBuilderState *state) {
  g_return_val_if_fail(state != NULL, FALSE);

  if (state->pending_actions_remaining > 0) {
    return FALSE;
  }

  for (guint i = 0; i < state->move.step_count; ++i) {
    if (state->move.steps[i].kind != HOMEWORLDS_STEP_CATASTROPHE) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean homeworlds_backend_state_is_catastrophe_boundary(const HomeworldsMoveBuilderState *state) {
  g_return_val_if_fail(state != NULL, FALSE);

  return state->stage == HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP ||
         state->stage == HOMEWORLDS_BUILDER_STAGE_COMPLETE;
}

static guint homeworlds_backend_collect_profitable_catastrophes(const HomeworldsMoveBuilderState *state,
                                                                HomeworldsProfitableCatastrophe *out_catastrophes,
                                                                guint max_catastrophes) {
  guint count = 0;
  guint side = 0;

  g_return_val_if_fail(state != NULL, 0);
  g_return_val_if_fail(out_catastrophes != NULL || max_catastrophes == 0, 0);

  if (state->working_position.phase != HOMEWORLDS_PHASE_PLAY ||
      state->move.step_count >= HOMEWORLDS_MAX_MOVE_STEPS ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_FIRST_STAR ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_SECOND_STAR ||
      state->stage == HOMEWORLDS_BUILDER_STAGE_SETUP_SHIP) {
    return 0;
  }

  side = state->working_position.turn;
  for (guint system_index = 0; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    const HomeworldsSystem *system = &state->working_position.systems[system_index];

    for (guint color = HOMEWORLDS_COLOR_RED; color <= HOMEWORLDS_COLOR_BLUE; ++color) {
      gint gain = 0;

      if (homeworlds_system_color_count(system, (HomeworldsColor) color) < 4) {
        continue;
      }

      gain = homeworlds_backend_immediate_catastrophe_gain(state, system_index, (HomeworldsColor) color, side);
      if (gain <= 0) {
        continue;
      }

      if (count < max_catastrophes) {
        HomeworldsSystemRef system_ref = {0};

        if (!homeworlds_position_system_ref_for_index(&state->working_position, system_index, &system_ref)) {
          continue;
        }
        out_catastrophes[count] = (HomeworldsProfitableCatastrophe){
          .system_index = system_index,
          .color = (HomeworldsColor) color,
          .system_ref = system_ref,
        };
      }
      count++;
    }
  }

  return MIN(count, max_catastrophes);
}

static gboolean homeworlds_backend_apply_profitable_catastrophe(HomeworldsMoveBuilderState *state,
                                                                const HomeworldsProfitableCatastrophe *catastrophe) {
  GameBackendMoveBuilder builder = {0};
  HomeworldsTurnStep step = {
    .kind = HOMEWORLDS_STEP_CATASTROPHE,
    .target_color = catastrophe->color,
  };

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(catastrophe != NULL, FALSE);
  g_return_val_if_fail(catastrophe->system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);

  builder.builder_state = state;
  builder.builder_state_size = sizeof(*state);
  if (state->stage != HOMEWORLDS_BUILDER_STAGE_COMPLETE) {
    return homeworlds_move_builder_apply_catastrophe(&builder, catastrophe->system_index, catastrophe->color);
  }

  if (state->move.step_count >= HOMEWORLDS_MAX_MOVE_STEPS ||
      !homeworlds_position_system_ref_for_index(&state->working_position,
                                                catastrophe->system_index,
                                                &step.target_system)) {
    return FALSE;
  }

  state->move.steps[state->move.step_count++] = step;
  if (!homeworlds_position_apply_turn_step(&state->working_position, &step)) {
    state->move.step_count--;
    return FALSE;
  }
  return TRUE;
}

static gboolean homeworlds_backend_root_catastrophe_is_optimal_now(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsProfitableCatastrophe *catastrophe) {
  guint side = 0;
  gint immediate_gain = 0;
  guint ceiling = 0;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(catastrophe != NULL, FALSE);
  g_return_val_if_fail(catastrophe->system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, FALSE);

  side = state->working_position.turn;
  immediate_gain = homeworlds_backend_immediate_catastrophe_gain(state,
                                                                 catastrophe->system_index,
                                                                 catastrophe->color,
                                                                 side);
  if (immediate_gain == G_MAXINT) {
    return FALSE;
  }

  ceiling = homeworlds_backend_existing_catastrophe_gain_ceiling(state,
                                                                 catastrophe->system_index,
                                                                 catastrophe->color,
                                                                 side,
                                                                 HOMEWORLDS_SIZE_LARGE);
  if (ceiling == G_MAXUINT) {
    return FALSE;
  }
  return immediate_gain >= 0 && (guint) immediate_gain >= ceiling;
}

static gboolean homeworlds_backend_root_catastrophes_are_all_optimal(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsGoodMoveContext *context) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);

  for (guint i = 0; i < context->root_catastrophe_count; ++i) {
    if (!homeworlds_backend_root_catastrophe_is_optimal_now(state, &context->root_catastrophes[i])) {
      return FALSE;
    }
  }
  return context->root_catastrophe_count > 0;
}

static gboolean homeworlds_backend_goal_make_child_branch(
    const HomeworldsPosition *root_position,
    HomeworldsGoalQueue *queue,
    HomeworldsGoodMoveContext *context,
    const HomeworldsGoalBranch *parent_branch,
    const HomeworldsMoveBuilderState *parent_state,
    const HomeworldsMoveBuilderState *child_state,
    HomeworldsGoalBranchKind kind,
    gsize leaf_upper_bound,
    const char *reason,
    HomeworldsGoalBranch **out_branch) {
  HomeworldsGenerationContext child_context = {0};
  HomeworldsGoalDedupeRef *child_dedupe_ref = NULL;
  HomeworldsGenerationDedupe stack_dedupe = {0};
  gboolean prune_child = FALSE;
  HomeworldsGoalBranch *branch = NULL;

  g_return_val_if_fail(root_position != NULL, FALSE);
  g_return_val_if_fail(queue != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(parent_branch != NULL, FALSE);
  g_return_val_if_fail(parent_state != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(out_branch != NULL, FALSE);

  *out_branch = NULL;
  if (!homeworlds_generation_prepare_child_context(&parent_branch->generation_context,
                                                   parent_state,
                                                   child_state,
                                                   &child_context,
                                                   &stack_dedupe,
                                                   &prune_child)) {
    return FALSE;
  }
  if (prune_child) {
    homeworlds_generation_dedupe_clear(&stack_dedupe);
    return TRUE;
  }

  if (!homeworlds_backend_child_state_is_good_after_step(parent_state, child_state)) {
    homeworlds_generation_dedupe_clear(&stack_dedupe);
    return TRUE;
  }

  if (child_context.sacrifice_dedupe == &stack_dedupe) {
    child_dedupe_ref = homeworlds_backend_goal_dedupe_ref_new();
    if (child_dedupe_ref == NULL) {
      homeworlds_generation_dedupe_clear(&stack_dedupe);
      return FALSE;
    }
    child_context.sacrifice_dedupe = &child_dedupe_ref->dedupe;
  } else if (child_context.sacrifice_dedupe != NULL) {
    child_dedupe_ref = parent_branch->dedupe_ref;
  }

  branch = homeworlds_backend_goal_branch_new(queue,
                                              kind,
                                              child_state,
                                              &child_context,
                                              child_dedupe_ref,
                                              reason);
  if (child_context.sacrifice_dedupe == &stack_dedupe) {
    child_context.sacrifice_dedupe = NULL;
  }
  homeworlds_generation_dedupe_clear(&stack_dedupe);
  if (child_dedupe_ref != NULL && child_dedupe_ref != parent_branch->dedupe_ref) {
    homeworlds_backend_goal_dedupe_ref_unref(child_dedupe_ref);
  }
  if (branch == NULL) {
    return FALSE;
  }

  branch->defer_root_catastrophes = parent_branch->defer_root_catastrophes;
  branch->root_defer_step_count = parent_branch->root_defer_step_count;
  if (!homeworlds_backend_goal_branch_update_estimate(root_position, context, child_state->working_position.turn,
                                                      branch)) {
    homeworlds_backend_goal_branch_free(branch);
    return FALSE;
  }
  if (leaf_upper_bound != G_MAXSIZE) {
    branch->leaf_upper_bound = MIN(branch->leaf_upper_bound, leaf_upper_bound);
  }
  *out_branch = branch;
  return TRUE;
}

static gboolean homeworlds_backend_collect_good_moves_recursive(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsGenerationContext *generation_context,
    HomeworldsGoodMoveContext *context,
    HomeworldsMoveBuffer *buffer,
    gboolean allow_pass_move,
    gboolean *out_covered);

static gboolean homeworlds_backend_state_has_only_catastrophe_prefix(const HomeworldsMoveBuilderState *state) {
  g_return_val_if_fail(state != NULL, FALSE);

  if (state->move.kind != HOMEWORLDS_MOVE_KIND_TURN) {
    return FALSE;
  }

  for (guint i = 0; i < state->move.step_count; ++i) {
    if (state->move.steps[i].kind != HOMEWORLDS_STEP_CATASTROPHE) {
      return FALSE;
    }
  }
  return TRUE;
}

static gboolean homeworlds_backend_state_should_collect_single_steps_first(const HomeworldsMoveBuilderState *state) {
  g_return_val_if_fail(state != NULL, FALSE);

  return state->working_position.phase == HOMEWORLDS_PHASE_PLAY &&
         state->stage == HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP &&
         state->pending_actions_remaining == 0 &&
         homeworlds_backend_state_has_only_catastrophe_prefix(state);
}

static gboolean homeworlds_backend_action_candidate_is_single_step(
    const HomeworldsMoveCandidate *candidate) {
  g_return_val_if_fail(candidate != NULL, FALSE);

  if (candidate->data.kind != HOMEWORLDS_CANDIDATE_ACTION) {
    return FALSE;
  }

  switch ((HomeworldsStepKind) candidate->data.target_color) {
    case HOMEWORLDS_STEP_ATTACK:
    case HOMEWORLDS_STEP_MOVE:
    case HOMEWORLDS_STEP_BUILD:
    case HOMEWORLDS_STEP_TRADE:
      return TRUE;
    case HOMEWORLDS_STEP_SACRIFICE:
    case HOMEWORLDS_STEP_PASS:
    case HOMEWORLDS_STEP_CATASTROPHE:
    case HOMEWORLDS_STEP_DISCOVER:
    case HOMEWORLDS_STEP_NONE:
    default:
      return FALSE;
  }
}

static gboolean homeworlds_backend_goal_enqueue_branch(HomeworldsGoalQueue *queue,
                                                       HomeworldsGoodMoveContext *context,
                                                       HomeworldsGoalBranch *branch) {
  g_return_val_if_fail(queue != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(branch != NULL, FALSE);

  homeworlds_backend_goal_queue_push(queue, context, branch);
  return TRUE;
}

static gboolean homeworlds_backend_goal_split_root_catastrophes(const HomeworldsPosition *root_position,
                                                                HomeworldsGoalQueue *queue,
                                                                HomeworldsGoodMoveContext *context,
                                                                const HomeworldsGoalBranch *branch,
                                                                gboolean *out_split) {
  gboolean all_optimal = FALSE;

  g_return_val_if_fail(root_position != NULL, FALSE);
  g_return_val_if_fail(queue != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(branch != NULL, FALSE);
  g_return_val_if_fail(out_split != NULL, FALSE);

  *out_split = FALSE;
  if (branch->kind != HOMEWORLDS_GOAL_BRANCH_ROOT ||
      context->root_catastrophe_count == 0 ||
      branch->state.move.step_count != 0) {
    return TRUE;
  }

  all_optimal = homeworlds_backend_root_catastrophes_are_all_optimal(&branch->state, context);
  if (all_optimal) {
    HomeworldsGoalBranch *forced = homeworlds_backend_goal_branch_new(queue,
                                                                      HOMEWORLDS_GOAL_BRANCH_ROOT_CATASTROPHE_NOW,
                                                                      &branch->state,
                                                                      &branch->generation_context,
                                                                      branch->dedupe_ref,
                                                                      "force optimal catastrophe from #0");

    if (forced == NULL) {
      return FALSE;
    }
    if (!homeworlds_backend_goal_branch_update_estimate(root_position, context, root_position->turn, forced)) {
      homeworlds_backend_goal_branch_free(forced);
      return FALSE;
    }
    homeworlds_backend_goal_enqueue_branch(queue, context, forced);
    context->goal_branches_split++;
    *out_split = TRUE;
    return TRUE;
  }

  for (guint i = 0; i < context->root_catastrophe_count; ++i) {
    HomeworldsMoveBuilderState child_state = branch->state;
    HomeworldsGoalBranch *child_branch = NULL;

    if (!homeworlds_backend_apply_profitable_catastrophe(&child_state, &context->root_catastrophes[i])) {
      continue;
    }
    if (!homeworlds_backend_goal_make_child_branch(root_position,
                                                   queue,
                                                   context,
                                                   branch,
                                                   &branch->state,
                                                   &child_state,
                                                   HOMEWORLDS_GOAL_BRANCH_ROOT_CATASTROPHE_NOW,
                                                   G_MAXSIZE,
                                                   "play profitable catastrophe from #0 now",
                                                   &child_branch)) {
      return FALSE;
    }
    if (child_branch != NULL) {
      homeworlds_backend_goal_enqueue_branch(queue, context, child_branch);
    }
  }

  HomeworldsGoalBranch *postpone = homeworlds_backend_goal_branch_new(queue,
                                                                      HOMEWORLDS_GOAL_BRANCH_ROOT_CATASTROPHE_POSTPONE,
                                                                      &branch->state,
                                                                      &branch->generation_context,
                                                                      branch->dedupe_ref,
                                                                      "postpone improvable catastrophe from #0");

  if (postpone == NULL) {
    return FALSE;
  }
  postpone->defer_root_catastrophes = TRUE;
  postpone->root_defer_step_count = branch->state.move.step_count;
  if (!homeworlds_backend_goal_branch_update_estimate(root_position, context, root_position->turn, postpone)) {
    homeworlds_backend_goal_branch_free(postpone);
    return FALSE;
  }
  homeworlds_backend_goal_enqueue_branch(queue, context, postpone);

  context->goal_branches_split++;
  *out_split = TRUE;
  return TRUE;
}

static gboolean homeworlds_backend_goal_split_single_step_action(const HomeworldsPosition *root_position,
                                                                 HomeworldsGoalQueue *queue,
                                                                 HomeworldsGoodMoveContext *context,
                                                                 const HomeworldsGoalBranch *branch,
                                                                 const HomeworldsMoveBuilderState *selected_state,
                                                                 const HomeworldsMoveCandidate *action_candidate,
                                                                 guint *inout_created_count) {
  HomeworldsMoveBuilderState action_state = {0};
  GameBackendMoveBuilder action_builder = {0};
  GameBackendMoveList target_candidates = {0};
  guint base_step_count = 0;

  g_return_val_if_fail(root_position != NULL, FALSE);
  g_return_val_if_fail(queue != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(branch != NULL, FALSE);
  g_return_val_if_fail(selected_state != NULL, FALSE);
  g_return_val_if_fail(action_candidate != NULL, FALSE);
  g_return_val_if_fail(inout_created_count != NULL, FALSE);

  action_state = *selected_state;
  action_builder.builder_state = &action_state;
  action_builder.builder_state_size = sizeof(action_state);
  base_step_count = selected_state->move.step_count;

  if (!homeworlds_backend_action_candidate_is_single_step(action_candidate) ||
      !homeworlds_move_builder_step(&action_builder, action_candidate)) {
    return TRUE;
  }

  if (action_state.move.step_count == base_step_count + 1) {
    HomeworldsGoalBranch *child_branch = NULL;

    if (!homeworlds_backend_goal_make_child_branch(root_position,
                                                   queue,
                                                   context,
                                                   branch,
                                                   selected_state,
                                                   &action_state,
                                                   HOMEWORLDS_GOAL_BRANCH_SINGLE_STEP,
                                                   HOMEWORLDS_GOAL_BRANCH_SMALL_LEAF_LIMIT,
                                                   "single-step action from #0",
                                                   &child_branch)) {
      return FALSE;
    }
    if (child_branch != NULL) {
      context->ordering_single_step_moves++;
      (*inout_created_count)++;
      homeworlds_backend_goal_enqueue_branch(queue, context, child_branch);
    }
    return TRUE;
  }

  if (action_state.move.step_count != base_step_count) {
    return TRUE;
  }

  target_candidates = homeworlds_move_builder_list_candidates(&action_builder);
  for (gsize i = 0; i < target_candidates.count; ++i) {
    const HomeworldsMoveCandidate *target_candidate =
        &((const HomeworldsMoveCandidate *) target_candidates.moves)[i];
    HomeworldsMoveBuilderState target_state = action_state;
    GameBackendMoveBuilder target_builder = {
      .builder_state = &target_state,
      .builder_state_size = sizeof(target_state),
    };
    HomeworldsGoalBranch *child_branch = NULL;

    if (target_candidate == NULL ||
        !homeworlds_move_builder_step(&target_builder, target_candidate) ||
        target_state.move.step_count != base_step_count + 1) {
      continue;
    }
    if (!homeworlds_backend_goal_make_child_branch(root_position,
                                                   queue,
                                                   context,
                                                   branch,
                                                   &action_state,
                                                   &target_state,
                                                   HOMEWORLDS_GOAL_BRANCH_SINGLE_STEP,
                                                   HOMEWORLDS_GOAL_BRANCH_SMALL_LEAF_LIMIT,
                                                   "single-step target action from #0",
                                                   &child_branch)) {
      homeworlds_backend_move_list_free(&target_candidates);
      return FALSE;
    }
    if (child_branch != NULL) {
      context->ordering_single_step_moves++;
      (*inout_created_count)++;
      homeworlds_backend_goal_enqueue_branch(queue, context, child_branch);
    }
  }

  homeworlds_backend_move_list_free(&target_candidates);
  return TRUE;
}

static gboolean homeworlds_backend_goal_split_sacrifice_action(const HomeworldsPosition *root_position,
                                                               HomeworldsGoalQueue *queue,
                                                               HomeworldsGoodMoveContext *context,
                                                               const HomeworldsGoalBranch *branch,
                                                               const HomeworldsMoveBuilderState *selected_state,
                                                               const HomeworldsMoveCandidate *action_candidate,
                                                               guint *inout_created_count) {
  HomeworldsMoveBuilderState child_state = *selected_state;
  GameBackendMoveBuilder child = {
    .builder_state = &child_state,
    .builder_state_size = sizeof(child_state),
  };
  HomeworldsGoalBranchKind kind = HOMEWORLDS_GOAL_BRANCH_SACRIFICE;
  HomeworldsGoalBranch *child_branch = NULL;

  g_return_val_if_fail(root_position != NULL, FALSE);
  g_return_val_if_fail(queue != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(branch != NULL, FALSE);
  g_return_val_if_fail(selected_state != NULL, FALSE);
  g_return_val_if_fail(action_candidate != NULL, FALSE);
  g_return_val_if_fail(inout_created_count != NULL, FALSE);

  if (action_candidate->data.kind != HOMEWORLDS_CANDIDATE_ACTION ||
      action_candidate->data.target_color != HOMEWORLDS_STEP_SACRIFICE ||
      !homeworlds_move_builder_step(&child, action_candidate)) {
    return TRUE;
  }

  if (homeworlds_backend_state_has_active_sacrifice(&child_state, HOMEWORLDS_COLOR_YELLOW)) {
    kind = HOMEWORLDS_GOAL_BRANCH_YELLOW_SACRIFICE;
  }
  if (!homeworlds_backend_goal_make_child_branch(root_position,
                                                 queue,
                                                 context,
                                                 branch,
                                                 selected_state,
                                                 &child_state,
                                                 kind,
                                                 G_MAXSIZE,
                                                 "sacrifice from #0",
                                                 &child_branch)) {
    return FALSE;
  }
  if (child_branch != NULL) {
    (*inout_created_count)++;
    homeworlds_backend_goal_enqueue_branch(queue, context, child_branch);
  }
  return TRUE;
}

static gboolean homeworlds_backend_goal_split_select_ship_root(const HomeworldsPosition *root_position,
                                                               HomeworldsGoalQueue *queue,
                                                               HomeworldsGoodMoveContext *context,
                                                               const HomeworldsGoalBranch *branch,
                                                               gboolean *out_split) {
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  const HomeworldsMoveCandidate *pass_candidate = NULL;
  guint created_count = 0;
  gboolean enqueued_pass = FALSE;

  g_return_val_if_fail(root_position != NULL, FALSE);
  g_return_val_if_fail(queue != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(branch != NULL, FALSE);
  g_return_val_if_fail(out_split != NULL, FALSE);

  *out_split = FALSE;
  if (!homeworlds_backend_state_should_collect_single_steps_first(&branch->state)) {
    return TRUE;
  }

  builder.builder_state = (gpointer) &branch->state;
  builder.builder_state_size = sizeof(branch->state);
  candidates = homeworlds_move_builder_list_candidates(&builder);
  context->ordering_single_step_passes++;

  for (gsize i = 0; i < candidates.count; ++i) {
    const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *) candidates.moves)[i];
    HomeworldsMoveBuilderState selected_state = branch->state;
    GameBackendMoveBuilder selected_builder = {
      .builder_state = &selected_state,
      .builder_state_size = sizeof(selected_state),
    };
    GameBackendMoveList action_candidates = {0};

    if (candidate != NULL && homeworlds_backend_candidate_is_pass(candidate)) {
      pass_candidate = candidate;
      continue;
    }
    if (candidate == NULL ||
        candidate->data.kind != HOMEWORLDS_CANDIDATE_SELECT_SHIP ||
        !homeworlds_move_builder_step(&selected_builder, candidate) ||
        selected_state.stage != HOMEWORLDS_BUILDER_STAGE_SELECT_ACTION) {
      continue;
    }

    action_candidates = homeworlds_move_builder_list_candidates(&selected_builder);
    for (gsize action_index = 0; action_index < action_candidates.count; ++action_index) {
      const HomeworldsMoveCandidate *action_candidate =
          &((const HomeworldsMoveCandidate *) action_candidates.moves)[action_index];

      if (action_candidate == NULL) {
        continue;
      }
      if (homeworlds_backend_action_candidate_is_single_step(action_candidate)) {
        if (!homeworlds_backend_goal_split_single_step_action(root_position,
                                                              queue,
                                                              context,
                                                              branch,
                                                              &selected_state,
                                                              action_candidate,
                                                              &created_count)) {
          homeworlds_backend_move_list_free(&action_candidates);
          homeworlds_backend_move_list_free(&candidates);
          return FALSE;
        }
        continue;
      }
      if (!homeworlds_backend_goal_split_sacrifice_action(root_position,
                                                          queue,
                                                          context,
                                                          branch,
                                                          &selected_state,
                                                          action_candidate,
                                                          &created_count)) {
        homeworlds_backend_move_list_free(&action_candidates);
        homeworlds_backend_move_list_free(&candidates);
        return FALSE;
      }
    }
    homeworlds_backend_move_list_free(&action_candidates);
  }

  if (created_count == 0 &&
      pass_candidate != NULL &&
      homeworlds_backend_state_can_use_pass_fallback(&branch->state)) {
    HomeworldsMoveBuilderState child_state = branch->state;
    GameBackendMoveBuilder child = {
      .builder_state = &child_state,
      .builder_state_size = sizeof(child_state),
    };
    HomeworldsGoalBranch *pass_branch = NULL;

    if (homeworlds_move_builder_step(&child, pass_candidate) &&
        !homeworlds_backend_goal_make_child_branch(root_position,
                                                   queue,
                                                   context,
                                                   branch,
                                                   &branch->state,
                                                   &child_state,
                                                   HOMEWORLDS_GOAL_BRANCH_SINGLE_STEP,
                                                   1,
                                                   "pass fallback",
                                                   &pass_branch)) {
      homeworlds_backend_move_list_free(&candidates);
      return FALSE;
    }
    if (pass_branch != NULL) {
      pass_branch->allow_pass_move = TRUE;
      homeworlds_backend_goal_enqueue_branch(queue, context, pass_branch);
      enqueued_pass = TRUE;
    }
  }

  homeworlds_backend_move_list_free(&candidates);
  if (created_count > 0 || enqueued_pass) {
    context->goal_branches_split++;
    *out_split = TRUE;
  }
  return TRUE;
}

static gboolean homeworlds_backend_goal_try_split_branch(const HomeworldsPosition *root_position,
                                                         HomeworldsGoalQueue *queue,
                                                         HomeworldsGoodMoveContext *context,
                                                         const HomeworldsGoalBranch *branch,
                                                         gboolean *out_split) {
  g_return_val_if_fail(root_position != NULL, FALSE);
  g_return_val_if_fail(queue != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(branch != NULL, FALSE);
  g_return_val_if_fail(out_split != NULL, FALSE);

  *out_split = FALSE;
  if (!homeworlds_backend_goal_split_root_catastrophes(root_position, queue, context, branch, out_split)) {
    return FALSE;
  }
  if (*out_split) {
    return TRUE;
  }
  return homeworlds_backend_goal_split_select_ship_root(root_position, queue, context, branch, out_split);
}

static gboolean homeworlds_backend_collect_child_state(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsGenerationContext *generation_context,
    HomeworldsGoodMoveContext *context,
    HomeworldsMoveBuffer *buffer,
    gboolean allow_pass_move,
    const HomeworldsMoveBuilderState *child_state,
    gboolean *out_child_covered) {
  HomeworldsGenerationContext child_context = {0};
  HomeworldsGenerationDedupe child_dedupe = {0};
  gboolean prune_child = FALSE;
  gboolean child_covered = FALSE;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(generation_context != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(child_state != NULL, FALSE);
  g_return_val_if_fail(out_child_covered != NULL, FALSE);

  *out_child_covered = FALSE;
  if (!homeworlds_generation_prepare_child_context(generation_context,
                                                   state,
                                                   child_state,
                                                   &child_context,
                                                   &child_dedupe,
                                                   &prune_child)) {
    return FALSE;
  }
  if (prune_child) {
    *out_child_covered = TRUE;
    homeworlds_generation_dedupe_clear(&child_dedupe);
    return TRUE;
  }
  if (!homeworlds_backend_child_state_is_good_after_step(state, child_state)) {
    context->goal_rejected_steps++;
    homeworlds_generation_dedupe_clear(&child_dedupe);
    return TRUE;
  }
  if (!homeworlds_backend_prepare_pruning_for_child(context,
                                                    buffer,
                                                    child_state,
                                                    &prune_child)) {
    homeworlds_generation_dedupe_clear(&child_dedupe);
    return FALSE;
  }
  if (prune_child) {
    *out_child_covered = TRUE;
    homeworlds_generation_dedupe_clear(&child_dedupe);
    return TRUE;
  }
  if (!homeworlds_backend_collect_good_moves_recursive(child_state,
                                                       &child_context,
                                                       context,
                                                       buffer,
                                                       allow_pass_move,
                                                       &child_covered)) {
    homeworlds_generation_dedupe_clear(&child_dedupe);
    return FALSE;
  }

  *out_child_covered = child_covered;
  homeworlds_generation_dedupe_clear(&child_dedupe);
  return TRUE;
}

static gboolean homeworlds_backend_collect_single_step_action(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsGenerationContext *generation_context,
    HomeworldsGoodMoveContext *context,
    HomeworldsMoveBuffer *buffer,
    gboolean allow_pass_move,
    const HomeworldsMoveCandidate *action_candidate,
    gboolean *out_covered) {
  HomeworldsMoveBuilderState action_state = {0};
  GameBackendMoveBuilder action_builder = {0};
  GameBackendMoveList target_candidates = {0};
  gboolean covered = FALSE;
  guint base_step_count = 0;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(generation_context != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(action_candidate != NULL, FALSE);
  g_return_val_if_fail(out_covered != NULL, FALSE);

  *out_covered = FALSE;
  action_state = *state;
  action_builder.builder_state = &action_state;
  action_builder.builder_state_size = sizeof(action_state);
  base_step_count = state->move.step_count;

  if (!homeworlds_backend_action_candidate_is_single_step(action_candidate) ||
      !homeworlds_move_builder_step(&action_builder, action_candidate)) {
    return TRUE;
  }

  if (action_state.move.step_count == base_step_count + 1) {
    context->ordering_single_step_moves++;
    return homeworlds_backend_collect_child_state(state,
                                                  generation_context,
                                                  context,
                                                  buffer,
                                                  allow_pass_move,
                                                  &action_state,
                                                  out_covered);
  }

  if (action_state.move.step_count != base_step_count) {
    return TRUE;
  }

  target_candidates = homeworlds_move_builder_list_candidates(&action_builder);
  for (gsize i = 0; i < target_candidates.count; ++i) {
    const HomeworldsMoveCandidate *target_candidate =
        &((const HomeworldsMoveCandidate *) target_candidates.moves)[i];
    HomeworldsMoveBuilderState target_state = action_state;
    GameBackendMoveBuilder target_builder = {
      .builder_state = &target_state,
      .builder_state_size = sizeof(target_state),
    };
    gboolean child_covered = FALSE;

    if (target_candidate == NULL ||
        !homeworlds_move_builder_step(&target_builder, target_candidate) ||
        target_state.move.step_count != base_step_count + 1) {
      continue;
    }

    context->ordering_single_step_moves++;
    if (!homeworlds_backend_collect_child_state(&action_state,
                                                generation_context,
                                                context,
                                                buffer,
                                                allow_pass_move,
                                                &target_state,
                                                &child_covered)) {
      homeworlds_backend_move_list_free(&target_candidates);
      return FALSE;
    }
    covered = covered || child_covered;
  }

  homeworlds_backend_move_list_free(&target_candidates);
  *out_covered = covered;
  return TRUE;
}

static gboolean homeworlds_backend_collect_single_step_moves_for_ship(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsGenerationContext *generation_context,
    HomeworldsGoodMoveContext *context,
    HomeworldsMoveBuffer *buffer,
    gboolean allow_pass_move,
    const HomeworldsMoveCandidate *ship_candidate,
    gboolean *out_covered) {
  HomeworldsMoveBuilderState selected_state = {0};
  GameBackendMoveBuilder selected_builder = {0};
  GameBackendMoveList action_candidates = {0};
  gboolean covered = FALSE;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(generation_context != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(ship_candidate != NULL, FALSE);
  g_return_val_if_fail(out_covered != NULL, FALSE);

  *out_covered = FALSE;
  selected_state = *state;
  selected_builder.builder_state = &selected_state;
  selected_builder.builder_state_size = sizeof(selected_state);

  if (ship_candidate->data.kind != HOMEWORLDS_CANDIDATE_SELECT_SHIP ||
      !homeworlds_move_builder_step(&selected_builder, ship_candidate) ||
      selected_state.stage != HOMEWORLDS_BUILDER_STAGE_SELECT_ACTION) {
    return TRUE;
  }

  action_candidates = homeworlds_move_builder_list_candidates(&selected_builder);
  for (gsize i = 0; i < action_candidates.count; ++i) {
    const HomeworldsMoveCandidate *action_candidate =
        &((const HomeworldsMoveCandidate *) action_candidates.moves)[i];
    gboolean child_covered = FALSE;

    if (action_candidate == NULL ||
        !homeworlds_backend_action_candidate_is_single_step(action_candidate)) {
      continue;
    }
    if (!homeworlds_backend_collect_single_step_action(&selected_state,
                                                       generation_context,
                                                       context,
                                                       buffer,
                                                       allow_pass_move,
                                                       action_candidate,
                                                       &child_covered)) {
      homeworlds_backend_move_list_free(&action_candidates);
      return FALSE;
    }
    covered = covered || child_covered;
  }

  homeworlds_backend_move_list_free(&action_candidates);
  *out_covered = covered;
  return TRUE;
}

static gboolean homeworlds_backend_collect_sacrifice_for_ship(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsGenerationContext *generation_context,
    HomeworldsGoodMoveContext *context,
    HomeworldsMoveBuffer *buffer,
    gboolean allow_pass_move,
    const HomeworldsMoveCandidate *ship_candidate,
    gboolean *out_covered) {
  HomeworldsMoveBuilderState selected_state = {0};
  GameBackendMoveBuilder selected_builder = {0};
  GameBackendMoveList action_candidates = {0};

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(generation_context != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(ship_candidate != NULL, FALSE);
  g_return_val_if_fail(out_covered != NULL, FALSE);

  *out_covered = FALSE;
  selected_state = *state;
  selected_builder.builder_state = &selected_state;
  selected_builder.builder_state_size = sizeof(selected_state);

  if (ship_candidate->data.kind != HOMEWORLDS_CANDIDATE_SELECT_SHIP ||
      !homeworlds_move_builder_step(&selected_builder, ship_candidate) ||
      selected_state.stage != HOMEWORLDS_BUILDER_STAGE_SELECT_ACTION) {
    return TRUE;
  }

  action_candidates = homeworlds_move_builder_list_candidates(&selected_builder);
  for (gsize i = 0; i < action_candidates.count; ++i) {
    const HomeworldsMoveCandidate *action_candidate =
        &((const HomeworldsMoveCandidate *) action_candidates.moves)[i];
    HomeworldsMoveBuilderState child_state = selected_state;
    GameBackendMoveBuilder child = {
      .builder_state = &child_state,
      .builder_state_size = sizeof(child_state),
    };

    if (action_candidate == NULL ||
        action_candidate->data.kind != HOMEWORLDS_CANDIDATE_ACTION ||
        action_candidate->data.target_color != HOMEWORLDS_STEP_SACRIFICE) {
      continue;
    }
    if (!homeworlds_move_builder_step(&child, action_candidate)) {
      continue;
    }

    if (!homeworlds_backend_collect_child_state(&selected_state,
                                                generation_context,
                                                context,
                                                buffer,
                                                allow_pass_move,
                                                &child_state,
                                                out_covered)) {
      homeworlds_backend_move_list_free(&action_candidates);
      return FALSE;
    }
    break;
  }

  homeworlds_backend_move_list_free(&action_candidates);
  return TRUE;
}

static gboolean homeworlds_backend_collect_select_ship_with_single_steps_first(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsGenerationContext *generation_context,
    HomeworldsGoodMoveContext *context,
    HomeworldsMoveBuffer *buffer,
    gboolean allow_pass_move,
    gboolean *out_covered) {
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  const HomeworldsMoveCandidate *pass_candidate = NULL;
  gboolean covered = FALSE;
  gboolean candidate_covered = FALSE;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(generation_context != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(out_covered != NULL, FALSE);

  *out_covered = FALSE;
  builder.builder_state = (gpointer) state;
  builder.builder_state_size = sizeof(*state);
  candidates = homeworlds_move_builder_list_candidates(&builder);
  context->ordering_single_step_passes++;

  for (gsize i = 0; i < candidates.count; ++i) {
    const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *) candidates.moves)[i];
    gboolean child_covered = FALSE;

    if (candidate != NULL && homeworlds_backend_candidate_is_pass(candidate)) {
      pass_candidate = candidate;
      continue;
    }
    if (candidate == NULL) {
      continue;
    }
    if (!homeworlds_backend_collect_single_step_moves_for_ship(state,
                                                               generation_context,
                                                               context,
                                                               buffer,
                                                               allow_pass_move,
                                                               candidate,
                                                               &child_covered)) {
      homeworlds_backend_move_list_free(&candidates);
      return FALSE;
    }
    covered = covered || child_covered;
    candidate_covered = candidate_covered || child_covered;
  }

  for (gsize i = 0; i < candidates.count; ++i) {
    const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *) candidates.moves)[i];
    gboolean child_covered = FALSE;

    if (candidate == NULL || homeworlds_backend_candidate_is_pass(candidate)) {
      continue;
    }
    if (!homeworlds_backend_collect_sacrifice_for_ship(state,
                                                       generation_context,
                                                       context,
                                                       buffer,
                                                       allow_pass_move,
                                                       candidate,
                                                       &child_covered)) {
      homeworlds_backend_move_list_free(&candidates);
      return FALSE;
    }
    covered = covered || child_covered;
    candidate_covered = candidate_covered || child_covered;
  }

  if (pass_candidate != NULL &&
      !candidate_covered &&
      homeworlds_backend_state_can_use_pass_fallback(state)) {
    HomeworldsMoveBuilderState child_state = *state;
    GameBackendMoveBuilder child = {
      .builder_state = &child_state,
      .builder_state_size = sizeof(child_state),
    };
    gboolean child_covered = FALSE;

    if (homeworlds_move_builder_step(&child, pass_candidate)) {
      if (!homeworlds_backend_collect_child_state(state,
                                                  generation_context,
                                                  context,
                                                  buffer,
                                                  TRUE,
                                                  &child_state,
                                                  &child_covered)) {
        homeworlds_backend_move_list_free(&candidates);
        return FALSE;
      }
      covered = covered || child_covered;
    }
  }

  homeworlds_backend_move_list_free(&candidates);
  *out_covered = covered;
  return TRUE;
}

static gboolean homeworlds_backend_collect_good_moves_recursive(
    const HomeworldsMoveBuilderState *state,
    const HomeworldsGenerationContext *generation_context,
    HomeworldsGoodMoveContext *context,
    HomeworldsMoveBuffer *buffer,
    gboolean allow_pass_move,
    gboolean *out_covered) {
  GameBackendMoveBuilder builder = {0};
  GameBackendMoveList candidates = {0};
  HomeworldsCandidateOrder *candidate_order = NULL;
  HomeworldsProfitableCatastrophe catastrophes[HOMEWORLDS_SYSTEM_SLOT_COUNT * 4] = {0};
  guint catastrophe_count = 0;
  gboolean forced_catastrophe_seen = FALSE;
  const HomeworldsMoveCandidate *pass_candidate = NULL;
  gboolean duplicate = FALSE;
  gboolean covered = FALSE;
  gboolean candidate_covered = FALSE;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(generation_context != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(out_covered != NULL, FALSE);

  *out_covered = FALSE;
  if (!homeworlds_generation_visit_state(generation_context, state, &duplicate)) {
    return FALSE;
  }
  if (duplicate) {
    context->goal_duplicate_states++;
    *out_covered = TRUE;
    return TRUE;
  }

  builder.builder_state = (gpointer) state;
  builder.builder_state_size = sizeof(*state);

  if (homeworlds_backend_state_is_catastrophe_boundary(state)) {
    catastrophe_count =
        homeworlds_backend_collect_profitable_catastrophes(state, catastrophes, G_N_ELEMENTS(catastrophes));
  }
  if (catastrophe_count > 0) {
    for (guint i = 0; i < catastrophe_count; ++i) {
      HomeworldsMoveBuilderState child_state = *state;
      HomeworldsGenerationContext child_context = {0};
      HomeworldsGenerationDedupe child_dedupe = {0};
      gboolean prune_child = FALSE;
      gboolean child_covered = FALSE;

      if (homeworlds_backend_catastrophe_is_root_required(context, &catastrophes[i])) {
        continue;
      }
      forced_catastrophe_seen = TRUE;
      if (!homeworlds_backend_apply_profitable_catastrophe(&child_state, &catastrophes[i])) {
        continue;
      }
      if (!homeworlds_generation_prepare_child_context(generation_context,
                                                       state,
                                                       &child_state,
                                                       &child_context,
                                                       &child_dedupe,
                                                       &prune_child)) {
        return FALSE;
      }
      if (prune_child) {
        covered = TRUE;
        homeworlds_generation_dedupe_clear(&child_dedupe);
        continue;
      }
      if (!homeworlds_backend_collect_good_moves_recursive(&child_state,
                                                           &child_context,
                                                           context,
                                                           buffer,
                                                           allow_pass_move,
                                                           &child_covered)) {
        homeworlds_generation_dedupe_clear(&child_dedupe);
        return FALSE;
      }
      covered = covered || child_covered;
      homeworlds_generation_dedupe_clear(&child_dedupe);
    }

    if (forced_catastrophe_seen) {
      *out_covered = covered;
      return TRUE;
    }
  }

  for (guint i = 0; i < catastrophe_count; ++i) {
    HomeworldsMoveBuilderState child_state = *state;
    HomeworldsGenerationContext child_context = {0};
    HomeworldsGenerationDedupe child_dedupe = {0};
    gboolean prune_child = FALSE;
    gboolean child_covered = FALSE;

    if (!homeworlds_backend_catastrophe_is_root_required(context, &catastrophes[i]) ||
        (context->defer_root_catastrophes &&
         state->move.step_count <= context->root_defer_step_count) ||
        homeworlds_backend_move_has_profitable_catastrophe(&state->move, &catastrophes[i]) ||
        !homeworlds_backend_apply_profitable_catastrophe(&child_state, &catastrophes[i])) {
      continue;
    }
    if (!homeworlds_generation_prepare_child_context(generation_context,
                                                     state,
                                                     &child_state,
                                                     &child_context,
                                                     &child_dedupe,
                                                     &prune_child)) {
      return FALSE;
    }
    if (prune_child) {
      covered = TRUE;
      homeworlds_generation_dedupe_clear(&child_dedupe);
      continue;
    }
    if (!homeworlds_backend_collect_good_moves_recursive(&child_state,
                                                         &child_context,
                                                         context,
                                                         buffer,
                                                         allow_pass_move,
                                                         &child_covered)) {
      homeworlds_generation_dedupe_clear(&child_dedupe);
      return FALSE;
    }
    covered = covered || child_covered;
    homeworlds_generation_dedupe_clear(&child_dedupe);
  }

  if (homeworlds_move_builder_is_complete(&builder)) {
    HomeworldsMove move = {0};

    if (!homeworlds_move_builder_build_move(&builder, &move)) {
      return FALSE;
    }
    if (!homeworlds_backend_move_is_good(state, &move, allow_pass_move)) {
      context->goal_rejected_bad_moves++;
      return TRUE;
    }
    if (!homeworlds_backend_move_satisfies_root_catastrophe_requirement(&move, context)) {
      context->goal_rejected_root_catastrophes++;
      return TRUE;
    }

    if (!homeworlds_backend_move_buffer_append(buffer, &move, context)) {
      return FALSE;
    }
    *out_covered = TRUE;
    return TRUE;
  }

  if (homeworlds_backend_state_should_collect_single_steps_first(state)) {
    return homeworlds_backend_collect_select_ship_with_single_steps_first(state,
                                                                         generation_context,
                                                                         context,
                                                                         buffer,
                                                                         allow_pass_move,
                                                                         out_covered);
  }

  candidates = homeworlds_move_builder_list_candidates(&builder);
  if (!homeworlds_backend_build_candidate_order(buffer->position,
                                                state,
                                                context,
                                                &candidates,
                                                &candidate_order)) {
    homeworlds_backend_move_list_free(&candidates);
    return FALSE;
  }
  for (gsize order_index = 0; order_index < candidates.count; ++order_index) {
    gsize i = candidate_order[order_index].index;
    const HomeworldsMoveCandidate *candidate = &((const HomeworldsMoveCandidate *) candidates.moves)[i];
    HomeworldsMoveBuilderState child_state = *state;
    HomeworldsGenerationContext child_context = {0};
    HomeworldsGenerationDedupe child_dedupe = {0};
    GameBackendMoveBuilder child = {
      .builder_state = &child_state,
      .builder_state_size = sizeof(child_state),
    };
    gboolean prune_child = FALSE;
    gboolean child_covered = FALSE;
    gboolean candidate_is_sacrifice_pass = FALSE;

    candidate_is_sacrifice_pass = candidate != NULL &&
                                  homeworlds_backend_candidate_is_pass(candidate) &&
                                  homeworlds_backend_state_has_active_sacrifice_any_color(state);
    if (candidate != NULL && homeworlds_backend_candidate_is_pass(candidate) && !candidate_is_sacrifice_pass) {
      pass_candidate = candidate;
      continue;
    }

    if (candidate == NULL ||
        !homeworlds_move_builder_step(&child, candidate)) {
      continue;
    }
    if (!homeworlds_generation_prepare_child_context(generation_context,
                                                     state,
                                                     &child_state,
                                                     &child_context,
                                                     &child_dedupe,
                                                     &prune_child)) {
      g_free(candidate_order);
      homeworlds_backend_move_list_free(&candidates);
      return FALSE;
    }
    if (prune_child) {
      covered = TRUE;
      candidate_covered = TRUE;
      homeworlds_generation_dedupe_clear(&child_dedupe);
      continue;
    }
    if (!homeworlds_backend_child_state_is_good_after_step(state, &child_state)) {
      context->goal_rejected_steps++;
      homeworlds_generation_dedupe_clear(&child_dedupe);
      continue;
    }
    if (!homeworlds_backend_prepare_pruning_for_child(context,
                                                      buffer,
                                                      &child_state,
                                                      &prune_child)) {
      homeworlds_generation_dedupe_clear(&child_dedupe);
      g_free(candidate_order);
      homeworlds_backend_move_list_free(&candidates);
      return FALSE;
    }
    if (prune_child) {
      covered = TRUE;
      candidate_covered = TRUE;
      homeworlds_generation_dedupe_clear(&child_dedupe);
      continue;
    }
    if (!homeworlds_backend_collect_good_moves_recursive(&child_state,
                                                         &child_context,
                                                         context,
                                                         buffer,
                                                         allow_pass_move || candidate_is_sacrifice_pass,
                                                         &child_covered)) {
      homeworlds_generation_dedupe_clear(&child_dedupe);
      g_free(candidate_order);
      homeworlds_backend_move_list_free(&candidates);
      return FALSE;
    }
    covered = covered || child_covered;
    candidate_covered = candidate_covered || child_covered;
    homeworlds_generation_dedupe_clear(&child_dedupe);
  }

  if (pass_candidate != NULL &&
      !candidate_covered &&
      homeworlds_backend_state_can_use_pass_fallback(state)) {
    HomeworldsMoveBuilderState child_state = *state;
    HomeworldsGenerationContext child_context = {0};
    HomeworldsGenerationDedupe child_dedupe = {0};
    GameBackendMoveBuilder child = {
      .builder_state = &child_state,
      .builder_state_size = sizeof(child_state),
    };
    gboolean prune_child = FALSE;
    gboolean child_covered = FALSE;

    if (homeworlds_move_builder_step(&child, pass_candidate)) {
      if (!homeworlds_generation_prepare_child_context(generation_context,
                                                       state,
                                                       &child_state,
                                                       &child_context,
                                                       &child_dedupe,
                                                       &prune_child)) {
        g_free(candidate_order);
        homeworlds_backend_move_list_free(&candidates);
        return FALSE;
      }
      if (prune_child) {
        covered = TRUE;
      } else if (homeworlds_backend_child_state_is_good_after_step(state, &child_state)) {
        if (!homeworlds_backend_prepare_pruning_for_child(context,
                                                          buffer,
                                                          &child_state,
                                                          &prune_child)) {
          homeworlds_generation_dedupe_clear(&child_dedupe);
          g_free(candidate_order);
          homeworlds_backend_move_list_free(&candidates);
          return FALSE;
        }
        if (prune_child) {
          covered = TRUE;
        } else {
          if (!homeworlds_backend_collect_good_moves_recursive(&child_state,
                                                               &child_context,
                                                               context,
                                                               buffer,
                                                               TRUE,
                                                               &child_covered)) {
            homeworlds_generation_dedupe_clear(&child_dedupe);
            g_free(candidate_order);
            homeworlds_backend_move_list_free(&candidates);
            return FALSE;
          }
          covered = covered || child_covered;
        }
      }
    }
    homeworlds_generation_dedupe_clear(&child_dedupe);
  }

  g_free(candidate_order);
  homeworlds_backend_move_list_free(&candidates);
  *out_covered = covered;
  return TRUE;
}

static void homeworlds_backend_goal_branch_update_delta(HomeworldsGoalBranch *branch) {
  g_return_if_fail(branch != NULL);

  branch->parent_delta_min = branch->interval_min == G_MININT ? G_MININT : branch->interval_min - branch->parent_score;
  branch->parent_delta_max = branch->interval_max == G_MAXINT ? G_MAXINT : branch->interval_max - branch->parent_score;
}

static gboolean homeworlds_backend_goal_branch_split_score_interval(HomeworldsGoalQueue *queue,
                                                                    HomeworldsGoodMoveContext *context,
                                                                    HomeworldsGoalBranch *branch,
                                                                    guint side,
                                                                    gint second_best_bound) {
  HomeworldsGoalBranch *remainder = NULL;
  gint old_min = 0;
  gint old_max = 0;

  g_return_val_if_fail(queue != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(branch != NULL, FALSE);
  g_return_val_if_fail(side < 2, FALSE);

  if (!homeworlds_backend_goal_bound_is_finite(second_best_bound) ||
      !homeworlds_backend_goal_bound_is_finite(homeworlds_backend_goal_branch_best_bound(side, branch))) {
    return TRUE;
  }

  old_min = branch->interval_min;
  old_max = branch->interval_max;
  if (side == 0) {
    gint top_min = MAX(old_min, second_best_bound);

    if (top_min <= old_min || top_min > old_max || top_min == G_MININT) {
      return TRUE;
    }
    remainder = homeworlds_backend_goal_branch_clone_for_interval(queue,
                                                                  branch,
                                                                  old_min,
                                                                  top_min - 1,
                                                                  "remaining lower score band");
    if (remainder == NULL) {
      return FALSE;
    }
    branch->interval_min = top_min;
  } else {
    gint top_max = MIN(old_max, second_best_bound);

    if (top_max >= old_max || top_max < old_min || top_max == G_MAXINT) {
      return TRUE;
    }
    remainder = homeworlds_backend_goal_branch_clone_for_interval(queue,
                                                                  branch,
                                                                  top_max + 1,
                                                                  old_max,
                                                                  "remaining higher score band");
    if (remainder == NULL) {
      return FALSE;
    }
    branch->interval_max = top_max;
  }

  homeworlds_backend_goal_branch_update_delta(branch);
  context->goal_branches_split++;
  context->goal_branches_requeued++;
  homeworlds_backend_goal_report_append(context,
                                        "score-split #%zu explore=[%d,%d] requeue=[%d,%d]",
                                        branch->id,
                                        branch->interval_min,
                                        branch->interval_max,
                                        remainder->interval_min,
                                        remainder->interval_max);
  homeworlds_backend_goal_queue_push(queue, context, remainder);
  return TRUE;
}

static gboolean homeworlds_backend_goal_branch_is_cutoff_skipped(const HomeworldsMoveBuffer *buffer,
                                                                 const HomeworldsGoalBranch *branch,
                                                                 gint *out_cutoff) {
  gint cutoff = 0;
  HomeworldsGoodMoveCutoffKind cutoff_kind = HOMEWORLDS_GOOD_MOVE_CUTOFF_NONE;

  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(branch != NULL, FALSE);
  g_return_val_if_fail(out_cutoff != NULL, FALSE);

  if (!homeworlds_backend_move_buffer_current_cutoff(buffer, &cutoff, &cutoff_kind)) {
    return FALSE;
  }

  *out_cutoff = cutoff;
  return !homeworlds_backend_goal_branch_can_reach_cutoff(buffer->side, branch, cutoff);
}

static gboolean homeworlds_backend_goal_explore_branch(HomeworldsGoodMoveContext *context,
                                                       HomeworldsMoveBuffer *buffer,
                                                       const HomeworldsGoalBranch *branch) {
  gboolean old_has_score_interval = FALSE;
  gint old_score_interval_min = 0;
  gint old_score_interval_max = 0;
  gboolean old_defer_root_catastrophes = FALSE;
  guint old_root_defer_step_count = 0;
  gsize old_leaves_seen = 0;
  gsize old_scored_moves = 0;
  gsize old_kept_moves = 0;
  gsize old_pruned_branches = 0;
  gsize old_created_branches = 0;
  gsize old_duplicate_states = 0;
  gsize old_rejected_steps = 0;
  gsize old_rejected_bad_moves = 0;
  gsize old_rejected_root_catastrophes = 0;
  gsize old_rejected_score_intervals = 0;
  gsize old_rejected_score_windows = 0;
  gsize old_rejected_full_buffer = 0;
  gint old_best_score = 0;
  gboolean old_has_best_score = FALSE;
  char old_best_text[32] = {0};
  char new_best_text[32] = {0};
  char old_cutoff_text[32] = {0};
  char new_cutoff_text[32] = {0};
  char prefix[256] = {0};
  gboolean covered = FALSE;

  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(branch != NULL, FALSE);

  old_has_score_interval = context->has_score_interval;
  old_score_interval_min = context->score_interval_min;
  old_score_interval_max = context->score_interval_max;
  old_defer_root_catastrophes = context->defer_root_catastrophes;
  old_root_defer_step_count = context->root_defer_step_count;
  old_leaves_seen = buffer->leaves_seen;
  old_scored_moves = buffer->scored_moves;
  old_kept_moves = buffer->count;
  old_pruned_branches = context->pruning_pruned_branches;
  old_created_branches = context->goal_branches_created;
  old_duplicate_states = context->goal_duplicate_states;
  old_rejected_steps = context->goal_rejected_steps;
  old_rejected_bad_moves = context->goal_rejected_bad_moves;
  old_rejected_root_catastrophes = context->goal_rejected_root_catastrophes;
  old_rejected_score_intervals = context->goal_rejected_score_intervals;
  old_rejected_score_windows = context->goal_rejected_score_windows;
  old_rejected_full_buffer = context->goal_rejected_full_buffer;
  old_best_score = buffer->best_score;
  old_has_best_score = buffer->has_best_score;
  homeworlds_backend_format_optional_goal_score(old_has_best_score,
                                                old_best_score,
                                                old_best_text,
                                                sizeof(old_best_text));
  homeworlds_backend_format_goal_cutoff(buffer, old_cutoff_text, sizeof(old_cutoff_text));
  homeworlds_backend_goal_branch_format_prefix(branch, prefix, sizeof(prefix));

  context->has_score_interval = TRUE;
  context->score_interval_min = branch->interval_min;
  context->score_interval_max = branch->interval_max;
  context->defer_root_catastrophes = branch->defer_root_catastrophes;
  context->root_defer_step_count = branch->root_defer_step_count;

  context->goal_branches_direct++;
  homeworlds_backend_goal_report_append(context,
                                        "explore #%zu %s interval=[%d,%d] leaves<=%zu prefix=[%s]",
                                        branch->id,
                                        homeworlds_backend_goal_branch_kind_name(branch->kind),
                                        branch->interval_min,
                                        branch->interval_max,
                                        branch->leaf_upper_bound,
                                        prefix);
  if (!homeworlds_backend_collect_good_moves_recursive(&branch->state,
                                                       &branch->generation_context,
                                                       context,
                                                       buffer,
                                                       branch->allow_pass_move,
                                                       &covered)) {
    context->has_score_interval = old_has_score_interval;
    context->score_interval_min = old_score_interval_min;
    context->score_interval_max = old_score_interval_max;
    context->defer_root_catastrophes = old_defer_root_catastrophes;
    context->root_defer_step_count = old_root_defer_step_count;
    return FALSE;
  }

  context->goal_branches_exhausted++;
  homeworlds_backend_format_optional_goal_score(buffer->has_best_score,
                                                buffer->best_score,
                                                new_best_text,
                                                sizeof(new_best_text));
  homeworlds_backend_format_goal_cutoff(buffer, new_cutoff_text, sizeof(new_cutoff_text));
  homeworlds_backend_goal_report_append(context,
                                        "explore-result #%zu covered=%u leaves+=%zu scored+=%zu kept=%zu->%zu "
                                        "best=%s->%s cutoff=%s->%s pruned+=%zu created+=%zu duplicate+=%zu "
                                        "step_reject+=%zu bad_move+=%zu root_cat_reject+=%zu interval_reject+=%zu "
                                        "window_reject+=%zu full_reject+=%zu",
                                        branch->id,
                                        covered ? 1 : 0,
                                        buffer->leaves_seen - old_leaves_seen,
                                        buffer->scored_moves - old_scored_moves,
                                        old_kept_moves,
                                        buffer->count,
                                        old_best_text,
                                        new_best_text,
                                        old_cutoff_text,
                                        new_cutoff_text,
                                        context->pruning_pruned_branches - old_pruned_branches,
                                        context->goal_branches_created - old_created_branches,
                                        context->goal_duplicate_states - old_duplicate_states,
                                        context->goal_rejected_steps - old_rejected_steps,
                                        context->goal_rejected_bad_moves - old_rejected_bad_moves,
                                        context->goal_rejected_root_catastrophes - old_rejected_root_catastrophes,
                                        context->goal_rejected_score_intervals - old_rejected_score_intervals,
                                        context->goal_rejected_score_windows - old_rejected_score_windows,
                                        context->goal_rejected_full_buffer - old_rejected_full_buffer);
  context->has_score_interval = old_has_score_interval;
  context->score_interval_min = old_score_interval_min;
  context->score_interval_max = old_score_interval_max;
  context->defer_root_catastrophes = old_defer_root_catastrophes;
  context->root_defer_step_count = old_root_defer_step_count;
  return TRUE;
}

static gboolean homeworlds_backend_goal_collect_good_moves(const HomeworldsPosition *root_position,
                                                           const HomeworldsMoveBuilderState *root_state,
                                                           const HomeworldsGenerationContext *generation_context,
                                                           HomeworldsGoodMoveContext *context,
                                                           HomeworldsMoveBuffer *buffer) {
  HomeworldsGoalQueue queue = {0};
  HomeworldsGoalBranch *root_branch = NULL;
  gboolean ok = TRUE;

  g_return_val_if_fail(root_position != NULL, FALSE);
  g_return_val_if_fail(root_state != NULL, FALSE);
  g_return_val_if_fail(generation_context != NULL, FALSE);
  g_return_val_if_fail(context != NULL, FALSE);
  g_return_val_if_fail(buffer != NULL, FALSE);

  homeworlds_backend_goal_queue_init(&queue);
  root_branch = homeworlds_backend_goal_branch_new(&queue,
                                                   HOMEWORLDS_GOAL_BRANCH_ROOT,
                                                   root_state,
                                                   generation_context,
                                                   NULL,
                                                   "root");
  if (root_branch == NULL ||
      !homeworlds_backend_goal_branch_update_estimate(root_position, context, root_position->turn, root_branch)) {
    homeworlds_backend_goal_branch_free(root_branch);
    homeworlds_backend_goal_queue_clear(&queue);
    return FALSE;
  }
  homeworlds_backend_goal_queue_push(&queue, context, root_branch);

  while (ok && queue.branches->len > 0) {
    HomeworldsGoalBranch *branch = homeworlds_backend_goal_queue_pop_best(&queue, root_position->turn);
    gint cutoff = 0;
    gint second_best_bound = 0;
    gsize created_before_split = 0;
    char prefix[256] = {0};
    gboolean split = FALSE;

    homeworlds_backend_goal_branch_format_prefix(branch, prefix, sizeof(prefix));
    context->goal_branches_selected++;
    homeworlds_backend_goal_report_append(context,
                                          "select #%zu %s interval=[%d,%d] leaves<=%zu prefix=[%s]",
                                          branch->id,
                                          homeworlds_backend_goal_branch_kind_name(branch->kind),
                                          branch->interval_min,
                                          branch->interval_max,
                                          branch->leaf_upper_bound,
                                          prefix);

    if (homeworlds_backend_goal_branch_is_cutoff_skipped(buffer, branch, &cutoff)) {
      context->goal_branches_skipped++;
      homeworlds_backend_goal_report_append(context,
                                            "skip #%zu cutoff=%d interval=[%d,%d] prefix=[%s]",
                                            branch->id,
                                            cutoff,
                                            branch->interval_min,
                                            branch->interval_max,
                                            prefix);
      homeworlds_backend_goal_branch_free(branch);
      continue;
    }

    created_before_split = context->goal_branches_created;
    if (branch->leaf_upper_bound > HOMEWORLDS_GOAL_BRANCH_SMALL_LEAF_LIMIT &&
        !homeworlds_backend_goal_try_split_branch(root_position, &queue, context, branch, &split)) {
      homeworlds_backend_goal_branch_free(branch);
      ok = FALSE;
      break;
    }
    if (split) {
      homeworlds_backend_goal_report_append(context,
                                            "split-result #%zu created+=%zu queue=%u",
                                            branch->id,
                                            context->goal_branches_created - created_before_split,
                                            queue.branches->len);
      homeworlds_backend_goal_branch_free(branch);
      continue;
    }

    if (homeworlds_backend_goal_queue_best_bound(&queue, root_position->turn, &second_best_bound) &&
        !homeworlds_backend_goal_branch_split_score_interval(&queue,
                                                             context,
                                                             branch,
                                                             root_position->turn,
                                                             second_best_bound)) {
      homeworlds_backend_goal_branch_free(branch);
      ok = FALSE;
      break;
    }

    ok = homeworlds_backend_goal_explore_branch(context, buffer, branch);
    homeworlds_backend_goal_branch_free(branch);
  }

  homeworlds_backend_goal_queue_clear(&queue);
  return ok;
}

static gboolean homeworlds_backend_score_after_move(const HomeworldsPosition *position,
                                                    const HomeworldsMove *move,
                                                    gint *out_score) {
  HomeworldsPosition child = {0};
  GameBackendOutcome outcome = GAME_BACKEND_OUTCOME_ONGOING;

  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(out_score != NULL, FALSE);

  homeworlds_position_copy(&child, position);
  if (!homeworlds_position_apply_move(&child, move)) {
    g_debug("Skipping invalid Homeworlds move while static-pruning good_moves()");
    homeworlds_position_clear(&child);
    return FALSE;
  }

  outcome = homeworlds_position_outcome(&child);
  *out_score = outcome == GAME_BACKEND_OUTCOME_ONGOING
      ? homeworlds_position_evaluate_static(&child)
      : homeworlds_position_terminal_score(outcome, 1);
  homeworlds_position_clear(&child);
  return TRUE;
}

static gboolean homeworlds_backend_score_is_inside_prune_window(guint side, gint score, gint best_score) {
  g_return_val_if_fail(side < 2, FALSE);

  if (side == 0) {
    return score >= best_score - HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_WINDOW;
  }
  return score <= best_score + HOMEWORLDS_GOOD_MOVE_STATIC_PRUNE_WINDOW;
}

static void homeworlds_backend_trace_good_moves(const HomeworldsPosition *position,
                                                guint depth_hint,
                                                guint side,
                                                gsize generated_leaves,
                                                gsize scored_moves,
                                                gsize kept_moves,
                                                const HomeworldsGoodMoveContext *context) {
  g_return_if_fail(position != NULL);
  g_return_if_fail(context != NULL);

  if (homeworlds_backend_good_move_trace_func == NULL) {
    return;
  }

  HomeworldsGoodMoveTrace trace = {
    .position = position,
    .depth_hint = depth_hint,
    .side = side,
    .generated_leaves = generated_leaves,
    .scored_moves = scored_moves,
    .kept_moves = kept_moves,
    .pruning_checked_branches = context->pruning_checked_branches,
    .pruning_window_cutoff_branches = context->pruning_window_cutoff_branches,
    .pruning_pruned_branches = context->pruning_pruned_branches,
    .ordering_candidate_lists = context->ordering_candidate_lists,
    .ordering_reordered_candidate_lists = context->ordering_reordered_candidate_lists,
    .ordering_reordered_candidates = context->ordering_reordered_candidates,
    .ordering_single_step_passes = context->ordering_single_step_passes,
    .ordering_single_step_moves = context->ordering_single_step_moves,
    .goal_branches_created = context->goal_branches_created,
    .goal_branches_selected = context->goal_branches_selected,
    .goal_branches_split = context->goal_branches_split,
    .goal_branches_requeued = context->goal_branches_requeued,
    .goal_branches_direct = context->goal_branches_direct,
    .goal_branches_skipped = context->goal_branches_skipped,
    .goal_branches_exhausted = context->goal_branches_exhausted,
    .goal_report = context->goal_report != NULL ? context->goal_report->str : NULL,
  };

  homeworlds_backend_good_move_trace_func(&trace, homeworlds_backend_good_move_trace_user_data);
}

static GameBackendMoveList homeworlds_backend_list_good_moves(gconstpointer position, guint depth_hint) {
  const HomeworldsPosition *homeworlds_position = position;
  GameBackendMoveBuilder builder = {0};
  HomeworldsGenerationContext generation_context = {0};
  HomeworldsGoodMoveContext context = {0};
  HomeworldsMoveBuffer buffer = {0};
  HomeworldsMove *moves = NULL;
  gsize count = 0;

  g_return_val_if_fail(homeworlds_position != NULL, (GameBackendMoveList){0});

  homeworlds_backend_move_buffer_init(&buffer, homeworlds_position);
  if (!homeworlds_move_builder_init(homeworlds_position, &builder)) {
    homeworlds_backend_move_buffer_clear(&buffer);
    return (GameBackendMoveList){0};
  }
  context.root_catastrophe_count = homeworlds_backend_collect_profitable_catastrophes(
      builder.builder_state,
      context.root_catastrophes,
      G_N_ELEMENTS(context.root_catastrophes));
  homeworlds_generation_context_init(&generation_context);
  if (!homeworlds_backend_goal_collect_good_moves(homeworlds_position,
                                                  builder.builder_state,
                                                  &generation_context,
                                                  &context,
                                                  &buffer)) {
    homeworlds_move_builder_clear(&builder);
    homeworlds_backend_move_buffer_clear(&buffer);
    if (context.goal_report != NULL) {
      g_string_free(context.goal_report, TRUE);
    }
    return (GameBackendMoveList){0};
  }

  count = buffer.count;
  moves = homeworlds_backend_move_buffer_copy_moves(&buffer);
  homeworlds_backend_trace_good_moves(homeworlds_position,
                                      depth_hint,
                                      homeworlds_position->turn,
                                      buffer.leaves_seen,
                                      buffer.scored_moves,
                                      count,
                                      &context);

  homeworlds_move_builder_clear(&builder);
  homeworlds_backend_move_buffer_clear(&buffer);
  if (context.goal_report != NULL) {
    g_string_free(context.goal_report, TRUE);
  }
  return (GameBackendMoveList){
    .moves = moves,
    .count = count,
  };
}

static gboolean homeworlds_backend_stream_moves(gconstpointer position,
                                                GameBackendMoveStreamFunc stream_func,
                                                gpointer user_data) {
  const HomeworldsPosition *homeworlds_position = position;

  g_return_val_if_fail(homeworlds_position != NULL, FALSE);
  g_return_val_if_fail(stream_func != NULL, FALSE);

  return homeworlds_position_stream_all_moves(homeworlds_position, stream_func, user_data);
}

static gboolean homeworlds_backend_apply_move(gpointer position, gconstpointer move) {
  HomeworldsPosition *homeworlds_position = position;
  const HomeworldsMove *homeworlds_move = move;

  g_return_val_if_fail(homeworlds_position != NULL, FALSE);
  g_return_val_if_fail(homeworlds_move != NULL, FALSE);

  return homeworlds_position_apply_move(homeworlds_position, homeworlds_move);
}

static gint homeworlds_backend_evaluate_static(gconstpointer position) {
  const HomeworldsPosition *homeworlds_position = position;

  g_return_val_if_fail(homeworlds_position != NULL, 0);

  return homeworlds_position_evaluate_static(homeworlds_position);
}

static gint homeworlds_backend_terminal_score(gconstpointer position, GameBackendOutcome outcome, guint ply_depth) {
  g_return_val_if_fail(position != NULL, 0);

  return homeworlds_position_terminal_score(outcome, ply_depth);
}

static guint64 homeworlds_backend_hash_position(gconstpointer position) {
  const HomeworldsPosition *homeworlds_position = position;

  g_return_val_if_fail(homeworlds_position != NULL, 0);

  return homeworlds_position_hash(homeworlds_position);
}

static gboolean homeworlds_backend_format_move(gconstpointer move, char *buffer, gsize size) {
  const HomeworldsMove *homeworlds_move = move;

  g_return_val_if_fail(homeworlds_move != NULL, FALSE);

  return homeworlds_move_format(homeworlds_move, buffer, size);
}

static gboolean homeworlds_backend_parse_move(const char *notation, gpointer out_move) {
  HomeworldsMove *homeworlds_move = out_move;

  g_return_val_if_fail(notation != NULL, FALSE);
  g_return_val_if_fail(homeworlds_move != NULL, FALSE);

  return homeworlds_move_parse(notation, homeworlds_move);
}

const GameBackend homeworlds_game_backend = {
  .id = "homeworlds",
  .display_name = "Homeworlds",
  .variant_count = 0,
  .position_size = sizeof(HomeworldsPosition),
  .move_size = sizeof(HomeworldsMove),
  .supports_move_list = FALSE,
  .supports_move_builder = TRUE,
  .supports_ai_search = TRUE,
  .supports_ascii_game_io = TRUE,
  .ascii_game_file_description = "Homeworlds text game files",
  .ascii_game_file_extension = "txt",
  .side_label = homeworlds_backend_side_label,
  .sgf_color_for_side = homeworlds_backend_sgf_color_for_side,
  .outcome_banner_text = homeworlds_backend_outcome_banner_text,
  .position_init = homeworlds_backend_position_init,
  .position_clear = homeworlds_backend_position_clear,
  .position_copy = homeworlds_backend_position_copy,
  .position_outcome = homeworlds_backend_position_outcome,
  .position_turn = homeworlds_backend_position_turn,
  .stream_moves = homeworlds_backend_stream_moves,
  .list_good_moves = homeworlds_backend_list_good_moves,
  .move_list_free = homeworlds_backend_move_list_free,
  .move_list_get = homeworlds_backend_move_list_get,
  .moves_equal = homeworlds_backend_moves_equal,
  .moves_equivalent = homeworlds_backend_moves_equivalent,
  .move_builder_init = (gboolean (*)(gconstpointer, GameBackendMoveBuilder *)) homeworlds_move_builder_init,
  .move_builder_clear = homeworlds_move_builder_clear,
  .move_builder_list_candidates = (GameBackendMoveList (*)(const GameBackendMoveBuilder *))
      homeworlds_move_builder_list_candidates,
  .move_builder_step = (gboolean (*)(GameBackendMoveBuilder *, gconstpointer)) homeworlds_move_builder_step,
  .move_builder_is_complete = (gboolean (*)(const GameBackendMoveBuilder *)) homeworlds_move_builder_is_complete,
  .move_builder_build_move = (gboolean (*)(const GameBackendMoveBuilder *, gpointer))
      homeworlds_move_builder_build_move,
  .apply_move = homeworlds_backend_apply_move,
  .evaluate_static = homeworlds_backend_evaluate_static,
  .terminal_score = homeworlds_backend_terminal_score,
  .hash_position = homeworlds_backend_hash_position,
  .format_move = homeworlds_backend_format_move,
  .parse_move = homeworlds_backend_parse_move,
  .sgf_apply_setup_node = homeworlds_sgf_position_apply_setup_node,
  .sgf_write_position_node = homeworlds_sgf_position_write_position_node,
  .supports_square_grid_board = FALSE,
};
