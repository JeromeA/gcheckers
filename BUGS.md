BUGS

This document lists past bugs, their symptoms, and how they were fixed. Add any new bugs you find to the end of this
list. The first entry below shows a template.

## Short description of the bug

[How the code was trying to do things right]

[What was actually happening and why]

[How it was fixed]

## SGF variation move 1 discs did not share a common parent

The SGF move list should show all move 1 variations branching from a shared root so sibling variations look connected.

The layout skipped rendering the root node entirely, so move 1 discs were placed in separate rows without a visible
parent and the link renderer could not draw a shared connector.

The fix renders the root node as a virtual move zero dot, anchors it in column zero, and shifts the first move column
over so every move 1 disc connects back to the shared root.

## GTK tests were skipped during make test

The test target should exercise all GTK widget tests in headless environments.

The GTK tests called gtk_init_check without a display backend, so the test binaries skipped their assertions when
XDG_RUNTIME_DIR or a Broadway display was not configured.

The fix runs the GTK test binaries under a shared Broadway server from make test so gtk_init_check succeeds and the
tests run instead of skipping.

## SGF horizontal scrollbar position could disagree with visible content position

The SGF inconsistency diagnostic should report only whether the horizontal scroll-window position matches the content
view effective horizontal position.

The old diagnostic compared and logged extra geometry (viewport sizes, vertical offsets, selected-node expected/actual
positions), which made the bug signal noisy and not focused on the visual mismatch being investigated.

The fix narrows the calculation and message to horizontal position mismatch only, and adds a test for the focused
inconsistency predicate.

## SGF inconsistency message fired for tiny drifts instead of large mismatches

The SGF inconsistency diagnostic should highlight only substantial horizontal disagreement between the scrolled-window
position and the content view position.

The prior threshold treated sub-pixel and small pixel differences as inconsistencies, so the message triggered for minor
layout drift instead of only severe displacement.

The fix raises the inconsistency threshold to differences strictly above 30 pixels and updates the emitted message to a
BIG inconsistency diagnostic.

## SGF overlay sizing drifted because overlay and drawing area were manually resized

The SGF view should derive overlay content size from the measured SGF node grid, with no manual size requests on the
overlay stack.

The old implementation manually set size requests on the overlay, drawing area, and viewport using computed extents.
Those explicit size requests could drift from GTK's measured layout and created extra resize paths.

The fix removes all SGF overlay stack size-request logic and sets `gtk_overlay_set_measure_overlay(..., tree_box, TRUE)`
so the tree grid alone drives overlay measurement.

## SGF scroller geometry math made retries and clamp targets brittle

The SGF scroller should clamp horizontal scrolling directly to the selected widget bounds and retry only when those
bounds are not ready yet.

The old implementation rebuilt target rectangles from grid row/column metadata, margins, spacings, and padding, and
also clamped vertical adjustments. That extra geometry path could diverge from actual widget bounds and complicated
readiness handling.

The fix removes derived geometry math and clamps only to `[bounds.origin.x, bounds.origin.x + bounds.size.width]`
from `gtk_widget_compute_bounds`; when bounds are unavailable or x is negative, it schedules an idle retry.

## SGF inconsistency diagnostics remained after scrolling behavior was fixed

The SGF view should keep scrolling behavior simple and avoid stale diagnostic geometry calculations once the scroller
logic is stable.

The old SGF view still carried horizontal inconsistency predicate logic and detailed layout-sync diagnostic geometry
code that depended on derived extents and extra bounds comparisons unrelated to the active scroll path.

The fix removes those diagnostic helpers and the associated test coverage, leaving link rendering and scrolling based on
disc bounds and selected-widget bounds respectively.

## SGF scroller exposed multiple retry entry points instead of one scroll API

SGF selection scrolling should expose a single caller API that either scrolls now or retries later internally.

The old scroller split behavior between `request_scroll`, `on_layout_changed`, and helper retry functions. That forced
callers to know when to trigger retries and scattered the scroll path across multiple entry points.

The fix replaces those paths with `sgf_view_scroller_scroll()`: it remembers selected-node context, tries to clamp
immediately, and schedules one internal idle retry path when selected widgets or bounds are not ready yet.

## SGF/Game state drifted because model history polling was used as timeline source

Move chronology and navigation should come from `SgfTree` current-node transitions, with game state projected from SGF.

The old path appended SGF nodes by polling `GCheckersModel` history size/last move after model state changes, while
board and AI actions could mutate the model directly. This inverted ownership and allowed SGF/game drift and stale
selection pointers when SGF reset/rebuild timing diverged.

The fix makes SGF the authority: move application now validates the model move, appends under SGF current, advances SGF
current, then updates the model from that SGF transition (single-step parent->child or reset+replay from root). Game
history storage was removed from `Game`, and SGF controller/window/board paths were rewired to use SGF-first APIs.

## New games started with black controlled by computer by default

A new game should start with both white and black player controls set to `User`.

The player controls panel initialized white to `User` but black to `Computer`, so a fresh game auto-played black unless
the user changed the dropdown manually.

The fix sets both dropdown defaults to `User` and updates widget tests to assert the new default behavior.

## Analysis node count appeared frozen until depth completed

The analysis panel should show node-count progress continuously while a single depth search is running.

The worker thread only published text when a depth finished, so even with a 100ms UI timer the `Nodes` value stayed
unchanged for long-running depths and then jumped at depth boundaries.

The fix adds alpha-beta progress callbacks that report running node counts during search, and the analysis worker now
publishes throttled in-depth `(searching...)` snapshots that the main thread flushes every 100ms.

## Analysis progress snapshots hid move scores between completed depths

The analysis report should keep showing move scores while also reporting live node-count progress.

The progress text replaced the full report body with a `(searching...)` placeholder, so users could temporarily lose
the best-to-worst scored move list until a depth completed and published the final report again.

The fix keeps a copy of the last completed depth report and includes its scored move section in in-progress snapshots,
while still updating `Nodes` live for the currently searching depth.

## New game dialog added extra blank space and kept growing when ruleset summary changed

The `New game` modal should keep a stable compact height when switching rulesets.

After adding the ruleset summary label, changing selection to longer summaries made the dialog grow taller and it did
not shrink back, which left visible blank space below the action buttons.

The fix keeps the dialog non-resizable and makes the summary a single-line ellipsized label, removing wrap-driven
height-for-width changes when the summary text changes.

## SGF nodes stored opaque payload bytes instead of SGF properties

The SGF in-memory model should mirror SGF file semantics by storing named properties per node (for example `B[...]`,
`W[...]`, `FF[...]`) rather than an opaque binary blob.

Move nodes stored `CheckersMove` data in `GBytes` payloads, and SGF save/load converted between payload bytes and SGF
text on every boundary. This made the tree representation diverge from SGF structure and dropped repeated property
values during parsing.

The fix replaces payload storage with per-node property maps (`ident -> values[]`), adds typed move-property helpers,
migrates SGF IO and controller paths to property-based access, and preserves repeated values during parse/save
roundtrips.

## Analysis scores oscillated by side to move instead of keeping one global sign convention

Analysis scores should use one stable sign convention so graph/report values are directly comparable across plies.

The alpha-beta score was produced from the current player perspective at each analyzed node. That made positive values
mean "good for the side to move", so branch values naturally flipped sign on alternating turns and appeared to swing
around zero in graphs and saved reports.

The fix makes scoring white-centric everywhere (`+` good for white, `-` good for black), keeps minimax behavior by
maximizing on white turns and minimizing on black turns, sorts root move lists by side-to-move preference (white
descending, black ascending), and updates mistake predicates/tests to compare scores using the mover color.

## SGF loader rejected setup-only nodes and ignored AB/AE/AW/PL during replay

SGF import should accept non-move nodes and apply setup/turn properties so loaded timelines reproduce board state.

The parser required every non-root node to contain exactly one of `B[]` or `W[]`, and controller replay only applied
moves. SGFs with setup-only nodes (`AB`, `AE`, `AW`) or side-to-play (`PL`) therefore failed to load or replayed with
incorrect board/turn state.

The fix allows non-move nodes in SGF parse, adds setup-node replay handling for `AB/AE/AW/PL` on root and child
nodes, and extends SGF IO/controller tests to cover setup-property load and navigation behavior.

## Save position SGF reloaded on top of initial setup and lost king identities

Saving a standalone position should round-trip exactly when loaded, with no extra pieces and preserved kings.

The first version wrote only `AB/AW/PL`, so loading applied piece additions over the engine's default initial board.
That produced extra men. It also encoded kings as normal men, so king state was lost on load.

The fix writes full setup snapshots with `AE` (all empties) first, then `AB/AW`, plus custom king markers
`ABK/AWK`, and updates setup replay to validate and apply `ABK/AWK` as king subsets of `AB/AW`.

## Board edit/play clicks were intermittently ignored after one successful click

Board square input should be processed consistently on every click, including repeated clicks on the same square.

## Starting a new computer-vs-computer game reset both sides to user control

Choosing computer players in the New game flow should carry through to the fresh game that gets created.

`gcheckers_sgf_controller_new_game()` emitted `manual-requested` after resetting the SGF tree, and the window's
`manual-requested` handler is intentionally used by SGF navigation/edit flows to force both controls back to user.
That made a new computer-vs-computer game immediately revert to user-vs-user.

The fix stops emitting `manual-requested` from the new-game path, keeps the reset scoped to real manual SGF flows,
and adds a window regression test to verify computer controls survive repeated New game actions.
Dark squares are `GtkButton` widgets. The board wired square actions to an additional generic `GtkGestureClick`
(`button=0`) on the same button, while the button itself handled activation internally. In failing cases, GTK still
delivered and activated the button (`clicked`), but the custom gesture callback path did not run, so board logic saw no
click.

The fix routes primary clicks through `GtkButton::clicked` and keeps only a dedicated secondary-button gesture for
right-click handling, eliminating the competing primary click gesture path. A board-view regression test now asserts
that repeated primary clicks are processed.

## Forced move plies consumed alpha-beta depth budget

## Puzzle Analyze restored the pre-puzzle board orientation

Entering a puzzle should orient the board for the puzzle attacker, and switching from that puzzle into analysis should
keep showing the same position from the same side unless some later action explicitly changes orientation.

The window saved `board_orientation_mode` and `board_bottom_color` on puzzle entry and restored them on puzzle exit.
Clicking `Analyze` therefore discarded the current puzzle-facing orientation and jumped back to whatever side had been
visible before the puzzle, which was often white.

The fix removes board orientation from the saved puzzle-mode snapshot. Puzzle mode still sets a fixed attacker-facing
orientation when it starts, but leaving puzzle mode now restores only layout/drawer state and keeps the current board
orientation intact.

## Puzzle mode window width could jump back to the normal layout on focus changes

While puzzle mode is active, the window should keep the puzzle layout width with both drawers hidden.

GTK could change the toplevel `default-width` while the window was inactive. Because puzzle mode only owned the saved
panel widths and not the toplevel default width, the window could reactivate at the normal-layout width even though
the drawers were still detached and hidden.

The fix makes puzzle mode reassert its own expected `default-width` whenever a foreign width change happens, and adds a
window regression test that simulates an external `gtk_window_set_default_size()` while puzzle mode is active.

Analysis depth should count only decision points, so mandatory single-move plies should not reduce remaining depth.

The search decremented `depth_remaining` on every recursive ply, including forced plies with exactly one legal move.
This reduced effective lookahead in tactical forcing lines and made configured depth inconsistent with user intent.

The fix keeps `depth_remaining` unchanged when `moves.count == 1`, applies cutoff only when depth is zero on
non-forced nodes, and also updates root move analysis so a forced root move does not consume depth before recursion.
Regression coverage was added in `test_checkers_model`.

## Puzzle continuation targets used searched depth-0 score instead of static material

Puzzle continuation extraction should stop when pure board material reaches the depth-8 target score for the candidate.

The generator used `checkers_ai_alpha_beta_evaluate_position(..., depth=0)` as its "eval0". After forced-move depth
extensions were introduced, depth-0 search was no longer purely static material and could change across non-capturing
moves due to forced tactical continuations.

The fix adds a dedicated public API `checkers_ai_evaluate_static_material()` and updates puzzle continuation building to
use that static evaluator for target matching.
