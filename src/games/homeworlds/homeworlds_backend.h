#ifndef HOMEWORLDS_BACKEND_H
#define HOMEWORLDS_BACKEND_H

#include "game_backend.h"
#include "homeworlds_move_builder.h"
#include "homeworlds_types.h"

typedef enum {
  HOMEWORLDS_GOOD_MOVE_PRUNING_OFF = 0,
  HOMEWORLDS_GOOD_MOVE_PRUNING_ON,
  HOMEWORLDS_GOOD_MOVE_PRUNING_VERIFY,
} HomeworldsGoodMovePruningMode;

typedef enum {
  HOMEWORLDS_GOOD_MOVE_PROOF_NOT_ACTIVE = 0,
  HOMEWORLDS_GOOD_MOVE_PROOF_NOT_PLAY,
  HOMEWORLDS_GOOD_MOVE_PROOF_COMPLETE,
  HOMEWORLDS_GOOD_MOVE_PROOF_UNSUPPORTED_WEIGHTS,
  HOMEWORLDS_GOOD_MOVE_PROOF_UNCERTAIN,
  HOMEWORLDS_GOOD_MOVE_PROOF_KEEP,
  HOMEWORLDS_GOOD_MOVE_PROOF_REJECT,
} HomeworldsGoodMoveProofResult;

typedef struct {
  const HomeworldsPosition *position;
  guint depth_hint;
  guint side;
  gsize generated_leaves;
  gsize scored_moves;
  gsize kept_moves;
  HomeworldsGoodMovePruningMode pruning_mode;
  gsize pruning_checked_branches;
  gsize pruning_would_prune_branches;
  gsize pruning_pruned_branches;
  gsize pruning_verified_leaves;
  gsize pruning_verification_failures;
} HomeworldsGoodMoveTrace;

typedef struct {
  HomeworldsGoodMoveProofResult result;
  guint pending_actions_remaining;
  guint step_count;
  gint cutoff;
  gint current_score;
  gint buildable_gain;
  guint catastrophe_gain;
  gint bound;
} HomeworldsGoodMoveProofStatus;

typedef void (*HomeworldsGoodMoveTraceFunc)(const HomeworldsGoodMoveTrace *trace, gpointer user_data);

extern const GameBackend homeworlds_game_backend;

void homeworlds_backend_set_good_move_trace(HomeworldsGoodMoveTraceFunc trace_func, gpointer user_data);
gboolean homeworlds_backend_describe_large_yellow_sacrifice_proof(const HomeworldsMoveBuilderState *state,
                                                                  guint side,
                                                                  gint cutoff,
                                                                  HomeworldsGoodMoveProofStatus *out_status);

#endif
