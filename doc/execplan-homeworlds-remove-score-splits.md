# Remove Homeworlds Score-Split Exploration

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds. This document follows `doc/PLANS.md`.

## Purpose / Big Picture

Homeworlds `good_moves()` currently has two different ways to split work: it can split a branch by tactical goals, and
it can split the same branch again by score interval. The user wants only tactical goal splits. After this change, a
selected branch is either split into explicit goal branches or explored in full. This makes reports easier to interpret
and removes the misleading behavior where exploration finds legal leaves and then discards them solely because their
score is outside an artificial score band.

The visible proof is a `homeworlds_proof_probe --iterations ...` report without score-split lines or outside-interval
filters, plus the backend tests continuing to pass.

## Progress

- [x] (2026-06-28 12:44Z) Read `doc/PLANS.md` and located score-split code in
  `src/games/homeworlds/homeworlds_backend.c`.
- [x] (2026-06-28 13:05Z) Removed active score-interval filtering from recursive move collection.
- [x] (2026-06-28 13:05Z) Removed score-band branch requeueing and bucket helpers.
- [x] (2026-06-28 13:15Z) Updated proof probe parsing/reporting and tests to no longer expect score split concepts.
- [x] (2026-06-28 13:20Z) Updated Homeworlds move-generation docs and repository overview.
- [x] (2026-06-28 13:25Z) Ran focused validation for the affected Homeworlds backend, proof probe, and eval
  experiment tests.
- [x] (2026-06-28 14:25Z) Ran full build and attempted full local validation; `make test-local` still hits the
  unrelated GTK `library-loads-imported-game` fatal critical in-suite, while the exact isolated command passes
  manually.

## Surprises & Discoveries

- Observation: Score splitting is not only a reporting layer. `homeworlds_backend_goal_explore_branch()` installs an
  active score interval into `HomeworldsGoodMoveContext`, and `homeworlds_backend_move_buffer_append_scored()` filters
  already-scored leaves against that interval.
  Evidence: `context->has_score_interval` and `goal_rejected_score_intervals` appear in both recursive collection and
  proof-probe report parsing.
- Observation: Removing score-band requeueing can change eval-experiment game outcomes because more equal-policy moves
  are considered in a different order before random tie selection.
  Evidence: The `reports-timeouts` fixture now produces the timeout row `5,0,0,,0,2` instead of the previous decisive
  row.

## Decision Log

- Decision: Keep branch lower and upper score estimates for ordering, cutoff skipping, and report context, but remove
  the active score interval that filters leaves.
  Rationale: The user asked to remove score split, not the useful static bounds that say whether a branch can reach the
  current buffer cutoff. Without score-interval filtering, a selected branch is explored in full unless a goal split is
  possible first.
  Date/Author: 2026-06-28, Codex.

## Outcomes & Retrospective

Implemented the cleanup with branch score bounds retained only for ordering, cutoff checks, and report context. The
scheduler now attempts tactical goal splits for large branches and otherwise explores the selected branch in full.
`homeworlds_proof_probe` now parses and prints `bounds=[min,max]`, with no score-split or outside-interval display.

Validation so far:

    make build/tools/homeworlds_proof_probe build/tests/test_homeworlds_backend \
      build/tests/test_homeworlds_proof_probe build/tests/test_homeworlds_eval_experiment
    build/tests/test_homeworlds_backend
    build/tests/test_homeworlds_proof_probe
    build/tests/test_homeworlds_eval_experiment
    build/tools/homeworlds_proof_probe --iterations=12 big_move_report_108.txt
    build/tools/homeworlds_proof_probe --iterations=12 homeworlds_3.3M_moves.txt
    make

`make test-local` was run twice with `GSETTINGS_BACKEND=memory GCHECKERS_ACCEPT_NONREPRO_LOCAL_FAILURES=1`. Both runs
failed in `/gcheckers-window/library-loads-imported-game` with `GLib-GObject-FATAL-CRITICAL: invalid (NULL) pointer
instance`; the exact isolated retry command
`GSETTINGS_BACKEND=memory build/tests/test_window --profile=checkers -p /gcheckers-window/library-loads-imported-game`
passes manually. This failure is outside the Homeworlds move-generation/proof-probe surface touched by this plan.

## Context and Orientation

The main code is in `src/games/homeworlds/homeworlds_backend.c`. A `HomeworldsGoalBranch` is a scheduled chunk of move
generation. It stores a position prefix, a kind such as root or yellow sacrifice, an estimated leaf count, optional
catastrophe goals, and score bounds. A goal split means creating child branches with explicit catastrophe contracts such
as "trigger `H2y!`" or "no scheduled catastrophe". A score split means taking one branch and exploring only a score
band, then requeueing the rest of the same branch for later. This plan removes score split.

`src/homeworlds_proof_probe.c` is a diagnostic tool that reads the backend's goal report and formats it for humans.
Tests in `tests/test_homeworlds_backend.c` and `tests/test_homeworlds_proof_probe.c` assert key report behavior.
`doc/homeworlds-move-generation.md` and `doc/OVERVIEW.md` describe the algorithm.

## Plan of Work

First, remove active score-interval fields and counters from `HomeworldsGoodMoveContext` and
`HomeworldsGoalCollectionSnapshot`. Delete the interval check in `homeworlds_backend_move_buffer_append_scored()` and
the interval cutoff injection in `homeworlds_backend_prepare_pruning_for_child()`.

Second, delete `homeworlds_backend_goal_branch_split_score_interval()` and the bucket helpers it uses. In the scheduler,
after goal splitting fails or is not needed, call `homeworlds_backend_goal_explore_branch()` directly.

Third, keep branch bounds as estimates. Rename report text from `interval=[min,max]` to `bounds=[min,max]` so the report
does not imply an active score filter. Leave the bound calculations in place because they still order branches and allow
cutoff skips.

Fourth, simplify `src/homeworlds_proof_probe.c`: parse `bounds=` where it previously parsed `interval=`, remove
score-split display, and remove outside-interval filter output.

Finally, update docs and tests to reflect that there are only goal splits and full branch exploration.

## Concrete Steps

Work from `/home/jerome/Data/gcheckers`.

Run focused checks during development:

    make test_homeworlds_backend test_homeworlds_proof_probe
    build/tests/test_homeworlds_backend
    build/tests/test_homeworlds_proof_probe

Run final checks:

    make
    make test-local

The local test target must run unsandboxed so GTK tests can use the local display.

## Validation and Acceptance

Acceptance requires `homeworlds_proof_probe --iterations` output to contain goal split lines and full explore-result
lines, but no `score split` or `outside-interval` text. Backend tests should still prove goal partitions exist, and the
test suite should pass with the known local non-reproducible GTK retry behavior accepted by `make test-local`.

## Idempotence and Recovery

The edits are source and documentation changes only. Re-running `make` and tests is safe. If a report test fails,
inspect the generated report text rather than reverting unrelated files; the worktree may contain unrelated local report
files.

## Artifacts and Notes

Artifacts will be recorded after validation.
