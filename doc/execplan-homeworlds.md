# Add Homeworlds as a second compiled game

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document must be maintained in accordance with `doc/PLANS.md`.

## Purpose / Big Picture

After this change, a developer will be able to build a second branded application from the same repository:
`ghomeworlds`, compiled with `GAME=homeworlds`. A user will be able to start a local two-player Homeworlds game,
interact with a Homeworlds-specific board/system view, take legal turns through the GTK UI, and win or lose according
to the rules in `src/games/homeworlds/RULES.md`.

The first shipped Homeworlds version in this plan is deliberately narrower than the current checkers build. It must
support local play, new-game setup, turn progression, victory detection, and a game-specific visual presentation. It
must also support a deliberately limited AI path, where the backend exposes only a heuristic subset of promising moves
instead of the full legal move space. It does not need puzzles or SGF-based puzzle playback. The visible proof is
that `GAME=homeworlds make` builds a dedicated binary and
`GAME=homeworlds build/bin/ghomeworlds` starts a playable local game where a player can create Homeworlds, take
turns, optionally let the limited AI choose from backend-provided good moves, and see the game end when a player
starts their turn with no ships at their Homeworld.

## Progress

- [x] (2026-04-25 16:05Z) Read `doc/PLANS.md`, `doc/OVERVIEW.md`, `doc/execplan-game-backend-interface.md`, and
      `src/games/homeworlds/RULES.md`, then write this initial ExecPlan.
- [x] (2026-04-25 17:15Z) Complete Milestone 1 by adding `GAME=homeworlds` selection, a branded `ghomeworlds`
      skeleton binary, the stub Homeworlds backend, and backend-selection coverage for the second game.
- [ ] Implement a Homeworlds engine and backend under `src/games/homeworlds/`.
- [ ] Add the missing backend-owned board widget path to shared UI so non-square games can render and accept input.
- [ ] Integrate Homeworlds into the window and new-game flows as a playable local game with limited AI and with
      unsupported features disabled.
- [ ] Add tests and validation commands for both the Homeworlds engine/backend and the full `GAME=homeworlds` build.
- [ ] Update `doc/OVERVIEW.md` as each `src/` milestone lands.

## Surprises & Discoveries

- Observation: the rules folder currently contains only prose rules, not an engine skeleton or test data.
  Evidence: `find src/games/homeworlds -maxdepth 2 -type f` returns only `src/games/homeworlds/RULES.md`.

- Observation: the repository can already mark AI as optional and separate gameplay move listing from future AI move
  selection, but it does not yet have a heuristic “good moves” API and the shared board UI still assumes square-grid
  rendering and, for interactive play, still assumes full move-list support.
  Evidence: `src/game_backend.h` now exposes `supports_move_builder` and `supports_ai_search`, but
  `src/board_view.c` still guards on `backend->supports_square_grid_board`, and
  `src/board_selection_controller.c` logs that it “requires list-move backends”.

- Observation: the product naming is still checkers-branded in several shared build/package variables.
  Evidence: `Makefile` still sets `APP_ID := io.github.jeromea.gcheckers`, `GCHECKERS_BIN := $(BIN_DIR)/gcheckers`,
  and `PUZZLES_INSTALL_DIR := $(DATADIR)/gcheckers/puzzles`.

- Observation: the current main window and SGF controller are still architecturally centered around one board view,
  one SGF timeline, and checkers-derived replay flows.
  Evidence: `src/window.c` owns `BoardView *board_view` and `GGameSgfController *sgf_controller`, and
  `src/sgf_controller.c` requires `BoardView` in its constructor.

- Observation: the existing shared application shell cannot simply be recompiled against a second backend yet.
  Evidence: Milestone 1 needed a separate `src/ghomeworlds.c` GTK skeleton because the current `src/application.c` and
  `src/window.c` still pull in checkers-specific modules and flows.

## Decision Log

- Decision: Homeworlds will ship with a deliberately limited AI based on a new backend callback named
  `list_good_moves`, whose contract is to return a heuristic subset of legal moves ordered best-first.
  Rationale: `src/games/homeworlds/RULES.md` implies a move space that can reach millions of legal moves, and the user
  explicitly stated that the current alpha-beta and full move-list assumptions are not adequate. The shared AI should
  therefore search only backend-curated candidates, while human interaction continues to use exact legality through the
  move builder.
  Date/Author: 2026-04-25 / Codex

- Decision: Homeworlds will use a backend-owned board/system widget instead of trying to squeeze the game into the
  existing square-grid board API.
  Rationale: Homeworlds is played over a dynamic graph of star systems and ships, not a rectangular board of playable
  squares. The earlier backend split intended to support non-square games, but the concrete widget path does not exist
  yet, so this plan must add it as part of Homeworlds.
  Date/Author: 2026-04-25 / Codex

- Decision: SGF remains the single source of truth for move history and replay state, even for Homeworlds.
  Rationale: the window and replay stack are already organized around SGF in-memory state, so adding a second
  backend-owned move log would create divergence for little benefit. If Homeworlds moves do not fit the existing text
  property conventions cleanly, the backend may still store move payloads as opaque binary blobs inside the SGF tree
  and postpone user-facing serialization details until later.
  Date/Author: 2026-04-25 / Codex

- Decision: Milestone 1 uses a branded Homeworlds GTK skeleton binary instead of trying to reuse the full shared
  `GGameApplication` and `GGameWindow` stack immediately.
  Rationale: the current shared application shell is still too checkers-specific to compile cleanly under
  `GAME=homeworlds`. A tiny `src/ghomeworlds.c` binary satisfies the milestone’s build/package-selection goal without
  pretending the later UI milestones are already complete.
  Date/Author: 2026-04-25 / Codex

## Outcomes & Retrospective

Milestone 1 is complete. The repository now has a second compile-time backend selection path, a stub
`src/games/homeworlds/homeworlds_backend.c`, and a branded `build/bin/ghomeworlds` skeleton binary that proves the
build/package split works. The main gap is unchanged: the real shared application shell still cannot host Homeworlds,
which is exactly why the next milestone remains the real engine/backend implementation rather than pretending the UI is
already generic.

## Context and Orientation

This repository is now a multi-game C/GTK codebase with one currently shipped game: checkers. Shared application code
is in top-level `src/`. Game-specific code lives in `src/games/<game>/`. The active game is selected at compile time
through the `GAME` variable in `Makefile`, which defines a preprocessor symbol such as `GGAME_GAME_CHECKERS`. The
selected backend is exposed through `src/active_game_backend.h` and described by the callback table in
`src/game_backend.h`.

The rules for the new game are in `src/games/homeworlds/RULES.md`. In plain terms, Homeworlds is a game of ships and
star systems built from colored pyramids. Each player starts by building a two-star “homeworld” and placing a large
ship beside it. Players lose if they begin their turn with no ships at their homeworld. A turn is either one basic
action in a system where the player has a ship, or a sacrifice of one ship to gain one to three actions of that
ship’s color across any systems where the player has ships. The four action types are construct (green), trade
(blue), attack/steal (red), and move/discover (yellow). Overpopulation by four or more pieces of one color causes a
catastrophe that removes all pieces of that color from the system; if a star is removed, the whole system collapses.

The current backend contract in `src/game_backend.h` already supports three capability flags that matter here:
`supports_move_list`, `supports_move_builder`, and `supports_ai_search`. Checkers currently opts into move lists and
AI. Homeworlds should also opt into AI, but through a new callback named `list_good_moves`. Its contract is different
from `list_moves`: it returns only a heuristic subset of legal moves, ordered best-first for search. Human input still
needs exact legality, so Homeworlds should use move building for interactive play. A “move builder” means a
backend-controlled state machine for interactive move construction, where the backend offers the next legal choices
without materializing all legal moves at once.

The current shared UI is not yet ready for Homeworlds. `src/board_view.c`, `src/board_grid.c`, and related files only
implement the square-grid presentation. `src/window.c` assumes one `BoardView` plus one `GGameSgfController`.
`src/new_game_dialog.c` assumes computer-player configuration is relevant to the active game. `src/puzzle_dialog.c`,
`src/settings_dialog.c`, and parts of the analysis/puzzle flows are still checkers-oriented even though the replay
stack itself is SGF-centered and should remain the history source of truth for Homeworlds too.

The build is also still checkers-branded in important places. `Makefile` names the binary `gcheckers`, the app ID
`io.github.jeromea.gcheckers`, the Flatpak manifest `flatpak/io.github.jeromea.gcheckers.yaml`, and the installed
data path `share/gcheckers`. Homeworlds needs the same shape but with its own names, for example `ghomeworlds`,
`io.github.jeromea.ghomeworlds`, and `flatpak/io.github.jeromea.ghomeworlds.yaml`.

When this plan says “board widget,” it means the main GTK widget that shows the current position and accepts move
input. When this plan says “system view,” it means a Homeworlds-specific board widget that renders star systems,
ships, layered system placement, selections, and pending move-builder state. When this plan says “timeline,” it means
the user-visible move history and replay model stored in the in-memory SGF tree, even if some Homeworlds move payloads
are represented as opaque binary blobs rather than text-like SGF moves.

## Plan of Work

The work begins in the build and backend selection layer. In `Makefile`, introduce a `homeworlds` branch alongside
the existing `checkers` branch. That branch must define the active backend symbol, source directory
`src/games/homeworlds`, game-specific binary name `ghomeworlds`, game-specific app ID, and any game-specific tool
names or packaging filenames. The result must be that `GAME=homeworlds make` builds a separate branded application
without changing the default checkers build. Because Homeworlds does not ship puzzle generation or AI tools in the
first milestone, the Makefile must allow those targets to be absent or no-op for that game instead of forcing
checkers-only tools into every build.

Next, create the Homeworlds engine under `src/games/homeworlds/`. Start with explicit data structures that match the
rules prose: pyramid colors, pyramid sizes, bank counts, star systems, homeworld ownership, ships, and turn state.
Implement a position type that can express a dynamic graph of systems rather than a fixed board. Implement rule
helpers for setup legality, access to action colors in a system, movement connectivity (“systems are connected if they
do not share a star size”), sacrificing for one-to-three same-color actions, capture legality by size, catastrophe
resolution, empty-system cleanup, and the start-of-turn loss check. Keep the implementation engine-first and UI-agnostic.

Then add a Homeworlds backend adapter in `src/games/homeworlds/homeworlds_backend.c` and
`src/games/homeworlds/homeworlds_backend.h`. The backend must set `supports_move_list = FALSE`,
`supports_move_builder = TRUE`, and `supports_ai_search = TRUE`, and it must implement a new AI callback named
`list_good_moves`. The contract for `list_good_moves` is that it returns a heuristic subset of legal moves, ordered
best-first, and it is allowed to omit many legal moves in order to keep the branching factor tractable. Its move
builder must represent the staged nature of Homeworlds turns: setup choices, choosing basic action versus sacrifice,
choosing systems, choosing ships, choosing targets, and optionally triggering catastrophes. The backend must expose
enough information for a custom widget to ask “what can the player do next?” and to build a final move object without
ever enumerating the full legal move space.

After the engine exists, add the missing shared board-widget abstraction. Replace the hard dependency on `BoardView`
inside `src/window.c` and `src/sgf_controller.c` with a thin generic board-surface interface in shared `src/`, for
example a controller object or interface that can either wrap the existing square-grid `BoardView` or a new
Homeworlds-specific `HomeworldsView`. The important result is not the exact type name; it is that the window can own
“the active game’s board widget” without assuming square-grid internals. The checkers path must continue to use the
existing square-grid widgets through an adapter, while Homeworlds must provide its own GTK widget in
`src/games/homeworlds/homeworlds_view.c` and `src/games/homeworlds/homeworlds_view.h`.

The Homeworlds view must visualize systems, stars, ships, and the current move-builder state. It does not need to be
beautiful in the first milestone, but it must be functionally clear. Do not draw explicit connection lines between
systems. Instead, use the traditional layered Homeworlds presentation guided by the two players’ homeworld star sizes.
For example, if player 0’s homeworld uses star sizes 1 and 3, and player 1’s homeworld uses star sizes 2 and 3, then
the layer nearest player 0 contains the systems one hop from player 0, which therefore use only size-2 stars; the
layer nearest player 1 contains the systems one hop from player 1, which therefore use only size-1 stars; and a
central layer contains everything else. If the two players are more directly connected, collapse the middle into one
central layer containing all remaining systems. On each system, ships (triangles of 3 different sizes) should be
pointing away from their owner, and be aligned at the right of the star from the owner's perspective.
The user must be able to click systems, ships, and available action targets to drive the move builder. The widget
must also make pending sacrifice counts and chosen action types visible, because those are essential parts of
Homeworlds turns.

Once the custom widget path exists, integrate Homeworlds into the window and new-game flows. `src/new_game_dialog.c`
must stop assuming that every backend’s AI is based on exhaustive move generation. For Homeworlds, the dialog should
offer human-vs-human and human-vs-computer start flows plus any game-specific setup assistance needed to create legal
initial homeworlds. `src/window.c` must gate actions and menus by backend capabilities: keep limited AI controls
available for Homeworlds, keep SGF-backed history/navigation as the internal source of truth, but still disable or
hide puzzle actions and any external SGF serialization actions that are not implemented yet. The main window must
still be usable and coherent when those features are absent.

The initial Homeworlds replay/history handling should stay minimal, but it must still use SGF as the in-memory source
of truth. Reuse the shared SGF tree and controller path so the window can show turn progress, navigation, and the last
move from one canonical history model. Do not require polished Homeworlds SGF import/export in the first
implementation; if needed, represent Homeworlds move payloads in the SGF tree as opaque binary blobs or backend-owned
properties. The shared code should tolerate a backend that participates in SGF-based in-memory history without yet
supporting external SGF serialization or puzzles.

Finally, add targeted tests. At the engine level, add pure rule tests under `tests/` for setup legality, construct,
trade, attack, move/discover, sacrifice, catastrophe resolution, system collapse, and start-of-turn loss detection.
At the backend level, add tests proving the capability flags for Homeworlds, proving that the move builder can advance
through at least one full legal turn without using `list_moves`, and proving that `list_good_moves` returns a
best-first subset rather than attempting exhaustive enumeration. At the UI level, add at least one focused window test
that builds with `GAME=homeworlds`, starts a Homeworlds game, and verifies that AI remains available while puzzle
actions and any not-yet-implemented external serialization actions are absent or not sensitive.

## Milestones

### Milestone 1: build and backend selection skeleton

At the end of this milestone, the repository can compile a Homeworlds-flavored application skeleton. The engine is not
playable yet, but `GAME=homeworlds make` produces a branded binary and links a stub backend that explicitly reports
“move builder plus heuristic AI candidates”.

Edit `Makefile` so the `ifeq ($(GAME),...)` block has a `homeworlds` branch with its own source directory, binary
name, application ID, and manifest path. Add `src/games/homeworlds/homeworlds_backend.c` and `.h` with a stub backend
object, and wire `src/active_game_backend.h` to select it under a new `GGAME_GAME_HOMEWORLDS` define. Where the build
still assumes checkers-only tools, either make the target conditional on the active game or add a generic wrapper that
skips unsupported tools for Homeworlds with a clear debug message.

Run:

    cd /home/jerome/Data/gcheckers
    GAME=homeworlds make

Expected proof:

    build/bin/ghomeworlds
    build/lib/libgame.a

Acceptance for this milestone is that the Homeworlds build compiles cleanly and the backend-selection tests can name a
second backend without regressing the default checkers build.

### Milestone 2: Homeworlds engine and move-builder backend

At the end of this milestone, the repository contains a pure Homeworlds rules engine plus a backend adapter that can
represent legal turn construction without full move enumeration and can also expose heuristic AI candidates through
`list_good_moves`.

Create engine files under `src/games/homeworlds/`, at minimum:

    src/games/homeworlds/homeworlds_types.h
    src/games/homeworlds/homeworlds_game.c
    src/games/homeworlds/homeworlds_game.h
    src/games/homeworlds/homeworlds_move_builder.c
    src/games/homeworlds/homeworlds_move_builder.h

The engine must represent bank inventory, systems, star sizes, ships, player ownership, and action state. The move
builder must support both setup and ordinary turns. Do not flatten the rules into one giant parser; keep helpers small
and named after game concepts so the rule tests stay readable.

Run:

    cd /home/jerome/Data/gcheckers
    make test_homeworlds_game test_homeworlds_backend
    build/tests/test_homeworlds_game
    build/tests/test_homeworlds_backend

Acceptance is that the rule tests pass for the core action types and catastrophes, and the backend tests prove
Homeworlds uses `supports_move_builder = TRUE`, `supports_move_list = FALSE`, `supports_ai_search = TRUE`, and a
working `list_good_moves` implementation.

### Milestone 3: backend-owned board widget path

At the end of this milestone, the window can host either the existing square-grid checkers board or a backend-owned
custom widget. This is the enabling milestone for Homeworlds to be visible and interactive.

Introduce a generic board-surface layer in shared `src/` that the window and the SGF-centered replay/timeline
controller can talk to without naming `BoardView` directly. Adapt the current checkers path to that layer first so
behavior does not change for checkers. Then add `src/games/homeworlds/homeworlds_view.c` and `.h` implementing the
Homeworlds system view. The widget must render enough state to let a user choose systems, ships, and action targets in
a staged way.

Run:

    cd /home/jerome/Data/gcheckers
    make test_board_view test_window
    build/tests/test_window -p /gcheckers-window/drawer-visibility-actions

Acceptance is that the checkers board tests still pass and the codebase no longer assumes that every backend uses
`BoardView`.

### Milestone 4: playable Homeworlds window integration

At the end of this milestone, `GAME=homeworlds build/bin/ghomeworlds` starts a real local game. Players can complete
setup, take legal turns, trigger catastrophes, and win or lose.

Update `src/window.c`, `src/new_game_dialog.c`, and any action/menu setup so backend capabilities control what the UI
offers. For Homeworlds, disable or hide:

    - computer-player controls
    - analysis actions
    - puzzle actions
    - external SGF load/save/import actions unless a serializer exists by then

Add enough status text to show turn ownership, whether the player is in setup, and any pending sacrifice/action state.
Homeworlds must still use the in-memory SGF timeline internally, even if external SGF serialization is not implemented
yet.

Run:

    cd /home/jerome/Data/gcheckers
    GAME=homeworlds make
    GAME=homeworlds build/bin/ghomeworlds

Manual acceptance scenario:

    1. Start a new Homeworlds game.
    2. Complete legal homeworld setup for both players.
    3. Take at least one construct, trade, attack, and move/discover turn across the session.
    4. Trigger a catastrophe and observe the correct pieces removed.
    5. Let the limited AI choose a move in at least one position and observe that the game continues normally.
    6. Reach a position where one player begins their turn with no ships at their homeworld and observe the game end.

Acceptance is that each of those steps is possible in the GUI and that unsupported actions stay absent or disabled.

### Milestone 5: packaging, docs, and cross-game validation

At the end of this milestone, Homeworlds is a first-class compiled game in the repository, with its own branding,
docs, and build validation.

Add the Homeworlds packaging metadata beside the checkers files:

    flatpak/io.github.jeromea.ghomeworlds.yaml
    data/io.github.jeromea.ghomeworlds.desktop
    data/io.github.jeromea.ghomeworlds.metainfo.xml
    data/icons/hicolor/scalable/apps/io.github.jeromea.ghomeworlds.svg

Update `doc/OVERVIEW.md` so the new shared board-widget abstraction and the Homeworlds modules are documented. Update
any README/build instructions that currently mention only checkers.

Run:

    cd /home/jerome/Data/gcheckers
    make
    GAME=homeworlds make
    make test_game_backend test_game_model
    GAME=homeworlds make test_homeworlds_game test_homeworlds_backend

Acceptance is that both games build from the same repository, each with its own branded binary, and the Homeworlds
documentation is accurate enough for a novice contributor to find the engine, backend, widget, and packaging files.

## Concrete Steps

The commands in this section are the exact commands to run from the repository root as work proceeds. Update this
section with new commands and observed outputs at every milestone.

For the initial research pass used to write this plan:

    cd /home/jerome/Data/gcheckers
    sed -n '1,240p' doc/PLANS.md
    sed -n '1,260p' src/games/homeworlds/RULES.md
    sed -n '1,260p' doc/execplan-game-backend-interface.md
    find src/games/homeworlds -maxdepth 2 -type f | sort
    rg -n "supports_square_grid_board|BoardView|supports_move_builder|supports_ai_search" src

Expected evidence from the research pass:

    src/games/homeworlds/RULES.md

    src/board_view.c:230:  g_return_if_fail(backend->supports_square_grid_board);
    src/board_selection_controller.c:... Board selection controller requires list-move backends

As implementation proceeds, add the milestone-specific build and test commands from the milestone text above with the
actual observed output snippets.

## Validation and Acceptance

Validation is split between pure rule tests, backend capability tests, UI/window tests, and a manual gameplay run.

For rule validation, the important observable behaviors are:

- legal setup accepts valid binary-star homeworlds and rejects illegal ones
- construct takes the smallest available pyramid of the matching color
- trade swaps color but preserves size
- attack requires sufficient ship size
- movement/discovery obeys “no shared star size” connectivity
- catastrophes remove all pieces of the overpopulated color and collapse systems when a star is destroyed
- a player loses when their turn begins with no ships at their homeworld

For UI validation, the important observable behaviors are:

- `GAME=homeworlds build/bin/ghomeworlds` opens a Homeworlds-branded application, not the checkers one
- the central board area renders Homeworlds systems and ships, not a square checkerboard
- unsupported features such as puzzles and external serialization are hidden or disabled instead of failing at runtime
- a human can complete setup and play turns without the program needing to enumerate all legal moves globally

For cross-game safety, always rerun the default checkers build after a shared-UI milestone. Acceptance is not just
that Homeworlds works, but that `make` for the default checkers build still succeeds and the focused regression tests
named in the milestones continue to pass.

## Idempotence and Recovery

All file-creation steps in this plan are additive and safe to repeat. Re-running `make` or `GAME=homeworlds make`
should rebuild only what changed. If a milestone stops halfway through, the recovery path is to finish wiring the new
files into `Makefile`, rerun the focused build for that milestone, and only then continue to the next milestone.

The main risky area is the shared board-widget abstraction, because it touches the existing checkers window path. Keep
that migration parallel and reversible: first add a generic wrapper around the current checkers `BoardView`, prove
checkers still passes its focused tests, and only then attach the new Homeworlds widget through the same abstraction.

Do not delete the existing square-grid code during the Homeworlds bring-up. Treat it as the stable checkers adapter
until Homeworlds is fully working and both games are validated.

## Artifacts and Notes

Important evidence snippets to preserve as implementation proceeds:

    GAME=homeworlds make
    ...
    cc ... -DGGAME_GAME_HOMEWORLDS -o build/bin/ghomeworlds ...

    build/tests/test_homeworlds_game
    All tests passed.

    build/tests/test_homeworlds_backend
    All tests passed.

    GAME=homeworlds build/bin/ghomeworlds
    [manual run: complete setup, perform actions, trigger catastrophe, observe victory]

Add short transcripts here as each milestone lands. Keep them concise and focused on proof rather than on full build
logs.

## Interfaces and Dependencies

In `src/games/homeworlds/homeworlds_backend.h`, define:

    extern const GameBackend homeworlds_game_backend;

In `src/active_game_backend.h`, add:

    #elif defined(GGAME_GAME_HOMEWORLDS)
    #include "games/homeworlds/homeworlds_backend.h"
    #define GGAME_ACTIVE_GAME_BACKEND (&homeworlds_game_backend)

In `src/games/homeworlds/homeworlds_game.h`, define explicit Homeworlds engine types. The exact fields may evolve, but
the final API must include functions in this shape:

    void homeworlds_game_init(HomeworldsGame *game);
    void homeworlds_game_clear(HomeworldsGame *game);
    gboolean homeworlds_game_apply_move(HomeworldsGame *game, const HomeworldsMove *move);
    GameBackendOutcome homeworlds_game_outcome(const HomeworldsGame *game);

In `src/games/homeworlds/homeworlds_move_builder.h`, define a backend-owned move builder with functions in this shape:

    gboolean homeworlds_move_builder_init(const HomeworldsGame *game, GameBackendMoveBuilder *out_builder);
    GameBackendMoveList homeworlds_move_builder_list_candidates(const GameBackendMoveBuilder *builder);
    gboolean homeworlds_move_builder_step(GameBackendMoveBuilder *builder, gconstpointer candidate);
    gboolean homeworlds_move_builder_is_complete(const GameBackendMoveBuilder *builder);
    gboolean homeworlds_move_builder_build_move(const GameBackendMoveBuilder *builder, gpointer out_move);
    void homeworlds_move_builder_clear(GameBackendMoveBuilder *builder);

In shared `src/`, add a board-surface abstraction that allows `src/window.c` to own one game-specific board widget
without naming `BoardView` directly. The exact type name may differ, but it must cover:

    - retrieving the GTK widget to pack into the window
    - binding the active `GGameModel`
    - updating the rendered position
    - enabling/disabling input
    - clearing any in-progress selection or move-builder state
    - installing a move-completion callback

The Homeworlds board widget should implement layered system placement instead of explicit edge drawing. The placement
algorithm should be guided first by the two players’ homeworld star sizes, then by one-hop reachability from those
homeworld layers, and only then by a central catch-all layer for the remaining systems.

The Homeworlds backend must not implement full exhaustive move listing for AI. Instead, it must provide:

    GameBackendMoveList (*list_good_moves)(gconstpointer position,
                                           guint max_count,
                                           guint depth_hint);

The contract of `list_good_moves` is:

    - every returned move is legal
    - the list is a subset of the full legal move space
    - the moves are ordered best-first according to backend heuristics
    - callers may treat omission as intentional pruning, not as “no legal move exists”

Human interaction must continue to rely on the move builder, not on `list_good_moves`.

Revision note: this revision replaces the earlier backend-owned move-log idea with a requirement that the shared SGF
tree remain the single history source of truth for Homeworlds, even if move payloads are opaque blobs, and it replaces
the earlier node-and-edge rendering wording with the traditional layered Homeworlds system layout.
