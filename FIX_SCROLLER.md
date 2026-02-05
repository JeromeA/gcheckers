Goal: make `sgf_view_scroller` trivially understandable and reliable by enforcing exactly two paths only: scroll requests and layout-changed retries for the remembered selection.

## What is already good

Your current behavior already matches your two-path description at a high level:

1. Request path: remember selection, then attempt scroll now.
2. Layout-changed path: if remembered selection exists, attempt scroll again.

That is exactly what `sgf_view_scroller_request_scroll()` and `sgf_view_scroller_on_layout_changed()` do today.

And `src/OVERVIEW.md` already describes that same two-path model explicitly.

## Where it still feels “complex” (and likely bug-prone)

Even though the flow is simple, the implementation has a lot of geometry/math and “helper chain” logic:

- lookup widget
- derive grid cell
- sum extents + spacing + margins
- clamp page

That’s spread across multiple helpers (`lookup_widget`, `compute_bounds`, `clamp_to_bounds`, `try_scroll...`) and each
can fail for different reasons.

Also, `NO_TICKS.md` still recommends a fallback waiting strategy with size-request notifications when geometry isn’t
ready. That introduces a third conceptual path in people’s heads (“request now”, “layout changed”, and “notification
fallback”), which conflicts with your “exactly two paths” principle.

## Suggestions to make it much simpler and clearer

### 1) Make one internal function the single source of truth

Create one internal function with a name like:

- `sgf_view_scroller_try_scroll_node_if_present(...)`

It should do exactly this contract:

- Input: node
- If node has a mapped widget in current view, scroll to fully visible
- Else no-op

No boolean return needed unless you truly consume it. This helps avoid accidental branch growth.

This consolidates the behavior currently split across helper naming and return values.

### 2) Make public API mirror the two paths literally

Keep only these two public entry points (you already do this):

- `request_scroll(selected)` → store `remembered_selected`, then `try_scroll_node_if_present(selected)`
- `on_layout_changed()` → if remembered != NULL then `try_scroll_node_if_present(remembered)`

That flow is already present; I’d reinforce it by reducing comments elsewhere and adding a tiny “truth table” comment
above these two functions.

### 3) Remove/avoid any “deferred retry” logic from scroller itself

Given your goal, do not keep fallback scheduling in scroller (ticks or notify callbacks). `NO_TICKS.md` should be
aligned to this stricter model: only immediate attempt on request + immediate attempt on layout-updated signal.
If geometry isn’t ready, that’s fine — layout update will call path #2.

Right now `NO_TICKS.md` still implies fallback notification wiring as part of scroller policy.

### 4) Prefer GTK coordinate APIs over manual grid math (if feasible)

The heaviest complexity/risk is `compute_bounds()` manually recomputing origin from extents, spacing, margins.

If possible, simplify to a “widget-to-scroller coordinates then clamp” approach using GTK allocation/translate APIs.
The less duplicated layout math you own, the fewer chronic off-by-one / stale-extent bugs.

Even if you keep current math, isolate it into one small block with explicit invariants (“bounds are in scrolled
content coordinates”).

### 5) Make “node exists in view” a strict, named condition

Define “exists in view” in code as:

- node is in `node_widgets`
- widget attached to expected parent
- bounds measurable

Treat everything else as “does not exist right now” (no-op).
This reframes many failure branches as one semantic outcome, which is exactly your desired first principle.

Today this is implied across multiple `FALSE` returns/debugs.

### 6) Document the invariant at call sites too

In `sgf_view.c`, the two call sites are clear:

- on select → request path
- on layout-updated signal → layout path

Add one concise comment there saying “these are the only two triggers.” This prevents future “helpful” third trigger
additions.

### 7) Tighten docs so they cannot be misread

Update wording in docs (when you choose to change code/docs) to:

- “No deferred retries in scroller.”
- “Scroller is stateless except remembered selection.”
- “Layout-updated is the only retry trigger.”

Current wording in `NO_TICKS.md` can be interpreted as adding async notification complexity again.

## Proposed mental model (the “bug-resistant” one)

- **State:** `remembered_selected` only.
- **Event A (request):** remember node; attempt now.
- **Event B (layout changed):** attempt remembered node.
- **Attempt rule:** if node present + measurable → clamp to full visibility; else no-op.
- **No other events, no retries, no timers, no tick callbacks, no deferred scheduling.**

That gives you a tiny deterministic state machine and makes “unexpected behavior” easier to reason about.
