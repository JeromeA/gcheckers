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
- Use "make screenshot" to check the UI, when doing a UI change (but make sure you don't commit the screenshots). It
  will run gcheckers from Xvfb and take screenshots of the window.

# Bugs

- When fixing a bug, add an entry in BUGS.md.

# ExecPlans
 
When writing complex features or significant refactors, use an ExecPlan (as described in PLANS.md) from design to
implementation.

# Repository overview

This overview lists each non-trivial file so you can quickly find where functionality and documentation live.

- AGENTS.md: contributor instructions, coding conventions, and workflow requirements for this repo.
- BUGS.md: log of past bugs, symptoms, and fixes to avoid regressions.
- EXECPLAN.md: current execution plan for GTK move-selection parity work, including progress and decisions.
- PLANS.md: detailed specification for writing and maintaining ExecPlans.
- README.md: high-level project description, dependencies, and basic build/run steps.
- Makefile: build targets for the library, CLI, GTK app, tests, coverage, and screenshots.
- .gitignore: ignored build artifacts and temporary files for Git.
- src/board.h: board data structures and public helpers for coordinates, pieces, and playable squares.
- src/board.c: board storage logic, reset/init, coordinate conversion, and piece helpers.
- src/checkers_constants.h: shared size limits for boards, moves, and byte storage.
- src/game.h: core game types, rules, state, and public API for move listing and application.
- src/game.c: game lifecycle, move application, history, promotion, and winner updates.
- src/game_print.c: terminal formatting for the board and move notation.
- src/move_gen.c: move generation for simple moves, jumps, and rules like forced captures.
- src/checkers_model.h: GObject model API that wraps the game engine for GTK use.
- src/checkers_model.c: model implementation, move validation, random AI moves, and state-change signals.
- src/checkers_cli.c: CLI entry point with a prompt-driven loop for human vs. AI play.
- src/gcheckers.c: GTK application entry point that launches the GApplication.
- src/gcheckers_application.h: GTK application type declaration.
- src/gcheckers_application.c: GTK application activation that creates the main window and model.
- src/gcheckers_window.h: GTK window type declaration.
- src/gcheckers_window.c: GTK UI, board rendering, move selection, and styling logic.
- tests/test_board.c: unit tests for board initialization, getters, and coordinate helpers.
- tests/test_game.c: unit tests for applying moves and rule presets.
- tests/test_game_print.c: tests for move notation formatting and board rendering output.
- tests/test_move_gen.c: unit tests for move generation rules, captures, and helper utilities.
- tests/test_checkers_model.c: unit tests for the GObject model APIs and state transitions.
- tools/coverage_report.py: generates HTML coverage reports from gcov output.
- tools/setup.sh: installs GTK/Xvfb/ImageMagick dependencies on Debian-based systems.
- tools/screenshot_gcheckers.sh: runs the GTK app under Xvfb and captures a screenshot.
