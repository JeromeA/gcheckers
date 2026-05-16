# Put Homeworlds in the Generic SGF Shell

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds. This document follows `doc/PLANS.md`.

## Purpose / Big Picture

Homeworlds should use the same application shell as Checkers and Boop. After this change, `build/bin/ghomeworlds`
opens a `GGameWindow` with the normal menu bar, toolbar, SGF navigation drawer, and the Homeworlds board host in the
main area. Human Homeworlds moves are recorded into the shared SGF tree, SGF navigation replays Homeworlds positions
through the generic `GGameSgfController`, and SGF load/save uses the same file actions as the other games.

The important architectural outcome is that SGF support is not a per-profile optional window feature. A new backend may
provide a custom board host when it is not square-grid based, but it does not get to replace the generic application
window or remove SGF navigation.

## Progress

- [x] (2026-05-16 06:38Z) Inspected the current profile, application, shared window, SGF controller, and Homeworlds
  backend/view code.
- [x] (2026-05-16 06:48Z) Removed the profile-level custom-window hook and made every application profile launch
  `GGameWindow`.
- [x] (2026-05-16 06:50Z) Made `GGameWindow` and `GGameSgfController` safe when the active backend does not use
  `BoardView`.
- [x] (2026-05-16 06:52Z) Added full Homeworlds move formatting/parsing suitable for SGF replay.
- [x] (2026-05-16 06:53Z) Added Homeworlds SGF position snapshot hooks for position-only SGFs.
- [x] (2026-05-16 06:54Z) Routed completed Homeworlds view moves into the generic shell move handler so they are
  appended to SGF.
- [x] (2026-05-16 06:57Z) Updated tests, documentation, and build rules.
- [x] (2026-05-16 06:57Z) Ran focused Homeworlds backend/window and profile backend tests.
- [x] (2026-05-16 07:02Z) Ran `make all`; all application binaries and puzzle tools built successfully.
- [x] (2026-05-16 07:05Z) Ran `make test`; the Homeworlds tests passed, then the suite failed at the pre-existing
  `/sgf-view/link-angles` GTK test with `GLib-GObject-FATAL-CRITICAL: invalid (NULL) pointer instance`.

## Surprises & Discoveries

- Observation: `GGameSgfController` already has generic replay support, but `ggame_sgf_controller_apply_move()` refuses
  backends without full move-list enumeration.
  Evidence: `src/sgf_controller.c` requires `backend->supports_move_list` before appending a move.

- Observation: `BoardView` assumes a square-grid backend when binding a model.
  Evidence: `board_view_set_model()` requires `backend->supports_square_grid_board`, while Homeworlds intentionally
  reports `supports_square_grid_board = FALSE`.

- Observation: Homeworlds move formatting is currently lossy.
  Evidence: `homeworlds_move_format()` only prints the first normal turn step, which cannot replay sacrifice turns.

- Observation: The existing Homeworlds catastrophe buttons still mutate the model position directly rather than
  appending an SGF node.
  Evidence: `homeworlds_view_catastrophe_clicked()` uses `ggame_model_set_position()`. This predates the generic SGF
  integration and is outside the completed-move path used by staged setup/turn moves.

## Decision Log

- Decision: Remove `GGameAppUiHooks.create_window` instead of adding an SGF controller to the Homeworlds custom window.
  Rationale: The generic shell must own SGF, menus, navigation, and common window actions for every backend.
  Date/Author: 2026-05-16, Codex.

- Decision: Keep `create_board_host` as the extension point and pass the generic move handler to it.
  Rationale: Homeworlds needs a non-square board renderer, but completed moves still need to enter the same SGF-backed
  path as square-grid board moves.
  Date/Author: 2026-05-16, Codex.

- Decision: Use a compact but readable Homeworlds notation: setup moves are `S<side>:<star0>,<star1>,<ship>` and turn
  moves are `T<side>:<step>/<step>...`, with step prefixes such as `C` for construct and `X` for sacrifice.
  Rationale: The notation round-trips all fields while staying short enough for existing fixed-size SGF move buffers.
  Date/Author: 2026-05-16, Codex.

## Outcomes & Retrospective

Homeworlds now launches through `GGameWindow`, with SGF navigation and file actions owned by the shared shell. The old
custom app-window sources were removed, and the profile extension point is limited to an optional board host that
receives the generic move handler.

The Homeworlds backend now exposes SGF color mapping, round-trippable compact move notation, parsing, and whole-position
snapshot hooks. The shared SGF controller accepts a null `BoardView`, skips square-board selection clearing in that
case, and validates builder-only backend moves by applying them to a copied position before appending them.

Focused validation passed for `make test_homeworlds_backend test_homeworlds_window test_game_backend`,
`build/tests/test_homeworlds_backend && build/tests/test_game_backend --profile=homeworlds &&
build/tests/test_homeworlds_window`, and `build/tests/test_sgf_io --profile=homeworlds &&
build/tests/test_game_model --profile=homeworlds`. `make all` passed. `make test` passed the Homeworlds cases and then
failed at the unrelated `/sgf-view/link-angles` GTK test.

## Context and Orientation

`src/application.c` starts the active profile. Before this plan, profiles could either create a generic `GGameWindow` or
replace it with a custom `create_window` hook. Homeworlds used that hook, which meant it did not get the SGF drawer or
file actions from the shared shell.

`src/window.c` implements `GGameWindow`, the generic shell. It owns one `GGameModel`, one `GGameSgfController`, the SGF
navigation panel, the analysis panel, player controls, and a board-host area. Checkers uses the default square
`BoardView`. Boop uses `create_board_host` to add side controls around the default `BoardView`. Homeworlds should use
the same board-host hook to place its custom system graph widget in the board-host area.

`src/sgf_controller.c` owns the SGF tree and view. It already knows how to replay generic backend positions by creating
a fresh position, applying optional setup-node properties, parsing move properties, and applying each move. It needs two
adjustments for Homeworlds: it must not require a square `BoardView`, and it must be able to append a move for a
builder-only backend that does not expose every complete legal move through `list_moves`.

`src/games/homeworlds/homeworlds_view.c` is currently both a renderer and a controller that directly applies completed
moves to its model. It must instead report completed moves to the generic shell when a move handler is installed. The
standalone fallback can still apply directly to the model for narrow view tests.

## Plan of Work

First, change `src/game_app_profile.h`, `src/game_app_profile.c`, and `src/application.c` so all profiles use
`GGameWindow`. Remove `supports_shared_shell`, remove `supports_sgf_files`, and remove `create_window`. Homeworlds will
advertise a `create_board_host` hook instead.

Second, make the shared shell non-square aware. `GGameWindow` should only bind and update `BoardView` when
`backend->supports_square_grid_board` is true. It should still create `GGameSgfController`, the SGF panel, menu actions,
and toolbar actions for every backend. `GGameSgfController` should accept a nullable `BoardView` and skip selection
clearing when none is present.

Third, add complete Homeworlds SGF support. `homeworlds_move_format()` must serialize all Homeworlds moves, and a new
`homeworlds_move_parse()` must reconstruct them. The Homeworlds backend must expose `sgf_color_for_side`,
`parse_move`, `sgf_apply_setup_node`, and `sgf_write_position_node`. The snapshot hooks should live in new
`src/games/homeworlds/homeworlds_sgf_position.[ch]` files and encode the whole position in custom `GH*` properties.

Fourth, route Homeworlds UI moves into the generic move path. Add a move handler to `HomeworldsView`, call it whenever
the staged builder or random AI completes a move, and add `homeworlds_view_create_board_host()` for the profile hook.
The generic window will pass its existing `ggame_window_apply_player_move()` handler into profile board hosts.

Finally, update tests and docs. Homeworlds backend tests should prove move notation and snapshot round-trips.
Homeworlds window tests should create a generic `GGameWindow`, assert that Homeworlds view and SGF controller widgets
coexist, and assert that a staged setup path appends SGF nodes.

## Concrete Steps

Run commands from `/home/jerome/Data/gcheckers`.

Edit the files named in the plan. Then run:

    make test_homeworlds_backend
    build/tests/test_homeworlds_backend
    make test_homeworlds_window
    build/tests/test_homeworlds_window
    make test_game_backend
    build/tests/test_game_backend
    make all

If a GTK display is unavailable, `test_homeworlds_window` may skip display-dependent tests. In the current developer
environment, the existing Homeworlds window test normally runs under the available display setup.

## Validation and Acceptance

The change is accepted when `build/bin/ghomeworlds` is built from the same generic application path as the other games,
the Homeworlds profile has no custom-window hook, and the Homeworlds window contains both `homeworlds-view` and an SGF
controller view. A completed Homeworlds setup through the view must add SGF move nodes, and navigating those nodes must
replay Homeworlds positions through `GGameSgfController`.

Backend acceptance is that `homeworlds_move_format()` and `homeworlds_move_parse()` round-trip setup moves, ordinary
turns, and multi-step sacrifice turns, and that Homeworlds SGF position snapshots can save and restore a non-starting
position.

## Idempotence and Recovery

All edits are ordinary source changes. If a test fails after a partial edit, rebuild only the affected test target and
rerun it. The untracked `annotate*` files and `puzzles/boop/` directory are unrelated and should not be staged or
modified.

## Artifacts and Notes

Current relevant status before edits:

    ## main...origin/main [ahead 2]
    ?? annotate
    ?? annotate1
    ?? annotate2
    ?? annotate3
    ?? annotate4
    ?? annotate5
    ?? puzzles/boop/

## Interfaces and Dependencies

At completion, `GGameAppUiHooks` should expose only board-host customization:

    GtkWidget *(*create_board_host)(GGameModel *model,
                                    BoardView *board_view,
                                    GGameAppMoveHandler move_handler,
                                    gpointer move_handler_data);

`HomeworldsView` should expose:

    void homeworlds_view_set_move_handler(HomeworldsView *view,
                                          HomeworldsViewMoveHandler handler,
                                          gpointer user_data);
    GtkWidget *homeworlds_view_create_board_host(GGameModel *model,
                                                 BoardView *board_view,
                                                 GGameAppMoveHandler move_handler,
                                                 gpointer move_handler_data);

The Homeworlds backend should expose parse and SGF hooks through `GameBackend`:

    gboolean homeworlds_move_parse(const char *notation, HomeworldsMove *out_move);
    gboolean homeworlds_sgf_position_apply_setup_node(gpointer position, const SgfNode *node, GError **error);
    gboolean homeworlds_sgf_position_write_position_node(gconstpointer position, SgfNode *node, GError **error);
