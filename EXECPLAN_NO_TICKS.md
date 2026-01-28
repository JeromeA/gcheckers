# Replace SGF scroller tick callbacks with layout-driven scrolling

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This plan is maintained according to PLANS.md at the repository root (`PLANS.md`).

## Purpose / Big Picture

After this change, the SGF move tree view scrolls to the selected node based on layout sizing notifications instead of
frame tick callbacks. Users should see selection scrolling behave consistently even as the SGF layout grows or changes.
You can see it working by running the GTK app or the SGF view tests and confirming that selection scrolls into view
without relying on repeated tick callbacks.

## Progress

- [x] (2025-02-14 23:55Z) Capture current SGF scrolling flow, document the "no ticks" rationale, and define the new
  layout-driven trigger points.
- [x] (2025-02-14 23:56Z) Implement layout-driven scroll logic in `src/sgf_view_scroller.c` and update callers.
- [x] (2025-02-14 23:59Z) Update documentation (`NO_TICKS.md`, `src/OVERVIEW.md`) and run build/tests.

## Surprises & Discoveries

- Observation: GTK4 does not expose a public `size-allocate` signal on widgets, so the plan uses size request
  notifications instead.
  Evidence: Connecting to `size-allocate` on `GtkGrid` fails with "signal 'size-allocate' is invalid".

## Decision Log

- Decision: Use `notify::width-request` and `notify::height-request` on the SGF overlay as the layout-driven trigger
  for deferred scrolling.
  Rationale: GTK4 lacks a public size-allocate signal, but size request notifications are emitted when SGF content
  sizing changes, providing a stable hook after layout updates.
  Date/Author: 2025-02-14 / Codex

## Outcomes & Retrospective

- Outcome: The SGF scroller now attempts immediate scrolling and falls back to size request notifications instead of
  tick callbacks, with documentation explaining why ticks are discouraged.

## Context and Orientation

The SGF view is implemented in `src/sgf_view.c` and uses helper objects for layout, selection, and scrolling. The
scroll helper lives in `src/sgf_view_scroller.c` and currently schedules a GTK tick callback to clamp the scrolled
window adjustments after selection changes. This plan replaces the tick callback approach with layout-driven size
request notifications (`notify::width-request` and `notify::height-request`) so scroll adjustments happen after
layout sizing changes. The public API for the scroller is exposed in `src/sgf_view_scroller.h`, and SGF-related tests
live in `tests/test_sgf_view.c`.

## Plan of Work

First, document why tick callbacks are unreliable for SGF scrolling and record the preferred layout-driven approach in
`NO_TICKS.md`. Next, update `src/sgf_view_scroller.c` and its header to attempt scrolling immediately when the layout
is already valid, and otherwise connect to size request notifications on the layout widget to scroll once layout
sizing updates are available. Update `src/sgf_view.c` to pass the appropriate layout widget into the scroller, and
adjust any selection flow to ensure layout-triggered scrolling still happens. Finally, update `src/OVERVIEW.md` to
reflect the new scrolling strategy, build all binaries with `make all`, and run the test suite with `make test`.

## Concrete Steps

1) Read `src/sgf_view_scroller.c`, `src/sgf_view_scroller.h`, and `src/sgf_view.c` to map the existing tick-based
   scrolling flow.
2) Create `NO_TICKS.md` at the repository root describing why tick callbacks have failed and how layout-driven scroll
   hooks should be used instead.
3) Replace the tick callback logic in `src/sgf_view_scroller.c` with size request notification handlers, update the
   scroller request structure, and update the public API as needed.
4) Update `src/sgf_view.c` to pass the correct layout widget when queueing a scroll, and adjust any helper calls to
   match the new API.
5) Update `src/OVERVIEW.md` to describe the layout-driven scrolling behavior.
6) From the repository root, run:

   make all
   make test

7) Update this plan with progress, discoveries, and decisions, then commit.

## Validation and Acceptance

Selection scrolling should continue to work in the SGF view without tick callbacks. `make all` should build all
binaries without warnings, and `make test` should pass. The SGF view scrolling tests in `tests/test_sgf_view.c` should
continue to pass, demonstrating that selection scrolls into view after layout allocation.

## Idempotence and Recovery

The changes are safe to reapply. If scrolling fails, re-check that the size request notification handlers are attached
to the correct layout widget and that requests are cleaned up after successful scrolling. If builds fail, re-run
`make all` after fixing compiler errors, and re-run `make test` until all tests pass.

## Artifacts and Notes

None yet.

## Interfaces and Dependencies

The scroller should expose a queue API in `src/sgf_view_scroller.h` that accepts the scrolled window, the layout
widget to observe for size request notifications, the node widget mapping, the selected node, and layout extents. The
new code should connect to `notify::width-request` and `notify::height-request` on the provided layout widget and only
keep the handlers long enough to perform a single successful scroll.

Plan updates:
- 2025-02-14: Created plan for replacing SGF scrolling tick callbacks with layout-driven scrolling.
- 2025-02-14: Implemented size request notification-based scrolling and updated documentation/tests.
