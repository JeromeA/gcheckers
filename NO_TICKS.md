# No tick callbacks for SGF scrolling

This project previously used GTK tick callbacks (`gtk_widget_add_tick_callback`) to scroll the SGF view after
selection changes. That approach repeatedly failed as SGF layout evolved: tick timing is not a reliable proxy for
"layout is valid", and retry logic based on frame cadence still produced incorrect scroll positions.

## Why tick callbacks are unreliable here

Tick callbacks run on frame clocks, not on layout completion. The SGF view needs precise widget bounds that are only
valid after GTK layout allocation has completed. When a tick fires too early, geometry can still be zero, stale, or
inconsistent across the overlay/grid hierarchy, and those invalid values cause clamp logic to use the wrong bounds.

Even with retries, tick callbacks couple scrolling to rendering cadence instead of the layout lifecycle. That coupling
gets brittle as the SGF tree grows, widgets are inserted or removed, or sizing logic changes.

## SGF scroller policy: exactly two paths

The scroller must stay simple and deterministic:

1. **Request path:** remember selected node and immediately try to scroll it.
2. **Layout-updated path:** immediately try again for the remembered node.

If geometry is not ready during either attempt, the attempt is a no-op. The next `layout-updated` signal is the only
retry trigger.

## Guidance for future changes

- Do not reintroduce tick callbacks for SGF selection scrolling.
- Do not add deferred retry scheduling to the scroller (no notify callbacks, timers, idles, or polling).
- Keep scroller state limited to remembered selection.
- Keep retries tied only to SGF layout's `layout-updated` signal.
