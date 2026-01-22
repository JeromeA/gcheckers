BUGS

This document lists past bugs, their symptoms, and how they were fixed. The goal is to detect patterns and avoid similar
mistakes in the future. The first entry below shows a template.

## Short description of the bug

[How the code was trying to do things right]

[What was actually happening and why]

[How it was fixed]

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
