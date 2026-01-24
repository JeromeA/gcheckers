# Source overview (window refactor components)

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
