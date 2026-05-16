# Finish Homeworlds as a profile-owned game

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document must be maintained in accordance with `doc/PLANS.md`.

## Purpose / Big Picture

Homeworlds already has a rules engine, backend adapter, branded launcher, and profile entry, but `build/bin/ghomeworlds`
still opens only a skeleton window. After this plan is complete, a user will be able to run `build/bin/ghomeworlds`,
complete Homeworlds setup, take legal local turns through a Homeworlds-specific GTK view, optionally let a simple
random AI choose legal moves through the staged builder, and see the game end when a player starts their turn with no
ships at their homeworld.

The purpose of this revision is also protective: since the original plan was written, the repository moved in the
right direction for multiple games. Boop is now a fully supported second shared-shell game, the Makefile builds all
application binaries generically, and runtime `GGameAppProfile` selection replaced compile-time `GAME` selection.
Future Homeworlds work must build on those improvements, not reintroduce stale compile-time branches or Checkers-only
assumptions.

## Progress

- [x] (2026-04-25 16:05Z) Read `doc/PLANS.md`, `doc/OVERVIEW.md`,
      `doc/execplan-game-backend-interface.md`, and `src/games/homeworlds/RULES.md`, then write the initial
      Homeworlds ExecPlan.
- [x] (2026-04-25 17:15Z) Add the first Homeworlds build/backend skeleton, branded `ghomeworlds` launcher, and
      backend-selection tests.
- [x] (2026-04-26 10:55Z) Add the slot-based Homeworlds engine, staged move builder, backend-owned `list_good_moves`,
      shared AI support for good-move-only backends, and focused Homeworlds engine/backend tests.
- [x] (2026-05-13 21:28Z) Review the plan against the current repository and rewrite it so it preserves the generic
      Makefile, runtime profiles, and Boop shared-shell support instead of instructing future work to revert them.
- [x] (2026-05-13 23:05Z) Replace the Homeworlds skeleton window in
      `src/games/homeworlds/homeworlds_app_window.c` with a real profile-owned playable window.
- [x] (2026-05-13 23:05Z) Add a Homeworlds-specific view/controller for systems, bank pieces, setup, ordinary actions,
      sacrifices, and catastrophes.
- [x] (2026-05-13 23:05Z) Connect the Homeworlds view to `GGameModel`, `homeworlds_move_builder`, and
      `homeworlds_game_backend` without using full move enumeration.
- [x] (2026-05-13 23:18Z) Add tests for the Homeworlds profile window, staged interaction paths, and profile-gated
      application behavior.
- [x] (2026-05-13 23:20Z) Add Homeworlds packaging metadata and update `doc/OVERVIEW.md` when `src/` or packaging
      changes land.
- [x] (2026-05-15 09:30Z) Move setup selection from the long side-panel candidate list to clickable bank piles
      overlaid directly on the Homeworlds board, with setup accepting any bank pyramid size.
- [x] (2026-05-15 10:10Z) Fix homeworld display geometry so player 1 is rendered at the bottom, player 2 at the top,
      and each homeworld's own ships sit beside the stars from that player's perspective.
- [x] (2026-05-15 10:30Z) Make ship and bank pyramids tall isosceles pieces, increase geometric size contrast, double
      vertical-pyramid scale, and render black base pips on stars and vertical pyramids.

## Surprises & Discoveries

- Observation: the repository is no longer selected by a compile-time `GAME` variable.
  Evidence: `src/active_game_backend.h` now expands to `ggame_active_app_profile()->backend`, and `src/gcheckers.c`,
  `src/gboop.c`, and `src/ghomeworlds.c` each call `ggame_app_main_run()` with a different `GGameAppProfile`.

- Observation: `make` now builds all branded binaries and puzzle tools together.
  Evidence: `Makefile` has `APP_BINS := build/bin/gcheckers build/bin/gboop build/bin/ghomeworlds`, `all` depends on
  `$(APP_BINS)`, and specific targets are named `all-checkers`, `all-boop`, and `all-homeworlds`.

- Observation: Boop proves that the shared shell is now a real multi-game path, not a Checkers-only shell.
  Evidence: the Boop profile sets `supports_shared_shell = TRUE`, provides `gboop_controls_create_board_host()` as a
  profile UI hook, supports SGF files, puzzles, analysis, save-position, and has profile-aware window tests.

- Observation: Homeworlds already has the engine/backend half of the original plan, but not the GUI half.
  Evidence: `src/games/homeworlds/homeworlds_game.c`, `homeworlds_move_builder.c`, and `homeworlds_backend.c` exist;
  `homeworlds_game_backend` advertises `supports_move_builder = TRUE`, `supports_move_list = FALSE`, and
  `supports_ai_search = TRUE`; `src/games/homeworlds/homeworlds_app_window.c` still displays the text
  "Homeworlds skeleton build".

- Observation: Homeworlds currently opts out of the shared shell through profile flags and a custom toplevel window
  hook.
  Evidence: `src/game_app_profile.c` sets Homeworlds `supports_shared_shell = FALSE` and
  `.ui.create_window = ghomeworlds_app_window_create`.

- Observation: the old plan's instruction to add a generic board-surface abstraction is no longer the only reasonable
  path. Boop integrated by wrapping the existing square-grid `BoardView` with a profile-owned board host, while
  Homeworlds can use its existing profile-owned toplevel window hook for a non-square game.
  Evidence: `GGameAppUiHooks` has both `create_window` and `create_board_host`.

- Observation: a staged Homeworlds view must distinguish external model refresh from internal partial-selection
  refresh.
  Evidence: rebuilding the builder after every candidate selection lost setup selections; the view now updates buttons
  and drawing from the current builder until the move completes.

- Observation: the random AI must reject source ships whose apparent actions cannot be completed.
  Evidence: a red-only starting homeworld exposes an attack action with no target and a sacrifice that leaves no ship
  for the granted actions; the stochastic policy now checks continuations recursively and rejects those source ships
  instead of passing.

- Observation: setup candidates are too numerous for a practical side-panel button list.
  Evidence: the initial setup stage can expose many near-duplicate bank choices. The view now renders the bank over the
  board and turns each pile of identical pyramids into one real `GtkButton`, while the side panel shows only an
  instruction during setup.

## Decision Log

- Decision: do not reintroduce compile-time `GAME=...` selection.
  Rationale: the current runtime profile registry is a better general solution. It allows one object graph and one
  shared application entry path to support `gcheckers`, `gboop`, and `ghomeworlds`, and it avoids wrong-object builds
  caused by preprocessor-selected sources.
  Date/Author: 2026-05-13 / Codex

- Decision: the Makefile should remain all-games-by-default.
  Rationale: `make` and `make all` now build every binary, while `make all-homeworlds`, `make all-checkers`, and
  `make all-boop` build individual application binaries. Homeworlds work should add sources to those generic lists
  rather than adding recursive Makefile calls or profile-specific object directories.
  Date/Author: 2026-05-13 / Codex

- Decision: finish Homeworlds first through its profile-owned window hook, not by replacing the working Checkers/Boop
  shared shell.
  Rationale: Homeworlds is not a square-grid game. The existing `create_window` hook lets Homeworlds own a custom GTK
  window while still using the same application/profile/backend infrastructure. That is lower risk than refactoring
  the mature shared shell before Homeworlds has one playable UI.
  Date/Author: 2026-05-13 / Codex

- Decision: keep Checkers and Boop shared-shell behavior as the compatibility baseline.
  Rationale: Boop's support for puzzles, SGF, analysis, save-position, and profile-owned board controls is evidence
  that the shared shell can support multiple games. Homeworlds changes must not remove or simplify those paths just to
  satisfy stale Homeworlds instructions.
  Date/Author: 2026-05-13 / Codex

- Decision: Homeworlds human interaction must continue to use the staged move builder, and backend search candidates
  must continue to use `list_good_moves`.
  Rationale: Homeworlds can have a very large legal move space, so full move enumeration is deliberately unsupported.
  The existing backend contract already encodes this with `supports_move_list = FALSE`, `supports_move_builder = TRUE`,
  and `supports_ai_search = TRUE`.
  Date/Author: 2026-05-13 / Codex

- Decision: the first playable Homeworlds window AI should be a simple stochastic policy, not a full search player.
  Rationale: the first UI milestone needs a legal automated opponent for testing and local play, not strong strategy.
  A builder-driven random policy exercises the same staged legality path as human input and avoids materializing the
  full move space.
  Date/Author: 2026-05-13 / Codex

## Outcomes & Retrospective

The playable Homeworlds milestone is complete in the profile-owned window path. The repository now has three profiles,
three launchers, one generic all-game Makefile, two fully supported shared-shell games, and a custom Homeworlds GTK
window for its non-square system graph. Homeworlds remains outside the shared SGF/puzzle/analysis shell, but it can be
set up and played locally through staged legal choices, direct catastrophe buttons, and a builder-driven random AI.

The important lesson is still that Homeworlds should not roll the repository back to a `GAME=homeworlds make` world.
The profile system, all-game Makefile, and Boop shared-shell support stayed intact; Homeworlds filled in its missing UI
behind the profile-owned window hook instead.

## Context and Orientation

The repository is a C/GTK application framework with three branded application binaries:

- `build/bin/gcheckers` for Checkers.
- `build/bin/gboop` for Boop.
- `build/bin/ghomeworlds` for Homeworlds.

The default command `make` or `make all` builds all three binaries plus shared libraries and puzzle tools. A developer
can build one application binary with `make all-checkers`, `make all-boop`, or `make all-homeworlds`. Do not use
`GAME=homeworlds make`; that was an older design and should not be restored.

The runtime game selection mechanism is `GGameAppProfile`, defined in `src/game_app_profile.h` and implemented in
`src/game_app_profile.c`. A profile is a descriptor containing the app ID, display name, settings schema ID, the
profile-owned `GameBackend`, feature flags, layout defaults, and optional UI hooks. The launcher files
`src/gcheckers.c`, `src/gboop.c`, and `src/ghomeworlds.c` select their profile and call the shared
`ggame_app_main_run()` function. Shared code gets the active backend through `GGAME_ACTIVE_GAME_BACKEND`, which now
means `ggame_active_app_profile()->backend`.

The backend interface is `src/game_backend.h`. A backend owns game-specific position and move types behind opaque
storage sizes, initializes/copies/clears positions, applies moves, evaluates positions, formats/parses moves when
supported, and advertises capabilities. The capability flags matter for Homeworlds:

- `supports_move_list` means the backend can enumerate every legal move in a position.
- `supports_move_builder` means the backend can guide a UI through legal move construction one choice at a time.
- `supports_ai_search` means the generic AI layer can evaluate backend moves.
- `list_good_moves` is a backend callback that returns a bounded, heuristic subset of legal moves for search.

Checkers supports the historical square-grid move-list path. Boop supports the shared shell, square-grid rendering,
SGF, puzzles, analysis, and a profile board-host hook in `src/games/boop/boop_controls.c`. Homeworlds currently
supports engine/backend/search but not a playable GUI. Its important files are:

- `src/games/homeworlds/RULES.md`: prose game rules.
- `src/games/homeworlds/homeworlds_types.h`: engine data types.
- `src/games/homeworlds/homeworlds_game.c` and `.h`: position logic, rule application, evaluation, hashing, and move
  formatting.
- `src/games/homeworlds/homeworlds_move_builder.c` and `.h`: staged legal-choice generation for setup and turns.
- `src/games/homeworlds/homeworlds_backend.c` and `.h`: `GameBackend` adapter.
- `src/games/homeworlds/homeworlds_app_window.c` and `.h`: current skeleton profile-owned GTK window.
- `src/games/homeworlds/homeworlds_app_window_stub.c`: weak stub used by headless test targets.
- `src/games/homeworlds/homeworlds.png`: visual mockup for the Homeworlds UI. It shows the intended visual direction,
  including a starfield background, rounded system boxes, labeled homeworlds, pieces drawn as colored pyramids/stars,
  and a right-side action/bank panel. Treat this image as a reference for layout and atmosphere, while the prose in
  this ExecPlan remains authoritative if the two conflict.

The shared `GGameWindow` in `src/window.c` is mature for Checkers and Boop. It owns the shared shell, `BoardView`,
SGF controller, puzzle mode, analysis drawer, and board-host hook. Homeworlds does not need to enter this shell before
it is playable. The Homeworlds profile already has `.ui.create_window = ghomeworlds_app_window_create`, so the next
implementation can replace the skeleton window with a custom Homeworlds window while still using the shared
application/profile/backend infrastructure.

## Plan of Work

Start by preserving the current architecture. Leave `src/active_game_backend.h` profile-driven. Leave the Makefile
all-games-by-default. Leave Checkers and Boop shared-shell code intact. Any new Homeworlds source files should be added
to the existing Homeworlds source variables in `Makefile` rather than adding a new compile-time game selector.

Replace the skeleton in `src/games/homeworlds/homeworlds_app_window.c` with a real profile-owned GTK window. The
window should create a `GGameModel` using `homeworlds_game_backend`, show turn/outcome status, show a Homeworlds view,
and expose simple controls for new game, reset selection, pass/cancel when legal, and optional AI move. The window can
be smaller and less feature-rich than `GGameWindow`; the acceptance bar is playable local Homeworlds, not parity with
Checkers and Boop puzzle/analysis/SGF workflows.

Add a Homeworlds-specific view/controller, likely in new files such as
`src/games/homeworlds/homeworlds_view.c` and `src/games/homeworlds/homeworlds_view.h`. The exact widget structure may
change during implementation, but it must render systems, stars, ships, the bank, selected pieces, legal next choices,
and pending sacrifice/action state. It must drive `homeworlds_move_builder` instead of asking the backend for a full
move list. When the builder completes a `HomeworldsMove`, apply it through `GGameModel` or directly through the
profile backend and then refresh the view from the resulting position.

Render the board using the traditional layered Homeworlds presentation rather than square-grid widgets. A "system" is
one star or two stars plus ships at that location. A "homeworld" is the special system owned by a player. The view
should place player homeworlds near opposite sides, put systems one connection hop from each homeworld in nearby
layers when that is helpful, and put remaining systems in a central layer. Stars can be squares in three sizes, ships
can be triangles in three sizes pointing away from their owner, and the bank can be a side panel grouped by color and
size. Do not try to fit Homeworlds into `BoardView`.

Use `src/games/homeworlds/homeworlds.png` as the visual reference for the first pass: a dark starfield board, rounded
white outlines around systems, explicit "Homeworld" labels on player homeworld systems, colored pyramids with size
marks, square stars, and a right-side vertical panel for action/bank choices. The text requirements in this plan take
precedence over the mockup when details differ, so do not copy visual details that would contradict the move-builder
or rules requirements.

Use the move builder as the user interaction source of truth. During setup, the user should select two bank stars and
one bank ship to create a legal homeworld. During ordinary turns, the user should select a source ship, choose a legal
action or sacrifice, then select any required target. For sacrifices, the chosen sacrificed ship fixes the action
color and action count, after which the view repeats the relevant source/target selection until the granted actions are
spent or a legal pass/end option is selected. The view should visibly distinguish selected items, legal candidates, and
disabled/non-candidate items.

Keep AI integration narrow. The default AI in the Homeworlds window should be builder-driven and stochastic: choose a
random owned ship to activate, choose a random currently available action, never choose pass, avoid choosing capture
when there is nothing to capture, and then execute that staged action through the same move-builder legality path as
human input. For a
move/discover action, if there are `N` existing destination systems, choose a random number in `[0, N]`; values below
`N` move to that indexed existing destination, while value `N` means discover by choosing a random valid new
destination from the bank. The UI must not require `list_moves`, and it must not enumerate all legal moves to find
either a human move or this default AI move. The Homeworlds backend should keep `list_good_moves` available for the
generic AI/search layer, but the first playable window AI does not need to call search.

Keep external SGF, puzzles, analysis, settings, and save-position disabled for Homeworlds until they are implemented
intentionally. The Homeworlds profile currently advertises those features as unsupported. Do not turn them on merely to
reuse Checkers/Boop UI. A later plan can decide whether Homeworlds should enter the shared shell, add SGF parsing, or
use another history model; this plan's playable milestone should not block on that.

Add tests as each piece lands. Pure engine and backend tests already exist and should remain focused on rules and
capabilities. New tests should cover the Homeworlds profile window enough to prove it is not the skeleton anymore, the
view can advance at least one setup path and one ordinary turn path through the move builder, and profile flags still
keep unsupported shared-shell actions unavailable. When adding `src/` files, update `doc/OVERVIEW.md` in the same
change.

After the game is playable, add Homeworlds packaging metadata beside the Checkers and Boop files. The Makefile already
has `HOMEWORLDS_APP_ID` and `HOMEWORLDS_APP_BIN_NAME`, but the installed desktop/metainfo/icon/Flatpak lists currently
cover Checkers and Boop. Add Homeworlds files and wire them into the existing generic metadata targets without
changing the all-games build model.

## Milestones

### Milestone 1: current architecture baseline

At the end of this milestone, the plan and tests describe the current profile-based architecture accurately. No source
architecture should be reverted. The developer should be able to prove that all three launchers build and that the
Homeworlds backend tests still pass.

Run from `/home/jerome/Data/gcheckers`:

    make all
    make test_homeworlds_game test_homeworlds_backend
    build/tests/test_homeworlds_game
    build/tests/test_homeworlds_backend
    build/tests/test_game_backend --profile=homeworlds
    build/tests/test_game_model --profile=homeworlds

Acceptance is that `build/bin/gcheckers`, `build/bin/gboop`, and `build/bin/ghomeworlds` exist, the Homeworlds
engine/backend tests pass, and profile tests report Homeworlds as move-builder/AI capable but not shared-shell or
puzzle capable.

### Milestone 2: Homeworlds view/controller

At the end of this milestone, there is a Homeworlds-specific GTK widget or controller that can render a position and
advance a `HomeworldsMoveBuilderState` through visible legal choices. It does not need to be beautiful, but it must be
clear enough for manual play and testable enough for automated coverage.

Add files such as:

    src/games/homeworlds/homeworlds_view.c
    src/games/homeworlds/homeworlds_view.h

The view should expose a small API for binding a model or position, starting/resetting a builder, notifying when a
complete move is ready, and refreshing after a move. The API names can evolve, but they should stay local to the
Homeworlds directory until there is a proven need for shared abstractions.

Use `src/games/homeworlds/homeworlds.png` to guide the first visual implementation, but keep the implementation
rule-driven. It is acceptable for the first GTK version to approximate the mockup if all legal choices remain clear
and the text requirements in this plan are satisfied.

Run from `/home/jerome/Data/gcheckers`:

    make all-homeworlds
    make test_homeworlds_game test_homeworlds_backend

Acceptance is that the Homeworlds binary still builds, rule/backend tests still pass, and any new view tests prove at
least setup candidate rendering plus one ordinary action candidate path.

### Milestone 3: playable Homeworlds profile window

At the end of this milestone, `build/bin/ghomeworlds` no longer displays a skeleton message. It starts a local
Homeworlds game, lets two humans complete setup, take turns, resolve catastrophes, and reach a terminal outcome.

Edit `src/games/homeworlds/homeworlds_app_window.c` to own the Homeworlds model, the Homeworlds view, status labels,
and simple actions. Keep this path profile-owned through `.ui.create_window`; do not move it into `GGameWindow` unless
there is a separate decision and tests for that migration.

Manual acceptance scenario:

    cd /home/jerome/Data/gcheckers
    make all-homeworlds
    build/bin/ghomeworlds

Then verify:

    1. The window shows an actual Homeworlds board/system view, not "Homeworlds skeleton build".
    2. Player 1 and player 2 can create legal homeworlds.
    3. A player can perform at least one construct action.
    4. A player can perform at least one trade action.
    5. A player can perform at least one attack action.
    6. A player can perform at least one move/discover action.
    7. A sacrifice grants the expected repeated same-color actions.
    8. A catastrophe removes all overpopulated pieces of that color.
    9. A player with no ships at their homeworld loses at the start of their turn.

Automated acceptance should include at least one profile/window test that can instantiate the Homeworlds window and
prove the skeleton label is gone or that the primary Homeworlds view is present.

### Milestone 4: default random AI in the Homeworlds window

At the end of this milestone, the Homeworlds window can apply one legal automated move using a simple random policy.
The AI does not need to be strong; it must be legal, bounded, connected to the same model state as human moves, and
implemented through staged move-builder choices rather than full move enumeration.

Add a simple AI control to the Homeworlds window. It should be disabled while a human move is partially selected and
enabled only when the current position is ongoing. The default policy should choose a random owned ship to activate,
choose a random available action for that ship, never choose pass, skip capture as an action choice when there are no
legal capture targets, and then complete the selected action through the builder. For move/discover, first collect the
`N` existing
systems the ship can legally move to. Pick a random integer from `0` through `N` inclusive. If the value is lower than
`N`, move to that existing destination. If the value is exactly `N`, choose a random valid discovery destination from
the bank and move there. If no discovery destination is valid, retry among the existing destinations or choose another
legal action rather than producing an invalid move.

Run from `/home/jerome/Data/gcheckers`:

    make all-homeworlds
    build/tests/test_homeworlds_backend

Manual acceptance is that after setup, clicking the AI control applies one legal Homeworlds move selected by this
random policy and the game remains playable.

### Milestone 5: packaging, documentation, and cross-game validation

At the end of this milestone, Homeworlds is a first-class branded target in build, docs, and packaging metadata.

Add Homeworlds metadata beside the existing app metadata, for example:

    data/io.github.jeromea.ghomeworlds.desktop
    data/io.github.jeromea.ghomeworlds.metainfo.xml
    data/icons/hicolor/scalable/apps/io.github.jeromea.ghomeworlds.svg
    flatpak/io.github.jeromea.ghomeworlds.yaml

Wire these into the existing generic Makefile metadata variables instead of creating a parallel Homeworlds-only build
path. Update `doc/OVERVIEW.md` to describe the Homeworlds window/view modules and the finalized profile behavior.

Run from `/home/jerome/Data/gcheckers`:

    make all
    make test_desktop_metadata test_flatpak_manifest
    build/tests/test_game_backend --profile=checkers
    build/tests/test_game_backend --profile=boop
    build/tests/test_game_backend --profile=homeworlds

Acceptance is that all three app binaries build from the same default Makefile target, metadata validation covers
Homeworlds, and Checkers/Boop profile behavior is unchanged.

## Concrete Steps

Use these commands from the repository root as work proceeds. Update this section with short observed transcripts at
each milestone.

Baseline commands for this revision:

    cd /home/jerome/Data/gcheckers
    make all
    make test_homeworlds_game test_homeworlds_backend
    build/tests/test_homeworlds_game
    build/tests/test_homeworlds_backend
    build/tests/test_game_backend --profile=homeworlds
    build/tests/test_game_model --profile=homeworlds

Commands to inspect the architecture before changing code:

    sed -n '1,220p' src/game_app_profile.c
    sed -n '1,140p' src/game_app_profile.h
    sed -n '1,80p' src/active_game_backend.h
    sed -n '1,260p' Makefile
    find src/games/homeworlds -maxdepth 2 -type f | sort
    rg -n "supports_shared_shell|create_window|create_board_host|list_good_moves" src tests doc/OVERVIEW.md

Expected evidence from the architecture inspection:

    src/active_game_backend.h:
    #define GGAME_ACTIVE_GAME_BACKEND (ggame_active_app_profile()->backend)

    Makefile:
    all: $(GSETTINGS_SCHEMA_COMPILED) $(LIBGAME_A) $(CREATE_PUZZLES_BINS) $(APP_BINS)
    ghomeworlds all-homeworlds: $(HOMEWORLDS_APP_BIN)

    src/game_app_profile.c:
    .id = "homeworlds"
    .backend = &homeworlds_game_backend
    .features.supports_shared_shell = FALSE
    .ui.create_window = ghomeworlds_app_window_create

When adding a new source file under `src/games/homeworlds/`, update the relevant `HOMEWORLDS_*_SRCS` variable in
`Makefile`. When changing `src/`, update `doc/OVERVIEW.md` in the same branch.

## Validation and Acceptance

For Homeworlds rules, the important observable behaviors are:

- Legal setup accepts a two-star homeworld plus one owned ship and rejects illegal setups.
- Construct takes the smallest available pyramid of the matching color.
- Trade swaps color while preserving size.
- Attack requires sufficient ship size.
- Movement/discovery obeys the rule that systems are connected only when they do not share a star size.
- Catastrophes remove all pieces of the overpopulated color and collapse systems whose stars are removed.
- A player loses when their turn begins with no ships at their homeworld.

For Homeworlds UI, the important observable behaviors are:

- `build/bin/ghomeworlds` opens a Homeworlds-branded playable window.
- The central view renders Homeworlds systems, ships, and bank pieces, not a square checkerboard.
- User clicks follow the staged move builder and never require full move-list enumeration.
- Selected items, legal next choices, pending sacrifice/action state, and terminal outcomes are visible.
- The AI control applies legal moves through the staged random policy without requiring full move enumeration.

For cross-game safety, every shared build or profile change must also protect Checkers and Boop:

- `make all` must still build `gcheckers`, `gboop`, and `ghomeworlds`.
- Checkers and Boop must remain runtime profiles, not compile-time preprocessor selections.
- Boop shared-shell support for SGF, puzzles, analysis, save-position, and board-host controls must not be removed.
- Profile tests should be run for `--profile=checkers`, `--profile=boop`, and `--profile=homeworlds` when profile
  behavior changes.

At the time of this revision, the full `make test` target is known to stop at `/sgf-view/link-angles` with a GTK
`invalid (NULL) pointer instance` fatal critical. Do not treat that as caused by Homeworlds unless a later change
shows otherwise, but do continue running focused tests and record any new failures.

## Idempotence and Recovery

The safe recovery rule is: keep the current profile/build architecture working, and make Homeworlds changes additive.
If a Homeworlds UI milestone fails halfway, `make all-homeworlds` should still compile either the last working
Homeworlds window or a clearly incomplete one. Avoid editing Checkers or Boop shared-shell code unless the Homeworlds
milestone explicitly needs a small generic hook and has regression tests for the existing profiles.

Re-running `make all`, `make all-homeworlds`, and the Homeworlds tests is safe. Re-running profile tests with
`--profile=checkers`, `--profile=boop`, and `--profile=homeworlds` is safe. Generated puzzle files and profiler
outputs should not be committed unless the milestone explicitly requires fixture updates.

If a change accidentally reintroduces `GAME=homeworlds`, `GGAME_GAME_HOMEWORLDS`, or other preprocessor-selected game
objects, stop and remove that direction. The current source of truth is `GGameAppProfile`.

## Artifacts and Notes

Important current-state snippets:

    $ make all
    make: Nothing to be done for 'all'.

    $ build/tests/test_homeworlds_game
    All tests passed.

    $ build/tests/test_homeworlds_backend
    All tests passed.

    $ build/tests/test_homeworlds_window
    ok 1 /homeworlds/view/homeworld-layout
    ok 2 /homeworlds/view/piece-metrics
    ok 3 /homeworlds/view/setup-bank-buttons
    ok 4 /homeworlds/view/advances-setup
    ok 5 /homeworlds/view/random-ai-after-setup
    ok 6 /homeworlds/window/replaces-skeleton

    $ build/tests/test_desktop_metadata
    # exits 0

    $ build/tests/test_flatpak_manifest
    # exits 0

    $ make test
    ...
    not ok /sgf-view/link-angles - GLib-GObject-FATAL-CRITICAL: invalid (NULL) pointer instance
    make: *** [Makefile:222: test] Error 133

    $ git status --short
     M Makefile
     M doc/OVERVIEW.md
     M doc/execplan-homeworlds.md
     M src/games/homeworlds/homeworlds_app_window.c
     M tests/test_desktop_metadata.sh
     M tests/test_flatpak_manifest.sh
     M tests/test_homeworlds_backend.c
    ?? annotate
    ?? annotate1
    ?? annotate2
    ?? annotate3
    ?? annotate4
    ?? annotate5
    ?? data/icons/hicolor/scalable/apps/io.github.jeromea.ghomeworlds.svg
    ?? data/io.github.jeromea.ghomeworlds.desktop
    ?? data/io.github.jeromea.ghomeworlds.metainfo.xml
    ?? flatpak/io.github.jeromea.ghomeworlds.yaml
    ?? puzzles/boop/
    ?? src/games/homeworlds/homeworlds.png
    ?? src/games/homeworlds/homeworlds_random_ai.c
    ?? src/games/homeworlds/homeworlds_random_ai.h
    ?? src/games/homeworlds/homeworlds_view.c
    ?? src/games/homeworlds/homeworlds_view.h
    ?? tests/test_homeworlds_window.c

The untracked `annotate*` files and `puzzles/boop/` directory are unrelated local artifacts observed during this
revision. Do not include them in Homeworlds commits unless a future task explicitly says to.
The untracked `src/games/homeworlds/homeworlds.png` file is intentional: it is the Homeworlds visual mockup referenced
by this plan and should be included when committing the Homeworlds UI plan/assets.

## Interfaces and Dependencies

The active profile API must remain:

    const GGameAppProfile *ggame_app_profile_get_by_kind(GGameAppKind kind);
    gboolean ggame_app_profile_set_active(const GGameAppProfile *profile);
    const GGameAppProfile *ggame_active_app_profile(void);

The active backend API must remain profile-driven:

    #define GGAME_ACTIVE_GAME_BACKEND (ggame_active_app_profile()->backend)

The Homeworlds profile should continue to identify the backend and custom window hook:

    .kind = GGAME_APP_KIND_HOMEWORLDS
    .id = "homeworlds"
    .app_id = "io.github.jeromea.ghomeworlds"
    .backend = &homeworlds_game_backend
    .ui.create_window = ghomeworlds_app_window_create

The Homeworlds backend must continue to advertise:

    .supports_move_list = FALSE
    .supports_move_builder = TRUE
    .supports_ai_search = TRUE
    .list_good_moves = homeworlds_backend_list_good_moves

A future `homeworlds_view` API may evolve, but it should expose these concepts:

    - bind or receive the active `GGameModel`
    - render the current `HomeworldsPosition`
    - start and reset a `HomeworldsMoveBuilderState`
    - render legal next candidates from `homeworlds_move_builder_list_candidates()`
    - notify the window when `homeworlds_move_builder_build_move()` produces a complete `HomeworldsMove`
    - clear partial selection after a move is applied or cancelled

Do not introduce a full-move-list dependency for human interaction. Do not make Homeworlds a square-grid backend.
Do not remove Checkers/Boop profile features to simplify Homeworlds.

Revision note, 2026-05-13: this document was rewritten because the repository had moved beyond the original
compile-time-game plan. The new plan explicitly preserves the profile registry, all-game Makefile, and Boop
shared-shell support, and narrows the remaining Homeworlds work to the missing playable profile-owned UI. It also
records `src/games/homeworlds/homeworlds.png` as a visual reference whose details are subordinate to the prose
requirements.
