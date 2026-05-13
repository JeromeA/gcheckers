# Boop Padded Bitboard Representation

This ExecPlan is a living document. Keep `Progress`, `Surprises`, `Decision Log`, and `Retrospective` current while
implementing.

## Purpose

Boop search spends most of its time detecting three-in-a-row patterns in
`boop_position_collect_line_promotion_choices()` and `boop_position_has_cat_line()`. The current `BoopPosition` stores
one `BoopPiece` per square and scans line definitions. Replace the internal board representation with padded bitboards
so line detection is a handful of bit operations:

```
h  = b & (b >> 1) & (b >> 2)
v  = b & (b >> 8) & (b >> 16)
d1 = b & (b >> 9) & (b >> 18)
d2 = b & (b >> 7) & (b >> 14)
```

Each 6x6 row occupies the low six bits of an 8-bit lane. The two unused bits between rows prevent horizontal and
diagonal wraparound from producing false positives.

## Progress

- [x] 2026-05-13T10:05:09Z: Captured the target representation and boundaries.
- [x] 2026-05-13T10:05:09Z: Replaced `BoopPosition.board[]` with side, cat, and occupied masks.
- [x] 2026-05-13T10:05:09Z: Converted promotion masks and ray masks to the padded mask format.
- [x] 2026-05-13T10:05:09Z: Replaced line scans with bitboard detection and line-choice extraction.
- [x] 2026-05-13T10:05:09Z: Updated SGF, backend, and tests to use helper accessors instead of direct board-array
  reads.
- [x] 2026-05-13T10:05:09Z: `make all` passed.
- [x] 2026-05-13T10:05:09Z: `make test` rebuilt tests and then stopped at the pre-existing
  `/sgf-view/link-angles` `GLib-GObject-FATAL-CRITICAL: invalid (NULL) pointer instance`.
- [x] 2026-05-13T10:05:09Z: Targeted Boop tests passed:
  `build/tests/test_boop_game`, `build/tests/test_boop_backend`, `GGAME_TEST_PROFILE=boop build/tests/test_sgf_io`,
  `GGAME_TEST_PROFILE=boop build/tests/test_game_backend`, and
  `GGAME_TEST_PROFILE=boop build/tests/test_sgf_controller`.

## Target State

`BoopPosition` stores:

- `side_mask[2]`: every square occupied by each side, cats and kittens.
- `cat_mask[2]`: every cat square for each side.
- `occupied_mask`: all occupied squares, kept equal to `side_mask[0] | side_mask[1]`.

Kittens are represented by `side_mask[side] & ~cat_mask[side]`. Empty squares are represented by
`BOOP_BOARD_MASK & ~occupied_mask`.

All masks in Boop code use the padded format. Square `(row, column)` maps to bit `row * 8 + column`. Existing
`BoopMove.square` and `BoopMove.path[]` keep their 0..35 square indices because notation, UI hit testing, and
selection paths need stable coordinates. Those indices are boundary values only; position membership and promotion masks
use padded bitboards.

## Implementation Plan

1. Add padded-board mask macros to `boop_types.h` and replace `BoopPosition.board[]` with the three masks.
2. Add Boop position accessors in `boop_game.h`/`boop_game.c`:
   `boop_position_get_piece()` for SGF/UI/tests, and `boop_position_set_piece()` for setup and SGF loading.
3. Update initialization, normalization, hashing, static evaluation, move placement, promotion application, and
   move-list generation to use masks.
4. Replace ray index tables with ray mask tables. Boop effects should test adjacent and destination masks directly,
   moving mask bits instead of reading and writing array slots.
5. Replace `boop_position_has_cat_line()` with direct bitboard detection on `cat_mask[side]`.
6. Replace `boop_position_collect_line_promotion_choices()` with direct start-mask extraction for horizontal, vertical,
   and diagonal three-in-a-row windows.
7. Update SGF position loading/saving, backend display code, and all Boop tests to use the helper accessors and padded
   promotion masks.
8. Update `doc/OVERVIEW.md` for the new Boop position representation.
9. Run `make all` and `make test`; record any failures.

## Acceptance Criteria

- No source code reads or writes `position->board[]`; the field no longer exists.
- Boop promotion masks, option masks, and ray masks use the padded 8-bit-row layout.
- `boop_position_has_cat_line()` detects cat wins without scanning line arrays.
- `boop_position_collect_line_promotion_choices()` collects the same legal line promotion choices using bit operations.
- `gboop`, `boop_create_puzzles`, SGF round-tripping, and existing Boop tests still work.
- `doc/OVERVIEW.md` describes the mask-based Boop position representation.

## Surprises

- `make test` still stops at the unrelated SGF view link-angle crash before the full suite completes. Targeted Boop
  tests were run separately.

## Decision Log

- Decision: Keep square indices in `BoopMove.square` and `BoopMove.path[]`.
  Rationale: SGF coordinates, board-widget hit testing, and move overlays are coordinate-oriented APIs. Changing them to
  masks would spread bitboard details into UI and notation code without helping the hot path.

- Decision: Treat `occupied_mask` as a cached invariant, not independent game state.
  Rationale: The authoritative ownership state is in `side_mask[]` and `cat_mask[]`; normalization can restore
  `occupied_mask` from the side masks.

## Retrospective

The refactor removed the per-square board array and the maximal-line scan tables from the Boop engine. The remaining
square indices are explicit boundary values for UI, SGF notation, and move paths; all position membership, promotion
masks, cat-line detection, and ray effects use the padded bitboard format.
