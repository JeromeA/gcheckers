# Runtime Profile Selection And Unified Build

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document must be maintained in accordance with `doc/PLANS.md`.

## Purpose / Big Picture

After this change, the repository will no longer compile a different shared object graph for `GAME=checkers`,
`GAME=boop`, or `GAME=homeworlds`. Instead, the process will select one `GGameAppProfile` at runtime, every shared
module will read behavior from that profile or its backend hooks, and a plain `make` will build all application
targets without risking stale game-specific object files. The visible proof is that `build/bin/gcheckers`,
`build/bin/gboop`, and `build/bin/ghomeworlds` all build from the same game-independent shared objects, and that the
remaining profile-specific behavior comes from runtime profile data instead of `#if defined(GGAME_GAME_...)`.

## Progress

- [x] (2026-04-30 16:42Z) Audited the current `Makefile`, `src/game_app_profile.[ch]`, and shared GTK modules to
      confirm that profile selection is still compile-time and that `build/obj`, `build/lib/libgame.a`, and several
      test binaries are game-dependent despite using the same output paths.
- [x] (2026-04-30 18:05Z) Replaced compile-time active-profile selection with an always-built runtime registry,
      launcher-selected activation, and a shared `app_main` helper. `gcheckers`, `gboop`, and `ghomeworlds` now all
      select their `GGameAppProfile` at process startup.
- [x] (2026-04-30 18:20Z) Removed `GGAME_GAME_*` branches from shared sources. `src/board_view.c` and
      `src/board_move_overlay.[ch]` now dispatch by active backend/profile at runtime instead of by preprocessor.
- [x] (2026-04-30 19:00Z) Refactored the `Makefile` and the profile-sensitive tests around runtime profile
      selection. `make` now builds all applications by default, `make test` runs an explicit runtime-profile matrix,
      and the Flatpak/install metadata uses explicit install targets instead of `GAME=...`.
- [x] (2026-04-30 19:18Z) Ran validation, updated repository documentation, and captured the remaining unrelated GTK
      failure in the final notes.

## Surprises & Discoveries

- Observation: the current build already exposes the core correctness bug that motivated the refactor. Shared object
  paths such as `build/obj/src/game_app_profile.o` and `build/tests/test_window` are reused even though the compiled
  source and `CFLAGS` change with `GAME`.
  Evidence: `Makefile` sets `GAME_BACKEND_DEFINE := -DGGAME_GAME_*` and also changes `TEST_WINDOW_SRC`,
  `GAME_SRCS`, and `GAME_UI_SRCS`, while keeping `OBJ_DIR := build/obj` and `TESTS_DIR := build/tests`.
- Observation: the “active profile” is not a runtime registry today. It is one compile-time-selected static object in
  `src/game_app_profile.c`.
  Evidence: `ggame_active_app_profile()` returns `&active_app_profile`, and `active_app_profile` is chosen behind
  `#if defined(GGAME_GAME_CHECKERS)` / `..._BOOP` / `..._HOMEWORLDS`.
- Observation: the last compile-time dependency hiding in the tests was not a `#if`, but an old helper token left in
  function declarations after the branch removal.
  Evidence: the first `make test` pass failed in `tests/test_sgf_io.c` on declarations like
  `static void TEST_SGF_IO_CHECKERS_ONLY ...`; removing that token restored runtime-only profile selection there.
- Observation: the repository still has one unrelated pre-existing GTK suite failure outside this refactor.
  Evidence: `make test` still bails out in `build/tests/test_sgf_view` at `/sgf-view/link-angles` with
  `invalid (NULL) pointer instance`, after the refactor-specific profile matrix tests have already compiled cleanly.

## Decision Log

- Decision: keep the existing global `ggame_active_app_profile()` API, but make the returned profile runtime-selected
  by explicit launcher setup instead of compile-time macros.
  Rationale: this preserves most call sites while still making the produced objects game-independent. Passing a
  profile argument through every helper would create a much larger migration without changing the user-visible goal.
  Date/Author: 2026-04-30 / Codex
- Decision: keep application-specific launchers (`gcheckers`, `gboop`, `ghomeworlds`) as tiny runtime-profile
  selectors rather than trying to infer the profile from `argv[0]`.
  Rationale: explicit launchers are easier to test, preserve the existing branded binaries, and keep startup logic
  obvious for a novice reader.
  Date/Author: 2026-04-30 / Codex

## Outcomes & Retrospective

The repository now builds one shared object graph and links three launchers from it: `build/bin/gcheckers`,
`build/bin/gboop`, and `build/bin/ghomeworlds`. Profile selection is runtime-only through launcher setup, the shared
source tree no longer contains `GGAME_GAME_*` branches, and the user-facing build/install metadata no longer depends
on `GAME=...`.

Validation landed in two layers. `make all` succeeds from the unified build. The profile-sensitive binaries pass when
run directly under their intended runtime profiles, including:

- `./build/tests/test_ai_search --profile=checkers`
- `./build/tests/test_game_backend --profile=checkers|boop|homeworlds`
- `./build/tests/test_game_model --profile=checkers|boop|homeworlds`
- `./build/tests/test_sgf_io --profile=checkers|boop|homeworlds`
- `./build/tests/test_app_settings --profile=checkers|boop`
- `./build/tests/test_file_dialog_history --profile=checkers|boop`
- `./build/tests/test_create_puzzles_check --profile=checkers`
- `./build/tests/test_puzzle_catalog --profile=checkers`
- `./build/tests/test_puzzle_progress --profile=checkers`
- `./build/tests/test_board_view --profile=checkers|boop`
- `./build/tests/test_sgf_controller --profile=checkers|boop|homeworlds`
- `./build/tests/test_window --profile=checkers`
- `./build/tests/test_window_boop --profile=boop`
- `./build/tests/test_desktop_metadata`
- `./build/tests/test_flatpak_manifest`

The remaining gap is not caused by the runtime-profile refactor: `make test` still stops at the existing GTK crash in
`/sgf-view/link-angles`.

## Context and Orientation

The repository currently mixes two ideas. Many modules are already runtime-generic: `src/window.c`,
`src/application.c`, `src/sgf_controller.c`, `src/ai_search.c`, and `src/game_model.c` all talk to the selected
backend through `GameBackend` callbacks and `GGameAppProfile` feature flags. However, the choice of which profile is
active is still made at compile time in `src/game_app_profile.c`, and the `Makefile` changes both source lists and
`CFLAGS` according to `GAME`. As a result, the same object path can contain either checkers code or boop code
depending on the last command that built it.

The files that matter most are:

- `src/game_app_profile.[ch]`: defines `GGameAppProfile` and currently chooses one static profile at compile time.
- `src/application.[ch]`: constructs the GTK application and currently assumes the compile-time active profile.
- `src/window.[ch]`: the shared shell used by checkers and boop. It already uses many runtime profile checks.
- `src/board_view.c` and `src/board_move_overlay.[ch]`: shared board widgets that still contain `GGAME_GAME_*`
  preprocessor branches.
- `src/sgf_io.c`, `src/sgf_move_props.c`, `src/puzzle_dialog.c`, `src/new_game_dialog.c`, and
  `src/games/checkers/puzzle_catalog.c`: helpers that currently reach the active backend through
  `GGAME_ACTIVE_GAME_BACKEND`.
- `Makefile`: compiles different game-specific source sets into shared output paths and only builds one game per run.
- `tests/`: many tests currently use `#if defined(GGAME_GAME_...)` to adapt expectations to the selected build.

In this repository, a “profile” means one data object that describes a branded application target: display strings,
settings schema ID, feature flags, UI hooks, layout defaults, and the `GameBackend` implementation that owns rules,
notation, search, and board callbacks. A “runtime-selected profile” means that every compiled binary contains every
profile, but each process chooses exactly one profile before it starts using shared helpers.

## Plan of Work

First, replace the compile-time `active_app_profile` with an always-built registry in `src/game_app_profile.c`. This
registry will define one static profile per app and add lookup helpers such as “find by profile ID” plus a setter for
the process-global active profile. The existing `ggame_active_app_profile()` accessor will remain, but it will return
the runtime-selected profile. The tiny application launchers in `src/` will set the active profile explicitly before
constructing the application or tool they own.

Next, move the remaining compile-time behavior into runtime hooks. `src/application.c` will stop assuming that
Homeworlds needs a separate standalone `main()` path; instead, profile data will decide whether activation uses the
shared shell or a profile-owned stub window creator. `src/board_view.c` and `src/board_move_overlay.[ch]` will stop
using `#if defined(GGAME_GAME_...)`; they will include the needed headers unconditionally and branch at runtime based
on the active backend or profile kind. Any helper that currently reads `GGAME_ACTIVE_GAME_BACKEND` will keep doing so,
but now that macro will be runtime-backed rather than compile-time-backed.

Then, refactor the test harness so each test binary can select its active profile at runtime. For generic tests, the
binary should default to a sensible profile and accept an explicit profile override from the command line. For tests
that only make sense for one game, the test should skip when run under a different profile rather than relying on a
preprocessor branch. The dedicated boop window tests can remain in their own source file if that keeps the coverage
readable, but they will be built by default instead of only under `GAME=boop`.

Finally, simplify the `Makefile` around the new invariant that shared objects are game-independent. Remove the
user-facing `GAME` parameter from normal builds, always compile the full shared source set once, always build the
three application binaries, and wire `make test` to run the intended profile matrix. Update `doc/OVERVIEW.md` to
describe the runtime profile registry instead of compile-time `GAME` selection.

## Concrete Steps

Work from the repository root:

    cd /home/jerome/Data/gcheckers

During implementation, keep the plan current and run focused rebuilds after each milestone. The expected command
sequence is:

    make all
    make test_game_backend test_game_model test_sgf_io test_sgf_controller test_window test_window_boop
    ./build/tests/test_game_backend
    ./build/tests/test_window --profile=checkers
    ./build/tests/test_window_boop --profile=boop

If GTK binaries report display-related skips, capture that outcome in the final notes exactly as emitted by the test
binary.

## Validation and Acceptance

Acceptance is behavioral:

1. `make all` succeeds from a clean tree without requiring `GAME=...`.
2. The build produces `build/bin/gcheckers`, `build/bin/gboop`, and `build/bin/ghomeworlds`.
3. Searching the shared source tree no longer finds `GGAME_GAME_` branches in `src/`.
4. The generic tests that previously depended on `GAME` now pass or skip appropriately when run with explicit runtime
   profile selection.
5. Re-running `make all` after building a different launcher does not require cleaning stale objects to get the right
   binary contents, because the shared objects are no longer profile-dependent.

## Idempotence and Recovery

The plan is additive and safe to repeat. Re-running `make all` or individual test binaries should not depend on build
order anymore once the refactor is complete. If a milestone fails halfway through, the safest recovery path is to keep
the runtime-profile registry compiling first, then remove `#if` users one module at a time while preserving existing
tests.

## Artifacts and Notes

The most important final proof will be:

    rg -n "GGAME_GAME_" src tests Makefile

and a successful `make all` transcript that shows all application binaries being linked from one shared object graph.

## Interfaces and Dependencies

The end state must keep these interfaces available:

- `const GGameAppProfile *ggame_active_app_profile(void);`
- a new runtime setter/lookup API in `src/game_app_profile.[ch]` for selecting the active profile explicitly
- `GGameApplication *ggame_application_new(void);` or a close equivalent that assumes the active profile is already
  selected
- profile-owned UI hooks sufficient for Homeworlds to keep its non-shared-shell stub window without a separate
  compile-time path

The implementation must keep using GLib, GObject, GTK 4, and the existing `GameBackend` abstraction. No new external
libraries are required.

Revision note: created this plan to guide the removal of compile-time `GAME` selection after confirming that shared
build artifacts are still game-dependent.
