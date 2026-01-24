# Project overview

## `GCheckersWindow` (`src/gcheckers_window.c`)
Class: `GCheckersWindow` (`GtkApplicationWindow`).
Role: composition root that binds model state to UI updates and high-level actions.
Owns: `GCheckersModel`, `BoardView`, `PlayerControlsPanel`, and `GCheckersSgfController`.
Collaborates with: `gcheckers_style_init()` for CSS and model signals for refresh.
Lifecycle: sinks and retains an owned `PlayerControlsPanel` reference, removes it from its current `GtkBox` parent
during dispose, and then clears its references.

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

## Board primitives (`src/board.c`, `src/board.h`)
Module: board storage and helpers.
Role: define board data structures, coordinate conversion helpers, piece helpers, and reset/init logic.
Collaborates with: `game.c` for rules and state transitions.

## Constants (`src/checkers_constants.h`)
Module: shared constants.
Role: centralize size limits for boards, moves, and byte storage used throughout the engine and UI.
Collaborates with: all game and model modules via compile-time limits.

## Game engine (`src/game.c`, `src/game.h`)
Module: core game rules and state.
Role: define game types, rule enforcement, history, promotion, winner updates, and the public game API.
Collaborates with: `move_gen.c` for move enumeration and `checkers_model.c` for GTK integration.

## Game printing (`src/game_print.c`)
Module: terminal formatting helpers.
Role: render board state and move notation for the CLI.
Collaborates with: `checkers_cli.c` for user-visible output.

## Move generation (`src/move_gen.c`)
Module: move generation.
Role: enumerate simple moves, jumps, and forced-capture rules.
Collaborates with: `game.c` to validate and apply generated moves.

## GTK model wrapper (`src/checkers_model.c`, `src/checkers_model.h`)
Class: `GCheckersModel` (`GObject`).
Role: wrap the engine for GTK, including move validation, random AI moves, and state-change signals.
Collaborates with: `GCheckersWindow` and SGF controllers via signals and high-level move APIs.

## CLI entry point (`src/checkers_cli.c`)
Module: CLI front end.
Role: provide a prompt-driven loop for human vs. AI play in the terminal.
Collaborates with: `game.c` and `game_print.c`.

## GTK application entry (`src/gcheckers.c`, `src/gcheckers_application.c`, `src/gcheckers_application.h`)
Class: `GCheckersApplication` (`GtkApplication`).
Role: define the GTK application type and activation flow that creates the main window and model.
Collaborates with: `GCheckersWindow` for UI wiring.

## Board view subsystem

### `BoardView` (`src/board_view.c`, `src/board_view.h`)
Class: `BoardView` (`GtkWidget`).
Role: coordinate rendering updates, input handling, and active-turn move highlighting for the board.
Collaborates with: selection, overlays, and square/grid helpers.

### `BoardGrid` (`src/board_grid.c`, `src/board_grid.h`)
Module: board grid helpers.
Role: construct the square layout grid and maintain square bookkeeping.
Collaborates with: `BoardView` and `BoardSquare`.

### `BoardSquare` (`src/board_square.c`, `src/board_square.h`)
Class: `BoardSquare` (`GtkWidget`).
Role: represent individual squares and update piece/index rendering state.
Collaborates with: `BoardGrid` and `PiecePalette`.

### Last move overlay (`src/board_move_overlay.c`, `src/board_move_overlay.h`)
Module: move overlay renderer.
Role: draw last-move arrows via cairo on top of the board.
Collaborates with: `BoardView` to render current move highlights.

### Selection controller (`src/board_selection_controller.c`, `src/board_selection_controller.h`)
Module: selection path logic.
Role: manage click-path selection and move application orchestration.
Collaborates with: `BoardView` and `GCheckersModel` for applying moves.

### Piece palette (`src/piece_palette.c`, `src/piece_palette.h`)
Module: piece paintable palette.
Role: provide paintables and fallback symbols for checker men and kings.
Collaborates with: `BoardSquare` and paintable factories.

### Man paintable (`src/gcheckers_man_paintable.c`, `src/gcheckers_man_paintable.h`)
Module: `GdkPaintable` factory.
Role: render checker men and kings as paintables for GTK widgets.
Collaborates with: `PiecePalette` and board rendering.

## SGF subsystem

### SGF tree (`src/sgf_tree.c`, `src/sgf_tree.h`)
Module: SGF tree storage.
Role: manage move nodes, parent/child links, payload access, and traversal helpers.
Collaborates with: SGF view and controller modules.

### SGF view (`src/sgf_view.c`, `src/sgf_view.h`)
Class: `SgfView` (`GtkWidget`).
Role: game-agnostic move tree UI that wires together layout, rendering, and selection helpers.
Collaborates with: SGF layout, selection, scroller, and disc factory helpers.

### SGF disc factory (`src/sgf_view_disc_factory.c`, `src/sgf_view_disc_factory.h`)
Module: disc widget creation.
Role: build SGF move buttons and wire the `node-clicked` signal.
Collaborates with: `SgfView` and the SGF tree.

### SGF layout (`src/sgf_view_layout.c`, `src/sgf_view_layout.h`)
Module: layout helpers.
Role: position discs in a grid-based SGF tree layout.
Collaborates with: `SgfView` and link rendering.

### SGF link renderer (`src/sgf_view_link_renderer.c`, `src/sgf_view_link_renderer.h`)
Module: connector renderer.
Role: compute disc centers and draw connector lines between SGF nodes.
Collaborates with: SGF layout data and view sizing.

### SGF scroller (`src/sgf_view_scroller.c`, `src/sgf_view_scroller.h`)
Module: selection scroll helper.
Role: clamp scrolled window adjustments and keep selected nodes in view.
Collaborates with: `SgfView` and selection controller updates.

### SGF selection controller (`src/sgf_view_selection_controller.c`, `src/sgf_view_selection_controller.h`)
Module: SGF selection logic.
Role: track SGF selection, update CSS classes, and navigate siblings and parents.
Collaborates with: `SgfView`, the SGF tree, and the scroller.
