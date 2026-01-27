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
