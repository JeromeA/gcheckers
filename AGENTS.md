
# Description

The purpose of this branch in not to work on the original project. Instead, it's to remove as much code as possible to
make a minimal bug reproduction that is found in sgf_view.c about the scrolling window of the sgf view.

# Coding Conventions

- Use 2 space indentation
- Convert existing code to use 2 space indentation
- Use glib/gtk idioms and patterns whenever possible
- Unused parameters should be in `/*comments*/`
- fix all compilation warnings.
- Wrap at 120 characters.
- Convert existing code to wrap at 120 characters.
- When a check fail, a return or return NULL is never enough, it should almost always be a g_return_val_if_fail(), or at
  least a g_debug().

# Tests

- Always write tests for new features.

# Before committing

- Always run "make test".

# Repository overview

Read (and update) src/OVERVIEW.md for any change in src/.

