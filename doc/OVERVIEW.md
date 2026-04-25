# Project overview

## `GGameWindow` (`src/window.c`)
Class: `GGameWindow` (`GtkApplicationWindow`).
Role: composition root that binds model state to UI updates, keeps board input available, and coordinates auto-play.
Owns: `GCheckersModel`, `BoardView`, `PlayerControlsPanel`, and `GGameSgfController`.
Collaborates with: `ggame_style_init()` for CSS, model signals for refresh, and SGF analysis signals to reset
player dropdowns. Computer turns are routed by control mode with alpha-beta depth configured from the shared
`Computer depth` slider (`0..16`). Uses a three-pane layout: board and player controls (left), SGF mode selector
and SGF view (middle), and analysis (right). Analysis is launched from shared window actions exposed in the
`Analysis` menubar submenu: current-position analysis iterates on the selected node, and full-game analysis always
processes nodes in reverse order so TT state is reused from later positions first.
The analysis pane owns its own `Analysis depth` slider; analysis no longer reuses the player `Computer depth`
setting. Current-position analysis iterates up to the selected depth, and full-game analysis uses the same selected
depth as a fixed search limit. Analysis menu entries are one-shot actions, so SGF navigation does not implicitly keep
restarting current-position analysis. Full-game analysis reconstructs each node position from SGF replay semantics
(setup properties plus moves from root to the exact node), so setup-root puzzle files and edited SGF setup nodes share
the same position source of truth as normal controller navigation.
Board orientation is runtime-only window state: live games choose `follow-player`, `follow-turn`, or `fixed`
orientation based on the new-game player modes, and SGF review/manual navigation switches back to `fixed` so analysis
navigation does not keep rotating the board.
Those shared board-orientation and board-input-enable decisions now come from the cached `GGameModel` through backend
`position_turn()` and `position_outcome()` callbacks rather than directly inspecting checkers state.
Puzzle mode starts with a modal chooser (`src/puzzle_dialog.c`) that lets the user pick one backend
variant and then click a numbered puzzle square from a ten-column grid. The dialog only reports
cancel-or-selected results back to `GGameWindow`; the window owns opening the selected puzzle and starting progress
tracking. The grid is built from the selected variant directory only and shows local status per puzzle: untried squares
are white, solved squares are green, and tried-without-success squares are red. Runtime loading resolves the puzzle root
(`GCHECKERS_PUZZLES_DIR` or the installed/local application data search path), appends the active game ID plus the
selected variant short name such as `checkers/american` or `checkers/russian`, and then loads the exact clicked
`puzzle-*.sgf`. While active it hides both drawers, disables SGF/review actions, shows puzzle-only `Next puzzle` and
`Analyze` buttons, and validates the player's moves against the SGF main-line solution while auto-playing defender
replies. `Next puzzle` now advances through the sorted catalog for the active variant, wrapping to the first puzzle
after the last one, and no longer selects a random puzzle. Puzzle `Analyze` exits puzzle mode, rewinds fully to move 0,
and then starts the same full-game analysis path used by the Analysis menu. The current node report still follows
normal SGF selection instead of being replaced by a generic completion message. Picker squares keep custom status colors
and now define an explicit darker `:active` state so mouse presses remain visible even with the custom button styling.
Puzzle mode now also records local progress for each opened puzzle entry. Opening the puzzle creates an append-only
attempt record with `started_unix_ms`, terminal outcomes are `success`, `failure`, or `analyze`, and the first wrong
move is stored only when the failure happened on the very first attempted move. A started-but-unfinished puzzle entry
is resolved as `failure` when the user starts a different puzzle, starts a new game, imports another game, or closes
the window.
Puzzle entry forces a fixed attacker-at-bottom orientation, while puzzle exit restores only layout/drawer state and
leaves the current board orientation unchanged.
Adds an `Analysis` menubar submenu for current-position and whole-game analysis, plus a `View` submenu with
independent toggles for the navigation drawer and analysis drawer; hiding both removes the entire right-side drawer
split while preserving the board pane. The analysis drawer keeps the text report bound to the currently selected SGF
node, and shows transient analysis progress in a separate status label under the graph instead of overwriting the
report text. Full-game analysis status uses one shared formatter from the initial `0/n` state through later progress
updates instead of switching from a separate startup string, includes the cumulative explored-node count in the status
label, and refreshes on the same 100 ms throttle used by current-position analysis while a node is being searched.
Panel width state is retained for the board, navigation drawer, and analysis drawer, and drawer show/hide transitions
recompute window width plus paned positions so visible panels keep their prior widths instead of stretching.
Puzzle mode also owns the toplevel default width while active, so foreign GTK/default-size changes are reasserted back
to the puzzle layout instead of expanding the window to the normal three-pane width.
Mode dropdown supports `Play` and `Edit`. In `Edit`, board clicks mutate SGF setup properties on the current node:
left click cycles empty->white man->white king->empty (with black pieces and any king clearing to empty), right click
mirrors this for black. SGF navigation and `Force move` actions are disabled while `Edit` is active.
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
(`New game...`, `Force move`, `Save position...`, SGF timeline rewind/step/skip actions) via GTK actions.
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
Default panel widths target about `500/300/300` pixels at the default window width (`1100x700`).
Lifecycle: sinks and retains an owned `PlayerControlsPanel` reference, removes it from its current `GtkBox` parent
during dispose via `ggame_widget_remove_from_parent()`, and then clears its references.
during dispose, cancels any pending auto-move idle source, and then clears its references.
At construction time it also pushes the active backend's side labels into `PlayerControlsPanel`, so the shared board
UI keeps generic two-side semantics while the compiled backend decides how those sides are named.

## `GGameSgfController` (`src/sgf_controller.c`)
Class: `GGameSgfController` (`GObject`).
Role: SGF timeline authority and synchronization point between SGF current-node transitions and game state updates.
Move application is SGF-first: validate model move, append under SGF current, set SGF current, then project that
transition to the model (`single move` if parent->child, otherwise reset+replay from root).
Replay applies SGF setup properties (`AB`, `AE`, `AW`) and side-to-play (`PL`) on every visited node before move
replay, so loading/setup-only SGF nodes can drive board state and turn correctly. Custom king markers (`ABK`, `AWK`)
are validated as subsets of `AB`/`AW` and then applied as kings. Root `RU[<ruleset-short-name>]` is now read on load
to switch the model to the matching ruleset before replay, and SGF loads now fail if `RU` is missing or unknown
instead of falling back to the current model rules. `RU` is stamped back onto fresh trees and saved SGFs so
variant-specific files round-trip explicitly.
`ggame_sgf_controller_set_model()` only binds/disconnects model references; timeline clearing is explicit via
`ggame_sgf_controller_new_game()`. Exposes SGF navigation helpers used by window actions: rewind to root, step
backward, step forward on main line, step to next branch point, and step to main-line end.
Selection-only navigation updates SGF view selection in place (`sgf_view_set_selected`) instead of rebuilding the
entire SGF layout.
Exposes a current-node refresh helper that replays SGF state into the model after setup-property edits on the current
node.
Owns: `SgfTree` and `SgfView`, plus replay guard (`is_replaying`).
Signals: `manual-requested` when analysis panel content should refresh for the selected node, and `node-changed`
whenever SGF current node changes so other UI (analysis graph) can synchronize cursor state.
Collaborates with: `GCheckersModel` for move validation/application, `BoardView` to clear selection on replay/reset,
and `GGameWindow` via the `manual-requested` signal for SGF navigation/edit flows. Starting a fresh game resets
the SGF tree and emits `node-changed`, but does not force player controls back to user mode. Also exposes the current
node's move so board overlays can use the same path for step-by-step and replay-based navigation.

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
Defaults: both side controls start as `User`, and labels are backend-supplied by the window (`White`/`Black` for the
current checkers backend).
Signals: `control-changed` for window-level coordination.
Collaborates with: `GGameWindow` (signal handlers and `player_controls_panel_set_all_user()`) and GTK widgets
(`GtkDropDown`, `GtkScale`).

## `Puzzle Catalog` (`src/puzzle_catalog.c`, `src/puzzle_catalog.h`)
Module: backend-variant puzzle discovery helpers.
Role: scan one variant directory under the puzzle root, keep only `puzzle-####.sgf` files, parse their numeric puzzle
numbers, sort them ascending, and return explicit catalog entries with basename, full path, and stable `puzzle_id`.
Collaborates with: `puzzle_dialog.c` for the numbered chooser grid and `window.c` for next-puzzle selection inside the
active variant.
Storage shape: checked-in puzzles now live under `puzzles/checkers/<variant-short-name>/`, and stable puzzle IDs are
prefixed with the active game ID, for example `checkers/international/puzzle-0007.sgf`.

## `ggame_style_init()` (`src/style.c`)
Module: `ggame_style_init()` (style helper, not a class).
Role: installs application CSS once per process using `g_once_init_enter/leave`, including SGF disc colors and the
colored puzzle-picker square styles.
Owns: CSS string and `GtkCssProvider` setup.
Collaborates with: `GdkDisplay`/`GtkStyleContext` and is invoked by `GGameWindow`.

## `GGameApplication` (`src/application.c`)
Class: `GGameApplication` (`GtkApplication`).
Role: top-level application shell that installs menu actions, creates the main window, and now owns shared puzzle
progress reporting state.
Owns: the application menubar/actions plus one `GGamePuzzleProgressStore` for the process, the configured report
URL (`GCHECKERS_PUZZLE_REPORT_URL`), the privacy/settings action, and the single in-flight background upload task.
Collaborates with: `GGameWindow`, which asks for the shared store indirectly by attaching to this application, and
`puzzle_progress.c`, which provides history storage, threshold decisions, and upload JSON formatting. Puzzle uploads are
also gated by the `send-puzzle-usage-data` application setting before any network request is attempted. The shared
progress store accessor refreshes the store if `GCHECKERS_PUZZLE_PROGRESS_DIR` resolves to a different state directory,
which keeps test and manual override sessions isolated.

## Application Settings (`src/app_settings.c`, `src/app_settings.h`, `src/settings_dialog.c`, `src/settings_dialog.h`)
Module: GSettings-backed application preferences and the modal settings UI.
Role: load the shared `io.github.jeromea.gcheckers` schema, expose the new privacy keys, and present the `Settings`
dialog from the File menu.
Settings: `send-puzzle-usage-data` defaults to true and is consulted before puzzle progress uploads; `send-
application-usage-data` also defaults to true and is stored for future telemetry work but is not consumed yet; and
`privacy-settings-shown` records whether the privacy dialog has already been presented to this user.
UI: the settings dialog is a small modal window with two checkboxes and `Cancel`/`Save` actions, following the same
simple GTK window pattern as the new-game and import dialogs. It also shows a `Puzzle Progress` section with the
number of solved puzzles out of the currently available puzzle catalog and a `Clear Progress` button that clears local
attempt history plus the chooser status cache. On first launch, `GGameApplication` presents this dialog
automatically after creating the main window so the user can review the privacy controls before continuing.

## Puzzle Progress Reporting (`src/puzzle_progress.c`, `data/schemas/io.github.jeromea.gcheckers.gschema.xml`)
Module: persistent puzzle attempt storage and report payload preparation.
Role: keep a stable per-user identifier, store local puzzle attempt history, maintain a derived per-puzzle status
cache for the chooser grid, decide when unsent data is old or large enough to send, and build the full-history JSON
payload for the reporting server.
Storage layout: the preferred user ID storage is the `puzzle-user-id` GSettings key in
`data/schemas/io.github.jeromea.gcheckers.gschema.xml`. Local history lives under
`~/.local/share/gcheckers/puzzle-progress/attempt-history.jsonl` by default, or under
`GCHECKERS_PUZZLE_PROGRESS_DIR` when that override is set for tests/manual runs. The derived chooser-status cache
lives beside it as `puzzle-status.json` in the same directory; no extra nested per-file directories are used.
History format: one JSON object per line with schema version, puzzle identity, timestamps, terminal result,
first-wrong-move metadata, and local report metadata (`first_reported_unix_ms`, `report_count`). The history is never
deleted after successful upload; successful sends only mark previously unreported resolved attempts as reported. The
settings dialog can explicitly clear local progress, which rewrites both the history and status cache as empty.
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
Collaborates with: all game and model modules via compile-time limits.

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
Collaborates with: `window.c`, `create_puzzles.c`, and `create_puzzles_cli.c` for ruleset-targeted puzzle generation,
and all game creators for explicit
`game_init_with_rules()` setup.

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
`GGameModel` mirror so shared square-grid UI can consume backend-driven state without pulling checkers headers.
Collaborates with: `GGameWindow`, SGF controllers, and shared square-grid board widgets.

## Generic AI search (`src/ai_search.c`, `src/ai_search.h`)
Module: backend-driven alpha-beta search.
Role: choose a move and analyze all legal moves via depth-limited alpha-beta using only `GameBackend` callbacks for
move generation, position copying, applying moves, static evaluation, terminal scoring, side-to-move inspection, and
hashing. Root move choice randomizes among all equal best-scoring moves, so repeated games can vary without lowering
evaluation quality. Analysis APIs can report searched node counts and TT stats (probes/hits/cutoffs), and TT stats
accumulate when callers reuse the same `GameAiSearchStats` across calls.
Depth accounting treats forced plies (`exactly one legal move`) as free extensions: depth is consumed only on
decision nodes with multiple legal moves.
Score convention: search scores are white-centric at all plies (`+` good for white, `-` good for black). Root move
lists are ordered by side to move preference (white: high to low, black: low to high) so index 0 remains the best move
for the player to act.
Search integrates backend hashing plus a depth/bound/age transposition table and uses stored best moves for local move
ordering. Exposes both searched position scoring and pure static scoring through generic APIs.
Collaborates with: `game_backend.h`, `tests/test_ai_search.c`, and the checkers compatibility wrapper.

## AI alpha-beta compatibility wrapper (`src/games/checkers/ai_alpha_beta.c`,
`src/games/checkers/ai_alpha_beta.h`)
Module: checkers-facing search compatibility.
Role: preserve the existing checkers-facing `Game` and `CheckersAiTranspositionTable` APIs while delegating the real
search work to `ai_search.c` through the checkers backend adapter.
Collaborates with: `checkers_model.c`, `create_puzzles.c`, and other existing checkers-only callers that have not
yet migrated to generic AI interfaces.

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
History parsing extracts each table's `table_id`, start timestamp (rendered as `YYYY-MM-DD HH:MM`, UTC), and player
names.
Collaborates with: import dialog flow for "Fetch game history" and `tests/test_bga_client.c` (token/login/history
parsing + live login smoke test with env-provided credentials).

## Puzzle generator CLI (`src/create_puzzles.c`)
Module: CLI front end.
Role: repeatedly self-play games at depth 0, detect mistake positions with configurable best-move-depth analysis,
validate each candidate immediately in one pass, require the attacker to have at least four legal moves and a best
response at least 50 points above the runner-up, then save puzzles as SGF files under
`puzzles/checkers/<ruleset-short-name>/puzzle-####.sgf` with root setup (`AE/AB/AW/ABK/AWK/PL`), explicit
`RU[<ruleset-short-name>]`, and a tactical continuation line.
Validation and emission are now split: one path computes a validated puzzle candidate from a post-mistake position,
and separate generation/checking paths either save that candidate or compare an existing saved puzzle against it.
The CLI requires `--ruleset <short-name>` so generation, checking, deduplication, and logging all target one explicit
variant at a time. It also accepts `--depth N` to override the puzzle-analysis depth, `--synthetic-candidates` to opt
into trying synthetic bad moves in addition to the played move during generation, `--save-games` to also persist the
originating `game-####.sgf` companion files beside the puzzle files, and `--check-existing` with optional `--dry-run`
to re-validate `puzzle-*.sgf` files in one variant directory and optionally delete stale ones. Without
`--check-existing`, it uses the built-in default depth 8 and only evaluates the actual game line.
Before generating anything, the CLI loads existing `puzzle-*.sgf` files from the selected variant directory and
deduplicates by solution move sequence, so equivalent puzzles are skipped instead of being saved twice.
The main validation path is organized as puzzle-rule predicates (`position_follows_a_serious_mistake`,
`position_is_valid`, `attacker_has_enough_choice`, `attacker_has_a_single_good_move`,
`solution_line_of_best_depth_moves_improves_static_evaluation`) so puzzle selection
reads close to its checkers-language definition.
The continuation re-analyzes every ply at the configured best-move depth, requires the attacker to keep a single good
move throughout the line, allows the defender to use any best reply, and stops once static material is better than at
the puzzle start. Candidate solutions are also filtered to reject boring shapes: a one-move line, or a three-move
line of move, move, jump. If the attacker starts 400 or more points behind, the tactical line must bring the score
back to at worst 300 points behind; otherwise the candidate is rejected as an uninteresting partial comeback. After the
solution line ends, the immediate next best reply must also avoid an instant recapture, or the candidate is rejected as
unstable.
The built tool name is derived from `GAME`, so the checkers build emits `build/tools/checkers_create_puzzles` while the
`make create_puzzles` target remains stable.
While replaying a generated self-play game, the CLI analyzes each pre-mistake position at the configured best-move
depth and reuses one shared TT allocation across the whole run. When `--synthetic-candidates` is enabled, it also
tries any synthetic mistake move that already trails the best move by at least 100 points, so puzzle generation is not
limited to the exact self-play move that happened in the game.
The CLI always prints self-play completion, loaded existing solution keys, each move considered as a candidate,
indented `->` rejection or keep reasons, and a final aggregated rejection report so puzzle filtering can be followed
from the terminal. In check-existing mode, it also reports how many puzzle files were checked and how many would be or
were removed.
By default it saves only `puzzles/checkers/<ruleset-short-name>/puzzle-####.sgf`;
`puzzles/checkers/<ruleset-short-name>/game-####.sgf` companions are written only when `--save-games` is enabled.
Collaborates with: `ai_alpha_beta.c`, `rulesets.c`, `sgf_tree.c`, `sgf_move_props.c`, `sgf_io.c`,
and `puzzle_generation.c`.

## Puzzle generation helpers (`src/games/checkers/puzzle_generation.c`,
`src/games/checkers/puzzle_generation.h`)
Module: puzzle-selection and output-index helpers.
Role: expose pure functions for mistake delta checks, "enough choice" and "single correct move" tests from scored move
lists, where "single correct move" means the best score is ahead of the second-best score by a configurable margin,
plus an attacker/defender move-clarity helper, a collector for all scored moves that qualify as mistakes under a given
threshold, and next puzzle file index discovery from existing `puzzle-####.sgf` files, plus pure predicates for
rejecting boring solution-line shapes, insufficient comeback swings from badly losing positions, and immediate
recaptures after the solution.
Collaborates with: `create_puzzles.c` and `tests/test_puzzle_generation.c`.

## File dialog history helpers (`src/file_dialog_history.c`, `src/file_dialog_history.h`)
Module: SGF file dialog folder persistence helpers.
Role: create `GSettings` with the app schema, read the remembered SGF folder as a `GFile`, and store the parent folder
of a chosen SGF file so future dialogs can reopen there. The helper first probes the installed
`io.github.jeromea.gcheckers` schema, then falls back to the in-tree `data/schemas` directory for local builds/tests.
Collaborates with: `sgf_file_actions.c` and `tests/test_file_dialog_history.c`.

## App data path helpers (`src/app_paths.c`, `src/app_paths.h`)
Module: application data directory lookup helpers.
Role: resolve installed or local read-only data subdirectories such as `puzzles` by checking an explicit environment
override first, then `g_get_user_data_dir()`, then `g_get_system_data_dirs()`, then the local checkout fallback.
Collaborates with: `window.c` for packaging-safe puzzle discovery and `tests/test_app_paths.c`.

## Game backend interface (`src/game_backend.h`, `src/active_game_backend.h`, `src/games/checkers/checkers_backend.c`)
Module: generic game-selection boundary plus the default checkers adapter.
Role: `game_backend.h` defines the generic callback table used to describe one compiled game backend.
`active_game_backend.h` maps the build-time define `GGAME_GAME_CHECKERS` to the active backend object, and
`src/games/checkers/checkers_backend.c` adapts the moved checkers engine, ruleset catalog, move list, and move
formatting APIs into that generic table.
Scope: shared application code still has some checkers-native compatibility layers, but the physical checkers source
ownership boundary is now explicit under `src/games/checkers/`.
Collaborates with: `Makefile` backend selection, `tests/test_game_backend.c`, and future generic model/search work.

## Generic game model (`src/game_model.c`, `src/game_model.h`)
Class: `GGameModel` (`GObject`).
Role: wrap one active `GameBackend` plus one opaque current position behind a GTK-friendly state container with a
`state-changed` signal. The model owns backend-sized position storage, initializes it from the backend's first
variant when one exists, exposes generic move listing, application, and whole-position replacement, and now backs the
shared square-grid UI through `GCheckersModel`'s compatibility bridge.
Collaborates with: `src/game_backend.h`, `src/games/checkers/checkers_backend.c`,
`src/games/checkers/checkers_model.c`, and `tests/test_game_model.c`.

## GTK application entry (`src/gcheckers.c`, `src/application.c`, `src/application.h`)
Class: `GGameApplication` (`GtkApplication`).
Role: define the GTK application type and activation flow that creates the main window and model, installs app actions
(`app.new-game`, `app.import`, `app.quit`), installs window game/SGF/navigation/analysis/puzzle/view actions, and
publishes a menubar model (`File` -> `New game...`, `Import...`, `Load...`, `Save as...`, `Save position...`, `Quit`;
`Game` -> `Force move` + navigation section; `Analysis` -> current-position and whole-game analysis; `Puzzle` ->
`Play puzzles`; `View` -> drawer toggles) with keyboard accelerators.
The canonical application ID is `io.github.jeromea.gcheckers`, which is also used by the installed desktop file,
metainfo, icon name, and GSettings schema.
Collaborates with: `GGameWindow` for UI wiring and new-game dialog presentation.

## Board view subsystem

### `BoardView` (`src/board_view.c`, `src/board_view.h`)
Class: `BoardView` (`GtkWidget`).
Role: coordinate rendering updates, input handling, and active-turn move highlighting for the shared square-grid board
path. It now consumes backend-provided square-grid callbacks (rows/cols, playable squares, dense square indexes, piece
views, move-path prefixes, and start/destination highlight sets) through `GGameModel`, while still accepting the
legacy `GCheckersModel` via an internal compatibility bridge.
Primary-click input is routed through each square button's `clicked` signal, and right-click input uses a dedicated
secondary-button `GtkGestureClick`. A button-aware square callback allows window-level edit-mode logic to intercept
square actions (left/right) before play-mode move-selection handling.
Board orientation is driven by a generic two-side bottom-index property; the grid and last-move overlay both mirror
rows/columns when the bottom side is side 1 so rotated boards keep pieces and arrows aligned.
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
checker men avoid `GtkPicture` downscaling artifacts.
Collaborates with: `BoardGrid` and `PiecePalette`.

### Last move overlay (`src/board_move_overlay.c`, `src/board_move_overlay.h`)
Module: move overlay renderer.
Role: draw the selected SGF node's move arrow via cairo on top of the shared square-grid board and, when the game is
over, a centered backend-provided winner banner across the board.
Collaborates with: `BoardView`, `GGameModel` for backend-driven board state, and `GGameSgfController` for the
selected-node move.

### Selection controller (`src/board_selection_controller.c`, `src/board_selection_controller.h`)
Module: selection path logic.
Role: manage click-path selection and move application orchestration using backend move-path prefix callbacks rather
than direct checkers-move inspection.
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
Collaborates with: SGF view and controller modules.

### SGF move properties (`src/sgf_move_props.c`, `src/sgf_move_props.h`)
Module: SGF move property helpers.
Role: convert between SGF move properties (`B[...]`/`W[...]`) and typed move storage supplied by the active backend.
The current implementation still parses and formats checkers notation internally, but the public helper API now only
accepts opaque move storage pointers.
Collaborates with: `sgf_io` and `GGameSgfController`.

### SGF IO (`src/sgf_io.c`, `src/sgf_io.h`)
Module: SGF load/save core.
Role: serialize and deserialize SGF trees using SGF syntax (`(`, `)`, `;`, `PROP[...]`) with move properties
`B[...]`/`W[...]` and standard SGF variation nesting for branches. gcheckers writes SGF metadata (`FF`, `CA`, `AP`,
`GM`, `RU`) and does not persist current UI selection. `RU` stores the active backend variant short name and is
exposed through small tree helpers so controllers and puzzle tooling can parse or stamp the active variant. Loaders
that open playable SGFs now require `RU` to be present and valid instead of inferring a variant heuristically. Node
analysis persists through custom properties:
`GCAD[depth]`, `GCAS[nodes=...;tt_probes=...;tt_hits=...;tt_cutoffs=...]`, and repeated
`GCAN[move:score:nodes]` for scored moves, while still accepting older `GCAN[move:score]` data when loading. This
layer is GTK-free so it can be reused by both GUI actions and future CLI commands.
Collaborates with: `GGameSgfController` load/save entry points and `tests/test_sgf_io.c`.

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
`SgfView` now refuses to start a scroll request at all when the scrolled window is currently unmapped, which avoids
queueing impossible retries while puzzle mode hides the navigation panel.
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
