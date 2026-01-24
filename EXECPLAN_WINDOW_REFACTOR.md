# Decompose gcheckers window into focused controllers and panels

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This plan is maintained according to PLANS.md at the repository root (`PLANS.md`).

## Purpose / Big Picture

After this change, `GCheckersWindow` will be a thinner composition root that assembles three extracted components: a
style initializer that owns CSS setup, a player-controls panel that encapsulates the color control dropdowns and the
force-move button, and an SGF controller that owns SGF history synchronization and replay behavior. The GTK app should
behave the same, but the window file will be smaller and easier to reason about. You can see it working by building the
GTK app, running tests, and optionally launching the GTK binary to confirm the UI still renders and responds.

## Progress

- [x] (2026-01-24 09:17Z) Review PLANS.md, repository overview, and the current `src/gcheckers_window.c`
  responsibilities.
- [x] (2026-01-24 09:21Z) Implement `src/gcheckers_style.{c,h}` and move the CSS provider logic behind a one-time
  initializer.
- [x] (2026-01-24 09:21Z) Implement `src/player_controls_panel.{c,h}` as a composite `GtkBox` with explicit signals
  and control-mode helpers.
- [x] (2026-01-24 09:21Z) Implement `src/gcheckers_sgf_controller.{c,h}` to own SGF tree/view wiring, history
  synchronization, and replay.
- [x] (2026-01-24 09:21Z) Refactor `src/gcheckers_window.c` to compose the extracted modules and remove SGF/CSS/control
  wiring from the window.
- [x] (2026-01-24 09:27Z) Update `Makefile`, add unit tests for the new components, and run `make all` plus
  `make test` (validated again after `make clean` to ensure no generated artifacts remain and after staging to confirm
  a clean pre-commit state).
- [x] (2026-01-24 09:41Z) Add `src/OVERVIEW.md` to describe the extracted classes, their responsibilities, and their
  main collaborators.
- [x] (2026-01-24 11:10Z) Reduce `src/OVERVIEW.md` so each file summary fits within five lines per follow-up review
  feedback.

## Surprises & Discoveries

- Observation: The repository already has an ExecPlan (`EXECPLAN.md`) documenting a prior large refactor, so adding a
  new plan file is consistent with existing workflow expectations.
  Evidence: `EXECPLAN.md` exists at the repo root and follows the PLANS.md skeleton.
- Observation: The SGF replay logic must still clear board selection, so the SGF controller needs access to `BoardView`
  even though it is not a widget itself.
  Evidence: The original replay flow in `src/gcheckers_window.c` called `board_view_clear_selection` immediately after
  resetting the model.
- Observation: GTK4 does not expose `gtk_button_clicked`, so tests should emit the `clicked` signal directly.
  Evidence: `make test` failed with `implicit declaration of function 'gtk_button_clicked'` under GTK4 headers.
- Observation: SGF controller tests must link most of the GTK UI sources because `BoardView` transitively depends on
  many helpers.
  Evidence: The SGF controller test target requires `board_view`, SGF view helpers, and the core model sources to link
  successfully.

## Decision Log

- Decision: Create a new ExecPlan file `EXECPLAN_WINDOW_REFACTOR.md` rather than overwriting the historical
  `EXECPLAN.md`.
  Rationale: The existing ExecPlan documents completed work; preserving it keeps the record intact while still following
  the ExecPlan requirement for this significant refactor.
  Date/Author: 2026-01-24 / Codex
- Decision: Make `PlayerControlsPanel` a `GtkBox` subclass that emits `control-changed` and `force-move-requested`
  signals.
  Rationale: Emitting higher-level signals keeps the window free from low-level widget wiring while still enabling
  simple coordination logic.
  Date/Author: 2026-01-24 / Codex
- Decision: Give `GCheckersSgfController` an explicit `BoardView` dependency and have it own its own model signal
  connection.
  Rationale: SGF replay has view-side effects (clearing selection) and SGF synchronization should be isolated from
  window logic; independent model subscriptions keep responsibilities separated.
  Date/Author: 2026-01-24 / Codex
- Decision: Use `g_signal_emit_by_name(button, "clicked")` in tests rather than GTK3-era helpers.
  Rationale: GTK4 removed `gtk_button_clicked`; emitting the signal directly keeps the test portable and warning-free.
  Date/Author: 2026-01-24 / Codex

## Outcomes & Retrospective

The window now delegates CSS setup, player control wiring, and SGF synchronization to dedicated modules while preserving
existing behavior. Both `make all` and `make test` complete successfully in this environment, with GTK-dependent tests
skipping cleanly when no display is available.

## Context and Orientation

`src/gcheckers_window.c` previously mixed three kinds of responsibilities. First, it applied a large inline CSS string
and installed the provider on the default display. Second, it built the full widget hierarchy, including the player
control dropdowns and the force-move button. Third, it contained SGF-specific controller logic that tracked history
size, appended SGF nodes on model changes, and replayed the model when SGF nodes were selected. The window also
coordinated model signal connections and status updates. The build wiring for the GTK binary lives in `Makefile` under
the `gcheckers` target, and GTK-aware tests follow the pattern in `tests/test_sgf_view.c` where `gtk_init_check()` gates
tests when no display is available.

## Plan of Work

Begin by extracting the inline CSS provider logic into a new `src/gcheckers_style.{c,h}` module with a single public
initializer that ensures the provider is only installed once per process. Next, introduce a `PlayerControlsPanel`
composite widget in `src/player_controls_panel.{c,h}`. It owns the white and black dropdowns plus the force-move button
and exposes explicit helpers to query control modes and set the force-move sensitivity, while emitting higher-level
signals for coordination. Then introduce a new `GCheckersSgfController` GObject in
`src/gcheckers_sgf_controller.{c,h}` that owns `SgfTree`, `SgfView`, and the SGF synchronization logic previously
embedded in the window. It exposes `set_model`, `reset`, `get_tree`, `get_view`, `get_widget`, and `is_replaying` entry
points.

With the new modules in place, refactor `src/gcheckers_window.c` to call the style initializer early, construct and
embed the player controls panel instead of raw dropdowns and buttons, construct and embed the SGF controller's widget,
and delegate SGF reset logic into the controller. Update model-state handling so the window becomes a coordinator that
asks these components to update themselves based on model state. Finally, update `Makefile` to compile the new modules
and add new unit tests for the extracted components. Run `make all` and `make test` from the repository root and fix any
warnings or failures before committing.

## Concrete Steps

From the repository root (`/workspace/gcheckers`):

1) Add `src/gcheckers_style.{c,h}` with a one-time CSS provider initializer.
2) Add `src/player_controls_panel.{c,h}` and write `tests/test_player_controls_panel.c`.
3) Add `src/gcheckers_sgf_controller.{c,h}` and write `tests/test_gcheckers_sgf_controller.c`.
4) Refactor `src/gcheckers_window.c` and any touched headers to use the new modules.
5) Update `Makefile` to include new sources and tests.
6) Run:

   make all
   make test

7) Update this ExecPlan's living sections to reflect discoveries and outcomes.

## Validation and Acceptance

Acceptance is met when the project builds without warnings, all tests pass, and the GTK binary composes the new
components without regressions. Concretely, `make all` should complete successfully, and `make test` should complete
with all test binaries succeeding or explicitly skipped due to no GTK display. The new component tests should validate
the SGF controller behavior (SGF append, replay, and reset interactions) and the player controls panel behavior (default
selection modes, signal emissions, and force-move sensitivity handling).

## Idempotence and Recovery

All changes are additive and can be re-run safely. If compilation fails during the refactor, re-run `make all` after
each module is wired in to catch missing includes or Makefile entries early. If a regression appears in SGF replay
behavior, compare the new SGF controller methods against the original logic in `src/gcheckers_window.c` and restore any
missing state guards (`is_replaying` and `last_history_size`).

## Artifacts and Notes

Key validation commands completed successfully:

   make all
   make test

## Interfaces and Dependencies

The new modules are repository-local GObjects or helpers that follow existing GTK patterns:

- `src/gcheckers_style.{c,h}` exposes `void gcheckers_style_init(void);` and internally guards against applying the CSS
  provider more than once.
- `src/player_controls_panel.{c,h}` defines a final type `PlayerControlsPanel` derived from `GtkBox` and
  exposes explicit helpers like `player_controls_panel_is_user_control(panel, color)` plus setters like
  `player_controls_panel_set_force_move_sensitive(panel, sensitive)`, while emitting higher-level signals.
- `src/gcheckers_sgf_controller.{c,h}` defines a final type `GCheckersSgfController` derived from `GObject`. It exposes
  `set_model`, `reset`, `get_widget`, `get_tree`, `get_view`, and `is_replaying` functions and owns the SGF tree/view.

Plan updates:
- 2026-01-24: Created the ExecPlan for the window decomposition work to comply with PLANS.md requirements.
- 2026-01-24: Updated progress, discoveries, and decisions after extracting the style module, controls panel, SGF
  controller, refactoring the window composition, wiring Makefile plus tests, and validating with `make all` and
  `make test`.
- 2026-01-24: Added `src/OVERVIEW.md` to document the new components and their collaborations per follow-up review
  feedback.
- 2026-01-24: Compressed `src/OVERVIEW.md` so each file section is no more than five lines, matching reviewer
  expectations.
