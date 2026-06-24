# Make Homeworlds Proof Pruning Always On

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document is maintained according to `doc/PLANS.md` from the repository root.

## Purpose / Big Picture

Homeworlds `good_moves()` currently has an environment-controlled proof pruning mode with `off`, `on`, and `verify`.
The profiling work showed that leaving pruning off by default makes AI profiling spend most time generating and scoring
moves that the proof can reject. After this change there is only one behavior: the yellow-sacrifice proof prunes
branches whenever it can. Users and profiling tools no longer need to remember an environment variable to get the fast
path.

## Progress

- [x] (2026-06-21T16:37Z) Read the backend, proof probe, eval experiment, and tests to find all pruning-mode and
  verification-mode references.
- [x] (2026-06-21T17:05Z) Collapsed backend pruning control to one unconditional pruning path.
- [x] (2026-06-21T17:14Z) Updated diagnostics, reports, and tests so they no longer expose pruning modes.
- [x] (2026-06-21T17:20Z) Built focused Homeworlds targets and ran focused backend/eval experiment tests.
- [x] (2026-06-21T17:45Z) Removed the candidate-ordering environment switch and the pre-512-only ordering gate.

## Surprises & Discoveries

- Observation: `homeworlds_backend_describe_yellow_sacrifice_proof()` is used by both pruning and candidate
  ordering.
  Evidence: `homeworlds_backend_candidate_order_set_proof_bound()` calls it independently of
  `homeworlds_backend_prepare_pruning_for_child()`.

## Decision Log

- Decision: Remove mode selection entirely instead of keeping an environment variable that accepts only `on`.
  Rationale: The user asked for pruning `on` to be the only mode and for dead code created by the old modes to be
  removed.
  Date/Author: 2026-06-21 / Codex

## Outcomes & Retrospective

`good_moves()` now always applies the yellow-sacrifice proof when a cutoff exists. The public trace keeps only
the counters that still describe production behavior: checked branches, score-window cutoff branches, and pruned
branches. Eval experiment reports and proof probe output no longer include pruning mode, would-prune, verified-leaf, or
verification-failure fields. Candidate ordering is also always active: normal single-step moves are collected before
sacrifices, and active yellow-sacrifice candidates are ordered by proof priority regardless of how full the
static-prune buffer already is.

## Context and Orientation

The Homeworlds backend lives in `src/games/homeworlds/homeworlds_backend.c` and exports diagnostic trace fields through
`src/games/homeworlds/homeworlds_backend.h`. The function `homeworlds_backend_list_good_moves()` builds the AI move
list by recursively walking the interactive move builder. The proof pruning logic is in
`homeworlds_backend_prepare_pruning_for_child()`: it computes the current cutoff from the move buffer, asks
`homeworlds_backend_yellow_sacrifice_bound_prunes()` whether a yellow sacrifice branch can still reach the
cutoff, and skips the branch when the proof says it cannot.

The old `verify` mode continued exploring branches that would have been pruned and counted whether any completed move
would have reached the cutoff. That state is threaded through recursive functions as
`HomeworldsGoodMovePruningVerification`. Once pruning is always on, that verification state, its counters, and its
warning path are dead code.

## Plan of Work

Edit `src/games/homeworlds/homeworlds_backend.h` to remove `HomeworldsGoodMovePruningMode` and trace fields that only
describe mode selection or verification. Keep trace fields for checked branches, score-window branches, and actual
pruned branches.

Edit `src/games/homeworlds/homeworlds_backend.c` to remove `GCHECKERS_HOMEWORLDS_GOOD_MOVE_PRUNING`, the mode parser,
the verification struct, and all recursive verification parameters. Make
`homeworlds_backend_prepare_pruning_for_child()` return only whether a child should be pruned.

Update `src/homeworlds_eval_experiment.c`, `src/homeworlds_proof_probe.c`, tests, `doc/OVERVIEW.md`, and
`doc/homeworlds-move-generation.md` so trace output and documentation describe unconditional pruning instead of
optional `off|on|verify` modes.

## Concrete Steps

From `/home/jerome/Data/gcheckers`, edit the files named above with `apply_patch`. Then run:

    make build/tests/test_homeworlds_backend build/tests/test_homeworlds_eval_experiment \
      build/tools/homeworlds_proof_probe build/tools/homeworlds_profile_moves
    build/tests/test_homeworlds_backend
    build/tests/test_homeworlds_eval_experiment

If the focused tests pass, run `git diff --check` and inspect `rg` output for stale pruning-mode references.

## Validation and Acceptance

The change is accepted when the code compiles without `HomeworldsGoodMovePruningMode`, without
`GCHECKERS_HOMEWORLDS_GOOD_MOVE_PRUNING`, and without verification counters. Focused Homeworlds backend and eval
experiment tests must pass. Diagnostic output may still report pruning counts, but it must not report a pruning mode or
verification failures.

## Idempotence and Recovery

The edits are source-only and can be reapplied safely. If compilation fails, use compiler errors and `rg` for stale
symbols such as `pruning_mode`, `verification`, `verified_leaves`, and `GOOD_MOVE_PRUNING`.

## Artifacts and Notes

The important validation artifacts will be added after tests run.

## Interfaces and Dependencies

`HomeworldsGoodMoveTrace` in `src/games/homeworlds/homeworlds_backend.h` must continue to expose:

    generated_leaves
    scored_moves
    kept_moves
    pruning_checked_branches
    pruning_window_cutoff_branches
    pruning_pruned_branches

No public pruning-mode type remains.
