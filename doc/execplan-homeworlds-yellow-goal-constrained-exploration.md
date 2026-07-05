# Constrain Yellow Goal Branch Exploration

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds. This document follows `doc/PLANS.md`.

## Purpose / Big Picture

Homeworlds `good_moves()` already splits large yellow sacrifices into goal branches such as "make `H1g!` happen".
Today those branches still enumerate most of the sacrifice tree and reject completed moves that do not include the
goal. After this change, a yellow goal branch will first solve the required catastrophe goal by forcing the necessary
yellow move steps, then enumerate only the remaining sacrifice actions. A proof-probe report for the usual large case
should show far fewer `outside-goal` rejections for goal branches like `goal=H1g!`.

## Progress

- [x] (2026-07-04 10:10Z) Confirmed that `outside-goal` is counted before scoring completed leaves.
- [x] (2026-07-04 10:18Z) Read the yellow sacrifice goal discovery, builder candidate, and recursive traversal code.
- [x] (2026-07-04 11:08Z) Add a constrained yellow-goal solver that applies required move-away and move-in steps
  before full exploration.
- [x] (2026-07-04 11:16Z) Add tests proving required goal branches no longer enumerate unrelated leaves.
- [x] (2026-07-04 11:24Z) Update `doc/homeworlds-move-generation.md` and `doc/OVERVIEW.md`.
- [x] (2026-07-04 12:14Z) Run focused validation and record full validation status.

## Surprises & Discoveries

- Observation: The current `goal_filter_reject` counter rejects completed moves before scoring.
  Evidence: `homeworlds_backend_move_satisfies_active_goal_filter()` is called before
  `homeworlds_backend_move_buffer_append()` in `src/games/homeworlds/homeworlds_backend.c`.

- Observation: The constrained path removes the large required-branch `outside-goal` count from
  `big_move_report_108.txt`.
  Evidence: `build/tools/homeworlds_proof_probe --iterations=12 big_move_report_108.txt` now reports
  `goal=H1g!` branches with only duplicate, bad-step, or outside-window filters; the large `outside-goal` filters are
  on `goal=no scheduled catastrophe` branches.

- Observation: Required goal construction must check the branch's stored gain, not only the catastrophe label.
  Evidence: Some `H1g!` move-in children could create the label with more expensive own material than the goal bound
  allowed. Filtering child states by the remaining achievable gain removed required-branch `outside-goal` filters from
  the report.

## Decision Log

- Decision: Start with required yellow goal branches only, and leave excluded/no-goal branches on the generic traversal.
  Rationale: A required goal has a concrete target catastrophe. Exclusion means "avoid this event", which is not a
  constructive target and should remain a completed-move contract until there is a separate solver for it.
  Date/Author: 2026-07-04, Codex.

- Decision: A goal plan should include own ships that must move away to make the required catastrophe quality match the
  branch bound, then own same-color ships that must move in to bring the color count to four, and finally the
  catastrophe step itself.
  Rationale: Moving doomed own material away is part of reaching the branch's advertised goal quality, while moving
  enough same-color ships in is what makes a future catastrophe reachable.
  Date/Author: 2026-07-04, Codex.

## Outcomes & Retrospective

The first implementation is intentionally narrow and constructive. It handles pending required yellow goals when the
branch is at a forced-yellow select-ship stage. A required goal whose target system already has enough color material
is fired directly if its current gain is good enough; otherwise, candidate children move doomed own ships away to
improve the catastrophe quality. A required goal whose target system lacks enough color material constrains the next
step to direct one-hop same-color move-in candidates. Constructed children are kept only when the required goal can
still reach its stored gain after remaining material costs or quality-improving move-away steps. A missing target, an
insufficient number of remaining yellow actions, or no available quality-improving move-away child exhausts the branch.
The generic fallback remains only for required move-in goals whose route is not solved by the direct one-hop planner.

## Context and Orientation

The main implementation lives in `src/games/homeworlds/homeworlds_backend.c`. A `HomeworldsGoalBranch` stores a copied
move-builder state, optional catastrophe goals, required/excluded goal masks, score bounds, and a leaf estimate. When a
branch is explored, `homeworlds_backend_goal_explore_branch()` installs the branch goal masks into
`HomeworldsGoodMoveContext` and calls `homeworlds_backend_collect_good_moves_recursive()`.

The recursive collector uses `homeworlds_move_builder_list_candidates()` and `homeworlds_move_builder_step()` from
`src/games/homeworlds/homeworlds_move_builder.c`. A yellow sacrifice is represented by
`state->pending_actions_remaining > 0` and `state->forced_action_color == HOMEWORLDS_COLOR_YELLOW`; each forced action
can select one own ship and move it to a connected existing system or discover a connected star from the bank.

Goal discovery is currently in `homeworlds_backend_collect_yellow_sacrifice_goals()`. It records target system/color
pairs such as `H1g!`, but exploration later only checks that completed moves include those catastrophes.

## Plan of Work

Add helper functions in `src/games/homeworlds/homeworlds_backend.c` near the existing yellow proof helpers. The helpers
will build a small concrete plan for required yellow goals. A plan is a sequence of builder operations: select an own
ship, apply the forced yellow move to a target system, and optionally apply the now-available catastrophe. The first
implementation only handles direct one-step moves to an existing target system; this covers the reported `H1g!` case
and avoids pretending that two-hop or discovery routing is solved.

When a goal branch has required goals and is an active yellow sacrifice, `homeworlds_backend_goal_explore_branch()`
will try the constrained path before generic recursion. If the helper can prove and apply a complete goal plan, it
recurses from that constrained state. If the helper cannot plan a required goal, it falls back to the current full
branch traversal so correctness is preserved.

Add report counters or details only if needed by tests. The main observable change should be fewer `outside-goal`
filters on constrained goal branches.

## Concrete Steps

Work from `/home/jerome/Data/gcheckers`.

Run focused checks during development:

    make build/tests/test_homeworlds_backend build/tools/homeworlds_proof_probe
    build/tests/test_homeworlds_backend
    build/tools/homeworlds_proof_probe --iterations=12 big_move_report_108.txt

Run final checks:

    make
    make test-local

`make test-local` must run unsandboxed so GTK tests use the local display.

## Validation and Acceptance

Acceptance requires the existing Homeworlds backend tests to pass and a proof-probe report for
`big_move_report_108.txt` to show that a required yellow goal branch no longer reports a large `outside-goal` count
from enumerating unrelated leaves. The move set must remain legal: every kept move is still replayed and scored through
`homeworlds_position_apply_move()`.

## Idempotence and Recovery

The change is source-only and safe to rerun. If the constrained solver cannot find a plan, it falls back to the old
generic traversal. If a new test fails, inspect the captured `goal_report` before changing pruning logic.

## Artifacts and Notes

- `build/tests/test_homeworlds_backend` passed.
- `build/tests/test_homeworlds_proof_probe` passed.
- `make` passed.
- `build/tools/homeworlds_proof_probe --iterations=12 big_move_report_108.txt` shows no `outside-goal` filters on
  required `goal=H1g!` branches. The remaining `outside-goal` filters are on `goal=no scheduled catastrophe`
  exclusion branches.
- `build/tools/homeworlds_proof_probe --iterations=12 homeworlds_3.3M_moves.txt` still reaches the terminal
  `goal=H2y!` branch and keeps one winning move.

## Interfaces and Dependencies

No public API changes are planned. New helpers stay `static` inside `src/games/homeworlds/homeworlds_backend.c`.
