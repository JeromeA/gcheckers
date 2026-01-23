# Refactor SGF view into composable components

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This plan is maintained according to PLANS.md at the repository root (`PLANS.md`).

## Purpose / Big Picture

After this change, the SGF view code is split into small, composable GObject helpers for disc creation, tree layout,
link rendering, selection/navigation, and scroll-to-selection. The SGF view behavior is unchanged for users, but the
code becomes easier to maintain and extend. You can see it working by running the GTK app and verifying the SGF tree
panel still renders discs, draws links, scrolls to selection, and responds to keyboard navigation.

## Progress

- [x] (2025-02-14 00:10Z) Capture current SGF view responsibilities and define new helper components with clear
  interfaces.
- [x] (2025-02-14 00:25Z) Implement new helper objects in `src/sgf_view_*.{c,h}` and wire them into `src/sgf_view.c`.
- [x] (2025-02-14 00:30Z) Update build/test wiring for new sources and verify compilation.
- [x] (2025-02-14 00:40Z) Run `make all` and `make test`, update this plan, and commit.

## Surprises & Discoveries

- Observation: The GTK test suite still skips SGF view tests when no display is available, so refactors remain safe but
  cannot validate the UI in headless runs.
  Evidence: `test_sgf_view` reports SKIP for GTK display availability during `make test`.

## Decision Log

- Decision: Split SGF view functionality into five helper types (disc factory, layout builder, link renderer,
  selection controller, and scroller).
  Rationale: Each responsibility is already present in sgf_view.c and has a clear, testable boundary.
  Date/Author: 2025-02-14 / Codex
- Decision: Add a selection controller method to set the selected node without requiring node-widget mappings.
  Rationale: Tree changes can update selection before widgets exist; a raw setter prevents noisy debug logs.
  Date/Author: 2025-02-14 / Codex

## Outcomes & Retrospective

- Outcome: SGF view responsibilities are split into dedicated helper objects with the same user-facing behavior. Builds
  and tests succeed with the new source layout, though UI tests remain skipped in headless environments.

## Context and Orientation

The SGF view is implemented in `src/sgf_view.c` and exposed via `src/sgf_view.h`. It builds a GTK scrolled window with
an overlay containing a drawing area for link lines and a grid of move discs. The file also handles selection state,
keyboard navigation, and scroll-to-selection. The build uses Makefile variables that list SGF view sources, so any new
source files must be added there.

## Plan of Work

First, extract disc creation and click handling into a small GObject helper that emits a node-clicked signal when a
user activates a disc. Next, move the tree layout traversal and grid attachment logic into a layout helper that knows
how to clear the grid, append branches, and populate the node-to-widget hash table using the disc factory. Then,
extract the link drawing (including coordinate translation) into a renderer helper used by the drawing area callback.
After that, move selection/navigation logic into a selection controller that manages the selected node and updates CSS
classes based on the node-widget map. Finally, extract scroll-to-selection into a scroller helper that queues the
adjustment updates. Update `src/sgf_view.c` to own and coordinate these helpers, and update the Makefile to compile the
new source files.

## Concrete Steps

1) Add new helper headers and sources under `src/` and move the corresponding logic from `src/sgf_view.c`.
2) Update `src/sgf_view.c` to hold the new helpers, delegate responsibilities, and keep behavior the same.
3) Update the Makefile source lists to include the new SGF view helper sources.
4) From the repository root, run:

   make all
   make test

5) Update this plan with progress, discoveries, and decisions, then commit.

## Validation and Acceptance

The GTK app should still render the SGF disc tree with link lines, selected-disc styling, keyboard navigation, and
scroll-to-selection. `make all` should build without warnings and `make test` should pass, including `test_sgf_view`.

## Idempotence and Recovery

The refactor is additive and safe to repeat. If compilation fails, re-run `make all` after adjusting the helper APIs
until it links cleanly. If runtime behavior regresses, compare the SGF panel behavior before and after to identify the
helper that needs adjustment.

## Artifacts and Notes

None.

## Interfaces and Dependencies

The helper types should be plain GObject final types placed under `src/sgf_view_*.{c,h}`. They should accept existing
GTK widgets, the SGF tree, and the node-widget hash table as inputs rather than owning the SGF tree themselves. Signals
should use `G_TYPE_POINTER` for SGF node pointers, matching existing `node-selected` conventions.

Plan updates:
- 2025-02-14: Created plan for SGF view refactor into helper objects.
- 2025-02-14: Updated progress, decisions, and outcomes after completing the refactor and validation.
