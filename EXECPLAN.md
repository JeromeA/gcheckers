# 2026-02-13 Leaf-feature pruning pass log

## Purpose
Trim unrelated leaf UI code while keeping the SGF scrolled-window bug reproducible.

## Passes completed

- Pass 1: Removed `player_controls_panel_set_force_move_sensitive()` and its call site in `gcheckers_window.c`.
  Difficulty: low; behavior unchanged because forced-move button state is not required for automated reproduction.
- Pass 2: Removed board-square index overlay rendering and converted `board_square_set_index()` into a no-op.
  Difficulty: medium; retained API to avoid broad call-site churn while deleting leaf UI details.
- Pass 3: Removed unused `GtkPaned`/`GtkStack` removal branches from `widget_utils.c`.
  Difficulty: low; current codepaths only require box/grid/overlay parent removal.

- Pass 4: Removed unused SGF-controller passthrough getters (`get_tree`/`get_view`) and kept only widget + replay state APIs.
  Difficulty: low; no call sites depended on these internals.
- Pass 5: Removed unused board-size getter from `board_grid` and trimmed SGF view public API (`get_selected`, public `force_layout_sync`) to internal-only usage.
  Difficulty: low; both were leaf exports with no external consumers.
- Pass 6: Removed forced-move console logging helper from `gcheckers_window` and call site, preserving move execution.
  Difficulty: low; UI behavior and model stepping remain unchanged.

## Validation
`make test` is the gate for this minimization pass and still reproduces the bug via
`GTK SCROLLEDWINDOW INCONSISTENCY ... THIS SHOULD NEVER HAPPEN.`
