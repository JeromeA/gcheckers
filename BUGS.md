BUGS

This document lists past bugs, their symptoms, and how they were fixed. The goal is to detect patterns and avoid similar
mistakes in the future. The first entry below shows a template.

## Short description of the bug

[How the code was trying to do things right]

[What was actually happening and why]

[How it was fixed]

## SGF tree selection scrolled back to the first node

The SGF move tree should keep the current scroll position stable and only nudge the view if a newly selected disc is
partially off screen.

Selecting a disc rebuilt the entire tree widget, which reset the scrolled window back to the first node before the
selection scroll adjustment ran.

The fix updates the selected disc styling in place when possible and only rebuilds the tree when the cached widget map
is unavailable, keeping the scroll position stable while still ensuring the selected disc is visible.

## SGF branch discs were not aligned with main line columns

The SGF move tree should keep discs aligned in strict columns so that main line moves and variations share the same
horizontal positions.

The layout relied on per-row indentation inside horizontal boxes, so any accumulated width differences from previous
rows or styling could shift branch rows just enough that variation discs drifted between columns.

The fix switches the move tree layout to a GtkGrid with homogeneous columns and attaches each disc to an explicit row
and column, keeping branches aligned regardless of depth or styling.

## SGF move tree navigation lost off-screen nodes

The SGF move tree should allow scrolling when the layout grows beyond the available panel size, and keyboard
navigation should keep the selected node visible.

The overlay that held the tree kept its size tied to the visible viewport, so scrollbars never appeared for wide
trees and the selected node could move off screen with no scroll adjustment.

The fix measures the tree grid to size the overlay for scrolling, scrolls to the selected node after rebuilds, and
adds keyboard navigation that moves between parent/child and sibling nodes while keeping focus in view.

## SGF navigation keys moved focus and scrolled to move 1

Navigation keys should keep focus in the SGF panel and leave the selected node unchanged when there is no valid sibling
or child.

The key handler propagated arrow events when it could not find a target node, letting GTK move focus to the first disc
and scroll the panel back to move 1.

The fix consumes navigation key presses even when no target node exists so focus stays in the SGF panel and the view
does not scroll unexpectedly.

## Split panes shrank below their content sizes on launch

The goal was for the left board pane and right controls pane to respect their natural minimum sizes when the window
opens.

In practice, the paned container allowed both sides to shrink below their minimum content sizes, so the board and
controls were clipped in a too-small window.

The fix disables shrinking for both paned children, ensuring each pane retains its minimum content size and the window
enforces a larger minimum size at startup.

## SGF move list showed black moves as light discs

The goal was for the SGF move list to display black moves as dark discs so move colors match the board state.

Instead, black move buttons inherited the theme's default button background image, so the dark background color never
appeared and the discs looked like light outline buttons.

The fix clears the button background image and applies a consistent border so the black disc background color shows up.

## SGF connector lines did not align with discs or show variations

The SGF move list should show connector lines below both the source and destination discs, and it should show lines for
variation branches.

The connectors were vertically centered in the disc rows, which put them above the source disc and below the
destination disc. Variation rows also started without a connector, so branch links were missing entirely.

The fix anchors connector drawing to the bottom of the row and ensures branch rows include an initial connector so
variation lines render consistently.

## SGF tree connectors did not link disc centers or branch correctly

The SGF move list should draw connector lines behind the discs, connect each parent and child disc center, and show
branching lines from a single parent to multiple children.

The connector widgets only drew horizontal segments in front of the discs, so they were misaligned and did not link
the parent disc to both branch discs.

The fix draws connectors in a dedicated background drawing layer, connects disc centers for every parent/child
relationship, and rebuilds the view without per-disc connector widgets.

## SGF move tree discs drifted left on lower rows

The goal was for the SGF move tree to place every disc on the same horizontal grid so identical move numbers align
vertically across rows.

The layout calculated row indentation using the disc size alone, but each disc's allocated size also included its
border, so every deeper row started slightly too far left.

The fix accounts for the full disc stride (including the border) when calculating row indentation and sizing the disc
widgets so each row lands on the same x positions.

## SGF move playback duplicated existing child nodes

When replaying or re-entering a previously explored move, the SGF tree should have reused an existing child node
instead of adding a duplicate.

The append logic always created a new node, even when a child with the same move payload and color already existed,
causing multiple identical branches to appear.

The fix compares the next move against existing children and reuses the matching node, updating the current pointer
instead of creating a duplicate.

## SGF move tree discs were offset to the left

The move tree should center each disc in its row so the columns line up visually.

Each disc button was aligned to the start of its row, which pulled every disc left relative to its row spacing.

The fix centers the disc widgets within their row slots so the grid alignment matches the expected positions.

## Screenshot test reported Chromium missing when chromium-browser was installed

The screenshot test should skip only when Chromium is truly unavailable.

The detection logic only looked for a `chromium` binary, but Debian-style installs often provide
`chromium-browser` instead, so the test incorrectly reported Chromium as missing.

The fix switches the check to `chromium-browser` and threads it through the screenshot script using a Makefile
variable.

## Screenshot tooling assumed chromium-browser when Chrome is installed

The screenshot tooling should launch the installed Chrome binary without requiring Chromium-specific names.

The scripts and Makefile defaulted to `chromium-browser`, so environments that only provide `google-chrome` skipped
screenshots even though Chrome was installed.

The fix updates the default binary name to `google-chrome` and keeps the presence check so screenshot capture still
fails fast when Chrome is missing.

## PlayerControlsPanel disposed while still parented

The window should release child widgets by removing them from their containers before dropping the last object
reference.

`GCheckersWindow` cleared its `PlayerControlsPanel` with `g_clear_object()` while the panel was still appended to a
`GtkBox`.

GTK containers hold references to their children, so disposing a still-parented widget triggered a Gtk-critical during
shutdown.

The fix removes the controls panel from its containing row during `GCheckersWindow::dispose` and adds a GTK test that
keeps the panel alive across `g_object_run_dispose()` to assert that it has been unparented.

## src/OVERVIEW.md mixed section styles and became hard to scan

The overview should use a consistent section format so contributors can quickly find module responsibilities.

The second half of the document used a flat bullet list while the first half used structured headings, which made the
file inconsistent and harder to navigate.

The fix rewrites the remainder of the document into the same heading-based format, grouping related modules under
subsystem sections.

## `GCheckersWindow::dispose` unparented the controls panel without holding a reference

The window should be able to unparent the `PlayerControlsPanel` during dispose without requiring external references.

`GCheckersWindow` called `gtk_box_remove()`/`gtk_widget_unparent()` and then `g_clear_object()` while it only held a
non-owning pointer to the controls panel.

Unparenting dropped the container's last reference and finalized the widget, so the subsequent `g_clear_object()` hit
`g_object_unref()` with a non-`GObject` pointer and raised a GLib-GObject critical.

The fix temporarily acquires a reference to the controls panel before unparenting so the final `g_clear_object()` runs
on a still-valid object, and adds a regression test that disposes the window without taking an external panel
reference.

## Black moves were not highlighted on the board

The board should highlight movable pieces for whichever color is to move so the same affordances appear for both
players.

The view only loaded available moves when the turn color was white, so `board-halo` classes were never applied when it
was black's turn even though the move generator already filtered by the active turn.

The fix removes the white-only guard in `BoardView` and adds a GTK test that advances to a black turn and verifies that
all black starting squares receive the highlight class.

## `GCheckersWindow::dispose` crashed if the controls panel had already been removed

The window should be able to dispose safely even when a child widget was removed from its container earlier in the
session.

`GCheckersWindow` kept only a non-owning pointer to the `PlayerControlsPanel`, so removing the panel from its `GtkBox`
could finalize it immediately while the window still held the stale pointer.

During shutdown, `gcheckers_window_unparent_controls_panel()` then called `gtk_widget_get_parent()` with a
non-`GtkWidget` pointer and triggered the Gtk-critical `gtk_widget_unparent: assertion 'GTK_IS_WIDGET (widget)' failed`.

The fix sinks and retains an owned reference to the controls panel at construction time, removes it via its current box
parent during dispose, and adds a regression test that removes the panel before disposing the window.

## Dispose-time `gtk_widget_unparent` assertions when tearing down views

Widget-owning helper objects should remove their widgets from container parents using the container's public removal
APIs before dropping their last references.

Several dispose handlers called `gtk_widget_unparent()` directly on children that were owned by GTK containers such as
`GtkOverlay` and `GtkGrid`.

Those low-level unparent calls bypassed container bookkeeping, leaving stale child pointers behind so later container
cleanup would attempt to unparent freed widgets and trigger `gtk_widget_unparent: assertion 'GTK_IS_WIDGET (widget)'
failed`.

The fix introduces `gcheckers_widget_remove_from_parent()` to detach widgets via container-specific removal APIs and
updates the relevant dispose handlers and GTK tests to run under a real display backend.
## Selecting Computer prevented the user from making a move

The goal was for the player dropdowns to control auto-play, not to disable manual input.

`GCheckersWindow` disabled board input whenever the active turn was set to Computer, but it did not automatically
trigger an AI move. This left the board insensitive with no follow-up action, stalling the game.

The fix keeps board input enabled whenever the game is still running, schedules a forced move on idle after a move when
the next player is set to Computer, and resets both dropdowns to User when the SGF tree is navigated.

## SGF view did not scroll to a newly appended move

The SGF panel should scroll to keep the most recently selected node visible, including when a move is appended during
play.

The scroll request ran in an idle callback. When a new node was appended, the view rebuilt but the idle callback could
run before GTK completed the next layout pass, so `gtk_widget_compute_bounds()` failed and the scroll request was
dropped.

The fix queues the scroll work on a tick callback so it runs after layout, retries for a few frames while widgets
settle, and adds a regression test that appends moves after the view is mapped and verifies the adjustment changes.

## SGF view snapped back to the first node after appending moves

While playing, the SGF panel could jump back to node one instead of keeping the newly appended node visible.

Even after moving the scroll work onto a tick callback, the `GtkScrolledWindow` adjustments could lag behind the SGF
content size, especially when the child was wrapped in a `GtkViewport`. The scroll callback would run before the
adjustments caught up and clamp the view against stale bounds near the origin.

The fix computes layout extents during rebuild, sets both the overlay and viewport size requests to the expected
content size, and updates the scroller to derive bounds from grid coordinates while ensuring the adjustments' upper
bounds are at least the expected size. The regression test now waits for scrolling to occur within a timeout and
asserts that all nodes are present.
