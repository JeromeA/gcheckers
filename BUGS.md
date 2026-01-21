BUGS

This document lists past bugs, their symptoms, and how they were fixed. The goal is to detect patterns and avoid similar
mistakes in the future. The first entry below shows a template.

## Short description of the bug

[What the goal of the code was]

[What was happening in practice and why]

[How it was fixed]

## Man cylinder bottom curve was shallower than the top

The goal was for the checker men to have identical curvature on the top and bottom edges of the cylinder.

In practice, the bottom arc used a smaller vertical radius than the top arc, so the base appeared flatter.

The fix reuses the same outer radius for the bottom arc as the top to keep the curves identical.

## Screenshot captures were black under Xvfb

The goal of the screenshot helper was to launch the GTK app under Xvfb and capture the rendered window.

In practice, GTK's default renderer produced a black frame buffer under Xvfb, so screenshots were entirely black.

The fix consisted of forcing the GTK renderer to the Cairo backend (and explicitly using the X11 backend) in the
screenshot helper so the UI is rasterized correctly under Xvfb.

## Square index numbers were not visible

The goal was to show square index numbers on each playable board square.

In practice, the index labels blended into the board and were too subtle to read against the square colors.

The fix added contrast styling and spacing to the square index labels so the numbers render with a visible background.

## Dark board squares stayed white

The goal was for playable squares to use the dark brown board color when the board was built.

In practice, the playable squares are buttons and the theme button background styling overrode the custom class, so the
dark background color was ignored.

The fix explicitly targeted the button class and cleared the default background image so the board-dark background color
is applied.

## Piece size request did not resize the men

The goal was to enlarge the men visuals by increasing the size request on the piece picture widget.

In practice, the picture lives inside a button that still had a 52x52 size request, so the button constrained the
overlay and prevented the picture from growing.

The fix introduced a single square size constant and applied it to both playable buttons and light-square labels so the
square size matches the requested piece dimensions.

## Empty playable squares appeared nearly black

The goal was for empty dark squares to render using the same brown background as occupied dark squares.

In practice, empty squares were showing a dark overlay because the piece label widget (with a semi-transparent black
background) was visible even when there was no piece.

The fix hides the piece overlay entirely when a square is empty, preventing the label background from tinting the square.

## Men SVGs were pixelated when resizing

The goal was for the SVG men to stay crisp at any window size.

In practice, the SVG was rasterized into a fixed-size texture and then scaled, which caused pixelation when the board
grew.

The fix replaced the one-time rasterization with a custom paintable that redraws the vector shapes at snapshot time.

## Bottom curvature did not match the top of the man cylinder

The goal was for the checker piece cylinder to have matching top and bottom curvature.

In practice, the bottom curvature was a custom bezier curve, so it did not match the ellipse used for the top.

The fix draws the bottom edge using the same scaled arc as the top, ensuring the curvature matches.
