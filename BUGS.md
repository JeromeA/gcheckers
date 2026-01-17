BUGS

This document lists past bugs, their symptoms, and how they were fixed. The goal is to detect patterns and avoid similar
mistakes in the future. The first entry below shows a template.

## Short description of the bug

The goal of the code was...

In practice, this led to...

The fix consisted of...

## Linker errors for GLib symbols

The goal of the build was to compile the CLI and test binaries with GLib logging and assertion helpers.

In practice, this led to linker failures for GLib symbols because the Makefile did not pass the GLib linker flags when
producing the binaries.

The fix consisted of adding the GLib linker flags to the test and CLI link commands.
