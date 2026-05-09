# Refactor create_puzzles around a generic SGF runner

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document follows `doc/PLANS.md` from the repository root.

## Purpose / Big Picture

After this change, `build/tools/checkers_create_puzzles` and `build/tools/boop_create_puzzles` will share one
backend-driven engine for puzzle source generation and analysis. The shared engine will pick the active profile, play a
complete depth-0 game through the profile's `GameBackend`, store that source game as SGF, replay the SGF main line,
log progress, and ask profile-specific puzzle policy code only whether each candidate position is interesting enough
to save as a puzzle. A user will see the same terminal phases for checkers and boop: loaded existing solution keys,
started depth-0 self-play, completed self-play after N moves, then one "Considering move #N" line per analyzed move.

This matters because the current implementation has drifted away from the profile/backend architecture. Checkers and
boop each own their own self-play and replay loops, and the current shared code only formats progress messages. The
intended result is that adding another backend with SGF, move application, and AI support will not require copying a
full create-puzzles program; it should require only puzzle-selection policy.

## Progress

- [x] (2026-05-08 14:57Z) Created this ExecPlan after confirming that the current code has separate checkers and boop
  self-play/replay loops.
- [ ] Introduce the generic SGF runner without changing command-line behavior.
- [ ] Move boop count-mode and SGF-file generation onto the generic runner.
- [ ] Move checkers count-mode and SGF-file generation onto the generic runner.
- [ ] Reduce `src/create_puzzles.c` to generic dispatch and checkers policy glue, or split checkers policy into a
  checkers-owned module if that produces clearer ownership.
- [ ] Remove duplicated progress/game-line code and update documentation/tests.

## Surprises & Discoveries

- Observation: The previous progress fix centralized only output formatting, not orchestration.
  Evidence: `src/create_puzzles.c` has `checkers_puzzle_generate_self_play_line()` and
  `checkers_puzzle_emit_from_line()`, while `src/games/boop/boop_create_puzzles.c` has
  `boop_puzzle_generate_self_play_line()` and `boop_puzzle_emit_from_line()`.

- Observation: The existing backend API is already sufficient for the generic runner's core loop.
  Evidence: `src/game_backend.h` exposes `position_init`, `position_copy`, `position_clear`, `position_outcome`,
  `position_turn`, `apply_move`, `format_move`, `parse_move`, `sgf_color_for_side`, `sgf_apply_setup_node`, and
  `sgf_write_position_node`. `src/ai_search.h` exposes `game_ai_search_choose_move()` and
  `game_ai_search_analyze_moves_cancellable_with_tt()`.

- Observation: SGF can be the durable shared game-line representation; a separate persistent move-blob line is not
  needed.
  Evidence: `src/sgf_tree.h` can build a main line of `SgfNode` objects, and `src/sgf_move_props.c` can parse and
  write moves through the active backend notation.

## Decision Log

- Decision: Use SGF trees and SGF main-line nodes as the shared source-game representation.
  Rationale: The project already has generic SGF parsing, formatting, variant metadata, and backend-owned root
  position snapshots. Persisting a parallel array of backend-sized move blobs would duplicate SGF state and recreate a
  game-specific data path. The runner may allocate a temporary `backend->move_size` buffer while parsing or applying a
  single node, but that buffer is not the architectural representation of a game line.
  Date/Author: 2026-05-08 / Codex.

- Decision: Do not add new `GameBackend` callbacks for this refactor unless implementation proves a concrete missing
  primitive.
  Rationale: The existing backend API was designed for this purpose. Puzzle-specific decisions such as default depth,
  rejection thresholds, duplicate keys, and "interesting enough" belong to create-puzzles policy code, not to every
  game backend.
  Date/Author: 2026-05-08 / Codex.

- Decision: The shared runner owns self-play, SGF replay, move-color validation, progress logging, output indexing,
  and optional `game-####.sgf` source-game saving.
  Rationale: These are not checkers or boop rules. They are the common mechanics of taking one backend-supported game,
  producing a source line, and examining each move in order.
  Date/Author: 2026-05-08 / Codex.

- Decision: Game-specific policy owns candidate selection and puzzle-solution construction.
  Rationale: Checkers puzzles are based on serious mistakes and tactical continuation shape. Boop puzzles are based on
  a clear unique best move in the current position. Those are puzzle-design rules, not generic runner mechanics.
  Date/Author: 2026-05-08 / Codex.

- Decision: Migrate boop first, then checkers.
  Rationale: Boop already uses the generic AI search and has a simpler one-position policy. Moving it first proves the
  runner shape with less risk. Checkers can then be migrated while preserving its more complex serious-mistake and
  synthetic-candidate behavior.
  Date/Author: 2026-05-08 / Codex.

## Outcomes & Retrospective

No implementation has been completed yet. This plan captures the intended architecture and the acceptance checks that
will prove the create-puzzles tools are using a shared backend-driven runner instead of duplicated game-specific
self-play loops.

## Context and Orientation

The repository root is `/home/jerome/Data/gcheckers`. The build uses `Makefile`, and binaries are written under
`build/`.

A "profile" is a runtime selection for one game application, defined in `src/game_app_profile.c`. A profile points to
a `GameBackend`. A "backend" is the generic C interface in `src/game_backend.h` that lets shared code initialize a
position, list or apply moves, format or parse moves, evaluate positions, map internal side numbers to SGF colors, and
save or load root-position SGF setup. Checkers and boop both have backends.

SGF is the file format used by this project to store game trees. An `SgfTree` has a root node, optional root setup
properties, and move nodes. A "main line" is the first-child sequence through that tree, returned as a `GPtrArray` of
`SgfNode *` by `sgf_tree_build_main_line()`. The runner in this plan treats that SGF main line as the canonical source
game. When the runner needs to apply a move, it parses the current SGF node into one temporary backend-sized move
buffer, applies that move through `backend->apply_move()`, then discards the buffer.

The current create-puzzles code is split incorrectly:

`src/create_puzzles.c` is both the launcher and the checkers implementation. It contains checkers-specific self-play,
checkers-specific main-line replay, checkers-specific progress calls, and checkers candidate validation. It dispatches
to boop when the executable name is `boop_create_puzzles`.

`src/games/boop/boop_create_puzzles.c` is a separate boop implementation. It now plays a full depth-0 game before
analysis, but it still has its own self-play and replay loops. That means progress looks similar, but the architecture
is still duplicated.

`src/create_puzzles_progress.c` centralizes terminal progress message formatting, but it does not decide when games
are played or moves are analyzed.

`src/ai_search.c` is the generic backend AI implementation. It can choose a move for any backend with
`supports_ai_search == TRUE` by calling `game_ai_search_choose_move()`, and it can analyze all legal moves with a
shared transposition table through `game_ai_search_analyze_moves_cancellable_with_tt()`.

The term "policy" in this plan means a small game-specific object or table of callbacks used by the generic runner.
Policy code answers questions such as: what output directory should this profile use, what is the default analysis
depth, how are existing solution keys loaded, and does this candidate position produce a puzzle worth saving? Policy
code must not play whole source games, replay SGF files move by move, or print the shared progress phases.

## Plan of Work

Start by adding a generic runner module, tentatively `src/create_puzzles_runner.c` and
`src/create_puzzles_runner.h`. This runner should depend on existing modules only: `game_backend.h`, `ai_search.h`,
`sgf_tree.h`, `sgf_io.h`, `sgf_move_props.h`, `create_puzzles_progress.h`, and `puzzle_catalog.h`. Add these files to
the create-puzzles tool build in `Makefile`.

The runner should expose one configuration type for the common run and one policy type for game-specific decisions.
Keep the first version deliberately small. A suitable shape is:

    typedef struct {
      const GameBackend *backend;
      const GameBackendVariant *variant;
      guint self_play_depth;
      guint analysis_depth;
      guint max_self_play_plies;
      guint tt_size_mb;
      gboolean save_games;
      const char *output_dir;
      GHashTable *existing_solution_keys;
      gpointer policy_data;
    } GGameCreatePuzzlesRunConfig;

    typedef struct {
      gboolean (*consider_move)(const GGameCreatePuzzlesRunConfig *config,
                                const SgfTree *source_tree,
                                const GPtrArray *main_line,
                                guint move_index,
                                gconstpointer position_before,
                                gconstpointer played_move,
                                gconstpointer position_after,
                                GameAiTranspositionTable *tt,
                                guint *inout_next_index,
                                guint limit,
                                guint *out_emitted,
                                GError **error);
      void (*print_report)(gpointer policy_data);
    } GGameCreatePuzzlesPolicy;

This exact signature may be adjusted during implementation, but the important boundary must remain: the runner passes
backend positions and SGF context to policy; policy does not replay source games itself. `position_before` is the
position before the SGF move at `move_index`, `played_move` is the parsed move from that node, and `position_after` is
a copy after applying that move. The runner creates `position_after` with `backend->position_copy()` and
`backend->apply_move()`, so each policy can reason about either side of the move without duplicating replay code.

Implement `ggame_create_puzzles_runner_generate_self_play_tree()`. It should initialize a backend position for the
configured variant, create a fresh `SgfTree`, write the variant metadata with `sgf_io_tree_set_variant()`, write the
root position with `backend->sgf_write_position_node()` when that callback exists, and then play moves while
`backend->position_outcome(position) == GAME_BACKEND_OUTCOME_ONGOING`. For each ply, allocate a temporary move buffer,
call `game_ai_search_choose_move(config->backend, position, config->self_play_depth, move)`, append an SGF node, write
the move with `sgf_move_props_set_move(node, backend->sgf_color_for_side(side), move)`, and apply the move through the
backend. It should call `ggame_create_puzzles_progress_start_self_play()` before the loop and
`ggame_create_puzzles_progress_finish_self_play()` after the loop. The finish label can be built from
`backend->side_label()` for wins and `"draw"` or `"none"` otherwise.

Implement `ggame_create_puzzles_runner_analyze_tree()`. It should read the root variant with `sgf_io_tree_get_variant()`
for file inputs, initialize the backend position with that variant, apply root setup with
`backend->sgf_apply_setup_node()` when present, build the SGF main line, and iterate over move nodes. For each node it
should parse the move with `sgf_move_props_parse_node()`, verify that the SGF color matches
`backend->sgf_color_for_side(backend->position_turn(position))`, format the move for progress, log
`ggame_create_puzzles_progress_consider_move()`, compute `position_after`, call `policy->consider_move()`, then advance
the current position to `position_after`. The same function must be used for self-play trees and user-supplied SGF
files.

Implement common source-game saving in the runner. When `--save-games` is enabled and a policy emits a puzzle, the
saved `game-####.sgf` should be the SGF source tree that the runner generated or loaded, not a policy-owned replay.
This is a key acceptance point because it proves the source game is shared SGF, not a parallel move array.

Move boop onto the runner first. `src/games/boop/boop_create_puzzles.c` should keep boop CLI extras only if they are
truly boop-specific; otherwise common options should be parsed by shared code. Its policy should validate
`position_before` using `game_ai_search_analyze_moves_cancellable_with_tt()`, keep positions with a clear best move,
build a puzzle SGF from `position_before` plus the one best-move solution, and update boop rejection stats. Remove
`boop_puzzle_generate_self_play_line()` and `boop_puzzle_emit_from_line()` once the runner replaces them. The boop file
may still contain seed-position fallback if desired, but that fallback should create a small SGF tree and feed it into
`ggame_create_puzzles_runner_analyze_tree()` rather than bypassing the runner.

Then move checkers onto the runner. Preserve the existing user-facing CLI, including `--ruleset`, `--depth`,
`--synthetic-candidates`, `--save-games`, `--check-existing`, and `--dry-run`. Checkers policy can cast
`position_before` and `position_after` to `const Game *` because the checkers backend's `position_size` is
`sizeof(Game)`. It should keep the current serious-mistake logic, forced-mistake option, continuation validation,
duplicate solution keys, and rejection report. Remove `checkers_puzzle_generate_self_play_line()` and
`checkers_puzzle_emit_from_line()` once the runner replaces them. If generic AI scoring differs from the old
checkers-specific alpha-beta helper, fix the checkers backend/search integration or document the difference in this
plan before changing acceptance; do not add a create-puzzles-only backend bypass.

After both games use the runner, shrink `src/create_puzzles.c`. Its job should be launcher dispatch, common option
parsing, variant resolution, runner configuration, and registration of the selected game's policy. It should not
contain a full checkers game loop. If keeping checkers policy in `src/create_puzzles.c` makes the file too large,
split it into `src/games/checkers/checkers_create_puzzles.c` and `src/games/checkers/checkers_create_puzzles.h`, then
leave `src/create_puzzles.c` as the generic main program.

Update tests and documentation. `tests/test_create_puzzles_check.c` should assert the common progress phases for both
tools. Add a focused runner test if practical, for example `tests/test_create_puzzles_runner.c`, that generates a small
self-play SGF tree for boop and verifies that the tree has a root setup and a non-empty main line. Update
`doc/OVERVIEW.md` to describe the new runner/policy split and update `doc/BUGS.md` only if an implementation bug is
fixed during the refactor.

## Concrete Steps

Work from `/home/jerome/Data/gcheckers`.

Before editing, inspect the current duplication:

    rg -n "generate_self_play_line|emit_from_line|game_ai_search_choose_move|checkers_ai_alpha_beta_choose_move" \
      src/create_puzzles.c src/games/boop/boop_create_puzzles.c

The expected pre-refactor result is that checkers and boop both have their own self-play and line-emission functions.

Add `src/create_puzzles_runner.c` and `src/create_puzzles_runner.h`, then add the runner source to the create-puzzles
binary rule in `Makefile`. Keep `src/create_puzzles_progress.c` as the output-format helper used by the runner.

Implement the runner in small slices. After the self-play-tree function exists, write or run a direct test that proves
it can create an SGF tree with boop. After the analyze-tree function exists, move boop onto it and run the boop
generator smoke test from a temporary directory:

    mkdir -p /tmp/gcheckers-boop-runner-smoke
    cd /tmp/gcheckers-boop-runner-smoke
    /home/jerome/Data/gcheckers/build/tools/boop_create_puzzles --depth 1 --save-games 1

The expected output should include:

    Loaded 0 existing puzzle solution keys
    Playing game at depth 0...
    Played game ended after
    Considering move #1
    Report:
      games processed: 1
      puzzles generated: 1

After checkers uses the runner, run a checkers smoke test from a temporary directory:

    mkdir -p /tmp/gcheckers-checkers-runner-smoke
    cd /tmp/gcheckers-checkers-runner-smoke
    /home/jerome/Data/gcheckers/build/tools/checkers_create_puzzles --ruleset international --depth 1 1

The expected output should include the same shared phases and should save
`puzzles/checkers/international/puzzle-0000.sgf` if a puzzle is found. If depth 1 does not find a puzzle reliably, use
the existing test fixture approach from `tests/test_create_puzzles_check.c` rather than weakening the shared-runner
acceptance.

At the end, run static checks proving the duplicated orchestration is gone:

    rg -n "generate_self_play_line|emit_from_line" src/create_puzzles.c src/games/boop/boop_create_puzzles.c
    rg -n "game_ai_search_choose_move|checkers_ai_alpha_beta_choose_move" src/create_puzzles.c \
      src/games/boop/boop_create_puzzles.c src/create_puzzles_runner.c

The first command should return no game-specific self-play or line-emission functions. The second should show
`game_ai_search_choose_move()` only in the generic runner for self-play. Checkers policy may still call analysis
helpers for candidate validation, but it must not own source-game self-play.

## Validation and Acceptance

Run these build and test commands from `/home/jerome/Data/gcheckers`:

    make all
    make test_create_puzzles_cli test_create_puzzles_check test_puzzle_catalog
    build/tests/test_create_puzzles_cli
    build/tests/test_create_puzzles_check --profile=checkers
    build/tests/test_puzzle_catalog --profile=checkers
    build/tests/test_puzzle_catalog --profile=boop
    git diff --check

If new runner tests are added, include them in the `Makefile` and run them explicitly, for example:

    make test_create_puzzles_runner
    build/tests/test_create_puzzles_runner --profile=boop
    build/tests/test_create_puzzles_runner --profile=checkers

Acceptance is not only that tests pass. The code must also satisfy these observable architecture checks:

`build/tools/boop_create_puzzles --depth 1 --save-games 1`, run from an empty temporary directory, prints one shared
self-play start line before any candidate analysis and saves a source `game-####.sgf` whose main line is the same game
the runner analyzed.

`build/tools/checkers_create_puzzles --ruleset international --depth 1 1`, run from an empty temporary directory or
through the existing focused test fixture, prints the same shared phases and still preserves checkers-specific
rejection reporting.

`src/create_puzzles_runner.c` is the only place where a full source game is played from an initialized backend
position. `src/games/boop/boop_create_puzzles.c` and checkers policy code do not contain loops that repeatedly choose
and apply self-play moves.

`src/create_puzzles_runner.c` is the only place where an SGF source main line is replayed for candidate consideration.
Policy code may inspect `position_before`, `played_move`, `position_after`, and SGF context, but it must not rebuild a
parallel source line by replaying from scratch.

No new `GameBackend` callbacks are added unless this ExecPlan is updated with a concrete reason in `Decision Log` and
the missing primitive cannot be expressed with the existing backend, SGF, and AI search APIs.

## Idempotence and Recovery

The refactor should be implemented additively. Add the runner while the old game-specific paths still compile, migrate
boop, run tests, then migrate checkers, run tests again, and only then remove obsolete helpers. This makes it safe to
pause after each milestone.

Generated smoke-test files should be written under `/tmp` or test-owned temporary directories, not into the repository
`puzzles/` tree. If a manual smoke test is accidentally run from the repository root, inspect `git status --short` and
remove only generated files that are known to be from that run; do not use `git reset --hard` or `git checkout --`.

If a migration step fails, keep the latest `Progress` entry accurate, leave both old and new paths compiling if
possible, and continue from the smallest failing test. Do not change public CLI behavior to make the refactor easier.

## Artifacts and Notes

Current pre-refactor evidence:

    src/create_puzzles.c: checkers_puzzle_generate_self_play_line()
    src/create_puzzles.c: checkers_puzzle_emit_from_line()
    src/games/boop/boop_create_puzzles.c: boop_puzzle_generate_self_play_line()
    src/games/boop/boop_create_puzzles.c: boop_puzzle_emit_from_line()

The expected post-refactor shape is:

    src/create_puzzles_runner.c: plays backend self-play into an SGF tree
    src/create_puzzles_runner.c: replays any SGF main line and calls policy for each move
    src/games/boop/boop_create_puzzles.c: boop puzzle policy only
    src/games/checkers/checkers_create_puzzles.c or src/create_puzzles.c: checkers puzzle policy only

Do not interpret temporary move allocation as "move blobs". The runner may allocate:

    gpointer move = g_malloc0(config->backend->move_size);

only to parse one SGF node or receive one AI-selected move. The stored source game remains SGF.

## Interfaces and Dependencies

Use these existing interfaces:

`GameBackend` from `src/game_backend.h` for all game operations: initialize, copy, clear, get outcome, get side to
move, apply a move, format and parse moves, map side numbers to SGF colors, and read/write root setup.

`game_ai_search_choose_move()` from `src/ai_search.h` for backend-generic self-play. Use self-play depth `0` unless a
future policy explicitly changes that and records the reason in this plan.

`game_ai_search_analyze_moves_cancellable_with_tt()` from `src/ai_search.h` for backend-generic candidate analysis
where practical. Checkers policy may keep checkers-specific helper functions only for puzzle semantics that are not
generic, such as serious-mistake classification and solution-shape rejection.

`SgfTree`, `SgfNode`, `sgf_tree_build_main_line()`, and `sgf_move_props_parse_node()` /
`sgf_move_props_set_move()` for source-game storage and replay.

`game_puzzle_catalog_find_next_index()` from `src/puzzle_catalog.c` for output indexing.

`ggame_create_puzzles_progress_start_self_play()`,
`ggame_create_puzzles_progress_finish_self_play()`, and
`ggame_create_puzzles_progress_consider_move()` from `src/create_puzzles_progress.c` for all shared progress phases.

Revision note: Initial plan created to capture the requested architectural refactor from duplicated game-specific
create-puzzles loops to a shared SGF/backend runner with puzzle-specific policy hooks.
