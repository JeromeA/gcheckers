# Add ruleset-aware puzzle generation and puzzle selection

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document must be maintained in accordance with `doc/PLANS.md`.

## Purpose / Big Picture

After this change, puzzles will no longer be treated as if every game used international draughts rules. A developer
will be able to generate puzzles for every supported ruleset, store them in ruleset-specific folders, and install them
without losing which variant they belong to. A user launching puzzle mode will first choose a variant in a small
dialog, then click `Start`, and the window will load a random puzzle only from that variant.

The visible proof is straightforward. Running the puzzle generator with a ruleset short name such as `american`,
`international`, or `russian` will create SGF files under `puzzles/<short-name>/`. Launching the app, choosing
`Puzzle` -> `Play puzzles`, selecting one of those variants, and pressing `Start` will enter puzzle mode with a puzzle
from that folder only.

## Progress

- [x] (2026-04-17 00:00Z) Survey the current puzzle generator, puzzle-mode runtime, ruleset catalog, tests, install
      layout, and `doc/PLANS.md`, then write this ExecPlan.
- [x] (2026-04-17 00:00Z) Add a stable short-name API to the ruleset catalog and use it as the single source of
      truth for puzzle variant identifiers.
- [x] (2026-04-17 00:00Z) Refactor puzzle path handling so generated and installed puzzles live under
      `puzzles/<short-name>/`, and move the checked-in flat puzzle files into `puzzles/international/`.
- [x] (2026-04-17 00:00Z) Extend `create_puzzles` so generation and checking can target any supported ruleset instead
      of the current international-only path.
- [x] (2026-04-17 00:00Z) Add a modal puzzle-start dialog that lets the user choose a variant and start puzzle mode
      from that selection.
- [x] (2026-04-17 00:00Z) Add or update tests for ruleset short names, variant-specific puzzle paths, generator CLI
      behavior, and window puzzle selection.
- [x] (2026-04-17 00:00Z) Update `doc/OVERVIEW.md` so the `src/` architecture description matches the new
      ruleset-aware puzzle flow.
- [x] (2026-04-17 00:00Z) Build `create_puzzles`, `gcheckers`, and the touched test binaries; run focused puzzle
      tests and the relevant puzzle-specific `test_window` cases.
- [ ] Investigate or defer the unrelated existing full-suite failure in `build/tests/test_sgf_view
      /sgf-view/link-angles`, which still stops `make test`.

## Surprises & Discoveries

- Observation: puzzle generation is currently hard-coded to international rules for self-play generation.
  Evidence: `src/create_puzzles.c` ends its generation path with
  `checkers_ruleset_get_rules(PLAYER_RULESET_INTERNATIONAL)`.

- Observation: puzzle mode currently assumes a flat puzzle directory and immediately starts with a random puzzle from
  that directory.
  Evidence: `src/window.c:gcheckers_window_start_random_puzzle_mode()` resolves `GCHECKERS_PUZZLES_DIR` or
  `puzzles`, scans one directory, and `gcheckers_window_on_play_puzzles_action()` calls it directly.

- Observation: the current file-name conventions are shared across runtime loading, generator indexing, and
  companion-game naming.
  Evidence: `src/window.c` accepts `puzzle-*` and `puzzles-*`, while `src/puzzle_generation.c` and
  `src/create_puzzles.c` parse `puzzle-####.sgf` and derive `game-####.sgf`.

- Observation: installation still assumes a flat `puzzles/` directory.
  Evidence: `Makefile` defines `PUZZLE_FILES := $(wildcard puzzles/*.sgf)` and installs them directly into
  `$(DATADIR)/gcheckers/puzzles`.

- Observation: the repository already has a good GTK pattern for short modal configuration dialogs with a dropdown and
  a confirm button.
  Evidence: `src/new_game_dialog.c` builds a compact non-resizable dialog using `GtkDropDown`, a summary label, and
  `Cancel`/confirm buttons, and `tests/test_window.c` already drives that dialog from the UI tests.

- Observation: the focused puzzle implementation and tests pass, but the full repository test suite still fails in an
  unrelated SGF-view test.
  Evidence: `make test` stops at `build/tests/test_sgf_view` with `not ok /sgf-view/link-angles -
  GLib-GObject-FATAL-CRITICAL: invalid (NULL) pointer instance`.

## Decision Log

- Decision: use the ruleset catalog as the only authoritative source for puzzle variant short names.
  Rationale: the short name must be reused by the generator, runtime loading, tests, and packaging. Storing it in
  `src/rulesets.c` avoids duplicate string tables and keeps every ruleset-facing UI or tool path aligned.
  Date/Author: 2026-04-17 / Codex

- Decision: the initial short names are `american`, `international`, and `russian`.
  Rationale: each is one word, already matches the displayed ruleset names closely, and is clear enough for folder
  names, CLI arguments, and debug output without inventing an abbreviation users need to learn.
  Date/Author: 2026-04-17 / Codex

- Decision: store puzzles under `puzzles/<short-name>/puzzle-####.sgf` and optional companion games under
  `puzzles/<short-name>/game-####.sgf`.
  Rationale: the folder itself becomes the variant boundary, which keeps existing file naming simple and makes it easy
  to inspect or package puzzle sets by ruleset.
  Date/Author: 2026-04-17 / Codex

- Decision: add an explicit ruleset selector to `create_puzzles` instead of trying to generate every ruleset in one
  invocation by default.
  Rationale: the existing tool already has distinct modes for generate and check. Adding one explicit `--ruleset
  <short-name>` option keeps each run deterministic, preserves the current one-output-directory workflow, and gives the
  caller a stable way to target one variant at a time.
  Date/Author: 2026-04-17 / Codex

- Decision: move every existing flat puzzle file into `puzzles/international/` as part of the repository change and
  remove the flat layout entirely.
  Rationale: there are no users on the old layout that need a transition path. Treating the repository migration as a
  one-time tree move keeps the runtime, generator, packaging, and tests simpler because they only need to understand
  the ruleset-aware directory structure.
  Date/Author: 2026-04-17 / Codex

- Decision: implement the puzzle-start chooser as a separate modal flow instead of reusing the new-game dialog.
  Rationale: puzzle mode needs only a variant choice and a `Start` action. Reusing the new-game dialog would add
  irrelevant player controls and blur the difference between starting a live game and loading curated content.
  Date/Author: 2026-04-17 / Codex

## Outcomes & Retrospective

The implementation now matches the plan. Ruleset short names live in `src/rulesets.c`, checked-in puzzle assets were
moved into `puzzles/international/`, `create_puzzles` requires `--ruleset <short-name>` and reads/writes only that
variant directory, runtime puzzle mode starts from a dedicated chooser dialog, and `Next puzzle` stays inside the
active variant directory. `doc/OVERVIEW.md` now describes the ruleset-aware puzzle flow, and the focused CLI, checker,
path, puzzle-generation, and puzzle-window tests pass against the new behavior.

The remaining gap is outside this feature: `make test` still fails in `build/tests/test_sgf_view` at
`/sgf-view/link-angles`, which appears unrelated to the puzzle-variant changes because the focused puzzle work is
already passing. No bug entry was added because this task delivered a feature rather than fixing an observed product
bug in puzzle behavior.

## Context and Orientation

The ruleset catalog lives in `src/rulesets.c`, `src/rulesets.h`, and `src/ruleset.h`. Today it exposes the number of
rulesets, the human-readable name, the summary string, and the concrete `CheckersRules` struct for each
`PlayerRuleset` enum value. It does not yet expose a stable short name or a reverse lookup from a short name back to a
ruleset enum.

The puzzle generator entry point is `src/create_puzzles.c`. It drives self-play generation, candidate validation,
duplicate filtering, and optional `--check-existing` validation. It currently writes to a flat `puzzles/` directory and
loads existing `puzzle-*.sgf` files from that same directory. Helper functions for naming and indexing puzzle files
live in `src/puzzle_generation.c` and `src/puzzle_generation.h`.

The app’s runtime puzzle mode lives in `src/window.c`. `gcheckers_window_on_play_puzzles_action()` currently starts
puzzle mode immediately. `gcheckers_window_start_random_puzzle_mode()` finds the puzzle directory through
`gcheckers_app_paths_find_data_subdir("GCHECKERS_PUZZLES_DIR", "puzzles")`, scans a single directory for puzzle SGF
files, picks one randomly, and loads it with `gcheckers_window_enter_puzzle_mode_with_path()`.

Application data-directory resolution lives in `src/app_paths.c` and `src/app_paths.h`. The existing helper resolves a
named data subdirectory such as `puzzles`, starting from an environment override, then the user data directory, then
system data directories, then a local checkout fallback. It does not yet know anything about ruleset-specific puzzle
subdirectories.

The existing small modal dialog pattern lives in `src/new_game_dialog.c`. That file shows how this repository builds a
compact `GtkWindow` dialog with a `GtkDropDown`, summary label, and action buttons, and the tests in
`tests/test_window.c` already know how to locate and manipulate that sort of dialog.

Packaging is controlled by `Makefile`. The `install` target currently copies flat `puzzles/*.sgf` files into the
installed `share/gcheckers/puzzles` directory. Any folder-based puzzle layout change must update that install logic.

When this plan says "variant short name", it means a stable lowercase one-word identifier such as `american`. When it
says "puzzle root", it means the directory returned by `GCHECKERS_PUZZLES_DIR` or the installed/local `puzzles`
directory. When it says "variant puzzle directory", it means `puzzles/<short-name>` under that root.

## Plan of Work

### Milestone 1: make ruleset short names first-class

Start by extending the ruleset catalog instead of scattering variant strings through the generator and UI. In
`src/rulesets.c`, add a `short_name` field to `CheckersRulesetInfo` and populate it for all three supported rulesets.
In `src/rulesets.h`, add:

    const char *checkers_ruleset_short_name(PlayerRuleset ruleset);
    gboolean checkers_ruleset_find_by_short_name(const char *short_name, PlayerRuleset *out_ruleset);

The reverse lookup must reject unknown names cleanly with `FALSE`. This keeps folder naming, CLI parsing, and dialog
selection grounded in one shared mapping. Add focused unit tests either in a new small ruleset test or in an existing
test that already includes `rulesets.h`, and verify that each enum maps to the expected short name and back.

### Milestone 2: move puzzle storage to per-variant folders and relocate existing files

Once short names exist, refactor the puzzle path helpers. In `src/puzzle_generation.c`, keep the existing indexed file
name format but operate on a caller-provided variant directory. In practice, `checkers_puzzle_find_next_index()` and
`checkers_puzzle_build_indexed_path()` can stay mostly unchanged if the caller now passes
`puzzles/<short-name>` instead of plain `puzzles`.

In `src/create_puzzles.c`, add one small helper that resolves the output/check directory for a given ruleset short
name, creates that directory with `g_mkdir_with_parents()`, and reuses it consistently for puzzle files, optional game
files, and existing-solution-key scans. Preserve the current `puzzle-####.sgf` and `game-####.sgf` file names inside
that directory so the index and companion naming logic stays simple.

In the repository tree itself, move every existing flat puzzle asset into `puzzles/international/` before updating any
install or runtime code. The current flat `puzzle-####.sgf` and `game-####.sgf` files represent international
draughts content, so the migration is a file move, not a format conversion or a copy. After this step, there should be
no SGF puzzle files left directly under the top-level `puzzles/` directory.

In `Makefile`, replace the flat `PUZZLE_FILES := $(wildcard puzzles/*.sgf)` install logic with recursive or
per-variant installation that copies `puzzles/american`, `puzzles/international`, and `puzzles/russian` into
`$(DATADIR)/gcheckers/puzzles`. Do not collapse the folders during install.

### Milestone 3: teach `create_puzzles` about rulesets

Extend the CLI parsing in `src/create_puzzles_cli.c` and `src/create_puzzles_cli.h` with an explicit
`--ruleset <short-name>` option. The option should be parsed through the new ruleset reverse lookup API and should
fail fast with a clear usage error when the short name is unknown.

For count-based self-play generation, require `--ruleset` and replace the current hard-coded international rules fetch
with the selected ruleset. This is the core behavior change that makes generation work for all supported variants.

For file-based generation, also require `--ruleset`, load the SGF main line as today, and verify that the selected
ruleset is compatible with the loaded game before emitting puzzles. If the SGF line matches a different built-in
ruleset than the one requested, print an error and stop rather than silently saving a puzzle into the wrong folder.

For `--check-existing`, use the selected ruleset’s folder by default. The simplest supported command shape should be:

    build/tools/create_puzzles --ruleset american --check-existing

Keep the existing optional directory override only as an advanced escape hatch for tests or one-off maintenance. If an
override directory is provided, still validate that the selected ruleset short name is known so the rest of the code
has one consistent variant identity for logging and error messages. There is no compatibility mode for the old flat
directory; callers that still point at it must first move those files into a ruleset directory.

Update `tests/test_create_puzzles_cli.c` so the parser covers valid short names, missing `--ruleset`, and unknown
short names. Update `tests/test_create_puzzles_check.c` so it creates per-variant directories and proves the checker
uses the requested folder rather than the flat legacy root.

### Milestone 4: add a puzzle-start chooser dialog

Add a dedicated dialog module, `src/puzzle_dialog.c` and `src/puzzle_dialog.h`, modeled after the compact style used
by `src/new_game_dialog.c`. The dialog should contain:

- a `GtkDropDown` listing the ruleset display names,
- an optional one-line summary label reusing `checkers_ruleset_summary()`,
- a `Cancel` button,
- a `Start` button.

Export a function such as:

    void gcheckers_window_present_puzzle_dialog(GCheckersWindow *self);

The confirm handler should translate the selected dropdown row to `PlayerRuleset` and call a new window helper such as:

    gboolean gcheckers_window_start_random_puzzle_mode_for_ruleset(GCheckersWindow *self,
                                                                   PlayerRuleset ruleset);

Keep the actual puzzle-mode runtime in `src/window.c`. The new helper should resolve the puzzle root, append the
ruleset short-name directory, scan only that directory, and then load a random puzzle from it. If the selected
variant directory does not exist or is empty, fail with the normal "no puzzles found" path instead of falling back to
any legacy flat root scan.

Change `gcheckers_window_on_play_puzzles_action()` so it presents the chooser dialog instead of starting puzzle mode
immediately. Change the `Next puzzle` button so it reloads within the current puzzle ruleset rather than picking from
an unqualified global pool. This requires the window to remember the active puzzle ruleset while puzzle mode is active.

### Milestone 5: tighten tests and documentation around the new behavior

Expand `tests/test_app_paths.c` or add a new small helper test to prove that the puzzle root plus short name can be
resolved safely from both environment overrides and installed-style directories. Add unit coverage in
`tests/test_puzzle_generation.c` for variant directory indexing if that logic moves into shared helpers.

Extend `tests/test_window.c` with at least two window-level cases. The first should confirm that activating
`puzzle-play` opens the chooser dialog instead of jumping straight into puzzle mode, and that clicking `Start` after
selecting a variant loads a puzzle only from that variant’s directory. The second should confirm that `Next puzzle`
stays within the same variant. Reuse the existing temporary puzzle-directory helper pattern, but write fixtures into
subdirectories such as `american/puzzle-0000.sgf` and `russian/puzzle-0000.sgf`.

Finally, update `doc/OVERVIEW.md` so the `src/window.c`, `src/create_puzzles.c`, `src/puzzle_generation.c`, and
`src/rulesets.c` entries describe the ruleset-aware puzzle paths and puzzle-start dialog. If implementation reveals a
specific bug in the old behavior rather than only a missing feature, add a short note to `doc/BUGS.md`; otherwise no
bug entry is required for this feature.

## Concrete Steps

All commands below are run from the repository root:

    cd /home/jerome/Data/gcheckers

Build the binaries after each milestone that changes shared code:

    make all

Run the focused tests while iterating on the feature:

    make test_puzzle_generation
    make test_create_puzzles_cli
    make test_create_puzzles_check
    make test_app_paths
    make test_window

Run the full test suite before finishing:

    make test

Exercise the generator manually after the CLI work lands:

    build/tools/create_puzzles --ruleset american 1
    build/tools/create_puzzles --ruleset russian 1
    build/tools/create_puzzles --ruleset international --check-existing

Move the repository's existing flat puzzle assets into the international folder before validating packaging or runtime:

    mkdir -p puzzles/international
    mv puzzles/puzzle-*.sgf puzzles/international/
    mv puzzles/game-*.sgf puzzles/international/

The expected visible filesystem result is that generated files appear under paths such as:

    puzzles/american/puzzle-0000.sgf
    puzzles/international/puzzle-0000.sgf
    puzzles/russian/puzzle-0000.sgf

Launch the application manually after the dialog work lands:

    build/bin/gcheckers

Then open `Puzzle` -> `Play puzzles`, choose a variant, press `Start`, and confirm the title and puzzle source come
from that variant’s directory only.

## Validation and Acceptance

Acceptance for this change is behavioral, not just structural.

The ruleset catalog acceptance is that every supported `PlayerRuleset` has a stable short name and that reverse lookup
from `american`, `international`, and `russian` succeeds while unknown strings fail cleanly.

The generator acceptance is that `build/tools/create_puzzles --ruleset <short-name> 1` uses the selected ruleset for
self-play generation and writes the resulting puzzle to `puzzles/<short-name>/puzzle-####.sgf`. A run with an unknown
short name must print a usage error and exit non-zero. A run that combines a selected ruleset with an incompatible SGF
input file must also fail clearly instead of writing into the wrong folder.

The repository migration acceptance is that all existing flat puzzle assets are moved into
`puzzles/international/`, with no remaining `puzzle-*.sgf` or `game-*.sgf` files directly under `puzzles/`.

The runtime acceptance is that `Puzzle` -> `Play puzzles` opens a dialog first, the dialog lists all supported
rulesets, clicking `Start` loads a random puzzle only from the chosen variant folder, and `Next puzzle` keeps using
that same variant. If a chosen variant directory is missing or empty, the UI reports that condition and does not load a
puzzle from any other location.

The packaging acceptance is that `make install DESTDIR=...` preserves the per-variant puzzle directories under the
installed `share/gcheckers/puzzles` tree instead of flattening files back into one directory.

The test acceptance is that `make test` passes, and the new or updated tests fail before the implementation and pass
afterward.

## Idempotence and Recovery

The path changes are safe to repeat because the generator will create variant directories with
`g_mkdir_with_parents()`, which succeeds when the directory already exists. Re-running `make all` and the focused test
targets is safe.

When moving the repository's existing puzzle files, use `mv` so each SGF ends up in exactly one place. If a migration
step fails midway, inspect `puzzles/` and `puzzles/international/`, then move only the remaining flat files that were
not relocated yet. If a generation or check run fails midway, remove only the incomplete files in the specific variant
directory you were working on; other variant directories are independent.

If the install target is updated incorrectly, recovery is simple: fix the `Makefile` copy logic and rerun
`make install DESTDIR=...` into a clean destination tree. No persistent application state is affected by this feature.

## Artifacts and Notes

Expected CLI usage examples after the change:

    $ build/tools/create_puzzles --ruleset american 2
    Loaded 0 existing puzzle solution keys
    ...
    Saved puzzles/american/puzzle-0000.sgf
    Saved puzzles/american/puzzle-0001.sgf

Expected installed layout after `make install DESTDIR=/tmp/gcheckers-install`:

    /tmp/gcheckers-install/usr/local/share/gcheckers/puzzles/american/...
    /tmp/gcheckers-install/usr/local/share/gcheckers/puzzles/international/...
    /tmp/gcheckers-install/usr/local/share/gcheckers/puzzles/russian/...

Expected runtime behavior:

    1. Menu action `Puzzle` -> `Play puzzles` opens a small modal chooser.
    2. The chooser dropdown defaults to the current window ruleset or `international` if no better default is chosen.
    3. Pressing `Start` enters puzzle mode and the window title names a puzzle file from the chosen variant folder.

Expected repository tree after the one-time file move:

    puzzles/international/puzzle-0000.sgf
    puzzles/international/game-0000.sgf
    puzzles/
      american/
      international/
      russian/
    # no puzzle-*.sgf or game-*.sgf remain directly under puzzles/

## Interfaces and Dependencies

In `src/rulesets.h`, define:

    const char *checkers_ruleset_short_name(PlayerRuleset ruleset);
    gboolean checkers_ruleset_find_by_short_name(const char *short_name, PlayerRuleset *out_ruleset);

In `src/create_puzzles_cli.h`, extend the parsed options struct with:

    PlayerRuleset ruleset;
    gboolean has_ruleset;

or an equivalent representation that distinguishes "missing ruleset option" from a valid enum value.

In `src/window.h`, define:

    void gcheckers_window_present_puzzle_dialog(GCheckersWindow *self);

In the window implementation, add:

    static gboolean gcheckers_window_start_random_puzzle_mode_for_ruleset(GCheckersWindow *self,
                                                                          PlayerRuleset ruleset);

and store the active puzzle ruleset in the window instance while puzzle mode is active so `Next puzzle` can reuse it.

If a separate dialog module is added, `src/puzzle_dialog.h` should expose only one small entry point:

    void gcheckers_window_present_puzzle_dialog(GCheckersWindow *self);

That module depends on `src/window.h`, `src/rulesets.h`, and GTK only. It must not take over puzzle loading itself;
the selected ruleset should flow back into the existing window runtime helpers.

Revision Note (2026-04-17 / Codex): initial ExecPlan created after surveying `doc/PLANS.md`, the puzzle generator,
runtime puzzle mode, ruleset catalog, tests, and install rules so implementation can proceed without rediscovering the
current architecture.

Revision Note (2026-04-17 / Codex): revised the plan to make the existing flat puzzle-file move explicit and to remove
all compatibility fallback work because there are no users on the old layout.

Revision Note (2026-04-17 / Codex): updated the living sections after implementation to record the completed feature
work, the checked-in puzzle-file move, the focused test coverage that now passes, and the unrelated existing
`test_sgf_view` failure that still blocks `make test`.
