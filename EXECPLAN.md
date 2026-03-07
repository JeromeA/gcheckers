# Replace SGF Node Payload With SGF Properties

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This plan is maintained according to PLANS.md at the repository root (`PLANS.md`).

## Purpose / Big Picture

After this change, SGF nodes store SGF properties directly instead of an opaque payload byte blob. This makes the
in-memory model match SGF structure (`PROP[value]`) and allows round-tripping metadata and move text through a single
representation.
You can observe this by loading SGF files, navigating and saving them, and seeing that node properties (including move
properties `B[...]`/`W[...]`) are preserved while controller replay still applies typed `CheckersMove` values.

## Progress

- [x] (2026-03-07 20:22Z) Reviewed current SGF payload usage in `sgf_tree`, `sgf_io`, `sgf_controller`, and tests.
- [x] (2026-03-07 20:28Z) Implemented SGF node property storage and APIs in `src/sgf_tree.[ch]`; removed payload API.
- [x] (2026-03-07 20:28Z) Added `src/sgf_move_props.[ch]` to convert move properties <-> `CheckersMove`.
- [x] (2026-03-07 20:30Z) Migrated `src/sgf_io.c` to property-backed parse/save with repeated property values
  retained.
- [x] (2026-03-07 20:29Z) Migrated `src/sgf_controller.c` replay/append paths to property-based move
  extraction/formatting.
- [x] (2026-03-07 20:31Z) Updated SGF tests and documentation (`src/OVERVIEW.md`, `BUGS.md`) for payload removal.
- [x] (2026-03-07 20:32Z) Validated with `make -j$(nproc)` and `make test`.
- [x] (2026-03-07 20:33Z) Added SGF IO coverage for repeated property values and re-ran `make test`.

## Surprises & Discoveries

- Observation: `sgf_io_parse_node_properties()` currently discards repeated values after the first and only keeps one
  string per property key.
  Evidence: `src/sgf_io.c` inserts one `g_hash_table` entry and parses extra `[...]` values into a variable named
  `ignored`.

- Observation: `make test` still prints crashpad/socket warnings from the screenshot/browser path in this environment,
  but test targets continue and exit successfully.
  Evidence: `make test` emitted `setsockopt: Operation not permitted` from Chromium and still returned success.

## Decision Log

- Decision: Keep `SgfColor` and `move_number` fields on `SgfNode` while replacing payload storage with SGF
  properties.
  Rationale: UI rendering and navigation already depend on color/move-number accessors; removing both in the
  same refactor adds migration risk without helping the payload->properties goal.
  Date/Author: 2026-03-07 / Codex

- Decision: Introduce a focused helper module (`sgf_move_props`) rather than spreading move-notation parse/format logic
  across controller and IO.
  Rationale: This keeps typed move behavior centralized while SGF tree remains generic-property oriented.
  Date/Author: 2026-03-07 / Codex

- Decision: Keep `sgf_tree_append_move()` as a move-specialized API (`color`, `move_value`) while adding generic
  node-property APIs separately.
  Rationale: Existing callers and SGF view tests rely on append+dedupe move semantics; keeping this API limited avoids
  broader tree construction churn during payload removal.
  Date/Author: 2026-03-07 / Codex

## Outcomes & Retrospective

The SGF tree no longer stores opaque payload bytes. Nodes now own SGF property maps with ordered identifier
retrieval and multi-value support, and all payload APIs have been removed.

Move conversion logic now lives in `src/sgf_move_props.[ch]`, which the SGF controller and SGF IO both use. Controller
replay and apply continue to operate on typed `CheckersMove` values while SGF storage remains property-native.

SGF IO now parses and preserves repeated property values, applies parsed properties to root and move nodes, and
serializes nodes from stored properties. Root metadata defaults are ensured through root properties.

`make -j$(nproc)` and `make test` pass after the migration. GTK-dependent tests remain skipped in this headless
environment as before.

## Context and Orientation

`src/sgf_tree.[ch]` now stores SGF properties per node (`ident -> values[]`) and exposes property add/get/clear APIs.
`src/sgf_controller.c` uses `sgf_move_props` to convert node move properties to/from typed `CheckersMove` values.
`src/sgf_io.c` parses SGF text into node properties and serializes directly from those properties. SGF view modules
(`src/sgf_view*.c`) still only depend on structure (`color`, children, move number). SGF tests now assert property
behavior (`tests/test_sgf_tree.c`, `tests/test_sgf_io.c`, `tests/test_sgf_controller.c`).

In this repository, an SGF property means an uppercase identifier like `B`, `W`, `FF`, `CA` with one or more bracketed
values. Nodes can legally have multiple values per property key, so storage must represent repeated values.

## Plan of Work

First, refactor tree storage so each node owns a property map and expose APIs to add and read properties
(including multi-value properties). Keep append semantics for move nodes by passing move text and color;
deduplicate sibling nodes by color plus move text.

Next, add `sgf_move_props` as the typed boundary between SGF properties and `CheckersMove`. It will parse move
notation from `B`/`W` properties and format `CheckersMove` back to move property values.

Then migrate SGF I/O to use properties as the source of truth: parser writes all properties into each node;
serializer emits properties from each node rather than reconstructing from payload. Root metadata handling
moves to root node properties.

After that, migrate SGF controller: when appending a move, format and store SGF move properties; when
replaying, parse move properties from selected nodes.

Finally, update tests and docs, add a BUGS entry for the fixed architectural mismatch, and validate full
build and tests.

## Concrete Steps

From repository root (`/home/jerome/Data/gcheckers`):

1. Edit `src/sgf_tree.[ch]` to replace payload with property storage and new APIs.
2. Add `src/sgf_move_props.[ch]` and wire it into SGF IO/controller build targets in `Makefile`.
3. Refactor `src/sgf_io.c` parse/save flow to populate and emit node properties directly.
4. Refactor `src/sgf_controller.c` move extraction and append code to use `sgf_move_props`.
5. Update tests in `tests/test_sgf_tree.c`, `tests/test_sgf_io.c`, `tests/test_sgf_controller.c`.
6. Update docs: `src/OVERVIEW.md` and `BUGS.md`.
7. Validate with:

   make -j$(nproc)
   make test

Expected outcomes:

- Compilation succeeds under `-Werror` with no warnings.
- SGF tests pass with no references to payload APIs.
- SGF save/load roundtrip preserves node properties and move behavior.

## Validation and Acceptance

Acceptance criteria:

- No SGF node payload API remains in `src/sgf_tree.h`.
- SGF nodes expose add/get property functions and can hold repeated property values.
- SGF controller replay and apply paths still function using `CheckersMove` conversions from properties.
- SGF IO load/save roundtrips move trees and properties.
- `make -j$(nproc)` and `make test` pass.

## Idempotence and Recovery

All edits are source-only and safe to rerun. If migration is interrupted, recovery path is to keep tree API
and call sites in sync before compiling: any payload reference after API removal will fail fast at compile
time. Re-running build/tests is idempotent.

## Artifacts and Notes

Validation transcript entries:

  make -j$(nproc)
  make test

Both commands completed successfully in this run.

## Interfaces and Dependencies

Target interfaces after migration:

- `const SgfNode *sgf_tree_append_move(SgfTree *self, SgfColor color, const char *move_value);`
- `gboolean sgf_node_add_property(SgfNode *node, const char *ident, const char *value);`
- `const GPtrArray *sgf_node_get_property_values(const SgfNode *node, const char *ident);`
- `const char *sgf_node_get_property_first(const SgfNode *node, const char *ident);`
- `gboolean sgf_move_props_set_move(SgfNode *node, SgfColor color, const CheckersMove *move, GError **error);`
- `gboolean sgf_move_props_parse_node(const SgfNode *node, SgfColor *out_color, CheckersMove *out_move,
  GError **error);`

Dependencies remain GLib/GObject/GTK as currently configured in `Makefile`.

Plan updates:
- 2026-03-07: Replaced prior SGF timeline ExecPlan content with this payload-to-properties migration plan
  requested by the user.
- 2026-03-07: Updated progress, decisions, discoveries, outcomes, and validation evidence after
  completing implementation and running build/tests.
