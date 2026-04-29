# Integrate compiled game targets into one shared application shell

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document must be maintained in accordance with `doc/PLANS.md`.

## Purpose / Big Picture

After this change, the repository will still be able to build separate user-facing binaries such as `gcheckers` and
`gboop`, but those binaries will come from the same application source files and the same GTK window shell. The active
game will customize the shared app through backend capabilities and small game-specific settings, not by copying or
forking the application.

The visible result is that `build/bin/gcheckers` and `build/bin/gboop` both show the same menu structure, the same SGF
tree/history panel, the same navigation controls, and the same user/computer player controls. Boop will no longer be a
separate focused window in `src/gboop.c`; it will be the same app running the boop backend, with unsupported features
such as puzzles or checkers import disabled or absent according to an explicit feature profile.

## Progress

- [x] (2026-04-28 00:00Z) Read `doc/PLANS.md`, the existing boop ExecPlan, the current `Makefile`, `src/gboop.c`,
      `src/application.c`, `src/window.c`, `src/sgf_controller.c`, and the player controls code, then write this
      initial ExecPlan.
- [ ] Introduce an explicit game application profile as the top-level description of the active compiled app. Review
      `GameBackend` in that context, split its callbacks into feature families where useful, and make the backend
      table owned by or reachable through the profile rather than a separate top-level global concept.
- [ ] Make both `GAME=checkers` and `GAME=boop` build from the same application entry point and window source set.
- [ ] Migrate `GGameWindow` from `GCheckersModel` ownership to `GGameModel` ownership, keeping checkers-only services
      behind feature gates.
- [ ] Generalize `GGameSgfController` so SGF append, replay, navigation, load, and save operate on `GGameModel` and
      backend move callbacks.
- [ ] Move boop supply and promotion controls out of `src/gboop.c` into a reusable game-side-panel hook installed in
      the shared window.
- [ ] Update tests so `GAME=boop` runs real SGF controller/window/player-control coverage instead of skip stubs.
- [ ] Remove or retire the standalone `src/gboop.c` app shell once `gboop` is built from the same sources as
      `gcheckers`.

## Surprises & Discoveries

- Observation: boop already has backend-owned move notation and SGF file IO, but not a user-visible SGF history panel.
  Evidence: `tests/test_sgf_io.c` has `GGAME_GAME_BOOP` tests for `K@a1` and `K@a1+a1,b1,c1`, while `src/gboop.c`
  constructs only a board, status label, supply panels, and a `New Game` button.

- Observation: the current shared SGF controller is still checkers-specific even though `sgf_move_props.c` now calls
  backend parse/format hooks.
  Evidence: `src/sgf_controller.h` exposes `ggame_sgf_controller_set_model(GGameSgfController *, GCheckersModel *)`
  and `ggame_sgf_controller_replay_node_into_game(const SgfNode *, Game *, GError **)`; `src/sgf_controller.c` replays
  through `Game`, `GameState`, and `CheckersMove`.

- Observation: the current shared window shell already contains the UI the user wants boop to share, but its state and
  feature logic are checkers-owned.
  Evidence: `src/window.c` creates `PlayerControlsPanel`, `BoardView`, `GGameSgfController`, navigation actions, SGF
  mode controls, analysis widgets, and menu-backed toolbar actions, but `src/window.h` and `ggame_window_set_model()`
  require `GCheckersModel`.

- Observation: the current build split selects a different application main source for boop.
  Evidence: in `Makefile`, `GAME=checkers` uses `APP_MAIN_SRC := src/gcheckers.c`, while `GAME=boop` uses
  `APP_MAIN_SRC := src/gboop.c`.

## Decision Log

- Decision: keep separate binaries and metadata, but make the C application/window source shared.
  Rationale: the user still expects `gcheckers` and `gboop` as distinct apps with separate IDs and branding, but the
  behavior should diverge through settings and backend capabilities instead of through copied window code.
  Date/Author: 2026-04-28 / Codex

- Decision: use `GGameModel` as the shared window and SGF-controller model type.
  Rationale: `GGameModel` already owns an opaque backend position and can apply backend moves for checkers, boop, and
  Homeworlds. Keeping `GCheckersModel` as the primary window model would preserve the current checkers dependency and
  prevent a real shared app.
  Date/Author: 2026-04-28 / Codex

- Decision: make the game application profile the primary description of a compiled game target, and treat the backend
  as one capability family owned by that profile.
  Rationale: the current `GameBackend` table mixes core rules callbacks, SGF notation, square-grid rendering,
  move-builder interaction, AI search, and display labels. Adding a profile should not create a second parallel source
  of truth. The first milestone must review these fields and either move app-level fields into the profile or group
  backend callbacks behind profile-owned feature structs so shared UI can ask the profile what the app supports.
  Date/Author: 2026-04-28 / Codex

- Decision: unsupported features should be expressed as feature flags or nullable hooks, not by compiling different
  menus.
  Rationale: the menu and toolbar structure should be the same across `gcheckers` and `gboop`. Feature flags let the
  shared app disable or hide actions deliberately while keeping the shell consistent and testable.
  Date/Author: 2026-04-28 / Codex

- Decision: boop-specific supply and promotion UI belongs behind a side-panel hook, not in a separate app.
  Rationale: boop needs controls that checkers does not need, but those controls should plug into the shared window in
  the same place that any future game-specific controls would plug in. This keeps game-specific UI small and local
  without forking the app.
  Date/Author: 2026-04-28 / Codex

## Outcomes & Retrospective

No implementation has been completed yet. This plan records the integration path needed to replace the current split
between the full checkers shell and the smaller boop shell with one shared application shell.

## Context and Orientation

The top-level `Makefile` selects one compiled game through `GAME`. The active backend is currently exposed through
`src/active_game_backend.h` as `GGAME_ACTIVE_GAME_BACKEND`, and the generic backend interface lives in
`src/game_backend.h`. A backend is a table of callbacks for model storage, move generation, move parsing/formatting,
AI search, board rendering, and staged move selection. This table is already doing more than "rules engine" work: it
also contains UI-facing labels and feature availability bits. This plan introduces an app profile as the larger
description of the compiled target, so `GameBackend` must be reviewed as part of that profile instead of being treated
as an unrelated global. Today `GAME=checkers` builds `build/bin/gcheckers` from `src/gcheckers.c`,
`src/application.c`, and `src/window.c`, while `GAME=boop` builds `build/bin/gboop` from `src/gboop.c`.

The current checkers application shell has the full user experience. `src/application.c` defines
`GGameApplication`, installs app actions such as `app.new-game`, `app.import`, `app.settings`, and `app.quit`, and
creates a `GGameWindow`. `src/window.c` defines `GGameWindow`, which owns the board, user/computer player controls,
SGF tree view, SGF navigation actions, analysis drawer, puzzle UI, file actions, and dialogs. This is the shell that
both `gcheckers` and `gboop` should use.

The current boop shell in `src/gboop.c` is intentionally smaller. It creates a plain `GtkApplication`, a
`GGameModel`, a `BoardView`, a status label, side supply panels, boop-specific colors, and the promotion confirmation
button. It does not install the shared menu, SGF controller, SGF view, navigation actions, save/load actions,
analysis UI, or `PlayerControlsPanel`. This file is the duplication this plan removes.

`GGameModel` in `src/game_model.c` is the generic state container. It owns a backend-sized current position and can
initialize, reset, list moves, apply moves, and replace a position through `GameBackend` callbacks. `GCheckersModel`
in `src/games/checkers/checkers_model.c` is an older checkers-specific wrapper used by the current shared window and
SGF controller. It should remain only where checkers-only features still need checkers internals, such as puzzle
generation or existing analysis helpers, while normal play, SGF navigation, and computer play move to `GGameModel`.

`GGameSgfController` in `src/sgf_controller.c` currently owns the SGF tree and view, but still replays through
checkers data structures. SGF means Smart Game Format: in this repository it is a tree of game positions and moves
represented by `SgfTree` and `SgfNode` in `src/sgf_tree.c`. The SGF view is the visible move tree widget implemented
under `src/sgf_view*.c`. A generic SGF controller must be able to append a backend-formatted move, replay a path of
backend-parsed moves into a fresh `GGameModel`, and update `SgfView` for any active backend.

The player controls in `src/player_controls_panel.c` are already mostly generic. They expose two sides, each with a
`User` or `Computer` dropdown, plus a computer depth slider. The shared window should keep this visible for boop. The
side labels must come from `GameBackend.side_label`, so boop displays `Player 1` and `Player 2` while checkers can
continue displaying its current labels.

When this plan says "game application profile" or "profile", it means the top-level static description of the compiled
app target. For example, checkers supports puzzles, setup editing, checkers import, and puzzle progress reporting;
boop initially supports local play, SGF history, save/load of ordinary move trees, and AI search, but not puzzles or
checkers import. The profile should own the backend table or own smaller capability structs that include the backend
rules callbacks. The rules implementation still lives in game modules such as `src/games/boop/boop_game.c`; the
profile is the root object shared code asks for app identity, supported features, optional UI hooks, and the rule
backend needed by `GGameModel`.

## Plan of Work

Start by introducing an explicit app profile, and use that work to review the shape of `GameBackend`. Add a new pair
of files such as `src/game_app_profile.h` and `src/game_app_profile.c`, or replace `src/active_game_backend.h` with a
more complete active profile header. The profile should be selected by the same compile-time `GGAME_GAME_CHECKERS` and
`GGAME_GAME_BOOP` defines as the backend. It should include app identity, window title, install metadata references if
needed, feature flags, nullable hooks for game-specific UI, and a pointer to the game rules capability used by
`GGameModel`.

During this first step, audit every field in `src/game_backend.h`. Fields such as `id`, `display_name`, side labels,
outcome banner text, and high-level support booleans may belong directly on the profile because they describe the app
or UI rather than the low-level rules engine. Fields such as position allocation, move generation, move application,
hashing, and terminal scoring belong in a core rules capability. Fields for square-grid rendering, staged move
selection, SGF notation, and AI search may be split into nested capability structs or may remain in one backend table
temporarily if a full split is too large for M1. The important M1 outcome is that shared code obtains the backend
through the profile, and the plan records which fields were moved immediately, which were grouped, and which remain as
temporary compatibility fields.

Then make the build use the same app source for both checkers and boop. In `Makefile`, change `GAME=boop` so
`APP_MAIN_SRC` points to the same main source as checkers. The simplest end state is that `src/gcheckers.c` becomes a
generic main, or it is renamed to `src/main.c` in a separate small patch. Both `GAME=checkers` and `GAME=boop` should
compile `src/application.c`, `src/window.c`, and the shared UI sources. The per-game values should come from the app
profile and data files, not from a separate `src/gboop.c` application.

Next migrate `GGameWindow` to own `GGameModel`. Change `ggame_window_new()` in `src/window.h` and `src/window.c` to
accept `GGameModel *model`, and change `ggame_window_set_model()` to call `board_view_set_model(self->board_view,
self->game_model)` directly. Keep a checkers compatibility path only where a checkers-only operation genuinely needs
it. For example, puzzle mode can construct or look up a `GCheckersModel` only when the profile says puzzles are
supported and the active backend is checkers. The normal play path must no longer require `GCheckersModel`.

Move the player-control and AI path to `GGameModel`. The existing `PlayerControlsPanel` can remain. In
`src/window.c`, when it is a computer-controlled side's turn, call a generic model/search helper that uses
`GameBackend.list_good_moves` or `ai_search` through `GGameModel` instead of calling checkers-only
`gcheckers_model_choose_best_move()`. If such a helper is missing, add it to `src/game_model.c` with a signature such
as `gboolean ggame_model_choose_best_move(GGameModel *self, guint depth, gpointer out_move)`. The move storage size is
backend-owned, so callers should allocate `backend->move_size` bytes or let the helper allocate and return a boxed
move that the caller frees.

Generalize `GGameSgfController`. Change `ggame_sgf_controller_set_model()` to accept `GGameModel *`. Change replay so
it creates a fresh backend position with `backend->position_init()`, walks the selected SGF node path from root to
target, parses each `B[]` or `W[]` value with `sgf_move_props_try_parse_node()`, checks that the SGF color matches the
position turn when the backend has two sides, applies each move with `backend->apply_move()`, and then publishes the
result with `ggame_model_set_position()`. For checkers setup properties such as `AB`, `AW`, `ABK`, `AWK`, and `PL`,
keep a checkers-only setup helper behind a backend/profile check. Boop can initially support empty-root SGF files plus
ordinary move nodes.

Make `ggame_sgf_controller_apply_move()` generic. It should format the move with `sgf_move_props_format_notation()`,
append it to the SGF tree with `B` or `W` based on `backend->position_turn()`, apply the move through `GGameModel`,
refresh the `SgfView`, and emit the existing signals. It should not enumerate a checkers `MoveList`. If validation is
needed before append, call `backend->apply_move()` on a copied position or rely on `ggame_model_apply_move()` to
reject illegal moves. The current boop backend already validates moves in `boop_position_apply_move()`, so this path
will work for boop once it uses `GGameModel`.

Integrate boop's supply UI into the shared window without copying the boop app. Move the boop-specific supply panel
logic from `src/gboop.c` into a new module such as `src/games/boop/boop_controls.c` and
`src/games/boop/boop_controls.h`, or into `src/games/boop/boop_backend.c` if it stays small. The preferred design is a
game profile hook that receives the shared `GGameModel *` and `BoardView *` and returns a `GtkWidget *` to insert near
the board. The hook should also install boop's candidate preference and completion confirmation callbacks on the
shared `BoardView`. The shared window should call this hook after constructing the board and player controls. Checkers
can leave the hook NULL.

Apply feature flags to actions and menu items. Keep the menu model shape the same in `src/application.c`, but update
action enabled states in `src/window.c` or `src/application.c` based on the active profile. For example,
`app.import`, `win.puzzle-play`, setup edit mode, and checkers-specific save-position behavior should be disabled for
boop until those features are generalized. SGF load/save of ordinary move trees, navigation, new game, user/computer
selection, force move, and view drawer actions should remain enabled for boop. The user should see the same menu bar
and toolbar in both binaries, with unavailable features clearly disabled rather than missing because a different app
was launched.

Retire `src/gboop.c` after the shared shell reaches parity. Do not delete the useful boop control code; move it first.
Once `GAME=boop make all` builds `build/bin/gboop` from the shared application source and manual play shows board,
supply controls, SGF view, and player controls in one window, remove `src/gboop.c` from the build. If keeping a tiny
source file named `src/gboop.c` is helpful for compatibility, it must be no more than the same generic main entry point
used by `gcheckers`; it must not construct its own window.

Finally, convert skipped tests into real coverage. Under `GAME=boop`, `tests/test_sgf_controller.c` should exercise
append, navigation, replay, and file load/save against boop moves. `tests/test_window.c` should verify that the shared
window constructs for boop, shows the player controls, shows the SGF panel, keeps boop's supply controls visible, and
can append a boop move to the SGF tree through normal board interaction. Existing checkers tests should continue to
run in the default build.

## Milestones

### Milestone 1: declare the shared app profile

At the end of this milestone, the active compiled game has one primary profile object that the shared application can
query. This profile owns or directly references the game rules backend. No large UI migration is required yet, but the
old idea that `GGAME_ACTIVE_GAME_BACKEND` is the top-level game description should be retired or wrapped by the
profile. The observable proof is a profile test that can fetch the active rules backend through the profile, reports
checkers supports puzzles and boop does not, and reports both games support SGF files and AI players.

Edit `src/game_app_profile.h` and `src/game_app_profile.c` or a similar pair. Define a struct with stable field names
for display metadata, feature booleans, optional hooks, and the active rules backend. Export one function, for example
`const GGameAppProfile *ggame_active_app_profile(void)`. As part of this milestone, inspect every field in
`src/game_backend.h` and decide whether it should move to the profile, become part of a nested capability family, or
remain temporarily in `GameBackend` for compatibility. At minimum, the profile must own a `const GameBackend *backend`
field or equivalent accessor, and all new shared code should prefer `ggame_active_app_profile()->backend` over
`GGAME_ACTIVE_GAME_BACKEND`.

If the backend table is split in M1, keep the split conservative and mechanical. A good shape is a profile with
`rules`, `square_grid`, `move_builder`, `sgf`, and `ai` capability pointers or embedded structs. The split does not
need to be perfect before later milestones, but the plan must not assume the current flat `GameBackend` table is final.
If a field remains in `GameBackend`, write a short note in the ExecPlan's `Surprises & Discoveries` or `Decision Log`
explaining why it was not moved yet.

Add tests in `tests/test_game_backend.c` or a new `tests/test_game_app_profile.c`. Update `Makefile` so the new source
is included in both app and test builds.

Run from the repository root:

  GAME=boop make -B test_game_backend
  build/tests/test_game_backend
  make -B test_game_backend
  build/tests/test_game_backend

Acceptance is that both builds compile and the tests prove the profile is selected by `GAME`, owns or references the
active backend, and exposes the feature booleans that later milestones will use to keep one shared menu/window shell.

### Milestone 2: build boop from the shared app entry point

At the end of this milestone, `GAME=boop make all` no longer uses a standalone boop application source. The binary may
still have disabled or incomplete shared-window behavior, but it must be created by the same `GGameApplication` and
`GGameWindow` source files as checkers.

Change `Makefile` so `GAME=boop` uses the shared application main source and links the shared application/window
sources. Move any required boop CSS or metadata into profile-controlled shared code. If this requires a temporary
adapter because `GGameWindow` still expects `GCheckersModel`, add the adapter only as a milestone bridge and record it
in `Surprises & Discoveries`; it must be removed by later milestones.

Run:

  GAME=boop make -B all
  make -B all

Acceptance is that both binaries compile from the same app/window sources. Manual startup of `build/bin/gboop` should
show the shared menu bar even if some actions are not functional yet.

### Milestone 3: migrate the shared window to `GGameModel`

At the end of this milestone, normal play in the shared window no longer depends on `GCheckersModel`. The board, move
application, user/computer controls, force move, and status updates should use `GGameModel` and backend callbacks.

Edit `src/window.h`, `src/window.c`, and `src/application.c`. Change constructors and model setters to accept
`GGameModel *`. Update status text and side labels to call `GameBackend.side_label()` and
`GameBackend.position_turn()`. Implement or use a generic AI move helper in `src/game_model.c`. Keep checkers-only
puzzle and analysis code gated behind the app profile so it is not reached in boop.

Run:

  make -B test_window test_game_model test_game_backend all
  build/tests/test_window
  build/tests/test_game_model
  build/tests/test_game_backend
  GAME=boop make -B test_window test_game_model test_game_backend all
  build/tests/test_window
  build/tests/test_game_model
  build/tests/test_game_backend

Acceptance is that checkers tests still pass, and boop no longer skips every window test. At minimum, boop window
construction, side labels, player controls, and basic model-driven board updates must be tested.

### Milestone 4: make SGF controller generic

At the end of this milestone, boop can append moves to the SGF tree, navigate backward and forward, load a boop SGF
file, save it, and replay to the same board position through the visible SGF panel.

Edit `src/sgf_controller.h` and `src/sgf_controller.c` so the controller owns `GGameModel *`. Replace checkers replay
with backend position replay. Keep checkers setup-node support in a helper that runs only for the checkers backend.
Change `ggame_sgf_controller_step_ai_move()` to call the generic AI helper. Update `tests/test_sgf_controller.c` so
the boop build has real tests instead of skip stubs.

Run:

  GAME=boop make -B test_sgf_io test_sgf_controller test_window all
  build/tests/test_sgf_io
  build/tests/test_sgf_controller
  build/tests/test_window
  make -B test_sgf_io test_sgf_controller test_window all
  build/tests/test_sgf_io
  build/tests/test_sgf_controller
  build/tests/test_window

Acceptance is that a boop test can create a game, apply a move through `GGameSgfController`, select the root node to
rewind, select the move node to replay, and observe the same boop board state each time.

### Milestone 5: move boop supply controls into the shared window

At the end of this milestone, boop in the shared app has the side supply piles, active rank selection, promotion
confirmation button, and boop colors that currently live in `src/gboop.c`.

Move the boop-specific code from `src/gboop.c` into a game-specific controls module and expose it through the app
profile hook. The shared window should allocate a place for optional game controls near the board without knowing
boop types. The boop hook should install the existing board-view candidate preference, promotion confirmation, and
selection-changed handlers. Checkers should continue with no extra side panel.

Run:

  GAME=boop make -B test_window test_board_view test_game_backend all
  build/tests/test_window
  build/tests/test_board_view
  build/tests/test_game_backend

Acceptance is that `build/bin/gboop` visibly contains the shared menu, player controls, SGF tree, board, and boop
supply controls in one window. Promotion choices must still require the confirm button where appropriate.

### Milestone 6: remove the duplicate boop shell and harden feature flags

At the end of this milestone, there is no separate boop application implementation. Unsupported actions are disabled
through explicit profile checks, and tests document the expected feature matrix.

Remove `src/gboop.c` from the build or reduce it to the same trivial generic main used by checkers. Update
`doc/OVERVIEW.md` to explain that `gcheckers` and `gboop` share the same app shell. Update `doc/BUGS.md` only if bugs
are fixed during implementation. Convert any remaining `#if defined(GGAME_GAME_BOOP)` skip blocks in SGF/window tests
into real tests or targeted feature-flag assertions.

Run the full required verification:

  git diff --check
  make -B all test
  GAME=boop make -B all test
  build/bin/gcheckers
  build/bin/gboop

Acceptance is that both full builds compile, both test suites pass, and manual startup of both binaries shows the same
application shell with game-specific behavior controlled by the active profile.

## Concrete Steps

Work from the repository root, `/home/jerome/Data/gcheckers`.

Before each milestone, inspect the current worktree:

  git status --short

Do not revert unrelated changes. This repository often has active work in progress. If a file you need already has
changes, read it and adapt rather than restoring it.

For each milestone, make the smallest source changes that satisfy that milestone, then run the commands listed in the
milestone. If a command fails because a GTK display is unavailable, keep the headless skip behavior for display-only
tests but do not skip non-display tests. If a command fails because a test still assumes checkers, either generalize
the test or replace it with a feature-profile assertion that names the unsupported behavior.

After source changes in `src/`, update `doc/OVERVIEW.md` in the same commit. If a bug is found and fixed while
executing this plan, add an entry to `doc/BUGS.md`.

## Validation and Acceptance

The final behavior must be observable in the two binaries.

For `gcheckers`, run:

  make -B all
  build/bin/gcheckers

Expected behavior: the window has the menu bar, toolbar, player controls, board, SGF view, and existing checkers
features. Existing checkers tests pass. Puzzle and import features remain available.

For `gboop`, run:

  GAME=boop make -B all
  build/bin/gboop

Expected behavior: the window has the same menu bar, toolbar, player controls, board, SGF view, and navigation drawer
as `gcheckers`. The boop board uses boop colors and symbols. Boop supply piles and promotion confirmation remain
visible when relevant. The user can set either side to `Computer`, play a move, and see the computer reply through the
same controls used by checkers. The user can play a boop move, see it appended in the SGF view, navigate back to the
root, navigate forward to the move, and observe the board replay correctly.

The final automated verification is:

  git diff --check
  make -B all test
  GAME=boop make -B all test

Expected behavior: both builds complete without warnings. Non-display tests pass. GTK display-dependent tests may
skip only when no display is available, and the skip output should clearly say `GTK display not available`.

## Idempotence and Recovery

All build and test commands in this plan are safe to rerun. They rebuild files under `build/` and do not modify source
files except for generated schema compilation under `data/schemas/gschemas.compiled`, which the project already uses.

If a migration step becomes too large, keep both old and new paths temporarily behind explicit feature checks, but
record the temporary state in `Progress` and `Surprises & Discoveries`. Do not leave boop on the standalone
`src/gboop.c` shell at the end of the plan.

If SGF replay fails for checkers setup-root files after the generic replay migration, restore checkers setup handling
inside a checkers-only helper rather than moving `GGameSgfController` back to `GCheckersModel`.

If a boop UI test cannot run because no GTK display exists, keep a non-display controller/model test that proves the
same behavior. The UI test may skip only the display-specific rendering assertion.

## Artifacts and Notes

The current build split to remove is:

  GAME=checkers:
    APP_MAIN_SRC := src/gcheckers.c
    APP_BIN_NAME := gcheckers

  GAME=boop:
    APP_MAIN_SRC := src/gboop.c
    APP_BIN_NAME := gboop

The desired build shape is:

  GAME=checkers:
    APP_MAIN_SRC := src/gcheckers.c or src/main.c
    APP_BIN_NAME := gcheckers
    active profile := checkers

  GAME=boop:
    APP_MAIN_SRC := the same source used by checkers
    APP_BIN_NAME := gboop
    active profile := boop

The current boop SGF IO proof already exists in `tests/test_sgf_io.c`:

  B[K@a1]
  B[K@a1+a1,b1,c1]

The missing proof to add is a visible controller/window replay path for boop:

  apply boop move through GGameSgfController
  SGF tree has one child move node
  select root
  board returns to empty start
  select child
  board returns to the post-move boop position

## Interfaces and Dependencies

The active app profile should be queryable from shared code without including game-specific headers. The profile is
the top-level game/app descriptor. It should own the rules backend or own a set of backend capability families; shared
code should not treat `GameBackend` as a separate global root once the profile exists. A suitable transitional
interface is:

  typedef struct _GGameAppProfile GGameAppProfile;
  typedef GtkWidget *(*GGameCreateControlsFunc)(GGameModel *model, BoardView *board_view, gpointer *out_state);
  typedef void (*GGameDestroyControlsFunc)(gpointer state);

  struct _GGameAppProfile {
    const char *app_id;
    const char *game_id;
    const char *display_name;
    const char *window_title;
    const GameBackend *backend;
    gboolean supports_sgf_files;
    gboolean supports_ai_players;
    gboolean supports_puzzles;
    gboolean supports_import;
    gboolean supports_setup_editing;
    gboolean supports_analysis;
    GGameCreateControlsFunc create_controls;
    GGameDestroyControlsFunc destroy_controls;
  };

  const GGameAppProfile *ggame_active_app_profile(void);

The exact names may change during implementation if the surrounding code suggests a better local convention, but the
capabilities must remain explicit and testable. The `backend` field above is acceptable as a first migration step. A
more complete M1 may replace it with named capability families such as:

  typedef struct {
    gsize position_size;
    gsize move_size;
    void (*position_init)(gpointer position, const GameBackendVariant *variant_or_null);
    void (*position_clear)(gpointer position);
    void (*position_copy)(gpointer dest, gconstpointer src);
    GameBackendOutcome (*position_outcome)(gconstpointer position);
    guint (*position_turn)(gconstpointer position);
    GameBackendMoveList (*list_moves)(gconstpointer position);
    gboolean (*apply_move)(gpointer position, gconstpointer move);
  } GGameRulesCapability;

  typedef struct {
    gboolean supported;
    gboolean (*format_move)(gconstpointer move, char *buffer, gsize size);
    gboolean (*parse_move)(const char *notation, gpointer out_move);
  } GGameSgfCapability;

The implementation may choose a different exact split, but M1 must review the current `GameBackend` fields in this
spirit. App identity and feature availability belong in the profile. Low-level game rules belong in a rules/backend
capability owned by the profile. Optional surfaces such as SGF, square-grid rendering, staged selection, and AI should
be grouped so the shared app can ask for those features deliberately.

The generic SGF controller should expose `GGameModel`, not `GCheckersModel`:

  GGameSgfController *ggame_sgf_controller_new(BoardView *board_view);
  void ggame_sgf_controller_set_model(GGameSgfController *self, GGameModel *model);
  gboolean ggame_sgf_controller_apply_move(GGameSgfController *self, gconstpointer move);
  gboolean ggame_sgf_controller_step_ai_move(GGameSgfController *self, guint depth, gpointer out_move);

The old checkers replay helper can remain for checkers-specific tests if needed, but it must not be required for boop
or for normal SGF navigation in the shared app.

The generic model should provide a backend-driven AI helper, either by filling caller-provided storage or returning an
allocated move:

  gboolean ggame_model_choose_best_move(GGameModel *self, guint depth, gpointer out_move);

If the implementation chooses an allocated form instead, document ownership clearly and write tests that free the move.

## Revision Notes

2026-04-28 / Codex: Initial ExecPlan written after confirming that boop has backend SGF notation and file IO but still
uses a separate application source, while the full shared window and SGF controller are still checkers-specific.

2026-04-28 / Codex: Revised Milestone 1 and the interface guidance so the new profile is the primary app/game
description. The plan now requires reviewing `GameBackend` fields in that context, with the backend owned by the
profile or split into profile-owned capability families.
