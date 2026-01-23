# Refactor board view into composable components

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This plan is maintained according to PLANS.md at the repository root (`PLANS.md`).

## Purpose / Big Picture

After this change, the checkers board UI is decomposed into smaller GObject helpers so each responsibility (grid
layout, square content, move selection, piece palette, and last-move overlay rendering) is isolated and reusable. The
GTK app should behave the same, but the board view logic is now easier to test and maintain. You can see it working by
building the GTK app, running it, and confirming the board renders, selection highlights work, and last-move arrows are
still drawn.

## Progress

- [x] (2025-02-14 14:15Z) Capture current board view responsibilities and define new helper components without the
  GCheckers prefix.
- [x] (2025-02-14 15:55Z) Implement new helper objects, rename the board view type/files, and wire helpers into the
  window.
- [x] (2025-02-14 15:58Z) Update build wiring, run `make all` and `make test`, and finalize documentation/commit.

## Surprises & Discoveries

- Observation: The screenshot test invoked by `make test` emits D-Bus connection errors in this environment, but the
  test still completes and the overall command succeeds.
  Evidence: `make test` logs `Failed to connect to the bus` while still completing all test binaries and SKIPing GTK
  UI tests when no display is available.

## Decision Log

- Decision: Rename the board view type/files to `BoardView`/`board_view.{c,h}` and extract helper GObjects named
  `BoardGrid`, `BoardSquare`, `BoardSelectionController`, `BoardMoveOverlay`, and `PiecePalette`.
  Rationale: The user asked to remove the `GCheckers` prefix for class and file names while keeping responsibilities
  separated along natural UI boundaries.
  Date/Author: 2025-02-14 / Codex

## Outcomes & Retrospective

- Outcome: The board view was decomposed into helper GObjects with non-GCheckers names, and the GTK build/test suite
  completed successfully.

## Context and Orientation

The existing board view is implemented in `src/gcheckers_board_view.c` and exposed in
`src/gcheckers_board_view.h`. It owns GTK widget construction, input handling, and last-move drawing in a single file.
The GTK window in `src/gcheckers_window.c` creates the board view and calls its update/selection APIs. The build for the
GTK app is defined in `Makefile` under the `gcheckers` target, so any new sources must be listed there.

## Plan of Work

Start by renaming the board view type and file names to drop the `GCheckers` prefix. Then introduce new helper
GObject types under `src/` for the board grid, square content, piece palette, move selection controller, and last-move
overlay. Move existing logic into those helpers while keeping behavior intact. Update the board view implementation to
own these helpers and delegate responsibilities. Update `src/gcheckers_window.c` to use the renamed board view type.
Finally, update the Makefile sources to include the new files and remove the old names.

## Concrete Steps

1) Replace `src/gcheckers_board_view.{c,h}` with `src/board_view.{c,h}` and rename the type and APIs accordingly.
2) Add helper sources/headers under `src/` for `board_grid`, `board_square`, `board_selection_controller`,
   `board_move_overlay`, and `piece_palette` and migrate logic from the old board view implementation.
3) Update `src/gcheckers_window.c` to include the renamed board view header and use the new type names.
4) Update the `gcheckers` target in `Makefile` to compile the new source files and remove the old ones.
5) From the repository root, run:

   make all
   make test

6) Update this plan with progress, discoveries, and decisions, then commit.

## Validation and Acceptance

The GTK app should still render the checkers board, allow square selection with correct highlight styling, and draw the
last move arrow overlay. `make all` should build all binaries without warnings, and `make test` should pass, with UI
tests still behaving as before.

## Idempotence and Recovery

The refactor is additive and safe to repeat. If compilation fails, re-run `make all` after adjusting helper APIs and
include paths. If behavior regresses, compare the old board view logic to the helper implementations and fix the
delegate responsible for the regression.

## Artifacts and Notes

None yet.

## Interfaces and Dependencies

The new helper types should be GObject final types in `src/board_*.{c,h}` and `src/piece_palette.{c,h}`. The board view
should own each helper and expose the same public API as before (renamed to `board_view_*`). The helpers should accept
existing GTK widgets, `GCheckersModel` pointers, and board sizing inputs rather than owning the model logic directly,
except where model access is required for move selection or last-move rendering.

Plan updates:
- 2025-02-14: Created plan for board view refactor into helper objects with non-GCheckers names.
- 2025-02-14: Marked implementation and validation steps complete after running build and tests.
