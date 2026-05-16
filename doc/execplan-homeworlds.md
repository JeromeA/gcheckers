# Homeworlds Support Baseline

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document must be maintained in accordance with `doc/PLANS.md`.

## Purpose / Big Picture

Homeworlds is now a supported game in the shared application framework. `build/bin/ghomeworlds` uses the same generic
application shell as Checkers and Boop: the shared menu bar, toolbar, SGF controller, SGF navigation drawer, analysis
drawer, and file actions all come from `GGameWindow`.

Homeworlds remains non-square-grid. Its extension point is a board-host hook that installs `HomeworldsView` into the
generic window's board area. The view owns Homeworlds-specific rendering and staged interaction, but completed moves are
sent back to the generic move handler so SGF recording and replay stay shared.

This plan supersedes the early Homeworlds plan that proposed a game-owned toplevel window. Future Homeworlds work must
build on the current generic-shell architecture and must not reintroduce a separate application window path,
compile-time game selection, or Checkers-only assumptions.

## Progress

- [x] (2026-04-25 16:05Z) Wrote the initial Homeworlds implementation plan after reading the planning docs and rules.
- [x] (2026-04-25 17:15Z) Added the Homeworlds build/backend skeleton, branded launcher, and profile tests.
- [x] (2026-04-26 10:55Z) Added the slot-based engine, staged move builder, bounded good-move callback, shared AI
  support for good-move-only backends, and focused engine/backend tests.
- [x] (2026-05-13 21:28Z) Updated the plan to preserve the generic Makefile, runtime profiles, and Boop support.
- [x] (2026-05-13 23:05Z) Added the first playable Homeworlds GTK view with staged setup, turns, sacrifices, and
  catastrophes.
- [x] (2026-05-15 09:30Z) Moved setup selection from the side-panel list to clickable bank piles overlaid on the board.
- [x] (2026-05-15 10:10Z) Fixed homeworld geometry so each player's ships sit beside their stars from that player's
  perspective.
- [x] (2026-05-15 10:30Z) Updated pyramid/star drawing, pips, and piece sizing to match the intended Homeworlds UI.
- [x] (2026-05-16 06:57Z) Moved Homeworlds into the generic SGF shell. The Homeworlds profile now provides a board
  host, the backend provides SGF move parsing and position snapshots, and setup moves played through the generic window
  append replayable SGF nodes.

## Surprises & Discoveries

- Observation: Homeworlds cannot expose full legal move lists without defeating the staged architecture.
  Evidence: the backend advertises `supports_move_list = FALSE`, `supports_move_builder = TRUE`, and uses
  `list_good_moves` for bounded AI search candidates.

- Observation: The shared shell can support non-square games if the board-specific part is just a host widget.
  Evidence: Homeworlds uses `homeworlds_view_create_board_host()` to replace square-board presentation while
  `GGameWindow` still owns SGF, menus, analysis, and model integration.

- Observation: Builder-only backends need a different SGF append validation path.
  Evidence: Homeworlds does not enumerate legal moves, so `GGameSgfController` validates a completed move by applying it
  to a copied position before appending the SGF node.

- Observation: The current catastrophe buttons are still state changes rather than SGF-recorded completed moves.
  Evidence: `homeworlds_view_catastrophe_clicked()` mutates the model position directly. This is a remaining cleanup
  target if Homeworlds catastrophe resolution should be visible in SGF history.

## Decision Log

- Decision: Keep runtime profile selection and the all-games Makefile.
  Rationale: one object graph and one shared entry path avoid wrong-artifact builds and make `make` build all branded
  binaries consistently.
  Date/Author: 2026-05-13 / Codex

- Decision: Keep Homeworlds interaction staged through `homeworlds_move_builder`.
  Rationale: Homeworlds has a large move space; the UI should guide legal construction without requiring full
  enumeration.
  Date/Author: 2026-05-13 / Codex

- Decision: Use a simple stochastic AI for the first Homeworlds automated player.
  Rationale: the first UI milestone needed legal automated moves for testing and local play, not strong strategy.
  Date/Author: 2026-05-13 / Codex

- Decision: Homeworlds must use the generic application window.
  Rationale: SGF support, menus, navigation, analysis, and file actions are application invariants, not optional
  per-game window features.
  Date/Author: 2026-05-16 / Codex

- Decision: The profile UI extension point is a board host only.
  Rationale: non-square games need custom presentation, but completed moves still need to enter the same SGF-backed
  path as square-grid board moves.
  Date/Author: 2026-05-16 / Codex

## Outcomes & Retrospective

Homeworlds is playable through `build/bin/ghomeworlds` and uses the shared application shell. The old standalone window
path has been removed. Homeworlds-specific code now lives in the engine, backend, staged move builder, random AI,
SGF-position codec, and board-host view.

The main architectural lesson is that generic application ownership matters. SGF, menus, file actions, and analysis are
cross-game application features; only board presentation and rule-specific move construction belong in game code.

## Context and Orientation

The repository builds three branded application binaries:

- `build/bin/gcheckers` for Checkers.
- `build/bin/gboop` for Boop.
- `build/bin/ghomeworlds` for Homeworlds.

The default command `make` or `make all` builds all three binaries plus shared libraries and puzzle tools. A developer
can build one application binary with `make all-checkers`, `make all-boop`, or `make all-homeworlds`.

Runtime game selection is handled by `GGameAppProfile` in `src/game_app_profile.[ch]`. Each launcher selects a profile
and then calls `ggame_app_main_run()`. Shared code gets the active backend through
`GGAME_ACTIVE_GAME_BACKEND`, which expands to the active profile's backend.

The Homeworlds files are:

- `src/games/homeworlds/RULES.md`: prose game rules.
- `src/games/homeworlds/homeworlds_types.h`: fixed-slot data types and pyramid helpers.
- `src/games/homeworlds/homeworlds_game.c` and `.h`: position logic, rule application, evaluation, hashing, and move
  formatting/parsing.
- `src/games/homeworlds/homeworlds_move_builder.c` and `.h`: staged legal-choice generation for setup and turns.
- `src/games/homeworlds/homeworlds_backend.c` and `.h`: `GameBackend` adapter.
- `src/games/homeworlds/homeworlds_sgf_position.c` and `.h`: whole-position SGF snapshot codec.
- `src/games/homeworlds/homeworlds_random_ai.c` and `.h`: builder-driven random AI.
- `src/games/homeworlds/homeworlds_view.c` and `.h`: GTK board host and staged Homeworlds interaction.
- `src/games/homeworlds/homeworlds_view_stub.c`: weak board-host stub for headless link contexts.
- `src/games/homeworlds/homeworlds.png`: visual mockup for the Homeworlds UI.

`GGameWindow` in `src/window.c` owns the shared shell, the `GGameModel`, `GGameSgfController`, player controls, SGF
drawer, analysis drawer, and a board-host area. For square-grid backends it binds `BoardView`; for Homeworlds it leaves
`BoardView` unbound and installs the Homeworlds board host instead.

## Current Work Guidelines

When changing Homeworlds:

- Keep the application profile-driven.
- Keep `make` and `make all` building every application binary.
- Add new Homeworlds sources to the existing `HOMEWORLDS_*_SRCS` variables in `Makefile`.
- Route completed user moves through the generic move handler so SGF recording stays shared.
- Do not add game-owned toplevel windows.
- Do not add preprocessor game selection in shared sources.
- Update `doc/OVERVIEW.md` for changes under `src/`.
- Add regression tests for new features.

## Validation and Acceptance

For Homeworlds rules, important observable behaviors include:

- Legal setup accepts a two-star homeworld plus one owned ship and rejects illegal setups.
- Any bank pyramid size can be selected for setup stars and the starting ship.
- Construct uses the smallest available same-color ship from the bank.
- Trade preserves ship size and changes color only when a matching bank pyramid exists.
- Attack requires sufficient attacker size and changes ownership of the target ship.
- Move/discover obeys star-size connectivity.
- Sacrifice grants the expected repeated same-color actions.
- Catastrophe removes all overpopulated pieces of that color.
- A player with no ships at their homeworld loses at the start of their turn.

For application integration, important observable behaviors include:

- `build/bin/ghomeworlds` opens the generic window shell with the normal menu bar, toolbar, SGF drawer, and analysis
  drawer actions.
- The Homeworlds view is installed as the board host.
- Completed staged setup/turn moves append SGF nodes.
- SGF navigation replays Homeworlds positions through `GGameSgfController`.
- `Save position...` writes a Homeworlds SGF snapshot that can be loaded back into the same position.

Run from `/home/jerome/Data/gcheckers`:

    make test_homeworlds_game test_homeworlds_backend test_homeworlds_window
    build/tests/test_homeworlds_backend
    build/tests/test_homeworlds_window
    make test
    make all

If no GTK display is available, `test_homeworlds_window` may skip display-dependent tests.

## Artifacts and Notes

The untracked `annotate*` files and `puzzles/boop/` directory are unrelated local artifacts observed during this
revision. Do not include them in Homeworlds commits unless a future task explicitly says to.
