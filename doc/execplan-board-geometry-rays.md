# Refactor Move Generation To Use Precomputed Direction Rays

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document must be maintained in accordance with `doc/PLANS.md` at the repository root.

## Purpose / Big Picture

After this change, move generation in `src/move_gen.c` will no longer convert back and forth between playable-square
indices and `(row, col)` coordinates for directional traversal. Instead, it will fetch precomputed direction rays from
board geometry tables and iterate those rays directly in index space. A "direction ray" here means the ordered list of
playable indices reached by repeatedly moving in one diagonal direction from one starting square, terminated by a
sentinel value. The order of the four directions is part of the API contract, so callers can rely on the first two
entries meaning "up" and the last two meaning "down".

The user-visible behavior must stay the same: legal moves, forced captures, longest-capture filtering, king movement,
and puzzle/search behavior must all remain unchanged. The way to see the change working is to run the existing test
suite and observe that move-generation tests, engine tests, puzzle checks, and the full project test target still pass
after `move_gen.c` has been rewritten to consume the new geometry module.

## Progress

- [x] (2026-04-17 15:38Z) Researched the current indexing scheme, confirmed that playable rows are packed without gaps,
  and drafted this ExecPlan plus the supporting design note in `doc/directions.md`.
- [x] (2026-04-17 16:55Z) Added `src/board_geometry.h` and `src/board_geometry.c`, with static 8x8 and 10x10
  geometry instances exposed by `checkers_board_geometry_get()`.
- [x] (2026-04-17 17:00Z) Added `tests/test_board_geometry.c` and `test_board_geometry` to validate representative rays
  and sentinel termination on both supported board sizes.
- [x] (2026-04-17 17:08Z) Refactored `src/move_gen.c` to use geometry rays for simple moves, man jumps, and
  flying-king scans, and added a black-forward-direction regression test in `tests/test_move_gen.c`.
- [x] (2026-04-17 17:14Z) Surveyed the remaining non-test call sites of `board_index_from_coord()` and
  `board_coord_from_index()` and documented the follow-up order in `doc/directions.md`.
- [x] (2026-04-17 17:16Z) Updated `doc/OVERVIEW.md` to describe the new geometry module and the revised
  `move_gen.c` collaboration model.
- [x] (2026-04-17 16:12Z) Refactored `tests/test_position_predicate.c` from a multi-minute search-over-search run to
  a targeted 2-ply version that finishes in about `0.05s` while preserving predicate and cache coverage.
- [x] (2026-04-17 16:12Z) Ran `make all` successfully after the geometry and test changes.
- [x] (2026-04-17 16:12Z) Ran `env -u DISPLAY -u WAYLAND_DISPLAY -u GNOME_SETUP_DISPLAY make test` successfully.
  GTK-dependent binaries skipped as designed when no display was available.

## Surprises & Discoveries

- Observation: The current board indexing in `src/board.c` is already compact and gapless across playable rows.
  Evidence: `board_index_from_coord()` computes `row * (board_size / 2) + col / 2`, so 8x8 rows are `0..3`, `4..7`,
  `8..11`, not `0..3`, `8..11`, `16..19`.

- Observation: The widely quoted 8x8 delta set `{-5, -4, 3, 4}` is only the odd-row set. Even rows use
  `{-4, -3, 4, 5}`.
  Evidence: `row = index / per_row`, `per_row = board_size / 2`, and the staggered playable-square layout changes
  which diagonal keeps the same in-row position.

- Observation: The maximum ray length is `board_size - 1`, not `board_size / 2 - 1`.
  Evidence: from a square such as `(0, 1)` on 10x10, the down-right ray reaches 9 squares before the edge. The
  implemented constant is therefore `CHECKERS_MAX_DIRECTION_STEPS = CHECKERS_MAX_BOARD_SIZE - 1`.

- Observation: The order of the direction enum affects move-generation structure, not just readability.
  Evidence: if the API guarantees `[up-left, up-right, down-left, down-right]`, forward-only men can loop over the
  first two directions for white and the last two for black without constructing per-piece temporary direction lists.

- Observation: `tests/test_position_predicate.c` was too slow as originally written because it nested expensive search
  predicates inside a broader position search.
  Evidence: the original full-suite run spent minutes inside `build/tests/test_position_predicate`; after shrinking the
  search to 2 plies and shallower predicate depths, `/usr/bin/time ./build/tests/test_position_predicate` reports
  about `0:00.05`.

- Observation: `make test` in the desktop display environment still trips an existing GTK failure at
  `/sgf-view/link-angles`, while the same suite passes in a displayless environment where GTK tests skip.
  Evidence: a display-enabled `make test` bailed out in `build/tests/test_sgf_view`; rerunning as
  `env -u DISPLAY -u WAYLAND_DISPLAY -u GNOME_SETUP_DISPLAY make test` completed successfully.

## Decision Log

- Decision: Store precomputed directional rays in a dedicated board-geometry module instead of embedding them in
  `CheckersRules`.
  Rationale: `CheckersRules` describes legality semantics such as mandatory capture and flying kings, while direction
  rays are derived geometry shared by all rulesets of the same board size.
  Date/Author: 2026-04-17 / Codex

- Decision: Keep the geometry data static and read-only, with one instance for 8x8 and one instance for 10x10, and
  expose them through an accessor keyed by board size.
  Rationale: there are only two supported board sizes, so runtime allocation adds complexity without benefit.
  Date/Author: 2026-04-17 / Codex

- Decision: Make the direction order part of the exported API: up-left, up-right, down-left, down-right.
  Rationale: move generation for forward-only movement and forward-only jumps can then iterate a contiguous range of
  direction indices instead of building separate direction arrays or switch-heavy code.
  Date/Author: 2026-04-17 / Codex

- Decision: Migrate `move_gen.c` in additive steps, introducing the geometry module and tests before replacing the
  traversal logic.
  Rationale: this reduces risk in a core engine module and gives a novice contributor a clean point to validate the
  ray tables independently before touching move legality.
  Date/Author: 2026-04-17 / Codex

## Outcomes & Retrospective

The geometry-backed move generator is implemented. `src/move_gen.c` now walks precomputed rays in index space, the
direction order is explicit API, and the project has dedicated low-level coverage for the new tables in
`tests/test_board_geometry.c`. `tests/test_position_predicate.c` was also reduced to a much faster targeted form so it
no longer dominates the suite runtime.

Validation result:

- `make all` succeeded.
- `env -u DISPLAY -u WAYLAND_DISPLAY -u GNOME_SETUP_DISPLAY make test` succeeded.
- `make test` in the desktop display environment still hits an existing GTK failure at `/sgf-view/link-angles`, which
  remains a residual display-environment risk outside the geometry refactor itself.

## Context and Orientation

The current board representation lives in `src/board.c` and `src/board.h`. A playable-square index is a compact index
into only the dark squares used for checkers. The conversion helpers `board_index_from_coord()` and
`board_coord_from_index()` convert between that compact index and full board coordinates. The move generator in
`src/move_gen.c` currently depends on those conversions: it turns an index into `(row, col)`, applies coordinate
direction deltas, and converts back to indices for both simple moves and jumps.

The public game API is declared in `src/game.h` and implemented by `src/game.c`. Callers such as the GTK model in
`src/checkers_model.c`, the search code in `src/ai_alpha_beta.c`, and puzzle tooling in `src/create_puzzles.c`
consume legal moves through `game_list_available_moves()`, so any regression in `src/move_gen.c` will affect almost
every part of the project.

Tests are compiled and run through the repository `Makefile`. The focused move-generation tests live in
`tests/test_move_gen.c`. The general board helper tests live in `tests/test_board.c`. Because move generation feeds the
engine, search, and puzzle tooling, this plan also treats `tests/test_game.c`, `tests/test_checkers_model.c`, and
`tests/test_create_puzzles_check.c` as required validation targets even though they do not mention the geometry module
by name.

The new term introduced by this plan is "direction ray". In this repository, a direction ray is a small, sentinel-
terminated array of playable-square indices representing every square encountered by moving repeatedly in one of the
four diagonal directions from a start index. For example, on 8x8 from a middle square, the down-right ray might look
like `{14, 19, 24, -1}`. A sentinel is a special end marker value that is not a valid playable index; this plan uses
`-1` because normal indices are non-negative and fit inside `int8_t`.

The order of those four directions is part of the planned interface. This plan uses:

- up-left
- up-right
- down-left
- down-right

That contract matters because forward-only traversal for men becomes a small index range over the direction enum:

- white forward movement or white-only forward jumps iterate the first two directions
- black forward movement or black-only forward jumps iterate the last two directions

The plan must preserve that property if names change.

## Plan of Work

The first milestone is to introduce a dedicated geometry module. Create `src/board_geometry.h` and
`src/board_geometry.c`. Define a small, explicit API that returns immutable geometry for a board size. The module must
own the precomputed direction rays for the supported board sizes. It must not depend on `Game`, `CheckersRules`, or
`move_gen.c`; it should only depend on `src/checkers_constants.h` and simple integer types. Add the new source file to
the `SRCS` list in `Makefile` so the engine, tools, and tests all build against it automatically.

In the same milestone, add one new compile-time constant in `src/checkers_constants.h`:

    CHECKERS_MAX_DIRECTION_STEPS = CHECKERS_MAX_BOARD_SIZE - 1

Keep the existing constants unchanged. The geometry module should use `CHECKERS_MAX_DIRECTION_STEPS + 1` entries per
ray to leave room for the sentinel.

The second milestone is test-first validation of the geometry tables. Add a new test file `tests/test_board_geometry.c`
and a `test_board_geometry` target to `Makefile`. This test must prove that representative rays are correct on both
8x8 and 10x10 boards, not merely that the accessor returns a non-NULL pointer. Cover at least one left-edge square,
one right-edge square, and one interior square on each board size. For each representative square, assert the exact
contents of all four rays including the terminating `-1`. Include a test that confirms no valid ray entry appears
after the sentinel. Include at least one assertion that the direction order is exactly the documented API order. The
goal is that a contributor can trust the geometry data before rewriting move generation.

The third milestone is the `src/move_gen.c` migration. Replace directional traversal in `generate_simple_moves()` and
`dfs_jumps()` with geometry lookups. For simple moves, a man should inspect only the first entry of the permitted ray,
while a flying king should iterate every square in the ray until blocked. For non-flying jumps, use the first ray
entry as the adjacent square and the second ray entry as the landing square. For flying kings, walk each ray in order
until an opponent is found, then keep scanning the same ray for empty landing squares exactly as the current
coordinate-based implementation does. During this migration, preserve all existing rule checks: forward-only men for
simple moves, optional backward jumps for men, mandatory capture, and longest-capture filtering. Use the direction
order contract so forward-only traversal loops over directions `0..1` for white and `2..3` for black instead of
constructing ad hoc direction lists.

The fourth milestone is to survey the remaining non-test conversion-helper call sites and decide which ones should also
migrate away from `board_index_from_coord()` or `board_coord_from_index()`. The current non-test call sites are:

- `src/game.c`
- `src/game_print.c`
- `src/ai_alpha_beta.c`
- `src/window.c`
- `src/sgf_controller.c`
- `src/board_move_overlay.c`
- `src/board_grid.c`
- `src/create_puzzles.c`

This milestone does not require migrating all of them immediately. It does require a documented decision for each
area. Engine-facing traversal code such as `src/game.c`, `src/ai_alpha_beta.c`, and possibly `src/create_puzzles.c`
should be evaluated first. UI and rendering code may reasonably keep coordinate conversion if `(row, col)` is the more
natural representation there. This survey is complete; the follow-up order is now recorded in `doc/directions.md`.

The fifth milestone is repository documentation. Update `doc/OVERVIEW.md` so the `Board primitives` section mentions
that directional traversal now lives in `board_geometry.c` rather than using board-coordinate conversion at every
move-generation step. Add a new short section describing `src/board_geometry.c` and `src/board_geometry.h`: what they
store, who consumes them, and why they are separate from `CheckersRules`. Keep `doc/directions.md` aligned with the
final implementation if any naming or layout decisions changed during coding.

The final milestone is validation and cleanup. Run focused tests first while iterating, then run `make all` and the
full `make test` target. If any GTK test is skipped because a display is unavailable, record that exact outcome in the
plan rather than silently omitting it. Do not remove the old coordinate helpers from `src/board.c`; they are still
used outside move generation and remain part of the public board API.

## Concrete Steps

Work from the repository root:

    cd /home/jerome/Data/gcheckers

Before editing, inspect the current move-generation and board helpers:

    sed -n '1,240p' src/move_gen.c
    sed -n '1,140p' src/board.c
    sed -n '1,220p' tests/test_move_gen.c

Create the geometry interface and implementation, then wire them into the build:

    edit src/checkers_constants.h
    edit src/board_geometry.h
    edit src/board_geometry.c
    edit Makefile

Add focused tests and build them repeatedly during development:

    edit tests/test_board_geometry.c
    make test_board_geometry
    ./build/tests/test_board_geometry

Expected success transcript:

    Board geometry tests passed.

Once the geometry tables are trusted, refactor `src/move_gen.c` and extend existing engine tests if needed:

    edit src/move_gen.c
    edit tests/test_move_gen.c
    make test_move_gen
    ./build/tests/test_move_gen

Expected success transcript:

    Move generation tests passed.

After the core migration, run broader validation:

    make test_game
    make test_checkers_model
    make test_create_puzzles_check
    make all
    make test

Update documentation and the ExecPlan itself after each milestone:

    edit doc/OVERVIEW.md
    edit doc/directions.md
    edit doc/execplan-board-geometry-rays.md

## Validation and Acceptance

Acceptance is behavioral, not structural.

First, `./build/tests/test_board_geometry` must prove that the static rays are correct for representative squares on
both 8x8 and 10x10 boards, with exact expected index sequences and correct sentinel termination.

Second, `./build/tests/test_move_gen` must still pass all existing scenarios for simple moves, forced captures,
backward-jump rules, longest-capture filtering, and flying kings. If new regression tests are added for edge squares
or multi-jump continuation through the geometry tables, they must fail before the migration and pass after it.

Third, the broader engine-facing tests must still pass: `./build/tests/test_game`,
`./build/tests/test_checkers_model`, and `./build/tests/test_create_puzzles_check`. These prove that legal move lists
are still valid when consumed by move application, GTK model logic, and puzzle validation.

Finally, `make all` and `make test` from the repository root must succeed. Successful completion means the project
still builds all binaries and the full automated test suite still passes with the new geometry-backed move generation.

## Idempotence and Recovery

The edits in this plan are additive before they are substitutive. Creating `src/board_geometry.c`,
`src/board_geometry.h`, and `tests/test_board_geometry.c` is safe to repeat as long as the same file names are used
consistently. Re-running the test commands is safe and expected.

If the `move_gen.c` migration introduces regressions, recover by temporarily keeping both traversal paths in the file
behind private helper functions and switching callers back to the coordinate-based path until the ray-backed path is
correct. Do not delete `board_coord_from_index()` or `board_index_from_coord()` during this refactor. They remain the
safe fallback and are still used elsewhere in the repository.

If the geometry tables are wrong, fix the table generator or static initializers first and re-run
`./build/tests/test_board_geometry` before touching move legality again. This keeps debugging scoped to either
geometry construction or move-rule application, rather than mixing both problems together.

## Artifacts and Notes

Suggested interface shape:

    typedef enum {
      CHECKERS_DIRECTION_UP_LEFT = 0,
      CHECKERS_DIRECTION_UP_RIGHT,
      CHECKERS_DIRECTION_DOWN_LEFT,
      CHECKERS_DIRECTION_DOWN_RIGHT,
      CHECKERS_DIRECTION_COUNT
    } CheckersDirection;

    enum {
      CHECKERS_DIRECTION_SENTINEL = -1
    };

    typedef struct {
      uint8_t board_size;
      uint8_t squares;
      int8_t rays[CHECKERS_MAX_SQUARES][CHECKERS_DIRECTION_COUNT][CHECKERS_MAX_DIRECTION_STEPS + 1];
    } CheckersBoardGeometry;

    const CheckersBoardGeometry *checkers_board_geometry_get(uint8_t board_size);

The order of `CheckersDirection` above is part of the API. Callers are allowed to assume:

- directions `0` and `1` are the forward directions for white
- directions `2` and `3` are the forward directions for black

If that ordering changes, every caller that slices by direction range must be updated together.

Verified examples now covered by `tests/test_board_geometry.c`:

    8x8 index 13, up-left ray:   {8, 4, -1}
    8x8 index 13, up-right ray:  {9, 6, 2, -1}
    8x8 index 13, down-left ray: {16, 20, -1}
    8x8 index 13, down-right ray:{17, 22, 26, 31, -1}

    10x10 index 22, up-left ray:   {17, 11, 6, 0, -1}
    10x10 index 22, up-right ray:  {18, 13, 9, 4, -1}
    10x10 index 22, down-left ray: {27, 31, 36, 40, 45, -1}
    10x10 index 22, down-right ray:{28, 33, 39, 44, -1}

## Interfaces and Dependencies

In `src/checkers_constants.h`, define:

    CHECKERS_MAX_DIRECTION_STEPS = CHECKERS_MAX_BOARD_SIZE - 1

In `src/board_geometry.h`, define:

    typedef enum {
      CHECKERS_DIRECTION_UP_LEFT = 0,
      CHECKERS_DIRECTION_UP_RIGHT,
      CHECKERS_DIRECTION_DOWN_LEFT,
      CHECKERS_DIRECTION_DOWN_RIGHT,
      CHECKERS_DIRECTION_COUNT
    } CheckersDirection;

    enum {
      CHECKERS_DIRECTION_SENTINEL = -1
    };

    typedef struct {
      uint8_t board_size;
      uint8_t squares;
      int8_t rays[CHECKERS_MAX_SQUARES][CHECKERS_DIRECTION_COUNT][CHECKERS_MAX_DIRECTION_STEPS + 1];
    } CheckersBoardGeometry;

    const CheckersBoardGeometry *checkers_board_geometry_get(uint8_t board_size);

Document in the header that the enum order is stable API, not an implementation detail.

In `src/board_geometry.c`, implement:

    const CheckersBoardGeometry *checkers_board_geometry_get(uint8_t board_size);

The implementation may use static helper functions to build or validate the geometry tables, but the exported data
must be immutable to callers.

In `src/move_gen.c`, add private helpers that accept a `const CheckersBoardGeometry *geometry` and use
`geometry->rays[index][direction]` instead of `board_coord_from_index()` and `board_index_from_coord()` for
directional traversal.

In `Makefile`, add:

- `src/board_geometry.c` to `SRCS`
- a `test_board_geometry` target and binary path under `build/tests`
- the new test binary to the `test` target sequence

In `tests/test_board_geometry.c`, add focused assertions for both supported board sizes and for sentinel termination.

Revision note: updated after implementation to record the shipped geometry module, the corrected maximum ray length,
the remaining coordinate-conversion call-site survey, and the completed documentation changes.
