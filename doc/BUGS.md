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

## Homeworlds good-move generation read staged candidates with the move accessor

The Homeworlds backend should walk staged `HomeworldsMoveCandidate` choices and only build `HomeworldsMove` values at
complete leaves.

The recursive good-move generator reused the backend move-list accessor on a candidate list. That accessor is sized for
`HomeworldsMove`, not `HomeworldsMoveCandidate`, so later candidate reads could use the wrong offset and skip or mangle
choices.

The fix reads candidate-list storage with the candidate element type directly. The Homeworlds move report test exercises
the resulting backend `good_moves()` walk from a playable position.

## Homeworlds sacrifice follow-ups still required local color access

After a sacrifice, choosing a ship for one of the granted actions should immediately execute that sacrificed color's
action or ask only for its target. The chosen ship does not need local access to that color.

The staged builder returned to the normal action-selection stage after each sacrifice ship choice, and the rules engine
used the ordinary action applicators that check local red/yellow/green/blue access. A green sacrifice could therefore
reject a build from a ship in a system with no green access, even though the sacrifice itself grants the green action.

The fix maps the sacrificed color to its action as soon as a follow-up ship is selected, skips the action picker, and
applies sacrifice-granted actions through a forced-action path that bypasses local color-access checks while preserving
all target, size, bank, and connectivity validation.

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

The fix represents catastrophes as normal symbolic `HomeworldsMove` steps such as `G3y!`. The view now submits that
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

After widening the board and then narrowing it again, the Homeworlds drawing area kept the old wide content width and
left empty scrollable space. The content width calculation had an unconditional fallback minimum, and viewport changes
were not guaranteed to emit a drawing-area resize. The fix uses the scrolled window adjustment page size as the
viewport, recalculates on adjustment changes, and shrinks the drawing area back to the viewport width whenever the row
contents fit.

## Homeworlds build moves duplicated same-color source ships

Building in Homeworlds is determined by the system and the color of an available source ship. The source ship size does
not change the built piece, because the action always takes the smallest available bank ship of that color.

Build moves were stored and formatted with the full source ship pyramid, so same-color ships produced redundant moves
such as `H1g1+` and `H1g3+` even though they applied the same build.

The fix canonicalizes build steps to store only the source system and build color. Build notation is now color-only,
for example `H1g+`, and generated move lists deduplicate those equivalent choices.

## Homeworlds AI could move into unfavorable catastrophes

The Homeworlds AI should avoid moves that immediately create a catastrophe where the moving player owns more ship pips
than the opponent.

The good-move filter rejected some unsafe builds but did not inspect move/discover destinations after the staged move
builder applied them. A ship could therefore move into a system where the next catastrophe would mostly destroy its own
side's material.

The fix checks every completed move/discover step against the post-step destination system and rejects AI candidate
moves that leave an unfavorable catastrophe there.

## Homeworlds AI considered redundant small sacrifices

A small sacrifice gives exactly one action. If the sacrificed ship's color action is already available in its system,
the sacrifice only spends a ship to do something that could already be done directly.

The good-move filter did not distinguish small sacrifices from larger sacrifices, so alpha-beta could consider lines
such as sacrificing a `g1` on a green-accessible system just to build once.

The fix rejects small-sacrifice candidates when the selected system already has access to the sacrificed ship's color.

## Homeworlds UI completed moves before optional catastrophes

After a primary action, any newly available catastrophe should still be triggerable as a free step in the same turn, or
the user should be able to pass on triggering it.

The Homeworlds view submitted a completed move as soon as the primary action finished. If that action created a
catastrophe, the SGF node was already recorded and the turn advanced, so the catastrophe could not be added to the same
move.

The fix keeps the staged move builder alive when the post-action working position has catastrophes. The board shows the
staged position, the catastrophe buttons append free steps to the same move, and an explicit pass button finalizes the
move without adding an SGF `pass` step.

## SGF disc buttons outlived their click-signal factory

SGF disc buttons should not keep raw signal user data pointing at a factory object that can be finalized before GTK has
finished disposing the buttons.

The disc factory connected each button's `clicked` handler with the factory as unowned user data. Some GTK test runs
entered the main loop after earlier SGF views had already released their factory, letting pending button/widget cleanup
process a signal closure with a stale object pointer and abort on a GLib critical.

The fix connects button handlers with a referenced factory and releases that reference when the signal closure is
destroyed, so the factory stays alive for as long as any generated button can use it.

## Homeworlds AI forced initial profitable catastrophes too early

If a profitable catastrophe is already available at the start of a Homeworlds turn, the AI should require the resulting
move to trigger it, but it does not have to be the first step of the move.

The good-move walker used one rule for every staged position: whenever a profitable catastrophe was available, it only
explored branches that triggered it immediately. That was still correct for catastrophes created during a multi-step
move, but it made beginning-of-turn catastrophes unnecessarily rigid.

The fix records the profitable catastrophes available at the root as a final-move requirement and adds those
catastrophes as optional branches during the staged walk. Newly created profitable catastrophes remain forced at the
earliest step.

## Homeworlds AI kept unsafe green sacrifices

A green sacrifice grants build actions, but those builds should still respect the AI safety rule against creating an
unfavorable catastrophe where the moving player would lose more ship pips than the opponent.

The good-move walker filtered ordinary unsafe builds and unsafe move/discover destinations, but a forced build produced
by a pending green sacrifice was applied while selecting the build source ship. That path bypassed the existing build
candidate filter, so alpha-beta could keep sacrifice lines that immediately left the player exposed to a losing
catastrophe.

The fix checks the staged child state after each pending green-sacrifice build and prunes that AI branch when the build
turns the target system from safe into an unfavorable catastrophe.

## Homeworlds action source ships lost their visual highlight

After a Homeworlds player selected a ship and then chose move, trade, or capture, the UI should keep the source ship
marked while the second-step destination, bank color, or target ship is being selected.

The board overlay only created highlight widgets for current clickable choices. Once the move builder advanced to a
trade color, capture target, or move destination stage, the source ship was no longer a candidate, so only the target
choices stayed visually marked.

The fix adds a non-interactive active-ship overlay for second-step action stages. Build still creates no active-ship
marker because it completes immediately without a second choice.

## Homeworlds sacrifice passes did not finish staged moves

A pass step after a sacrifice is the canonical way to skip all remaining sacrifice actions. The staged move builder
interpreted the same candidate as a top-level pass, leaving the pending action count unchanged. Recursive move reporting
could therefore never complete lines such as `H1g3- pass pass pass`.

The fix makes pass append every remaining sacrificed pass action whenever a sacrifice is active, and only treats pass
as a top-level move when no sacrifice actions remain.

## Homeworlds blue sacrifices generated chained recolors

A multi-action blue sacrifice can trade the same physical ship through multiple colors, such as `H1r2=g H1g2=y`.
That is legal but redundant, because it has the same final position as trading the ship directly to the final color and
passing the remaining sacrifice action.

The staged builder did not recognize that the later `g2` actor had just been created by the earlier trade, so move
generation explored these equivalent chained recolors.

The fix checks the already staged trade steps while listing and accepting blue-sacrifice ship choices. If a ship in the
same system matches a pyramid produced by an earlier trade in the move, that ship is skipped for the remaining forced
blue actions.

## Homeworlds good moves removed the only legal pass

The Homeworlds AI should avoid passing when it has any useful non-pass move, but a position can still leave pass as the
only legal move after the good-move filters remove dead-end or redundant actions.

`good_moves()` filtered pass before exploring the candidate tree and rejected completed moves containing a pass step.
That made such positions report no good moves at all.

The fix treats pass as a top-level fallback candidate. The good-move walker explores non-pass branches first; if none
append a good move before a primary action is staged, it explores pass and allows the resulting pass move through final
filtering.

## Homeworlds good moves let yellow sacrifices empty a homeworld

The Homeworlds AI should avoid moving or sacrificing the last ship at its own homeworld, because that immediately loses
the game. The good-move walker enforced that rule while choosing an ordinary top-level action, but a yellow sacrifice
skips that action-selection stage for its forced move actions. Those forced moves could therefore move the last
remaining homeworld ship away during the sacrifice.

The fix applies action safety to the actual step appended by the staged builder instead of only to ordinary action
candidates. Last-homeworld, redundant small-sacrifice, unsafe build-catastrophe, and unsafe move-destination checks now
run through the same child-state policy path for normal actions and sacrifice-granted actions.

## Homeworlds good moves kept redundant yellow sacrifice hops

A multi-action yellow sacrifice can move the same ship through intermediate systems. Some of those routes are legal but
do not improve the final position, such as moving a ship away and then back to its original system, or routing through
intermediate systems before ending at a destination the original source could already reach in one hop.

The good-move walker did not compare a yellow-sacrifice move chain against the ship's original source system, so
alpha-beta considered redundant lines like `H2b1>B3 B3b1>H2` and `H2b1>B3 B3b1>B1 B1b1>Y3` when the final `Y3`
destination was already reachable from the original `H2` system.

The fix tracks repeated movement of the same ship during a pending yellow sacrifice. When a later hop returns to the
origin or targets a system that was directly reachable from that origin at the start of the chain, `good_moves()`
prunes that branch.

## Homeworlds duplicate safety check made good-move generation quadratic

Homeworlds good-move generation should not produce duplicate symbolic moves. The safety check that guarded this
invariant compared each completed move against every previously collected move by formatting both to notation strings.
Large positions could therefore spend most of their time in duplicate detection instead of move generation.

The fix tracks structural move hashes in the good-move buffer. A duplicate now emits a warning with the root position
and duplicated move, but the move is still appended so the guard does not silently hide the generator bug. The duplicate
cases exposed while adding the guard were fixed in the generator: build actions now use one canonical same-color source
ship, and AI catastrophe insertion only happens at stable move boundaries.

## Homeworlds setup text changed the side-panel width

The Homeworlds text panel should keep one fixed width while setup choices advance. The panel requested a fixed content
width, but its child labels and side-panel buttons still contributed changing natural widths; after selecting the first
star, the setup prompt could widen the panel before later setup stages shrank it again.

The fix makes the text panel explicitly non-expanding, allows horizontal scrolling only as a containment fallback, and
constrains side-panel labels and buttons to wrap inside the fixed panel width. The fixed-width test now realizes the
view and checks the allocated panel width across the first setup choices.

## Homeworlds board kept fallback width at startup

The Homeworlds board content width should match the board viewport when the position fits. At startup, the board was
initialized before the scroller had a real allocation, so the drawing area could keep the fallback width and show excess
empty space until a later window resize forced a viewport-based recalculation.

The fix makes the board start with an intentionally tiny content width and avoids replacing it with the fallback width
before GTK has allocated the scroller. Once the scroller maps, the board schedules a frame tick that keeps
recalculating until the allocated scroller width, overlay width, and drawing-area content width agree, so startup
allocation is applied before user scrollbar interaction. A realized-window test now checks that the initial board
content width matches the startup viewport and that the overlaid bank starts inside that viewport.

## Homeworlds board settle tick could replace ship click targets

The Homeworlds board startup settle tick should stop once GTK has applied the real board allocation. In some real
windows, the visible resize could already be correct while the tick's strict allocation predicate still failed. The
tick then kept clearing and recreating the board-choice overlay every frame, so a highlighted ship button could be
replaced between pointer press and release and the click did nothing.

The fix makes board content-width updates report whether the width actually changed. The settle tick now rebuilds the
interactive board-choice overlay only after a real content-width change, its stop predicate accepts the allocated board
and overlay once they are no longer wider than the expected content width, and the root widget cancels any remaining
tick during destruction.

## Homeworlds board height stayed fixed instead of viewport-sized

The Homeworlds board content width already matched the viewport when rows fit and expanded only when a row needed more
horizontal space. Height still used a fixed drawing-area content height, so the board could be taller than the available
viewport even when the rows fit, or too short when several tall rows needed more vertical room.

The fix replaces the width-only content calculation with a single content-size calculation for both axes. It keeps the
viewport size when all visible row boxes fit, expands width for horizontally crowded rows, and expands height when row
boxes would clip or overlap vertically. The scroller now tracks both horizontal and vertical viewport adjustments.

## Homeworlds text panel could expose tiny horizontal scrolling

The Homeworlds text panel should have one fixed width and scroll only vertically. Its scrolled window had fixed content
width bounds, but the horizontal scrollbar policy still allowed automatic scrolling when labels or selectable text
reported a natural width a few pixels wider than the panel. Some panel messages also still used GTK's default label
sizing, so once horizontal scrolling was hidden they could be clipped instead of wrapping inside the panel.

The fix makes horizontal scrolling external, so no horizontal scrollbar is shown and horizontal content size no longer
drives the panel allocation. The panel also uses overlay scrolling and gives the inner text box a fixed width inside the
panel margins, so textual content and the vertical scrollbar cannot change the panel's horizontal behavior. Text panel
labels use GTK's word-based natural wrapping without a character-width minimum, and all catastrophe status text goes
through the same label helper. Vertical scrolling remains automatic.

## Homeworlds bank piles expanded after setup

The Homeworlds bank should use one compact layout throughout the game. During setup, every bank pile was selectable and
therefore used the custom `homeworlds-bank-choice` CSS, which removes GTK button padding. Once setup ended, ordinary
non-selectable piles fell back to GTK's `flat` button styling, so their natural size grew and the same grid appeared to
switch to a wider spacing mode.

The fix gives every bank pile the same `homeworlds-bank-pile` base style with zero padding and a stable transparent
border, then adds `homeworlds-bank-choice` only for the selectable outline. A realized-window regression now checks that
the bank frame does not grow after the six setup selections.

## SGF tree scroll could run before adjustment ranges settled

The SGF tree scroller retried when selected-disc geometry was not ready, but stopped retrying once it could compute
disc bounds. GTK can still report stale scroll adjustment ranges for a short time after rebuilding a large tree, so
`gtk_adjustment_clamp_page()` could run while the horizontal adjustment was effectively unscrollable and leave the
new selected disc offscreen.

The fix checks whether the requested disc range is actually visible after clamping. If it is not, the scroller schedules
another bounded idle retry, covering both geometry readiness and adjustment-range readiness without allowing an
unbounded retry loop.

## Generic model position replacement dereferenced null receivers

`ggame_model_set_position()` delegated to `ggame_model_set_position_variant()` but read `self->variant` before the
delegate could validate `self`. Invalid callers therefore crashed instead of getting the same `FALSE` result and GLib
precondition diagnostic as the variant-aware API.

The fix adds the missing receiver precondition before reading the current variant, and the model test covers the null
receiver path.

## Failed Homeworlds multi-step moves could leave partial mutations

`homeworlds_position_apply_move()` applied each turn step directly to the live position while validating a whole
multi-step move. If a later step then made the move invalid, such as a second primary action without a sacrifice, the
function returned `FALSE` after earlier steps had already changed the bank, ships, or systems.

The fix applies setup and turn moves to a working copy first. The original position is replaced only after the entire
move has validated and applied successfully, so rejected moves leave their input position unchanged.

## Failed Homeworlds move parses cleared or partially rewrote the caller output

`homeworlds_move_parse()` wrote directly into the caller's `HomeworldsMove` while trying setup and turn notation. A
failed parse could therefore clear a previous value or leave a partial turn move behind.

The fix parses into a local scratch move and copies it to the caller only after the whole notation string succeeds.

## Failed Homeworlds bank-ship lookup left stale output

`homeworlds_system_find_smallest_bank_ship()` reported `FALSE` when no ship of the requested color remained in the
bank, but it left `out_pyramid` unchanged. Callers that reused a variable could accidentally inspect a stale pyramid
after the failed lookup.

The fix clears `out_pyramid` to `0` before searching, so both success and failure return a deterministic value.

## Failed Homeworlds empty-system lookup left stale output

`homeworlds_position_find_empty_system()` returned `FALSE` when every non-homeworld system slot was occupied, but left
`out_system_index` unchanged. That made failure ambiguous for callers that reused an index variable.

The fix initializes `out_system_index` to `HOMEWORLDS_INVALID_INDEX` before searching, so a failed lookup has the same
sentinel convention as the rest of the Homeworlds code.

## Failed Homeworlds system-reference resolution left stale output

`homeworlds_position_resolve_system_ref()` returned `FALSE` for unresolved symbolic references but did not reset
`out_system_index`. A caller that inspected the index after failure could see a stale valid system index.

The fix initializes `out_system_index` to `HOMEWORLDS_INVALID_INDEX` before resolving the reference.

## Homeworlds bank layout test poisoned the next rendered window

The bank layout regression test only needed to prove that the bank kept its compact natural width after setup.

It presented and destroyed a throwaway `GtkWindow` just to read the bank's allocated width. The failure was not in the
next text-panel assertions: logs showed the next test aborted on its first main-context iteration, and gdb showed GTK
calling `g_signal_emit()` with a NULL instance from an internal render idle before the new Homeworlds view ran.

The fix measures the bank widget's natural width directly with `gtk_widget_measure()`. That keeps the test focused on
the compact layout contract and avoids leaving fragile renderer state behind for the following toplevel test.

## Homeworlds Build action was hidden for non-canonical same-color ships

The Homeworlds move builder should let the player choose any visible ship and then offer every legal action available
from that ship's system.

Build moves are stored as `system + color` rather than by physical source-ship slot, so the builder tried to avoid
duplicate symbolic build moves by exposing Build from only the first same-color ship in a system. That backend
deduplication leaked into the side panel: the move report could list `H1g+`, while selecting another green ship in the
same homeworld showed the other actions and `Cancel` but no `Build`.

The fix offers Build from every selected ship whose system has green access, and keeps duplicate symbolic build moves
out of backend good-move output by deduplicating the completed move buffer.

## Homeworlds board kept empty rows between connected systems

The Homeworlds board should use empty vertical rows to show disconnected neighboring row groups, not merely because an
intermediate star-size row is absent.

A top-row system such as `Y1` and a bottom-row system such as `B3` are directly connected, so reserving the absent
middle row left unnecessary space between them. The text formatter already skipped blank rows for connected groups, but
the board used fixed fractions for player 2, top, middle, bottom, and player 1 rows.

The fix assigns board-row fractions from the visible row groups and their connectivity. Connected neighboring groups
use adjacent spacing, while disconnected groups reserve an extra empty slot; a real middle-row system still gets its own
row.

## Homeworlds move report compared moves by formatting them

The Homeworlds move report should compare generated moves structurally. It deduplicated candidates by formatting both
moves into notation strings and comparing the text, even though the backend already had a structural comparator for
move identity.

That made report generation spend most of its time in move formatting when many generated moves collapsed to the same
symbolic notation. The fix moves the structural comparator into the Homeworlds core API and lets both the backend and
move report use it directly.

## Homeworlds move report subtracted good moves with a nested scan

The Homeworlds move report should use the same structural move identity for both deduplicating all generated moves and
subtracting `good_moves()` from the remaining legal moves.

After moving all-move generation into the core, the report still rendered "all possible moves minus good_moves()" by
checking every all-move against every good move. That left report formatting quadratic in the two move-list sizes.

The fix builds one temporary structural hash set from `good_moves()` and uses it while rendering the remaining legal
moves, so the report keeps stable ordering without the nested scan.

## Failed Homeworlds turn steps could partially mutate a position

Homeworlds turn-step application should either apply a whole symbolic step or leave the position unchanged.

Some step helpers changed the board before their final failure point. For example, building in a full system removed
the bank ship and then failed when the destination had no free ship slot.

The fix applies each public turn step to a scratch position first and copies it back only after the step succeeds.

## Homeworlds move application trusted overlong turn step counts

Homeworlds moves store at most `HOMEWORLDS_MAX_MOVE_STEPS` turn steps. `homeworlds_position_apply_move()` rejected
empty turn moves, but it did not reject a malformed move whose `step_count` was larger than the fixed step array.

That could make the validation loop read beyond the move's `steps` storage. The fix rejects overlong step counts before
iterating over the array.

## Some Homeworlds candidate lists ignored append failures

Homeworlds move-builder candidate lists should either contain every legal choice for that builder stage or fail
cleanly.

The trade-color, attack-target, and move-target list builders appended to the candidate buffer without checking the
append result. If allocation failed while growing the buffer, the builder could return a truncated list and hide legal
choices.

The fix routes those paths through the same checked append-and-abort behavior used by the other candidate lists.

## SGF loading accepted trailing content after the game tree

SGF loading should reject non-whitespace data after the parsed game tree. `sgf_io_load_data()` delegated to the tree
parser and accepted the result as soon as one tree was parsed, without checking whether the input cursor had reached
the end of the data.

That allowed malformed files such as a valid SGF followed by arbitrary bytes to load silently. The fix skips trailing
whitespace after the tree and reports an SGF parse error for any remaining content.

## Failed SGF move parsing cleared caller output

SGF move parsing should not mutate caller-owned move storage unless the notation parses successfully.
`sgf_move_props_parse_notation()` cleared the output buffer before delegating to the active backend parser, so a bad
move string could erase the caller's previous move value even though the function returned failure.

The fix parses into scratch storage and copies the typed move back only after the backend accepts the notation.

## SGF analysis move text could be truncated while saving

Analysis move entries are saved as `GCAN[move:score:nodes]`, and the move text comes from the analysis report rather
than a fixed-size backend move buffer. `sgf_io_sync_analysis_properties()` formatted each entry into a 192-byte stack
buffer, so longer move labels were silently truncated in saved SGF data.

The fix formats each `GCAN` value dynamically, preserving the full analysis move text through save/load roundtrips.

## SGF analysis counters accepted negative text

SGF analysis counters such as `GCAS[nodes=...]` are unsigned values. The parser delegated to `g_ascii_strtoull()`
without first rejecting signed text, so values like `nodes=-1` could be accepted as unsigned data depending on the C
library conversion behavior.

The fix requires unsigned SGF numeric fields to start with a digit and rejects range errors from the conversion.

## Board selection trusted overlong backend paths

The shared board selection controller stores visible click paths in a fixed-size array. It passed that capacity to
backend path callbacks, but it still trusted the returned length when copying paths or reading the next selected index.

A backend that returned a length larger than the provided capacity could make the controller read or write past its
scratch path arrays. The fix validates every backend-reported selection path length before copying or indexing it and
clears the current builder after an unexpected failed click.

## Homeworlds failed ship lookup left stale outputs

Homeworlds symbolic ship references are resolved into concrete system and ship-slot indexes. The system-reference
lookup already wrote `HOMEWORLDS_INVALID_INDEX` on failure, but ship lookup helpers returned failure without clearing
their output indexes or pyramid value.

That made callers depend on separately initialized locals to avoid stale data after a failed lookup. The fix writes the
invalid index sentinels and clears the output pyramid before attempting ship resolution.

## SGF analysis accepted empty move labels

SGF analysis move entries are stored as `GCAN[move:score:nodes]`. The parser rejected missing separators, but it did
not reject an empty `move` portion when the value started with a colon, such as `GCAN[:12:345]`.

The fix rejects empty analysis move text before accepting the scored move entry.

## BGA response buffering trusted unchecked curl chunk sizes

The BoardGameArena client appends response data from libcurl using the callback's `size * nmemb` byte count. That
product was computed directly and then narrowed to `gssize` for `g_string_append_len()`, so a malformed or impossible
callback size could wrap before the append.

The fix checks the multiplication and the `gssize` range before appending response bytes.

## Puzzle upload response discard trusted unchecked curl chunk sizes

The application discards puzzle-progress upload response bodies through a libcurl write callback, but it still returns
`size * nmemb` to tell libcurl how many bytes were consumed. That product was returned directly, so an impossible
callback size could wrap and report the wrong byte count.

The fix validates the multiplication before returning the consumed byte count.

## SGF analysis stats accepted empty fields

Analysis stats are stored as semicolon-separated `key=value` fields in `GCAS[...]`. The parser skipped empty fields,
so malformed data such as `GCAS[nodes=1;]` loaded as if the trailing separator was not present.

The fix rejects empty `GCAS` fields instead of silently ignoring them.

## Homeworlds move report froze the UI when it contained tens of thousands of lines

The Homeworlds move report should remain scrollable and selectable even when a position has a very large number of
diagnostic legal moves.

The side panel rendered the whole report as one selectable, wrapped `GtkLabel`. Large reports could reach tens of
thousands of lines, and GTK/Pango repeatedly measured that single wrapped label during layout. In practice the main
thread could spend all its time scanning the report text and stop responding.

The fix replaces the report label with a read-only, wrapped `GtkTextView` backed by a `GtkTextBuffer`, so the report is
handled by GTK's text widget instead of label measurement.

## Homeworlds catastrophes collapsed binary-star systems after one star was destroyed

When a catastrophe removed a star, Homeworlds returned all remaining ships in that system to the bank immediately. That
treated a binary-star homeworld like a destroyed star system even when its second star survived.

The fix only returns all remaining ships when the system has no stars left after the catastrophe color is removed. A
binary-star system can now survive losing one star, and the rules text says ships are returned when a star system is
destroyed rather than when any one star is destroyed.

## Homeworlds depth-0 AI generated child move lists only to detect forced moves

Depth-0 AI should score each candidate move by applying it once and statically evaluating the child position for games
that do not have checkers-style forced continuations.

The generic search treated every backend as if a single legal move should extend the search without consuming depth.
To detect that, it generated the child position's legal moves before static evaluation even when `depth_remaining == 0`.
For Homeworlds, choosing a move from a position with tens of thousands of good moves therefore generated another good
move list for each child position and could block the UI for minutes.

The fix makes forced-ply extension an explicit backend flag. Checkers opts in, while boop and Homeworlds leave it off,
so their depth-0 child positions are statically evaluated immediately after terminal-outcome checks.

## Settings dialog close left a hidden window pending destruction

The settings dialog should disappear from GTK's toplevel window list as soon as the user saves or cancels it.

The close path hid the dialog first and scheduled the actual destroy on a low-priority idle. Tests and users could open
another settings dialog while the hidden one still existed, and order-sensitive GTK teardown could later trip over that
stale toplevel.

The fix destroys the modal directly from the save and cancel handlers, matching the simpler dialog lifecycle used by
the other shared dialogs.

## BoardGameArena integer parsing narrowed oversized values

BoardGameArena login responses include integer fields such as `status`. The parser extracted decimal JSON values into
a 64-bit integer and then narrowed them to `int` without checking the target range.

Malformed or unexpected oversized values could wrap before the login-response state machine inspected them. The fix
rejects parse failures, range errors, and values outside the local `int` range before assignment.

## BoardGameArena history kept whitespace around player names

BoardGameArena history entries provide both player names in one comma-separated JSON field. The importer split that
field into two names but kept any surrounding whitespace from the response.

That made formatted history rows depend on response spacing. The fix strips leading and trailing whitespace from both
names after splitting the field.

## Puzzle progress could write JSON strings it could not read

Puzzle progress history is stored as JSON lines. The writer escaped control characters as `\u00xx`, but the custom
reader rejected all `\u` escapes.

Any stored string containing one of those escaped control characters made the history unreadable. The fix decodes
four-digit JSON unicode escapes and rejects invalid code points.

## Puzzle progress accepted raw control characters in JSON strings

Puzzle progress history is JSON lines, so strings must not contain unescaped control characters.

The custom JSON reader accepted raw control characters in strings even though the writer would always escape them.
The fix rejects malformed raw control characters while still accepting the writer's escaped form.

## Puzzle progress accepted oversized JSON numbers

Puzzle progress history stores timestamps and counters as JSON numbers. The reader used `g_ascii_strtoll()` but did not
check for range errors.

Oversized values could therefore be clamped by the C library and accepted as real history data. The fix rejects numeric
conversion range errors while loading history.

## Homeworlds good moves kept equivalent sacrifice follow-ups

Large green sacrifices can generate equivalent build permutations, such as building twice at one system and once at
another system in different adjacent orders. Those permutations should not all be searched when the bank supply makes
the swapped order reach the same final position.

The good-move filter only had a commutation proof for blue-sacrifice trades. The fix generalizes that proof to green
builds by reversing the previous build, trying the swapped order, and pruning only when the swap is legal,
position-equivalent, and catastrophe-free. Yellow-sacrifice route tests also cover the existing rule that repeated
moves of the same ship do not keep an irrelevant intermediate system when the original source could have reached the
final system directly.

## Homeworlds green-build commutation stopped at the first reversible predecessor

The green-sacrifice build commutation proof reconstructs the position before the previous build, then tries the swapped
order. Reversing a build can be ambiguous when the built system already had smaller same-color ships: removing one of
those pre-existing ships and reapplying the build can recreate the same intermediate position even though it is not the
real predecessor needed for the swap proof.

The proof stopped after the first reversible build predecessor. That could leave genuinely equivalent build
permutations in `good_moves()`. The fix tries every reversible built-ship candidate and accepts the commutation if any
candidate makes the swapped order legal and position-equivalent. A regression also records that the saved position
containing `H1g3- H1g+ H1g+ B2g+` and `H1g3- H1g+ B2g+ H1g+` is bank-dependent: those two moves are both kept because
they put the returned `g3` at different systems.

## Generic AI search stored a best-move pointer after freeing its move list

The generic alpha-beta search remembered the current best move as a pointer returned by `move_list_get()`. That pointer
belongs to the backend move list and is only valid until `move_list_free()`.

When the search stored a transposition-table entry, it freed the move list first and then asked the TT to copy the best
move. Homeworlds depth-2 analysis could therefore read freed move-list memory and crash while profiling
`game-homeworlds.sgf` after 19 moves. The fix stores the TT entry before releasing the move list, and the regression
uses a test backend that poisons retired move lists to prove the copied best move was taken while still valid.

## Homeworlds asked for pass after a terminal homeworld catastrophe

When a catastrophe destroyed the last ship at a homeworld during a staged turn, the move builder still followed the
normal sacrifice/pass completion path. A player could therefore be asked to add pass steps even though the game was
already decided by the destroyed homeworld.

The builder now records the homeworld ship counts from the start of the move. If a staged action or catastrophe reduces
either side from a non-empty homeworld to no homeworld ships, the staged move completes immediately. The move validator
accepts that same early terminal condition, including during a sacrifice with unused actions remaining.

## Generic AI search leaked partial analysis moves on cancellation

Cancellable root-move analysis should leave no owned move copies behind when the caller cancels partway through the
root move loop.

The analysis code copied each scored root move into a result array as it went, but the cancellation path freed only the
array itself. Any copied move payloads already stored in the array leaked.

The fix routes both successful result cleanup and failed partial-result cleanup through the same helper that frees each
copied move before freeing the array. A regression cancels after one root move has been scored and verifies that no
partial result list is returned.

## Homeworlds profiling leaked an empty random-move candidate list

The profiling CLI should release the move list returned by the backend even when random source-game generation cannot
continue.

The random-generation path handled the normal success path and the oversized-list path, but the `moves.count == 0`
error path printed an error and returned without calling the backend move-list free callback. That could leak an owned
empty candidate list if a backend returned one.

The fix calls the Homeworlds backend move-list cleanup before returning from the empty-list path.

## Homeworlds SGF snapshots accepted duplicate system entries

A Homeworlds SGF position snapshot should contain at most one `GHS` value for each system index.

The parser applied each `GHS` value directly into the parsed position. If a node contained two values for the same
system index, the later value silently overwrote the earlier one instead of treating the snapshot as malformed.

The fix records which system indices have already been seen while parsing a snapshot and rejects duplicate entries. A
regression writes a valid snapshot, duplicates one system property, and verifies that loading it fails.

## Homeworlds SGF snapshots accepted ships without a star

Loaded Homeworlds snapshots should never contain ships in a system that has no star. Such a system is not a legal
star system and cannot be produced by the writer.

The parser accepted that shape and rebuilt cached color counts for it, leaving later rules code to work with an
impossible position. The fix rejects starless non-empty systems while parsing each `GHS` value.

## Homeworlds SGF snapshots accepted loose numeric fields

Homeworlds snapshot numbers should be written and read as plain unsigned decimal digits. The loader used
`g_ascii_strtoull()` directly, which accepts leading ASCII whitespace and signs.

That meant malformed properties such as a turn value with a leading space could be accepted as valid state. The fix
requires every character in a numeric snapshot field to be a digit before converting it.

## Homeworlds SGF snapshots accepted impossible pyramid supplies

A Homeworlds position must account for exactly three copies of each pyramid type across the bank and every star system.

The snapshot loader parsed the bank and systems independently, then trusted the result. A malformed snapshot could keep
all pieces in the bank while also placing those same pieces on the board. The fix validates the full pyramid supply
before installing the loaded position.

## Homeworlds SGF snapshots accepted orphaned stars

Star systems without ships are immediately destroyed by the Homeworlds rules and should not appear in saved position
checkpoints.

The loader already rejected ships without a star, but it still accepted the opposite invalid shape: a system with a
star and no ships. The fix treats only empty systems and systems with both a star and at least one ship as valid
snapshots.

## SGF loading accepted empty variations

An SGF variation must contain at least one node. The parser accepted `()` after a valid node and discarded it, which
meant malformed tree structure could disappear silently during load/save roundtrips.

The parser now rejects any SGF tree or nested variation that closes before a node sequence has been read.

## SGF loading accepted repeated move-property values

`B[]` and `W[]` are single-value move properties. The loader accepted nodes such as `B[12-16][11-15]`, built the tree
from the first move, and then copied both values onto the node.

That left later replay code with ambiguous move data. The parser now rejects move properties with more than one value.

## SGF root loading skipped move-property validation

The SGF loader validated child nodes so they could not contain both `B[]` and `W[]`, but root-node properties used a
separate path that copied properties directly.

A malformed root such as `B[12-16]W[23-18]` could therefore load even though no later replay path can treat it
unambiguously. The fix shares the move-property validation between root and child nodes.

## Homeworlds wins with the new score scale were not shown as win distances

Homeworlds terminal positions should still render as compact `W#X` / `B#X` analysis scores after lowering the win
score from 3000 to 1000.

The generic analysis score formatter only recognized terminal score bands around 3000 and 100000, so Homeworlds
scores such as 997 were shown as regular numeric evaluations instead of `W#3`.

The fix teaches the formatter about all current terminal score scales and keeps tests for the Homeworlds, checkers, and
Boop bands.

## Homeworlds did not persist shared shell settings

SGF file dialogs should reopen in the last SGF folder, and top-level windows should restore their last size, for every
shared-shell game.

The SGF folder history helper used the active profile's app-specific settings schema. Checkers and boop had schemas,
but Homeworlds intentionally did not, so `ghomeworlds` skipped folder history entirely. Window dimensions were not
persisted anywhere, so they had the same cross-game wiring problem waiting to happen.

The fix adds one relocatable `io.github.jeromea.ggame` schema for shared shell settings. File-dialog history and window
size now use that common schema under a per-profile path, so new games inherit the behavior without copying settings
keys into another app schema.

## Restored window sizes were overwritten during initial layout

Shared-shell windows loaded their saved default size from common GSettings, but the first drawer layout sync captured
panel geometry before the window had an allocation. That reset the saved extra width to zero and reapplied the hardcoded
layout width/height, so a newly opened window could ignore the stored size.

The fix derives the saved extra width from the loaded default size, preserves the loaded default height until the window
has an allocation, and applies the initial drawer layout without capturing transient geometry.

## Homeworlds good moves allowed unsafe trade catastrophes

Homeworlds `good_moves()` filtered builds that create an unfavorable catastrophe and moves that enter one, but a trade
could still convert a ship into the fourth piece of an owned color at the same system. In the reported game, Player 2
could trade `H2g2=y` while already having two yellow ships and a yellow star at `H2`, creating an immediate yellow
catastrophe that mostly destroyed Player 2's own material.

The fix applies the same unfavorable-catastrophe check to trade steps after the staged builder applies them.

## Current-position analysis status duplicated the move report

The analysis drawer has two separate text surfaces: the status label should show transient progress such as depth and
node count, while the report view should show the selected node's saved move scores.

Current-position analysis reused the full report formatter for the status label after each completed depth, and its
in-progress formatter also copied the last completed scored moves into the status text. That made the status area show
the same move list that belongs in the report below it.

The fix adds a status-only formatter for completed current-position analysis, removes scored moves from progress
updates, and leaves move-score formatting exclusively in the report view.

## Homeworlds board labels did not match move notation

The Homeworlds move notation and ASCII position text refer to homeworlds as `H1`/`H2` and non-homeworld systems as
`S0`, `S1`, and so on, where `S0` is internal system slot 2.

The board and side-panel labels still displayed raw internal indexes, such as `System 2` or `system 2`, which made the
same system appear under different names in the UI and move text.

The fix shares one view-local formatter for visible system labels, so board labels, move-target labels, ship-selection
labels, and catastrophe buttons all use the same notation-facing names.

## Homeworlds good moves kept redundant green medium sacrifices

Homeworlds `good_moves()` already skipped one-action sacrifices when the sacrificed color was available at the source
system, but it still kept a two-action green sacrifice that built green again at the sacrificed `g2`'s system and then
performed another build that was already legal before the sacrifice.

The fix recognizes that exact two-build pattern and keeps the direct build instead of the redundant sacrifice sequence.

## BoardGameArena archive log import missed the review-page session step

The import wizard fetched `archive/archive/logs.html` directly after the history list, so BoardGameArena could return
`Invalid session information for this action` even though the login and history fetch had succeeded.

The fix mimics the browser flow for a selected history row: open `gamereview?table=...` in the same cookie session,
refresh the request token when the page exposes one, then fetch the archive logs as an XHR with the game-review page as
referer.

## BoardGameArena archive import assumed old archive logs were already materialized

BGA archive imports should work for history rows whose log files have not recently been opened in a browser.

The client opened the review page and then fetched `archive/archive/logs.html` directly. For older archived tables, BGA
can return `Cannot find gamenotifs log file of an archived table` until the game-review page has requested archive
materialization. Loading the same table in a browser first made the bug disappear because the browser performed that
extra request.

The fix mirrors the browser sequence: open the table page, refresh the table and game-review templates as XHRs, detect
the `Searching for the game archive` waiting message, call `requestTableArchive.html` when needed, and retry the logs
fetch if BGA still reports the missing archive file.

## BoardGameArena history import only showed the first page

BGA history responses contain a limited `tables` page and a total game count under `stats.general.played`. The import
wizard fetched only the first `getGames.html` response, so older imported games beyond the first page were not shown in
the history list and could not be selected.

The fix walks subsequent `getGames.html?page=N` pages with `updateStats=0`, deduplicating table ids and treating an
empty `tables` page as the normal end of pagination. History rows are cached per profile. Refreshes stop when they hit
a cached table id or the 10-page batch limit, and the history page exposes `More...` to fetch the next 10-page batch.

## BoardGameArena imported games lost their start time in SGF metadata

BGA history rows include start timestamps, but imported SGFs stored only the calendar date in `DT`. The game
information dialog and library therefore could not show or edit the precise imported start time.

The fix writes imported `DT` metadata as `YYYY-MM-DD HH:MM` in UTC, keeps the full value editable in the game
information dialog, and shows the full date/time value in the library.

## Drawer width restoration clamped the main paned handle

The main board/drawer split could not be dragged far enough to the right even when the navigation and analysis panels
needed only a small minimum width. The layout debug log proved the clamp: `main_paned` had width `1766`, but its end
child reported `end_min=884` and `max_position_from_end_min=882`, matching the observed stop around position `881`.
That `884` did not come from the SGF or analysis content, which measured as `133` and `116`; it came from an explicit
`drawer_host request=884x-1` set while restoring saved panel widths.

The fix keeps restoring preferred widths through the paned positions and window default size, but no longer turns the
saved drawer width into a hard minimum size request on `drawer_host` or `drawer_split`.

## GTK window tests used incomplete main-context drains

Window tests should wait for GTK to present and draw widgets through GTK's own test helper, and they should not rely on
showing and tearing down multiple toplevels in one process when a single window interaction can exercise the behavior.

Several tests simulated application progress by repeatedly calling `g_main_context_iteration()`. That processed some
pending work, but it was not equivalent to running a GTK main loop through a draw cycle, and repeated show/destroy
sequences could still hit GTK critical warnings while hidden toplevel cleanup was pending.

The fix replaces those ad hoc drains with `gtk_test_widget_wait_for_draw()` and runs window-heavy test binaries one test
path per process from `make test`. The harness now finishes collecting a binary's test list before it starts the first
isolated path, so the listing process cannot overlap another GTK process using the same app. Tests that only needed
persisted or runtime puzzle state now start fixture puzzles directly by SGF path instead of opening the puzzle chooser;
chooser tests still cover chooser-specific behavior. The temporary `bug.c` repro target was removed after confirming
the underlying GTK issue was reported.

## Wrong puzzle move rollback kept the window alive after cleanup

The puzzle wrong-move feedback schedules a short timeout to restore the puzzle position after showing the wrong move.
That timeout held a reference to the window, but the source did not have a destroy notifier. If the window was closed or
a test released its last explicit reference before the timeout fired, removing the source leaked the timeout's window
reference and let the partially torn-down window survive into later GTK work.

The fix gives the timeout source a `g_object_unref` destroy notifier and lets GLib release the reference when the source
fires or is removed.

## Puzzle picker scrolled before GTK had allocated the grid

Opening the puzzle chooser should scroll a large numbered catalog to the first untried puzzle, even when most earlier
entries are already solved.

The picker scheduled one idle scroll immediately after rebuilding the grid. On some runs GTK had not yet allocated a
scrollable adjustment range, so the requested row clamped to zero and the dialog stayed at the top. Large catalogs could
also make the dialog grow instead of forcing the picker area to scroll.

The fix caps the picker scroller's natural height and retries the initial scroll until the adjustment reports a real
scroll range.

## Import cached-history test repeated GTK draw waits

`gtk_test_widget_wait_for_draw()` can hit a GTK critical when a test calls it repeatedly while toplevel teardown is
still being processed.

The import cached-history window test waited for a parent-window draw after opening the import dialog, after selecting a
history row, and again after closing the dialog. The test only needed the first draw to present the main window. The fix
waits for the import dialog to appear or disappear by running the main context against explicit toplevel predicates, and
relies on the synchronous row-selection callback for the import button sensitivity check. The BGA import cache directory
is also set once before GTK initialization, instead of changing `GCHECKERS_BGA_IMPORT_DIR` inside GTK tests.

## Import dialog close emitted child updates during teardown

Closing the import wizard from the cached-history step could emit a late `row-selected` signal while GTK was already
destroying the dialog's child widgets.

That signal re-entered the import step update path with some child widget pointers no longer valid, which could trigger
a critical assertion on the history step buttons. The fix disconnects the import dialog's child widget handlers before
destroying the toplevel from Cancel, cached Load, or imported Load paths.

## Homeworlds good-move pruning stored every generated complete move before trimming

Homeworlds good-move generation should cap the static-pruned move list without requiring memory proportional to every
complete move produced by the recursive builder walk.

The backend accumulated every unique complete move in one growable array and hash table, then allocated a second scored
array, sorted all generated moves, and kept only the best 512 inside the static score window. Some positions generated
millions of complete moves before pruning, so a depth-1 evaluation experiment could spike to multiple GiB of RSS and be
killed by the kernel OOM killer.

The fix scores complete moves as they are produced for play positions and keeps only a sorted top-512 scored set plus
deduplication keys for the currently kept moves. The returned good-move list preserves the same score ordering and
window pruning, but peak memory no longer scales with the number of generated complete leaves.

## Homeworlds SGF child analysis reuse missed deduped equivalent moves

Root analysis should reuse a direct SGF child's stored analysis when the generated root move reaches the same child
position as that SGF move.

The reuse lookup compared the generated candidate with the SGF child using strict backend move equality. Homeworlds
`good_moves()` deduplicates by generated state and may return only a canonical representative, so an SGF child with a
different but equivalent step ordering was ignored and searched again.

The fix adds a required backend move-equivalence callback and uses it for child-score reuse. Most backends define
equivalence as strict equality, while Homeworlds compares the positions reached after applying both moves from the same
parent position.

## Homeworlds equivalent-analysis window test used an invalid SGF snapshot

The Homeworlds window regression for reusing equivalent SGF child analysis should replay its hand-built root snapshot
through the same SGF path as a real loaded game.

The test replaced the prepared homeworld ship list to create a blue-sacrifice position, but it did not return the
removed ship to the bank or remove the newly added ships from the bank. When GTK tests ran on the local display, the
controller replayed the root snapshot and rejected it because the serialized pyramid supply was impossible.

The fix updates the test fixture's bank whenever it rewrites those ships, so the saved SGF snapshot remains a legal
Homeworlds position.

## Library load test waited for a parent draw during dialog teardown

Loading an imported game from the Library dialog closes the Library toplevel before the test inspects the loaded SGF.

The window test waited for a parent-window draw immediately after clicking Load. When GTK tests ran on the local
display, that draw wait could run while the child toplevel was being torn down and hit a NULL-instance GTK critical.

The fix waits for the Library toplevel to disappear instead and releases the test's dialog reference after the close.
