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

- Always run the tests before committing.
- Always build all the binaries before committing.

# Bugs

- When fixing a bug, add an entry in BUGS.md.

# ExecPlans
 
When writing complex features or significant refactors, use an ExecPlan (as described in PLANS.md) from design to
implementation.

# Repository overview

See REPOSITORY_OVERVIEW.md for the repository overview.
