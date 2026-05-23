# Project overview

This repository contains the `ggame` application framework plus three branded GTK targets, `gcheckers`, `gboop`, and
`ghomeworlds`. The shared code in top-level `src/` owns the application shell, GTK UI, SGF workflows,
puzzle/import/reporting flows, and the generic AI/model/backend interfaces.
Game-specific code lives under `src/games/<game>/` and provides the rules, position and move types, search
evaluation, notation helpers, optional puzzle tooling, and any board-specific callbacks needed by the selected
backend. `src/games/boop/` contains both the boop rules/backend and the boop-only supply/promotion controls that now
plug into the shared window, while `src/games/homeworlds/` provides the rules/backend plus a profile-owned toplevel
board host and SGF snapshot codec for the shared window. Homeworlds SGF snapshots are strict replay checkpoints:
numeric fields must be plain decimal digits, the pyramid supply must add up exactly, and duplicate system entries or
systems missing either a star or ships are rejected instead of being normalized into impossible positions.

The default build now compiles one shared object graph and links all three application binaries. Each process selects
exactly one `GGameAppProfile` at startup through its launcher, then shared code queries
`src/game_app_profile.[ch]` for the active profile and reaches the profile-owned `GameBackend` and feature/UI hooks
from there. Shared code talks to the rules/search/rendering layer through generic APIs such as `GGameModel`,
`ai_search`, SGF backend hooks, square-grid board callbacks, or a custom board host, so adding another game is mostly a
matter of adding a new backend directory plus the corresponding profile and packaging entries.

## `GGameAppProfile` (`src/game_app_profile.c`, `src/game_app_profile.h`)
Module: runtime app-profile registry and descriptors.
Role: describe each branded target, including app ID, display strings, settings schema ID, the profile-owned
`GameBackend *`, feature flags, derived helpers such as puzzle-catalog support, shared-window layout and computer-depth
defaults, and an optional board-host hook. Launchers select one of these profiles at startup, and
`ggame_active_app_profile()`
exposes the chosen profile to shared code. Profiles do not get a custom-window hook; the menu bar, toolbar, SGF
controller, and drawer actions are always owned by the generic application window. Board-host creation receives shared
options such as the initial move-report enabled state, so profile-owned hosts can avoid optional work before the first
refresh.
Collaborates with: `src/application.c`, `src/window.c`, `src/app_settings.c`, `src/file_dialog_history.c`, and
`src/active_game_backend.h`.

## `GGameWindow` (`src/window.c`)
Class: `GGameWindow` (`GtkApplicationWindow`).
Role: composition root that binds model state to UI updates, keeps board input available, and coordinates auto-play.
Owns: the active `GGameAppProfile`, one `GGameModel`, `BoardView`, `PlayerControlsPanel`, `GGameSgfController`, and
an optional profile-owned board host around the board. For boop, that board host adds the supply piles, selected-rank
highlight, promotion confirmation button, and boop color styling while keeping the same shared shell. For Homeworlds,
the board host replaces the square `BoardView` presentation with the system graph, bank piles, staged-choice controls,
and catastrophe controls while still routing completed moves through the shared SGF controller.
During Homeworlds multi-step actions, the source ship remains highlighted while the user chooses a trade color,
capture target, or move destination; build completes immediately and has no second selection step.
Collaborates with: `ggame_style_init()` for CSS, model signals for refresh, profile feature flags to enable/disable
actions, and SGF navigation signals to synchronize analysis and board-host state. Computer turns are routed by control
mode with alpha-beta depth configured from the shared `Computer depth` slider (`0..16`). Uses a three-pane layout: board
and player controls (left), SGF mode selector and SGF view (middle), and analysis (right). Analysis is launched from
shared window actions exposed in the `Analysis` menubar submenu: current-position analysis iterates on the selected
node, and full-game analysis always processes nodes in reverse order so TT state is reused from later positions
first. Current-position analysis now runs through the generic backend AI API for every shared-shell build, while
full-game analysis keeps the checkers setup-aware replay path for checkers and uses backend-position SGF replay for
boop and Homeworlds.
Shared pane and computer-depth defaults also come from the active profile. Checkers keeps the historical `500/300/300`
board, navigation, and analysis widths with both drawers visible by default, while boop starts with a wider `760` board
pane and the analysis drawer hidden by default so its square board host can reach the same practical size as the old
standalone `gboop` window. Homeworlds starts with a wider `960` board pane because the custom board host contains both
the scrollable board viewport and Homeworlds action controls, but its profile uses a smaller board-pane minimum so the
splitter can still move. The shared square-board height clamp only applies to square-grid profiles, so Homeworlds can
use the full horizontal split while its own board viewport scrolls. It starts with computer depth `0` because its move
search is expensive. Even with its hidden default, boop now enables the shared `Analysis` actions and populates the
drawer on demand.
The analysis pane owns its own `Analysis depth` slider; analysis no longer reuses the player `Computer depth`
setting. Current-position analysis iterates up to the selected depth, and full-game analysis uses the same selected
depth as a fixed search limit. Analysis menu entries are one-shot actions, so SGF navigation does not implicitly keep
restarting current-position analysis. Full-game analysis reconstructs each node position from SGF replay semantics
(setup properties plus moves from root to the exact node), so setup-root puzzle files and edited SGF setup nodes share
the same position source of truth as normal controller navigation.
Board orientation is runtime-only window state: live games choose `follow-player`, `follow-turn`, or `fixed`
orientation based on the new-game player modes, and SGF review/manual navigation switches back to `fixed` so analysis
navigation does not keep rotating the board. SGF navigation does not mutate the player-mode dropdowns; `User` and
`Computer` selections persist until the user or new-game settings explicitly change them.
Those shared board-orientation and board-input-enable decisions now come from the cached `GGameModel` through backend
`position_turn()` and `position_outcome()` callbacks rather than directly inspecting checkers state.
Puzzle mode starts with a modal chooser (`src/puzzle_dialog.c`) that lets the user pick one backend
variant when the active backend has variants, then click a numbered puzzle square from a ten-column grid. Zero-variant
backends such as boop skip the variant row and load their single catalog directly. The dialog only reports
cancel-or-selected results back to `GGameWindow`; the window owns opening the selected puzzle and starting progress
tracking. The grid is built from the selected variant directory only and shows local status per puzzle: untried squares
are white, solved squares are green, and tried-without-success squares are red. Runtime loading resolves the puzzle root
(`GCHECKERS_PUZZLES_DIR` or the installed/local application data search path), appends the active game ID plus the
selected variant short name when present, such as `checkers/american` or `checkers/russian`; for boop it appends only
`boop`. It then loads the exact clicked `puzzle-*.sgf`. While active it hides both drawers, disables SGF/review
actions, shows puzzle-only `Next puzzle` and `Analyze` buttons, and validates the player's moves against the SGF
main-line solution while auto-playing defender replies. Puzzle steps are stored as backend-sized move snapshots plus
generic side numbers, so checkers and boop share the same playback code while each backend keeps its own notation and
move type. `Next puzzle` now advances through the sorted catalog for the active variant or zero-variant backend,
wrapping to the first puzzle after the last one, and no longer selects a random puzzle. Puzzle `Analyze` exits puzzle
mode, rewinds fully to move 0, and then starts the same full-game analysis path used by the Analysis menu. The current
node report still follows normal SGF selection instead of being replaced by a generic completion message. Picker
squares keep custom status colors and now define an explicit darker `:active` state so mouse presses remain visible
even with the custom button styling.
Puzzle mode now also records local progress for each opened puzzle entry. Opening the puzzle creates an append-only
attempt record with `started_unix_ms`, terminal outcomes are `success`, `failure`, or `analyze`, and the first wrong
move is stored only when the failure happened on the very first attempted move. A started-but-unfinished puzzle entry
is resolved as `failure` when the user starts a different puzzle, starts a new game, imports another game, or closes
the window.
Puzzle entry forces a fixed attacker-at-bottom orientation, while puzzle exit restores only layout/drawer state and
leaves the current board orientation unchanged.
Adds an `Analysis` menubar submenu for current-position and whole-game analysis, plus a `View` submenu with
independent toggles for the navigation drawer and analysis drawer; Homeworlds also exposes a `Move report` toggle
there. Hiding both drawers removes the entire right-side drawer split while preserving the board pane. The analysis
drawer keeps the text report bound to the currently selected SGF node, and shows transient analysis progress in a
separate status label under the graph instead of overwriting the report text. Full-game analysis status uses one shared
formatter from the initial `0/n` state through later progress
updates instead of switching from a separate startup string, includes the cumulative explored-node count in the status
label, and refreshes on the same 100 ms throttle used by current-position analysis while a node is being searched.
Panel width state is retained for the board, navigation drawer, and analysis drawer, and drawer show/hide transitions
recompute window width plus paned positions so visible panels keep their prior widths instead of stretching.
Puzzle mode also owns the toplevel default width while active, so foreign GTK/default-size changes are reasserted back
to the puzzle layout instead of expanding the window to the normal three-pane width.
Mode dropdown supports `Play` and `Edit`. In `Edit`, board clicks mutate SGF setup properties on the current node:
left click cycles empty->white man->white king->empty (with black pieces and any king clearing to empty), right click
mirrors this for black. SGF navigation, SGF node deletion, and `Force move` actions are disabled while `Edit` is
active.
Worker output is staged through a mutex-protected shared report buffer, and the GTK text view is refreshed from the
main thread every 100ms while analysis is active. During iterative deepening, intermediate node-count snapshots are
published and shown with a temporary `(searching...)` marker. Completed results are converted to `SgfNodeAnalysis` and
attached to SGF nodes on the main thread, while text in the panel is formatted from that structured node analysis.
Analysis score text always shows an explicit `+` sign for positive centipawn-style values and converts terminal
`2900..3000` magnitudes into compact `W#X` / `B#X` mate-distance-style labels.
Per-move analysis lines also include the root-search node count used to score that move, making TT-assisted shortcuts
visible in the report text.
Static material in search also values man advancement: men gain `+1/+2/+3` as they get within three rows of
promotion, while the standalone static-material API remains pure material.
Full-game completion gating uses processed-job counts (not only payload-attached counts), so terminal/no-move nodes do
not leave the whole-game analysis action disabled after completion.
Analysis lifecycle transitions are centralized (begin/finish/sync-ui helpers), so full-game button state, transient
graph progress highlight, and runtime counters reset from one source of truth.
Analysis text reports depth, node count, and scored moves, while reusing a single TT allocation across passes.
Owns an analysis graph widget that shows branch values for the current SGF branch (root->current plus current->main
line end), tracks SGF selection with a vertical cursor, and supports click-to-select SGF navigation. During
full-game analysis, the latest node that received analysis is highlighted in yellow when it is visible on the current
branch.
Top-level menu actions are
also exposed in a toolbar
(`New game...`, `Force move`, SGF timeline rewind/step/skip/delete actions, and analysis actions) via GTK actions.
Owns modal flows for `New game` and `Import games` wizards.
`New game` now builds its optional `Variant` dropdown and summary label from the active backend metadata rather than
hard-coding checkers names in the dialog. `GGameWindow`'s public new-game and puzzle APIs now take backend variants,
while `window.c` keeps the current checkers-only ruleset mapping private. Side labels (`White`/`Black` today) and
variant display text come from the backend. When a backend exposes no variants, the dialog omits that row and keeps
the current game setup. The modal remains
non-resizable and renders the summary as a single-line ellipsized label so variant switches do not change dialog
height or leave extra blank space below the action buttons.
Import wizard persists BoardGameArena email/password and remember flag with `GSettings` when fetching history, and
prefills credentials on the credentials step from stored values. Parsed login responses drive in-memory result
handling; status/error responses trigger an error dialog and close the wizard. Successful login advances to a history
step that lists checkers games as `table_id` + `player_one vs player_two`.
Import fetch flow for BoardGameArena uses a dedicated libcurl client: GET home page, extract `requestToken`, then
POST `loginUserWithPassword.html` with username/password/remember/request token and logs the HTTP/body result.
Default panel widths come from the active profile, with `500/300/300` as the checkers baseline. Profiles can also set a
smaller minimum board-panel width when the startup/default board width should not become the paned handle's hard limit.
Lifecycle: sinks and retains an owned `PlayerControlsPanel` reference, removes it from its current `GtkBox` parent
during dispose via `ggame_widget_remove_from_parent()`, and then clears its references.
during dispose, cancels any pending auto-move idle source, and then clears its references.
At construction time it also pushes the active backend's side labels into `PlayerControlsPanel`, so the shared board
UI keeps generic two-side semantics while the active backend decides how those sides are named.

## `GGameSgfController` (`src/sgf_controller.c`)
Class: `GGameSgfController` (`GObject`).
Role: SGF timeline authority and synchronization point between SGF current-node transitions and game state updates.
Move application is SGF-first: validate the move against the bound model, append or reuse the matching SGF child
under SGF current, set SGF current, then project that transition back into the model (`single move` if parent->child,
otherwise reset+replay from root).
After a move has been accepted and projected, the controller saves the current SGF tree through the SGF autosave
helper. The autosave session timestamp is reset when the controller starts a new game or successfully loads an SGF
file, so each play session gets its own filename prefix.
Replay now delegates node-setup handling to optional backend SGF hooks before replaying any `B[...]`/`W[...]` move on
that node. Checkers uses that hook for `AE`/`AB`/`AW` plus `ABK`/`AWK` king markers and `PL`, while boop uses it for
custom root snapshot properties that restore on-board kittens/cats, per-side supplies, total ply count, and `PL`. Root
`RU[<ruleset-short-name>]` is still read on load to switch the legacy checkers model to the matching ruleset before
replay, and SGF loads now fail if `RU` is missing or unknown instead of falling back to the current model rules.
SGF parsing also rejects non-whitespace content after the parsed tree, so malformed files cannot be accepted by
silently ignoring trailing bytes.
`RU` is stamped back onto fresh trees and saved SGFs when the active backend has variants; zero-variant backends such
as boop accept and write no `RU`. SGF move colors also come from the active backend through `sgf_color_for_side()` so
checkers can keep side 0 as white while boop maps side 0 to black. `Save position...` now uses the same backend hook
layer in reverse: the shared controller creates a fresh one-node SGF tree, asks the active backend to write a root
position snapshot, and then saves through `sgf_io_save_file()`. That keeps position-only SGFs shared while leaving the
actual snapshot encoding backend-owned.
`ggame_sgf_controller_set_model()` binds the legacy checkers wrapper plus its inner `GGameModel`;
`ggame_sgf_controller_set_game_model()` binds generic callers directly. Timeline clearing remains explicit via
`ggame_sgf_controller_new_game()`. Exposes SGF navigation helpers used by the shared window: rewind to root, step
backward, step forward on main line, step to next branch point, and step to main-line end. Navigation-driven model
synchronization runs under the replay guard so the shared window does not treat stepping through the tree as a newly
played move.
Deleting the current non-root SGF node removes that node and all descendants, selects the parent node, replays the
model back to that parent, refreshes the SGF view, and saves the edited SGF through the same autosave path as move
application.
Selection-only navigation updates SGF view selection in place (`sgf_view_set_selected`) instead of rebuilding the
entire SGF layout.
Exposes a current-node refresh helper that replays SGF state into the model after setup-property edits on the current
node.
Owns: `SgfTree` and `SgfView`, plus replay guard (`is_replaying`).
Signals: `manual-requested` when analysis panel content should refresh for the selected node, and `node-changed`
whenever SGF current node changes so other UI (analysis graph) can synchronize cursor state.
Collaborates with: `GCheckersModel` compatibility callers plus generic `GGameModel` callers for move
validation/application, `BoardView` to clear selection on replay/reset, and `GGameWindow` via the `manual-requested`
signal for SGF navigation/edit flows. Starting a fresh game resets the SGF tree and emits `node-changed`, but does
not force player controls back to user mode. Manual SGF navigation also cancels any pending automatic computer reply.
Also exposes the current node's move so board overlays can use the same path for step-by-step and replay-based
navigation.
Pending move confirmations can still accept further backend builder steps before falling back to selection reset, which
lets boop disambiguate between confirming a single-kitten graduation and continuing to select a line promotion.

## `AnalysisGraph` (`src/analysis_graph.c`, `src/analysis_graph.h`)
Class: `AnalysisGraph` (`GObject`).
Role: wraps a `GtkDrawingArea` chart for SGF branch evaluations. Draws best-score points/segments from per-node
`SgfNodeAnalysis`, renders a vertical selected-node bar, highlights a window-provided progress node in yellow, and
maps pointer clicks to nearest node index.
Display scaling: graph y-values apply score compression `f(x)=x/(1+abs(x)/1800)` before plotting.
Y-axis range always includes at least `[-200, +200]` (and expands as needed), with guide ticks at `-200`, `-100`,
`100`, and `200` shown as scaled labels (`-2`, `-1`, `1`, `2`). Chart background is split at the zero line with a
slight white tint above and slight black tint below to indicate white/black advantage regions.
Score convention: positive means white is better, negative means black is better (independent of side to move).
Signals: `node-activated` with the clicked branch node pointer so window code can call SGF controller selection APIs.
Collaborates with: `GGameWindow` (data binding) and `GGameSgfController` (selection updates).

## `PlayerControlsPanel` (`src/player_controls_panel.c`)
Class: `PlayerControlsPanel` (`GtkBox`).
Role: encapsulate two-side player mode controls.
Modes: side 0 / side 1 each select `User` or `Computer`, plus a shared `Computer depth` slider (`0..16`).
Defaults: side 0 starts as `User`, side 1 starts as `Computer`, and the active profile supplies the initial computer
depth. Labels are backend-supplied by the window (`White`/`Black` for the current checkers backend).
Signals: `control-changed` for window-level coordination.
Collaborates with: `GGameWindow` signal handlers and GTK widgets (`GtkDropDown`, `GtkScale`).

## `Puzzle Catalog` (`src/puzzle_catalog.c`, `src/puzzle_catalog.h`)
Module: backend-variant puzzle discovery helpers.
Role: scan one backend puzzle directory under the puzzle root, keep only `puzzle-####.sgf` files, parse their numeric
puzzle numbers, sort them ascending, and return explicit catalog entries with basename, full path, and stable
`puzzle_id`. For backends with variants, the catalog path is `puzzles/<backend-id>/<variant-short-name>/`; for
zero-variant backends, `variant == NULL` is valid and the path is `puzzles/<backend-id>/`.
Collaborates with: `puzzle_dialog.c` for the numbered chooser grid and `window.c` for next-puzzle selection inside the
active variant or backend catalog.
Storage shape: checked-in puzzles now live under `puzzles/checkers/<variant-short-name>/`, and stable puzzle IDs are
prefixed with the active game ID, for example `checkers/international/puzzle-0007.sgf` or
`boop/puzzle-0000.sgf`.

## Boop Puzzle Generator (`src/games/boop/boop_create_puzzles.c`)
Module: `boop_create_puzzles_main()`.
Role: profile-specific puzzle policy for `build/tools/boop_create_puzzles`. The CLI accepts `--depth`, `--save-games`,
`--check-existing`, and `--dry-run`; it rejects checkers-only `--ruleset` values because boop has no variants. It
writes generated puzzles under `puzzles/boop/`, using boop backend SGF root snapshots for board pieces, supplies, total
ply count, and side to move, and writes solution moves with the shared SGF move notation helpers. Candidate validation
uses the generic backend AI search API against `boop_game_backend` and keeps positions with a clear unique best move.
Count-mode and SGF-file analysis use the shared create-puzzles runner for depth-0 source-game generation, main-line
replay, and terminal progress. The check-existing path reloads each SGF and validates the saved line against current
search, then deletes or dry-runs invalid puzzle/game file pairs.
Collaborates with: `src/create_puzzles_runner.c`, `src/ai_search.c`, `src/puzzle_catalog.c`,
`src/create_puzzles_progress.c`, `src/sgf_io.c`, `src/sgf_move_props.c`, and boop's `boop_sgf_position.c`.

## `ggame_style_init()` (`src/style.c`)
Module: `ggame_style_init()` (style helper, not a class).
Role: installs application CSS once per process using `g_once_init_enter/leave`, including SGF disc colors and the
colored puzzle-picker square styles, plus transparent Homeworlds board/bank choice hitboxes, stable compact bank-pile
button sizing, and active-ship highlights with visible borders.
Owns: CSS string and `GtkCssProvider` setup.
Collaborates with: `GdkDisplay`/`GtkStyleContext` and is invoked by `GGameWindow`.

## `GGameApplication` (`src/application.c`)
Class: `GGameApplication` (`GtkApplication`).
Role: top-level application shell that installs menu actions, creates the main window, and now owns shared puzzle
progress reporting state.
Owns: the application menubar/actions plus one `GGamePuzzleProgressStore` for the process, the configured report
URL (`GCHECKERS_PUZZLE_REPORT_URL`), the privacy/settings action, and the single in-flight background upload task.
Collaborates with: `GGameAppProfile` for app ID and feature availability, `GGameWindow`, which asks for the shared
store indirectly by attaching to this application, and `puzzle_progress.c`, which provides history storage, threshold
decisions, and upload JSON formatting. Puzzle uploads are also gated by the `send-puzzle-usage-data` application
setting before any network request is attempted, and the upload response discard callback validates libcurl byte
counts before returning them. The shared progress store accessor refreshes the store if
`GCHECKERS_PUZZLE_PROGRESS_DIR` resolves to a different state directory, which keeps test and manual override sessions
isolated.

## Application Settings (`src/app_settings.c`, `src/app_settings.h`, `src/settings_dialog.c`, `src/settings_dialog.h`)
Module: GSettings-backed application preferences and the modal settings UI.
Role: load the active profile's settings schema (currently `io.github.jeromea.gcheckers` or
`io.github.jeromea.gboop`), expose the privacy keys, and present the `Settings` dialog from the File menu.
Settings: `send-puzzle-usage-data` defaults to true and is consulted before puzzle progress uploads; `send-
application-usage-data` also defaults to true and is stored for future telemetry work but is not consumed yet; and
`privacy-settings-shown` records whether the privacy dialog has already been presented to this user.
UI: the settings dialog is a small modal window with two checkboxes and `Cancel`/`Save` actions, following the same
simple GTK window pattern as the new-game and import dialogs. When the active profile also exposes a puzzle catalog,
it adds a `Puzzle Progress` section with the number of solved puzzles out of the currently available puzzle catalog and
a `Clear Progress` button that clears local attempt history plus the chooser status cache. Checkers counts every
variant catalog; boop counts the zero-variant `puzzles/boop/` catalog. Save and cancel destroy the modal immediately
so repeated openings do not leave hidden settings windows pending in the GTK toplevel list. On first launch,
`GGameApplication` presents this dialog automatically after
creating the main window so the user can review the privacy controls before continuing.

## Puzzle Progress Reporting (`src/puzzle_progress.c`, `data/schemas/io.github.jeromea.gcheckers.gschema.xml`,
`data/schemas/io.github.jeromea.gboop.gschema.xml`)
Module: persistent puzzle attempt storage and report payload preparation.
Role: keep a stable per-user identifier, store local puzzle attempt history, maintain a derived per-puzzle status
cache for the chooser grid, decide when unsent data is old or large enough to send, and build the full-history JSON
payload for the reporting server.
Storage layout: the preferred user ID storage is the `puzzle-user-id` GSettings key in
the active profile schema. Local history lives under
`~/.local/share/gcheckers/puzzle-progress/attempt-history.jsonl` by default, or under
`GCHECKERS_PUZZLE_PROGRESS_DIR` when that override is set for tests/manual runs. The derived chooser-status cache
lives beside it as `puzzle-status.json` in the same directory; no extra nested per-file directories are used.
History format: one JSON object per line with schema version, puzzle identity, timestamps, terminal result,
first-wrong-move metadata, and local report metadata (`first_reported_unix_ms`, `report_count`). The history is never
deleted after successful upload; successful sends only mark previously unreported resolved attempts as reported. The
reader accepts the escaped control characters that the writer emits in JSON strings, while rejecting raw control
characters that are invalid JSON, and rejects numeric range errors when loading stored history. The settings dialog can
explicitly clear local progress, which rewrites both the history and status cache as empty.
Status-cache format: one JSON document keyed by stable `puzzle_id` values such as
`checkers/russian/puzzle-0007.sgf`, storing reduced `untried`/`failed`/`solved` state plus minimal metadata. If the
cache is missing or corrupt,
`puzzle_progress.c` rebuilds it from `attempt-history.jsonl`.
Reporting policy: the application sends the full local resolved history when there are at least 10 unsent attempts, or
when there are at least 5 unsent attempts and the oldest unsent one is more than 24 hours old. Uploads are best-effort
and asynchronous so puzzle interaction stays responsive. This data is intended both for operational reporting and for
later puzzle-difficulty calibration work.

## Widget utilities (`src/widget_utils.c`, `src/widget_utils.h`)
Module: parent-removal helpers.
Role: safely detach widgets from common GTK containers (box, grid, overlay, paned, stack) before dropping the last
reference to avoid GTK4 dispose-time criticals.
Collaborates with: `GGameWindow`, `BoardView`, and SGF view helpers during disposal.

## Board primitives (`src/games/checkers/board.c`, `src/games/checkers/board.h`)
Module: board storage and helpers.
Role: define board data structures, coordinate conversion helpers, piece helpers, and reset/init logic.
Collaborates with: `game.c` for rules and state transitions, and `board_geometry.c` for one-time directional-ray
construction.

## Board geometry (`src/games/checkers/board_geometry.c`, `src/games/checkers/board_geometry.h`)
Module: precomputed directional traversal data.
Role: build and expose immutable per-board-size direction rays in playable-square index space. Direction order is API:
up-left, up-right, down-left, down-right.
Collaborates with: `move_gen.c` for hot-path move enumeration, and `board.c` for one-time index/coordinate conversion
while initializing the static geometry tables.

## Constants (`src/games/checkers/checkers_constants.h`)
Module: shared constants.
Role: centralize size limits for boards, moves, and byte storage used throughout the engine and UI.
Collaborates with: all game and model modules via shared size limits.

## Game engine (`src/games/checkers/game.c`, `src/games/checkers/game.h`)
Module: core game rules and state.
Role: define game types, rule enforcement, promotion, winner updates, and the public game API.
Collaborates with: `move_gen.c` for move enumeration and `checkers_model.c` for GTK integration.
Game creation is explicit via `game_init_with_rules()`; callers fetch concrete presets from the shared ruleset catalog
before initialization.

## Ruleset catalog (`src/games/checkers/rulesets.c`, `src/games/checkers/rulesets.h`,
`src/games/checkers/ruleset.h`)
Module: ruleset metadata and presets.
Role: central single source of truth for the checkers backend's ruleset IDs, display names, short names
(`american`, `international`, `russian`), UI summaries, and `CheckersRules` values in one enum-indexed table. Shared
code now consumes backend `GameBackendVariant` metadata instead, while `window.c` and other checkers-owned code use
this catalog to translate variants to concrete checkers rulesets.
Collaborates with: `window.c`, `checkers_create_puzzles.c`, and `create_puzzles_cli.c` for ruleset-targeted puzzle
generation, and all game creators for explicit `game_init_with_rules()` setup.

## Game printing (`src/games/checkers/game_print.c`)
Module: terminal formatting helpers.
Role: render board state and move notation for tooling and tests.
Collaborates with: game/SGF formatting callers.

## Move generation (`src/games/checkers/move_gen.c`)
Module: move generation.
Role: enumerate simple moves, jumps, and forced-capture rules.
Collaborates with: `game.c` to validate and apply generated moves, and `board_geometry.c` for direct index-space
direction traversal without per-step coordinate conversion.

## GTK model wrapper (`src/games/checkers/checkers_model.c`, `src/games/checkers/checkers_model.h`)
Class: `GCheckersModel` (`GObject`).
Role: wrap the engine for GTK, including move validation, alpha-beta move selection, state-change signals, and
last-move caching for board overlay rendering. Exposes structured move-analysis API
(`gcheckers_model_analyze_moves`) returning scored moves plus search stats. Also exposes `gcheckers_model_set_state()`
to publish replayed SGF positions (for setup/property-driven nodes) into the GTK model. It now also owns a synced
`GGameModel` mirror so shared square-grid UI can consume backend-driven state without pulling checkers headers, and it
mirrors generic model updates back into the legacy wrapper for compatibility callers.
Collaborates with: `GGameWindow`, SGF controllers, and shared square-grid board widgets.

## Generic AI search (`src/ai_search.c`, `src/ai_search.h`)
Module: backend-driven alpha-beta search.
Role: choose a move and analyze all legal moves via depth-limited alpha-beta using only `GameBackend` callbacks for
move generation, position copying, applying moves, static evaluation, terminal scoring, side-to-move inspection, and
hashing. Root move choice randomizes among all equal best-scoring moves, so repeated games can vary without lowering
evaluation quality. Analysis APIs can report searched node counts and TT stats (probes/hits/cutoffs), and TT stats
accumulate when callers reuse the same `GameAiSearchStats` across calls. Cancellable analysis owns any copied partial
root results until the call succeeds and frees them on cancellation.
This is now an optional backend capability: backends that opt into `supports_ai_search` must provide backend-sized move
records plus either `list_good_moves` or the full move-list API. The search layer now prefers `list_good_moves`, so a
backend can expose heuristic best-first subsets to AI without also exposing exhaustive move lists to shared code. Any
candidate truncation is backend-owned; the generic search does not pass a caller-selected cap.
Backends can opt into forced-ply extension through `extends_forced_moves`; checkers uses this so forced continuations
do not consume depth, while boop and Homeworlds use ordinary depth accounting and can statically evaluate depth-0 child
positions without generating their candidate moves first.
Score convention: search scores are white-centric at all plies (`+` good for white, `-` good for black). Root move
lists are ordered by side to move preference (white: high to low, black: low to high) so index 0 remains the best move
for the player to act.
Search integrates backend hashing plus a depth/bound/age transposition table and uses stored best moves for local move
ordering. Best moves are copied into the transposition table before the backend move list is released, because
`move_list_get()` pointers are only valid while their owner list is alive. Exposes both searched position scoring and
pure static scoring through generic APIs.
Collaborates with: `game_backend.h`, `tests/test_ai_search.c`, and the checkers compatibility wrapper.

## AI alpha-beta compatibility wrapper (`src/games/checkers/ai_alpha_beta.c`,
`src/games/checkers/ai_alpha_beta.h`)
Module: checkers-facing search compatibility.
Role: preserve the existing checkers-facing `Game` and `CheckersAiTranspositionTable` APIs while delegating the real
search work to `ai_search.c` through the checkers backend adapter.
Collaborates with: `checkers_model.c`, `checkers_create_puzzles.c`, and other existing checkers-only callers that
have not yet migrated to generic AI interfaces.

## Transposition table (`src/games/checkers/ai_transposition_table.c`,
`src/games/checkers/ai_transposition_table.h`)
Module: checkers-facing TT compatibility wrapper.
Role: preserve the existing checkers TT API while delegating storage to the generic backend-sized TT used by
`ai_search.c`. TT entries remain ephemeral search-cache data only (pruning and move ordering), not authoritative
user-visible analysis storage.
Collaborates with: `ai_search.c` and `ai_alpha_beta.c`.

## Zobrist hashing (`src/games/checkers/ai_zobrist.c`, `src/games/checkers/ai_zobrist.h`)
Module: position hashing.
Role: deterministic 64-bit keying of board occupancy, board size, side to move, and winner state.
Collaborates with: TT probe/store in `ai_alpha_beta.c`.

## BoardGameArena client (`src/bga_client.c`, `src/bga_client.h`)
Module: BoardGameArena login HTTP client.
Role: perform libcurl requests to fetch `requestToken` from `https://en.boardgamearena.com/`, then submit
`username`/`password`/`remember_me`/`request_token` to
`https://en.boardgamearena.com/account/auth/loginUserWithPassword.html`, then prefetch
`https://boardgamearena.com/gamestats?...` and refresh `requestToken` from that page before fetching checkers history
from `https://boardgamearena.com/gamestats/gamestats/getGames.html` for the authenticated user/session.
All BoardGameArena HTTP response bodies are saved to `/tmp/gcheckers-bga-*.txt` for debugging.
History parsing extracts each table's `table_id`, start timestamp (rendered as `YYYY-MM-DD HH:MM`, UTC), and trimmed
player names. Numeric JSON fields are parsed with explicit range checks before narrowing to local integer types, and
HTTP response buffering rejects impossible libcurl chunk sizes before appending response bytes.
Collaborates with: import dialog flow for "Fetch game history" and `tests/test_bga_client.c` (token/login/history
parsing + live login smoke test with env-provided credentials).

## Puzzle generator CLI (`src/create_puzzles.c`, `src/create_puzzles_launcher.c`, `src/create_puzzles_profile.c`)
Module: CLI front end and profile dispatch.
Role: choose the create-puzzles profile from the executable name, activate that app profile, then call the
profile-specific generator entry point registered in `create_puzzles_profile.c`. The top-level CLI source contains no
game-specific puzzle policy; checkers and boop policy live under their game directories. The unified build emits two
launcher names: `build/tools/checkers_create_puzzles` for checkers and `build/tools/boop_create_puzzles` for boop.
The checkers launcher keeps the historical default analysis depth `8`; the boop launcher uses default analysis depth
`4` and has no ruleset argument. `make create_puzzles` builds both launchers.
Collaborates with: `game_app_profile.c`, `create_puzzles_launcher.c`, `create_puzzles_profile.c`, and the
profile-specific generator modules.

## Puzzle generator runner (`src/create_puzzles_runner.c`, `src/create_puzzles_runner.h`)
Module: backend-driven source-game generation and SGF replay.
Role: play depth-0 source games through the active profile's `GameBackend`, store them as SGF with backend-owned root
setup and variant metadata, replay any generated or loaded SGF main line, validate move colors against side to move,
log shared progress phases, and pass `position_before`, `played_move`, and `position_after` to game-specific puzzle
policy callbacks. The runner is the only create-puzzles module that chooses self-play moves with
`game_ai_search_choose_move()` or replays source-game SGF main lines for candidate consideration.
It also owns the default count-mode source-game attempt limit used by checkers and boop so failed puzzle filters end
with a report instead of an unbounded retry loop.
Collaborates with: `ai_search.c`, `sgf_io.c`, `sgf_move_props.c`, `sgf_tree.c`, `create_puzzles_progress.c`, and the
boop/checkers puzzle policy modules.

## Checkers Puzzle Generator (`src/games/checkers/checkers_create_puzzles.c`)
Module: `checkers_create_puzzles_main()`.
Role: profile-specific puzzle policy for `build/tools/checkers_create_puzzles`. It requires `--ruleset <short-name>`
so generation, checking, deduplication, and logging target one explicit variant at a time. It also accepts
`--depth N`, `--synthetic-candidates`, `--save-games`, `--check-existing`, and `--dry-run`. Generated puzzles are saved
under `puzzles/checkers/<ruleset-short-name>/puzzle-####.sgf` with root setup (`AE/AB/AW/ABK/AWK/PL`), explicit
`RU[<ruleset-short-name>]`, and a tactical continuation line.
Validation detects serious mistakes with configurable best-move-depth analysis, requires the attacker to have at
least four legal moves and a best response at least 50 points above the runner-up, then validates the solution line in
one pass. The continuation re-analyzes every ply at the configured best-move depth, requires the attacker to keep a
single good move, allows the defender to use any best reply, and stops once static material is better than at the
puzzle start. Candidate solutions reject a one-move line, a three-move line of move, move, jump, insufficient partial
comebacks from heavily losing positions, and unstable lines where the immediate next best reply is a recapture.
Before generating anything, the generator loads existing `puzzle-*.sgf` files from the selected variant directory and
deduplicates by solution move sequence, so equivalent puzzles are skipped instead of being saved twice.
While the shared runner replays a generated self-play game or loaded SGF file, the checkers policy analyzes each
pre-mistake position at the configured best-move depth and reuses one shared TT allocation across the whole run. When
`--synthetic-candidates` is enabled, it also
tries any synthetic mistake move that already trails the best move by at least 100 points, so puzzle generation is not
limited to the exact self-play move that happened in the game.
The generator always prints self-play start, self-play completion, loaded existing solution keys, each move considered
as a candidate, indented `->` rejection or keep reasons, and a final aggregated rejection report so puzzle filtering
can be followed from the terminal. In check-existing mode, it also reports how many puzzle files were checked and how
many would be or were removed.
By default it saves only `puzzles/checkers/<ruleset-short-name>/puzzle-####.sgf`;
`puzzles/checkers/<ruleset-short-name>/game-####.sgf` companions are written only when `--save-games` is enabled.
Count-mode generation stops after the shared source-game attempt limit and exits with an explicit partial-generation
error if the filters reject every candidate before the requested puzzle count is reached.
Collaborates with: `create_puzzles_runner.c`, `ai_alpha_beta.c`, `rulesets.c`, `sgf_tree.c`, `sgf_move_props.c`,
`sgf_io.c`, and `puzzle_generation.c`.

## Puzzle Generator Progress (`src/create_puzzles_progress.c`, `src/create_puzzles_progress.h`)
Module: shared terminal progress formatting for puzzle generator tools.
Role: centralize the user-visible progress messages for self-play start, self-play completion, per-move candidate
analysis, and generic progress lines. Checkers and boop both use this helper so count-mode generation has the same
observable phases even though their candidate validation logic is profile-specific.
Collaborates with: `src/create_puzzles_runner.c`, `src/games/checkers/checkers_create_puzzles.c`, and
`src/games/boop/boop_create_puzzles.c`.

## Puzzle generation helpers (`src/games/checkers/puzzle_generation.c`,
`src/games/checkers/puzzle_generation.h`)
Module: puzzle-selection and output-index helpers.
Role: expose pure functions for mistake delta checks, "enough choice" and "single correct move" tests from scored move
lists, where "single correct move" means the best score is ahead of the second-best score by a configurable margin,
plus an attacker/defender move-clarity helper, a collector for all scored moves that qualify as mistakes under a given
threshold, and next puzzle file index discovery from existing `puzzle-####.sgf` files, plus pure predicates for
rejecting boring solution-line shapes, insufficient comeback swings from badly losing positions, and immediate
recaptures after the solution. Indexed output paths are built through GLib filename helpers instead of manual
separator concatenation.
Collaborates with: `checkers_create_puzzles.c` and `tests/test_puzzle_generation.c`.

## File dialog history helpers (`src/file_dialog_history.c`, `src/file_dialog_history.h`)
Module: SGF file dialog folder persistence helpers.
Role: create `GSettings` with the app schema, read the remembered SGF folder as a `GFile`, and store the parent folder
of a chosen SGF file so future dialogs can reopen there. The helper uses the active profile's schema ID, then falls
back to the in-tree `data/schemas` directory for local builds/tests.
Collaborates with: `sgf_file_actions.c` and `tests/test_file_dialog_history.c`.

## App data path helpers (`src/app_paths.c`, `src/app_paths.h`)
Module: application data directory lookup helpers.
Role: resolve installed or local read-only data subdirectories such as `puzzles` by checking an explicit environment
override first, then `g_get_user_data_dir()`, then `g_get_system_data_dirs()`, then the local checkout fallback.
It also creates writable user-state subdirectories for features such as puzzle progress and SGF autosaves, with
environment overrides available for tests and manual runs.
Collaborates with: `window.c` for packaging-safe puzzle discovery, `sgf_autosave.c` for SGF autosave storage, and
`tests/test_app_paths.c`.

## Homeworlds engine (`src/games/homeworlds/homeworlds_types.h`, `src/games/homeworlds/homeworlds_game.c`,
`src/games/homeworlds/homeworlds_game.h`)
Module: slot-based Homeworlds rules engine.
Role: represent Homeworlds positions as fixed-size slots: a 36-slot bank, 16 system slots (systems `0` and `1` are
the players' homeworlds), two star slots per system, and fourteen ship slots per side per system. Inline helpers in
`homeworlds_types.h` own pyramid encoding/decoding and low-level slot semantics so the representation can change in
one place later if needed. Each star system caches per-color pyramid totals; gameplay mutators update those totals
incrementally, while SGF snapshot loading and raw test fixtures rebuild them after direct slot construction.
`HomeworldsMove` does not store physical slot indexes or an acting side; it stores the same symbolic system and ship
references used by notation (`H1`, `G3'`, `g2`, etc.), and the rules engine resolves those references against the
current position at apply time. Failed system-reference resolution returns
`HOMEWORLDS_INVALID_INDEX` through the output index, and failed ship-reference resolution clears its resolved indexes
and pyramid. Build steps are canonicalized further: they store only the source system plus the build color in
`target_color`, because the exact source ship size does not change the move.
Rules covered: setup, build, trade, attack, move, discover, sacrifice, catastrophe resolution, empty-system
cleanup, end-of-turn homeworld loss detection for either side, static evaluation, terminal scoring, hashing, compact
move formatting/parsing, structural move equality, ordered unique all-move generation, and whole-position SGF snapshots
in `homeworlds_sgf_position.c`. Catastrophes return remaining ships only when the affected star system has no stars
left after removing the overpopulated color, so binary-star systems survive losing one star. Move notation uses pyramid
letters and sizes directly, such as `Y2B1g3`, `H1g+`, `G3y2>G2 G3y!`, and `pass`; multi-step moves are formatted with
spaces between complete steps and no internal slash separator, while the parser also accepts older saved `H1 g+` and
slash-separated step notation. The public parser only writes the output move after the whole notation succeeds. Static
evaluation counts ship material, homeworld health, and build access: an empty homeworld is scored like a simple
three-pip setup system with one buildable color, a single-star homeworld is penalized, the largest own ship at each
player's homeworld is repeated, and each buildable ship color is rewarded once. Applying a move is transactional: setup
and turn moves are resolved against a
working copy and only replace the original position after the full move succeeds, and malformed turn moves with
overlong step counts are rejected before step storage is read. Sacrifice-granted actions reuse the normal action
application code but bypass local color-access checks because the sacrificed ship supplies the action color. Public
single-step application is also transactional, so failed staged steps do not leak partial bank or system changes into
the builder. Bank lookup helpers return `0` as their output pyramid when no matching bank ship is available, and
empty-system lookup returns `HOMEWORLDS_INVALID_INDEX` when no empty system slot is available.
`homeworlds_position_text.c` formats a non-GTK ASCII board snapshot, with each system shown as player 2 ships,
stars, and player 1 ships in the same top-to-bottom reachability order as the board.
Collaborates with: `homeworlds_move_builder.c`, `homeworlds_backend.c`, `homeworlds_sgf_position.c`,
`homeworlds_view.c`, `homeworlds_position_text.c`, `tests/test_homeworlds_game.c`, and
`tests/test_homeworlds_backend.c`.

## Homeworlds move builder (`src/games/homeworlds/homeworlds_move_builder.c`,
`src/games/homeworlds/homeworlds_move_builder.h`)
Module: staged Homeworlds move construction.
Role: expose incremental legal choices without enumerating the full legal move space. The builder owns a working copy
of the position plus the partial move under construction, and advances through setup-star selection, setup-ship
selection, source-ship selection, action choice, and target-specific substages for trade, attack, and move/discover.
Sacrifices are modeled as a prefix step that fixes the remaining action color and count, after which the builder loops
back through source-ship selection for each granted action. Choosing a ship for a sacrificed action immediately starts
that forced action instead of showing the normal action picker again. Passing while those actions remain appends passes
for every remaining sacrificed action and completes the move; only a pass with no pending sacrifice is a top-level pass.
If a staged action or catastrophe destroys a homeworld that still had ships at the start of the move, the builder
completes the move immediately instead of asking for sacrifice-fill or catastrophe-pass steps.
Candidate data can still use transient slot indexes for UI selection, but committed move steps are converted to
symbolic references before they are applied or saved to SGF. Committed build steps are converted to system-plus-color
form so two same-color source ships produce the same internal move and notation. The interactive action list still
offers Build from every selected same-color ship; duplicate symbolic build moves are handled after complete moves are
formed instead of hiding a legal action from the clicked ship.
During a multi-action blue sacrifice, ships created by earlier trade steps in that system are not offered as later
trade actors; changing a ship through multiple colors is canonicalized as one direct trade followed by passes.
Physically interchangeable choices, such as identical bank stars for discovery or identical enemy ships for capture,
are deduplicated before they become user-visible choices. Action candidates are appended through one helper so normal
and sacrifice-forced action lists use the same candidate shape, and all candidate-list builders abort cleanly on append
failure instead of returning truncated choices.
Collaborates with: `homeworlds_game.c`, `homeworlds_backend.c`, `homeworlds_view.c`, and
`tests/test_homeworlds_backend.c`.

## Homeworlds UI, reports, and AI candidates (`src/games/homeworlds/homeworlds_view.c`,
`src/games/homeworlds/homeworlds_move_report.c`, `src/games/homeworlds/homeworlds_backend.c`)
Module: Homeworlds board host, staged GTK view/controller, shared move-report generator, and backend-owned AI
candidate policy.
Role: let `ghomeworlds` use the shared `GGameWindow` while replacing only the square board presentation.
`homeworlds_view.c` renders a starfield board with dynamic system boxes sized around their contents,
connectivity-aware reachability-row placement between the two homeworlds that only reserves empty vertical slots between
disconnected row groups, measured-width row packing that reserves the bank footprint and expands inside a scroller only
when needed, viewport-tracking drawing-area content size that expands width for crowded rows and height for vertically
crowded row boxes, mapped-frame ticks that recheck startup allocation without replacing stable click targets, a fixed
minimum board viewport, homeworld labels, pipped square stars, tall pipped ship pyramids, overlaid compact bank piles
with a shared base button style, an in-panel title, staged legal-choice buttons, and catastrophe buttons that stage
normal symbolic steps into the
current move so SGF, model state, and the last-move label stay synchronized. Homeworld rendering keeps player 1 at the
bottom, player 2 at
the top, and lays out every system in one horizontal row with player 2 ships left of the stars and player 1 ships right
of the stars. During setup and target
selection, the bank piles on the board are real `GtkButton`s that feed the staged builder directly, so start stars,
start ships, trade colors, and discovery stars do not require a long side-panel button list. During normal turns,
selectable ships, capture targets, and existing-system move targets are also overlaid as board buttons; the side panel
has a 350 px fixed-width text area with horizontal scrolling externalized, overlay vertical scrolling enabled,
word-wrapped labels, width-constrained buttons, a read-only wrapped `GtkTextView` for the move report, and only keeps
non-board choices such as pass or follow-up actions, with a `Cancel` button whenever those choices belong to an
in-progress move.
Bank pile matching is split into setup, trade-color, and discovery predicates so the board button layer does not
duplicate raw candidate-field tests. During play, the side panel also reports the backend `good_moves()` list followed
by the remaining legal moves from the core
all-move generator, after a header that counts both groups. The report subtracts the good moves with a structural hash
set, deduplicates canonical moves, but does not cap the backend-good or diagnostic collectors. The `View` -> `Move
report` action disables this report before either collector runs. The separate `build/tools/homeworlds_profile_moves`
CLI applies `--moves` random good moves from a `--seed` or replays the first moves of a Homeworlds SGF main line with
`--file`, prints an ASCII board snapshot, and then runs the AI at `--depth` and prints the scored moves plus search
stats. It frees any generated candidate list before leaving an error path. The Homeworlds board host also syncs its
last-move label from SGF current-node changes so timeline
navigation and direct play report the same move. Human interaction
advances
`homeworlds_move_builder` one visible choice at a time and sends each completed `HomeworldsMove` to the generic window
move handler. Manual catastrophes update the builder's working position and are emitted as part of the same multi-step
move once the turn completes. When a primary action leaves a catastrophe available, the staged move remains open so
the user can trigger one or pass before a SGF node is emitted. In the app, completed moves append SGF nodes through
`GGameSgfController`; standalone view tests can still install no handler and apply directly to `GGameModel`.
Homeworlds intentionally does not expose a
full legal move list, so the
shared SGF controller validates completed moves by applying them to a copied position before appending the node.
Homeworlds SGF position snapshots reject duplicate per-system entries instead of letting a later `GHS` property
silently replace an earlier snapshot for the same system.
`homeworlds_backend.c` walks the same staged builder to feed the shared alpha-beta search with backend-good moves. That
AI path deduplicates completed symbolic moves, rejects pass moves while non-pass good moves remain, keeps pass as a
top-level fallback when every non-pass branch is filtered away before a primary action is staged, applies
Homeworlds-specific opening and safety heuristics, and lets dead-end choices such as attacks with no target naturally
produce no completed move. When a builder choice
appends a turn step, the AI applies the same safety checks to ordinary actions and sacrifice-granted actions: it keeps
the last own homeworld ship in place, rejects moves into unfavorable catastrophes, rejects builds that create
unfavorable catastrophes, and skips one-action sacrifices when that color action is already available at the selected
system. It also prunes yellow-sacrifice move chains when a ship returns to its original source or reaches a system the
original source could already reach directly without crossing a catastrophe boundary. During blue and green sacrifices,
independent adjacent trades and builds are kept in canonical order only when a swapped order is legal,
position-equivalent, and catastrophe-free; blue trade commutation is proven from local ship, bank, and color counts,
while green build reversal tries every reversible built-ship candidate because bank supply can make the built size
ambiguous.
During play, `good_moves()` scores each candidate by the one-ply static or terminal value, orders it from the current
player's perspective, and keeps only the first 512 moves that are within 50 points of the best one.
Profitable catastrophes available at the start of a turn are required somewhere in the final move, while profitable
catastrophes created by an earlier step are forced immediately in the staged walk.
`doc/homeworlds-move-generation.md` describes how the legal builder, diagnostic move report, profiling CLI,
`good_moves()`, and generic alpha-beta search interact.
Collaborates with: `GGameAppProfile`, `GGameWindow`, `GGameModel`, `GGameSgfController`, `homeworlds_game.c`,
`homeworlds_move_builder.c`, `homeworlds_backend.c`, `tests/test_homeworlds_profile_moves.c`, and
`tests/test_homeworlds_window.c`.

## Boop engine (`src/games/boop/boop_types.h`, `src/games/boop/boop_game.c`,
`src/games/boop/boop_game.h`, `src/games/boop/boop_backend.c`, `src/games/boop/boop_backend.h`,
`src/games/boop/boop_sgf_position.c`, `src/games/boop/boop_sgf_position.h`)
Module: boop position, move, rules, builder, and backend adapter.
Role: implement the 6x6 boop rules from `src/games/boop/RULES.md`. Positions store padded bitboards for each side's
pieces, each side's cats, and occupied squares, plus side to move, per-side kitten/cat supplies, promoted-kitten counts,
and terminal outcome. Each mask row uses the low six bits of an eight-bit lane, so bit shifts can test horizontal,
vertical, and diagonal threes without wraparound. Moves store placement square, placed rank, and an optional
promotion/graduation mask in the same padded format so fully resolved turns can be serialized and replayed. `BoopPiece`
is a single-byte encoded value with side-0 kitten/cat and side-1 kitten/cat packed before the empty value; helpers for
construction, validation, and rank extraction live with it in `boop_types.h`. The unchecked `boop_piece_side()` macro
maps empty to extracted side `2`, so side comparisons can ignore emptiness.
The engine applies simultaneous one-square boops from the newly placed piece, returns booped-off pieces to the owner
supply, resolves mandatory line promotions, supports optional one-kitten graduation when all eight pieces are on board,
offers both line-promotion and single-kitten graduation choices when both rules apply, and awards the active player an
end-of-turn win for three cats in a row or all eight kittens promoted. Cat-line wins are checked before line-promotion
collection, so promotion collection can treat every same-side three-in-a-row as promotable.
The hot rules paths are bitboard-based. Cat-line win detection tests the active cat mask with four shift-and-intersect
expressions, and line-promotion collection uses the same start masks to emit exact three-square promotion masks. Boop
effects read ownership and rank directly from masks and move ownership bits directly. Boop rays are precomputed as
adjacent and destination masks for each square.
Row/column conversion remains at UI, notation, and SGF boundaries, and `BoopMove.square`/`path[]` still use stable
0..35 square indices for those APIs.
The backend exposes full move lists for validation/search, a staged square-grid builder for interactive placement plus
promotion selection, deterministic notation such as `K@a1+a1,b1,c1`, symbol-only board pieces, static evaluation,
terminal scores, and position hashing. Boop static evaluation scores promoted kittens plus on-board material derived
from remaining kitten/cat supplies, then applies a small malus to the side that just moved to smooth turn-to-turn
material spikes. The position tracks total plies played so Boop terminal scoring is a stable property of the position:
wins score 10000 minus that total ply count. When both kittens and cats are available, the placement builder exposes
both ranks for each empty square and leaves the active rank choice to the UI candidate-preference hook. The
all-pieces-on-board graduation rule uses empty kitten and cat supplies instead of rescanning board occupancy.
Promotion-stage selection paths contain only the promotion squares, so the just-placed piece is highlighted only when
it is actually one of the candidate promotion squares. The boop engine also exposes last-move overlay metadata so the
GTK board can circle the placed piece and draw arrows for every booped piece, including off-board boops that return to
supply.
`boop_position_normalize()` is the shared validator for arbitrary boop snapshots: it derives promoted-cat counts from
mask/supply state, restores the cached occupied mask, recomputes terminal outcome, and rejects impossible totals before
position-only SGF replay publishes a snapshot into the model. `boop_sgf_position.c` uses that helper to encode and
decode root snapshot properties (`GBK`, `GBC`, `GWK`, `GWC`, plus per-side supply counts, `GPLY`, and `PL`) for boop
`Save position...`.
Collaborates with: `src/games/boop/boop_controls.c`, `GGameModel`, `BoardView`, `GGameWindow`, `tests/test_boop_game.c`,
`tests/test_boop_backend.c`, and the generic backend/model/SGF tests.

## Boop controls (`src/games/boop/boop_controls.c`, `src/games/boop/boop_controls.h`,
`src/games/boop/boop_controls_stub.c`)
Module: boop-specific shared-shell board host.
Role: build the boop supply/promotion side panels around the shared `BoardView`, install boop CSS, track the active
side's selected kitten/cat rank, show the `Confirm promotions` button only when the move builder requires it, and
bridge boop's move-candidate preference / selection-changed / completion-confirmation hooks into the shared board
input flow. The host also hides the shared per-square numbering and adds boop edge coordinates directly on the board's
existing border (letters along the bottom, numbers on the left) that stay synchronized with `BoardView` orientation
changes. `boop_controls_stub.c` provides a weak non-GTK fallback so profile/model/backend tests can link without
pulling GTK UI code into headless targets.
Collaborates with: `GGameAppProfile`, `GGameWindow`, `BoardView`, `GGameModel`, and `tests/test_window_boop.c`.

## Game backend interface (`src/game_backend.h`, `src/active_game_backend.h`, `src/game_app_profile.h`,
`src/games/checkers/checkers_backend.c`, `src/games/homeworlds/homeworlds_backend.c`,
`src/games/boop/boop_backend.c`)
Module: generic game-selection boundary plus the compiled game adapters.
Role: `game_backend.h` defines the generic callback table used to describe one compiled game backend.
`active_game_backend.h` exposes the backend owned by the active `GGameAppProfile`, and
`src/games/checkers/checkers_backend.c` adapts the moved checkers engine, ruleset catalog, move list, AI, and move
formatting APIs into that generic table. `src/games/homeworlds/homeworlds_backend.c` now adapts the slot-based
Homeworlds engine and staged move builder, advertises `supports_move_builder = TRUE`, `supports_move_list = FALSE`,
`supports_ai_search = TRUE`, and implements `list_good_moves` by exploring the builder in heuristic order while
filtering nonsensical setup, pass, unsafe homeworld, and unsafe catastrophe-triggering build choices. Its good-move
buffer tracks structural move hashes only to warn with the root position and duplicated move if generation ever
produces a duplicate; it does not rely on the guard to suppress duplicates. The playable Homeworlds UI uses the same
builder directly rather than asking this backend for full move enumeration.
`src/games/boop/boop_backend.c` adapts the boop engine, advertises move lists, staged
move-building, square-grid rendering, AI search, notation formatting/parsing, hashing, and backend-owned SGF position
snapshot hooks.
Scope: shared application code still has some checkers-native compatibility layers, but the physical checkers source
ownership boundary is now explicit under `src/games/checkers/`.
Backends now advertise whether they support full move-list enumeration, incremental move-building, AI search, forced
move extension, and backend-owned move parsing/formatting for SGF. Each SGF-capable backend also maps its own side
numbers to SGF `B`/`W` colors, which keeps shared controller code from assuming that side 0 means the same color in
every game. Move-builder backends can also expose preview positions, builder-owned selection paths, and selection reset
behavior for multi-stage interactions such as boop promotion choices. Backend outcome banner text is reserved for
terminal outcomes; ongoing positions should return no banner text. They can also optionally expose SGF setup-node and
root-position snapshot hooks so the shared controller can replay setup-root SGFs and save position-only SGFs without
game-specific branches.
Collaborates with: `Makefile` backend selection, `tests/test_game_backend.c`, and future generic model/search work.

## Generic game model (`src/game_model.c`, `src/game_model.h`)
Class: `GGameModel` (`GObject`).
Role: wrap one active `GameBackend` plus one opaque current position behind a GTK-friendly state container with a
`state-changed` signal. The model owns backend-sized position storage, initializes it from the backend's first
variant when one exists, exposes generic move listing, application, whole-position replacement, and whole-position
replacement plus variant changes, and now backs the shared square-grid UI directly. `GCheckersModel` still mirrors it
for checkers-only compatibility paths such as puzzles, analysis, and setup-root SGF helpers. Construction accepts
either a full move-list backend or a move-builder backend; boop and checkers support both, while builder-only games
can still apply moves through direct backend validation. Whole-position replacement validates the model receiver before
reading the current variant used by the replacement helper.
Collaborates with: `src/game_backend.h`, `src/games/checkers/checkers_backend.c`,
`src/games/checkers/checkers_model.c`, `src/window.c`, and `tests/test_game_model.c`.

## GTK application entry (`src/gcheckers.c`, `src/application.c`, `src/application.h`, `src/ghomeworlds.c`)
Class: `GGameApplication` (`GtkApplication`) for the shared-shell checkers and boop builds; plain `GtkApplication`
for the separate Homeworlds prototype.
Role: define the GTK application type and activation flow that creates the main window and model, installs app actions
(`app.new-game`, `app.import`, `app.quit`, `app.settings`), installs window game/SGF/navigation/analysis/puzzle/view
actions, and publishes one shared menubar model (`File` -> `New game...`, `Import...`, `Load...`, `Save as...`,
`Save position...`, `Settings`, `Quit`; `Move` -> `Force move` + SGF navigation/delete-node section; `Analysis` ->
current-position and whole-game analysis; `Puzzle` -> `Play puzzles`; `View` -> drawer toggles plus profile-specific
items such as the Homeworlds move report) with keyboard accelerators, including Delete for SGF node deletion. Both
`gcheckers` and `gboop` are built from this same shell and diverge through `GGameAppProfile` feature flags and boop's
optional board-host hook. Unsupported actions stay in the same shared shell but are disabled for profiles that do not
support them. `src/ghomeworlds.c` selects the Homeworlds profile and opens the same shared shell with the Homeworlds
board-host hook.
The active application ID, display strings, settings schema ID, and backend all come from `GGameAppProfile`, with the
current profiles selecting `io.github.jeromea.gcheckers`, `io.github.jeromea.gboop`, or
`io.github.jeromea.ghomeworlds`.
Collaborates with: `GGameWindow`, `GGameAppProfile`, and new-game/settings dialog presentation.

## Board view subsystem

### `BoardView` (`src/board_view.c`, `src/board_view.h`)
Class: `BoardView` (`GtkWidget`).
Role: coordinate rendering updates, input handling, and active-turn move highlighting for the shared square-grid board
path. It now consumes backend-provided square-grid callbacks (rows/cols, playable squares, dense square indexes, piece
views, and staged move-candidate paths) through `GGameModel`, while still accepting the legacy `GCheckersModel` in
checkers builds via an internal compatibility bridge.
When a backend exposes a move-builder preview position, the board renders that provisional position instead of the
committed model position; boop uses this to show the post-boop board state during promotion selection.
Primary-click input is routed through each square button's `clicked` signal, and right-click input uses a dedicated
secondary-button `GtkGestureClick`. A button-aware square callback allows window-level edit-mode logic to intercept
square actions (left/right) before play-mode move-selection handling.
Board orientation is driven by a generic two-side bottom-index property. Square-grid backend coordinates are defined
from side 0's perspective with row 0 at the bottom; the grid and last-move overlay flip rows for side 0 at the bottom
and flip columns for side 1 at the bottom so rotated boards keep pieces and arrows aligned. Hosts can also register a
small bottom-side-changed callback when they need orientation-aware chrome such as boop's border-mounted coordinates.
Collaborates with: selection, overlays, and square/grid helpers.

### `BoardGrid` (`src/board_grid.c`, `src/board_grid.h`)
Module: board grid helpers.
Role: construct the optional shared square-grid layout from backend callbacks instead of hard-coded checkers parity and
index math.
Dark squares wire primary `clicked` and optional secondary `pressed` callbacks separately to avoid gesture arbitration
conflicts with `GtkButton` activation.
Collaborates with: `BoardView` and `BoardSquare`.

### `BoardSquare` (`src/board_square.c`, `src/board_square.h`)
Class: `BoardSquare` (`GtkWidget`).
Role: represent individual dense playable squares and update piece/index rendering state from a generic
`GameBackendSquarePieceView`. Piece artwork is drawn directly with a `GtkDrawingArea` at the square's allocated size so
checker men avoid `GtkPicture` downscaling artifacts. The square also owns the small dense-index overlay used by the
shared square-grid board.
Collaborates with: `BoardGrid` and `PiecePalette`.

### Last move overlay (`src/board_move_overlay.c`, `src/board_move_overlay.h`)
Module: move overlay renderer.
Role: draw the selected SGF node's last-move overlay via cairo on top of the shared square-grid board and, when the
game is over, a centered backend-provided winner banner across the board. Checkers draws its move path as translucent
green arrows. Boop circles the placed kitten/cat in the same translucent green, draws arrows for every piece that was
booped, and marks each removed kitten/cat with a red cross, reconstructing the pre-move position from the SGF parent
node so the overlay matches the actual boop resolution. Removed-piece crosses are painted above boop arrows so
off-board returns to supply still leave a visible removal mark. Ongoing positions never draw a banner, even if a
backend accidentally returns non-NULL text for `GAME_BACKEND_OUTCOME_ONGOING`.
Collaborates with: `BoardView`, `GGameModel` for backend-driven board state, and `GGameSgfController` for the
selected-node move.

### Selection controller (`src/board_selection_controller.c`, `src/board_selection_controller.h`)
Module: selection path logic.
Role: manage click-path selection and move application orchestration using backend `GameBackendMoveBuilder`
candidates. The controller initializes a backend builder from the current position, highlights the next square choices
from candidate paths, steps the builder on each click, and applies the completed backend move. Checkers now exposes
its ordinary move paths through this staged builder, while boop uses the same flow for placement and promotion-square
selection. Cached builder state is invalidated on `GGameModel::state-changed` so computer replies, SGF navigation,
and other external position changes cannot leave stale candidate highlights on the board. During a partial selection,
only continuations are highlighted; clicking a non-continuation clears the current builder and reprocesses the same
click as a fresh first step. A candidate-preference callback can break ties when several backend candidates share the
same visible square path, which the boop supply UI uses to choose kitten or cat placement on a selected square. A
completion-confirmation callback can defer applying a completed builder move until the UI explicitly confirms it, used
by boop promotion selections.
Backends with multi-stage selection can override the visible selected path and reset only the current selection stage;
boop uses that to keep promotion selection independent from the placement square and to let ordinary board clicks
change or clear an unconfirmed promotion choice without applying it. Backend-reported selection paths are bounded by
the controller's fixed path capacity before being copied or indexed.
Collaborates with: `BoardView` and `GGameModel` for applying moves.

### Piece palette (`src/piece_palette.c`, `src/piece_palette.h`)
Module: piece palette.
Role: provide direct cairo rendering data plus fallback symbols for backend-provided square-grid piece views. The
current shared palette still draws the checkers side-0/side-1 man/king style.
Collaborates with: `BoardSquare` and man paintable helpers.

### Man paintable (`src/man_paintable.c`, `src/man_paintable.h`)
Module: checker man renderer.
Role: render checker men and kings either via `GdkPaintable` snapshots or direct cairo drawing at final widget size,
using taller ellipse radii for the rounded caps and inner ring plus layer-count-aware vertical centering so both men
and kings sit evenly inside their squares.
Collaborates with: `PiecePalette` and board rendering.

## SGF subsystem

### SGF tree (`src/sgf_tree.c`, `src/sgf_tree.h`)
Module: SGF tree storage.
Role: manage move nodes, parent/child links, SGF property access, traversal helpers, and the SGF current-node timeline
used as the source of truth for move chronology/navigation. Nodes also carry optional structured analysis
(`SgfNodeAnalysis`) containing depth, search stats, and best-to-worst scored legal moves.
Traversal helpers include root-to-node path construction, main-line collection from arbitrary nodes, current-branch
construction for graphing, and deterministic preorder collection for full-tree analysis jobs.
The tree also supports deleting a non-root node and its whole descendant subtree while keeping current selection on
the deleted node's parent when needed.
Collaborates with: SGF view and controller modules.

### SGF move properties (`src/sgf_move_props.c`, `src/sgf_move_props.h`)
Module: SGF move property helpers.
Role: convert between SGF move properties (`B[...]`/`W[...]`) and typed move storage supplied by the active backend.
Parsing and formatting are delegated to the active backend, so checkers notation and boop notation share the same SGF
property helpers while the public API only accepts opaque move storage pointers. Failed notation parsing leaves caller
move storage untouched.
Collaborates with: `sgf_io` and `GGameSgfController`.

### SGF IO (`src/sgf_io.c`, `src/sgf_io.h`)
Module: SGF load/save core.
Role: serialize and deserialize SGF trees using SGF syntax (`(`, `)`, `;`, `PROP[...]`) with move properties
`B[...]`/`W[...]` and standard SGF variation nesting for branches. gcheckers writes SGF metadata (`FF`, `CA`, `AP`,
`GM`, `RU`) and does not persist current UI selection. `RU` stores the active backend variant short name when the
backend has variants, and is omitted/accepted as missing for zero-variant games such as boop. Node
analysis persists through custom properties:
`GCAD[depth]`, `GCAS[nodes=...;tt_probes=...;tt_hits=...;tt_cutoffs=...]`, and repeated
`GCAN[move:score:nodes]` for scored moves, while still accepting older `GCAN[move:score]` data when loading. `GCAN`
values are formatted dynamically so long analysis move labels are not truncated, and empty analysis move labels are
rejected on load. Analysis stat lists reject empty fields, and unsigned SGF numeric fields reject signed text and range
errors. Empty SGF trees and empty nested variations are rejected instead of being discarded. This layer is GTK-free so
it can be reused by both GUI actions and future CLI commands.
Collaborates with: `GGameSgfController` load/save entry points and `tests/test_sgf_io.c`.

### SGF autosave (`src/sgf_autosave.c`, `src/sgf_autosave.h`)
Module: SGF autosave path and write helper.
Role: save SGF trees into a writable per-game autosave repository under
`~/.local/share/gcheckers/autosave/<game-id>/` by default, or under `GCHECKERS_AUTOSAVE_DIR/<game-id>/` when that
override is set. Filenames use `YYYYMMDDHHMMSS-YYYYMMDDHHMMSS-XX`: game-session start time, move time, then the first
free two-digit suffix for that timestamp pair.
Collaborates with: `GGameSgfController`, `app_paths.c`, `sgf_io.c`, and `tests/test_sgf_autosave.c`.

## Puzzle Catalog (`src/puzzle_catalog.c`, `src/puzzle_catalog.h`)
Module: shared puzzle catalog loader.
Role: scan `puzzles/<game-id>/<variant>/` for `puzzle-####.sgf` files, sort them by puzzle number, and expose stable
`<game-id>/<variant>/puzzle-####.sgf` IDs to shared settings, puzzle-picker, and window code. This keeps the path and
ID layout generic while still letting checkers-specific generation tools emit the existing file names.
Collaborates with: `window.c`, `puzzle_dialog.c`, `settings_dialog.c`, and `tests/test_puzzle_catalog.c`.

### SGF view (`src/sgf_view.c`, `src/sgf_view.h`)
Class: `SgfView` (`GtkWidget`).
Role: game-agnostic move tree UI that wires together layout, rendering, selection helpers, and selection resync calls.
The SGF disc grid (`tree_box`) is measured directly by the overlay (via `gtk_overlay_set_measure_overlay`) so no manual
size requests are applied to the overlay stack. It syncs selection after layout updates with debug logging when widgets
are not ready, and annotates notify-driven resync attempts with the emitting object/property pair. Full node-widget
mapping dumps are opt-in via `GCHECKERS_DEBUG_SGF_VIEW`.
Collaborates with: SGF layout (layout-updated signal), selection, scroller, and disc factory helpers.

### SGF disc factory (`src/sgf_view_disc_factory.c`, `src/sgf_view_disc_factory.h`)
Module: disc widget creation.
Role: build SGF move buttons (including the virtual move zero dot) and wire the `node-clicked` signal. Each button
keeps the factory alive through its click-signal closure so deferred GTK widget cleanup cannot leave dangling factory
user data behind.
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
widget bounds (`[bounds.origin.x, bounds.origin.x + bounds.size.width]`), then verifies that the selected range is
visible after clamping. It internally schedules bounded idle retries for transient geometry or scroll-adjustment
readiness paths, while missing selected-node widget mappings are not retried to avoid perpetual idle loops on stale
selection pointers. Callers use one API and do not handle retry paths. `SgfView` now refuses to start a scroll request
at all when the scrolled window is currently unmapped, which avoids queueing impossible retries while puzzle mode hides
the navigation panel.
Collaborates with: `SgfView`, SGF node widget mapping, and selection controller updates.

### SGF selection controller (`src/sgf_view_selection_controller.c`, `src/sgf_view_selection_controller.h`)
Module: SGF selection logic.
Role: track SGF selection, update CSS classes, and navigate siblings and parents.
Collaborates with: `SgfView`, the SGF tree, and the scroller.

### SGF file actions (`src/sgf_file_actions.c`, `src/sgf_file_actions.h`)
Module: GTK SGF file action integration.
Role: register `win.sgf-load` and `win.sgf-save-as` actions, present `GtkFileDialog` file pickers, reopen them in the
last remembered SGF folder, call SGF controller load/save APIs, and show errors as modal dialogs.
Collaborates with: `GGameWindow` action map, `GGameSgfController`, and `file_dialog_history.c`.
