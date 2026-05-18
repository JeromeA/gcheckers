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

## Boop could crash on startup when the shared settings dialog auto-opened

Boop should reuse the shared settings dialog for privacy controls without assuming that it also exposes checkers puzzle
catalog features.

The shared dialog always built the `Puzzle Progress` section and counted puzzles through
`GGAME_ACTIVE_GAME_BACKEND->variant_by_short_name(...)`. That was safe for checkers, but boop's backend does not
implement `variant_by_short_name` because it has no puzzle variants. On first launch, `GGameApplication` auto-opened
the settings dialog and immediately dereferenced that null function pointer, so `gboop` could segfault before the user
even started a game.

The fix makes puzzle-progress support a derived `GGameAppProfile` capability, builds that settings section only when
the active profile really has a puzzle catalog, and counts variants generically through `backend->variant_at()`
instead of using checkers-only ruleset helpers. A boop window regression test now opens the shared settings dialog and
asserts that the puzzle-progress section is absent.

## Boop board size was capped by checkers pane defaults after the shared-shell merge

The merged boop target should preserve roughly the same usable board size as the old standalone `gboop` window.

After boop moved into the shared shell, `GGameWindow` still initialized every target with the checkers layout defaults:
`500px` for the board pane, `300px` for the SGF pane, and another `300px` for the analysis pane. Boop's custom board
host is much wider than checkers because it keeps both supply panels beside the board. With only `500px` of board-pane
width, the square board host could only allocate a small square in the middle, leaving large empty bands above and
below. The always-visible but unsupported analysis drawer also wasted width that boop could not use.

The fix moves shared-shell layout defaults into `GGameAppProfile`. Boop now starts with a wider `760px` board pane and
the analysis drawer hidden by default, while checkers keeps the previous defaults. A boop window regression test now
checks that the wider board pane request and hidden analysis drawer are both applied at startup.

## Boop removed-piece markers could disappear underneath off-board arrows

When a booped kitten or cat left the board, the last-move overlay should still make the removal square obvious.

The boop overlay painted red removal crosses before the green boop arrows. For off-board boops, the arrow starts in the
same square that is being marked as removed, so the later arrow stroke could cover the cross and make the removal
marker look absent.

The fix factors boop overlay painting into a reusable helper and paints removal crosses after arrows so the removed
square remains visible. A cairo-level regression test now checks that an off-board removal still leaves a red marker on
top of the arrow.

## Boop used square numbers instead of board coordinates

Boop notation should use edge coordinates, not dense square numbers painted on every playable square.

The shared square-grid board always rendered each playable square's dense index. That matched checkers well enough, but
boop's notation names squares by board coordinates. Showing `1..36` inside the cells was misleading, and it left no
coordinate guide around the board edge.

The fix hides the shared dense square numbers through boop-local CSS and paints boop coordinates onto the board's
existing border instead of reserving space around it. Letters run along the bottom edge, numbers run down the left
edge, and both update when board orientation flips so the visible coordinates stay aligned with the rotated board.

## Checkers puzzles could reopen at the starting position

Checkers puzzles should load the setup-root position stored in the SGF file, not the normal starting layout.

After the shared window switched to binding the generic `GGameModel`, SGF replay for checkers puzzle loads started
flowing through the generic backend-position path. That replay helper only reapplied moves, so a puzzle whose root
used SGF setup properties such as `AE`, `AB`, `AW`, or `PL` would ignore them and leave the board at the default
starting position.

The fix keeps runtime profile selection intact but routes checkers generic-position replay back through the
setup-aware checkers replay helper. A headless SGF controller regression now checks that replaying a root setup node
into a generic checkers backend position produces the configured puzzle state.

## Boop puzzle tooling exposed checkers-only ruleset assumptions

The boop launcher should generate boop puzzles from the boop profile, and `gboop` should be able to open those
puzzles without inventing a ruleset or variant.

After the runtime-profile launcher split added `build/tools/boop_create_puzzles`, it still ran the checkers
generator entrypoint all the way into checkers CLI parsing. That made the boop-named binary fail with
`Missing --ruleset <short-name>`. A later temporary launcher avoided that specific error by reporting boop as
unsupported, but the core problem remained: puzzle generation, catalog discovery, the picker dialog, progress IDs, and
runtime playback all still assumed a checkers variant.

The fix adds a boop generator that uses boop's backend AI and SGF snapshot hooks, stores files directly under
`puzzles/boop/`, and rejects `--ruleset` explicitly. Shared catalog, dialog, settings, and window puzzle code now
accept zero-variant backends, using puzzle IDs such as `boop/puzzle-0000.sgf`. Regression coverage checks generation,
check-existing, zero-variant catalog loading, and boop puzzle opening when GTK is available.

## Boop could not save and reload position-only SGFs

Boop should be able to use the shared `Save position...` action and reopen the saved file at the same midgame board,
supplies, and side to move.

The shared action was only enabled for profiles that claimed `supports_save_position`, and boop still reported that as
false. Underneath, the controller implementation was also still hardwired to `GCheckersModel`: it serialized checkers
setup properties directly from `GameState`, and the generic SGF replay path only reapplied `B[]`/`W[]` moves without
any backend-owned root setup handling. Even if boop had exposed the action, loading a saved snapshot would have
fallen back to the opening position.

The fix moves SGF position snapshots behind backend hooks. `GGameSgfController` now asks the active backend to apply
node setup during replay and to write a root snapshot for `Save position...`. Checkers moved its existing setup codec
behind those hooks, and boop adds its own root snapshot codec for board pieces, supply counts, and `PL`. Boop now
enables `supports_save_position`, and regression coverage includes a headless boop SGF snapshot roundtrip plus the
controller/save-position path when GTK is available.

## Shared SGF color mapping treated side 0 as black for every game

SGF move color should follow the active backend's side numbering, not a global convention.

The shared SGF controller mapped side 0 to `B[]` and side 1 to `W[]`. That matched boop, but checkers defines side 0 as
white and side 1 as black. Generic SGF replay therefore rejected valid checkers setup-root lines when color validation
was enabled, and newly appended checkers moves could be stamped with the wrong SGF color.

The fix adds a backend-owned `sgf_color_for_side()` callback. Checkers maps side 0 to `W[]` and side 1 to `B[]`, while
boop maps side 0 to `B[]` and side 1 to `W[]`. Regression coverage now asserts both backend mappings and replays the
checkers setup-root path through the shared controller.

## Boop puzzle generation skipped shared self-play progress

Boop puzzle count generation should show the same terminal phases as checkers: start a depth-0 self-play game, finish
that game with its move count, then analyze each move from the played line.

The boop generator tried handcrafted seed positions before self-play and analyzed self-play positions while the game
was still being played. When the requested puzzle count was satisfied by a seed position, the command produced a
puzzle without ever reporting the full-game progress that the checkers generator already showed.

The fix adds a shared create-puzzles progress helper, routes checkers and boop through it, and changes boop count-mode
generation to play a complete depth-0 game before analyzing the resulting move line. Seed positions remain only as a
fallback after self-play attempts fail to produce enough puzzles.

## Checkers puzzle generation could retry source games forever

`checkers_create_puzzles --ruleset international --depth 1 1` could keep playing and analyzing new source games
indefinitely when the generated games failed every puzzle-interest filter. The terminal looked stuck on whichever
candidate move was currently being analyzed, but the outer count-mode loop had no attempt limit and therefore had no
way to report that it could not satisfy the requested count.

The fix makes checkers use the shared create-puzzles source-game attempt limit that boop already needed. Count-mode
generation now stops after a bounded number of source games, prints the normal rejection report, and exits with an
explicit "Only generated..." error when it cannot produce the requested number of puzzles.

## Boop analysis notation used top-origin ranks while the board showed bottom-origin ranks

Boop coordinates should use `a1` at the bottom-left corner, and every user-facing move formatter should agree with the
board labels.

The shared square-grid display treated backend row 0 as the top row for side 0, while Boop notation formatted internal
row 0 as rank 1. The Boop coordinate labels were already bottom-origin, so a user move shown on the board as `d3` could
appear in analysis output as a different ranked square.

The fix makes the square-grid backend contract side-0 bottom-origin, updates the generic grid and overlay transforms to
display row 0 at the bottom for side 0, and adapts checkers at its backend boundary to keep its existing internal
top-origin board storage.

## Boop terminal scores depended on search path depth instead of game state

Winning Boop positions should evaluate from the actual game history, so the same terminal position has the same score
whenever it appears in search or replay.

Boop reused the generic `terminal_score(outcome, ply_depth)` callback and returned `10000 - ply_depth`. That `ply_depth`
was the recursive search distance from the current root, not a value stored in the position. The same terminal position
could therefore score differently depending on how the search reached it, and a transposition-table entry could carry a
path-specific distance adjustment.

The fix stores a total ply count in `BoopPosition`, increments it on each applied move, serializes it in Boop SGF
snapshots, includes it in the position hash, and makes Boop terminal scores use `10000 - position->ply_count`.

## Legacy Homeworlds random AI could choose pass

The legacy Homeworlds random helper should have picked an actual playable move when one was available, not consumed its
turn with the interactive pass fallback.

The staged move builder exposes pass as a ship-selection candidate for human play. The random AI reused that candidate
list and explicitly accepted pass whenever it was not resolving a sacrifice action, so some random seeds produced a
`pass` move even in positions with legal ship actions.

The first fix filtered pass out of the random helper. That helper was later removed, and the no-pass policy now lives in
the backend good-move generation used by computer play.

## Homeworlds bypassed the shared SGF shell

Homeworlds should use the same application window as the other games, so SGF navigation, load/save actions, menus, and
toolbar controls are owned by generic code.

The Homeworlds profile installed a custom toplevel window hook. That window owned its own model and view, so there was
no shared `GGameSgfController`, no SGF drawer, and no generic file-action path for Homeworlds moves. The profile API
also made that split an available option for future backends.

The fix removes the custom-window hook from app profiles. `ghomeworlds` now launches `GGameWindow`, installs the
Homeworlds view only as a board host, routes completed Homeworlds moves through the shared move handler, and adds
Homeworlds SGF move parsing plus whole-position snapshot hooks. Regression coverage verifies that a Homeworlds setup
played through the generic window appends SGF nodes and can be replayed through the shared controller.

## Legacy checkers model could go stale after generic-window moves

Checkers compatibility callers that still hold a `GCheckersModel` should observe moves applied through the shared
`GGameModel` path.

The generic window now applies moves through `GGameSgfController` and the backend-backed `GGameModel`. The legacy
`GCheckersModel` wrapper published its state into that generic model, but did not mirror state changes coming back from
generic callers. Tests that inspected the compatibility wrapper after a forced move still saw the starting turn.

The fix connects the wrapper to its inner `GGameModel::state-changed` signal and copies the generic checkers position
back into the legacy wrapper, with a guard to avoid recursive self-sync notifications.

## Homeworlds loss detection was delayed after self-vacating a homeworld

Moving the last ship out of your own homeworld should lose immediately at the end of that turn.

`homeworlds_position_finish_turn()` switched `position->turn` to the opponent before checking whether the side to move
had any ships at their homeworld. That detected opponent homeworld destruction, but missed the case where the acting
player vacated or destroyed their own homeworld. The game stayed ongoing for the opponent's next move and only ended
when turn switched back.

The fix checks the acting player's homeworld before switching turns, then checks the opponent's homeworld, and only
advances the turn if neither side has lost. Regression coverage now asserts both self-vacating loss and opponent
homeworld destruction.

## Homeworlds computer play ignored the simple AI directives

The Homeworlds computer player should use the shared alpha-beta path, but its candidate pool should still follow the
basic policy that the AI never passes and never chooses dead-end actions such as attacks with no target.

Those directives were implemented only in an unused random-AI helper. `Force move` and computer-controlled players
call the generic SGF/alpha-beta flow, which asks the backend for `list_good_moves()`, so the random helper had no
effect on normal app play.

The fix moves the policy into the Homeworlds backend good-move generation and removes the unused random-AI module.

## Homeworlds last-move panel stayed stale during SGF navigation

The Homeworlds text panel should show the move attached to the currently selected SGF node, not only the last move that
was played through the board controls.

The shared SGF controller correctly replayed the model when navigating the SGF tree, but the Homeworlds board host kept
its own `Last move` label and only updated it when the view completed a move itself. Rewinding or stepping through SGF
nodes therefore left the panel showing stale text from the previous direct interaction.

The fix adds a generic board-host SGF synchronization hook in the app profile UI hooks and implements it for
Homeworlds by parsing the selected SGF node's move property. The shared window calls this hook on every current-node
change, so the Homeworlds side panel now updates during SGF timeline navigation.

## Homeworlds manual catastrophes bypassed SGF recording

Homeworlds catastrophes are free actions that can happen at any time, but they should still be recorded in SGF and
shown in the same notation as other moves.

The catastrophe buttons mutated a copied position directly and then replaced the model state. That kept the board
visually correct, but skipped the shared move handler, so no SGF node was appended and the last-move label displayed a
generic `catastrophe` string instead of a replayable move.

The fix represents catastrophes as normal symbolic `HomeworldsMove` steps such as `G3 y!`. The view now submits that
move through the same completion path as other Homeworlds actions, while the rules engine allows catastrophe-only
moves without advancing the turn.

## Homeworlds discovery listed identical bank stars as separate choices

The Homeworlds move builder should expose one discovery choice per symbolic new star, not one choice per physical copy
of that pyramid in the bank.

Discovery target generation scanned every bank slot and appended a candidate for each matching physical pyramid. If
the bank still had multiple `B3` pyramids, the UI showed multiple `B3` discovery choices even though all of them would
produce the same symbolic move.

The fix deduplicates discovery candidates by pyramid before they are shown, also deduplicates identical capture targets
and completed backend moves, and keeps committed moves represented by symbolic system/ship references rather than bank
slots.

## Homeworlds target selection stayed in the side panel and systems drifted outside the board graph

Homeworlds target choices should be made on the visual object being selected: bank pyramids for setup, trade, and
discovery; ships for activation and capture; and systems for normal movement.

The view only overlaid buttons for setup bank choices and ship activation. Trade colors, capture targets, and movement
targets still appeared as side-panel buttons, while discovered systems were placed by raw slot number in a fixed grid
that could put a new system above player 2's homeworld instead of between the two homeworld rows.

The fix reuses the same staged builder for visual target hitboxes, keeps selectable bank buttons transparent with only
a visible border, and computes non-home system rows from reachability relative to the two homeworlds.

## SGF navigation reset computer-player settings

Player control settings should be persistent runtime preferences. Navigating backward in SGF history, selecting another
node, or returning to the current head should not silently change a player from `Computer` back to `User`.

The SGF controller emits `manual-requested` for timeline navigation, and the window handled that by forcing both player
controls to user mode. That avoided automatic replies during review, but it also discarded the explicit computer-player
configuration the user had chosen. Selecting an already-current SGF node also only emitted the manual-navigation signal,
so the analysis report could stay stale because no `node-changed` signal was emitted.

The fix keeps SGF navigation's board-orientation and analysis refresh behavior, but stops mutating the player controls.
The selected `User`/`Computer` modes and computer depth now persist across SGF navigation and branch exploration, and
manual SGF navigation refreshes the analysis report from the current node.

## SGF navigation triggered computer replies

Tree navigation should only review recorded nodes. It should not be treated as a newly played move, even when the
selected node leaves a computer-controlled player to move.

Direct parent-to-child SGF navigation replayed the stored move by applying it to the live model without setting the
SGF replay guard. The window saw the resulting state change as normal play and scheduled an automatic computer move.
The fix marks all SGF navigation model synchronization as replay/manual state and cancels any pending auto-move source
when manual navigation is requested.

## Homeworlds ship catastrophes could leave orphaned stars

A star system with no ships should disappear and return its stars to the bank. This must happen whether the last ship
left by movement, sacrifice, capture, or catastrophe.

Ship-only catastrophes removed all matching ships but only performed abandoned-star cleanup when a star of the same
color had also been destroyed. A system could therefore keep stars on the board with no ships. The fix runs the normal
orphan-star cleanup after every successful catastrophe.

## Homeworlds manual catastrophes were recorded as separate same-player SGF moves

A Homeworlds turn can contain multiple steps. Catastrophes are free steps that may happen before the primary action,
so a manual catastrophe followed by a pass or action should be one SGF move node with multiple steps, not two
consecutive nodes by the same player.

The catastrophe buttons submitted a complete one-step move immediately. Because catastrophe-only moves do not finish
the turn, the next action appended a second SGF node by the same player. The fix stages manual catastrophes into the
Homeworlds move builder, updates the builder's working position, and appends the SGF node only when the turn later
completes.

## Homeworlds row layout ignored system widths

Rows with multiple Homeworlds systems placed centers at equal fractions of the row. That put two systems at one-third
and two-thirds even when one system was much wider than the other, so the empty spaces around and between systems were
visually uneven and could collapse when the row was crowded.

The fix measures each rendered system box first, distributes the remaining row width equally across the empty spaces
before, between, and after systems, and expands the board content width when a row plus the bank footprint cannot fit
in the current viewport. The Homeworlds board now lives in a scrolled window so expanded layouts get scrollbars.

The first scroller version let GTK collapse the board viewport because the scrolled window hid the drawing area's
natural width from the surrounding panes. Since the Homeworlds host also has its own side controls, the board itself
could start almost invisible. The next fix made the Homeworlds default board-panel width large enough for launch, but
the shared window used that default as the pane's minimum size, so the splitter could not move. The fix separates the
profile's startup/default board width from its minimum board width and keeps the Homeworlds scroller minimum small
enough that the pane remains resizable.

The pane was still capped at roughly the board height because the shared window had a square-board splitter guard that
limited the main split position to the paned height. That guard is now only applied to square-grid board profiles, so
Homeworlds can use the full available window width and leave the SGF drawer narrow when desired.
