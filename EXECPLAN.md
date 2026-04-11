# Refactor Puzzle Validation and Add Existing-Puzzle Check Mode

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

`PLANS.md` is checked into this repository root and this document must be maintained in accordance with it.

## Purpose / Big Picture

After this change, `create_puzzles` will have two clear behaviors. It will still generate new puzzles from self-play or
from an SGF game, but it will also be able to walk the existing `puzzles/puzzle-*.sgf` files, re-validate them under
the current puzzle rules, and either report stale puzzles or delete them. This matters because puzzle-interest rules
change over time, and the repository needs a way to bring the existing puzzle set back into sync without manually
opening files one by one.

The core user-visible behavior is a new command-line mode such as `./create_puzzles --check-existing --dry-run` that
prints which puzzles would be removed, and a non-dry-run variant that actually deletes invalid puzzle files. The
generation path should keep working, but the code should no longer mix “validate candidate puzzle” with “write puzzle
files”, because pruning existing puzzles needs the same validation without saving.

## Progress

- [x] (2026-04-11 11:29Z) Read `PLANS.md`, inspected `src/create_puzzles.c`, and decided the shared validation API
      should validate a puzzle artifact (start position plus solution line), not rediscover the original mistake from a
      companion game file.
- [x] (2026-04-11 12:08Z) Introduced `CheckersPuzzleValidatedCandidate` and split validation from emission in
      `src/create_puzzles.c`; generation now validates first and saves in a separate step.
- [x] (2026-04-11 12:18Z) Added SGF loading helpers so `create_puzzles` can reconstruct a puzzle start position and
      saved main line from a `puzzle-####.sgf` file without GTK dependencies.
- [x] (2026-04-11 12:27Z) Added CLI parsing and execution for `--check-existing` and `--dry-run`, including deletion
      of invalid puzzle files and matching `game-####.sgf` companions.
- [x] (2026-04-11 12:34Z) Added and ran CLI parsing and end-to-end pruning tests, updated `src/OVERVIEW.md`, and ran
      the focused build/test commands.

## Surprises & Discoveries

- Observation: `create_puzzles` already stores enough data in the puzzle SGF to validate the puzzle artifact itself:
  root setup (`AE/AB/AW/ABK/AWK/PL`) and the saved tactical continuation line.
  Evidence: `checkers_puzzle_save_sgf()` in `src/create_puzzles.c` writes root setup properties and the continuation
  moves, and `checkers_puzzle_try_build_candidate_from_resulting_position()` already validates that shape from a
  post-mistake position.

- Observation: `gcheckers_sgf_controller_replay_node_into_game()` can replay setup-root SGFs, but `sgf_controller.h`
  pulls in GTK types through `board_view.h` and `sgf_view.h`, which makes it a poor dependency for the CLI binary.
  Evidence: `src/sgf_controller.h` includes `<gtk/gtk.h>` and UI headers, while `create_puzzles` currently builds
  without GTK in `Makefile`.

- Observation: a single-move setup-root puzzle is enough to test the pruning mode end to end because it is legal SGF
  input yet guaranteed to fail the current “interesting solution” rules.
  Evidence: `tests/test_create_puzzles_check.c` writes a one-move puzzle and both
  `./create_puzzles --check-existing --dry-run <dir>` and `./create_puzzles --check-existing <dir>` behave as
  expected.

## Decision Log

- Decision: Validate existing puzzles from the puzzle SGF artifact itself rather than from the saved `game-####.sgf`
  companion file.
  Rationale: The pruning mode is about whether the current puzzle file still satisfies current puzzle rules. Requiring
  the game companion would make pruning brittle and would keep “find the original mistake” coupled to “is this puzzle
  still interesting”. The requested API split is cleaner if the shared function answers “is this puzzle candidate valid
  from this start position and line?”.
  Date/Author: 2026-04-11 / Codex

- Decision: Keep the inverse SGF setup-property replay local to `src/create_puzzles.c` instead of extracting a new
  shared non-GUI module during this change.
  Rationale: The feature needed a non-GTK loader for setup-root puzzle SGFs, but the only existing replay helper lives
  behind `sgf_controller.h`. Copying the small setup parser locally kept the CLI build free of GTK and avoided a larger
  module split in the same change.
  Date/Author: 2026-04-11 / Codex

## Outcomes & Retrospective

`create_puzzles` now has a reusable validation step and a separate save step, which made it straightforward to add an
existing-puzzle pruning mode without teaching the checker how to write new files. The new `--check-existing` mode can
scan a puzzle directory, compare each saved puzzle against the current validation rules, and either report or delete
invalid puzzles and their companion game files.

The biggest lesson from the implementation was that the puzzle SGF artifact already carried enough information to
re-validate itself, so the new mode did not need to recover the original mistake from the saved game companion. That
kept the API boundary clean: generation produces a validated candidate and saves it, while checking re-loads a saved
candidate and asks whether it is still valid.

## Context and Orientation

`src/create_puzzles.c` is both the puzzle generator and the command-line entry point. Today it performs three distinct
jobs in one file: it discovers candidate mistakes in a game line, it validates whether the resulting position is a good
puzzle, and it writes `puzzles/puzzle-####.sgf` plus `puzzles/game-####.sgf`. The function
`checkers_puzzle_try_build_candidate_from_resulting_position()` currently combines validation and emission by building
the solution line, rejecting uninteresting candidates, and then immediately calling
`checkers_puzzle_emit_validated_candidate()`.

The validation logic depends on helpers in `src/puzzle_generation.c` and `src/puzzle_generation.h`. Those helpers are
pure functions over move lists and scores. They already host the “interesting shape” and “immediate recapture” rules,
so they are the right place for any reusable non-I/O puzzle predicates.

The CLI parsing was recently moved into `src/create_puzzles_cli.c` and `src/create_puzzles_cli.h`. That module is pure
argument parsing, and it already has a dedicated test in `tests/test_create_puzzles_cli.c`. Any new command-line flags
for pruning existing puzzles should be parsed there rather than open-coded in `main()`.

Existing puzzles live under `puzzles/` with names like `puzzle-0007.sgf`. The generator also writes companion files
named `game-0007.sgf`. The pruning mode should walk the existing puzzle files, load each puzzle SGF, reconstruct the
starting `Game` from the root setup properties, parse the saved main line, and re-run the same validation used for new
candidate puzzles. A “dry run” means printing what would be deleted without actually unlinking files. A non-dry-run
check should remove invalid puzzle SGFs and, when present, the matching `game-####.sgf` companion because it is no
longer useful after its puzzle is removed.

In this repository, “setup properties” means SGF properties on the root node that encode the board state instead of a
sequence of moves: `AE` clears squares, `AB` and `AW` place black/white men, `ABK` and `AWK` mark kings, and `PL`
sets the side to move. `create_puzzles` already knows how to write those properties in
`checkers_puzzle_add_setup_properties()`. The new pruning path needs the inverse operation for the CLI binary, but
without depending on `src/sgf_controller.c` because that file is tied to GTK.

## Plan of Work

First, introduce a result type in `src/create_puzzles.c` that represents a validated puzzle candidate. It should carry
the solution line, the best move, and the start/final static scores that are currently computed inside
`checkers_puzzle_try_build_candidate_from_resulting_position()`. Replace that function with a validation function that
returns this result without writing any files. Then adjust the generation path so that saving happens in a separate step
that consumes a validated result and emits `puzzle-####.sgf` and `game-####.sgf`.

Second, add SGF-loading helpers local to `src/create_puzzles.c` that can read a puzzle SGF into two pieces of data: a
`Game` representing the root setup position and a `GArray` main line of `CheckersPuzzleLineMove`. Reuse the existing
move parser used by `checkers_puzzle_load_main_line()`, but add the inverse of `checkers_puzzle_add_setup_properties()`
for the root node. Do not import `sgf_controller.h`; keep the CLI free of GTK dependencies.

Third, extend `src/create_puzzles_cli.c` with two new flags. `--check-existing` switches the program from generation
mode into validation/pruning mode. `--dry-run` is only meaningful with `--check-existing` and suppresses deletion while
printing the same “would delete” decisions. In check mode, the positional argument will be optional; when omitted, the
mode should inspect the default `puzzles/` directory. If a positional argument is present in check mode, treat it as
the directory to inspect. Outside check mode, preserve the current positional-argument behavior.

Fourth, implement the pruning executor in `src/create_puzzles.c`. It should iterate `puzzle-*.sgf` files, validate
each one with the shared validation function, and either keep it, print that it would be removed, or delete it and its
matching `game-####.sgf`. Make the log messages explicit enough to compare dry-run and destructive mode. Update the
final report so it includes how many existing puzzles were checked and how many were removed or would be removed.

Finally, add tests. `tests/test_create_puzzles_cli.c` should cover the new CLI flags and the positional-argument rules.
`tests/test_puzzle_generation.c` can keep the pure-rule tests. Add at least one new test module, or extend an existing
one if practical, that exercises the SGF puzzle-loading and validation helper on a setup-root puzzle. Update
`src/OVERVIEW.md` to describe the split between validation and emission and to document the new `--check-existing`
mode. Run the focused tests and the relevant builds.

## Concrete Steps

All commands should be run from `/home/jerome/Data/gcheckers`.

Build and run the focused tests while working:

  make test_create_puzzles_cli
  ./test_create_puzzles_cli
  make test_puzzle_generation
  ./test_puzzle_generation
  make create_puzzles

After the pruning mode is implemented, validate the new behavior manually with:

  ./create_puzzles --check-existing --dry-run

Expected behavior:

  The command prints one line per invalid puzzle explaining that it would be deleted, but the files remain on disk.

Then run:

  ./create_puzzles --check-existing

Expected behavior:

  Invalid `puzzle-####.sgf` files are removed, and matching `game-####.sgf` companions are removed if they exist.

## Validation and Acceptance

Acceptance for the refactor is:

1. New puzzle generation still builds and validates puzzles, with saving clearly separated from validation.
2. `./create_puzzles --check-existing --dry-run` scans existing puzzle files and reports invalid ones without deleting
   them.
3. `./create_puzzles --check-existing` deletes invalid puzzle files and their companion game files.
4. CLI parsing tests cover the new flags and positional argument rules.
5. The focused build/test commands above pass.

## Idempotence and Recovery

The refactor itself is additive and safe to rerun. `--check-existing --dry-run` is idempotent because it only reads and
prints. The destructive `--check-existing` mode is intentionally not idempotent in the sense that files disappear after
the first run; however, repeated runs should simply find fewer files and not fail. Companion `game-####.sgf` deletion
must tolerate missing files so reruns remain safe.

## Artifacts and Notes

The final implementation should leave concise command transcripts here if they reveal anything non-obvious, such as a
dry-run line of the form:

  would delete puzzles/puzzle-0042.sgf: solution does not improve a losing position enough

and a destructive run line of the form:

  deleted puzzles/puzzle-0042.sgf
  deleted puzzles/game-0042.sgf

## Interfaces and Dependencies

In `src/create_puzzles_cli.h`, keep `CheckersCreatePuzzlesCliOptions` as the parser output type, but extend it with
fields that unambiguously describe the execution mode, whether dry-run is enabled, and the optional check directory.

In `src/create_puzzles.c`, define a validated-candidate struct local to the file. It must own the generated solution
line and enough metadata for `checkers_puzzle_save_sgf()` and the final report. Provide a validation function that has
the shape:

  static gboolean checkers_puzzle_validate_candidate_from_resulting_position(...,
                                                                             CheckersPuzzleValidatedCandidate *out)

and a matching clear/free helper.

Also in `src/create_puzzles.c`, define a puzzle-loading helper that can reconstruct a `Game` from a puzzle SGF root
without GTK dependencies. Keep the SGF setup parsing local to the CLI path unless a cleaner non-GUI shared module is
obvious from the source tree while implementing.

Revision note (2026-04-11): Initial ExecPlan added to cover the validation refactor and the new existing-puzzle check
mode requested by the user.

Revision note (2026-04-11): Updated after implementation to record the completed milestones, the local non-GTK SGF
setup replay decision, and the passing pruning-mode test coverage.
