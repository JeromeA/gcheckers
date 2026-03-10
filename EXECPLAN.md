# Full-Game SGF Analysis Button And Branch Value Graph

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This plan is maintained according to PLANS.md at the repository root (`PLANS.md`).

## Purpose / Big Picture

After this change, the analysis panel supports two new user-visible workflows.

First, users can click a new `Analyze full game` button to analyze every node in the SGF tree, not only the currently
selected node. Analysis results remain attached to SGF nodes (`SgfNodeAnalysis`) exactly like existing per-node
analysis.

Second, users see a graph for the current SGF branch (root to current node, then from current node to end along the
main-child line). The graph plots one value per node from stored node analysis. Changing SGF selection moves a vertical
bar in the graph. Clicking the graph selects the corresponding SGF node.

A user can verify this by loading or creating a game with several moves and at least one variation, running full-game
analysis, seeing multiple branch points render values, navigating with SGF actions and observing the vertical cursor
move, then clicking graph points and seeing SGF current selection follow.

## Progress

- [x] (2026-03-08 10:28Z) Re-scanned SGF controller, window analysis loop, SGF tree API, tests, build wiring, and style
  system to map all integration points.
- [x] (2026-03-08 10:33Z) Replaced stale ExecPlan scope with this feature-specific living plan.
- [x] (2026-03-08 10:50Z) Added SGF tree branch/traversal helpers and tests (`current-branch`, preorder collection).
- [x] (2026-03-08 10:57Z) Added `AnalysisGraph` module with drawing, selected cursor, and node-activated signal.
- [x] (2026-03-08 11:00Z) Extended SGF controller with `select_node()` and `node-changed` signal.
- [x] (2026-03-08 11:04Z) Integrated analysis panel full-game button + graph and wired bidirectional selection.
- [x] (2026-03-08 11:07Z) Implemented cancellable full-game threaded analysis using SGF-node replay jobs.
- [x] (2026-03-08 11:11Z) Added/updated tests in `test_sgf_tree`, `test_sgf_controller`, and `test_window`.
- [x] (2026-03-08 11:03Z) Updated `src/OVERVIEW.md`.
- [x] (2026-03-08 11:03Z) Ran `make -j$(nproc)` and `make test`.
- [x] (2026-03-10 17:34Z) Refactored `src/window.c` analysis lifecycle into centralized begin/finish/sync helpers to
  remove duplicated UI/state transitions and ensure graph progress clears on natural full-analysis completion.
- [x] (2026-03-10 18:12Z) Added SGF edit mode interactions and mode-gated navigation/force-move behavior across
  `window.c`, `board_view.c`, and `sgf_controller.c`.

## Surprises & Discoveries

- Observation: The repository already has a threaded iterative analyzer that writes `SgfNodeAnalysis` onto SGF nodes,
  so full-game analysis can reuse that payload path instead of inventing a second storage channel.
  Evidence: `gcheckers_window_analysis_flush_cb()` in `src/window.c` attaches `analysis` to `analysis_node`.

- Observation: SGF navigation state changes currently surface as `analysis-requested` but there is no dedicated public
  API for "select this specific node" in `GCheckersSgfController`; graph click handling therefore requires a new API.
  Evidence: `gcheckers_sgf_controller_navigate_to()` is `static` in `src/sgf_controller.c`.

- Observation: `make test` still logs Chromium crashpad warnings in screenshot test setup in this environment, but the
  full target exits with code 0 and all mandatory tests pass/skip normally.
  Evidence: `setsockopt: Operation not permitted` appears before non-failing test execution.

## Decision Log

- Decision: Build the graph as a small dedicated module (`src/analysis_graph.[ch]`) backed by `GtkDrawingArea` and
  `GtkGestureClick`.
  Rationale: This keeps window logic focused on orchestration while encapsulating drawing and hit-testing, and allows
  targeted unit-style widget assertions in `test_window.c`.
  Date/Author: 2026-03-08 / Codex

- Decision: Use the first scored move (`analysis->moves[0]->score`) as the plotted node value.
  Rationale: The analysis text already presents moves "Best to worst"; index 0 is the canonical node evaluation used
  by users. Missing analysis renders as a gap marker.
  Date/Author: 2026-03-08 / Codex

- Decision: Centralize analysis lifecycle transitions in `window.c` (`begin_session`, `finish_session`, `sync_ui`).
  Rationale: Button state, progress marker visibility, and runtime counters were previously reset in multiple places,
  which caused drift (for example, yellow progress marker persisting after natural completion).
  Date/Author: 2026-03-10 / Codex

- Decision: Keep edit-mode board mutations SGF-driven by writing setup properties on the current node and replaying the
  current node into the model via a controller refresh helper.
  Rationale: SGF remains the source of truth, so edits persist in-tree and remain consistent with replay/load/save
  behavior without introducing a separate model-only editing path.
  Date/Author: 2026-03-10 / Codex

## Outcomes & Retrospective

Implementation is complete and matches the requested behavior.

The analysis panel now includes `Analyze full game` and a branch graph. Full-game analysis iterates SGF nodes in
preorder, reconstructs each node position from root-path move snapshots, and attaches `SgfNodeAnalysis` results to each
node at configured depth (computer level, with `0 => 1`). Existing iterative current-node analysis remains available
through the `Analyze` toggle.

Graph behavior is bidirectional with SGF selection: SGF current-node changes refresh graph data/cursor through the new
controller `node-changed` signal, and graph activation calls the new public SGF controller node-selection API.

Validation succeeded with `make -j$(nproc)` and `make test`.

Follow-up lifecycle cleanup consolidated full-game/current-analysis session transitions so UI and transient state are
driven by one state machine path rather than duplicated manual resets.
Follow-up edit-mode support now allows direct SGF setup editing on the current node with left/right click piece cycles
while SGF navigation and force-move actions are suspended.

## Context and Orientation

`src/window.c` owns the right-side analysis panel, including the existing `Analyze` toggle and threaded analysis loop.
That loop currently analyzes only the current SGF node, publishing progress text and completed `SgfNodeAnalysis` back to
GTK main thread.

`src/sgf_controller.c` is the SGF timeline authority and already knows how to replay/select nodes while keeping model
state in sync. SGF view clicks and toolbar actions all funnel through internal navigation helpers.

`src/sgf_tree.c` stores SGF node hierarchy and per-node structured analysis, but it currently lacks branch-construction
helpers for "current branch" and full-tree enumeration.

`tests/test_window.c` already probes analysis-panel controls and SGF navigation behavior through GTK widget traversal,
which is the right place to assert the new button and graph-driven selection behavior.

In this document, "current branch" means: path from root to current node, followed by repeatedly taking child index 0
until leaf.

## Plan of Work

First add SGF traversal helpers in `src/sgf_tree.[ch]` so both graph data generation and full-tree analysis iteration
share one canonical implementation. These helpers return `GPtrArray` lists of `const SgfNode *` in deterministic order.

Then add `src/analysis_graph.[ch]`, a small GTK object that exposes a widget, accepts a branch node list and computed
values, draws a line plot with a vertical selected-index bar, and emits a signal when clicked index changes.

Next extend `src/sgf_controller.[ch]` with a public node-selection entry point and a `node-changed` signal emitted for
all successful current-node transitions. Window code will use this for graph synchronization and graph click handling.

After that, refactor `src/window.c` analysis internals to support two modes: existing iterative current-node analysis
(toggle) and new fixed-depth full-tree analysis (button). Both modes reuse cancellation generations and publish to main
thread. Full-tree mode iterates all nodes, reconstructs each node position from SGF root path, runs analysis, and
attaches results to nodes.

Finally, wire graph refreshes whenever SGF current changes or new analysis data arrives, add tests, then update overview
documentation and run full build/tests.

## Concrete Steps

From `/home/jerome/Data/gcheckers`:

1. Edit `src/sgf_tree.h` and `src/sgf_tree.c` to add:
   - `sgf_tree_build_path_to_node(SgfTree *, const SgfNode *)`
   - `sgf_tree_build_main_line_from_node(const SgfNode *)`
   - `sgf_tree_build_current_branch(SgfTree *)`
   - `sgf_tree_collect_nodes_preorder(SgfTree *)`
2. Add traversal tests to `tests/test_sgf_tree.c`.
3. Create `src/analysis_graph.h` and `src/analysis_graph.c`.
4. Add/compile the new module in `Makefile` targets (`gcheckers`, `test_window`).
5. Extend `src/sgf_controller.h`/`src/sgf_controller.c` with a public node-selection API and `node-changed` signal.
6. Extend `src/window.c` with:
   - `Analyze full game` button in analysis panel,
   - graph widget placement,
   - branch rebuild and graph selection synchronization,
   - graph click -> SGF node select,
   - full-game analysis worker path.
7. Extend `tests/test_sgf_controller.c` and `tests/test_window.c`.
8. Update `src/OVERVIEW.md` for new analysis panel behavior and new module.
9. Run:
   - `make -j$(nproc)`
   - `make test`

## Validation and Acceptance

Acceptance criteria:

- Analysis panel shows both `Analyze` toggle and `Analyze full game` button.
- Full-game analysis populates `SgfNodeAnalysis` on multiple SGF nodes in one run.
- Graph renders branch values from SGF node analyses and updates cursor on SGF selection changes.
- Clicking graph causes SGF current selection to move to the corresponding node.
- SGF controller exposes and passes tests for explicit node selection API and node change signaling.
- `make -j$(nproc)` and `make test` pass.

## Idempotence and Recovery

The implementation is additive and safe to repeat. If partial edits break compilation, recover by synchronizing header
and implementation signatures first, then rerun `make -j$(nproc)` to surface remaining mismatches. If GTK tests fail in
headless environments, rerun with existing Broadway-backed test targets via `make test`.

## Artifacts and Notes

Validation commands to capture in this section as work completes:

  make -j$(nproc)
  make test

Expected new/updated tests include names similar to:

  /sgf-tree/current-branch
  /sgf-controller/select-node
  /gcheckers-window/analysis-full-game-controls

## Interfaces and Dependencies

New SGF tree interfaces in `src/sgf_tree.h`:

- `GPtrArray *sgf_tree_build_path_to_node(SgfTree *self, const SgfNode *node);`
- `GPtrArray *sgf_tree_build_main_line_from_node(const SgfNode *node);`
- `GPtrArray *sgf_tree_build_current_branch(SgfTree *self);`
- `GPtrArray *sgf_tree_collect_nodes_preorder(SgfTree *self);`

New analysis graph module interface in `src/analysis_graph.h`:

- `AnalysisGraph *analysis_graph_new(void);`
- `GtkWidget *analysis_graph_get_widget(AnalysisGraph *self);`
- `void analysis_graph_set_nodes(AnalysisGraph *self, GPtrArray *nodes, guint selected_index);`
- `void analysis_graph_set_selected_index(AnalysisGraph *self, guint selected_index);`
- `guint analysis_graph_get_selected_index(AnalysisGraph *self);`
- `guint analysis_graph_get_node_count(AnalysisGraph *self);`

New SGF controller interface in `src/sgf_controller.h`:

- `gboolean gcheckers_sgf_controller_select_node(GCheckersSgfController *self, const SgfNode *node);`

New SGF controller signal in `src/sgf_controller.c`:

- `"node-changed"` with one `G_TYPE_POINTER` parameter (`const SgfNode *`).

Plan updates:
- 2026-03-08: Replaced previous analysis-persistence-only ExecPlan with this feature implementation plan because the
  user requested new full-game analysis and graph behavior.
- 2026-03-08: Recorded final implementation results, validation evidence, and completed progress state.
- 2026-03-10: Added lifecycle cleanup milestone to centralize analysis session state transitions and prevent
  post-completion progress highlight drift.
