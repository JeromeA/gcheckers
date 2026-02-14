# Project overview

## `GCheckersWindow` (`src/gcheckers_window.c`)
Class: `GCheckersWindow` (`GtkApplicationWindow`).
Role: SGF-only composition root for reproducing the scrolling inconsistency. The window intentionally keeps minimal
chrome, seeds synthetic SGF nodes at startup, and schedules extra synthetic steps on idle so reproduction does not
depend on gameplay state.
Owns: a dummy controls `GtkBox` and `GCheckersSgfController`.
Collaborates with: `GCheckersSgfController` only.
Lifecycle: sinks and retains an owned dummy controls `GtkBox` reference, removes it from its current parent during
`dispose` via `gcheckers_widget_remove_from_parent()`, and then clears its references. During dispose it also cancels
pending startup synthetic-node idle sources.

## `GCheckersSgfController` (`src/gcheckers_sgf_controller.c`)
Class: `GCheckersSgfController` (`GObject`).
Role: SGF tree ownership, SGF node selection handling, and synthetic-node append support for detached SGF-only repro
flows.
Owns: `SgfTree` and `SgfView`.
Collaborates with: SGF tree/view modules.

## Widget utilities (`src/widget_utils.c`, `src/widget_utils.h`)
Module: parent-removal helpers.
Role: safely detach widgets from common GTK containers (box and grid) before dropping the last reference to avoid GTK4
dispose-time criticals.

## Constants (`src/checkers_constants.h`)
Module: shared constants.
Role: centralize limits used by SGF modules.

## GTK application entry (`src/gcheckers.c`, `src/gcheckers_application.c`, `src/gcheckers_application.h`)
Class: `GCheckersApplication` (`GtkApplication`).
Role: define activation flow that creates the SGF-only repro window and schedules process quit two seconds after launch
for deterministic repro runs.
Collaborates with: `GCheckersWindow`.

## SGF subsystem

### SGF tree (`src/sgf_tree.c`, `src/sgf_tree.h`)
Module: SGF tree storage.
Role: manage move nodes, parent/child links, payload access, and traversal helpers.
Collaborates with: SGF view and controller modules.

### SGF view (`src/sgf_view.c`, `src/sgf_view.h`)
Class: `SgfView` (`GtkWidget`).
Role: game-agnostic move tree UI that wires together layout, selection helpers, scrolled content sizing, and
diagnostic sizing logs. Layout-sync diagnostics compare selected-node geometry in content coordinates, selected-node
geometry in viewport coordinates, and overlay content origin in content coordinates; they emit the inconsistency log
used by repro tests.
Collaborates with: SGF layout (layout-updated signal), selection, scroller, and disc factory helpers.

### SGF disc factory (`src/sgf_view_disc_factory.c`, `src/sgf_view_disc_factory.h`)
Module: disc widget creation.
Role: build SGF move buttons (including the virtual move zero dot) and wire the `node-clicked` signal.
Collaborates with: `SgfView` and the SGF tree.

### SGF layout (`src/sgf_view_layout.c`, `src/sgf_view_layout.h`)
Module: layout helpers.
Role: position discs in a grid-based SGF tree layout, measure natural disc sizes, report extents and per-axis sizes,
and emit a layout-updated signal after rebuilds.
Collaborates with: `SgfView`.

### SGF scroller (`src/sgf_view_scroller.c`, `src/sgf_view_scroller.h`)
Module: selection scroll helper.
Role: remember selected node scroll requests and retry them after layout updates with detailed adjustment diagnostics.
Collaborates with: `SgfView`, SGF node widget mapping, layout extents, and selection controller updates.

### SGF selection controller (`src/sgf_view_selection_controller.c`, `src/sgf_view_selection_controller.h`)
Module: SGF selection logic.
Role: track SGF selection and update CSS classes.
Collaborates with: `SgfView` and `SgfTree`.
