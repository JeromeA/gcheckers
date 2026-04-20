# Add a puzzle picker grid with per-puzzle status

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document must be maintained in accordance with `doc/PLANS.md`.

Storage layout used by this plan: keep all local puzzle-progress files together in the existing puzzle-progress state
directory, which is `~/.local/share/gcheckers/puzzle-progress/` by default or the exact path named by
`GCHECKERS_PUZZLE_PROGRESS_DIR` when that override is set. Do not create a separate subdirectory for the attempt
history file or the per-puzzle summary file. Both files live side by side in that one directory.

## Purpose / Big Picture

After this change, choosing `Puzzle` -> `Play puzzles` will no longer immediately start a random puzzle after only
selecting a variant. Instead, the dialog will show the variant selector at the top and, below it, a grid of numbered
square buttons laid out ten per row. Each square represents one puzzle file in the selected variant and shows the
user's past status for that puzzle: white if never tried, green with a check mark if the user has ever solved it, and
red with a cross if the user has tried it but only failed it. Clicking a square starts that exact puzzle.

The visible proof is simple. Launch the app, open the puzzle dialog, switch between variants, and observe that the
grid rebuilds for the selected variant. If a puzzle was solved earlier, its number appears as a green checked square.
If a puzzle was tried and never solved, its number appears as a red crossed square. If it was never tried, it remains
white. Clicking any square closes the dialog and loads that numbered puzzle instead of a random one.

## Progress

- [x] (2026-04-19 00:00Z) Survey `doc/PLANS.md`, the existing puzzle variant and progress ExecPlans, the current
      dialog in `src/puzzle_dialog.c`, the runtime puzzle entry points in `src/window.c`, and the existing progress
      storage/tests, then write this ExecPlan.
- [x] (2026-04-20 00:00Z) Implement `src/puzzle_catalog.c` and `src/puzzle_catalog.h` so one ruleset directory can be
      scanned into sorted explicit puzzle entries with puzzle number, basename, full path, and stable `puzzle_id`.
- [x] (2026-04-20 00:00Z) Add a derived `puzzle-status.json` cache in `src/puzzle_progress.c` and `src/puzzle_progress.h`,
      stored beside `attempt-history.jsonl`, rebuilt from history when missing or corrupt, and used to answer per-puzzle
      `untried` / `failed` / `solved` queries.
- [x] (2026-04-20 00:00Z) Replace the current `Start`-button dialog flow with a variant-aware numbered grid whose
      buttons encode puzzle status and directly start the chosen puzzle.
- [x] (2026-04-20 00:00Z) Update puzzle runtime code so the chosen puzzle path is preserved, the next puzzle flow is
      still coherent, and status data updates whenever terminal attempts rewrite local history.
- [x] (2026-04-20 00:00Z) Add or update focused tests for catalog ordering, summary reduction, dialog rendering,
      direct puzzle launch, and status color/state transitions.
- [x] (2026-04-20 00:00Z) Update `doc/OVERVIEW.md` so the repository description matches the new puzzle picker UI and
      storage split.
- [x] (2026-04-20 00:00Z) Build all binaries with `make all`, run the repository test suite with
      `env -u DISPLAY -u WAYLAND_DISPLAY -u GNOME_SETUP_DISPLAY make test`, and rerun
      `make test_puzzle_progress && build/tests/test_puzzle_progress` explicitly to confirm the new status-cache tests.
- [x] (2026-04-20 00:00Z) Fix the remaining random continuation path so `Next puzzle` advances through the ordered
      variant catalog rather than choosing a random puzzle; add a regression test and a `doc/BUGS.md` entry for the
      behavior.

## Surprises & Discoveries

- Observation: the current puzzle dialog in `src/puzzle_dialog.c` only owns a ruleset dropdown, a summary label, and
  `Cancel`/`Start` buttons. It has no concept of listing puzzle files or rendering per-puzzle state.
  Evidence: `gcheckers_window_present_puzzle_dialog()` builds only those widgets and calls
  `gcheckers_window_start_random_puzzle_mode_for_ruleset()` when `Start` is clicked.

- Observation: the current runtime entry point in `src/window.c` can start puzzle mode only by choosing a random file
  from the selected ruleset directory.
  Evidence: `gcheckers_window_start_random_puzzle_mode_for_ruleset()` scans `puzzles/<short-name>/`, selects a random
  entry, and then passes that path to `gcheckers_window_enter_puzzle_mode_with_path()`.

- Observation: puzzle progress is currently modeled as per-attempt history for reporting, not as a direct per-puzzle
  lookup table for UI.
  Evidence: `src/puzzle_progress.h` exposes `CheckersPuzzleAttemptRecord` and history-oriented functions such as
  `checkers_puzzle_progress_store_load_attempt_history()`, `append_attempt()`, `replace_attempt()`, and
  `mark_reported()`.

- Observation: tests already cover puzzle dialog opening, ruleset selection, and attempt recording, so the feature can
  be validated by extending existing puzzle-focused test scaffolding rather than inventing a new test harness.
  Evidence: `tests/test_window.c` already drives the `Play puzzles` dialog and exercises puzzle-mode success,
  failure, analyze, and next-puzzle behavior, while `tests/test_puzzle_progress.c` covers persistence and reduction of
  attempt records.

- Observation: storing puzzle number `0` in GTK object data needs an offset because `GUINT_TO_POINTER(0)` becomes
  `NULL`, which breaks test and callback lookup for the first puzzle square.
  Evidence: the first grid implementation could not rediscover button metadata for `puzzle-0000.sgf` until the stored
  `puzzle-number` data was changed to `puzzle_number + 1`.

- Observation: the status cache can be treated as fully derived data and updated centrally inside the history write
  paths, which keeps `window.c` and `puzzle_dialog.c` free of persistence duplication.
  Evidence: `src/puzzle_progress.c` now rewrites `puzzle-status.json` after successful append/replace operations and
  rebuilds it from `attempt-history.jsonl` when the cache is missing or invalid.

- Observation: the first implementation accidentally left one old random-selection path behind in `Next puzzle`, even
  after direct numbered selection had replaced random entry.
  Evidence: `gcheckers_window_on_puzzle_next_clicked()` still called
  `gcheckers_window_start_random_puzzle_mode_for_ruleset()` until the follow-up fix replaced it with ordered catalog
  navigation.

## Decision Log

- Decision: clicking a numbered square will start the puzzle immediately, and the dialog will no longer require a
  separate `Start` button.
  Rationale: the requested behavior says the user has to click a square to start playing that puzzle. Removing the
  extra confirmation keeps the interaction direct and makes the square grid the primary chooser instead of a secondary
  detail below the variant selector.
  Date/Author: 2026-04-19 / Codex

- Decision: represent the grid by existing puzzle files discovered in the selected variant directory, sorted by parsed
  puzzle number, rather than by an assumed contiguous range.
  Rationale: file numbering is currently `puzzle-####.sgf`, but the repository should not silently invent buttons for
  missing numbers or break if a directory has gaps. The UI can still show numbers in ascending order while only
  exposing real puzzles.
  Date/Author: 2026-04-19 / Codex

- Decision: keep the raw per-attempt history for reporting and analytics, but add a derived per-puzzle summary layer
  for the dialog.
  Rationale: the existing JSONL history is still the right source for upload payloads and future calibration work
  because it preserves every attempt. The new UI needs a fast answer to a different question: for each puzzle, has the
  user never tried it, ever solved it, or only failed it? A summary cache or snapshot keyed by puzzle identity is a
  better fit for that query than repeatedly reducing the full attempt log in UI code.
  Date/Author: 2026-04-19 / Codex

- Decision: treat a puzzle as `solved` if any recorded attempt for that puzzle ended in `success`, treat it as
  `failed` if it has at least one recorded attempt and none ended in `success`, and treat unresolved/no-attempt state
  as `untried`.
  Rationale: this exactly matches the requested color semantics. An earlier success should dominate later failures so a
  once-solved puzzle stays visibly solved.
  Date/Author: 2026-04-19 / Codex

- Decision: key the summary by the stable `puzzle_id` that already includes ruleset plus basename, for example
  `russian/puzzle-0007.sgf`.
  Rationale: this identity already exists in the attempt records, matches the installed puzzle layout, and avoids
  collisions between variants that may share the same number.
  Date/Author: 2026-04-19 / Codex

## Outcomes & Retrospective

Implementation is complete. The puzzle dialog now shows a scrollable ten-column numbered grid per ruleset, clicking a
square starts that exact puzzle, and solved/failed status is persisted locally in `puzzle-status.json` beside the
existing `attempt-history.jsonl`. Runtime entry gained an explicit path-based start helper, `Next puzzle` still stays
within the active ruleset, and the new `src/puzzle_catalog.c` keeps UI and runtime selection grounded in one sorted
view of each ruleset directory.

The main lesson from implementation is that the summary cache should stay derived data. Treating history as
authoritative and the cache as rebuildable made error recovery straightforward and kept the UI code simple. The one
important follow-up discovered during manual use was that `Next puzzle` must obey the same ordered catalog semantics as
the picker itself. That fix is now included, so puzzle mode no longer contains any runtime random-selection path.

## Context and Orientation

Puzzle selection currently starts in `src/puzzle_dialog.c`. That module creates a small modal GTK window titled
`Play puzzles` and lets the user choose one `PlayerRuleset`. After clicking `Start`, it calls back into the main
window. The main window type is `GCheckersWindow` in `src/window.c`.

Puzzle loading itself happens in `src/window.c`. The important current helpers are:

- `gcheckers_window_start_random_puzzle_mode_for_ruleset()`, which scans the selected variant directory and chooses a
  random puzzle path.
- `gcheckers_window_enter_puzzle_mode_with_path()`, which loads one explicit SGF file, extracts its main-line puzzle
  steps, enters puzzle mode, and records puzzle identity fields such as `self->puzzle_ruleset`, `self->puzzle_path`,
  and `self->puzzle_number`.
- `gcheckers_window_on_puzzle_next_clicked()`, which currently starts another random puzzle in the same selected
  ruleset.
- `gcheckers_window_puzzle_attempt_ensure_started()`, `gcheckers_window_puzzle_attempt_finish_failure()`,
  `gcheckers_window_puzzle_attempt_finish_analyze()`, and the success path in
  `gcheckers_window_play_next_puzzle_step_if_needed()`, which are the places where attempt history becomes durable.

Puzzle progress persistence lives in `src/puzzle_progress.c` and `src/puzzle_progress.h`. A `CheckersPuzzleAttemptRecord`
stores one attempt, not one puzzle. The current storage file under the user state directory is a JSON Lines file named
`attempt-history.jsonl`, where each line is one JSON object. This format is good for preserving raw history and for
uploading reports, but it is not an ergonomic direct index for a dialog that needs to show one status per puzzle
button.

That user state directory is already the puzzle-progress-specific subdirectory under the gcheckers state/data area:
`~/.local/share/gcheckers/puzzle-progress/` by default. In other words, `attempt-history.jsonl` is not stored at the
top level of `~/.local/share/gcheckers/`; it already lives inside the shared `puzzle-progress/` directory, and the new
summary file should live in that same directory beside it.

When this plan says "catalog", it means the discovered list of actual puzzle files in one ruleset directory, sorted by
their parsed puzzle numbers. When it says "summary", it means the reduced per-puzzle status used by the dialog:
`untried`, `failed`, or `solved`. When it says "cache", it does not require an in-memory-only object; it means a
small persisted snapshot or index that can be loaded quickly and updated atomically when attempt results change.

This plan builds on the already-implemented ruleset-aware puzzle directories described by
`doc/execplan-puzzle-variants.md` and the already-implemented attempt-history reporting pipeline described by
`doc/execplan-puzzle-progress-reporting.md`. Everything needed to implement this feature is restated here because this
document must remain self-contained.

## Plan of Work

### Milestone 1: add a puzzle catalog API for explicit puzzle choice

Start by separating "find available puzzles" from "start a random puzzle". Add a small helper layer, either in a new
module such as `src/puzzle_catalog.c` and `src/puzzle_catalog.h` or as a clearly named private section near the
existing puzzle path helpers, that can:

- resolve the ruleset directory `puzzles/<short-name>`,
- scan only files matching the existing `puzzle-####.sgf` naming convention,
- parse the numeric puzzle number from the basename,
- sort the resulting entries by that number in ascending order,
- expose both the user-visible number and the full path.

Use a plain C struct such as:

    typedef struct {
      guint puzzle_number;
      char *basename;
      char *path;
      char *puzzle_id;
    } CheckersPuzzleCatalogEntry;

Add a loader function with a stable signature such as:

    GPtrArray *checkers_puzzle_catalog_load_for_ruleset(PlayerRuleset ruleset, GError **error);

This catalog is what the dialog will render. It must be independent from GTK so it can be unit-tested without a
display.

In `src/window.c`, keep `gcheckers_window_enter_puzzle_mode_with_path()` as the one function that enters puzzle mode
for an explicit file path. Then split the current random-start helper into two parts: a reusable explicit-start helper
that accepts a catalog entry or path, and a thin random-picker wrapper that still exists for `Next puzzle` if that
behavior is retained.

### Milestone 2: add a per-puzzle summary layer next to attempt history

Extend `src/puzzle_progress.h` and `src/puzzle_progress.c` with a summary data model that the dialog can query without
replaying UI logic. Define:

    typedef enum {
      CHECKERS_PUZZLE_STATUS_UNTRIED = 0,
      CHECKERS_PUZZLE_STATUS_FAILED,
      CHECKERS_PUZZLE_STATUS_SOLVED,
    } CheckersPuzzleStatus;

    typedef struct {
      char *puzzle_id;
      PlayerRuleset puzzle_ruleset;
      guint puzzle_number;
      CheckersPuzzleStatus status;
      gint64 last_finished_unix_ms;
    } CheckersPuzzleStatusEntry;

Expose functions along these lines:

    GHashTable *checkers_puzzle_progress_store_load_status_map(CheckersPuzzleProgressStore *store, GError **error);
    CheckersPuzzleStatus checkers_puzzle_progress_reduce_status_for_attempts(const GPtrArray *attempts,
                                                                             const char *puzzle_id);

The first implementation should persist a dedicated summary file in the same state directory, for example
`puzzle-status.json`. That file should map `puzzle_id` to summary status plus the small metadata needed for stable
rebuilds. Update it whenever an attempt becomes terminal. The summary file should be rewritten atomically, just as the
history rewrite path already handles full-file replacement. If the summary file is missing or invalid, rebuild it from
`attempt-history.jsonl`, write it back, and continue. This gives the dialog fast startup while preserving a safe
recovery path from the authoritative attempt history.

Concretely, the directory layout should be:

    ~/.local/share/gcheckers/puzzle-progress/attempt-history.jsonl
    ~/.local/share/gcheckers/puzzle-progress/puzzle-status.json

When `GCHECKERS_PUZZLE_PROGRESS_DIR` is set, use that directory directly and still place both files side by side:

    $GCHECKERS_PUZZLE_PROGRESS_DIR/attempt-history.jsonl
    $GCHECKERS_PUZZLE_PROGRESS_DIR/puzzle-status.json

Do not change the reporting payload to read from the summary file. Reporting continues to use full attempt history.
The summary exists only to support local UI state and must always be derivable from history if it is lost.

### Milestone 3: redesign the dialog around direct puzzle selection

Replace the current `Start`-button flow in `src/puzzle_dialog.c` with a dialog that contains:

- the existing variant dropdown,
- the existing one-line summary label under the dropdown,
- a scrollable puzzle grid below it,
- a `Cancel` button,
- no `Start` button.

The grid should be implemented as a `GtkGrid` inside a `GtkScrolledWindow`, with exactly ten columns. Each puzzle is a
square `GtkButton` whose label includes the puzzle number. The button should also carry a simple icon or glyph for the
status:

- `untried`: white background, number only,
- `solved`: green background, number plus a check mark,
- `failed`: red background, number plus a cross.

Use repository-local CSS classes added through `gcheckers_style_init()` rather than ad hoc inline styling. For
example, assign classes such as `puzzle-picker-button`, `puzzle-picker-solved`, and `puzzle-picker-failed`. The
button should also have a tooltip that includes the ruleset name and exact puzzle number, which will help both manual
testing and future accessibility work.

When the dropdown selection changes, destroy and rebuild the grid from the selected ruleset's catalog plus the loaded
status map. Rebuilds must be idempotent and must not leak old button signal data. If the selected ruleset has no
puzzles, show an inline label such as `No puzzles found for this variant.` in place of the grid rather than silently
leaving the area blank.

When a square is clicked, the dialog must call a new explicit-start helper in `src/window.c`, passing the exact
puzzle path and ruleset, then close itself only if puzzle startup succeeds.

### Milestone 4: keep runtime state and next-puzzle behavior coherent

Once direct puzzle launch exists, update `src/window.c` so the chosen puzzle is reflected clearly in runtime state.
The window already stores `self->puzzle_path`, `self->puzzle_ruleset`, and `self->puzzle_number`; keep using those as
the active-puzzle identity and ensure they are set consistently whether the puzzle was started from a random choice or
from a clicked grid button.

Add a dedicated helper such as:

    gboolean gcheckers_window_start_puzzle_mode_for_path(GCheckersWindow *self,
                                                         PlayerRuleset ruleset,
                                                         const char *path);

This helper should validate that the chosen path belongs to the requested ruleset directory before entering puzzle
mode. That avoids accidental mismatches between the dialog state and the runtime state.

For `Next puzzle`, advance through the sorted catalog for the active ruleset rather than using a random chooser. The
button should move from the current puzzle to the next higher-numbered puzzle in that ruleset, wrapping to the first
entry only after the last one. Document this in code and tests so puzzle continuation obeys the same numbering model as
the picker grid.

### Milestone 5: update status data when attempts resolve

The per-puzzle summary must stay synchronized with attempt history. Update the attempt-finalization helpers in
`src/window.c` so that after a terminal result is persisted, the corresponding summary entry is also updated through
`src/puzzle_progress.c`. Do not duplicate reduction logic in `window.c`; give `puzzle_progress.c` one function that
accepts the finished attempt record and updates both the history-derived summary and any on-disk cache.

This is also the right place to formalize how `analyze` contributes to summary status. `analyze` should count as a
non-success attempt for the purpose of the dialog, because the requested UI has only three states and does not define a
separate color for "used analysis". Therefore, if a puzzle has only `failure` and `analyze` records and no `success`,
its summary status is `failed`.

If the implementation reveals that unresolved attempts should also count as tried once the user makes a move, do not
encode that in the first version. The requested dialog semantics are explicit: white for never tried, green for ever
solved, red for only failed. Since the current attempt model persists only terminal outcomes for finished attempts and
records nothing for entering then leaving without a move, keep the summary aligned with that existing behavior.

### Milestone 6: expand tests and documentation

Add small non-GTK tests for the new catalog and summary behavior. At minimum:

- a catalog test that creates variant directories with `puzzle-0007.sgf`, `puzzle-0002.sgf`, and an unrelated file,
  then asserts that only puzzle files are returned and that they are sorted as `2, 7`,
- a summary reduction test that proves `success` dominates earlier or later failures,
- a summary persistence test that proves a missing or corrupt summary file is rebuilt from attempt history,
- a summary update test that proves terminal `analyze` without any success yields `failed`.

Extend `tests/test_window.c` with GTK-level cases that:

- open `Play puzzles`, confirm the dialog contains the variant dropdown and a numbered button grid instead of a
  `Start` button,
- click a specific numbered puzzle button and confirm that exact SGF file loads,
- solve a puzzle, reopen the dialog, and confirm the same numbered button is now marked solved,
- fail a puzzle without ever solving it, reopen the dialog, and confirm the button is marked failed,
- switch variants and confirm the grid rebuilds from the other ruleset directory.

Prefer adding helper functions in `tests/test_window.c` to find puzzle-picker buttons by number and to inspect their
CSS classes or icon child widgets. Avoid brittle tree walks that depend on incidental GTK widget ordering when a
clearer helper can search by stored widget data.

Finally, update `doc/OVERVIEW.md` so the entries for `src/puzzle_dialog.c`, `src/window.c`, and `src/puzzle_progress.c`
describe the numbered picker grid and the split between attempt history and per-puzzle summary state. This is a
feature, not a product bug fix, so `doc/BUGS.md` does not need a new entry unless implementation uncovers a specific
pre-existing defect.

## Concrete Steps

Work from the repository root:

    cd /home/jerome/Data/gcheckers

Build everything after the code changes:

    make all

Run the full test suite in the standard displayless mode used by this repository:

    env -u DISPLAY -u WAYLAND_DISPLAY -u GNOME_SETUP_DISPLAY make test

Run focused non-GTK puzzle tests during development:

    build/tests/test_puzzle_progress

If a display backend is available, run the focused GTK puzzle tests that cover the picker flow:

    build/tests/test_window -p /gcheckers-window/puzzle-mode
    build/tests/test_window -p /gcheckers-window/puzzle-first-move-failure
    build/tests/test_window -p /gcheckers-window/puzzle-next-keeps-selected-ruleset

Add new focused test paths for the picker grid and run them directly the same way. Expected outcomes:

- `make all` completes without warnings,
- `make test` passes, except for any already-known unrelated skipped GTK cases when no display is available,
- the new catalog and summary tests fail before implementation and pass after,
- the new picker-grid window tests fail before implementation and pass after when a GTK display is available.

## Validation and Acceptance

Acceptance is behavioral, not just compilational.

First, start the application in an environment with puzzle files in at least two ruleset directories and some recorded
puzzle progress. Open `Puzzle` -> `Play puzzles`. The dialog must show the ruleset dropdown at the top and a grid of
square numbered puzzle buttons below it, with ten buttons per row. There must be no `Start` button.

Second, click one numbered button. The dialog should close and the main window should enter puzzle mode for that exact
puzzle file. The window title and puzzle message must reflect the clicked number, not a random puzzle.

Third, solve one puzzle and fail another. Reopen the dialog. The solved puzzle button must be green and visibly marked
with a check. The failed-only puzzle button must be red and visibly marked with a cross. A puzzle that was never
attempted must still be white.

Fourth, switch the variant dropdown. The grid must rebuild for that ruleset only. Puzzle numbers and statuses must not
bleed across variants.

Finally, restart the application and reopen the dialog. The same per-puzzle statuses must still be present, proving
that the summary persistence works across restarts.

## Idempotence and Recovery

The catalog scan is naturally idempotent because it reads the puzzle directories without mutating them. The summary
cache must also be idempotent: rebuilding it from `attempt-history.jsonl` should produce the same logical statuses
every time. If the summary file is missing, unreadable, or contains an unknown schema version, delete only that
derived file, rebuild it from history, and continue. Do not delete or rewrite the raw attempt history except through
the already-established append/replace/mark-reported paths.

If an implementation step partially lands and the dialog UI breaks, the safest rollback is to preserve the new
catalog/summary helpers and temporarily route `puzzle-play` back through the old random-start helper while the GTK UI
is repaired. Avoid destructive migration steps because the new summary file is derived data and the authoritative
history file remains intact.

## Artifacts and Notes

Expected user-visible grid examples after implementation:

    [ 1 ] [ 2 ✓ ] [ 3 ] [ 4 ✗ ] [ 5 ] [ 6 ] [ 7 ✓ ] [ 8 ] [ 9 ] [ 10 ]
    [ 11 ] [ 12 ] [ 13 ✗ ] ...

Expected summary reduction semantics:

    attempts for russian/puzzle-0007.sgf = [failure, analyze] -> failed
    attempts for russian/puzzle-0007.sgf = [failure, success] -> solved
    attempts for russian/puzzle-0007.sgf = [] -> untried

Suggested summary file shape if JSON is used:

    {
      "schema_version": 1,
      "puzzles": {
        "russian/puzzle-0007.sgf": {
          "ruleset": "russian",
          "puzzle_number": 7,
          "status": "solved",
          "last_finished_unix_ms": 1713300005000
        }
      }
    }

This structure is intentionally small and fully derivable from `attempt-history.jsonl`.

## Interfaces and Dependencies

Use the existing GLib and GTK stack already present in the repository. Do not add a new serialization or UI
dependency.

At the end of the implementation, the following interfaces should exist in stable form, even if names vary slightly:

In `src/puzzle_progress.h`, define:

    typedef enum {
      CHECKERS_PUZZLE_STATUS_UNTRIED = 0,
      CHECKERS_PUZZLE_STATUS_FAILED,
      CHECKERS_PUZZLE_STATUS_SOLVED,
    } CheckersPuzzleStatus;

    typedef struct {
      char *puzzle_id;
      PlayerRuleset puzzle_ruleset;
      guint puzzle_number;
      CheckersPuzzleStatus status;
      gint64 last_finished_unix_ms;
    } CheckersPuzzleStatusEntry;

    GHashTable *checkers_puzzle_progress_store_load_status_map(CheckersPuzzleProgressStore *store,
                                                               GError **error);
    gboolean checkers_puzzle_progress_store_update_status_for_attempt(CheckersPuzzleProgressStore *store,
                                                                      const CheckersPuzzleAttemptRecord *record,
                                                                      GError **error);

In either `src/puzzle_catalog.h` or a clearly named existing module, define:

    typedef struct {
      guint puzzle_number;
      char *basename;
      char *path;
      char *puzzle_id;
    } CheckersPuzzleCatalogEntry;

    GPtrArray *checkers_puzzle_catalog_load_for_ruleset(PlayerRuleset ruleset, GError **error);

In `src/window.h`, expose or preserve an explicit-start entry point:

    gboolean gcheckers_window_start_puzzle_mode_for_path(GCheckersWindow *self,
                                                         PlayerRuleset ruleset,
                                                         const char *path);

Revision note: updated on 2026-04-20 after implementation to record the chosen on-disk layout
(`attempt-history.jsonl` plus `puzzle-status.json` in the same `puzzle-progress` directory), the new
`src/puzzle_catalog.c` module, the direct-grid dialog flow, and the completed validation commands.
