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
- Run GTK tests on the local display. Do not wrap them in `xvfb-run`; use the existing `$DISPLAY`.
- Use `make test-local` for the default full test run. It sets `GSETTINGS_BACKEND=memory` and delegates to `make test`.
- When Codex runs `make test-local`, run it unsandboxed / with escalated permissions so GTK tests can use the local
  display.

# Before committing

- Always run `make test-local` before committing.
- Always build all the binaries before committing.

# Bugs

- When fixing a bug, add an entry in doc/BUGS.md.

# ExecPlans
 
When writing complex features or significant refactors, use an ExecPlan (as described in doc/PLANS.md) from design to
implementation.

# Repository overview

Read (and update) doc/OVERVIEW.md for any change in src/.
