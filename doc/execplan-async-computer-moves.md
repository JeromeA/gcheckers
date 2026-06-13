# Keep Computer Move Search Off the GTK Main Thread

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document follows `doc/PLANS.md` from the repository root.

## Purpose / Big Picture

When a player side is set to Computer, the application currently chooses the computer move on the GTK main thread.
Large searches, especially in Homeworlds, make the window stop repainting and stop responding until the search
finishes. After this change, both automatic computer turns and the manual Force Move button start a background search
on a copied game position. The GTK thread remains free to redraw and process input, and the user can stop a pending
computer move before it applies.

## Progress

- [x] (2026-06-13) Identified that `ggame_window_force_move()` synchronously calls
  `ggame_sgf_controller_step_ai_move()` from the main thread.
- [x] (2026-06-13) Confirmed the existing analysis code in `src/window.c` already demonstrates the worker-thread and
  `g_main_context_invoke()` pattern needed here.
- [x] (2026-06-13) Add a cancellable AI move chooser in `src/ai_search.c`.
- [x] (2026-06-13) Add computer-thinking controls and Stop button support to `PlayerControlsPanel`.
- [x] (2026-06-13) Replace synchronous window computer moves with a cancellable worker task.
- [x] (2026-06-13) Update tests and `doc/OVERVIEW.md`.
- [x] (2026-06-13) Build and run focused validation plus the full `make` build.

## Surprises & Discoveries

- Observation: `PlayerControlsPanel` owns the side dropdowns and computer-depth scale, so the disabled AI settings and
  Stop button belong there rather than in the toolbar.
  Evidence: `src/player_controls_panel.c` stores `side0_control`, `side1_control`, and `computer_depth_scale`.

- Observation: Manual Force Move can be requested while the current side is user-controlled.
  Evidence: `tests/test_window.c` has `/gcheckers-window/force-move-user-turn`.

- Observation: Resyncing every window action from the computer-move completion path left stale GTK work that could
  surface in the next window test.
  Evidence: `/gcheckers-window/auto-move-next-player-computer` followed by `/gcheckers-window/force-move-user-turn`
  failed with a `GLib-GObject-FATAL-CRITICAL` until completion updated only the player panel and `game-force-move`
  action.

## Decision Log

- Decision: Make both automatic computer turns and manual Force Move asynchronous.
  Rationale: Both paths use `ggame_window_force_move()` today, and leaving Force Move synchronous would preserve a
  visible way to freeze the UI.
  Date/Author: 2026-06-13 / Codex.

- Decision: Treat Stop and UI state changes as cancellation by generation token.
  Rationale: Search already supports cooperative cancellation checks; stale worker results must be ignored even if a
  backend reaches cancellation later than the user expects.
  Date/Author: 2026-06-13 / Codex.

- Decision: Keep move application on the GTK thread.
  Rationale: `ggame_sgf_controller_apply_move()` updates the SGF tree, model, views, and autosave state and should not
  run from a worker thread.
  Date/Author: 2026-06-13 / Codex.

- Decision: Let Force Move start a worker regardless of side control mode, but require automatic moves to re-check
  that the current side is still computer-controlled before the worker starts.
  Rationale: This preserves the existing manual Force Move behavior while preventing a pending idle auto-move from
  firing after the user changes the side back to `User`.
  Date/Author: 2026-06-13 / Codex.

- Decision: Use `GCancellable` as the task cancellation token and keep the worker thread away from `GGameWindow`.
  Rationale: The worker only needs a thread-safe cancellation object. The main-thread completion can use a weak window
  reference and then validate live state before applying a move.
  Date/Author: 2026-06-13 / Codex.

- Decision: Completion updates only the player controls' thinking state and the Force Move action.
  Rationale: A full mode/action resync from an asynchronous completion created unnecessary GTK churn and order-sensitive
  test failures. Other mode state is still synchronized through the existing mode/state-change paths.
  Date/Author: 2026-06-13 / Codex.

## Outcomes & Retrospective

Implemented. Computer move choice now runs on a background thread for both automatic turns and manual Force Move, while
the GTK thread remains responsible for applying only current results. The Stop button cancels pending search, and AI
settings are disabled while thinking. Focused AI/window tests and the full `make` build passed; the standalone
`test_player_controls_panel` binary skipped GTK cases because its `gtk_init_check()` reported no display.

## Context and Orientation

`src/window.c` owns the main `GGameWindow` object and wires the toolbar, player controls, board view, SGF controller,
and analysis UI together. `ggame_window_maybe_trigger_auto_move()` schedules an idle source for computer turns, and
that idle source currently calls `ggame_window_force_move()`. `ggame_window_force_move()` then asks
`src/sgf_controller.c` to choose and apply an AI move synchronously.

`src/ai_search.c` exposes generic search functions for all supported game backends. A backend position is an opaque
memory block whose size and copy/clear functions are described by `GameBackend`. A worker thread must use its own copy
of the position and move buffers.

`src/player_controls_panel.c` is the widget containing the user/computer dropdowns and the computer-depth scale. It is
the right place to disable AI settings while the computer is thinking and to expose a Stop button for cancellation.

## Plan of Work

First, extend `src/ai_search.h` and `src/ai_search.c` with a cancellable move chooser. It should reuse the existing
cancellable analysis function, then preserve the current behavior of choosing randomly among moves tied for best
score. The existing synchronous chooser remains as a wrapper.

Next, extend `PlayerControlsPanel` with a Stop button and a `computer-thinking` state API. When thinking is active,
the side dropdowns and depth scale are insensitive and the Stop button is sensitive. The panel emits a signal when Stop
is clicked.

Then, add a computer-move task to `src/window.c`. Starting a task copies the current backend position, captures the SGF
node, side to move, selected depth, and control modes, then starts a `GThread`. Completion is marshalled back to the
main thread. The result is applied only if the generation token and captured state still match the live window.

Finally, update tests and documentation. Existing window tests should wait for asynchronous move completion. New tests
should cover the panel thinking state, Stop signal, and cancellable chooser.

## Concrete Steps

Work from `/home/jerome/Data/gcheckers`.

Run focused builds and tests as the implementation lands:

    make build/tests/test_ai_search build/tests/test_player_controls_panel build/tests/test_window
    build/tests/test_ai_search
    build/tests/test_player_controls_panel
    build/tests/test_window -p /ggame-window/auto-move-next-player-computer

Before committing, run the repository-required full build and tests:

    make
    make test

## Validation and Acceptance

The feature is accepted when pressing Force Move or reaching a computer-controlled turn does not perform AI search on
the GTK main thread. While a computer move is pending, the player-control dropdowns and depth slider are disabled, Stop
is enabled, and pressing Stop prevents the pending result from applying. Existing automatic computer move behavior must
still eventually play a legal move when not stopped.

## Idempotence and Recovery

The edits are additive and can be rebuilt repeatedly with `make`. If a worker result arrives after cancellation, the
generation check makes it harmless. If a test run leaves build artifacts, they remain under `build/` and can be
recreated by `make`.

## Artifacts and Notes

The current local worktree already contains unrelated changes in `src/games/homeworlds/homeworlds_game.c`,
`src/homeworlds_eval_experiment.c`, and untracked files. This plan intentionally leaves them untouched.

## Interfaces and Dependencies

Add to `src/ai_search.h`:

    gboolean game_ai_search_choose_move_cancellable(const GameBackend *backend,
                                                    gconstpointer position,
                                                    guint max_depth,
                                                    gpointer out_move,
                                                    GameAiCancelFunc should_cancel,
                                                    gpointer user_data);

Add to `src/player_controls_panel.h`:

    gboolean player_controls_panel_get_computer_thinking(PlayerControlsPanel *self);
    void player_controls_panel_set_computer_thinking(PlayerControlsPanel *self, gboolean thinking);

The Stop button should emit a `stop-computer` signal from `PlayerControlsPanel`.
