# Homeworlds Board And Move Cleanup

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` are maintained according to `doc/PLANS.md`.

## Purpose / Big Picture

Homeworlds should be easier to read and should record player actions accurately. The board should show only star
systems and selectable pieces, not extra connector lines. Star systems should use the available board space, keep stars
and ships aligned in one row, and remove abandoned stars when the last ship leaves. Catastrophes chosen by the user
should become part of the same SGF move as the action they accompany, so a turn remains one tree node even if it has
multiple steps. Rows should place systems by their measured widths, reserve the bank footprint, and expand the board
inside a scroller when the systems cannot fit.

## Progress

- [x] (2026-05-17) Identified the Homeworlds connection-line drawing loop in `src/games/homeworlds/homeworlds_view.c`.
- [x] (2026-05-17) Removed visual connection lines between star systems.
- [x] (2026-05-17) Improved row placement so systems account for the bank overlay and spread across each row.
- [x] (2026-05-17) Aligned each system's stars and ships in one horizontal row from the owners' perspectives.
- [x] (2026-05-17) Returned orphaned stars to the bank after ship catastrophes as well as moves.
- [x] (2026-05-17) Recorded user-triggered catastrophes as steps in a single SGF move.
- [x] (2026-05-18) Replaced slot-fraction row placement with measured-width row packing and horizontal scrolling.
- [x] (2026-05-18) Gave the scrolled board a practical minimum viewport and widened the Homeworlds default board panel.
- [x] (2026-05-18) Split default board-panel width from minimum width so the Homeworlds paned handle remains movable.
- [x] (2026-05-18) Exempted non-square-grid board hosts from the shared square-board splitter height clamp.

## Surprises & Discoveries

- Observation: Homeworlds already represents a turn as a `HomeworldsMove` with up to `HOMEWORLDS_MAX_MOVE_STEPS`.
  Evidence: `src/games/homeworlds/homeworlds_types.h` has `HomeworldsMove.steps[]`, and parser/formatter tests already
  cover notation such as `G3 y2>G2 G3 y!`.

## Decision Log

- Decision: Keep reachability calculations even after removing connector lines.
  Rationale: The user asked to remove the visible lines, but the row layout still uses reachability to group systems.
  Date/Author: 2026-05-17 Codex.

- Decision: Use the existing `HomeworldsMove.steps[]` representation for manual catastrophes rather than adding a new
  move blob or SGF encoding.
  Rationale: The engine, parser, and formatter already support multi-step turns. The bug was in the UI submitting
  catastrophe buttons as complete moves before the rest of the turn.
  Date/Author: 2026-05-17 Codex.

- Decision: Measure each system box before choosing row x positions.
  Rationale: Fractional centers such as one-third and two-thirds only work for same-width systems. Equal empty spaces
  must be computed from the sum of rendered system widths, and overflow should increase the board content width so the
  scrolled board can expose the extra space.
  Date/Author: 2026-05-18 Codex.

- Decision: Keep the board content scrollable, but do not let the board viewport collapse below a usable width.
  Rationale: A `GtkScrolledWindow` does not naturally ask the surrounding panes for the full drawing-area width. Without
  an explicit minimum viewport and a larger Homeworlds board-panel default, the Homeworlds side controls can consume
  most of the panel and leave the board almost invisible at launch.
  Date/Author: 2026-05-18 Codex.

- Decision: Add a profile-level minimum board-panel width separate from the default board-panel width.
  Rationale: The shared window uses `gtk_widget_set_size_request()` to preserve panel widths. If the large Homeworlds
  startup width is reused as the minimum size, the splitter cannot move. Homeworlds needs a large startup width and a
  smaller minimum width because the board itself can scroll horizontally.
  Date/Author: 2026-05-18 Codex.

- Decision: Keep the main splitter height clamp only for square-grid board profiles.
  Rationale: Checkers and boop need the guard because their board hosts are square grids. Homeworlds is a custom
  horizontally scrollable board host, so capping the split position to the paned height incorrectly prevents the SGF
  drawer from being reduced on wide windows.
  Date/Author: 2026-05-18 Codex.

## Outcomes & Retrospective

Removed the cairo connection-line pass from `homeworlds_view_draw()` while keeping reachability row placement intact.
Rows now measure each system box and distribute the remaining row width equally in the empty spaces before, between,
and after the systems. The usable row width reserves the right-side bank footprint, and oversized rows increase the
drawing area's content width inside a horizontal scroller. The board scroller also keeps a minimum practical viewport,
and the Homeworlds default board panel width accounts for the board plus the Homeworlds side controls without becoming
the splitter's hard minimum. The shared square-board splitter clamp now only runs for square-grid profiles, so the
Homeworlds board panel can take the full width of a wide window while its drawing area remains scrollable.
System internals now render as one row ordered player 2 ships, stars, then player 1 ships, matching each owner's
right-hand side from their own perspective.
Catastrophe application now runs orphan-star cleanup after all successful catastrophes, not only star-destroying
catastrophes.
Manual catastrophes are now staged into the Homeworlds move builder, update the builder's working position immediately,
and are recorded in SGF only when the later primary action or pass completes the turn.
All five requested items are complete and committed as separate milestones.

## Context and Orientation

The Homeworlds UI lives in `src/games/homeworlds/homeworlds_view.c`. It renders the board with cairo, builds GTK
buttons over the rendered board for clickable choices, and sends completed `HomeworldsMove` values to the shared app
window. The Homeworlds rules engine lives in `src/games/homeworlds/homeworlds_game.c`; it applies setup moves, turn
steps, moves, captures, sacrifices, and catastrophes to a `HomeworldsPosition`. Tests for this area live mainly in
`tests/test_homeworlds_window.c`, `tests/test_homeworlds_game.c`, and `tests/test_homeworlds_backend.c`.

## Plan of Work

First, remove only the connection-line cairo drawing pass from `homeworlds_view_draw()`. Then adjust system placement
helpers so each row computes the list of systems it contains and places them evenly across the usable board width,
excluding the bank overlay and accounting for each system's measured width. If the measured rows are wider than the
viewport, increase the board content width and rely on the board scroller. Then replace the multi-row per-system piece
layout with a one-row layout where player 1's ships appear to the right of the stars and player 2's ships appear to the
left. Then make catastrophe cleanup call the same orphan-star cleanup used after moving away or sacrificing. Finally,
change manual catastrophe selection so it is staged into the current move rather than submitted as a separate one-step
move when it belongs to the same turn.

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
