## SGF scroller misses the newly added move after play

The SGF move list should scroll to the freshly inserted node so the latest move stays visible when the tree grows.

When move 12 is inserted while move 11 is off screen, the tick callback runs and clamps the scroll adjustments, but the
view still ends up showing move 11 and not the newly inserted move 12.

Diagnostics timeline:
1. Selection updates to move 12, and the node widget is present in the hash table for that selection.
2. Layout computes per-column/per-row extents and content sizing computes a larger expected width than the allocated
   overlay width.
3. The overlay and viewport are requested at the larger content size, and resizes are queued.
4. The tick callback runs, clamps the adjustments, and finds that the overlay allocation still matches a smaller
   width that is constrained to the visible viewport allocation.
5. The viewport allocation matches the root allocation, confirming the viewport stays at visible size.
6. The scrolled window adjustments (upper/page size) mirror the overlay allocation, so the clamp can only move to the
   maximum available scroll value.
7. The selected move bounds still sit beyond that maximum, so move 12 remains off screen even though the clamp ran.
8. Logs show the scrolled window policy is automatic and propagate-natural sizing is disabled, which aligns with the
   viewport sticking to the visible allocation.
9. To prove remaining likely causes, log: (a) root, viewport, and overlay allocations in the tick callback to show the
   viewport remains constrained, (b) adjustment upper/page size alongside expected content size to show stale sizing,
   (c) each size request vs the tick allocation to confirm a later allocation override, and (d) scrolled window policy
   and propagate-natural settings to detect viewport allocation constraints.

Remaining likely causes:
- The viewport allocation remains constrained to the visible area, so the overlay never receives the full requested
  width.
- The scrolled window adjustments are still computed from a stale allocation (before the new size request is applied),
  so `upper` remains capped at the old width.
- A later size allocation step overrides the requested overlay size and constrains the scrollable range even after a
  resize is queued.
- Scrolled window policy or propagate-natural settings keep the viewport at the visible size rather than natural
  content width.

