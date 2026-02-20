# Make SGF Tree The Timeline Source Of Truth

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This plan is maintained according to PLANS.md at the repository root (`PLANS.md`).

## Purpose / Big Picture

After this change, move chronology and navigation are owned by `SgfTree` rather than inferred from `Game` history. A
played move is now processed in SGF-first order: validate, append under SGF current, advance SGF current, then project
that SGF transition to the game model. When SGF selection jumps to a non-child node, the model resets and replays from
root to the selected SGF node. This is observable by running SGF controller and window tests and seeing consistent SGF
branching/replay behavior without history-size polling.

## Progress

- [x] (2026-02-20 08:18Z) Replaced SGF scroller API surface with one public scroll function and internal retry path.
- [x] (2026-02-20 08:42Z) Added SGF scroller diagnostics for node-widget map snapshots at rebuild and selection-miss
  points.
- [x] (2026-02-20 08:56Z) Removed `Game` move history storage and moved last-move overlay dependency to model-level
  cached last move.
- [x] (2026-02-20 09:00Z) Refactored `GCheckersSgfController` to SGF-first move application and SGF->model transition
  syncing (single-step parent->child vs reset+replay).
- [x] (2026-02-20 09:02Z) Rewired board/window move paths to call SGF controller APIs instead of mutating model/game
  directly.
- [x] (2026-02-20 09:03Z) Updated tests, docs (`BUGS.md`, `src/OVERVIEW.md`), and validated with `make -j$(nproc)` and
  `make test`.

## Surprises & Discoveries

- Observation: `sgf_view_set_tree()` had a same-pointer fast path that rebuilt view widgets without refreshing selected
  raw pointer from the tree current node.
  Evidence: Debug logs showed root node pointer changes after reset with immediate `selected widget missing` lookups.

- Observation: GTK test commands still report Chromium crashpad socket warnings in this environment.
  Evidence: `make test` printed crashpad `setsockopt` warnings while still completing and returning success.

## Decision Log

- Decision: Keep `GCheckersModel::gcheckers_model_peek_last_move()` but back it with model-level cached move, not
  `Game` history.
  Rationale: Board overlay needs last-move rendering, but full game timeline ownership moved to SGF.
  Date/Author: 2026-02-20 / Codex

- Decision: Route board input through a move callback into `GCheckersSgfController` (`board_view_set_move_handler`) so
  board clicks no longer call `gcheckers_model_apply_move()` directly.
  Rationale: Enforces SGF-first transition order at the UI boundary.
  Date/Author: 2026-02-20 / Codex

- Decision: Remove SGF append-from-history polling (`last_history_size`) and model state-change coupling in SGF
  controller.
  Rationale: Polling inverted ownership and caused SGF/game drift risks; SGF must be explicit timeline authority.
  Date/Author: 2026-02-20 / Codex

## Outcomes & Retrospective

- Outcome: SGF timeline is now authoritative and explicit. Move application, random AI stepping, and SGF navigation all
  flow through SGF controller transition logic.
- Outcome: `Game` no longer stores historical move arrays; model overlay behavior remains intact through cached last
  move.
- Remaining gap: GTK display-dependent assertions are still skipped in this CI-like environment.

## Context and Orientation

Relevant modules:

- `src/sgf_tree.c`, `src/sgf_tree.h`: SGF node graph and current-node pointer.
- `src/gcheckers_sgf_controller.c`, `src/gcheckers_sgf_controller.h`: SGF authority and SGF<->model projection logic.
- `src/checkers_model.c`, `src/checkers_model.h`: model API and state-change emission.
- `src/game.c`, `src/game.h`: core game state transitions (now without history buffer).
- `src/board_selection_controller.c`, `src/board_view.c`, `src/gcheckers_window.c`: board and window input/control
  paths.
- `tests/test_gcheckers_sgf_controller.c`, `tests/test_gcheckers_window.c`, `tests/test_checkers_model.c`,
  `tests/test_game.c`: regression and behavior tests for the refactor.

## Plan of Work

First, remove game-history ownership and ensure model still exposes last move for overlay rendering. Next, rewrite SGF
controller so SGF selection/move operations directly drive model transitions, including optimized parent-child one-step
application and fallback full replay. Then route board clicks and auto-move requests through SGF controller APIs to
enforce SGF-first move order. Finally, update tests and architecture docs to reflect new ownership.

## Concrete Steps

From repository root:

1) Build all binaries:

   make -j$(nproc)

2) Run test suite:

   make test

Expected outcomes:

- Build completes with `-Werror` and no compilation warnings.
- Test binaries pass; GTK display-dependent tests may report SKIP in headless mode.

## Validation and Acceptance

Acceptance criteria:

- A legal move applied through SGF controller appears as a payloaded SGF child of current and advances SGF current.
- SGF navigation to non-child nodes replays model state from root path.
- Window and board paths use SGF controller apply/step APIs (no direct model move application in those paths).
- `make -j$(nproc)` and `make test` succeed.

## Idempotence and Recovery

This refactor is source-only and idempotent: rerunning builds/tests is safe. If SGF/model synchronization fails during
future edits, restore invariants by checking that every move entry point calls SGF controller APIs and that
`gcheckers_sgf_controller_sync_model_for_transition()` still handles both transition modes.

## Artifacts and Notes

Observed validation commands:

  make -j$(nproc)
  make test

Both commands completed successfully after the refactor.

## Interfaces and Dependencies

Key resulting interfaces:

- `gboolean gcheckers_sgf_controller_apply_move(GCheckersSgfController *self, const CheckersMove *move);`
- `gboolean gcheckers_sgf_controller_step_random_move(GCheckersSgfController *self, CheckersMove *out_move);`
- `void board_view_set_move_handler(BoardView *self, BoardViewMoveHandler handler, gpointer user_data);`
- `void board_selection_controller_set_move_handler(BoardSelectionController *self, BoardSelectionControllerMoveHandler
  handler, gpointer user_data);`

Plan updates:
- 2026-02-20: Replaced prior board-view refactor plan with SGF-source-of-truth execution record to match implemented
  architecture changes requested in this task.
