# Add GTK move-selection parity with the CLI

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This plan is maintained according to PLANS.md at the repository root (`PLANS.md`).

## Purpose / Big Picture

After this change, the GTK UI shows the full checkers board and lets the user play the same moves as the CLI by
clicking one of their pieces and then clicking one or more destination squares. The UI only accepts moves that appear
in the engine-provided move list and prints both the player and computer moves to the console. You can see it working
by running `./gcheckers`, clicking a white piece followed by valid destinations, and observing that the console logs the
user move and the AI reply while the board updates in the window.

## Progress

- [x] (2026-01-18 18:35Z) Review the existing GTK window, model APIs, and CLI move flow to map the requirements to
  current code.
- [x] (2026-01-18 18:35Z) Extend the model API to list moves, apply only valid moves, and report random AI moves, with
  updated unit tests.
- [x] (2026-01-18 18:35Z) Build a GTK board grid with selectable squares, move validation via prefixes, and AI
  responses.
- [x] (2026-01-18 18:35Z) Update the GTK window to print user/AI moves to the console and keep the status label in sync.
- [x] (2026-01-18 18:35Z) Run `make all` and `make test`, then commit the changes.

## Surprises & Discoveries

- Observation: The existing GTK UI had no move selection logic, so the model needed a stricter move-application API to
  enforce "only from the move list" while keeping the UI simple.
  Evidence: The previous model only exposed `gcheckers_model_step_random_move`, which could not validate user
  selections.

## Decision Log

- Decision: Implement move validation in `GCheckersModel` so the UI can only apply moves from the authoritative move
  list.
  Rationale: Centralizing move validation in the model prevents invalid UI state and matches CLI behavior.
  Date/Author: 2026-01-18 / Codex.
- Decision: Represent the board as a GTK grid of buttons and labels instead of a custom drawing surface.
  Rationale: This keeps the UI simple, uses GTK widgets directly, and supports click handling without new drawing code.
  Date/Author: 2026-01-18 / Codex.

## Outcomes & Retrospective

- Outcome: The GTK UI now renders the board, enforces legal move selection via click paths, and logs both player and AI
  moves to the console, matching the CLI flow.
  Notes: The model API gained list/apply functions, and tests now cover valid and invalid move application.

## Context and Orientation

The checkers engine lives in `src/game.c`, `src/move_gen.c`, and `src/board.c`. The CLI entrypoint
`src/checkers_cli.c` shows the move loop: list legal moves for the player, prompt for a choice, then let the AI choose a
random move. The GTK front-end uses `src/checkers_model.c` as a GObject wrapper around `Game` and
`src/gcheckers_window.c` for UI widgets. The existing GTK window previously displayed only a status label with
random-move buttons; it needed a board and click-based move selection. Tests for the model live in
`tests/test_checkers_model.c` and are run by `make test`.

## Plan of Work

Update `src/checkers_model.h` and `src/checkers_model.c` to expose a move list function, a validated move application
function, and a random-move function that can return the chosen move for logging. Update `tests/test_checkers_model.c`
to exercise the new APIs with a valid move, an invalid move, and a random move. Replace the GTK window contents in
`src/gcheckers_window.c` with a grid-based board that renders the current pieces, tracks a click path, validates
prefixes against the move list, and applies a move once the user has clicked a complete path. After applying the player
move, let the AI reply with a random legal move and print both moves to stdout. Ensure the status label and board update
on every state change.

## Concrete Steps

1) Update `src/checkers_model.h` and `src/checkers_model.c` to add move listing and validated move application helpers.
2) Update `tests/test_checkers_model.c` to cover the new API surface.
3) Replace the GTK window layout in `src/gcheckers_window.c` with a board grid, selection logic, and console logging.
4) From the repository root, run:

   make all
   make test

5) Commit the changes with a descriptive message.

## Validation and Acceptance

Run `make all` and expect `libgame.a`, `checkers`, and `gcheckers` to build without warnings. Run `make test` and expect
all tests to pass, including the updated `test_checkers_model`. Launch `./gcheckers`, click a white piece and one or
more legal destination squares, and confirm that the UI moves the piece only when the selection matches an available
move. The console should log lines like `Player plays: 9-14` and `AI plays: 22-18`, and the board should update after
each move.

## Idempotence and Recovery

All edits are additive and can be re-applied safely. If a build fails due to missing GTK development packages, install
GTK4 and rerun `make all` and `make test` without changing the source. `make clean` removes built artifacts if you need
a fresh rebuild.

## Artifacts and Notes

Example console output after a player move:

  Player plays: 9-14
  AI plays: 22-18

## Interfaces and Dependencies

`src/checkers_model.h` must expose:

  MoveList gcheckers_model_list_moves(GCheckersModel *self);
  gboolean gcheckers_model_apply_move(GCheckersModel *self, const CheckersMove *move);
  gboolean gcheckers_model_step_random_move(GCheckersModel *self, CheckersMove *out_move);

`src/gcheckers_window.c` must build a GTK grid for the board, update square labels from the `GameState`, and use the
model APIs to validate and apply moves. The UI should emit move logs with `game_format_move_notation` so the console
matches CLI formatting.

Plan updates:
- 2026-01-18: Replaced the previous GTK bootstrap plan with the new parity plan after implementing board rendering and
  click-based move selection.
