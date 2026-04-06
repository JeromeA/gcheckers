# create_puzzles Generator

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This plan is maintained according to PLANS.md at the repository root (`PLANS.md`).

## Purpose / Big Picture

Add a new CLI binary `create_puzzles` that repeatedly self-plays games and emits tactical SGF puzzles.

A generated puzzle is defined as:
- Player A makes a mistake at a node: played move is at least 100 points worse than best at depth 10.
- Puzzle start is the resulting position after that mistake (Player B to move).
- At puzzle start, Player B has at least 4 legal moves and exactly one top move (all other moves are at least
  100 points worse, equivalently not tied with best).
- Tactical continuation is generated from puzzle start by playing best moves at depth 0 (`effective depth = 1`) for
  both sides until the position depth-0 evaluation equals the punished target value (the depth-10 best score at puzzle
  start).

The tool stops when it has generated `N` puzzles requested on the CLI and saves files as:
`puzzles/puzzle-0000.sgf`, starting from the highest existing index + 1.

## Progress

- [x] (2026-03-10 16:30Z) Replaced stale ExecPlan scope with this feature-specific plan.
- [x] (2026-03-10 17:12Z) Implemented reusable puzzle helper module (`src/puzzle_generation.[ch]`) for scoring/index
  logic.
- [x] (2026-03-10 17:18Z) Implemented `src/create_puzzles.c` end-to-end generation loop and SGF writing.
- [x] (2026-03-10 17:20Z) Wired `create_puzzles` and new tests into `Makefile`.
- [x] (2026-03-10 17:12Z) Added `tests/test_puzzle_generation.c` for uniqueness/mistake/index behavior.
- [x] (2026-03-10 17:21Z) Updated `src/OVERVIEW.md`.
- [x] (2026-03-10 17:32Z) Ran required validation (`make all`, `make test`) and targeted new binary smoke run.
- [x] (2026-04-06 12:45Z) Refactored puzzle validation into English-style predicates and added helper tests for
  "enough choice" and "single correct move" rules.

## Surprises & Discoveries

- Observation: Engine "depth 0" behavior in the app is implemented as alpha-beta depth `1` (`0 => 1` mapping).
  Evidence: `gcheckers_window_choose_computer_move()` in `src/window.c` maps `configured_depth == 0 ? 1 : configured_depth`.

- Observation: `create_puzzles 1` can take longer than a short smoke timeout in this environment because each candidate
  requires repeated depth-10 scans inside a full self-play loop.
  Evidence: `timeout 30s ./create_puzzles 1` exited with code `124` during smoke validation.

- Observation: The original candidate-building flow mixed engine analysis details with puzzle-rule decisions, which made
  the selection logic harder to compare against a checker player's verbal definition of a puzzle.
  Evidence: `checkers_puzzle_collect_candidate_from_position()` previously combined depth-8 analysis, min-legal-move
  checks, unique-best filtering, static-material comparison, and tactical-line building in one block.

## Decision Log

- Decision: Use engine-level APIs (`Game`, `checkers_ai_alpha_beta_*`, `SgfTree`, `sgf_io`) directly in `create_puzzles`
  instead of routing through GTK model/controller layers.
  Rationale: `create_puzzles` is a CLI pipeline and should avoid GTK dependencies and signal plumbing.
  Date/Author: 2026-03-10 / Codex

- Decision: Introduce a small helper module with pure functions for puzzle eligibility math and file-index scanning.
  Rationale: Enables deterministic unit tests for core puzzle selection rules and naming behavior.
  Date/Author: 2026-03-10 / Codex

- Decision: Refactor the candidate validator around English-style predicate names instead of one engine-centric block.
  Rationale: Keeps the generated-puzzle definition visible in the control flow and makes future rule changes easier to
  audit against checkers terminology.
  Date/Author: 2026-04-06 / Codex

## Outcomes & Retrospective

Implementation is complete for the requested scope.

`create_puzzles` now exists as a standalone CLI generator. It loops through self-play games at effective depth 0,
detects mistakes using depth-10 move-score deltas (threshold 100), filters puzzle starts to post-mistake positions
with at least four legal moves and exactly one top move, and writes SGF puzzles with setup root + tactical line under
`puzzles/puzzle-####.sgf` using highest-existing-index + 1 naming.

A helper module (`puzzle_generation`) encapsulates mistake delta checks, unique-best detection, and next-index
discovery. Unit tests were added for these helpers.

Validation completed with `make all` and `make test`; all tests pass in this environment (with existing expected
headless skips and screenshot warning behavior).

The validator has since been reorganized so the candidate path reads like a rules checklist: a position follows a
serious mistake, the side to move has enough choice, the side to move has a single correct move, the best move wins
real material, and the solution can be shown as a forcing line. The helper module now exposes the "enough choice" and
"single correct move" predicates directly.

## Context and Orientation

`src/ai_alpha_beta.c` already provides:
- move choice (`checkers_ai_alpha_beta_choose_move`),
- scored move lists (`checkers_ai_alpha_beta_analyze_moves`),
- and position eval (`checkers_ai_alpha_beta_evaluate_position`).

`src/sgf_tree.[ch]` + `src/sgf_move_props.[ch]` + `src/sgf_io.[ch]` provide everything needed to create SGF trees,
attach setup properties (`AE`, `AB`, `AW`, `ABK`, `AWK`, `PL`), append move nodes, and write files.

`src/sgf_controller.c` already has a save-position code path with setup-property conventions that can be mirrored in CLI
code (without pulling in GTK controller dependencies).

## Plan of Work

1. Add helper module `src/puzzle_generation.[ch]` with pure functions:
   - mistake delta and threshold check by side-to-move,
   - unique-best test from scored move list with minimum legal move constraint,
   - highest existing puzzle index scan in output directory.
2. Add `tests/test_puzzle_generation.c` for these helpers.
3. Implement `src/create_puzzles.c`:
   - parse `count` argument,
   - ensure/create `puzzles/` directory,
   - compute starting output index,
   - loop games until `count` puzzles emitted,
   - self-play each game using depth-0 mapping (`1`),
   - detect mistakes and candidate puzzle starts,
   - build tactical line to target value,
   - serialize SGF per puzzle with root setup + tactical moves + metadata comment.
4. Wire new binary/test in `Makefile` and update `.gitignore` for `/create_puzzles`.
5. Update `src/OVERVIEW.md` with the new CLI and helper module.
6. Validate with `make all` and `make test`.

## Concrete Steps

From `/home/jerome/Data/gcheckers`:

1. Add files:
   - `src/puzzle_generation.h`
   - `src/puzzle_generation.c`
   - `src/create_puzzles.c`
   - `tests/test_puzzle_generation.c`
2. Update `Makefile`:
   - include `create_puzzles` in `all`,
   - add `test_puzzle_generation` to `test` target and test build recipe,
   - add binary recipe for `create_puzzles`.
3. Update `.gitignore` with `/create_puzzles`.
4. Update `src/OVERVIEW.md`.
5. Run:
   - `make all`
   - `make test`
   - `./create_puzzles 1` (smoke)

## Validation and Acceptance

Acceptance criteria:

- `create_puzzles <N>` emits exactly `N` SGF puzzle files under `puzzles/`.
- Output naming starts from highest existing `puzzle-####.sgf` index + 1.
- Puzzle eligibility obeys all confirmed rules:
  - mistake delta >= 100 at depth 10,
  - puzzle start is post-mistake,
  - candidate side has >= 4 legal moves,
  - exactly one top move (no tie for best),
  - continuation stops when depth-0 eval reaches target depth-10 punished value.
- `make all` and `make test` pass.

## Idempotence and Recovery

- Re-running the tool appends new numbered puzzle files without overwriting old files.
- If a game yields no valid puzzle, generator discards it and continues to next game.
- Tactical-line generation has a hard ply cap to avoid infinite loops if target cannot be reached in practice; such
  candidates are discarded.

## Artifacts and Notes

Validation commands to capture as executed:

- `make all`
- `make test`
- `./create_puzzles 1`

## Interfaces and Dependencies

New helper interface (`src/puzzle_generation.h`):

- `gint checkers_puzzle_mistake_delta(CheckersColor turn, gint best_score, gint played_score);`
- `gboolean checkers_puzzle_is_mistake(CheckersColor turn, gint best_score, gint played_score, gint threshold);`
- `gboolean checkers_puzzle_has_unique_best(const CheckersScoredMoveList *moves,
                                           guint min_legal_moves,
                                           gint *out_best_score,
                                           guint *out_best_count);`
- `gboolean checkers_puzzle_find_next_index(const char *dir_path, guint *out_next_index, GError **error);`

New binary:

- `create_puzzles` (CLI): `./create_puzzles <puzzle-count>`

Plan updates:
- 2026-03-10: Created new ExecPlan for `create_puzzles` generator and retired old scope.
- 2026-03-10: Marked implementation complete after adding `create_puzzles`, helper module/tests, and full validation.
- 2026-04-06: Updated the plan after refactoring puzzle validation to use English-style predicate names and helper
  functions that match the verbal puzzle rules more closely.
