# Project overview

## `GCheckersWindow` (`src/gcheckers_window.c`)
Class: `GCheckersWindow` (`GtkApplicationWindow`).
Role: composition root that binds model state to UI updates, keeps board input available, and schedules forced moves
(automatic follow-ups plus three startup moves) so bug-reproduction runs do not require manual clicks.
Owns: `GCheckersModel`, `BoardView`, `PlayerControlsPanel`, and `GCheckersSgfController`.
Collaborates with: model signals for refresh and SGF controller updates.
Lifecycle: sinks and retains an owned `PlayerControlsPanel` reference, removes it from its current `GtkBox` parent
during dispose via `gcheckers_widget_remove_from_parent()`, and then clears its references. During dispose it also
cancels pending auto-move and startup forced-move idle sources.

## `GCheckersSgfController` (`src/gcheckers_sgf_controller.c`)
Class: `GCheckersSgfController` (`GObject`).
Role: SGF history synchronization, node selection handling, and replay orchestration.
Owns: `SgfTree` and `SgfView`, plus replay guards (`is_replaying`, `last_history_size`).
Collaborates with: `GCheckersModel` for history and `BoardView` to clear selection on replay.

## `PlayerControlsPanel` (`src/player_controls_panel.c`)
Class: `PlayerControlsPanel` (`GtkBox`).
Role: reduced control strip that only exposes a force-move button for deterministic move advancement.
Signals: emits `force-move-requested` when the force-move button is clicked.
Collaborates with: `GCheckersWindow` force-move handling.

## Widget utilities (`src/widget_utils.c`, `src/widget_utils.h`)
Module: parent-removal helpers.
Role: safely detach widgets from common GTK containers (box, grid, overlay, paned, stack) before dropping the last
reference to avoid GTK4 dispose-time criticals.
Collaborates with: `GCheckersWindow`, `BoardView`, and SGF view helpers during disposal.

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
Role: define game types, rule enforcement, history, promotion, winner updates, winner labels, and the public game
API.
Collaborates with: `move_gen.c` for move enumeration and `checkers_model.c` for GTK integration.


## Move generation (`src/move_gen.c`)
Module: move generation.
Role: enumerate simple moves, jumps, and forced-capture rules.
Collaborates with: `game.c` to validate and apply generated moves.

## GTK model wrapper (`src/checkers_model.c`, `src/checkers_model.h`)
Class: `GCheckersModel` (`GObject`).
Role: wrap the engine for GTK, including move validation, random AI moves, and state-change signals. Move queries
call `game_list_available_moves()` directly.
Collaborates with: `GCheckersWindow` and SGF controllers via signals and high-level move APIs.


## GTK application entry (`src/gcheckers.c`, `src/gcheckers_application.c`, `src/gcheckers_application.h`)
Class: `GCheckersApplication` (`GtkApplication`).
Role: define the GTK application type and activation flow that creates the main window and model, and schedules
a process quit two seconds after launch for deterministic repro runs.
Collaborates with: `GCheckersWindow` for UI wiring.

## Board view subsystem

### `BoardView` (`src/board_view.c`, `src/board_view.h`)
Class: `BoardView` (`GtkWidget`).
Role: coordinate rendering updates, input handling, and active-turn move highlighting for the board.
Collaborates with: selection and square/grid helpers.

### `BoardGrid` (`src/board_grid.c`, `src/board_grid.h`)
Module: board grid helpers.
Role: construct the square layout grid and maintain square bookkeeping.
Collaborates with: `BoardView` and `BoardSquare`.

### `BoardSquare` (`src/board_square.c`, `src/board_square.h`)
Class: `BoardSquare` (`GtkWidget`).
Role: represent individual squares and update piece/index rendering state with inline unicode symbols.
Collaborates with: `BoardGrid`.

### Selection controller (`src/board_selection_controller.c`, `src/board_selection_controller.h`)
Module: selection path logic.
Role: manage click-path selection and move application orchestration.
Collaborates with: `BoardView` and `GCheckersModel` for applying moves.

## SGF subsystem

### SGF tree (`src/sgf_tree.c`, `src/sgf_tree.h`)
Module: SGF tree storage.
Role: manage move nodes, parent/child links, payload access, and traversal helpers.
Collaborates with: SGF view and controller modules.

### SGF view (`src/sgf_view.c`, `src/sgf_view.h`)
Class: `SgfView` (`GtkWidget`).
Role: game-agnostic move tree UI that wires together layout, selection helpers, scrolled content sizing, and
diagnostic sizing logs. It syncs selection after layout updates with debug logging when widgets are not ready and
annotates notify-driven sync attempts with the emitting object/property pair. Layout-sync diagnostics keep adjustment
state logging and compare model content position against measured content position in viewport coordinates. On mismatch
they emit a capitalized GtkScrolledWindow inconsistency message with size/position numbers; otherwise they emit a
short "no inconsistencies" debug line.
Collaborates with: SGF layout (layout-updated signal), selection, scroller, and disc factory helpers.

### SGF disc factory (`src/sgf_view_disc_factory.c`, `src/sgf_view_disc_factory.h`)
Module: disc widget creation.
Role: build SGF move buttons (including the virtual move zero dot) and wire the `node-clicked` signal.
Collaborates with: `SgfView` and the SGF tree.

### SGF layout (`src/sgf_view_layout.c`, `src/sgf_view_layout.h`)
Module: layout helpers.
Role: position discs in a grid-based SGF tree layout (anchoring the virtual root in column zero), measure natural disc
sizes, report both maximum row/column extents and per-column/per-row sizes for accurate sizing and scrolling, and emit
a layout-updated signal after rebuilds.
Collaborates with: `SgfView`.

### SGF scroller (`src/sgf_view_scroller.c`, `src/sgf_view_scroller.h`)
Module: selection scroll helper.
Role: implement two paths only: on scroll requests remember the selected node and attempt scrolling immediately; on
layout updates retry scrolling for the remembered node only, applying a small visibility padding. The implementation
uses one internal helper that treats missing or not-yet-measurable nodes as no-op outcomes and emits detailed
before/after adjustment diagnostics (target ranges, deltas, and visibility state) for each attempt. The file-level
contract comment in `src/sgf_view_scroller.c` is the canonical policy reference (including the prohibition on deferred
retry mechanisms such as ticks/idles/timers).
Collaborates with: `SgfView`, SGF node widget mapping, layout extents, and selection controller updates.

### SGF selection controller (`src/sgf_view_selection_controller.c`, `src/sgf_view_selection_controller.h`)
Module: SGF selection logic.
Role: track SGF selection and update CSS classes.
Collaborates with: `SgfView`, the SGF tree, and the scroller.
