# Add Generic ASCII Game IO

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document is maintained according to `doc/PLANS.md` from the repository root.

## Purpose / Big Picture

Some games can be represented well as a plain text numbered move list. After this change, the application can load and
save those plain text game files through generic support instead of requiring SGF syntax. Homeworlds will support the
format shown by `big_move_game.txt`, where each non-empty line is a move number, a period, and the backend's usual move
notation, such as `17. H1y3- H1g1>S0(Y3) S0g1>S1(R1) S1g1>H2 H2g!`.

## Progress

- [x] (2026-06-12T09:10Z) Read `big_move_game.txt`, `doc/PLANS.md`, and the existing SGF IO/controller paths.
- [x] (2026-06-12T09:16Z) Add generic ASCII game load/save support using backend move notation and replay validation.
- [x] (2026-06-12T09:17Z) Enable the format for Homeworlds and wire GUI load/save dispatch by extension.
- [x] (2026-06-12T09:19Z) Add tests for parsing, formatting, failure handling, and Homeworlds CLI replay.
- [x] (2026-06-12T09:20Z) Update `doc/OVERVIEW.md` for the source changes.
- [x] (2026-06-12T09:22Z) Run focused build and tests, including the provided `big_move_game.txt` example.

## Surprises & Discoveries

- Observation: The current SGF parser already normalizes backend move notation while loading SGF move nodes.
  Evidence: `src/sgf_io.c` parses `B[]`/`W[]` through `sgf_move_props_parse_notation()`, formats the result again,
  and appends a generic `SgfTree` move node.

- Observation: Replay already validates side-to-move through generic backend callbacks.
  Evidence: `src/sgf_controller.c` uses `backend->position_turn()`, `backend->sgf_color_for_side()`, and
  `backend->apply_move()` when replaying generic SGF nodes.

- Observation: The existing Homeworlds profile-moves SGF test fixture contained future moves that were not legal from
  the first two replayed setup moves.
  Evidence: The new text loader validates the whole file before replay, and the initial text fixture failed with
  `Illegal move on line 3: H1r+`; changing those later fixture moves to legal same-color builds made the test pass.

## Decision Log

- Decision: Represent ASCII games as ordinary `SgfTree` instances after loading.
  Rationale: The UI, replay, navigation, comments, and autosave already work from `SgfTree`. Converting text move lists
  into the same model keeps the feature generic and avoids a parallel game-history model.
  Date/Author: 2026-06-12 / Codex

- Decision: Gate ASCII game IO with explicit backend metadata while reusing existing `parse_move`, `format_move`,
  `apply_move`, and side callbacks.
  Rationale: Not every backend necessarily wants free-form numbered text files accepted in its file dialogs, but games
  that opt in should not need bespoke parser infrastructure for a line-oriented move list.
  Date/Author: 2026-06-12 / Codex

- Decision: Validate the whole numbered text file during load, even if a caller later replays only a prefix.
  Rationale: The text loader returns a complete `SgfTree`; validating every line prevents the tree from storing invalid
  future moves that would only fail much later during navigation or save.
  Date/Author: 2026-06-12 / Codex

- Decision: Save the current SGF branch as ASCII and reject non-move nodes in that branch.
  Rationale: The plain text format has no syntax for SGF comments, setup snapshots, or variations. Rejecting unsupported
  nodes is safer than silently dropping data.
  Date/Author: 2026-06-12 / Codex

## Outcomes & Retrospective

Completed. Generic `game_text_io` can load and save numbered ASCII move lists for backends that opt in, Homeworlds
advertises `.txt` files, the shared controller and file dialog dispatch `.txt` paths to the text IO layer, and the
Homeworlds profiling CLI can replay the same text files. Focused tests pass, and `big_move_game.txt` was accepted by
`build/tools/homeworlds_profile_moves --file big_move_game.txt --moves 2 --depth 0`.

## Context and Orientation

The application stores game history in `SgfTree`, defined in `src/sgf_tree.c` and `src/sgf_tree.h`. SGF file parsing
and writing live in `src/sgf_io.c`. The window file actions in `src/sgf_file_actions.c` currently present SGF-only
filters and call `ggame_sgf_controller_load_file()` and `ggame_sgf_controller_save_file()`, which are implemented in
`src/sgf_controller.c`.

`GameBackend`, defined in `src/game_backend.h`, is the generic interface each game implements. It already provides
move parsing, move formatting, side-to-move lookup, SGF color mapping, position initialization, and move application.
Those callbacks are enough to validate a linear plain text game: start from the backend initial position, parse each
line's move notation, confirm it can be applied, append a move node with the current side's SGF color, then apply the
move to advance the validation position.

Homeworlds implements its backend in `src/games/homeworlds/homeworlds_backend.c`; its move parser and formatter live in
`src/games/homeworlds/homeworlds_game.c`. The sample `big_move_game.txt` is a linear Homeworlds game where lines have
the form `<number>. <move notation>`. Setup moves are included as the first two moves.

## Plan of Work

Add `src/game_text_io.c` and `src/game_text_io.h` with functions that load and save numbered ASCII move lists. The
loader accepts only the line-oriented form: blank lines are ignored; each non-empty line must
start with the expected one-based move number, followed by a period, optional spaces, then non-empty backend move
notation. The loader will parse through the active backend, append canonical formatted moves to a new `SgfTree`, and
apply each move to a temporary position so illegal or wrong-turn text is rejected with a useful line-number error.

Add opt-in metadata to `GameBackend` for plain text game support. Homeworlds sets this metadata. The file dialog
includes text game files when the active backend opts in, and the controller load/save path dispatches based on the
selected path's extension and backend support. SGF remains the default and remains supported exactly as before.

Update tests. `tests/test_sgf_io.c` or a new focused test will cover generic text game parsing and serialization using
Homeworlds. Homeworlds backend tests will verify the backend advertises the format. The sample file itself remains an
untracked user artifact; tests should include a small fixture string rather than relying on the huge or local file.

## Concrete Steps

Work from `/home/jerome/Data/gcheckers`. Edit files with `apply_patch`.

1. Add opt-in fields to `src/game_backend.h` for ASCII game support, a file description, and an extension.
2. Implement generic numbered text load/save functions in `src/game_text_io.c`.
3. Wire `src/sgf_controller.c` and `src/sgf_file_actions.c` so load/save can use either SGF or the new ASCII format.
4. Set the Homeworlds backend text-game metadata in `src/games/homeworlds/homeworlds_backend.c`.
5. Update `src/homeworlds_profile_moves.c` so `--file` accepts Homeworlds `.txt` games.
6. Add tests and update `doc/OVERVIEW.md`.

## Validation and Acceptance

Run focused tests from `/home/jerome/Data/gcheckers`:

    make build/tests/test_sgf_io build/tests/test_homeworlds_backend
    build/tests/test_sgf_io
    build/tests/test_homeworlds_backend

Acceptance is that the new tests pass, a Homeworlds numbered move-list string loads into an `SgfTree` with the expected
move nodes, saving that tree as ASCII produces numbered lines with canonical Homeworlds notation, and invalid line
numbers or illegal move text produce errors naming the failing line.

## Idempotence and Recovery

All changes are ordinary source and test edits. Re-running the loader on the same text produces a fresh tree and does
not mutate global state except through the existing active backend. Keep existing uncommitted profiling files out of
this feature unless explicitly needed.

## Artifacts and Notes

The input example begins:

    1. G1R2b3
    2. Y3G2b3
    3. H1b+
    4. H2b+

These lines should load as alternating Homeworlds setup/play moves, with move colors inferred from the backend's
current position before each move.

Final validation run:

    make build/tests/test_sgf_io build/tests/test_homeworlds_backend
    build/tests/test_sgf_io --profile=homeworlds
    build/tests/test_sgf_io --profile=checkers
    build/tests/test_sgf_io --profile=boop
    build/tests/test_homeworlds_backend
    make build/bin/ghomeworlds
    make build/tools/homeworlds_profile_moves build/tests/test_homeworlds_profile_moves
    build/tests/test_homeworlds_profile_moves
    build/tools/homeworlds_profile_moves --file big_move_game.txt --moves 2 --depth 0

The final command printed:

    Replayed moves from big_move_game.txt (2 requested):
    1. G1R2b3
    2. Y3G2b3

## Interfaces and Dependencies

At the end of the work, `GameBackend` should expose whether a backend supports ASCII game files and the extension to
use in dialogs. Generic IO should expose functions similar to:

    gboolean ggame_text_game_io_load_data(const GameBackend *backend,
                                          const GameBackendVariant *variant_or_null,
                                          const char *content,
                                          SgfTree **out_tree,
                                          GError **error);
    char *ggame_text_game_io_save_data(const GameBackend *backend,
                                       const SgfTree *tree,
                                       GError **error);

The exact names may change to match local style, but callers must not need Homeworlds-specific code.
