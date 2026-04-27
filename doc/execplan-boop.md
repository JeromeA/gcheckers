# Add boop as a compiled square-grid game

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document must be maintained in accordance with `doc/PLANS.md`.

## Purpose / Big Picture

After this change, a developer will be able to build a second square-grid application from this repository with
`GAME=boop`, producing a `gboop` binary. A user will be able to start a local boop game, place kittens and cats on a
6x6 board, see adjacent pieces get booped according to the rules in `src/games/boop/RULES.md`, choose promotion
outcomes when the rules require player input, play against another human or the existing computer-player shell, and win
by either forming three cats in a row or promoting all eight kittens.

The first shipped boop version in this plan is intentionally narrower than the checkers build. It must support local
play, replay/navigation through the existing SGF tree, and computer play through the shared search layer. It does not
need puzzle mode, puzzle generation, arbitrary setup editing, or checkers-specific import/export semantics. The visible
proof is that `GAME=boop make` builds `build/bin/gboop`, the program starts a playable boop window, backend-driven
staged move selection works for both checkers and boop, and focused boop tests pass.

## Progress

- [x] (2026-04-27 00:00Z) Read `doc/PLANS.md`, `doc/OVERVIEW.md`, existing backend ExecPlans, the current shared
      backend/UI/SGF seams, and `src/games/boop/RULES.md`, then write the initial ExecPlan.
- [x] (2026-04-27 00:30Z) Update the plan after the rules file started spelling out previously ambiguous boop
      resolution behavior directly.
- [x] (2026-04-27 00:45Z) Revise the plan to remove the now-obsolete local rule clarifications and align it with the
      updated end-of-turn win rule in `src/games/boop/RULES.md`.
- [x] (2026-04-27 01:20Z) Complete Milestone 1 by adding `GAME=boop` build selection, a stub boop backend and game
      module, a branded `gboop` skeleton binary, boop metadata files, and boop-aware backend/model test coverage.
- [ ] Migrate square-grid interaction from path-only move picking to backend-driven staged selection, and port
      checkers to that model first.
- [ ] Implement the boop engine and backend under `src/games/boop/` using the same staged-selection interface.
- [ ] Generalize backend-owned SGF move notation so boop turns can be saved and replayed through the shared SGF tree.
- [ ] Integrate boop into the shared application/window flows while disabling unsupported checkers-only features.
- [ ] Add boop engine/backend/UI regression tests and update `doc/OVERVIEW.md` as `src/` changes land.

## Surprises & Discoveries

- Observation: `src/games/boop/` currently contains only prose rules, not an engine skeleton, backend adapter, or
  tests.
  Evidence: `find src/games/boop -maxdepth 3 -type f` returns only `src/games/boop/RULES.md`.

- Observation: the shared square-grid click path is fundamentally path-based, while both Homeworlds and boop need
  backend-owned staged selection.
  Evidence: `src/board_selection_controller.c` only knows how to grow a selected square path against
  `square_grid_move_has_prefix`, while `src/games/homeworlds/homeworlds_move_builder.c` already models interaction as
  repeated `list_candidates` plus `step` over explicit builder stages.

- Observation: SGF move parsing and formatting are still hard-coded to `CheckersMove`.
  Evidence: `src/sgf_move_props.c` includes `games/checkers/game.h`, requires at least two numeric squares, and writes
  checkers-only notation such as `12-16` or `23x18`.

- Observation: the shared application shell still has several checkers-owned types and flows in its construction path.
  Evidence: `src/application.c` includes `games/checkers/checkers_model.h`, and `src/window.c` still stores
  `GCheckersModel`, `PlayerRuleset`, `CheckersColor`, and checkers puzzle state directly on `GGameWindow`.

- Observation: boop can reuse the existing square-grid board renderer even though it cannot reuse the existing move
  selection semantics unchanged.
  Evidence: `src/board_view.c` already paints arbitrary square-grid boards through `GameBackendSquarePieceView`, but
  it relies on `BoardSelectionController` for primary-click move selection.

## Decision Log

- Decision: the initial boop backend will expose no variants.
  Rationale: `src/games/boop/RULES.md` describes one fixed 6x6 ruleset with one starting supply and two win
  conditions. Adding a variant list would create boilerplate without any user-visible benefit.
  Date/Author: 2026-04-27 / Codex

- Decision: square-grid interaction will be migrated to the same backend-driven staged-selection model that
  Homeworlds is preparing to use, and checkers will be ported to it before boop depends on it.
  Rationale: boop is not just a single-click placement game; it also has turns that require extra player choices, such
  as selecting which three kittens to promote when more than one alignment is available. A generalized builder-style
  flow solves both checkers and boop interaction cleanly and avoids introducing another square-grid special case.
  Date/Author: 2026-04-27 / Codex

- Decision: the first boop UI will render kittens and cats with backend-supplied board symbols instead of new custom
  artwork.
  Rationale: `src/board_square.c` and `src/piece_palette.c` already support symbol-only pieces. Reusing that path
  keeps the feature focused on gameplay, replay, and AI instead of on new paintables. If the user wants custom boop
  artwork later, that can be a follow-up change.
  Date/Author: 2026-04-27 / Codex

- Decision: the implementation will generalize SGF move parsing/formatting behind backend callbacks instead of adding a
  second boop-only SGF path.
  Rationale: replay, save, import, and analysis already pass through `sgf_move_props.c`, `sgf_controller.c`, and
  `sgf_io.c`. A backend-owned notation hook fixes the root architectural problem once and lets checkers and boop share
  the same tree/timeline code.
  Date/Author: 2026-04-27 / Codex

- Decision: puzzle mode, puzzle generation, and arbitrary setup editing are explicitly out of scope for the first boop
  milestone and must be disabled in boop builds.
  Rationale: those flows are still deeply checkers-specific today and are not required to demonstrate that boop is
  supported as a playable game. Shipping a smaller but coherent boop build is better than blocking on puzzle
  generalization.
  Date/Author: 2026-04-27 / Codex

- Decision: boop win detection will follow the updated end-of-turn rule literally: the active player wins if their turn
  creates either winning condition, even if the opponent also has a winning position afterward.
  Rationale: `src/games/boop/RULES.md` now states this explicitly, so the implementation should encode that rule
  directly instead of inventing draw behavior for simultaneous-looking positions.
  Date/Author: 2026-04-27 / Codex

## Outcomes & Retrospective

Milestone 1 is complete. The repository now accepts `GAME=boop`, builds `build/bin/gboop`, archives a boop-flavored
`build/lib/libgame.a`, and selects a stub boop backend through `src/active_game_backend.h`. The new boop code is still
only a scaffold: `src/games/boop/boop_game.c` and `src/games/boop/boop_backend.c` implement a tiny placeholder
position plus one-step builder so the generic backend/model tests can compile and run, and `src/gboop.c` opens a
branded GTK placeholder window instead of the full shared application shell. The next major implementation risk is no
longer build selection; it is the amount of remaining checkers-specific code in SGF, board input, window, and
application modules. The next milestone therefore remains the staged-selection migration for square-grid games.

## Context and Orientation

This repository is a multi-game GTK codebase selected at compile time by `GAME` in the top-level `Makefile`. The
active backend is exposed through `src/active_game_backend.h`, and shared code interacts with it through the callback
table in `src/game_backend.h`. The active backend owns position storage, move storage, move generation, move-builder
state, search callbacks, side labels, and square-grid board mapping.

The boop rules source is `src/games/boop/RULES.md`. In plain language, boop is a two-player game on a 6x6 empty
board. Each player starts with eight kittens in supply and no cats. A turn places one piece from supply on an empty
square. The newly placed piece pushes each adjacent piece one square directly away. Kittens can boop kittens and cats
only if the moving piece is a cat; kittens cannot push cats. Pushed pieces that leave the board return to their
owner's supply. After the boops finish, three kittens in a row are removed from the board and returned as three cats
to their owner's supply. If more than three kittens are aligned, the player chooses which alignment of three to
promote. If all eight of a player's pieces are on the board, that player may graduate one kitten back to supply as a
cat. A mixed line of kittens and cats sends the whole line to supply while promoting the kittens. A player wins at the
end of their turn if that turn forms a line of three cats or if all eight kittens have been promoted, even if the
resulting position also looks winning for the opponent.

Several important shared files still assume checkers. `src/application.c` creates a `GCheckersModel`. `src/window.c`
stores checkers-specific puzzle, orientation, and move-analysis state. `src/sgf_move_props.c` understands only
checkers move notation. `src/sgf_io.c` assumes variant handling through `RU[...]` and fills root metadata with
`AP[gcheckers]` and `GM[40]`. `src/board_selection_controller.c` assumes square-grid play means selecting a path
through a move list, which works for current checkers but not for Homeworlds-style builders or for boop's
placement-plus-promotion choices.

The reusable pieces are already substantial. `src/game_model.c` can hold any backend-owned position and can apply a
move without pre-validating it against a move list when the backend opts out of exhaustive lists. `src/game_backend.h`
already defines a move-builder interface through `GameBackendMoveBuilder`, `move_builder_init`,
`move_builder_list_candidates`, `move_builder_step`, and `move_builder_build_move`. `src/games/homeworlds/`
demonstrates the intended staged-selection direction with explicit builder stages and candidate objects.
`src/ai_search.c` already searches through `GameBackend` callbacks. `src/board_view.c`, `src/board_grid.c`, and
`src/board_square.c` already render arbitrary square-grid positions through backend callbacks and can fall back to
backend-supplied symbols.

When this plan says "supply", it means off-board pieces still owned by a player and available for future placement.
When this plan says "promotion", it means removing a completed line of three kittens from the board and adding that
many cats to the owner's supply. When this plan says "staged move selection", it means the backend lists the currently
selectable choices, the UI lets the user choose one of them, the backend advances its builder state, and then the
backend lists the next legal choices until a full move is complete. This is the same interaction shape that
`src/games/homeworlds/homeworlds_move_builder.c` is already preparing for a non-square game.

## Plan of Work

The work begins in the build and backend-selection layer. Extend `Makefile` with a `boop` branch beside `checkers`
and `homeworlds`. That branch must define `APP_ID := io.github.jeromea.gboop`, `APP_BIN_NAME := gboop`,
`APP_MAIN_SRC := src/gboop.c`, a new backend define such as `-DGGAME_GAME_BOOP`, and boop-specific source lists under
`src/games/boop/`. It must also adjust test target selection so a boop build does not try to compile or run
checkers-only tests like puzzle generation. Update `src/active_game_backend.h` to select a new boop backend object.
Add boop-branded metadata files in `data/` and a Flatpak manifest path if the repository expects one per game.

Before boop depends on staged interaction, migrate square-grid input onto the move-builder model and port checkers
first. Replace the path-only assumptions in `src/board_selection_controller.c` with backend-driven staged selection.
The controller should initialize a backend builder from the current position, ask the backend for the current
candidate list, map those candidates onto square-grid highlights or other square-level affordances, feed the user's
choice back through `move_builder_step`, and continue until the builder reports completion and yields a final move. The
important requirement is that the controller no longer derives legality from `square_grid_move_has_prefix`; legality
must come from the builder's candidate list. Checkers should implement a builder that exposes exactly the same user
choices as today, just through stages instead of path-prefix filtering. This checkers migration is a deliberate
milestone in this plan because it validates the shared square-grid builder UI before boop adds promotion-choice cases.

Next, add the boop engine under `src/games/boop/`. The engine should stay UI-agnostic and expose explicit types for
piece rank, side, supply counts, board cells, move payloads, and terminal outcome. A good starting file set is:

  src/games/boop/boop_types.h
  src/games/boop/boop_game.h
  src/games/boop/boop_game.c
  src/games/boop/boop_backend.h
  src/games/boop/boop_backend.c

The engine position should include a 6x6 board, the side to move, per-side supply counts for kittens and cats, and a
cached outcome. A move should encode at least the destination square and the rank being placed, because once a player
has cats in supply they may place either a kitten or a cat, and it should also encode any later promotion choice
needed to distinguish different resolved turns that began from the same placement. The engine must provide helpers to
enumerate legal placements, apply one move, evaluate boops, restore booped-off pieces to supply, resolve cat-line
wins, resolve kitten promotions including the player choice for overlong kitten lines, handle the "all eight pieces are
on the board" graduation rule, update the side to move, and recalculate outcome. Keep these helpers deterministic and
testable without any GTK code.

Then implement the boop backend on top of that engine. Boop should expose `supports_move_list = TRUE`,
`supports_move_builder = TRUE`, and `supports_ai_search = TRUE`. The move list remains useful for AI, tests, and any
non-interactive callers, but the user-facing board interaction should be driven through the move builder. The builder's
first stage should offer legal placement choices, and later stages must cover any extra player choices required after
placement, especially selecting which kitten alignment to promote when the rules allow multiple valid promotions. The
backend must provide square-grid mapping for a 6x6 board, side labels, outcome text, move equality, move formatting,
move parsing, static evaluation, terminal scoring, and hashing. For piece rendering, use
`GAME_BACKEND_SQUARE_PIECE_KIND_SYMBOL_ONLY` with simple symbols such as `k` and `c` for side 0 and `K` and `C` for
side 1, or another similarly clear ASCII mapping, so the board is readable without new art assets.

In parallel, generalize SGF move handling to backend-owned notation. Add callbacks to `GameBackend` for parsing and
formatting SGF move values, or adjust `sgf_move_props.c` to call generic parse/format hooks that the backend owns. For
boop, the notation must serialize the fully resolved move, including any post-placement promotion choice when one was
required. Document the chosen notation in the backend tests. `src/sgf_move_props.c`, `src/sgf_controller.c`, and the
SGF-analysis helpers must stop including `games/checkers/game.h` directly. `src/sgf_io.c` must also stop requiring
`RU[...]` when the active backend has zero variants. The initial boop SGF path only needs empty-start games plus move
trees; it does not need arbitrary setup nodes or puzzle setup properties.

Once board interaction and SGF move notation are generic enough, integrate boop into the shared application shell. The
construction path in `src/application.c` and `src/window.c` must stop requiring `GCheckersModel` to create a playable
window. Replace the remaining model ownership with `GGameModel` where possible, and gate boop-unsupported features by
backend capability instead of by silent failure. For boop, the New Game dialog should offer only the player controls
and AI depth because there are no variants. Puzzle actions, puzzle dialogs, and arbitrary setup editing must be hidden
or insensitive in boop builds. SGF import/export may remain available only for the supported empty-start move-tree
format.

Finally, add focused boop tests and update the architecture documentation. Add engine-level tests for boops,
boop-off-board supply returns, kitten promotions, cat-line wins, the "all eight pieces are on the board" graduation
rule, and end-of-turn win detection when both sides appear winning after the active player's move. Add backend tests
for metadata, move formatting/parsing, move generation counts, move-builder stage progression, square-grid mapping, and
AI candidate ordering. Add shared UI/controller tests proving that checkers works through the new staged-selection
controller and that boop can complete both single-placement turns and promotion-choice turns through the same
mechanism. As each `src/` milestone lands, update `doc/OVERVIEW.md` so the source tree stays accurate for the next
contributor.

## Milestones

### Milestone 1: build selection and boop skeleton

At the end of this milestone, the repository can build a boop-flavored binary and select a boop backend at compile
time, even if the backend still returns placeholder move data. The user-visible proof is that `GAME=boop make`
produces `build/bin/gboop` and that the boop build does not try to compile checkers-only tests or tools.

Edit `Makefile`, `src/active_game_backend.h`, and the boop metadata files so `GAME=boop` is a first-class option.
Create stub boop backend files under `src/games/boop/` with correct metadata, sizes, and callback stubs guarded by
`g_return_val_if_fail()` or debug logging where appropriate. Add boop-focused test targets such as `test_boop_game`
and `test_boop_backend`, even if the first milestone only uses trivial assertions.

Run:

  cd /home/jerome/Data/gcheckers
  GAME=boop make

Expected proof:

  build/bin/gboop
  build/lib/libgame.a

Acceptance for this milestone is that the boop build compiles cleanly, the boop backend is selectable, and the default
checkers build still works unchanged.

### Milestone 2: staged square-grid selection and checkers migration

At the end of this milestone, square-grid interaction no longer depends on path-prefix filtering in shared UI code.
Instead, the shared controller consumes backend-provided staged candidates, and checkers has been migrated onto that
flow without changing its user-visible move semantics.

Implement the square-grid builder controller in shared code, using the same conceptual pattern as
`src/games/homeworlds/homeworlds_move_builder.c`: initialize builder state from the position, ask for the current
candidate list, let the user choose one candidate, step the builder, and repeat until a complete move is produced.
Then add a checkers move builder and wire checkers board interaction through it. The checkers builder should expose the
same start-square and destination choices that the current path controller infers from the move list today, but it must
derive them from backend stages rather than from `square_grid_move_has_prefix`.

Run:

  cd /home/jerome/Data/gcheckers
  make test_board_view test_sgf_controller test_window test_game_backend test_game_model
  build/tests/test_board_view
  build/tests/test_sgf_controller
  build/tests/test_window

Acceptance for this milestone is that the default checkers build still behaves the same to a user, but the board input
path is now builder-driven and no longer assumes every square-grid move is a path grown entirely in shared UI code.

### Milestone 3: boop engine and backend

At the end of this milestone, the repository contains a complete boop rules engine and a backend that can enumerate and
apply legal placement moves while also driving interactive move construction through staged selections.

Implement the board, supply, move, and outcome logic in `src/games/boop/boop_game.c`. The engine must cover all rule
details from `src/games/boop/RULES.md`, especially overlong-line promotion choice, the "all eight pieces on the
board" graduation rule, and the active-player-wins end-of-turn rule. Then implement `src/games/boop/boop_backend.c`
so the backend exposes move lists, move-builder stages and candidates, square-grid mapping, search hooks, and
symbol-based piece views for the board.

Run:

  cd /home/jerome/Data/gcheckers
  GAME=boop make test_boop_game test_boop_backend test_game_backend test_game_model
  build/tests/test_boop_game
  build/tests/test_boop_backend
  build/tests/test_game_backend
  build/tests/test_game_model

Acceptance for this milestone is that boop rule tests pass, backend metadata and move flow tests pass, and the boop
backend can initialize a starting position, list legal placements, format a move, apply one move successfully, and
walk through promotion-choice interactions when multiple kitten alignments are available.

### Milestone 4: boop SGF move generalization and shared app integration

At the end of this milestone, a boop move can be entered through staged square-grid interaction and can be stored in
and replayed from the shared SGF tree.

Update `src/sgf_move_props.c`, `src/sgf_move_props.h`, `src/sgf_controller.c`, and `src/sgf_io.c` so move parsing and
formatting are backend-owned and zero-variant games are supported without `RU[...]`. Integrate boop into the shared
application/window flows using the new staged-selection controller, and gate unsupported checkers-only features by
backend capability rather than by silent failure. Add regression tests for boop move notation and for staged
controller behavior across both checkers and boop.

Run:

  cd /home/jerome/Data/gcheckers
  GAME=boop make test_sgf_io test_sgf_controller test_board_view test_window test_game_backend test_boop_backend
  build/tests/test_sgf_io
  build/tests/test_sgf_controller
  build/tests/test_board_view
  build/tests/test_window

Acceptance for this milestone is that a boop move can be serialized into the SGF tree, loaded back into a fresh model,
and replayed to the same position, and that the shared application can drive both checkers and boop through staged
selection without regressing checkers behavior.

### Milestone 5: focused boop app validation

At the end of this milestone, `gboop` is a coherent playable application. A user can start a boop game, choose human
or computer players, place pieces, resolve promotion choices when necessary, navigate move history, and finish a game.
Unsupported puzzle and edit-mode features are clearly unavailable rather than silently broken.

Run:

  cd /home/jerome/Data/gcheckers
  GAME=boop make test_app_paths test_desktop_metadata test_flatpak_manifest
  build/bin/gboop

Acceptance for this milestone is that the boop application window opens, a local game is playable through the board,
promotion-choice turns are resolvable through the staged UI, history navigation works, and unsupported boop features
are absent or insensitive rather than failing at runtime.

## Concrete Steps

Use these commands from `/home/jerome/Data/gcheckers` as implementation proceeds. Update this section with real
transcripts as milestones land.

For the build-selection milestone:

  GAME=boop make

For the staged-selection migration:

  make test_board_view test_sgf_controller test_window test_game_backend test_game_model
  build/tests/test_board_view
  build/tests/test_sgf_controller
  build/tests/test_window

For the boop engine and backend:

  GAME=boop make test_boop_game test_boop_backend test_game_backend test_game_model
  build/tests/test_boop_game
  build/tests/test_boop_backend
  build/tests/test_game_backend
  build/tests/test_game_model

For the boop SGF and shared-app milestone:

  GAME=boop make test_sgf_io test_sgf_controller test_board_view test_window
  build/tests/test_sgf_io
  build/tests/test_sgf_controller
  build/tests/test_board_view
  build/tests/test_window

For final application validation:

  GAME=boop make test_app_paths test_desktop_metadata test_flatpak_manifest
  build/bin/gboop

The first manual validation scenario should be:

  1. Start `build/bin/gboop`.
  2. Create a new human-vs-human game.
  3. Select a legal placement through the staged board UI and confirm the move completes correctly.
  4. Continue until one placement boops a piece off the board and confirm the returned piece is again available in
     supply.
  5. Continue until one side forms more than one promotable kitten alignment and confirm the staged UI lets the user
     choose the promotion outcome.
  6. Save or replay the game through the SGF-backed history and confirm the same board state is reconstructed.

## Validation and Acceptance

Validation is complete only when all of the following are true.

First, `GAME=boop make` succeeds and produces `build/bin/gboop`.

Second, the boop-focused automated tests pass. At minimum that means the new boop engine/backend tests plus the
relevant shared tests for backend metadata, model application, staged square-grid interaction, SGF move replay, and
window interaction. The boop test suite must cover at least these observable rules:

  - placing a kitten boops adjacent kittens but not adjacent cats;
  - placing a cat boops adjacent kittens and adjacent cats;
  - booped-off pieces return to the correct supply;
  - three kittens in a row are removed and become cats in supply;
  - a line longer than three kittens requires choosing one alignment of three to promote;
  - when all eight pieces are on the board, one kitten can graduate back to supply as a cat;
  - three cats in a row ends the game immediately;
  - end-of-turn win detection awards the move to the active player even if the opponent also has a winning position;
  - staged square-grid interaction can complete both a simple placement turn and a promotion-choice turn.

Third, manual play in `build/bin/gboop` shows that the window can start a game, complete staged selections, navigate
history, and finish with the correct winner banner text.

## Idempotence and Recovery

All build and test commands in this plan are safe to re-run. If a milestone fails midway, keep the code compiling and
update the `Progress` section before stopping. When changing shared SGF, board-input, or window code, prefer additive
hooks and backend capability checks before deleting old checkers paths so the default build stays working during the
migration. The checkers builder migration should land before boop depends on shared staged selection.

If a partial boop SGF implementation lands before generic setup handling is finished, keep boop import/export limited
to empty-start move trees and disable unsupported import cases with a clear error rather than silently constructing the
wrong position.

## Artifacts and Notes

Important evidence to capture as the work proceeds:

  - the exact `GAME=boop make` success transcript that shows `build/bin/gboop`;
  - one short transcript or test assertion showing checkers using the new staged-selection controller;
  - one boop SGF example that shows the chosen root metadata and resolved move notation;
  - a concise test transcript for the new boop engine/backend tests;
  - a short note in `doc/OVERVIEW.md` describing where boop-specific source now lives and which shared files were
    generalized for it.

## Interfaces and Dependencies

The boop backend must remain within the existing `GameBackend` architecture. If new callbacks are needed, add them to
`src/game_backend.h` in a way that keeps checkers building. Shared square-grid interaction should prefer the existing
move-builder ABI where possible rather than inventing another parallel selection interface. The final boop-facing
interfaces should include these concepts, even if the exact names differ slightly:

In `src/games/boop/boop_types.h`, define position and move storage for boop:

  typedef enum {
    BOOP_PIECE_NONE = 0,
    BOOP_PIECE_KITTEN,
    BOOP_PIECE_CAT
  } BoopPieceRank;

  typedef struct {
    guint8 side;
    BoopPieceRank rank;
  } BoopPiece;

  typedef struct {
    guint8 square;
    BoopPieceRank rank;
    guint8 promotion_choice;
  } BoopMove;

  typedef struct {
    BoopPiece board[36];
    guint8 kittens_in_supply[2];
    guint8 cats_in_supply[2];
    guint8 turn;
    GameBackendOutcome outcome;
  } BoopPosition;

In `src/games/boop/boop_game.h`, define engine helpers similar to:

  void boop_position_init(BoopPosition *position);
  GameBackendMoveList boop_list_moves(const BoopPosition *position);
  gboolean boop_apply_move(BoopPosition *position, const BoopMove *move);
  gint boop_evaluate_static(const BoopPosition *position);
  guint64 boop_hash_position(const BoopPosition *position);
  gboolean boop_format_move(const BoopMove *move, char *buffer, gsize size);
  gboolean boop_parse_move(const char *text, BoopMove *out_move);
  gboolean boop_move_builder_init(const BoopPosition *position, GameBackendMoveBuilder *out_builder);

The static evaluation does not need to be sophisticated in the first milestone, but it must be deterministic and
should at least value cat count, threatened cat-lines, central-board occupancy, and immediate promotion opportunities
strongly enough that the shared search can choose plausible placements.

Revision note (2026-04-27 / Codex): revised the ExecPlan so staged backend-driven move selection is a first-class
milestone, with checkers migrated to that flow before boop depends on it, matching the same move-builder direction now
visible in `src/games/homeworlds/`.

Revision note (2026-04-27 / Codex): updated the living document after implementing Milestone 1 so the progress and
outcomes reflect the new `GAME=boop` build branch, stub backend, and boop skeleton app.
