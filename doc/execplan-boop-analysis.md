# Enable boop analysis in the shared window

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document must be maintained in accordance with `doc/PLANS.md`.

## Purpose / Big Picture

After this change, the `GAME=boop` shared-shell build will expose the existing `Analysis` actions, and those actions
will produce per-node analysis results instead of remaining disabled. A user will be able to open `gboop`, analyze
the current position, run full-game analysis over the SGF tree, and see the right drawer populate with scored boop
moves just like the checkers build already does.

## Progress

- [x] (2026-04-29 00:00Z) Read `doc/PLANS.md`, `doc/OVERVIEW.md`, `src/window.c`, `src/ai_search.[ch]`,
      `src/sgf_controller.[ch]`, `src/game_app_profile.c`, and the boop window/backend tests to identify why boop
      analysis was disabled.
- [x] (2026-04-29 00:30Z) Refactor the shared analysis workers in `src/window.c` to use the generic backend AI API
      for current-position analysis and for non-checkers full-game analysis while keeping checkers' setup-aware
      replay behavior intact.
- [x] (2026-04-29 00:40Z) Enable boop's shared-shell analysis feature flag, add boop analysis regressions in
      `tests/test_window_boop.c`, extend score-format coverage in `tests/test_window.c`, and update
      `doc/OVERVIEW.md`.
- [x] (2026-04-29 00:55Z) Build `GAME=boop` plus the focused test binaries and run `test_game_backend`,
      `test_boop_backend`, `test_sgf_io`, `test_sgf_controller`, and `test_window`.
- [ ] (2026-04-29 00:55Z) Remaining validation gap: run the new GTK boop analysis smoke tests on a machine with a
      usable GTK display so they execute instead of skipping at `gtk_init_check()`.

## Surprises & Discoveries

- Observation: boop already advertises `supports_ai_search = TRUE`, so the missing piece is not backend search.
  Evidence: `src/games/boop/boop_backend.c` exports `.supports_ai_search = TRUE` and `tests/test_boop_backend.c`
  already exercises search callbacks.

- Observation: the shared analysis UI is still wired directly to checkers-only helper types.
  Evidence: `src/window.c` stores `CheckersAiTranspositionTable`, `CheckersAiSearchStats`, and calls
  `checkers_ai_alpha_beta_analyze_moves_cancellable_with_tt()` from both threaded analysis entry points.

- Observation: the generic SGF replay helper for backend positions replays SGF moves but not checkers-only setup
  properties.
  Evidence: `ggame_sgf_controller_replay_node_into_position()` in `src/sgf_controller.c` parses `B[]` and `W[]`
  properties only, while `ggame_sgf_controller_replay_node_into_game()` still applies `AE`, `AB`, `AW`, `ABK`,
  `AWK`, and `PL`.

- Observation: `make GAME=boop all` rebuilds the application binary but does not rebuild the test executables.
  Evidence: the first post-change `test_window` run still exposed the old seven boop paths until the explicit
  `make GAME=boop build/tests/test_window ...` invocation rebuilt the test targets.

- Observation: the new boop window smoke tests are registered correctly but this environment still skips GTK tests.
  Evidence: `./build/tests/test_window` now reports nine boop paths, including
  `/ggame-window/boop/current-position-analysis` and `/ggame-window/boop/full-game-analysis`, and both skip with
  `GTK display not available`.

## Decision Log

- Decision: the shared current-position analysis path will move fully to `src/ai_search.c`.
  Rationale: that path already searches through `GameBackend` callbacks, so switching it once enables boop without
  changing visible checkers behavior.
  Date/Author: 2026-04-29 / Codex

- Decision: full-game analysis will keep the checkers replay path for checkers and use backend-position replay for
  boop and other generic models.
  Rationale: this avoids regressing checkers setup-root analysis while still enabling boop, which does not currently
  rely on checkers-only SGF setup properties.
  Date/Author: 2026-04-29 / Codex

## Outcomes & Retrospective

The code now enables boop analysis in the shared window. `src/window.c` no longer depends on checkers-only search
wrappers for current-position analysis, boop now advertises `supports_analysis = TRUE`, and the boop window test file
contains explicit current-position and full-game analysis smoke tests. The remaining gap is environmental rather than
code-level: those new GTK smoke tests still skip here because no usable display is available, so they need one
follow-up run on a machine with GTK display support.

## Context and Orientation

The active game backend is selected at compile time. `src/game_app_profile.c` decides which features the shared GTK
window enables for the current build. `src/window.c` owns the `Analysis` menu actions, the analysis drawer widgets,
and the worker threads that publish `SgfNodeAnalysis` payloads back into the SGF tree. `src/ai_search.c` is the
generic alpha-beta search implementation used by backends through `GameBackend` callbacks. `src/sgf_controller.c`
owns the helpers that reconstruct a node position from SGF history.

Before this work, boop already had a searchable backend but the boop profile still reported
`supports_analysis = FALSE`, and the worker code in `src/window.c` still depended on checkers-only search wrappers and
replay types. That combination left the boop analysis actions disabled even though the backend could evaluate moves.

## Plan of Work

Start in `src/window.c`. Replace the checkers-only analysis task fields with backend-generic search state:
`GameAiTranspositionTable`, `GameAiSearchStats`, and a copied backend-owned position buffer for current-position
analysis. Convert the helper that turns scored moves into `SgfNodeAnalysis` so it accepts a `GameBackend` plus a
`GameAiScoredMoveList`, and format move text through `backend->format_move`.

For full-game analysis, keep two replay modes. Checkers will continue to rebuild a `Game` through
`ggame_sgf_controller_replay_node_into_game()` so SGF setup nodes still behave exactly as they do today. Non-checkers
will allocate a backend position with `position_init()`, replay the SGF node with
`ggame_sgf_controller_replay_node_into_position()`, and analyze that position through the generic AI search API.

After the shared worker path is generic enough, flip boop's `supports_analysis` feature flag in
`src/game_app_profile.c`. Update the boop window tests so they expect enabled analysis actions, keep the hidden-by-
default analysis drawer expectation, and add at least one smoke test that proves the current-position or full-game
analysis action actually attaches analysis results to SGF nodes. Update `doc/OVERVIEW.md` so it explains that the
shared analysis drawer is now available to boop as well.

## Concrete Steps

Work from the repository root:

  1. Edit `doc/execplan-boop-analysis.md`, `src/window.c`, `src/game_app_profile.c`, `tests/test_game_backend.c`,
     `tests/test_window.c`, `tests/test_window_boop.c`, and `doc/OVERVIEW.md`.
  2. Build with `make GAME=boop all`.
  3. Rebuild the focused test binaries with
     `make GAME=boop build/tests/test_window build/tests/test_game_backend build/tests/test_boop_backend
     build/tests/test_sgf_controller build/tests/test_sgf_io`.
  4. Run `./build/tests/test_game_backend`, `./build/tests/test_boop_backend`, `./build/tests/test_sgf_io`,
     `./build/tests/test_sgf_controller`, and `./build/tests/test_window`.

## Validation and Acceptance

Acceptance is behavioral:

- In a boop build, `analysis-current-position` and `analysis-whole-game` are enabled.
- Triggering current-position analysis on the initial boop position attaches an `SgfNodeAnalysis` payload to the
  current SGF node.
- Triggering full-game analysis on a short boop line attaches analysis to the traversed nodes without crashing.
- `make GAME=boop all` succeeds, the rebuilt focused non-GTK tests pass, and the shared `test_window` binary exposes
  the two new boop analysis test paths.

## Idempotence and Recovery

The code changes are additive and can be re-applied safely by re-running the same build and focused test commands. If
the shared analysis refactor breaks checkers behavior, the safest recovery path is to keep the generic worker changes
but restore the checkers replay branch for full-game analysis before retrying the boop tests.

## Artifacts and Notes

The most important artifact is `tests/test_window_boop.c`, which now includes
`/ggame-window/boop/current-position-analysis` and `/ggame-window/boop/full-game-analysis` so the feature is covered
beyond a simple profile-flag toggle.

## Interfaces and Dependencies

`src/window.c` must depend on the generic search interfaces from `src/ai_search.h`:

  GameAiScoredMoveList
  GameAiSearchStats
  GameAiTranspositionTable
  game_ai_search_analyze_moves_cancellable_with_tt()
  game_ai_scored_move_list_free()
  game_ai_tt_new()
  game_ai_tt_free()

The full-game path must continue using:

  ggame_sgf_controller_replay_node_into_game()
  ggame_sgf_controller_replay_node_into_position()

Revision note (2026-04-29 / Codex): initial ExecPlan written after surveying the shared analysis path, the generic
search API, and the boop window/profile tests.

Revision note (2026-04-29 / Codex): updated after implementation to record the generic worker refactor, the explicit
test-binary rebuild step, the passing focused command set, and the remaining GTK-display validation gap.
