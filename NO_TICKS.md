# No tick callbacks for SGF scrolling

This project previously used GTK tick callbacks (`gtk_widget_add_tick_callback`) to scroll the SGF view after
selection changes. That approach has repeatedly failed as the SGF layout has evolved: tick timing is not a reliable
proxy for "layout is valid", and it leads to fragile retry logic (multiple ticks, polling, and defensive geometry
checks) that still produces incorrect scroll positions.

## Why tick callbacks are unreliable here

Tick callbacks run on frame clocks, not on layout completion. The SGF view needs precise widget bounds that are only
valid after GTK has completed layout allocation. When a tick fires too early, geometry can still be zero, stale, or
inconsistent across the overlay/grid hierarchy, and those invalid values cause the scroll clamp logic to act on the
wrong bounds.

Even with retries, tick callbacks couple scrolling to rendering cadence instead of the actual layout lifecycle. That
relationship becomes more brittle as the SGF tree grows, as widgets are inserted or removed, or as the sizing logic
changes. The result is a persistent class of bugs where selection is not scrolled into view reliably.

## Preferred approach: layout-driven scrolling

If we need to scroll after layout changes, the scroll request should be tied to layout-driven signals, not to a frame
tick. In GTK4, widgets expose size request properties rather than a public size-allocate signal. The SGF scroller
should attempt to scroll immediately when possible, but if layout is not ready it must wait for size request
notifications (for example, `notify::width-request` and `notify::height-request` on the overlay that owns the tree
content) and scroll once those requests are updated.

This approach keeps scrolling deterministic and tied to the actual source of truth for geometry: layout allocation.
It avoids fragile timing and reduces the amount of defensive code needed to make the selection visible.

## Guidance for future changes

- Do not reintroduce tick callbacks for SGF selection scrolling.
- Prefer layout-driven hooks like size request notifications to ensure geometry is valid before computing scroll
  bounds.
- If scrolling is triggered by selection changes without layout changes, attempt to scroll immediately; only fall back
  to size request notifications when geometry is not yet available.
