# Enable shared Save Position support for boop

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document must be maintained in accordance with `doc/PLANS.md`.

## Purpose / Big Picture

After this change, `gboop` will enable the existing `Save position...` action and write a position-only SGF file that
can be loaded back into the shared window without falling back to the opening setup. A user will be able to save a
midgame boop position, reopen that file, and see the same board, supplies, and side to move restored.

## Progress

- [x] (2026-05-01 10:00Z) Read `doc/PLANS.md`, `src/sgf_controller.[ch]`, `src/sgf_io.c`,
      `src/game_backend.h`, `src/game_app_profile.c`, `src/games/checkers/checkers_backend.c`,
      `src/games/boop/boop_backend.c`, `src/games/boop/boop_game.[ch]`, and the SGF controller/backend tests to map
      the current checkers-only save-position path.
- [x] (2026-05-01 13:15Z) Added backend SGF position setup/save hooks, moved the shared controller replay/save path
      onto those hooks, and implemented root-position codecs for checkers and boop.
- [x] (2026-05-01 13:35Z) Enabled boop `supports_save_position`, added SGF controller/backend regressions, updated
      `doc/OVERVIEW.md`, and ran the focused validation matrix plus `make all`.

## Surprises & Discoveries

- Observation: the current shared `Save position...` action is only disabled by a profile flag, but the implementation
  underneath is hardwired to `GCheckersModel`.
  Evidence: `src/window.c` enables `sgf-save-position` from `supports_save_position`, while
  `ggame_sgf_controller_save_position_file()` immediately requires `GCHECKERS_IS_MODEL(self->checkers_model)`.

- Observation: generic SGF replay already handles backend moves, but it still skips all root setup for non-checkers
  backends.
  Evidence: `ggame_sgf_controller_replay_node_into_position()` in `src/sgf_controller.c` walks the node path and
  applies `B[]` and `W[]` only.

- Observation: boop does not need SGF `RU[...]` variants for save/load because the SGF layer already treats
  `variant_count == 0` as “no `RU` property allowed”.
  Evidence: `sgf_io_tree_get_variant()` and `sgf_io_tree_set_variant()` in `src/sgf_io.c` special-case
  variantless backends.

- Observation: once SGF position codecs live under the runtime backends, even backend-only test binaries now inherit a
  link dependency on `src/sgf_tree.c`.
  Evidence: `test_game_backend` and `test_boop_backend` failed to link until their Makefile rules started pulling in
  `sgf_tree.c` alongside the backend source set.

## Decision Log

- Decision: use backend-owned SGF position hooks instead of another boop-specific branch in `src/sgf_controller.c`.
  Rationale: the controller should own the shared SGF file flow, while each backend owns only the encoding of its own
  position snapshot.
  Date/Author: 2026-05-01 / Codex

- Decision: keep the SGF file format move-based for the main line, but let backends define custom root setup
  properties for position-only files.
  Rationale: this preserves the existing shared SGF tree behavior while allowing boop to restore supplies and other
  non-move-derived state.
  Date/Author: 2026-05-01 / Codex

- Decision: represent boop position snapshots with boop-owned root properties (`GBK`, `GBC`, `GWK`, `GWC`,
  `GBKS`, `GBCS`, `GWKS`, `GWCS`, and `PL`) instead of trying to overload checkers setup properties or move notation.
  Rationale: boop needs to restore both board pieces and off-board supply counts, and those are not derivable from
  the shared move stream alone.
  Date/Author: 2026-05-01 / Codex

## Outcomes & Retrospective

The implementation landed as one shared controller path plus backend-owned codecs. `GGameSgfController` now replays
setup nodes and saves position-only SGFs through `GameBackend` hooks, checkers moved its existing setup logic into
`checkers_sgf_position.c`, boop adds `boop_sgf_position.c` plus `boop_position_normalize()`, and boop now exposes the
shared `Save position...` action. The focused validation matrix passed in this environment: `make all`,
`./build/tests/test_game_backend --profile=checkers`, `./build/tests/test_game_backend --profile=boop`,
`./build/tests/test_game_model --profile=checkers`, `./build/tests/test_sgf_io --profile=checkers`,
`./build/tests/test_sgf_io --profile=boop`, `./build/tests/test_boop_backend`,
`./build/tests/test_sgf_controller --profile=checkers`, `./build/tests/test_sgf_controller --profile=boop`, and
`./build/tests/test_window_boop --profile=boop`. GTK-dependent controller/window cases still skip here because
`gtk_init_check()` has no usable display, but the new headless boop snapshot roundtrip runs and passes.

## Context and Orientation

`src/sgf_controller.c` owns the current SGF tree, applies moves to the active model, and currently contains the only
`Save position...` implementation. That implementation is checkers-specific: it reads `GameState` from
`GCheckersModel`, writes setup properties like `AE`, `AB`, `AW`, `ABK`, `AWK`, and `PL`, and reconstructs such files
through checkers-only setup helpers. `src/game_backend.h` defines the runtime interface each game exposes to the
shared shell. `src/games/checkers/checkers_backend.c` and `src/games/boop/boop_backend.c` are the concrete runtime
backends. `src/game_app_profile.c` decides which shared-shell features are enabled per app.

In this repository, a “position-only SGF” means a file whose root node stores a whole game state snapshot and whose
tree has no `;B[...]` or `;W[...]` moves yet. Checkers already supports that through root setup properties. Boop does
not, because its state includes not only board pieces and side to move but also the piece supplies described by
`BoopPosition` in `src/games/boop/boop_types.h`.

## Plan of Work

Start by extending `src/game_backend.h` with two optional callbacks: one that applies SGF setup properties from a
single node into a backend-owned position, and one that writes a backend-owned position into the SGF root node. These
callbacks will use `SgfNode *` plus `GError **` so the controller can remain shared and error-reporting stays
consistent.

Then refactor `src/sgf_controller.c`. Replace the checkers-only root-setup branch inside
`ggame_sgf_controller_replay_node_into_position()` with a shared loop that first lets the backend apply node setup and
then replays any move on that node. Replace the checkers-only `ggame_sgf_controller_save_position_file()` with a
shared implementation that copies the current active position, creates a fresh `SgfTree`, syncs `RU` through
`sgf_io_tree_set_variant()`, asks the backend to write root setup properties, and saves through `sgf_io_save_file()`.

Implement the checkers codec behind the new backend callbacks by moving the existing setup encode/decode logic out of
`src/sgf_controller.c` into a checkers-owned module. Implement the boop codec in a boop-owned module that writes
custom root properties for on-board kittens/cats, supply counts, and `PL`. The boop loader must rebuild a full
`BoopPosition`, derive `promoted_count`, validate piece totals, and recompute `outcome`.

Finally, flip boop’s `supports_save_position` feature flag in `src/game_app_profile.c`, add regressions in
`tests/test_game_backend.c` and `tests/test_sgf_controller.c`, update `doc/OVERVIEW.md`, and run the focused SGF and
backend tests plus `make all`.

## Concrete Steps

Work from the repository root:

  1. Edit `doc/execplan-boop-save-position.md`, `src/game_backend.h`, `src/sgf_controller.c`,
     `src/game_app_profile.c`, `src/games/checkers/checkers_backend.c`, `src/games/boop/boop_backend.c`,
     `tests/test_game_backend.c`, `tests/test_sgf_controller.c`, and `doc/OVERVIEW.md`.
  2. Add backend-owned SGF position codec modules under `src/games/checkers/` and `src/games/boop/`, then wire them
     into `Makefile`.
  3. Build with `make all`.
  4. Run `./build/tests/test_game_backend --profile=checkers`,
     `./build/tests/test_game_backend --profile=boop`,
     `./build/tests/test_sgf_io --profile=checkers`,
     `./build/tests/test_sgf_io --profile=boop`,
     `./build/tests/test_sgf_controller --profile=checkers`, and
     `./build/tests/test_sgf_controller --profile=boop`.

## Validation and Acceptance

Acceptance is behavioral:

- In the boop profile, `supports_save_position` is true and the shared window enables the `Save position...` action.
- Saving a boop position produces an SGF file with no move nodes and with enough root data to restore the same
  board, supplies, and side to move.
- Loading that SGF file in the boop profile restores the saved position instead of the opening setup.
- Existing checkers save-position behavior still roundtrips through the shared backend-hook path.
- `make all` succeeds and the focused backend/SGF tests pass for both checkers and boop.

## Idempotence and Recovery

These edits are additive and can be re-run safely. If the generic replay refactor breaks SGF loading for one backend,
the safest recovery is to keep the backend hook interface but restore the previously passing replay logic before
retrying the focused SGF controller tests.

## Artifacts and Notes

The most important artifact will be a boop SGF controller regression that saves a custom `BoopPosition`, reloads it,
and compares the restored fields. That test must fail before the change and pass after it.

## Interfaces and Dependencies

At the end of this work, `src/game_backend.h` must expose two new optional hooks with signatures equivalent to:

  gboolean (*sgf_apply_setup_node)(gpointer position, const SgfNode *node, GError **error);
  gboolean (*sgf_write_position_node)(gconstpointer position, SgfNode *node, GError **error);

`src/sgf_controller.c` must use those hooks from the active backend when replaying root setup and when saving a
position-only SGF. Checkers and boop must each provide concrete implementations through their backend tables.

Revision note (2026-05-01 / Codex): initial ExecPlan written after surveying the shared save-position flow, the
generic replay path, and boop state requirements.
