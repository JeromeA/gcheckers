# Add Buildability Effects to Homeworlds Goal Branch Contracts

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds. This document follows `doc/PLANS.md`.

## Purpose / Big Picture

Homeworlds `good_moves()` splits expensive yellow sacrifices into tactical goal branches, but those branches currently
only describe scheduled catastrophes. Any possible buildability improvement remains folded into a broad optimistic
score bound, so reports still show branches like `goal=H1g!` with wide ranges even though the missing points come from
whether buildability changes. After this change, yellow-sacrifice goal branches will have complete effect contracts:
required catastrophes, explicitly required buildability gains or losses, and explicit exclusion of every discovered
effect that is not named by the branch.

The visible result is in `build/tools/homeworlds_proof_probe --iterations ...`: yellow branches that used to say
`goal=[H1g!]` can split into narrower contracts such as `goal=[H1g! +b+ +r+]`, `goal=[H1g!]`, or `goal=[-b+]`
depending on what effects the position can plausibly produce. A missing catastrophe or buildability token in a branch
label is part of the contract: if `H1g!` is omitted, that branch rejects moves that trigger `H1g!`; if `+b+` and `-b+`
are omitted, that branch keeps blue buildability unchanged.

## Progress

- [x] (2026-07-05 09:54Z) Read the existing goal-tree and constrained-yellow ExecPlans, the Homeworlds backend goal
  structures, buildability helpers, and relevant report tests.
- [x] (2026-07-05 09:54Z) Wrote this ExecPlan with the selected design: a two-phase contract generator enumerates
  complete effect combinations first, then rejects impossible contradictions before enqueueing branches.
- [x] (2026-07-05 10:20Z) Added buildability contract masks to goal branches and active branch exploration.
- [x] (2026-07-05 10:20Z) Changed yellow-sacrifice splitting to enumerate catastrophe and buildability-effect
  combinations, with terminal wins represented as absorbing win-only branches.
- [x] (2026-07-05 10:20Z) Updated completed-leaf filtering so omitted discovered buildability changes are rejected.
- [x] (2026-07-05 10:20Z) Updated yellow branch bounds so only required buildability changes contribute to the bound.
- [x] (2026-07-05 10:20Z) Updated focused backend tests and Homeworlds move-generation docs.
- [x] (2026-07-05 12:44Z) Ran focused probe/eval tests, built all binaries with `make`, and ran
  `make test-local` unsandboxed. Focused tests and `make` passed; `make test-local` is blocked by the existing
  `/gcheckers-window/library-loads-imported-game` GTK fatal null warning, which reproduced in isolation.

## Surprises & Discoveries

- Observation: The current wide yellow bounds come from optimistic buildability upside added outside the branch goal.
  Evidence: `homeworlds_backend_goal_branch_apply_yellow_goal_bound()` adds
  `(4 - current_buildable_color_count) * buildable_color_value` to every yellow goal branch.

- Observation: Terminal branches must not exclude buildability changes.
  Evidence: The first implementation required `goal=[H2y!]` while excluding discovered `+b+` and `-y+`, causing the
  known winning branch to score no leaves and letting non-terminal branches remain in the buffer. Changing terminal
  contracts to require only the win restored the terminal-stop behavior in `tests/test_homeworlds_backend.c`.

- Observation: Full local validation is blocked by the known GTK teardown failure.
  Evidence: `make test-local` failed on `/gcheckers-window/library-loads-imported-game` with
  `GLib-GObject-FATAL-CRITICAL: invalid (NULL) pointer instance`, and the same test failed when rerun directly in
  isolation.

## Decision Log

- Decision: A yellow branch contract is complete over every discovered effect.
  Rationale: The user wants absence to be meaningful. This makes branches disjoint and makes a branch label a precise
  promise instead of a hint.
  Date/Author: 2026-07-05 / Codex

- Decision: Buildability effects are represented as `+b+` for adding blue buildability and `-b+` for losing blue
  buildability, with the same pattern for red, yellow, and green.
  Rationale: This matches the requested notation and keeps buildability distinct from ship-building actions.
  Date/Author: 2026-07-05 / Codex

- Decision: Generate yellow contracts in two phases: enumerate all combinations of discovered effects, then reject
  contradictions that are certainly impossible.
  Rationale: Exhaustive enumeration is the safer first implementation because it cannot miss a valid combination. Cheap
  filtering controls obvious branch explosion, and later work can add stronger constructive feasibility checks.
  Date/Author: 2026-07-05 / Codex

- Decision: A terminal winning catastrophe is not combined with any other effect.
  Rationale: A terminal win score is absolute; once a branch reaches a win, other static score effects cannot improve
  or be summed with it.
  Date/Author: 2026-07-05 / Codex

- Decision: Terminal win branches require the win but do not exclude buildability changes or non-terminal
  catastrophes.
  Rationale: Those effects are irrelevant after the win and filtering them out can hide the winning move. Non-terminal
  branches still exclude terminal wins so the split remains useful.
  Date/Author: 2026-07-05 / Codex

## Outcomes & Retrospective

Implemented the core complete-contract split. Yellow sacrifices now split by discovered catastrophe effects plus final
buildability gain/loss effects. Branch labels list required effects, omitted discovered effects are excluded, and
`goal=[no scheduled effect]` means no discovered effect may occur. Required buildability changes tighten the branch
bound, while excluded buildability changes no longer create optional optimistic upside. Focused backend, proof-probe,
and eval-experiment tests pass, and `make` passes. Full `make test-local` remains blocked by the existing
`/gcheckers-window/library-loads-imported-game` GTK fatal null warning reproducing in isolation.

## Context and Orientation

The main implementation is in `src/games/homeworlds/homeworlds_backend.c`. `HomeworldsGoalBranch` currently stores
`HomeworldsGoalCatastrophe` entries plus required and excluded bit masks. Those entries drive yellow goal splitting,
report labels, active goal filtering, and constrained exploration. Buildability helpers already exist:
`homeworlds_backend_buildable_color_mask_for_side()` computes which colors the moving side can build now, and
`homeworlds_backend_future_catastrophe_buildable_gain_ceiling()` estimates buildability upside from a future
catastrophe.

A buildable color means the moving side has access to green at a system containing one of its own ships of that color.
For example, `+b+` means blue was not buildable at the start of the branch but is buildable at the completed leaf.
`-b+` means blue was buildable at the start of the branch but is no longer buildable at the completed leaf.

The proof report is produced by `src/homeworlds_proof_probe.c`, but the source report lines come from the backend's
goal report buffer. Tests in `tests/test_homeworlds_backend.c` assert branch labels and bounds; tests in
`tests/test_homeworlds_proof_probe.c` assert probe formatting.

## Plan of Work

First, replace the branch's catastrophe-only storage with a complete contract structure. Keep catastrophe goals as a
typed effect, and add buildability-change effects with a color and direction. The branch should keep separate required
and excluded masks over all discovered effects. A listed effect is required; an omitted discovered effect is excluded.
The report label should list only required effects for readability, but `goal=[no scheduled effect]` must mean all
discovered catastrophes and buildability changes are excluded.

Second, update yellow-sacrifice splitting. Discover possible positive catastrophe effects as today, and discover
possible buildability gain/loss effects from a conservative source: compare the branch start buildability mask with
buildability masks seen after each legal immediate child step, and include color gains and losses that can appear.
Then enumerate all complete contracts over those discovered effects. For each color, do not allow both `+c+` and
`-c+` in the same contract. For terminal winning catastrophe effects, make one branch requiring the terminal win and
excluding every other discovered effect, then enumerate non-terminal combinations separately.

Third, compute bounds from required effects only. Required catastrophes contribute their stored gain. Required
buildability gains and losses contribute `buildable_color_value` with the correct sign for the moving side. Optional
effects that are excluded by the contract must not widen the branch. This narrows the old buildability-wide ranges.

Fourth, enforce the contract on completed leaves. The existing catastrophe filter should check that every required
catastrophe happened and every excluded catastrophe did not happen. Add a final buildability-mask comparison between
the branch start and the completed move result; require listed `+c+` or `-c+` changes, and reject omitted discovered
buildability changes. This ensures branch disjointness even when exploration is not yet fully constructive.

Fifth, keep constrained required-catastrophe exploration for required catastrophe goals. Buildability-only or
buildability-combined branches may initially use normal legal exploration plus the completed-leaf contract filter. This
is correct but not fully proof-oriented; future improvements can add constructive buildability solvers once the report
shows which contracts dominate.

Finally, update tests and docs. Add tests proving that the report includes buildability contract tokens, that omitted
buildability changes are excluded, that terminal win branches do not combine with buildability effects, and that bounds
no longer include unrequired buildability upside. Update `doc/OVERVIEW.md` and the Homeworlds move-generation docs if
they describe yellow goal branches.

## Concrete Steps

Work from `/home/jerome/Data/gcheckers`.

Focused commands during development:

    make build/tests/test_homeworlds_backend build/tests/test_homeworlds_proof_probe build/tools/homeworlds_proof_probe
    build/tests/test_homeworlds_backend
    build/tests/test_homeworlds_proof_probe
    build/tests/test_homeworlds_eval_experiment
    build/tools/homeworlds_proof_probe --iterations=16 big_move_report_108.txt

Final validation:

    make
    make test-local

`make test-local` must run with escalated permissions so GTK tests use the existing local display.

Validation results from 2026-07-05:

    make build/tests/test_homeworlds_backend build/tests/test_homeworlds_proof_probe \
      build/tests/test_homeworlds_eval_experiment build/tools/homeworlds_proof_probe
    build/tests/test_homeworlds_backend
    build/tests/test_homeworlds_proof_probe
    build/tests/test_homeworlds_eval_experiment
    make

Those commands passed. `make test-local` was run unsandboxed and failed on
`/gcheckers-window/library-loads-imported-game`; direct isolated rerun reproduced the same GTK fatal null warning.

## Validation and Acceptance

Acceptance requires the focused Homeworlds tests and probe tests to pass. A probe report for `big_move_report_108.txt`
should show yellow-sacrifice branches whose `goal=[...]` labels contain buildability effects where applicable and whose
bounds no longer include buildability changes that are excluded by the contract. Full validation requires `make` and
`make test-local`.

## Idempotence and Recovery

The change is source-only. If branch enumeration creates too many branches, keep the two-phase model but add a
conservative fallback: when discovered effects exceed a fixed internal cap, emit one broad branch with the old
catastrophe-only contract and a report note. Do not drop discovered effects silently, because that would break the
meaning that omitted effects are excluded.

## Artifacts and Notes

Current important files:

    src/games/homeworlds/homeworlds_backend.c
    src/homeworlds_proof_probe.c
    tests/test_homeworlds_backend.c
    tests/test_homeworlds_proof_probe.c
    doc/OVERVIEW.md
    doc/homeworlds-move-generation.md

## Interfaces and Dependencies

No public command-line options are planned. The backend's trace struct remains API-compatible except for report text
content. New helpers should remain `static` in `src/games/homeworlds/homeworlds_backend.c`.

Revision note, 2026-07-05 09:54Z: Created the plan before implementation to record the complete-contract semantics,
the requested buildability notation, and the conservative two-phase split strategy.

Revision note, 2026-07-05 12:44Z: Recorded the implemented complete-contract split, the terminal-win filtering fix,
and the validation outcome including the GTK local-test blocker.
