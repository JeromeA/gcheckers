# Rewrite Homeworlds Good Moves as a Goal Tree

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document follows `doc/PLANS.md` from the repository root.

## Purpose / Big Picture

Homeworlds can have millions of legal turn continuations from a single position. The current `good_moves()` code starts
from legal move exploration and tries to prune while it walks, so it often spends most of its time enumerating moves that
cannot enter the final 512-move static-prune buffer. After this change, `good_moves()` starts from tactical goals. It
creates branches with score ranges, explores the branch band that can currently improve the buffer, and skips branches
whose upper bound can no longer matter.

The behavior is visible through the existing Homeworlds move-count diagnostics and big move reports: they will include
goal-tree branch creation, selection, splitting, requeueing, direct exploration, and cutoff skips. The returned move set
may change, but kept moves must still obey the existing static score quality window.

## Progress

- [x] (2026-06-23 13:07Z) Read `doc/PLANS.md`, the current Homeworlds backend traversal, the move builder, trace
  consumers, and relevant tests.
- [x] (2026-06-23 13:07Z) Wrote this ExecPlan with the selected design: production uses only the goal-tree scheduler,
  small branches use an internal cutoff of 50 leaves, and detailed branch reports are emitted through diagnostics.
- [x] (2026-06-23 13:18Z) Replaced the root `good_moves()` entrypoint with a goal-branch queue and branch scheduler.
- [x] (2026-06-23 13:18Z) Represented partial exploration by splitting score intervals and requeueing the remaining
  interval.
- [x] (2026-06-23 13:18Z) Preserved existing AI policy pruning by calling the current child-state predicates for every
  branch child.
- [x] (2026-06-23 13:21Z) Added branch trace counters and detailed goal-tree report output for eval reports and probes.
- [x] (2026-06-23 13:25Z) Updated tests, `doc/homeworlds-move-generation.md`, and `doc/OVERVIEW.md`.
- [x] (2026-06-23 13:29Z) Built all binaries with `make` and ran `make test-local` unsandboxed.

## Surprises & Discoveries

- Observation: The existing dedupe context is sacrifice-scoped and is stack-owned by the recursive traversal.
  Evidence: `homeworlds_generation_prepare_child_context()` receives a caller-owned `HomeworldsGenerationDedupe` and
  makes descendants point at it only after a sacrifice. The goal tree must not store dangling pointers to those stack
  objects.

- Observation: Existing simple prune cases are already centralized enough to reuse.
  Evidence: `homeworlds_backend_child_state_is_good_after_step()` rejects last-homeworld-ship movement/sacrifice,
  redundant small sacrifices, unfavorable build/trade catastrophes, and moving/discovering into unfavorable
  catastrophes.

- Observation: An optimal root catastrophe cannot always be physically prepended in the generated move.
  Evidence: the existing canonicalization test expects a sacrifice-first spelling such as `H1g2- S0r! ...`; applying
  `S0r!` before entering the branch explorer causes the sacrifice branch to be pruned as a catastrophe-before-sacrifice
  spelling. The implemented “root catastrophe now” branch therefore means “the root catastrophe is forced by policy”
  while preserving canonical sacrifice-first move spellings.

- Observation: The broad local test run still hit the known GTK teardown/import flake once.
  Evidence: `make test-local` reported `/gcheckers-window/library-loads-imported-game` as
  `GLib-GObject-FATAL-CRITICAL: invalid (NULL) pointer instance`, reran the isolated path, passed it, and ignored the
  non-reproducible local failure as configured.

## Decision Log

- Decision: Production will not keep an old-recursion runtime toggle.
  Rationale: The user explicitly asked to remove old optional modes in recent related work, and selected immediate
  replacement here. Tests will validate score quality rather than exact move identity.
  Date/Author: 2026-06-23 / Codex

- Decision: Partial exploration is represented by branch interval splitting, not by a mutable “half-explored” status.
  Rationale: A split branch is easier to reason about: one branch records the score interval just explored, while a
  second branch records the remaining interval and can be ordered against all other branches.
  Date/Author: 2026-06-23 / Codex

- Decision: The small-branch direct-exploration threshold is a named internal constant set to 50.
  Rationale: The user wanted 50 as the v1 value and did not want another runtime mode.
  Date/Author: 2026-06-23 / Codex

- Decision: Root-like branches do not use a heuristic priority bound as a hard score interval.
  Rationale: A root or root-catastrophe branch is too broad for a depth-limited ordering estimate to be treated as a
  complete score range. Specialized branches may still use proof/order bounds, but root-like branches keep the full
  score interval until the scheduler explicitly splits them.
  Date/Author: 2026-06-23 / Codex

## Outcomes & Retrospective

Implemented the v1 goal-tree scheduler. `good_moves()` now seeds a queue with a root branch, splits root catastrophe,
single-step, and sacrifice branches, explores high score bands first, requeues remaining score intervals, and skips
branches that cannot reach the current cutoff. The branch explorer still uses the legal builder and existing policy
predicates, so legal move generation remains unchanged.

Diagnostics now include goal branch counters and a bounded text report in eval big-move reports. Focused tests cover
score-quality behavior, branch counters, score splitting/requeueing, and the updated eval/probe output. Validation
passed with `make`, focused Homeworlds/eval/probe tests, and `make test-local` unsandboxed; the local target ignored one
known non-reproducible GTK failure after the isolated rerun passed.

## Context and Orientation

`src/games/homeworlds/homeworlds_backend.c` owns the Homeworlds AI move policy. The legal move builder in
`src/games/homeworlds/homeworlds_move_builder.c` remains the source of legal next choices. A `HomeworldsMoveBuilderState`
contains a working position, a partially built move, the current builder stage, and sacrifice action state. The backend
walks those states to collect `good_moves()`, which is the AI subset of legal moves.

The existing backend already has useful policy helpers. The move buffer keeps at most 512 moves during play and only
keeps moves within 50 static-eval points of the best static score found so far. Profitable catastrophe helpers identify
catastrophes that are positive from the moving side's perspective. The yellow-sacrifice proof computes an optimistic
bound for a branch after any yellow sacrifice. These helpers should be reused rather than rewritten.

A goal branch is a scheduled unit of move generation. It owns a copied builder state, a generation context, a
side-aware score interval, a conservative upper bound on leaf count, and a reason string for diagnostics. A branch can
be split into children by tactical category, or split into score intervals. A score interval uses normal integer static
scores: for player 1, higher is better; for player 2, lower is better.

## Plan of Work

First, add internal goal-tree types to `homeworlds_backend.c`: branch kinds, branch objects, a branch queue, side-aware
score-bound helpers, a detailed-report buffer, and trace counters. Extend `HomeworldsGoodMoveTrace` in
`src/games/homeworlds/homeworlds_backend.h` with branch counters and a transient `goal_report` string.

Second, adapt the current recursive collector into a branch explorer. It should still call the legal builder and the
existing child-state policy predicates, but it should accept an active score interval from the current goal branch.
Completed leaves outside the active interval are scored and then discarded before entering the move buffer. Large yellow
proof pruning should use the stricter of the buffer cutoff and the active interval cutoff.

Third, implement root branch splitting. If positive catastrophes exist at the beginning of a turn, create immediate
catastrophe branches and, only when the catastrophe is improvable, a postponed branch. Otherwise split the normal
select-ship root into single-step branches and sacrifice branches. Redundant simple sacrifices and other existing cheap
prunes must be rejected at branch creation by the existing child-state policy.

Fourth, implement score-band splitting in the scheduler. When the best branch has bound `+50` and the next-best branch
has bound `+30`, explore only the `+30` through `+50` score band and enqueue the remaining lower band as a separate
branch. This same logic is side-aware for player 2, where lower scores are better. If a bound is unknown or infinite,
explore the branch as a whole rather than creating useless infinite score slices.

Finally, update diagnostics, tests, and docs. Existing tests that assert exact counts may need to assert score quality
instead. New tests should verify interval splitting, branch counters, goal reports, and preservation of redundant green
sacrifice pruning.

## Concrete Steps

Work from `/home/jerome/Data/gcheckers`.

Use `apply_patch` for edits. Useful focused commands during the implementation are:

    make test_homeworlds_backend
    make test_homeworlds_eval_experiment
    make test_homeworlds_proof_probe

Before completion, run:

    make
    make test-local

`make test-local` must be run with escalated permissions so GTK tests use the local display, as described in
`AGENTS.md`.

## Validation and Acceptance

The change is accepted when the project builds, the focused Homeworlds tests pass, and `make test-local` passes or only
reports GTK cases that pass in isolation under the existing local-test behavior.

The backend trace must show nonzero goal-tree branch counters on a nontrivial play position. Big move reports must
include a human-readable goal-tree report. The static-prune tests must still prove that every kept move is inside the
same 50-point score window around the best kept move for the side to move.

## Idempotence and Recovery

All code changes are local source edits and can be safely re-run through the test commands. If the branch scheduler
misorders or over-prunes moves, inspect the detailed goal report first, then temporarily lower the branch split logic by
marking a branch as directly explorable while keeping the same child-state policy predicates. Do not change legal move
generation to fix an AI policy issue.

## Artifacts and Notes

The most important current code anchors are:

    src/games/homeworlds/homeworlds_backend.c
    src/games/homeworlds/homeworlds_backend.h
    src/homeworlds_eval_experiment.c
    src/homeworlds_proof_probe.c
    tests/test_homeworlds_backend.c

## Interfaces and Dependencies

`HomeworldsGoodMoveTrace` must keep existing fields and gain branch diagnostics. The `goal_report` field is transient:
it is valid only while the trace callback is running. Consumers that need it after the callback must copy it.

The legal move builder interface remains unchanged. No new runtime environment variable or user-visible mode is added.

Revision note, 2026-06-23 13:07Z: Created the plan before implementation to satisfy the repository requirement for
significant refactors and to record the score-interval split decision.

Revision note, 2026-06-23 13:29Z: Updated progress, discoveries, decisions, and outcome after implementing and
validating the goal-tree scheduler.
