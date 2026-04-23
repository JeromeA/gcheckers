# Split checkers into a selectable game backend

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document must be maintained in accordance with `doc/PLANS.md`.

## Purpose / Big Picture

After this change, the program will no longer be architecturally tied to checkers. A developer will be able to build
the same GTK application, SGF-style timeline UI, board widget, and alpha-beta search against a selected game backend.
The selected backend is chosen at compile time with a preprocessor define, and the active backend exposes one structure
of callbacks and metadata that shared code uses instead of calling checkers-specific functions directly.

The first implementation does not add a second playable game. It moves checkers into `src/games/checkers/`, introduces
a generic game backend interface in shared `src/` code, and proves that compiling with the checkers backend still
produces the current application behavior. The visible proof is that `make`, the existing checkers tests, and the
existing GTK application still work when the Makefile defines the active game as checkers. The architectural proof is
that shared files such as `src/window.c`, `src/board_view.c`, and `src/ai_alpha_beta.c` no longer include
checkers-only headers like `game.h`, `board.h`, `rulesets.h`, or `checkers_model.h`; they include the generic backend
API instead.

## Progress

- [x] (2026-04-23 00:00Z) Survey `doc/PLANS.md`, `doc/OVERVIEW.md`, `Makefile`, and key engine/model headers, then
      write this initial ExecPlan.
- [ ] Add a generic backend API in shared `src/` code and a compile-time active-backend selection header.
- [ ] Introduce a checkers backend adapter while leaving existing checkers engine files in place.
- [ ] Move checkers-specific engine, ruleset, puzzle, notation, and model code under `src/games/checkers/`.
- [ ] Convert shared UI, SGF, puzzle shell, settings, and AI code to use generic backend types and callbacks.
- [ ] Update the Makefile so selecting another backend is a matter of changing one compile define and linked backend
      source list.
- [ ] Rename or wrap checkers-specific tests so they compile through the checkers backend and add backend-interface
      tests.
- [ ] Update `doc/OVERVIEW.md` after each `src/` milestone so the source architecture remains accurate.
- [ ] Run the full build and focused test suite; document any unrelated pre-existing test failures.

## Surprises & Discoveries

- Observation: the current `Game` struct already has a tiny callback shape, but the public types around it are still
  checkers-specific.
  Evidence: `src/game.h` defines `struct Game` with `print_state` and `available_moves` callbacks, but its state and
  moves are `GameState`, `CheckersBoard`, `CheckersMove`, `CheckersRules`, and `CheckersWinner`.

- Observation: the shared AI is not actually game-neutral today.
  Evidence: `src/ai_alpha_beta.c` directly inspects `CheckersPiece`, `CheckersColor`, `CheckersBoard`, promotion-row
  advancement, and checkers Zobrist hashing.

- Observation: the build already centralizes engine source selection enough to make a staged migration practical.
  Evidence: `Makefile` defines `SRCS := src/board.c src/board_geometry.c src/game.c ... src/checkers_model.c` and
  reuses that list across libraries and many tests.

- Observation: `doc/OVERVIEW.md` identifies many checkers-specific modules that currently sit at the top of `src/`.
  Evidence: the overview has separate sections for `Board primitives`, `Board geometry`, `Constants`, `Game engine`,
  `Ruleset catalog`, `Move generation`, and `GTK model wrapper`, all describing checkers concepts.

## Decision Log

- Decision: place game-specific source under `src/games/<game-id>/`, starting with `src/games/checkers/`.
  Rationale: this keeps all C source under the existing `src/` include and build discipline while making the ownership
  boundary obvious. It also avoids relying on the currently untracked top-level `games/` directory for source code.
  Date/Author: 2026-04-23 / Codex

- Decision: keep compile-time backend selection, not runtime plugin loading.
  Rationale: the user requested that compiling a different game be a matter of using a different `#define` and linking
  the game-specific code. A compile-time selection header is simple C, produces one application binary per game, and
  avoids a dynamic plugin loader while the codebase is still being generalized.
  Date/Author: 2026-04-23 / Codex

- Decision: define one public `GameBackend` structure in shared code and one active-backend selection header.
  Rationale: shared UI and AI need a stable contract, but each game needs freedom to own its internal structs,
  notation, variants, board geometry, and persistence details. A callback table makes the boundary explicit and keeps
  future game additions local to `src/games/<game-id>/` plus one Makefile selection.
  Date/Author: 2026-04-23 / Codex

- Decision: use opaque generic handles and byte-sized move storage at the shared boundary.
  Rationale: shared code must not know that checkers moves are paths through playable-square indexes. The backend can
  still store checkers paths internally, but the shared layer should treat a position as an opaque value and a move as
  a bounded byte payload with backend callbacks for equality, formatting, validation, and application.
  Date/Author: 2026-04-23 / Codex

- Decision: migrate in compatibility layers before physically moving files.
  Rationale: a large directory move makes diffs noisy and can hide behavioral regressions. Adding a generic wrapper
  around the existing checkers engine first lets tests prove the interface, then the file move can be mostly mechanical.
  Date/Author: 2026-04-23 / Codex

## Outcomes & Retrospective

This plan is not implemented yet. It records the target design and a staged path for moving checkers behind a generic
backend boundary while preserving current behavior. The expected final outcome is a repository where the checkers
backend is selected by a compile define, shared code depends only on generic backend headers, and adding a second game
requires implementing a new backend source directory plus Makefile source selection rather than rewriting the UI.

## Context and Orientation

The current repository is a GTK checkers application. The main window lives in `src/window.c` and owns a
`GCheckersModel`, a board view, player controls, SGF timeline controls, puzzle mode, and analysis UI. The GTK model
wrapper lives in `src/checkers_model.c` and `src/checkers_model.h`; it wraps the engine and emits GObject signals so
the UI can repaint when moves are applied.

The current checkers engine is spread across top-level `src/` files. `src/board.c` and `src/board.h` define checkers
board storage and piece values. `src/board_geometry.c` and `src/board_geometry.h` precompute diagonal rays for
playable checkers squares. `src/game.c` and `src/game.h` define `Game`, `GameState`, `CheckersMove`, `CheckersRules`,
and winner logic. `src/move_gen.c` generates legal checkers moves. `src/rulesets.c`, `src/rulesets.h`, and
`src/ruleset.h` define American, International, and Russian checkers variants. `src/game_print.c` formats positions
and moves for tests and tools.

The current AI is also checkers-specific even though much of the search shape is reusable. `src/ai_alpha_beta.c`
implements alpha-beta search, move scoring, cancellation, progress callbacks, and transposition-table use. However,
it evaluates material by reading checkers piece enums and board squares directly, and `src/ai_zobrist.c` hashes
checkers board positions. The future shared AI must keep the alpha-beta control flow but call backend callbacks for
move generation, applying moves, undoing or copying positions, static evaluation, terminal scoring, hashing, and move
ordering.

The current board UI has reusable and checkers-specific parts mixed together. `src/board_view.c`,
`src/board_grid.c`, `src/board_square.c`, `src/board_selection_controller.c`, `src/piece_palette.c`,
`src/man_paintable.c`, and `src/board_move_overlay.c` render a square board, pieces, selection highlights, legal move
hints, and winner overlays. A future generic board UI can still assume a rectangular grid of squares, but it must ask
the backend whether a square is playable, what piece occupies it, how to draw that piece, how to interpret a click,
and how to format move overlays.

The current SGF layer lives in `src/sgf_tree.c`, `src/sgf_io.c`, `src/sgf_move_props.c`, and
`src/sgf_controller.c`. In this plan, "SGF-style" means the shared tree/timeline structure remains useful, but the
backend owns game-specific properties, move parsing, move formatting, setup-position serialization, and ruleset or
variant identifiers. If a future game cannot use real SGF semantics, the shared tree can still serve as an internal
timeline format as long as the backend provides import/export callbacks.

The current puzzle system is checkers-specific in data and semantics. `src/puzzle_catalog.c` scans
`puzzles/<ruleset>/puzzle-####.sgf`; `src/puzzle_dialog.c` shows checkers ruleset choices; `src/puzzle_progress.c`
records puzzle IDs and outcomes; and `src/window.c` validates puzzle moves against checkers SGF main lines. The shared
application can keep a generic puzzle shell and progress storage, but each game backend must define its puzzle
variants, puzzle file parser, solution validation, and next-puzzle catalog rules.

The build is controlled by `Makefile`. It currently puts top-level checkers engine files into `SRCS` and links them
into the main app, tools, and many tests. This is the main integration point for compile-time backend selection.

When this plan says "backend", it means a game-specific module that implements a C structure of callbacks used by
shared code. When this plan says "active backend", it means the one backend selected for this build by a compile
define such as `GCHECKERS_GAME_CHECKERS`. When this plan says "opaque", it means shared code holds a pointer or byte
buffer but does not inspect the fields inside; only backend callbacks understand that memory.

## Target Architecture

Add shared generic headers in `src/`:

    src/game_backend.h
    src/active_game_backend.h
    src/game_model.c
    src/game_model.h
    src/ai_search.c
    src/ai_search.h
    src/ai_transposition_table.c
    src/ai_transposition_table.h

`src/game_backend.h` defines the shared C ABI. The exact names may be adjusted during implementation, but the final
shape must include these concepts:

    typedef struct GameBackend GameBackend;
    typedef struct GameBackendVariant GameBackendVariant;
    typedef struct GamePosition GamePosition;
    typedef struct GameMove GameMove;
    typedef struct GameMoveList GameMoveList;

    typedef enum {
      GAME_OUTCOME_ONGOING = 0,
      GAME_OUTCOME_SIDE_0_WIN,
      GAME_OUTCOME_SIDE_1_WIN,
      GAME_OUTCOME_DRAW
    } GameOutcome;

    typedef struct {
      const char *id;
      const char *name;
      const char *short_name;
      const char *summary;
    } GameBackendVariant;

    typedef struct {
      const char *id;
      const char *display_name;
      guint max_sides;
      guint max_board_rows;
      guint max_board_cols;
      gsize position_size;
      gsize move_size;

      guint (*variant_count)(void);
      const GameBackendVariant *(*variant_at)(guint index);
      const GameBackendVariant *(*variant_by_short_name)(const char *short_name);

      void (*position_init)(GamePosition *position, const GameBackendVariant *variant);
      void (*position_clear)(GamePosition *position);
      void (*position_copy)(GamePosition *dest, const GamePosition *src);
      GameOutcome (*position_outcome)(const GamePosition *position);
      guint (*position_turn)(const GamePosition *position);

      GameMoveList *(*list_moves)(const GamePosition *position);
      void (*move_list_free)(GameMoveList *moves);
      guint (*move_list_count)(const GameMoveList *moves);
      const GameMove *(*move_list_get)(const GameMoveList *moves, guint index);
      gboolean (*moves_equal)(const GameMove *left, const GameMove *right);
      gboolean (*apply_move)(GamePosition *position, const GameMove *move);

      gint (*evaluate_static)(const GamePosition *position);
      gint (*terminal_score)(GameOutcome outcome, guint ply_depth);
      guint64 (*hash_position)(const GamePosition *position);

      gboolean (*format_move)(const GameMove *move, char *buffer, gsize size);
      gboolean (*parse_move)(const char *text, GameMove *out_move);
    };

This interface is intentionally lower-level than the current `GCheckersModel`. Shared UI and AI can call it without
knowing checkers-specific structs. Each backend controls its own `position_size` and `move_size`; shared allocation
helpers allocate enough bytes and cast them only inside backend code. If C ergonomics make incomplete opaque structs
awkward, use fixed storage wrappers such as:

    typedef struct {
      guint8 bytes[GAME_BACKEND_MAX_POSITION_BYTES];
    } GamePositionStorage;

and require the backend to report and validate the used sizes. The implementation must choose one safe strategy and
document it in `src/game_backend.h`.

`src/active_game_backend.h` is the only shared header that knows compile-time backend names. It should look like this
in spirit:

    #ifndef ACTIVE_GAME_BACKEND_H
    #define ACTIVE_GAME_BACKEND_H

    #include "game_backend.h"

    #if defined(GCHECKERS_GAME_CHECKERS)
    #include "games/checkers/checkers_backend.h"
    #define GCHECKERS_ACTIVE_GAME_BACKEND (&checkers_game_backend)
    #else
    #error "No game backend selected. Define GCHECKERS_GAME_CHECKERS or another backend define."
    #endif

    #endif

The important rule is that application code includes `active_game_backend.h` only at composition roots such as
`src/application.c`, `src/game_model.c`, tools, or tests that intentionally bind to the selected game. Most shared
modules should receive a `const GameBackend *backend` pointer from their owner instead of including the active
selection header themselves. This keeps shared code testable with small fake backends.

Move checkers-specific code under:

    src/games/checkers/board.c
    src/games/checkers/board.h
    src/games/checkers/board_geometry.c
    src/games/checkers/board_geometry.h
    src/games/checkers/checkers_backend.c
    src/games/checkers/checkers_backend.h
    src/games/checkers/checkers_constants.h
    src/games/checkers/game.c
    src/games/checkers/game.h
    src/games/checkers/game_print.c
    src/games/checkers/move_gen.c
    src/games/checkers/ruleset.h
    src/games/checkers/rulesets.c
    src/games/checkers/rulesets.h
    src/games/checkers/position_format.c
    src/games/checkers/position_format.h
    src/games/checkers/puzzle_catalog.c
    src/games/checkers/puzzle_catalog.h
    src/games/checkers/puzzle_generation.c
    src/games/checkers/puzzle_generation.h
    src/games/checkers/ai_zobrist.c
    src/games/checkers/ai_zobrist.h

The exact list may change as implementation reveals more checkers-only files, but every file that exposes
`Checkers*`, checkers piece enums, checkers ruleset names, checkers move paths, or checkers board geometry must end
under `src/games/checkers/` or become a compatibility wrapper scheduled for deletion.

Rename shared abstractions as they are generalized. `GCheckersModel` should become `GGameModel` or `GameModel` in
`src/game_model.c` and `src/game_model.h`, with signal names and API terms that refer to "game", "position", "move",
"side", and "variant" rather than "checkers". The application and binary can keep the `gcheckers` name for now; this
plan is about source architecture, not product branding.

## Plan of Work

### Milestone 1: introduce the backend contract without moving files

Create `src/game_backend.h` with the generic types and callback table. Keep it small enough to compile quickly, but
complete enough to support the current checkers engine through an adapter. Add `src/active_game_backend.h` with a
single supported branch for `GCHECKERS_GAME_CHECKERS`.

Add `src/games/checkers/checkers_backend.h` and `src/games/checkers/checkers_backend.c` initially as adapters that
include the existing top-level `src/game.h`, `src/board.h`, `src/rulesets.h`, and AI support headers. This first
adapter may call existing checkers functions directly. It must expose:

    extern const GameBackend checkers_game_backend;

Update the Makefile so `CFLAGS` includes `-DGCHECKERS_GAME_CHECKERS` by default. Do not move existing engine files in
this milestone. Add a small test, for example `tests/test_game_backend.c`, that includes `active_game_backend.h`,
gets `GCHECKERS_ACTIVE_GAME_BACKEND`, verifies the backend ID is `checkers`, verifies the variant list contains
`american`, `international`, and `russian`, creates an initial position for one variant, lists legal moves, applies
one move, and formats that move. This proves the callback table is useful before any larger refactor.

Acceptance for this milestone is that `make test_game_backend`, `make`, and the existing `make test_game` pass, and
the new test fails if `-DGCHECKERS_GAME_CHECKERS` is removed.

### Milestone 2: create a generic model wrapper beside `GCheckersModel`

Add `src/game_model.c` and `src/game_model.h` as a generic GObject wrapper around `const GameBackend *` plus one
current position. It should initially be parallel to `GCheckersModel`, not a replacement. Its API should expose
generic operations:

    GameModel *game_model_new(const GameBackend *backend);
    void game_model_reset(GameModel *self, const GameBackendVariant *variant);
    GameMoveList *game_model_list_moves(GameModel *self);
    gboolean game_model_apply_move(GameModel *self, const GameMove *move);
    const GamePosition *game_model_peek_position(GameModel *self);
    const GameBackend *game_model_peek_backend(GameModel *self);
    char *game_model_format_status(GameModel *self);

Keep GObject signals equivalent to the current model's state-change signal so UI migration can happen incrementally.
Add tests that create a `GameModel` for the active backend, apply one legal move, and verify the state-change signal
fires. Do not remove `GCheckersModel` yet.

Acceptance for this milestone is that both `tests/test_checkers_model.c` and the new `tests/test_game_model.c` pass.
This preserves the old path while proving the new path can own state and emit UI-friendly signals.

### Milestone 3: generalize alpha-beta search behind backend callbacks

Introduce `src/ai_search.c` and `src/ai_search.h` as the generic search engine. It should preserve the current search
features: max depth, cancellation callback, progress callback, scored move list, node counters, and transposition
table counters. Replace direct board inspection with backend callbacks:

    list_moves
    position_copy
    apply_move
    position_outcome
    evaluate_static
    terminal_score
    hash_position
    moves_equal

Keep `src/ai_alpha_beta.c` as a compatibility wrapper for checkers during this milestone if that reduces risk. The
wrapper can convert `Game` and `CheckersMove` calls into the generic search API through the checkers backend. Once the
window and model no longer use checkers APIs, the wrapper can move under `src/games/checkers/` or be deleted.

The transposition table entry currently stores `CheckersMove`. Make it store backend move bytes plus `move_size`, or
store a fixed maximum move payload with the active backend's move size. The table must not include `CheckersMove` in
its public header after this milestone. Add tests that run the generic search on the checkers backend and compare its
chosen move and analysis count shape against the old checkers API for a simple position.

Acceptance for this milestone is that `tests/test_ai_transposition_table.c`, `tests/test_position_search.c`, and the
new generic AI tests pass. The user-visible application should still be unchanged because the old model can still call
the wrapper.

### Milestone 4: migrate shared UI from `GCheckersModel` to `GameModel`

Convert UI modules that should be shared across games to use `GameModel`, `GameBackend`, `GamePosition`, and
`GameMove`. Start with the smallest closed path: `src/board_view.c`, `src/board_grid.c`, `src/board_square.c`,
`src/board_selection_controller.c`, and `src/board_move_overlay.c`. These modules currently assume checkers playable
square indexes and checkers pieces. Add backend callbacks needed by board rendering:

    guint (*board_rows)(const GamePosition *position);
    guint (*board_cols)(const GamePosition *position);
    gboolean (*board_square_playable)(const GamePosition *position, guint row, guint col);
    gboolean (*board_square_piece)(const GamePosition *position, guint row, guint col, GamePieceView *out_piece);
    gboolean (*move_origin_destinations)(const GameMoveList *moves, ...);

Define `GamePieceView` in shared code as the minimum drawing information the UI needs, such as side index, piece kind
index, and whether the piece is promoted or special. For checkers, this maps white man, black man, white king, and
black king to the existing piece paintable. Keep the existing visual result for checkers.

Then migrate `src/window.c`, `src/player_controls_panel.c`, and `src/analysis_graph.c` to ask `GameModel` and
`GameBackend` for status, legal moves, AI analysis, and side labels. Keep `PlayerControlsPanel` generic by referring
to side 0 and side 1 labels supplied by the backend, not hard-coded white and black labels.

Acceptance for this milestone is that `build/tests/test_board_view`, `build/tests/test_window`, and
`build/tests/test_player_controls_panel` compile without including checkers-only headers from shared UI files. Running
`build/bin/gcheckers` with the default checkers backend should still show a normal checkers board and allow a move.

### Milestone 5: migrate SGF and puzzle hooks to backend ownership

Keep the shared tree/timeline code in `src/sgf_tree.c` and the shared view code in `src/sgf_view*.c`, but move
checkers-specific SGF parsing and formatting into backend callbacks. The backend must own:

    parsing a move property into a GameMove
    writing a GameMove into a property string
    parsing setup properties into a GamePosition
    writing setup properties from a GamePosition
    mapping a variant to and from a persistent short name

Rename shared APIs if necessary so they no longer promise real SGF compliance for all games. For example, keep file
names as `.sgf` for existing checkers compatibility, but make shared code talk about "game record tree" where the
semantics are backend-defined.

Move checkers puzzle catalog and generation code into `src/games/checkers/`. Keep `src/puzzle_progress.c` shared
because progress storage can be game-neutral if puzzle IDs are prefixed with the backend ID, such as
`checkers/international/puzzle-0007.sgf`. The active backend should provide puzzle catalog callbacks:

    GPtrArray *(*puzzle_catalog_load)(const GameBackendVariant *variant, GError **error);
    gboolean (*puzzle_load)(const char *path, GameRecordTree **out_tree, GError **error);
    gboolean (*puzzle_validate_attempt)(...);

The exact validation signature can be shaped by current puzzle mode, but it must keep shared `src/window.c` from
knowing checkers main-line solution semantics.

Acceptance for this milestone is that puzzle picker, next puzzle, analyze puzzle, progress recording, and puzzle
clearing still pass their focused tests with the checkers backend. Existing checked-in puzzle files can remain under
`puzzles/american`, `puzzles/international`, and `puzzles/russian`; this plan changes source ownership, not puzzle
asset layout.

### Milestone 6: physically move checkers files and remove compatibility includes

After shared code compiles through generic interfaces, move the top-level checkers files to `src/games/checkers/`.
Use `git mv` for each move so history is preserved. Update includes in moved files to use local or relative paths
that keep checkers internals private. The only checkers headers included outside `src/games/checkers/` should be:

    src/games/checkers/checkers_backend.h

and only `src/active_game_backend.h`, checkers-specific tools, and checkers-specific tests should include that header
directly.

Rename tests that remain inherently checkers-specific into a checkers namespace where useful. For example,
`tests/test_move_gen.c`, `tests/test_board_geometry.c`, and `tests/test_game.c` are checkers backend tests, not
generic shared tests. They may keep their filenames initially to limit churn, but their includes must point at
`src/games/checkers/...` and their Makefile source list must use `CHECKERS_BACKEND_SRCS`.

Acceptance for this milestone is a source grep:

    rg '#include "(game|board|rulesets|ruleset|checkers_model|move_gen|board_geometry|checkers_constants)\\.h"' src

The output should either be empty or contain only files under `src/games/checkers/` and explicitly named
compatibility wrappers scheduled for deletion. Also run:

    rg 'Checkers[A-Z]|CHECKERS_' src --glob '!src/games/checkers/**'

The output should be limited to product names, application IDs, and temporary compatibility names that are documented
in the plan before completion.

### Milestone 7: make backend selection explicit in the build

Refactor `Makefile` source variables into shared and backend groups. The default should build checkers:

    GAME ?= checkers

    ifeq ($(GAME),checkers)
    GAME_BACKEND_DEFINE := -DGCHECKERS_GAME_CHECKERS
    GAME_BACKEND_SRCS := $(CHECKERS_BACKEND_SRCS)
    else
    $(error Unknown GAME '$(GAME)')
    endif

Add `$(GAME_BACKEND_DEFINE)` to the compile flags for application, tools, and tests that bind to the active backend.
Keep checkers-specific tests explicitly linked with `$(CHECKERS_BACKEND_SRCS)` so they can still inspect checkers
internals. Shared tests should link only the active backend through `$(GAME_BACKEND_SRCS)`.

The required command for the default build is still:

    make

The explicit equivalent must also work:

    make GAME=checkers

Add a short failure-path test or documented manual check:

    make GAME=unknown

It should fail immediately with a clear Makefile error naming the unknown game.

Acceptance for this milestone is that changing `GAME=checkers` is sufficient to select the checkers backend and no
source file outside the backend directory must be edited to select it.

### Milestone 8: update documentation and remove obsolete checkers names from shared APIs

Update `doc/OVERVIEW.md` after every source milestone, and do a final pass here. The final overview should explain:

1. shared application/UI modules,
2. generic `GameBackend` and `GameModel`,
3. shared AI search,
4. active-backend compile-time selection,
5. the checkers backend directory,
6. which tests are generic and which are checkers-specific.

Remove obsolete names from shared APIs where practical. It is acceptable for the binary, desktop file, schema ID, and
class prefixes like `GCheckersWindow` to remain as product names during this plan. It is not acceptable for generic
interfaces to expose `CheckersMove`, `CheckersRules`, `CheckersBoard`, or `CheckersWinner`.

Acceptance for this milestone is that a novice can read `doc/OVERVIEW.md`, find the active backend selection point,
and understand where to add a new game backend without reading this ExecPlan.

## Concrete Steps

Start from a clean tracked worktree except for unrelated untracked local files. From `/home/jerome/Data/gcheckers`,
run:

    git status --short

If there are modified tracked files unrelated to this plan, stop and ask whether to continue. Untracked local files
such as scratch directories can be ignored unless they overlap with paths this plan needs to create.

Implement milestones in order. After each milestone, run the focused tests named in that milestone and update this
ExecPlan's `Progress`, `Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` sections before
committing. Prefer small commits with messages like:

    Add active game backend interface
    Add generic game model wrapper
    Move checkers engine under games/checkers

When moving files, use `git mv` rather than delete-and-add. Keep compatibility wrappers only when they reduce risk,
and record every wrapper in `Outcomes & Retrospective` with the reason it still exists.

At the end of each milestone, update `doc/OVERVIEW.md` if any `src/` file changed. This is required by `AGENTS.md`.
Do not add a `doc/BUGS.md` entry unless the milestone fixes an actual bug; this plan is a refactor and architecture
change.

## Validation and Acceptance

The final implementation is acceptable when all of these are true:

The default build still works:

    cd /home/jerome/Data/gcheckers
    make

Expected result: `build/bin/gcheckers`, `build/tools/create_puzzles`, `build/tools/find_position`, and the static
library build without compiler warnings.

The explicit active-backend build still works:

    make clean
    make GAME=checkers

Expected result: same binaries as the default build, with `GCHECKERS_GAME_CHECKERS` selected through the Makefile.

The focused generic backend tests pass:

    build/tests/test_game_backend
    build/tests/test_game_model
    build/tests/test_ai_search

Expected result: each test exits 0 and demonstrates creating a checkers position through the generic backend,
applying a generic move, formatting a generic move, and running generic search.

The existing checkers behavior tests pass through the moved backend:

    build/tests/test_game
    build/tests/test_board
    build/tests/test_board_geometry
    build/tests/test_move_gen
    build/tests/test_checkers_model
    build/tests/test_sgf_io
    build/tests/test_sgf_controller
    build/tests/test_window
    build/tests/test_puzzle_catalog
    build/tests/test_puzzle_generation
    build/tests/test_puzzle_progress

Expected result: each exits 0. If a known unrelated GTK test failure still exists, document the exact command and
failure in `Surprises & Discoveries` and run the focused tests that cover this refactor.

The full test target is still the preferred final proof:

    make test

Expected result: all tests pass. If `make test` is blocked by a pre-existing unrelated failure, record the failure
verbatim and include evidence that the changed areas pass focused tests.

The source boundary is clean:

    rg 'Checkers[A-Z]|CHECKERS_' src --glob '!src/games/checkers/**'
    rg '#include "(game|board|rulesets|ruleset|checkers_model|move_gen|board_geometry|checkers_constants)\\.h"' src

Expected result: no shared source depends on checkers-only public types. Allowed exceptions must be product names,
application IDs, `src/active_game_backend.h`, or compatibility wrappers documented in this plan before final
completion.

Manual application smoke test:

    build/bin/gcheckers

Expected result: the checkers board appears, a new game can be started, a legal move can be made, analysis can run,
and puzzle mode can open a checkers puzzle. The user should not see any functional difference from the pre-refactor
checkers application.

## Idempotence and Recovery

Most milestones are additive until the physical file move. If a milestone fails midway, keep the old checkers path
compiling while fixing the new generic path. Do not delete `GCheckersModel`, `src/ai_alpha_beta.c`, or top-level
checkers headers until their generic replacements have focused tests passing.

For file moves, use `git mv` so rerunning status and diffs is understandable. If a move is made too early and shared
code breaks, do not use `git reset --hard`; either move the file back with `git mv` or add temporary compatibility
headers that include the moved header and mark them in this plan.

The Makefile `GAME ?= checkers` default must remain throughout the migration. A developer should be able to run
`make` after each milestone and get a checkers build, even if generic names are only partially adopted.

When adding generic move or position storage, guard sizes with `g_return_val_if_fail()` or compile-time assertions.
Silent truncation is not acceptable because future games may have larger move payloads than checkers.

## Artifacts and Notes

The current checkers-specific public type surface starts with these headers:

    src/game.h
    src/board.h
    src/checkers_constants.h
    src/ruleset.h
    src/rulesets.h
    src/checkers_model.h
    src/ai_alpha_beta.h

The current Makefile source group that must be split is:

    SRCS := src/board.c src/board_geometry.c src/game.c src/game_print.c src/move_gen.c src/ai_alpha_beta.c \
      src/rulesets.c \
      src/ai_transposition_table.c src/ai_zobrist.c src/checkers_model.c

The current `src/game.h` already hints at callbacks:

    struct Game {
      GameState state;
      const CheckersRules *rules;

      void (*print_state)(const Game *game, FILE *out);
      MoveList (*available_moves)(const Game *game);
    };

Do not reuse this exact struct as the generic backend interface. It is too small and still exposes checkers state.
Instead, use it as evidence that callback-based design fits the repository.

## Interfaces and Dependencies

Use GLib and GTK idioms already present in the repository. Generic backend APIs should use `gboolean`, `guint`,
`gint`, `gsize`, `GError`, and `GPtrArray` where those are already conventional in nearby code. Keep plain C structs
for hot-path AI and move generation.

The final public interface must include:

    src/game_backend.h
    src/active_game_backend.h
    src/game_model.h
    src/ai_search.h
    src/games/checkers/checkers_backend.h

`src/game_backend.h` owns the `GameBackend` struct and generic move, position, outcome, variant, board-view, puzzle,
and record callbacks. It must not include checkers headers.

`src/active_game_backend.h` maps compile defines to the active backend object. It may include
`src/games/checkers/checkers_backend.h` inside the `GCHECKERS_GAME_CHECKERS` branch. It must produce a compile error
if no backend define is present.

`src/game_model.h` is the GObject model wrapper that shared UI owns. It must not expose checkers types.

`src/ai_search.h` is the generic alpha-beta API. It must not expose `CheckersMove`, `Game`, or `CheckersRules`.

`src/games/checkers/checkers_backend.h` exposes only:

    extern const GameBackend checkers_game_backend;

All other checkers internals should be included only by checkers backend implementation files, checkers tools, and
checkers-specific tests.

The Makefile must expose:

    GAME ?= checkers
    GAME_BACKEND_DEFINE
    GAME_BACKEND_SRCS
    SHARED_SRCS
    CHECKERS_BACKEND_SRCS

The active backend selection must be visible in compiler commands through `$(GAME_BACKEND_DEFINE)`.

## Revision Notes

2026-04-23 / Codex: Initial ExecPlan written after surveying the existing architecture. The plan resolves the user's
requested direction into a compile-time selected `GameBackend` callback structure, staged compatibility work, and a
final `src/games/checkers/` ownership boundary.
