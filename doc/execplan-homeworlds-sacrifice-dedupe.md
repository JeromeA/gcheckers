# Homeworlds Sacrifice-Scoped Move Deduplication

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document is maintained according to `doc/PLANS.md` from the repository root.

## Purpose / Big Picture

Homeworlds move generation can produce many symbolic moves that reach the same position, especially under sacrifices
where independent forced actions and catastrophes can be ordered several ways. A full result-position table is not
acceptable because worst-case move generation can exceed one billion leaves. After this change, `all_moves` and
`good_moves` prune only within the descendants of one generated sacrifice, so equivalent continuations under that
sacrifice are represented once without storing state for the whole move tree. Legal move entry remains permissive:
the UI and SGF replay still accept moves where a player triggers a catastrophe before making a sacrifice.

## Progress

- [x] (2026-06-12T07:30Z) Read the existing builder, `all_moves`, `good_moves`, and planning conventions.
- [x] (2026-06-12T07:44Z) Add shared generation helpers for sacrifice-scoped state dedupe and sacrifice-first
  canonicalization.
- [x] (2026-06-12T07:47Z) Wire the helpers into `homeworlds_position_list_all_moves()`,
  `homeworlds_position_stream_all_moves()`, and
  `list_good_moves()`.
- [x] (2026-06-12T07:50Z) Remove global symbolic move hash tables and redundant sacrifice-order pruning that the
  scoped deduper replaces.
- [x] (2026-06-12T07:56Z) Add tests for generated sacrifice-first behavior, local dedupe, and UI/legal acceptance of
  catastrophe-before-sacrifice.
- [x] (2026-06-12T07:59Z) Update Homeworlds move-generation documentation and the project overview.
- [x] (2026-06-12T08:15Z) Build and run the relevant tests.

## Surprises & Discoveries

- Observation: The staged builder keeps `working_position` at the current prefix before `finish_turn()`, even when the
  staged move is complete.
  Evidence: `homeworlds_builder_finish_or_continue()` only sets `HOMEWORLDS_BUILDER_STAGE_COMPLETE`; final turn
  completion happens in `homeworlds_position_apply_move()`.

- Observation: Several tests encoded the old global symbolic-move dedupe and bespoke good-move filters.
  Evidence: `test_list_all_moves_deduplicates_symbolic_moves()` expected all generated symbolic moves to be unique, and
  the static-prune fixture count changed from 119 to 68 after sacrifice-scope dedupe replaced the old filters.

## Decision Log

- Decision: Keep canonicalization out of `homeworlds_move_builder_step()` and `homeworlds_position_apply_move()`.
  Rationale: The builder is the UI/legal-entry path, and the user explicitly wants catastrophe-before-sacrifice input
  to remain accepted even though generated lists should choose sacrifice-first representatives.
  Date/Author: 2026-06-12 / Codex

- Decision: Dedupe only at clean builder boundaries inside a sacrifice scope.
  Rationale: `SELECT_SHIP` and `COMPLETE` states do not depend on selected transient fields, so a key made from
  working position, stage, sacrifice state, and step count is enough to describe the future subtree. Half-selected
  trade, attack, and move targets are not safe to merge without selected-ship data.
  Date/Author: 2026-06-12 / Codex

- Decision: Remove the global symbolic move hash tables from all-move and good-move materialization.
  Rationale: The user explicitly rejected storing even small hashes for every generated move in worst-case trees. The
  remaining generated-list dedupe is scoped to one sacrifice branch.
  Date/Author: 2026-06-12 / Codex

- Decision: Keep `step_count` in the sacrifice-dedupe key.
  Rationale: Two equal positions with different consumed step budgets do not have identical future legality under the
  maximum move-step limit. Including the count keeps pruning local and conservative.
  Date/Author: 2026-06-12 / Codex

## Outcomes & Retrospective

Implementation is complete. `all_moves` and `good_moves` now share sacrifice-scoped builder-state dedupe, generated
sacrifice branches prune catastrophe-before-sacrifice spellings, and the legal builder still accepts those spellings for
UI/SGF input. The targeted Homeworlds tests, the full binary build, and whitespace checks pass. Two full `make test`
attempts failed in unrelated checkers window tests with a NULL GObject critical, and both failed paths passed when run
directly.

## Context and Orientation

The Homeworlds rules live in `src/games/homeworlds/`. `homeworlds_move_builder.c` is the staged legal builder used by
the UI. `homeworlds_game.c` owns legal move application plus the diagnostic `all_moves` traversal.
`homeworlds_backend.c` owns `good_moves`, the pruned list used by the AI search.

A "sacrifice scope" means the subtree reached after one generated sacrifice step. All descendants of that exact
sacrifice share one temporary dedupe table. The table is destroyed when recursion returns from that sacrifice branch.
This bounds memory by the number of unique continuation states under one sacrifice, not by the number of total moves
from the root.

A "clean builder boundary" means a state where `HomeworldsMoveBuilderState.stage` is either
`HOMEWORLDS_BUILDER_STAGE_SELECT_SHIP` or `HOMEWORLDS_BUILDER_STAGE_COMPLETE`. At those stages, the builder is not
waiting for a target color, attack target, or move destination.

## Plan of Work

Add shared helper types in `src/games/homeworlds/homeworlds_move_builder.h` and implement them in
`src/games/homeworlds/homeworlds_move_builder.c`. The helper will inspect the step appended between a parent and child
builder state, reject generated sacrifices after a prefix catastrophe, create one temporary dedupe scope when a
sacrifice is generated, and add/check boundary-state keys inside that scope.

Move semantic position equality into the game layer so the dedupe key can compare positions by pyramid contents rather
than by physical array slot order. The existing backend-only equality logic already shows the intended semantics: bank
contents are counted, system stars are counted, and side-owned ships are counted per system.

Update `homeworlds_collect_all_moves_recursive()` to carry a generation context and visit states through the shared
helper before emitting completed moves or expanding children. Update the good-move recursive traversal similarly, but
return a branch-covered flag so a subtree pruned as a duplicate does not accidentally enable pass fallback.

Remove global `seen_moves` tables from `HomeworldsMoveListBuilder` and `HomeworldsMoveBuffer`. Remove good-move
commutativity filters that become redundant under the sacrifice-scoped state deduper, while keeping strategic pruning
such as unsafe catastrophes, last-homeworld-ship checks, and small-sacrifice pruning.

## Concrete Steps

Work from `/home/jerome/Data/gcheckers`. Edit files with `apply_patch`. Build the affected Homeworlds test binaries
with:

    make build/tests/test_homeworlds_game build/tests/test_homeworlds_backend

Then run:

    build/tests/test_homeworlds_game
    build/tests/test_homeworlds_backend

Before finishing, also run `git diff --check` and inspect line lengths for edited C and Markdown files.

## Validation and Acceptance

The new tests must demonstrate that a legal catastrophe-before-sacrifice move still applies through
`homeworlds_position_apply_move()`, while generated `all_moves` and `good_moves` choose the sacrifice-first spelling
and omit the catastrophe-first equivalent. Existing good-move tests for commutative blue trades, commutative green
builds, yellow sacrifice hops, and green rebuilds should continue to pass through the shared sacrifice-scope dedupe
rather than through bespoke adjacent-step filters.

## Idempotence and Recovery

The edits are ordinary source changes. If a test fails, rerun the individual Homeworlds test binary after fixing the
smallest relevant area. The working tree already contains unrelated changes in `Makefile`,
`src/homeworlds_eval_experiment.c`, and untracked big move reports; do not modify or remove those files as part of this
plan.

## Artifacts and Notes

Interim validation:

    make build/tests/test_homeworlds_game build/tests/test_homeworlds_backend
    build/tests/test_homeworlds_game
    build/tests/test_homeworlds_backend

The interim backend run exposed stale expectations from the removed global and bespoke filters; the tests were updated
to cover the new scoped-dedupe contract.

Final validation:

    make
    git diff --check
    awk 'length($0) > 120 { printf "%s:%d:%d\n", FILENAME, FNR, length($0) }' \
      src/games/homeworlds/homeworlds_game.c src/games/homeworlds/homeworlds_game.h \
      src/games/homeworlds/homeworlds_move_builder.c src/games/homeworlds/homeworlds_move_builder.h \
      src/games/homeworlds/homeworlds_backend.c tests/test_homeworlds_game.c tests/test_homeworlds_backend.c \
      doc/homeworlds-move-generation.md doc/OVERVIEW.md doc/execplan-homeworlds-sacrifice-dedupe.md

`make test` was also attempted twice. The first run failed at
`/gcheckers-window/library-loads-imported-game`, and the second failed at `/gcheckers-window/import-wizard-flow`; both
failures were `GLib-GObject-FATAL-CRITICAL: invalid (NULL) pointer instance` in the checkers window test binary. Running
each failed path directly with `build/tests/test_window --profile=checkers -p ...` passed.

## Interfaces and Dependencies

`src/games/homeworlds/homeworlds_move_builder.h` will expose:

    typedef struct {
      GHashTable *states;
    } HomeworldsGenerationDedupe;

    typedef struct {
      HomeworldsGenerationDedupe *sacrifice_dedupe;
    } HomeworldsGenerationContext;

    void homeworlds_generation_context_init(HomeworldsGenerationContext *context);
    void homeworlds_generation_dedupe_init(HomeworldsGenerationDedupe *dedupe);
    void homeworlds_generation_dedupe_clear(HomeworldsGenerationDedupe *dedupe);
    gboolean homeworlds_generation_visit_state(const HomeworldsGenerationContext *context,
                                               const HomeworldsMoveBuilderState *state,
                                               gboolean *out_duplicate);
    gboolean homeworlds_generation_prepare_child_context(const HomeworldsGenerationContext *parent_context,
                                                         const HomeworldsMoveBuilderState *parent_state,
                                                         const HomeworldsMoveBuilderState *child_state,
                                                         HomeworldsGenerationContext *child_context,
                                                         HomeworldsGenerationDedupe *child_dedupe,
                                                         gboolean *out_prune);

`src/games/homeworlds/homeworlds_game.h` will expose `homeworlds_positions_equal()` for semantic position equality.
