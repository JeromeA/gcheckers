BUGS

This document lists past bugs, their symptoms, and how they were fixed. Add any new bugs you find to the end of this
list. The first entry below shows a template.

## Short description of the bug

[How the code was trying to do things right]

[What was actually happening and why]

[How it was fixed]

## SGF variation move 1 discs did not share a common parent

The SGF move list should show all move 1 variations branching from a shared root so sibling variations look connected.

The layout skipped rendering the root node entirely, so move 1 discs were placed in separate rows without a visible
parent and the link renderer could not draw a shared connector.

The fix renders the root node as a virtual move zero dot, anchors it in column zero, and shifts the first move column
over so every move 1 disc connects back to the shared root.

## GTK tests were skipped during make test

The test target should exercise all GTK widget tests in headless environments.

The GTK tests called gtk_init_check without a display backend, so the test binaries skipped their assertions when
XDG_RUNTIME_DIR or a Broadway display was not configured.

The fix runs the GTK test binaries under a shared Broadway server from make test so gtk_init_check succeeds and the
tests run instead of skipping.

## SGF horizontal scrollbar position could disagree with visible content position

The SGF inconsistency diagnostic should report only whether the horizontal scroll-window position matches the content
view effective horizontal position.

The old diagnostic compared and logged extra geometry (viewport sizes, vertical offsets, selected-node expected/actual
positions), which made the bug signal noisy and not focused on the visual mismatch being investigated.

The fix narrows the calculation and message to horizontal position mismatch only, and adds a test for the focused
inconsistency predicate.
