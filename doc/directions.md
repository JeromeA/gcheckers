# Direction Offsets And Precomputed Rays

This note describes the move-generation geometry refactoring in `src/move_gen.c` and `src/board_geometry.c`.

Move generation now stays in index space once it has fetched the precomputed direction rays for the active board size.
The geometry module still uses `board_coord_from_index()` and `board_index_from_coord()` once at initialization time
to build those rays, but the hot traversal path in `src/move_gen.c` no longer converts back and forth for each scan.

## Current index layout

The current board code already stores playable squares without gaps between rows:

  index = row * (board_size / 2) + col / 2

For an 8x8 board, there are `4` playable squares per row:

  row 0:  0  1  2  3
  row 1:  4  5  6  7
  row 2:  8  9 10 11
  row 3: 12 13 14 15

For a 10x10 board, there are `5` playable squares per row.

It is convenient to name this value:

  per_row = board_size / 2

Moving one playable row up or down changes the index by `per_row`.

## Why parity matters

Playable squares are staggered between rows. Because of that, diagonal movement does not always use the same delta.

For a given square:

  row = index / per_row

Then:

  row % 2

tells which staggered row pattern we are on.

That parity decides which diagonal keeps the same position within the playable row and which diagonal shifts by one.

## Index deltas

The deltas depend on `per_row` and on the row parity.

General formula:

- even row:
  up-left `= -per_row`
  up-right `= -per_row + 1`
  down-left `= per_row`
  down-right `= per_row + 1`
- odd row:
  up-left `= -per_row - 1`
  up-right `= -per_row`
  down-left `= per_row - 1`
  down-right `= per_row`

For an 8x8 board, `per_row == 4`, so:

- even row: `{-4, -3, 4, 5}`
- odd row: `{-5, -4, 3, 4}`

The commonly cited set:

  {-5, -4, 3, 4}

is the odd-row set on 8x8. It is not sufficient by itself for every row. The code still needs parity awareness, or it
needs precomputed rays that already encode the correct result for each index.

For a 10x10 board, `per_row == 5`, so:

- even row: `{-5, -4, 5, 6}`
- odd row: `{-6, -5, 4, 5}`

## Edge detection

If the code computes moves directly from deltas, then:

  index % per_row

gives the position inside the playable row.

On 8x8, this is `index % 4`.
On 10x10, this is `index % 5`.

For example, on 8x8:

- `index % 4 == 0` means the leftmost playable square in the row
- `index % 4 == 3` means the rightmost playable square in the row

So `% per_row` is enough to detect when a left-going or right-going move would wrap into the next row.

## Precomputed rays

Instead of checking boundaries during move generation, we can precompute directional rays.

For each playable index, store 4 arrays, one per direction.

The order of those 4 directions should be part of the API contract:

- up-left
- up-right
- down-left
- down-right

That order is useful by itself. It means the code can rely on the first 2 entries being the "up" directions and the
last 2 entries being the "down" directions.

Each array is sentinel-terminated and contains every playable index encountered when repeatedly moving in that
direction from the starting square.

Example shape:

  rays[index][direction] = {next_1, next_2, next_3, ..., -1}

Then move generation becomes simpler:

- a man usually needs only the first entry in a direction
- a non-flying jump can look at the first entry as the jumped square and the second entry as the landing square
- a flying king can iterate the whole ray until the sentinel

It also means forward-only traversal can be written as a simple range over the direction indices:

- for white, forward means "up", so loop over the first 2 directions
- for black, forward means "down", so loop over the last 2 directions

The same rule applies to rulesets where men may move forward only, or may jump forward only. The move generator does
not need a separate direction table for those cases if the enum order is stable and documented.

This removes most of the boundary logic from the hot path. The edge handling is done once when building the tables.

## Maximum ray length

The maximum number of playable squares in one direction is:

  CHECKERS_MAX_BOARD_SIZE - 1

Because one step moves to the next board row on the same diagonal. On a board with `N` rows, a square can have at
most `N - 1` squares in front of it on one diagonal.

In this repository:

- `CHECKERS_MAX_BOARD_SIZE == 10`
- `CHECKERS_MAX_BOARD_SIZE - 1 == 9`

So a sentinel-terminated ray needs room for 9 indices plus the sentinel.

A useful constant would be:

  CHECKERS_MAX_DIRECTION_STEPS = CHECKERS_MAX_BOARD_SIZE - 1

And the storage shape could be:

  rays[CHECKERS_MAX_SQUARES][4][CHECKERS_MAX_DIRECTION_STEPS + 1]

`CHECKERS_MAX_SQUARES` is still 50 because that is the maximum number of playable squares on the whole board, not the
maximum number of squares in one direction.

## Recommended storage

These tables should not live inside `CheckersRules`.

`CheckersRules` describes game semantics:

- board size
- mandatory capture
- longest capture rule
- flying kings
- men jumping backwards

Directional rays are not rules. They are derived board geometry for a given indexing scheme and board size.

The implemented design is a separate geometry module:

- `src/board_geometry.h`
- `src/board_geometry.c`

With a data structure such as:

  typedef struct {
    uint8_t board_size;
    uint8_t squares;
    int8_t rays[CHECKERS_MAX_SQUARES][4][CHECKERS_MAX_DIRECTION_STEPS + 1];
  } CheckersBoardGeometry;

and an accessor such as:

  const CheckersBoardGeometry *checkers_board_geometry_get(uint8_t board_size);

That module exposes one static geometry instance for 8x8 and one for 10x10.

This separation keeps responsibilities clear:

- `CheckersRules` answers "what is legal?"
- board geometry answers "what squares are in this direction?"

Different rulesets with the same board size can then share the same precomputed geometry.

## Other coordinate-conversion call sites

This refactoring starts with `src/move_gen.c`, because that is where directional traversal is hottest and where the
precomputed rays help the most. That part is now done.

After that, the project should review the remaining uses of `board_index_from_coord()` and `board_coord_from_index()`
and decide case by case whether they should also move to the geometry module, or whether coordinate conversion still
fits their job better.

Current non-test call sites are:

- `src/game.c`
  uses `board_coord_from_index()` and `board_index_from_coord()` while applying moves and locating captured pieces
- `src/game_print.c`
  uses `board_index_from_coord()` while printing the full checkerboard layout
- `src/ai_alpha_beta.c`
  uses `board_coord_from_index()` in evaluation
- `src/window.c`
  uses `board_coord_from_index()` for UI-facing board handling
- `src/sgf_controller.c`
  uses both conversion helpers for SGF setup and replay logic
- `src/board_move_overlay.c`
  uses `board_coord_from_index()` for drawing overlays
- `src/board_grid.c`
  uses `board_index_from_coord()` when constructing UI squares
- `src/create_puzzles.c`
  uses both conversion helpers for puzzle and SGF-related logic

These call sites are not all equally good candidates for migration.

Recommended follow-up order:

1. `src/game.c`
   This is engine logic and may benefit from geometry helpers, especially if captured-square lookup can be rewritten in
   index space.
2. `src/ai_alpha_beta.c`
   This is engine-side code, so avoiding repeated conversions may help once the geometry API is established.
3. `src/create_puzzles.c`
   This is tooling built on engine concepts and may benefit from reusing the same traversal primitives.

The UI- and rendering-oriented call sites such as `src/window.c`, `src/board_move_overlay.c`, and `src/board_grid.c`
are less urgent, because they naturally think in `(row, col)` screen coordinates. They may reasonably keep using the
conversion helpers.

## Practical effect on move generation

With precomputed rays, `src/move_gen.c` no longer needs to call `board_coord_from_index()` or
`board_index_from_coord()` for directional traversal.

Instead it can:

1. fetch the geometry for the current board size
2. iterate the correct ray for the current index and direction
3. apply rule logic on top of those precomputed indices

This keeps the move legality logic in `move_gen.c` while moving indexing and boundary handling into a dedicated,
reusable geometry layer.
