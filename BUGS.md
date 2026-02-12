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

## GTK scrolled-window inconsistency only reproduced after clicking Force move

The branch needs a deterministic way to trigger the GTK scrolled-window inconsistency immediately for debugging and
reduction work.

The previous reproduction required manually clicking the Force move button three times, which made scripted runs
harder and slower and required interactive input.

The reproduction path now schedules three forced moves automatically during window startup and adds a Broadway make
target that runs `G_MESSAGES_DEBUG=all ./gcheckers` so the inconsistency appears without manual clicks.
