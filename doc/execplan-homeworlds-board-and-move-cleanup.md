# Homeworlds Board And Move Cleanup

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` are maintained according to `doc/PLANS.md`.

## Purpose / Big Picture

Homeworlds should be easier to read and should record player actions accurately. The board should show only star
systems and selectable pieces, not extra connector lines. Star systems should use the available board space, keep stars
and ships aligned in one row, and remove abandoned stars when the last ship leaves. Catastrophes chosen by the user
should become part of the same SGF move as the action they accompany, so a turn remains one tree node even if it has
multiple steps.

## Progress

- [x] (2026-05-17) Identified the Homeworlds connection-line drawing loop in `src/games/homeworlds/homeworlds_view.c`.
- [x] (2026-05-17) Removed visual connection lines between star systems.
- [ ] Improve row placement so systems account for the bank overlay and spread across each row.
- [ ] Align each system's stars and ships in one horizontal row from the owners' perspectives.
- [ ] Return orphaned stars to the bank after ship catastrophes as well as moves.
- [ ] Record user-triggered catastrophes as steps in a single SGF move.

## Surprises & Discoveries

- Observation: Homeworlds already represents a turn as a `HomeworldsMove` with up to `HOMEWORLDS_MAX_MOVE_STEPS`.
  Evidence: `src/games/homeworlds/homeworlds_types.h` has `HomeworldsMove.steps[]`, and parser/formatter tests already
  cover notation such as `G3 y2>G2 G3 y!`.

## Decision Log

- Decision: Keep reachability calculations even after removing connector lines.
  Rationale: The user asked to remove the visible lines, but the row layout still uses reachability to group systems.
  Date/Author: 2026-05-17 Codex.

## Outcomes & Retrospective

Removed the cairo connection-line pass from `homeworlds_view_draw()` while keeping reachability row placement intact.
Remaining milestones are still pending.

## Context and Orientation

The Homeworlds UI lives in `src/games/homeworlds/homeworlds_view.c`. It renders the board with cairo, builds GTK
buttons over the rendered board for clickable choices, and sends completed `HomeworldsMove` values to the shared app
window. The Homeworlds rules engine lives in `src/games/homeworlds/homeworlds_game.c`; it applies setup moves, turn
steps, moves, captures, sacrifices, and catastrophes to a `HomeworldsPosition`. Tests for this area live mainly in
`tests/test_homeworlds_window.c`, `tests/test_homeworlds_game.c`, and `tests/test_homeworlds_backend.c`.

## Plan of Work

First, remove only the connection-line cairo drawing pass from `homeworlds_view_draw()`. Then adjust system placement
helpers so each row computes the list of systems it contains and places them evenly across the usable board width,
excluding the bank overlay. Then replace the multi-row per-system piece layout with a one-row layout where player 1's
ships appear to the right of the stars and player 2's ships appear to the left. Then make catastrophe cleanup call the
same orphan-star cleanup used after moving away or sacrificing. Finally, change manual catastrophe selection so it is
staged into the current move rather than submitted as a separate one-step move when it belongs to the same turn.

## Concrete Steps

Run commands from `/home/jerome/Data/gcheckers`. After each milestone, run the relevant Homeworlds test binary and
`make all`, then commit that milestone before starting the next.

## Validation and Acceptance

The Homeworlds window tests should pass after each milestone:

    build/tests/test_homeworlds_window

Rules and SGF notation changes should also pass:

    build/tests/test_homeworlds_game
    build/tests/test_homeworlds_backend

The repository should still build every binary:

    make all

## Idempotence and Recovery

Every edit is local to tracked source, tests, or documentation. If a milestone fails, inspect `git diff`, adjust the
smallest relevant area, rerun the same tests, and only commit once the tests pass.

## Artifacts and Notes

No artifacts yet.

## Interfaces and Dependencies

Do not add new external dependencies. Continue using GTK and cairo for rendering, GLib assertions and helpers, and the
existing `HomeworldsMove` multi-step data structure for SGF move notation.
