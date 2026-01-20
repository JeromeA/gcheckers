BUGS

This document lists past bugs, their symptoms, and how they were fixed. The goal is to detect patterns and avoid similar
mistakes in the future. The first entry below shows a template.

## Short description of the bug

[What the goal of the code was]

[What was happening in practice and why]

[How it was fixed]

## Screenshot captures were black under Xvfb

The goal of the screenshot helper was to launch the GTK app under Xvfb and capture the rendered window.

In practice, GTK's default renderer produced a black frame buffer under Xvfb, so screenshots were entirely black.

The fix consisted of forcing the GTK renderer to the Cairo backend (and explicitly using the X11 backend) in the
screenshot helper so the UI is rasterized correctly under Xvfb.
