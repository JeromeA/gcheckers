# Conservative Bounds for Non-Yellow Homeworlds Sacrifices

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds. This document follows `doc/PLANS.md`.

## Purpose / Big Picture

Homeworlds `good_moves()` now uses a goal scheduler and strong yellow-sacrifice bounds, but red, green, and blue
sacrifice branches often keep an unbounded best side of `[-2147483648,...]` or `[...,2147483647]`. In zero-window
eval experiments this makes branches such as a blue sacrifice enumerate hundreds of leaves even though every leaf is
outside the current best-score window. After this change, red, green, and blue sacrifices get conservative optimistic
best bounds. If even that optimistic score cannot reach the cutoff, the branch is skipped before leaf enumeration.

The visible result is in big move reports: more non-yellow sacrifice branches have finite bounds and more of them end
with `skip #... cutoff=...` instead of an `explore-result` with many `window_reject` leaves. Returned good moves remain
scored by the existing exact scoring path.

## Progress

- [x] (2026-07-05 13:35Z) Inspected the current yellow proof, generic branch estimate path, non-yellow leaf-count
  helpers, and the big-move report case that fully explores `S0b3-`.
- [x] (2026-07-05 14:20Z) Added conservative best-bound helpers for red, green, and blue sacrifices.
- [x] (2026-07-05 14:20Z) Wired the new bound into branch estimates and candidate ordering without adding goal splits.
- [x] (2026-07-05 14:25Z) Added focused backend tests and updated Homeworlds documentation.
- [x] (2026-07-05 15:15Z) Ran focused tests, built all binaries, and ran `make test-local` unsandboxed.

## Surprises & Discoveries

- Observation: The existing leaf upper-bound code already counts forced red, green, and blue action choices plus
  reachable positive catastrophes, but it does not compute a score bound for those colors.
  Evidence: `homeworlds_backend_estimate_forced_sacrifice_leaf_upper_bound()` calls color-specific counters, while
  `homeworlds_backend_goal_branch_update_estimate()` only applies proof bounds for yellow.

- Observation: The pass-floor helper gives non-yellow sacrifices a finite pass score on the worse side, but not a
  useful optimistic best side.
  Evidence: Side 1 blue branches can show `bounds=[-2147483648,-260]`, so the cutoff check cannot skip them.

- Observation: The full local test suite can be quiet for a long time while still running.
  Evidence: `/homeworlds/view/text-panel-fixed-width` completed successfully after about 2590 seconds during
  `make test-local`.

## Decision Log

- Decision: Keep this change conservative-only.
  Rationale: The user explicitly chose not to add goal-contract splitting for red, green, and blue in this pass.
  Date/Author: 2026-07-05 / Codex

- Decision: A red sacrifice bound counts capture upside, not destruction.
  Rationale: Red attacks capture opponent ships and do not create new catastrophes. The bound should model the score
  upside from ownership changes and existing positive catastrophes only.
  Date/Author: 2026-07-05 / Codex

- Decision: A blue sacrifice bound gives no material-value upside for the trade itself.
  Rationale: Blue trades preserve ship size. Only buildability changes and catastrophes caused by the traded color can
  improve static score.
  Date/Author: 2026-07-05 / Codex

## Outcomes & Retrospective

Implemented. Red, green, and blue sacrifice branches now receive conservative optimistic best bounds when the code can
prove one safely. Red counts capture upside rather than destruction. Green counts build material and buildability
upside. Blue counts buildability upside only. Green and blue also include positive catastrophe upside when existing
feasibility helpers can prove the forced actions can create it; uncertain cases remain unbounded.

The focused backend test now verifies that a bounded blue sacrifice branch can be skipped by the zero-window cutoff.
Full validation passed:

    make build/tests/test_homeworlds_backend build/tests/test_homeworlds_eval_experiment
    build/tests/test_homeworlds_backend
    build/tests/test_homeworlds_eval_experiment
    make
    make test-local

## Context and Orientation

The implementation is in `src/games/homeworlds/homeworlds_backend.c`. Branch bounds live on `HomeworldsGoalBranch` as
`score_min_bound` and `score_max_bound`; for side 0 the best bound is the maximum, and for side 1 it is the minimum.
`homeworlds_backend_goal_branch_can_reach_cutoff()` uses that best side to skip queued branches. Yellow sacrifices
already use `homeworlds_backend_describe_yellow_sacrifice_proof()` to compute such a best bound.

The non-yellow change should be source-local and static. It should reuse existing static-evaluation helpers:
`homeworlds_backend_ship_eval_value()`, `homeworlds_backend_future_catastrophe_gain_ceiling()`,
`homeworlds_backend_existing_catastrophe_gain_ceiling()`, and
`homeworlds_backend_buildable_color_mask_for_side()`.

## Plan of Work

First, add a helper that computes an optimistic improvement for an active red, green, or blue sacrifice. The helper
returns `FALSE` only for real errors; it returns an `uncertain` flag when a safe finite bound is unavailable. Its
result is a non-negative improvement relative to the current static score after the sacrifice prefix.

Second, implement color-specific improvement ceilings. Red sums the best capture values for legal attacks, capped by
remaining forced actions, and adds currently available positive catastrophe upside. Green sums the best build values
for legal builds, capped by bank availability and remaining forced actions, and adds optimistic positive catastrophe
upside when build-count feasibility is already proven by the existing helpers. Blue gives no trade material credit; it
adds possible buildability gain from same-size bank-available trades and positive catastrophe upside when the existing
trade-count feasibility proves the needed trades can be made.

Third, convert that improvement into a side-aware best bound with the existing score convention. For side 0, optimistic
best is `current_score + improvement`; for side 1, it is `current_score - improvement`. Feed it into
`homeworlds_backend_goal_branch_set_best_bound()` from `homeworlds_backend_goal_branch_update_estimate()`. Also let
`homeworlds_backend_candidate_order_set_proof_bound()` use the same helper so branch ordering can use the bound while
recursing, but keep yellow behavior unchanged.

Fourth, keep all exact scoring and legal move filtering unchanged. The bound may overestimate and miss pruning, but it
must not underestimate. If any weight or feasibility case is hard to bound safely, mark the bound uncertain and leave
the branch unbounded.

Finally, add tests and docs. Tests should prove that at least one non-yellow sacrifice branch that used to enumerate
window-rejected leaves is now skipped, and that returned good-move scores are still exact. Update `doc/OVERVIEW.md`
and `doc/homeworlds-move-generation.md` if they describe sacrifice pruning.

## Concrete Steps

Work from `/home/jerome/Data/gcheckers`.

Focused commands:

    make build/tests/test_homeworlds_backend build/tests/test_homeworlds_eval_experiment
    build/tests/test_homeworlds_backend
    build/tests/test_homeworlds_eval_experiment

Final validation before committing:

    make
    make test-local

`make test-local` must run with escalated permissions so GTK tests use the existing local display.

## Validation and Acceptance

Acceptance requires focused Homeworlds backend and eval-experiment tests to pass. A report for a position like
`big_move_report_001.txt` should show the `S0b3-`-style blue sacrifice branch with a finite best bound and a skip when
the zero-window cutoff is already better than that bound. Full validation requires `make` and `make test-local`,
subject to the known local GTK isolation behavior.

## Idempotence and Recovery

The change is source-only. If a bound looks risky, keep that color or case uncertain rather than forcing a finite
estimate. Re-running reports and tests is safe. Diagnostic `big_move_report_*.txt` files are local artifacts and
should not be staged unless explicitly requested.

## Artifacts and Notes

Expected modified files:

    src/games/homeworlds/homeworlds_backend.c
    tests/test_homeworlds_backend.c
    doc/OVERVIEW.md
    doc/homeworlds-move-generation.md
    doc/execplan-homeworlds-nonyellow-sacrifice-bounds.md

Revision note, 2026-07-05 13:35Z: Created the plan after choosing the conservative-only version and correcting the
red/blue tactical terminology.

Revision note, 2026-07-05 15:15Z: Completed implementation and validation. The local `make test-local` run passed
after a long GTK view test.
