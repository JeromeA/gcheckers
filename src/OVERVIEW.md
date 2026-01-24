# Project overview

## `GCheckersWindow` (`src/gcheckers_window.c`)
Class: `GCheckersWindow` (`GtkApplicationWindow`).
Role: composition root that binds model state to UI updates and high-level actions.
Owns: `GCheckersModel`, `BoardView`, `PlayerControlsPanel`, and `GCheckersSgfController`.
Collaborates with: `gcheckers_style_init()` for CSS and model signals for refresh.

## `GCheckersSgfController` (`src/gcheckers_sgf_controller.c`)
Class: `GCheckersSgfController` (`GObject`).
Role: SGF history synchronization, node selection handling, and replay orchestration.
Owns: `SgfTree` and `SgfView`, plus replay guards (`is_replaying`, `last_history_size`).
Collaborates with: `GCheckersModel` for history and `BoardView` to clear selection on replay.

## `PlayerControlsPanel` (`src/player_controls_panel.c`)
Class: `PlayerControlsPanel` (`GtkBox`).
Role: encapsulates player mode dropdowns and force-move UI.
Signals: `control-changed` and `force-move-requested` for window-level coordination.
Collaborates with: `GCheckersWindow` (signal handlers) and GTK widgets (`GtkDropDown`, `GtkButton`).

## `gcheckers_style_init()` (`src/gcheckers_style.c`)
Module: `gcheckers_style_init()` (style helper, not a class).
Role: installs application CSS once per process using `g_once_init_enter/leave`.
Owns: CSS string and `GtkCssProvider` setup.
Collaborates with: `GdkDisplay`/`GtkStyleContext` and is invoked by `GCheckersWindow`.

- src/board.h: board data structures and public helpers for coordinates, pieces, and playable squares.
- src/board.c: board storage logic, reset/init, coordinate conversion, and piece helpers.
- src/checkers_constants.h: shared size limits for boards, moves, and byte storage.
- src/game.h: core game types, rules, state, and public API for move listing and application.
- src/game.c: game lifecycle, move application, history, promotion, and winner updates.
- src/game_print.c: terminal formatting for the board and move notation.
- src/move_gen.c: move generation for simple moves, jumps, and rules like forced captures.
- src/checkers_model.h: GObject model API that wraps the game engine for GTK use.
- src/checkers_model.c: model implementation, move validation, random AI moves, and state-change signals.
- src/checkers_cli.c: CLI entry point with a prompt-driven loop for human vs. AI play.
- src/gcheckers.c: GTK application entry point that launches the GApplication.
- src/gcheckers_application.h: GTK application type declaration.
- src/gcheckers_application.c: GTK application activation that creates the main window and model.
- src/board_view.h: board view widget API.
- src/board_view.c: board view coordination, rendering updates, and input handling.
- src/board_grid.h: board grid helper API for laying out squares.
- src/board_grid.c: board grid construction and square bookkeeping.
- src/board_square.h: board square widget API for piece/index rendering.
- src/board_square.c: board square widget creation and state updates.
- src/board_move_overlay.h: last-move overlay renderer API.
- src/board_move_overlay.c: cairo overlay drawing for the last move arrows.
- src/board_selection_controller.h: move selection controller API for click paths.
- src/board_selection_controller.c: selection path logic and move application.
- src/piece_palette.h: piece palette API for paintables and fallback symbols.
- src/piece_palette.c: piece palette implementation for checker men and kings.
- src/gcheckers_man_paintable.h: GdkPaintable factory for board pieces.
- src/gcheckers_man_paintable.c: paintable implementation for checker men and kings.
- src/gcheckers_window.h: GTK window type declaration.
- src/gcheckers_window.c: GTK UI, board rendering, move selection, and styling logic.
- src/sgf_tree.h: SGF tree API for move nodes, parent/child links, and payload access.
- src/sgf_tree.c: SGF tree storage, node allocation, and traversal helpers.
- src/sgf_view.h: SGF view API for the move tree UI; game agnostic and should not mention checkers.
- src/sgf_view.c: SGF view widget wiring, input handling, and helper coordination.
- src/sgf_view_disc_factory.h: disc factory API for SGF move buttons.
- src/sgf_view_disc_factory.c: disc widget creation and node-clicked signal wiring.
- src/sgf_view_layout.h: layout helper API for positioning discs in the SGF tree grid.
- src/sgf_view_layout.c: grid layout implementation for the SGF tree.
- src/sgf_view_link_renderer.h: link renderer API for drawing connector lines between SGF nodes.
- src/sgf_view_link_renderer.c: connector renderer that computes disc centers and draws node links.
- src/sgf_view_scroller.h: scroller API for keeping selected SGF nodes in view.
- src/sgf_view_scroller.c: scroll helper that clamps scrolled window adjustments to selected nodes.
- src/sgf_view_selection_controller.h: selection controller API for tracking SGF selection and navigation.
- src/sgf_view_selection_controller.c: selection logic that updates CSS classes and navigates siblings/parents.
