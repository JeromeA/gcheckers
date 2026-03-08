# Project overview

## `GCheckersWindow` (`src/window.c`)
Class: `GCheckersWindow` (`GtkApplicationWindow`).
Role: composition root that binds model state to UI updates, keeps board input available, and coordinates auto-play.
Owns: `GCheckersModel`, `BoardView`, `PlayerControlsPanel`, and `GCheckersSgfController`.
Collaborates with: `gcheckers_style_init()` for CSS, model signals for refresh, and SGF analysis signals to reset
player dropdowns. Computer turns are routed by control mode with alpha-beta depth configured from the shared
`Computer level` slider (`0..16`). Uses a three-pane layout: board and player controls (left), SGF mode selector
and SGF view (middle), and analysis (right) with both an `Analyze` toggle (iterative deepening on current SGF node)
and an `Analyze full game` button (fixed-depth analysis of all SGF nodes).
Worker output is staged through a mutex-protected shared report buffer, and the GTK text view is refreshed from the
main thread every 100ms while analysis is active. During iterative deepening, intermediate node-count snapshots are
published and shown with a temporary `(searching...)` marker. Completed results are converted to `SgfNodeAnalysis` and
attached to SGF nodes on the main thread, while text in the panel is formatted from that structured node analysis.
Analysis reports TT hit/probe/cutoff counters and hit ratio, while reusing a single TT allocation across passes.
Owns an analysis graph widget that shows branch values for the current SGF branch (root->current plus current->main
line end), tracks SGF selection with a vertical cursor, and supports click-to-select SGF navigation.
Top-level menu actions are
also exposed in a toolbar
(`New game...`, `Force move`, SGF timeline rewind/step/skip actions) via GTK actions.
Owns modal flows for `New game` and `Import games` wizards.
`New game` shows a ruleset dropdown (American/International/Russian) with a concise summary label below it.
Ruleset names, summaries, and `CheckersRules` construction are defined in one shared catalog (`rulesets.c`).
The modal is non-resizable and renders the ruleset summary as a single-line ellipsized label so switching rulesets
does not trigger dialog height growth or leave extra blank space below action buttons.
Import wizard persists BoardGameArena email/password and remember flag with `GSettings` when fetching history, and
prefills credentials on the credentials step from stored values. Parsed login responses drive in-memory result
handling; status/error responses trigger an error dialog and close the wizard. Successful login advances to a history
step that lists checkers games as `table_id` + `player_one vs player_two`.
Import fetch flow for BoardGameArena uses a dedicated libcurl client: GET home page, extract `requestToken`, then
POST `loginUserWithPassword.html` with username/password/remember/request token and logs the HTTP/body result.
Default panel widths target about `500/300/300` pixels at the default window width (`1100x700`).
Lifecycle: sinks and retains an owned `PlayerControlsPanel` reference, removes it from its current `GtkBox` parent
during dispose via `gcheckers_widget_remove_from_parent()`, and then clears its references.
during dispose, cancels any pending auto-move idle source, and then clears its references.

## `GCheckersSgfController` (`src/sgf_controller.c`)
Class: `GCheckersSgfController` (`GObject`).
Role: SGF timeline authority and synchronization point between SGF current-node transitions and game state updates.
Move application is SGF-first: validate model move, append under SGF current, set SGF current, then project that
transition to the model (`single move` if parent->child, otherwise reset+replay from root).
`gcheckers_sgf_controller_set_model()` only binds/disconnects model references; timeline clearing is explicit via
`gcheckers_sgf_controller_new_game()`. Exposes SGF navigation helpers used by window actions: rewind to root, step
backward, step forward on main line, step to next branch point, and step to main-line end.
Selection-only navigation updates SGF view selection in place (`sgf_view_set_selected`) instead of rebuilding the
entire SGF layout.
Owns: `SgfTree` and `SgfView`, plus replay guard (`is_replaying`).
Signals: `analysis-requested` when analysis panel content should refresh for the selected node, and `node-changed`
whenever SGF current node changes so other UI (analysis graph) can synchronize cursor state.
Collaborates with: `GCheckersModel` for move validation/application, `BoardView` to clear selection on replay/reset,
and `GCheckersWindow` via the `analysis-requested` signal.

## `AnalysisGraph` (`src/analysis_graph.c`, `src/analysis_graph.h`)
Class: `AnalysisGraph` (`GObject`).
Role: wraps a `GtkDrawingArea` chart for SGF branch evaluations. Draws best-score points/segments from per-node
`SgfNodeAnalysis`, renders a vertical selected-node bar, and maps pointer clicks to nearest node index.
Signals: `node-activated` with the clicked branch node pointer so window code can call SGF controller selection APIs.
Collaborates with: `GCheckersWindow` (data binding) and `GCheckersSgfController` (selection updates).

## `PlayerControlsPanel` (`src/player_controls_panel.c`)
Class: `PlayerControlsPanel` (`GtkBox`).
Role: encapsulates player mode controls.
Modes: white/black each select `User` or `Computer`, plus a shared `Computer level` slider (depth `0..16`).
Defaults: both white and black controls start as `User`.
Signals: `control-changed` for window-level coordination.
Collaborates with: `GCheckersWindow` (signal handlers and `player_controls_panel_set_all_user()`) and GTK widgets
(`GtkDropDown`, `GtkScale`).

## `gcheckers_style_init()` (`src/style.c`)
Module: `gcheckers_style_init()` (style helper, not a class).
Role: installs application CSS once per process using `g_once_init_enter/leave`, including SGF disc colors.
Owns: CSS string and `GtkCssProvider` setup.
Collaborates with: `GdkDisplay`/`GtkStyleContext` and is invoked by `GCheckersWindow`.

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
Role: define game types, rule enforcement, promotion, winner updates, and the public game API.
Collaborates with: `move_gen.c` for move enumeration and `checkers_model.c` for GTK integration.
Game creation is explicit via `game_init_with_rules()`; callers fetch concrete presets from the shared ruleset catalog
before initialization.

## Ruleset catalog (`src/rulesets.c`, `src/rulesets.h`, `src/ruleset.h`)
Module: ruleset metadata and presets.
Role: central single source of truth for ruleset IDs, display names, UI summaries, and `CheckersRules` values in one
enum-indexed table.
Collaborates with: `window.c`/`new_game_dialog.c` for UI selection + summaries, and all game creators for explicit
`game_init_with_rules()` setup.

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
Role: wrap the engine for GTK, including move validation, alpha-beta move selection, state-change signals, and
last-move caching for board overlay rendering. Exposes structured move-analysis API
(`gcheckers_model_analyze_moves`) returning scored moves plus search stats.
Collaborates with: `GCheckersWindow` and SGF controllers via signals and high-level move APIs.

## AI alpha-beta search (`src/ai_alpha_beta.c`, `src/ai_alpha_beta.h`)
Module: alpha-beta search.
Role: choose a move and analyze all legal moves via depth-limited alpha-beta with a material heuristic and
terminal-win scoring. Root move choice randomizes among all equal best-scoring moves, so repeated games can vary
without lowering evaluation quality. Analysis APIs can report searched node counts and TT stats (probes/hits/cutoffs),
and TT stats accumulate when callers reuse the same `CheckersAiSearchStats` across calls. Callers can use
`checkers_ai_search_stats_clear()` for per-call snapshots or `checkers_ai_search_stats_add()` to aggregate
independent runs.
Search integrates zobrist hashing + a depth/bound/age transposition table and uses stored best moves for local move
ordering. Also exposes direct position scoring for tooling predicates.
Collaborates with: `checkers_model.c` for model-facing AI move selection and structured analysis APIs.

## Transposition table (`src/ai_transposition_table.c`, `src/ai_transposition_table.h`)
Module: transposition cache.
Role: fixed-size direct-mapped TT keyed by zobrist hash entries, storing depth, score, bound type, age generation, and
best move. Replacement prefers deeper entries and replaces old generations to keep iterative-deepening searches fresh.
TT entries are ephemeral search-cache data only (pruning and move ordering), not authoritative user-visible analysis
storage.
Collaborates with: `ai_alpha_beta.c`.

## Zobrist hashing (`src/ai_zobrist.c`, `src/ai_zobrist.h`)
Module: position hashing.
Role: deterministic 64-bit keying of board occupancy, board size, side to move, and winner state.
Collaborates with: TT probe/store in `ai_alpha_beta.c`.

## Position search helpers (`src/position_search.c`, `src/position_search.h`)
Module: position traversal.
Role: enumerate game positions over a ply range, apply caller-provided predicates, report matches via callbacks, and
optionally deduplicate transpositions by board state + side to move.
Collaborates with: predicate and formatting modules plus `find_position`.

## Position predicates (`src/position_predicate.c`, `src/position_predicate.h`)
Module: reusable position predicates.
Role: provide search predicates and helpers such as "alpha-beta score is non-zero at depth N", with a cached score
for immediate match reporting.
Collaborates with: `ai_alpha_beta.c` and `position_search.c`.

## Position formatting (`src/position_format.c`, `src/position_format.h`)
Module: position output formatting.
Role: format move sequences for CLI/tooling output.
Collaborates with: `find_position` and search callbacks.

## BoardGameArena client (`src/bga_client.c`, `src/bga_client.h`)
Module: BoardGameArena login HTTP client.
Role: perform libcurl requests to fetch `requestToken` from `https://en.boardgamearena.com/`, then submit
`username`/`password`/`remember_me`/`request_token` to
`https://en.boardgamearena.com/account/auth/loginUserWithPassword.html`, then prefetch
`https://boardgamearena.com/gamestats?...` and refresh `requestToken` from that page before fetching checkers history
from `https://boardgamearena.com/gamestats/gamestats/getGames.html` for the authenticated user/session.
All BoardGameArena HTTP response bodies are saved to `/tmp/gcheckers-bga-*.txt` for debugging.
History parsing extracts each table's `table_id`, start timestamp (rendered as `YYYY-MM-DD HH:MM`, UTC), and player
names.
Collaborates with: import dialog flow for "Fetch game history" and `tests/test_bga_client.c` (token/login/history
parsing + live login smoke test with env-provided credentials).

## CLI entry point (`src/checkers_cli.c`)
Module: CLI front end.
Role: provide a prompt-driven loop for human vs. AI play in the terminal.
Collaborates with: `game.c` and `game_print.c`.

## Position finder CLI (`src/find_position.c`)
Module: CLI front end.
Role: hardcode ad hoc position-search queries by combining reusable search traversal, predicates, and line formatters.
Current query scans unique positions after exactly four plies from the initial state and prints lines where at least
one of those four plies is a deep mistake: not a mistake at depth 8, but a mistake at depth 10.
Collaborates with: `position_search.c`, `position_predicate.c`, and `position_format.c`.

## GTK application entry (`src/gcheckers.c`, `src/application.c`, `src/application.h`)
Class: `GCheckersApplication` (`GtkApplication`).
Role: define the GTK application type and activation flow that creates the main window and model, installs app actions
(`app.new-game`, `app.import`, `app.force-move`, `app.quit`), installs window SGF navigation actions, and publishes a
menubar model (`File` -> `New game...`, `Import...`, `Load...`, `Save as...`, `Quit`; `Game` -> `Force move` + SGF
navigation section) with keyboard accelerators.
Collaborates with: `GCheckersWindow` for UI wiring and new-game dialog presentation.

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

### Man paintable (`src/man_paintable.c`, `src/man_paintable.h`)
Module: `GdkPaintable` factory.
Role: render checker men and kings as paintables for GTK widgets.
Collaborates with: `PiecePalette` and board rendering.

## SGF subsystem

### SGF tree (`src/sgf_tree.c`, `src/sgf_tree.h`)
Module: SGF tree storage.
Role: manage move nodes, parent/child links, SGF property access, traversal helpers, and the SGF current-node timeline
used as the source of truth for move chronology/navigation. Nodes also carry optional structured analysis
(`SgfNodeAnalysis`) containing depth, search stats, and best-to-worst scored legal moves.
Traversal helpers include root-to-node path construction, main-line collection from arbitrary nodes, current-branch
construction for graphing, and deterministic preorder collection for full-tree analysis jobs.
Collaborates with: SGF view and controller modules.

### SGF move properties (`src/sgf_move_props.c`, `src/sgf_move_props.h`)
Module: SGF move property helpers.
Role: convert between SGF move properties (`B[...]`/`W[...]`) and typed `CheckersMove` values used by controller/model
paths.
Collaborates with: `sgf_io` and `GCheckersSgfController`.

### SGF IO (`src/sgf_io.c`, `src/sgf_io.h`)
Module: SGF load/save core.
Role: serialize and deserialize SGF trees using SGF syntax (`(`, `)`, `;`, `PROP[...]`) with move properties
`B[...]`/`W[...]` and standard SGF variation nesting for branches. gcheckers writes SGF metadata (`FF`, `CA`, `AP`,
`GM`) and does not persist current UI selection. Node analysis persists through custom properties:
`GCAD[depth]`, `GCAS[nodes=...;tt_probes=...;tt_hits=...;tt_cutoffs=...]`, and repeated
`GCAN[move:score]`. This layer is GTK-free so it can be reused by both GUI actions and future CLI commands.
Collaborates with: `GCheckersSgfController` load/save entry points and `tests/test_sgf_io.c`.

### SGF view (`src/sgf_view.c`, `src/sgf_view.h`)
Class: `SgfView` (`GtkWidget`).
Role: game-agnostic move tree UI that wires together layout, rendering, selection helpers, and selection resync calls.
The SGF disc grid (`tree_box`) is measured directly by the overlay (via `gtk_overlay_set_measure_overlay`) so no manual
size requests are applied to the overlay stack. It syncs selection after layout updates with debug logging when widgets
are not ready, and annotates notify-driven resync attempts with the emitting object/property pair.
Collaborates with: SGF layout (layout-updated signal), selection, scroller, and disc factory helpers.

### SGF disc factory (`src/sgf_view_disc_factory.c`, `src/sgf_view_disc_factory.h`)
Module: disc widget creation.
Role: build SGF move buttons (including the virtual move zero dot) and wire the `node-clicked` signal.
Collaborates with: `SgfView` and the SGF tree.

### SGF layout (`src/sgf_view_layout.c`, `src/sgf_view_layout.h`)
Module: layout helpers.
Role: position discs in a grid-based SGF tree layout (anchoring the virtual root in column zero) and emit a
layout-updated signal after rebuilds.
Collaborates with: `SgfView` and link rendering.

### SGF link renderer (`src/sgf_view_link_renderer.c`, `src/sgf_view_link_renderer.h`)
Module: connector renderer.
Role: compute disc bounds/centers and draw connector lines between SGF node discs. First-child links are direct,
second-child links are direct diagonals, and child index 3+ uses a two-segment route (vertical to previous sibling
row, then diagonal to the target) to keep dense branching readable.
Collaborates with: SGF node widget mapping and view sizing.

### SGF scroller (`src/sgf_view_scroller.c`, `src/sgf_view_scroller.h`)
Module: selection scroll helper.
Role: `sgf_view_scroller_scroll()` remembers selected-node context, attempts immediate horizontal clamping from selected
widget bounds (`[bounds.origin.x, bounds.origin.x + bounds.size.width]`), and internally schedules idle retries only
for transient geometry readiness paths. Missing selected-node widget mappings are logged with a hash-table dump and not
retried to avoid perpetual idle loops on stale selection pointers. Callers use one API and do not handle retry paths.
Collaborates with: `SgfView`, SGF node widget mapping, and selection controller updates.

### SGF selection controller (`src/sgf_view_selection_controller.c`, `src/sgf_view_selection_controller.h`)
Module: SGF selection logic.
Role: track SGF selection, update CSS classes, and navigate siblings and parents.
Collaborates with: `SgfView`, the SGF tree, and the scroller.

### SGF file actions (`src/sgf_file_actions.c`, `src/sgf_file_actions.h`)
Module: GTK SGF file action integration.
Role: register `win.sgf-load` and `win.sgf-save-as` actions, present `GtkFileDialog` file pickers, call SGF
controller load/save APIs, and show errors as modal dialogs.
Collaborates with: `GCheckersWindow` action map and `GCheckersSgfController`.
