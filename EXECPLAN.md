# Store Structured Analysis On SGF Nodes And Persist It

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This plan is maintained according to PLANS.md at the repository root (`PLANS.md`).

## Purpose / Big Picture

After this change, full analysis results (all legal move scores for a position) are stored as structured data on each
SGF node, not as transient text. The analysis panel still shows text, but that text is now a formatter view of node
analysis data. The transposition table remains an optimization cache only. SGF save/load also preserves analysis via
custom SGF properties, so analysis survives round-trips.

A user can verify this by analyzing a position, saving SGF, loading it again, selecting the same node, and seeing the
same scored move list and depth metadata without re-running analysis.

## Progress

- [x] (2026-03-07 20:45Z) Re-read current SGF APIs and confirmed payload removal is already complete.
- [x] (2026-03-07 20:53Z) Added `SgfNodeAnalysis` model and APIs in `src/sgf_tree.[ch]` plus coverage in
  `tests/test_sgf_tree.c`.
- [x] (2026-03-07 21:00Z) Refactored analysis flow: window now stages structured node analysis and formats text at
  UI edge; model text API replaced with structured API.
- [x] (2026-03-07 21:01Z) Clarified TT semantics as ephemeral cache in `src/ai_transposition_table.h`.
- [x] (2026-03-07 21:06Z) Added SGF analysis persistence (`GCAD`/`GCAS`/`GCAN`) in `src/sgf_io.c` with roundtrip test
  coverage in `tests/test_sgf_io.c`.
- [x] (2026-03-07 21:07Z) Updated docs (`src/OVERVIEW.md`) and validated with `make -j$(nproc)` and `make test`.

## Surprises & Discoveries

- Observation: The previous ExecPlan in this repository targeted an older SGF payload migration and is now stale for
  the requested analysis-storage work.
  Evidence: `EXECPLAN.md` content before this rewrite described replacing SGF payload bytes with SGF properties.

- Observation: `make test` still emits Chromium crashpad warnings from the screenshot path in this environment, but
  test targets complete successfully.
  Evidence: `setsockopt: Operation not permitted` appeared during `tools/screenshot_gcheckers.sh`, and `make test`
  exited with code 0.

## Decision Log

- Decision: Keep SGF analysis storage attached directly to `SgfNode` rather than introducing a separate sidecar index.
  Rationale: The user requested SGF-tree-centric storage and this keeps ownership/invalidation local to tree edits.
  Date/Author: 2026-03-07 / Codex

- Decision: Represent persisted analysis with gcheckers-specific SGF properties (`GCAD`, `GCAN`, `GCAS`) instead of
  overloading `C[]` comments.
  Rationale: This preserves machine-readability and avoids colliding with user-authored comments.
  Date/Author: 2026-03-07 / Codex

- Decision: Serialize analysis by syncing structured node analysis into SGF properties during save, then parse those
  properties back into structured analysis during load.
  Rationale: This keeps SGF serialization deterministic while preserving generic property roundtrip behavior.
  Date/Author: 2026-03-07 / Codex

## Outcomes & Retrospective

The requested 4-step migration is complete.

`SgfTree` now stores structured per-node analysis (`SgfNodeAnalysis`) with explicit copy/free semantics and dedicated
set/get/clear APIs. The window analysis worker now produces structured node analysis and publishes it to the main
thread, where it is attached to the selected SGF node and formatted into text only at the UI boundary. The model now
exposes structured analysis (`gcheckers_model_analyze_moves`) and no longer mixes text formatting with analysis logic.
TT API docs now explicitly call out cache-only semantics.

SGF persistence now includes node analysis custom properties (`GCAD`, `GCAS`, `GCAN`) and reconstructs structured
analysis on load. Roundtrip tests validate this path.

## Context and Orientation

`src/sgf_tree.[ch]` currently stores SGF properties and move tree structure, but no evaluation data per node.
`src/window.c` runs iterative deepening in a worker thread and formats analysis text directly from temporary search
results. `src/checkers_model.[ch]` still provides a string API (`gcheckers_model_analyze_moves_text`) that duplicates
report formatting concerns. `src/ai_transposition_table.[ch]` stores short-lived hash entries for pruning and move
ordering during search.

In this plan, “node analysis” means a typed structure containing depth, cumulative search stats, and all scored legal
moves for that node position. “Persistence” means serializing this structure into SGF node properties and reading it
back on load.

## Plan of Work

First add a typed `SgfNodeAnalysis` model to `sgf_tree` with clear copy/free semantics and node accessor APIs. Include
helpers to convert from alpha-beta move lists so callers do not hand-roll conversions.

Then refactor analysis execution paths so analysis is produced as structured node analysis and only converted to text in
one formatter path used by the UI. Keep cancellation/progress behavior intact.

After that, update TT docs/comments to state explicitly that TT entries are transient and never authoritative user data.

Finally, add SGF serialization/deserialization of node analysis using custom properties and add roundtrip tests proving
analysis survives save/load. Update overview docs and run full validation.

## Concrete Steps

From `/home/jerome/Data/gcheckers`:

1. Edit `src/sgf_tree.h` and `src/sgf_tree.c` to add:
   - `SgfNodeScoredMove`
   - `SgfNodeAnalysis`
   - allocation/copy/free helpers
   - `sgf_node_set_analysis()`, `sgf_node_get_analysis()`, `sgf_node_clear_analysis()`
2. Add/update tests in `tests/test_sgf_tree.c` for node-analysis set/get/reset behavior.
3. Refactor model/window analysis APIs so structured analysis is the main data path and text formatting is centralized.
4. Update `src/ai_transposition_table.h` and `src/OVERVIEW.md` wording to clarify TT ephemerality.
5. Extend `src/sgf_io.c` load/save with custom analysis properties and add SGF roundtrip tests in
   `tests/test_sgf_io.c`.
6. Run:
   make -j$(nproc)
   make test

## Validation and Acceptance

Acceptance criteria:

- SGF nodes can hold typed analysis results and expose them through tree APIs.
- UI analysis text is derived from structured node analysis, not ad-hoc text-only APIs.
- TT comments/docs clearly state cache-only semantics.
- SGF save/load roundtrips analysis metadata and scored moves.
- `make -j$(nproc)` and `make test` pass.

## Idempotence and Recovery

Edits are additive/refactor-oriented and safe to reapply. If partial refactor breaks compilation, recover by keeping
public headers and their call sites synchronized, then rerun `make -j$(nproc)` to surface remaining mismatches.

## Artifacts and Notes

Validation transcripts:

  make -j$(nproc)
  make test

Key result snippets:

  ok 8 /sgf-io/analysis-roundtrip
  make test ... exited with code 0

## Interfaces and Dependencies

Target SGF tree interface additions:

- `SgfNodeAnalysis *sgf_node_analysis_new(void);`
- `SgfNodeAnalysis *sgf_node_analysis_copy(const SgfNodeAnalysis *analysis);`
- `void sgf_node_analysis_free(SgfNodeAnalysis *analysis);`
- `gboolean sgf_node_set_analysis(SgfNode *node, const SgfNodeAnalysis *analysis);`
- `SgfNodeAnalysis *sgf_node_get_analysis(const SgfNode *node);`
- `gboolean sgf_node_clear_analysis(SgfNode *node);`

SGF IO custom properties (one node):

- `GCAD[<depth>]`
- `GCAS[nodes=<u64>;tt_probes=<u64>;tt_hits=<u64>;tt_cutoffs=<u64>]`
- repeated `GCAN[<move>:<score>]`

Dependencies remain GLib/GObject/GTK + existing project modules.

Plan updates:
- 2026-03-07: Replaced previous stale ExecPlan with this analysis-storage and persistence implementation plan.
- 2026-03-07: Completed implementation, updated progress/decisions/discoveries/outcomes, and added validation
  evidence.
