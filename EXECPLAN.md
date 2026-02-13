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

- Pass 7: Removed non-essential window chrome in `gcheckers_window` (status label, reset button, and extra controls row container).
  Difficulty: low; SGF reproduction path uses auto/forced moves and SGF panel, so these controls were leaf UI only.
- Pass 8: Removed unused public `gcheckers_window` getters for controls panel and SGF controller.
  Difficulty: low; no in-tree callers depended on these accessors.
- Pass 9: Removed SGF panel CSS class attachment from window initialization.
  Difficulty: low; style hook is cosmetic and does not affect scroll logic.
- Pass 10: Simplified window refresh helper from status formatting + board refresh down to board refresh only.
  Difficulty: low; status text no longer exists after UI pruning.
- Pass 11: Removed reset-button callback path and all related wiring.
  Difficulty: low; reset remains reachable via SGF controller codepaths but no longer exposed in the top-level window.

- Pass 12: Removed unused status-formatting API (`gcheckers_model_format_status`) and its game winner-label helper.
  Difficulty: low; both symbols were orphaned after prior UI simplification and did not affect SGF scrolling.
- Pass 13: Demoted `gcheckers_sgf_controller_reset()` from public API to private internal helper.
  Difficulty: low; only in-file callers existed, so behavior remained identical with smaller public surface.
- Pass 14: Removed unused SGF tree traversal helper `sgf_tree_build_main_line()`.
  Difficulty: low; no call sites existed in app or tests, so this was dead-code elimination only.
- Pass 15: Replaced `PlayerControlsPanel` custom type with a plain dummy `GtkBox` in `gcheckers_window` and dropped the module.
  Difficulty: medium; required ownership/dispose path updates and build wiring cleanup while preserving window structure.
- Pass 16: Removed public `sgf_view_set_selected()` export and kept selection setter internal to `sgf_view.c`.
  Difficulty: low; all usage was already internal and selection behavior remains unchanged.

## Validation
`make test` is the gate for this minimization pass and still reproduces the bug via
`GTK SCROLLEDWINDOW INCONSISTENCY ... THIS SHOULD NEVER HAPPEN.`
