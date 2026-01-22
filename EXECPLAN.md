# Add SGF move tree and navigation panel

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This plan is maintained according to PLANS.md at the repository root (`PLANS.md`).

## Purpose / Big Picture

After this change, the GTK app shows a split window with the checkers board on the left and an SGF move tree panel on
the right. Each move played adds a numbered black or white disc to the SGF panel, selects it, and lets the user click
any prior move to restore the game state by replaying moves from the beginning. The SGF data model and view are generic
and reusable for other games. The UI also exposes per-color dropdowns to choose between human and computer control and a
"Force move" button that triggers an AI move for the current player without auto-playing when navigating the SGF.
You can see it working by running `./gcheckers`, making a few moves, and clicking earlier discs to jump the board state
back; the SGF panel should reflect the current selection, and the Force move button should cause the AI to play a move
when clicked.

## Progress

- [x] (2026-01-18 20:05Z) Review existing GTK layout, model APIs, and tests to determine integration points for an SGF
  model/view and AI control widgets.
- [x] (2026-01-18 20:20Z) Define a generic SGF tree object with nodes, colors, payload storage, and selection APIs, plus
  unit tests.
- [x] (2026-01-18 20:30Z) Add a generic SGF view widget that renders numbered discs with selection handling and emits
  node-selection signals.
- [x] (2026-01-18 20:40Z) Integrate SGF tree and view into the GTK window with a split layout, AI control dropdowns, and
  Force move button.
- [x] (2026-01-18 20:50Z) Wire the checkers model to append moves to SGF, replay moves on selection, and ensure AI only
  moves when forced.
- [x] (2026-01-18 20:55Z) Run `make all`, `make test`, and `make screenshot`, then update the plan and commit.

## Surprises & Discoveries

- Observation: The board view previously hard-coded white as the only human-controlled side, so it needed explicit input
  gating to support user/computer control per color.
  Evidence: The pre-change board view only enabled clicks when `state->turn == CHECKERS_COLOR_WHITE`.

## Decision Log

- Decision: Store SGF node payloads as `GBytes` to keep the SGF tree generic and decoupled from game-specific structs.
  Rationale: `GBytes` gives copyable, ref-counted storage that callers can interpret without the SGF tree knowing the
  payload type.
  Date/Author: 2026-01-18 / Codex
- Decision: Use the model's history size to detect newly applied moves for SGF updates instead of comparing move
  contents.
  Rationale: Move content can repeat, while the history counter accurately signals when a new move has been applied.
  Date/Author: 2026-01-18 / Codex
- Decision: Remove automatic AI turns in favor of the explicit Force move button.
  Rationale: The requirement states navigation should never auto-trigger AI and introduces Force move for manual control.
  Date/Author: 2026-01-18 / Codex

## Outcomes & Retrospective

- Outcome: The GTK UI now includes a split SGF panel with selectable move discs, generic SGF tree/view components, and
  explicit player-control widgets plus a Force move action. SGF navigation replays moves without triggering AI. Tests,
  builds, and UI screenshot checks were run.

## Context and Orientation

The checkers engine is wrapped by `src/checkers_model.c`, which exposes move listing, application, and random AI
selection; the GTK window in `src/gcheckers_window.c` builds the UI and responds to state changes. The board itself is
rendered via `src/gcheckers_board_view.c`. The new SGF model and SGF view must be generic and not depend on checkers
code, so they should live in new `src/sgf_*.{c,h}` files with GLib/GObject patterns. The SGF view will be a GObject
wrapper around GTK widgets, similar to `GCheckersBoardView`, and will emit signals for selection changes so the window
can replay moves via the checkers model. Unit tests live under `tests/` and are run by `make test`.

## Plan of Work

First, inspect the existing GTK window layout, model API, and tests to choose integration points for new objects. Next,
add a generic SGF tree implementation with nodes that store color, move numbers, and arbitrary payload bytes. Provide
functions to append moves, retrieve the root and current nodes, change the current node, and traverse the main line for
rendering; add unit tests in `tests/test_sgf_tree.c` that cover append, selection, and payload access. Then implement a
generic SGF view object that owns a GTK container, renders a chain of numbered black/white discs with selection
highlighting, and emits a signal when a node is selected by clicking. After that, update the GTK window to use a split
pane with the board on the left and a sidebar containing the SGF view plus AI control widgets (dropdowns for human vs.
computer for each color, and a Force move button). Finally, wire the checkers model to append each applied move to the
SGF tree, to replay moves from the SGF when a node is selected, and to only let the AI move when the Force move button
is pressed.

## Concrete Steps

1) Review `src/gcheckers_window.c`, `src/checkers_model.c`, and the current tests in `tests/` to plan integration.
2) Add `src/sgf_tree.h` and `src/sgf_tree.c` defining the SGF tree API and implement unit tests in
   `tests/test_sgf_tree.c`.
3) Add `src/sgf_view.h` and `src/sgf_view.c` to render a chain/tree of numbered discs with selection and signals.
4) Update `src/gcheckers_window.c` to build the split UI, add AI controls, and integrate SGF view events.
5) Update `src/checkers_model.c` or window logic to append moves to the SGF tree and replay moves from selection.
6) From the repository root, run:

   make all
   make test
   make screenshot

7) Update this plan with progress, surprises, and decisions, then commit.

## Validation and Acceptance

The GTK window should show a split layout with the board on the left and an SGF panel on the right. As the user plays
moves, the SGF panel should show new numbered discs with the correct color, and the newest disc should be selected.
Clicking an earlier disc should restore the board to that position by replaying moves from the start, without triggering
an automatic AI move. The Force move button should apply one AI move for the current player, regardless of the dropdown
settings. `make all` should build all binaries without warnings, `make test` should pass including the new SGF unit
tests, and `make screenshot` should produce a UI screenshot without committing it.

## Idempotence and Recovery

All steps are additive and can be repeated safely. If build tools or GTK dependencies are missing, install them using the
existing tooling described in README.md or tools/setup.sh, then rerun the build and tests. If the SGF view layout needs
adjustment, modify the GTK widgets and rerun `make screenshot` without committing the generated images.

## Artifacts and Notes

Expected SGF unit test output excerpt:

  PASS: sgf tree append and selection

## Interfaces and Dependencies

`src/sgf_tree.h` should define:

  typedef enum {
    SGF_COLOR_NONE = 0,
    SGF_COLOR_BLACK,
    SGF_COLOR_WHITE
  } SgfColor;

  typedef struct _SgfNode SgfNode;
  G_DECLARE_FINAL_TYPE(SgfTree, sgf_tree, SGF, TREE, GObject)

  SgfTree *sgf_tree_new(void);
  void sgf_tree_reset(SgfTree *self);
  const SgfNode *sgf_tree_get_root(SgfTree *self);
  const SgfNode *sgf_tree_get_current(SgfTree *self);
  const SgfNode *sgf_tree_append_move(SgfTree *self, SgfColor color, GBytes *payload);
  gboolean sgf_tree_set_current(SgfTree *self, const SgfNode *node);
  GPtrArray *sgf_tree_build_main_line(SgfTree *self);
  SgfColor sgf_node_get_color(const SgfNode *node);
  guint sgf_node_get_move_number(const SgfNode *node);
  const SgfNode *sgf_node_get_parent(const SgfNode *node);
  const GPtrArray *sgf_node_get_children(const SgfNode *node);
  GBytes *sgf_node_get_payload(const SgfNode *node);

`src/sgf_view.h` should define:

  G_DECLARE_FINAL_TYPE(SgfView, sgf_view, SGF, VIEW, GObject)

  SgfView *sgf_view_new(void);
  GtkWidget *sgf_view_get_widget(SgfView *self);
  void sgf_view_set_tree(SgfView *self, SgfTree *tree);
  void sgf_view_set_selected(SgfView *self, const SgfNode *node);
  void sgf_view_refresh(SgfView *self);

The SGF view should emit a `node-selected` signal with the selected node pointer when the user clicks a disc.

Plan updates:
- 2026-01-18: Plan created for SGF move tree, view, and UI integration.
- 2026-01-18: Updated interfaces to include sgf tree reset, node child access, and SGF view refresh.
