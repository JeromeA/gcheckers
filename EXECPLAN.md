# Add a GTK4 gcheckers application binary

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This plan is maintained according to PLANS.md at the repository root (`PLANS.md`).

## Purpose / Big Picture

After this change, a user can launch a GTK4 desktop application named `gcheckers` that shows the current game status and
lets them advance the game with a button. The application uses GObject-based (object-oriented C) types to manage game
state and the GTK window, and it builds as a new binary alongside the existing CLI. You can see it working by running the
new `gcheckers` binary, observing a window with a status label, and clicking buttons to step moves or reset the game.

## Progress

- [x] (2025-09-19 03:18Z) Review current build system, game core files, and testing setup to decide where GTK4 and
  GObject code should live.
- [x] (2025-09-19 03:41Z) Add a GObject-based model that wraps `Game`, emits a state-changed signal, and supports reset
  and random move stepping, with unit tests.
- [x] (2025-09-19 03:46Z) Add GTK4 application and window classes that bind to the model and expose controls and a status
  label.
- [x] (2025-09-19 03:52Z) Update the Makefile to build the new `gcheckers` binary with GTK4, include new tests, and run
  the full test suite.
- [x] (2025-09-19 03:54Z) Update this ExecPlan with final progress, outcomes, and any surprises, then commit.

## Surprises & Discoveries

- Observation: No unexpected behavior encountered during GTK4 wiring or model tests.
  Evidence: `make test` completed successfully on the first run.

## Decision Log

- Decision: Build a lightweight GTK4 UI that exposes status text plus buttons to step random moves and reset the game.
  Rationale: Keeps the UI straightforward while still demonstrating a working GTK4 binary integrated with the game
  logic.
  Date/Author: 2025-09-19 / Codex.

## Outcomes & Retrospective

- Outcome: Added a GTK4 `gcheckers` binary backed by a GObject model, plus a new unit test and Makefile updates.
  Notes: The new UI exposes basic controls and status text while keeping the core game logic reusable and testable.

## Context and Orientation

The existing codebase is a C checkers engine with a CLI front-end. The core game logic lives in `src/game.c` and
`src/board.c`, with `src/checkers_cli.c` as the current binary entrypoint and `Makefile` defining build and test targets.
Tests live under `tests/` and are compiled directly with the source files. The new GTK4 binary should be built in
parallel with the CLI and should reuse the existing game logic via a new GObject model wrapper.

## Plan of Work

First, create a new GObject-based model in `src/checkers_model.c` and `src/checkers_model.h` that owns a `Game`, provides
methods to reset and apply a random move, and emits a `state-changed` signal when the game state updates. Add a unit test
`tests/test_checkers_model.c` that constructs the model, confirms initial state, steps a move, and resets the game. Next,
add GTK4 types: a `GCheckersApplication` in `src/gcheckers_application.c`/`.h` that subclasses `GtkApplication`, and a
`GCheckersWindow` in `src/gcheckers_window.c`/`.h` that subclasses `GtkApplicationWindow`. The window should create a
status label and buttons for "Play Random Move" and "Reset", listening to the model's signal to refresh the label.
Finally, add a `src/gcheckers.c` main file that instantiates the application, update the `Makefile` to build the new
binary with GTK4 and GObject flags, include the new test in the `test` target, and run `make test` before committing.

## Concrete Steps

1) In the repository root, create `src/checkers_model.h` and `src/checkers_model.c` with the GObject wrapper for `Game`.
   Ensure the model emits a `state-changed` signal and provides methods for reset and random moves.
2) Add `tests/test_checkers_model.c` with assert-based checks for initial winner/turn, a random move application, and
   reset behavior.
3) Create `src/gcheckers_application.h` and `.c` for a `GtkApplication` subclass, plus `src/gcheckers_window.h` and `.c`
   for a `GtkApplicationWindow` subclass that wires the model to the UI.
4) Add `src/gcheckers.c` to run the GTK application.
5) Update `Makefile` to add GTK4 and GObject pkg-config flags, a `gcheckers` binary target, the new test target, and add
   `gcheckers` to `all`.
6) From the repository root, run:

   make test

   Expect all tests (including `test_checkers_model`) to pass, and use `make gcheckers` to build the GTK4 binary.

## Validation and Acceptance

Run `make test` and expect all existing tests plus `test_checkers_model` to pass. Then run `./gcheckers` and observe a GTK
window with a status label and two buttons. Clicking "Play Random Move" should advance the turn and update the status
label. Clicking "Reset" should return the game to its initial state with no winner. The status label should reflect the
current player and winner after each action.

## Idempotence and Recovery

All steps are additive and can be rerun safely. If a build fails due to missing GTK4 dependencies, rerun after installing
GTK4 development packages and keep the source changes intact. You can clean outputs with `make clean` and rerun `make
all`.

## Artifacts and Notes

Example command transcript after implementation:

  $ make test
  ./test_game
  ./test_game_print
  ./test_board
  ./test_move_gen
  ./test_checkers_model
  All tests passed.

## Interfaces and Dependencies

The new model should expose the following interface in `src/checkers_model.h`:

  G_DECLARE_FINAL_TYPE(GCheckersModel, gcheckers_model, GCHECKERS, MODEL, GObject)
  GCheckersModel *gcheckers_model_new(void);
  void gcheckers_model_reset(GCheckersModel *self);
  gboolean gcheckers_model_step_random_move(GCheckersModel *self);
  char *gcheckers_model_format_status(GCheckersModel *self);
  const GameState *gcheckers_model_peek_state(GCheckersModel *self);

The GTK4 application should subclass `GtkApplication` and create a `GtkApplicationWindow` subclass that accepts a
`GCheckersModel` instance, constructs a label and buttons, connects signals for button actions, and updates the label when
`state-changed` fires. The build should use `pkg-config --cflags --libs gtk4` and `gobject-2.0` to ensure proper GTK4 and
GObject linkage.

Plan updates:
- 2025-09-19: Initial plan created to deliver the GTK4 gcheckers binary with a GObject model and test coverage.
- 2025-09-19: Updated progress, outcomes, and discoveries after implementing the GTK4 binary and running tests.
