BUGS

This document lists past bugs, their symptoms, and how they were fixed. Add any new bugs you find to the end of this
list. The first entry below shows a template.

## Short description of the bug

[How the code was trying to do things right]

[What was actually happening and why]

[How it was fixed]

## Puzzle continuation targets used searched depth-0 score instead of static material

Puzzle continuation extraction should stop when pure board material reaches the depth-8 target score for the candidate.

The generator used `checkers_ai_alpha_beta_evaluate_position(..., depth=0)` as its "eval0". After forced-move depth
extensions were introduced, depth-0 search was no longer purely static material and could change across non-capturing
moves due to forced tactical continuations.

The fix adds a dedicated public API `checkers_ai_evaluate_static_material()` and updates puzzle continuation building to
use that static evaluator for target matching.

## Full-game analysis rebuilt puzzle nodes from move replay instead of SGF state

Full-game analysis should reconstruct each target node from the SGF itself so setup-root puzzles, edited positions, and
variation nodes analyze the exact position shown in the UI.

The full-game worker built a move list for each SGF node and replayed those moves from a fresh default `Game`. That
ignored SGF setup properties such as `AE`, `AB`, `AW`, `ABK`, `AWK`, and `PL`, so setup-root puzzle nodes could fail
replay, report `replay skipped`, and end up with missing saved analysis.

The fix centralizes SGF node replay into a shared helper in `sgf_controller.c` that applies setup properties and moves
from root to the exact node. Full-game analysis now uses that helper instead of a move-only reconstruction path.

## Board men were raster-shrunk through GtkPicture and showed visible source pixels

The checker men should be rasterized at the final square size so their curved edges stay smooth.

`BoardSquare` rendered the procedural `GdkPaintable` men through `GtkPicture`. The paintable advertised a `64x64`
intrinsic size, but board squares are only `31x31`, so GTK shrank the already-rasterized result during picture
rendering. That exposed source pixels around ellipses and made the men look un-antialiased.

The fix replaces the `GtkPicture` path with direct cairo drawing in a `GtkDrawingArea` sized to the board square. The
same man renderer is now shared between the paintable snapshot path and direct square rendering, and a regression test
checks that the direct renderer produces partially covered edge pixels.

## Piece renderer centered men and kings from the same origin, leaving them visibly low

Men and kings should be vertically centered from their actual painted bounds, even though kings are stacked from two
layers with a different overall height.

The renderer anchored all layers around the same ellipse center. That put single men several logical units below center,
and the taller stacked king shape needed a different offset again, so both piece types looked low in their squares.

The fix computes the vertical origin from the layer count before drawing. Men and kings now use separate centering math,
and regression tests verify that both painted bounds are vertically centered.

## Puzzle attempts vanished on restart and could not be reported later

Puzzle mode should retain every started attempt so a wrong move, success, or Analyze abandonment survives shutdown and
can be uploaded later once the local threshold is met.

The original puzzle runtime lived only in `src/window.c` and kept no persistent attempt state. Once the user left the
puzzle, started another one, or quit the app, the result was gone. There was also no stable per-user identifier or
local history file for later reporting.

The fix adds a dedicated `src/puzzle_progress.c` module with a stable user ID, JSONL attempt history, threshold logic,
and upload payload construction. `GCheckersApplication` now owns the shared store and background flush requests, while
`GCheckersWindow` records one attempt per opened puzzle entry.

## Puzzle continuation still used a random chooser after direct puzzle selection landed

Once the puzzle picker grid let the user choose an exact numbered puzzle, continuing from `Next puzzle` should have
stayed inside that ordered puzzle list instead of jumping to an unrelated random puzzle.

The dialog flow was updated to launch an explicit path, but the `Next puzzle` button still called the older random
ruleset helper. That left one remaining runtime path that ignored the selected puzzle order, so finishing puzzle 1
could jump straight to puzzle 27.

The fix removes that random continuation path and makes `Next puzzle` walk the sorted ruleset catalog in order,
wrapping only after the last puzzle in that variant.

## Puzzle Analyze launched full-game analysis from the wrong node and hid the current move report

Puzzle `Analyze` should leave puzzle mode, rewind to move 0, start the normal analysis flow from there, and keep
showing whatever saved report belongs to the node currently selected.

The old button handler first launched the wrong analysis mode, and later still forced an extra step to move 1 after
the rewind. That replaced the panel text with generic whole-game progress messages such as “all moves analyzed” or
made move 1 behave like a special case, even though puzzle analysis should follow the same full-game path as the
regular Analysis menu.

The fix rewinds fully to move 0 and starts the shared full-game analysis path. The analysis panel now preserves the
selected node’s saved report while analysis runs, rather than replacing it with a generic full-game status message.

## Analysis progress text replaced the current node report

The analysis drawer should always show the saved report for the currently selected SGF node. Transient progress belongs
in separate status UI and must not overwrite the node report itself.

The window reused the same text view both for saved analysis reports and for runtime status strings such as
“Analyzing full game...”. That meant starting analysis from a puzzle, or from any other node, could hide the current
node’s report behind progress text until the analysis session ended.

The fix adds a dedicated status label under the graph for progress updates and moves report refresh onto the normal
SGF node-changed path. The text view now consistently follows the selected node, while status updates stay separate.

## Puzzle Analyze still launched single-node analysis instead of the shared full-game path

Leaving puzzle mode through `Analyze` should use the same full-game analysis path as the regular Analysis menu after a
full rewind to the puzzle root.

The puzzle button was rewinding to the first puzzle move, but then it called the current-position analysis starter
instead of the normal full-game starter. That made move 1 look like a special case, filled the status label with the
single-node progress/report text, and left every other node without saved analysis.

The fix reuses the shared full-game analysis entry point from the puzzle button, so puzzle Analyze now produces the
same status updates and per-node reports as a normal full-game analysis run.

## Puzzle attempt timing started only after the first move

Puzzle attempt timing should include the time spent looking at the opened puzzle before choosing the first move.

The window created the persistent attempt record from the move handler, so `started_unix_ms` represented the first
player move attempt rather than the puzzle-open time. Leaving a puzzle without moving also produced no history entry
because no attempt had been started yet.

The fix starts the unresolved attempt record as soon as a puzzle is opened. Terminal updates still replace that same
record with `success`, `failure`, or `analyze`, and first-move failure detection now tracks whether the player has
attempted any move separately from whether the record already exists.

## Simple board moves could be selected but not played through the shared square-grid UI

The shared selection controller should apply an exact move as soon as the user finishes a valid path, including a
normal non-capturing move with exactly two squares.

The generic board path asks the backend for move length first with `square_grid_move_get_path(move, &len, NULL, 0)`,
then checks whether the clicked path exactly matches one legal move. The checkers backend rejected every non-empty move
in that "length-only" mode because it compared the move length against `max_indices` even when no output buffer was
requested. That made exact two-point moves look like extendable prefixes, so the destination square turned green
instead of applying the move.

The fix lets the checkers backend answer length-only path queries when `out_indices == NULL`, and a regression test now
verifies that a simple opening move can be queried that way and still reports its full two-square path.

## Boop promotion selection could show the pre-boop board and ask for confirmation with no real choice

Once a boop placement is made, any follow-up promotion selection should render the board after the boops have already
happened. Forced single-line promotions should also apply immediately instead of surfacing a confirm step with nothing
selectable.

The shared board view always painted the committed model position, even while a backend move builder was in an
intermediate post-selection state. Boop therefore showed the pre-boop board during promotion handling. Separately, the
UI deferred completion whenever a move carried any promotion mask at all, even when the backend had already resolved the
only legal promotion set and exposed no selectable continuation squares.

The fix lets backends provide a builder preview position and teaches boop to expose its post-placement board during the
promotion stage. Promotion selection paths are now separate from the placement path, so the placed piece is not shown as
selected unless the user explicitly selects it for promotion. Pending promotion choices also reset or change selection
on board clicks instead of applying a move; only the confirmation button can apply an unresolved promotion move. The
promotion confirmation UI appears only when the backend still has real promotion choices to resolve, while forced
three-kitten promotions auto-apply again.

## Boop SGF updates could leave the SGF pane or board one step behind

The boop SGF pane should add a visible node as soon as a move is played, and navigation buttons should move both the
visible SGF selection and the board state in lockstep.

Two refresh paths had drifted apart. First, move application still appended to the SGF tree and updated the bound
model, but the SGF widget was no longer rebuilt after adding a new node. That left the internal current-node pointer
ahead of the visible node widgets. Second, the shared board view did not subscribe to `GGameModel::state-changed`, so
SGF replay in boop could update the model position without forcing a board redraw. Because replay also cleared board
selection before publishing the new model position, each navigation click repainted the old board first and only showed
the newly selected node's board on the next click.

The fix restores an SGF view refresh after appended moves so the widget tree is rebuilt immediately from the mutated
SGF tree, and it teaches `BoardView` to redraw itself whenever its bound `GGameModel` emits `state-changed`. Regression
tests now check the visible SGF disc count after the first appended move and verify that the board highlights update
after an external model move without a manual `board_view_update()` call.
