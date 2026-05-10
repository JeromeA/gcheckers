# Implement boop puzzle generation and playback

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document follows `doc/PLANS.md` from the repository root.

## Purpose / Big Picture

After this change, `make all` builds a working `build/tools/boop_create_puzzles` binary, that binary can generate and
check boop puzzle SGF files under `puzzles/boop/`, and `gboop` can open and play those puzzles through the same
Puzzle menu flow used by checkers. A boop puzzle is a saved boop position plus a main-line solution stored in SGF. The
boop app has no rulesets or variants, so boop puzzle discovery must not require a `RU[...]` ruleset property or a
variant subdirectory.

The implementation must also include a cleanup milestone. The cleanup is not optional: after the feature works, review
the code touched by this plan for pieces that became too large or too game-specific, then simplify them while keeping
the behavior and tests intact.

## Progress

- [x] (2026-05-01 00:00Z) Confirmed the starting point: the worktree contains an earlier
  `boop_create_puzzles` launcher scaffold that still exits as unsupported, and `GGameWindow` still stores puzzle
  state as checkers types.
- [x] (2026-05-01 00:00Z) Wrote this ExecPlan with milestones for boop generation, boop playback, and cleanup.
- [x] (2026-05-01 00:00Z) Implemented a real boop `create_puzzles` path with no `--ruleset` requirement and default
  depth 4.
- [x] (2026-05-01 00:00Z) Made puzzle catalog, dialog, progress, and window playback handle zero-variant backends such
  as boop.
- [x] (2026-05-01 00:00Z) Enabled boop puzzle support in the profile and added tests that prove boop puzzles can be
  generated and opened.
- [x] (2026-05-01 00:00Z) Cleanup milestone: inspected the changed code for growth and simplified shared helpers where
  it makes the code easier to maintain.
- [x] (2026-05-01 00:00Z) Ran `make all`, the focused test matrix, and `git diff --check`. `make test` still fails in
  the GTK SGF view test `/sgf-view/link-angles` when a display is available to the test runner.

## Surprises & Discoveries

- Observation: Boop already has the hard part needed for puzzle files: backend-owned SGF position snapshot hooks can
  write and replay a complete boop root position.
  Evidence: `boop_game_backend` wires `sgf_apply_setup_node` and `sgf_write_position_node`, and
  `tests/test_sgf_controller.c` has a boop snapshot roundtrip.

- Observation: Puzzle playback in `src/window.c` is not generic yet. It stores `PlayerRuleset`,
  `CheckersColor`, and `CheckersMove`, so boop cannot use the current puzzle path even if the profile advertises
  puzzles.
  Evidence: `GGameWindow` has `puzzle_ruleset`, `puzzle_attacker`, and `GArray *puzzle_steps` whose element type is
  `GGameWindowPuzzleStep` containing `CheckersMove`.

- Observation: SGF move colors are backend-specific. Checkers side 0 is white, but boop side 0 is black.
  Evidence: After generic boop playback was added, checkers SGF controller tests exposed that the shared controller was
  assuming side 0 always mapped to SGF black moves.

## Decision Log

- Decision: Boop puzzles will be stored directly under `puzzles/boop/` and use puzzle IDs like
  `boop/puzzle-0000.sgf`.
  Rationale: Boop has no rulesets, and adding a fake `default` variant would recreate the ruleset confusion the user
  reported. The catalog can support a `NULL` variant for backends with `variant_count == 0`.
  Date/Author: 2026-05-01 / Codex.

- Decision: The boop generator will use the generic backend AI API and boop's backend-owned SGF snapshot codec, not the
  checkers alpha-beta and checkers setup helpers.
  Rationale: This keeps behavior profile-driven and avoids adding more checkers assumptions to a new boop path.
  Date/Author: 2026-05-01 / Codex.

- Decision: Puzzle playback will store generic move blobs plus backend side numbers.
  Rationale: This lets checkers and boop share the same window puzzle flow while each backend keeps its own move type,
  notation, and SGF position encoding.
  Date/Author: 2026-05-01 / Codex.

- Decision: SGF move color mapping belongs to the backend.
  Rationale: The shared controller cannot infer a backend's side-to-SGF-color mapping from the side number alone.
  Date/Author: 2026-05-01 / Codex.

## Outcomes & Retrospective

Boop puzzle generation and playback are implemented. `build/tools/boop_create_puzzles` uses the boop profile by
default, rejects `--ruleset`, defaults to depth 4, writes puzzle files under `puzzles/boop/`, and can check existing
boop puzzle files. `gboop` now advertises puzzle support, opens a zero-variant puzzle catalog, and plays puzzles
through the shared window flow.

The cleanup milestone simplified the code added for this feature instead of leaving boop-specific branches in shared
paths. Puzzle playback now stores generic backend move blobs and side numbers, zero-variant catalog path logic is
centralized in catalog helpers, the boop generator shares one SGF line writer, and the SGF controller asks the backend
how to map side numbers to SGF colors.

`make all` and the focused feature matrix pass. Full `make test` remains blocked by `/sgf-view/link-angles` with
`GLib-GObject-FATAL-CRITICAL: invalid (NULL) pointer instance` when the GTK display path is available; the same test
binary skips in a headless direct run. That failure is outside the boop puzzle path covered by this plan.

## Context and Orientation

Runtime profiles live in `src/game_app_profile.c`. A profile points to a `GameBackend`, which is the generic interface
for move generation, move application, AI search, board rendering, SGF move parsing, and SGF position snapshots. Boop's
backend is `src/games/boop/boop_backend.c`; its rules are in `src/games/boop/boop_game.c`; its SGF root-position codec
is `src/games/boop/boop_sgf_position.c`.

`src/create_puzzles.c` is currently the checkers puzzle generator. It has a small launcher helper in
`src/create_puzzles_launcher.c` that picks the active profile from the executable name. The boop launcher name already
exists in the current worktree, but it still reports that boop puzzle generation is unsupported. The implementation
must replace that unsupported path with a real boop path.

Puzzle playback starts in the application menu, which calls `ggame_window_present_puzzle_dialog()` in `src/window.c`.
`src/puzzle_dialog.c` shows a variant chooser and a grid of puzzle files loaded by `src/puzzle_catalog.c`. This must
change for boop because boop has no variants. The window then loads the chosen SGF through `GGameSgfController`,
parses the main-line solution, applies player moves, auto-plays defender moves, records local progress through
`src/puzzle_progress.c`, and enables "Next puzzle" and "Analyze".

The key term "zero-variant backend" means a game backend whose `variant_count` is zero. Boop is such a backend: it has
one ruleset and no selectable ruleset or variant object. A zero-variant puzzle catalog should use `NULL` for the
variant pointer and read files from `puzzles/<game-id>/`.

## Plan of Work

First, implement boop puzzle generation without touching checkers generation behavior. Add a boop-specific generator
module under `src/games/boop/` and have `src/create_puzzles.c` dispatch to it when the launcher profile is `boop`.
The boop CLI should accept `--depth`, `--save-games`, `--check-existing`, and `--dry-run`; it must reject
`--ruleset`; and it must default to depth 4. Generated files go to `puzzles/boop/puzzle-####.sgf`. Saved games, when
requested, go beside them as `game-####.sgf`. The boop generator should use `game_ai_search_analyze_moves...` and
`game_ai_search_choose_move()` with `boop_game_backend`, write root snapshots through
`backend->sgf_write_position_node`, and write solution moves through `sgf_move_props_set_move()`.

Second, make the shared app puzzle path work for boop. `src/puzzle_catalog.c` should accept a `NULL` variant for
zero-variant backends and use `puzzles/<backend-id>/` as the directory. `src/puzzle_dialog.c` should hide the variant
row when the backend has no variants and should still show the puzzle grid. `src/settings_dialog.c` should count
zero-variant catalog entries. `src/window.c` should replace checkers-only puzzle fields and step parsing with generic
backend move storage, backend side labels, and generic move equality/formatting.

Third, add the requested cleanup milestone. After the behavior exists, inspect the changed modules and simplify code
that grew too much. The intended cleanup targets are `src/window.c` puzzle helpers, boop generator helper boundaries,
and repeated zero-variant catalog logic. The cleanup must be behavior-preserving and tested.

Finally, update `doc/OVERVIEW.md` and `doc/BUGS.md`, run `make all`, run focused generator/catalog/window/backend/SGF
tests, and leave the worktree ready for review.

## Concrete Steps

Work from `/home/jerome/Data/gcheckers`.

Run:

    git status --short
    rg -n "supports_puzzles|create_puzzles|puzzle_steps|puzzle_ruleset|variant_count" src tests Makefile

Then implement the generator and playback changes. After each milestone, run the focused tests listed in the
Validation section. The final command set must include:

    make all
    make test_create_puzzles_cli test_create_puzzles_check test_puzzle_catalog
    make test_game_backend test_sgf_controller test_window_boop
    ./build/tests/test_create_puzzles_cli
    ./build/tests/test_create_puzzles_check --profile=checkers
    ./build/tests/test_puzzle_catalog --profile=checkers
    ./build/tests/test_puzzle_catalog --profile=boop
    ./build/tests/test_game_backend --profile=boop
    ./build/tests/test_sgf_controller --profile=boop
    ./build/tests/test_window_boop --profile=boop

If GTK is not available, GTK-dependent window tests may skip, but headless boop puzzle parsing and catalog tests must
pass.

## Validation and Acceptance

`build/tools/boop_create_puzzles --depth 1 <input.sgf>` should create at least one `puzzles/boop/puzzle-####.sgf` when
given a boop SGF whose root position has a clear best move. The generated puzzle should contain no `RU[...]` property,
should contain boop root snapshot properties such as `GBK`, `GBC`, `GWK`, `GWC`, and supply counts, and should contain
a main-line move that can be replayed by the boop backend.

`gboop` should enable the Puzzle menu action. With `GCHECKERS_PUZZLES_DIR` pointing to a directory containing
`puzzles/boop/puzzle-0000.sgf`, the puzzle dialog should show that puzzle without any variant dropdown. Starting the
puzzle should orient the board with the attacker at the bottom, accept the expected boop move, mark the puzzle solved,
and keep progress IDs in the `boop/puzzle-0000.sgf` shape.

The cleanup milestone is accepted when the changed code has no new checkers-only puzzle state in the boop path, no
unsupported boop launcher branch remains, and the new helper boundaries are documented in `doc/OVERVIEW.md`.

## Idempotence and Recovery

Generated puzzle files under `puzzles/boop/` are append-only by index. Re-running the generator may add more files but
should not overwrite existing puzzles. Tests that need temporary puzzle roots must set `GCHECKERS_PUZZLES_DIR` to a
temporary directory and unset it before returning.

If a partial implementation fails, the safe recovery path is to rerun `git status --short`, inspect only the modified
files, and continue from the latest `Progress` entry. Do not reset the worktree because it may contain user work.

## Artifacts and Notes

Validation completed:

    git diff --check
    make all
    build/tests/test_create_puzzles_cli
    build/tests/test_create_puzzles_check --profile=checkers
    build/tests/test_puzzle_catalog --profile=checkers
    build/tests/test_puzzle_catalog --profile=boop
    build/tests/test_game_backend --profile=checkers
    build/tests/test_game_backend --profile=boop
    build/tests/test_sgf_controller --profile=checkers
    build/tests/test_sgf_controller --profile=boop
    build/tests/test_window_boop --profile=boop

Manual boop generator smoke test completed:

    /home/jerome/Data/gcheckers/build/tools/boop_create_puzzles --depth 1 1

Full test caveat:

    make test

fails in `/sgf-view/link-angles` with `GLib-GObject-FATAL-CRITICAL: invalid (NULL) pointer instance` when the test
runner sees a GTK display.

## Interfaces and Dependencies

The boop generator module should expose one entry point:

    int boop_create_puzzles_main(int argc, char **argv, guint default_depth);

`src/create_puzzles.c` should call this when the launcher config selects profile `boop`; checkers should continue to
use the existing checkers parser and generator.

The shared catalog function keeps its existing signature:

    GPtrArray *game_puzzle_catalog_load_variant(const GameBackend *backend,
                                                const GameBackendVariant *variant,
                                                GError **error);

The new rule is that `variant == NULL` is valid only when `backend->variant_count == 0`, in which case the directory is
`puzzles/<backend-id>/` and the puzzle ID is `<backend-id>/<basename>`.

Revision note: Initial plan created to capture the requested boop puzzle generation/playback work plus the mandatory
cleanup milestone.
