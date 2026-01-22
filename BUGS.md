BUGS

This document lists past bugs, their symptoms, and how they were fixed. The goal is to detect patterns and avoid similar
mistakes in the future. The first entry below shows a template.

## Short description of the bug

[What the goal of the code was]

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
