BUGS

This document lists past bugs, their symptoms, and how they were fixed. The goal is to detect patterns and avoid similar
mistakes in the future. The first entry below shows a template.

## Short description of the bug

The goal of the code was...

In practice, this led to...

The fix consisted of...

## Men pieces rendered with 3-character board squares

The goal of the board renderer was to keep each playable square at a consistent 4-character width for alignment.

In practice, the draughts men symbols were treated as double-width, so the padding calculation only added one space and
the displayed square width collapsed to 3 characters in terminals that render those glyphs as single-width.

The fix consisted of treating the draughts man/king symbols as single-width characters when computing padding.

## Linker errors for GLib symbols

The goal of the build was to compile the CLI and test binaries with GLib logging and assertion helpers.

In practice, this led to linker failures for GLib symbols because the Makefile did not pass the GLib linker flags when
producing the binaries.

The fix consisted of adding the GLib linker flags to the test and CLI link commands.

## Jump generation could capture a friendly piece

The goal of the move generator was to chain captures without mutating the board state during depth-first search.

In practice, the jump continuation used the piece stored on the board at the landing square, which is empty during
generation. The empty square was treated as a black piece, so the search considered white men to be opponents and
offered illegal jumps like 28x19x26 that captured a friendly piece.

The fix consisted of passing the moving piece into the jump search so that opponent checks use the correct color.

## GTK application build failed with deprecated flags

The goal of the GTK application constructor was to set the application flags for the GtkApplication instance.

In practice, the build failed under -Werror because G_APPLICATION_FLAGS_NONE is deprecated in newer GLib versions.

The fix consisted of switching to G_APPLICATION_DEFAULT_FLAGS to match the updated API and clear the warning.

## Square index numbers disappeared on the GTK board

The goal of the GTK board view was to show square numbers beneath each playable square.

In practice, the label ended up in a layout slot that could be compressed by the piece stack, so the numbers rendered
faintly or not at all when the button height matched the piece artwork.

The fix consisted of using a GtkOverlay to position the index label on top of the square so it always renders visibly.

## Screenshot captures were black under Xvfb

The goal of the screenshot helper was to launch the GTK app under Xvfb and capture the rendered window.

In practice, GTK's default renderer produced a black frame buffer under Xvfb, so screenshots were entirely black.

The fix consisted of forcing the GTK renderer to the Cairo backend (and explicitly using the X11 backend) in the
screenshot helper so the UI is rasterized correctly under Xvfb.
