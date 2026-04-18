# Add puzzle progress tracking and server reporting

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and
`Outcomes & Retrospective` must be kept up to date as work proceeds.

This document must be maintained in accordance with `doc/PLANS.md`.

## Purpose / Big Picture

After this change, puzzle mode will remember how the user performed on each puzzle they actually tried. A "try" means
the user entered a puzzle and made at least one move attempt. For each tried puzzle, the application will keep whether
the attempt ended in success, failure, or "analyze" abandonment, and for failures caused by the very first move it
will also keep the first move the user chose. The application will package those results with a stable per-user
identifier and send them to a server. Local data is retained even after sending so the reporting policy can evolve
later without losing raw history; the server is responsible for deduplication.

The visible result is that puzzle mode keeps working normally, but puzzle attempts survive restarts and are uploaded in
the background once local thresholds are met. A developer can see the change working by running focused unit tests for
storage and payload building, running the window puzzle tests, then launching the app with a test upload URL and
confirming that a local history file is created and that a full report is sent when the "enough unsent data" rule
fires.

## Progress

- [x] (2026-04-17 00:00Z) Write this ExecPlan after surveying the current puzzle runtime, storage helpers, build
      system, and tests.
- [x] (2026-04-18 09:10Z) Add `src/puzzle_progress.c` and `src/puzzle_progress.h` for stable user identity, JSONL
      attempt-history storage, threshold decisions, report marking, and full-history payload construction.
- [x] (2026-04-18 09:35Z) Integrate puzzle attempt lifecycle recording into `src/window.c` so attempts start on the
      first move attempt, terminal outcomes are persisted exactly once, and unresolved started attempts fall back to
      `failure` on puzzle replacement or window shutdown.
- [x] (2026-04-18 09:55Z) Add application-owned async threshold-based upload handling plus startup and shutdown flush
      requests in `src/application.c`.
- [x] (2026-04-18 10:20Z) Add focused tests for user ID persistence, history rewrite/report marking, payload
      formatting, threshold decisions, writable path creation, and compile-checked puzzle-window integration cases.
- [x] (2026-04-18 10:35Z) Update `doc/OVERVIEW.md` and `doc/BUGS.md` to describe the new runtime puzzle progress
      pipeline and the persistence/reporting bug that this change fixes.
- [x] (2026-04-18 10:50Z) Run `make all` and `env -u DISPLAY -u WAYLAND_DISPLAY -u GNOME_SETUP_DISPLAY make test`.
- [x] (2026-04-18 20:21Z) Add `tests/puzzle_progress_report_server.php` as a simple PHP receiver and verify it live
      with `php -S` plus loopback `curl`, including creation of a UUID-named report file.
- [x] (2026-04-18 20:58Z) Add `make test_puzzle_progress_report_server` to start a temporary PHP server, run the
      server-dependent shell test, and tear the server down automatically.

## Surprises & Discoveries

- Observation: The current puzzle feature lives entirely in `src/window.c`. Puzzle mode loads random SGF files,
  compares user moves against the SGF main line, and has no separate puzzle-session object today.
  Evidence: `src/window.c` contains `gcheckers_window_enter_puzzle_mode_with_path()`,
  `gcheckers_window_apply_player_move()`, `gcheckers_window_play_next_puzzle_step_if_needed()`,
  `gcheckers_window_on_puzzle_next_clicked()`, and `gcheckers_window_on_puzzle_analyze_clicked()`.

- Observation: The repository already links `gio-2.0` and `libcurl` in `Makefile`, but there is no existing generic
  JSON helper or server-reporting module.
  Evidence: `Makefile` defines `GIO_LIBS`, `CURL_LIBS`, and uses them in `LDLIBS`; searches for `SoupSession`,
  `json-glib`, and puzzle reporting code returned no runtime module.

- Observation: `src/app_paths.c` currently resolves read-oriented data directories such as `puzzles`, but there is no
  helper for writable application state.
  Evidence: `gcheckers_app_paths_find_data_subdir()` checks `g_get_user_data_dir()`, system data dirs, and a local
  subdirectory, and always returns a directory path for a named subdirectory.

- Observation: The current `Analyze` button leaves puzzle mode and starts review analysis immediately, so if it is to
  become a distinct recorded outcome the lifecycle decision must happen before `gcheckers_window_leave_puzzle_mode()`
  clears puzzle state.
  Evidence: `src/window.c:gcheckers_window_on_puzzle_analyze_clicked()` calls `gcheckers_window_leave_puzzle_mode()`
  before rewinding and starting full-game analysis.

- Observation: The repository’s standard displayless test command still skips all GTK-heavy window cases, including the
  new puzzle persistence assertions, on this machine.
  Evidence: `env -u DISPLAY -u WAYLAND_DISPLAY -u GNOME_SETUP_DISPLAY make test` reported `ok ... # SKIP GTK display
  not available.` for all `test_window` puzzle cases after compiling them successfully.

- Observation: PHP 8.3 is available locally, so the fake reporting server can be tested directly with `php -S`
  without adding a separate runtime dependency or external service.
  Evidence: `php -v` reported `PHP 8.3.6`, and `php -S 127.0.0.1:18081 tests/puzzle_progress_report_server.php`
  accepted a sample `/puzzle-report` POST that wrote
  `tests/puzzle-progress-report-data/3c98f7b2-1111-4111-8111-123456789abc.jsonl`.

- Observation: A dedicated Makefile target can own the entire temporary PHP server lifecycle without needing a helper
  process manager.
  Evidence: `make test_puzzle_progress_report_server` successfully copied the shell test, started a temporary PHP
  server on an available loopback port, ran the POST/verification script, and exited cleanly with `Puzzle progress PHP
  server test passed.`.

## Decision Log

- Decision: Store puzzle attempt history outside `GSettings` and outside SGF files, in a dedicated writable
  application-state directory under the user's data directory, but store the stable user identifier in `GSettings` by
  default.
  Rationale: Attempt history is unbounded and record-oriented, so it fits a file better than a settings key. The user
  identifier is a single small stable string, which `GSettings` handles well. If `GSettings` is unavailable in a given
  test or packaging environment, the implementation may fall back to a state file, but the plan should treat
  `GSettings` as the preferred first implementation for the UUID.
  Date/Author: 2026-04-17 / Codex

- Decision: Treat "attempt identity" as one record per puzzle entry, not one record per puzzle file forever.
  Rationale: The user asked for each puzzle that was tried to be recorded as success or failure. If a user retries the
  same puzzle later, that is new behavior and should be a new attempt record rather than overwriting history.
  Date/Author: 2026-04-17 / Codex

- Decision: Distinguish three terminal outcomes for a started puzzle attempt: `success`, `failure`, and `analyze`.
  Store the first wrong move only when the first attempted move was wrong.
  Rationale: `Analyze` is a meaningful user action distinct from simply failing or abandoning the puzzle, and it may be
  useful later for puzzle calibration and UX decisions. The first-wrong-move detail is still only meaningful for a
  first-move failure.
  Date/Author: 2026-04-17 / Codex

- Decision: Generate a local user identifier once and persist it as a random UUID string, with `GSettings` as the
  preferred storage backend and a plain file as fallback if needed.
  Rationale: A stable locally generated identifier avoids duplicate reporting across restarts without requiring login or
  device-specific invasive identifiers such as machine IDs. `GSettings` is not overkill for one stable string; it is a
  reasonable place for the identifier because it behaves more like an application setting than like unbounded event
  history.
  Date/Author: 2026-04-17 / Codex

- Decision: Keep an append-only local history of all attempt records and track upload metadata separately instead of
  deleting data after upload.
  Rationale: The reporting threshold is not final yet, and the same raw data may need to be re-summarized or resent
  later. Keeping all raw attempt records locally also supports later puzzle-difficulty calibration. The server will
  deduplicate whole-report resends.
  Date/Author: 2026-04-17 / Codex

- Decision: The first reporting policy will send the whole local history when either there are at least 10 unsent
  attempts, or the oldest unsent attempt is more than 24 hours old and there are at least 5 unsent attempts.
  Rationale: This gives a concrete low-risk starting point while preserving flexibility. It avoids sending too often
  for light puzzle usage but still ensures stale local data is not left unsent indefinitely.
  Date/Author: 2026-04-17 / Codex

- Decision: Treat the first terminal puzzle outcome as authoritative for a puzzle entry even if the current UI still
  lets the user keep clicking inside the same puzzle after a wrong move.
  Rationale: Puzzle mode behavior was intentionally left unchanged, but the reporting model needs one stable terminal
  result per entry. Recording the first resolved outcome preserves the user’s actual first success/failure/analyze
  performance without turning later retries inside the same entry into history rewrites.
  Date/Author: 2026-04-18 / Codex

- Decision: Keep the upload worker in `src/application.c` and limit `src/puzzle_progress.c` to storage, thresholding,
  and payload preparation.
  Rationale: The application already owns process-wide lifecycle and startup/shutdown hooks. Keeping HTTP orchestration
  there avoids turning the storage module into a mixed persistence/network layer while still leaving the domain logic in
  one reusable module.
  Date/Author: 2026-04-18 / Codex

## Outcomes & Retrospective

Implementation is complete. Puzzle attempts now persist to JSONL under the user data directory once the player makes a
move, terminal outcomes are stored exactly once per puzzle entry, the application owns a shared background uploader
with threshold-based flush requests, and the new `test_puzzle_progress` suite covers the
persistence/payload/threshold logic directly. `make all` and `env -u DISPLAY -u WAYLAND_DISPLAY -u
GNOME_SETUP_DISPLAY make test` both succeeded in this workspace.

The main remaining limitation is environmental rather than code-level: the repository’s standard headless GTK run on
this machine compiles the new `test_window` puzzle cases but skips executing them because no display backend is
available. That is now documented in `Surprises & Discoveries`, while the storage-layer tests remain mandatory and
passing. A simple PHP fake receiver now also exists under `tests/puzzle_progress_report_server.php`, a reusable
production endpoint lives under `tools/puzzle_progress_report_server.php`, and
`make test_puzzle_progress_report_server` now exercises the temporary-server path end to end.

## Context and Orientation

The current application entry point is `src/application.c`. `GCheckersApplication` is a `GtkApplication` that creates
one `GCheckersWindow` in `gcheckers_application_activate()`. It currently owns no custom runtime state beyond the
window itself.

Puzzle play is implemented in `src/window.c` with a small entry dialog in `src/puzzle_dialog.c`. The important pieces
are:

- `gcheckers_window_present_puzzle_dialog()`, which opens the modal variant chooser.
- `gcheckers_window_start_random_puzzle_mode_for_ruleset()`, which chooses a random `puzzle-*.sgf` file from
  `puzzles/<ruleset-short-name>/` under the puzzle data root.
- `gcheckers_window_enter_puzzle_mode_with_path()`, which loads the SGF, extracts the main-line puzzle steps, and
  enters puzzle mode.
- `gcheckers_window_apply_player_move()`, which compares the user's move against the expected puzzle step. This is the
  current success/failure decision point for the first attempted move.
- `gcheckers_window_play_next_puzzle_step_if_needed()`, which auto-plays defense moves and marks the puzzle finished
  when the main line is complete.
- `gcheckers_window_on_puzzle_next_clicked()` and `gcheckers_window_on_puzzle_analyze_clicked()`, which are the two
  current exits from an active puzzle attempt.
- `gcheckers_window_leave_puzzle_mode()`, which tears puzzle mode down and clears puzzle-only runtime fields.

The current code does not have a persistent writable app-state helper. `src/app_paths.c` only resolves read-oriented
data subdirectories and is used today for things like the installed `puzzles/<ruleset-short-name>` directories. The
feature in this plan therefore needs either new writable-path helpers in `src/app_paths.c` or a new small helper
module that creates and returns a state directory such as:

    <user-data-dir>/gcheckers/puzzle-progress

For example, on a typical Linux desktop this is usually:

    ~/.local/share/gcheckers/puzzle-progress

The build is managed by `Makefile`. It already links `gio-2.0` and `libcurl`, so no new transport dependency is
strictly required. This plan deliberately avoids introducing `json-glib`; payload JSON can be built with `GString`
because the schema is small and fixed.

When this plan says "attempt history", it means a local on-disk list of all recorded puzzle attempts, whether sent or
not. When it says "unsent attempts", it means records that are present in the local history but have not yet been
included in any attempted upload batch according to local metadata. The server may receive duplicates because the whole
history can be resent; that is intentional.

## Plan of Work

The work begins by creating a dedicated puzzle progress module instead of burying persistence and HTTP code directly in
`src/window.c`. Add `src/puzzle_progress.h` and `src/puzzle_progress.c`. This module is responsible for four things
only: providing a stable user identifier, storing puzzle-attempt records durably on disk, tracking local upload state,
and preparing upload batches. It must not know GTK widgets or SGF controller details.

Define a small plain C data model in `src/puzzle_progress.h`. At minimum, it should include:

    typedef enum {
      CHECKERS_PUZZLE_ATTEMPT_RESULT_UNRESOLVED = 0,
      CHECKERS_PUZZLE_ATTEMPT_RESULT_SUCCESS,
      CHECKERS_PUZZLE_ATTEMPT_RESULT_FAILURE,
      CHECKERS_PUZZLE_ATTEMPT_RESULT_ANALYZE,
    } CheckersPuzzleAttemptResult;

    typedef struct {
      char *attempt_id;
      char *puzzle_id;
      guint puzzle_number;
      char *puzzle_source_name;
      CheckersColor attacker;
      gint64 started_unix_ms;
      gint64 finished_unix_ms;
      CheckersPuzzleAttemptResult result;
      gboolean failure_on_first_move;
      gboolean has_failed_first_move;
      CheckersMove failed_first_move;
      gint64 first_reported_unix_ms;
      guint report_count;
    } CheckersPuzzleAttemptRecord;

    typedef struct _CheckersPuzzleProgressStore CheckersPuzzleProgressStore;

The `attempt_id` is a per-attempt unique identifier generated locally. It is separate from the stable user identifier.
This matters because the same puzzle may be tried several times and because later calibration work may need
per-attempt, not just per-puzzle, history. The `puzzle_id` should be stable across packaged files and should not
depend on the random choice order. Use a ruleset-aware identifier derived from the selected variant plus the loaded
puzzle file basename, for example `international/puzzle-0007.sgf`. That matches the packaged layout under
`puzzles/<short-name>/` and avoids collisions between variants that may eventually reuse the same file numbers. Keep
`puzzle_number` as an optional numeric convenience because `window.c` already parses it. The `first_reported_unix_ms`
and `report_count` fields are local metadata, not gameplay results; they exist so the client can later reason about
what has or has not been sent without deleting the raw attempt record.

Persist the stable user identifier in `GSettings` under the existing application schema if possible. Add a key such as:

    puzzle-user-id

Implement a function similar to:

    char *checkers_puzzle_progress_store_get_or_create_user_id(CheckersPuzzleProgressStore *store, GError **error);

If the settings key exists and contains a non-empty value, reuse it. If it does not exist, generate a UUID with GLib,
store it, and return it. Do not use machine IDs, hostnames, MAC addresses, or anything derived from the user's system
identity. If schema constraints or test environments make this impractical in a specific context, a fallback file in
the state directory is acceptable, but the first implementation should prefer `GSettings` and explain any fallback in
`doc/OVERVIEW.md` (and avoid it if possible).

Persist attempt history in a file such as:

    <state-dir>/attempt-history.jsonl

Use one JSON object per line. "JSONL" means JSON Lines: each line is a complete JSON object. This format is easy to
append, inspect manually, and recover from if the last line is partially written. Each stored object must include the
attempt record and a small schema version. For example:

    {"schema_version":1,"attempt_id":"...","puzzle_id":"international/puzzle-0007.sgf","puzzle_number":7,
     "puzzle_ruleset":"international","attacker":"white","started_unix_ms":1713300000000,"finished_unix_ms":1713300005000,
     "result":"failure","failure_on_first_move":true,"first_reported_unix_ms":0,"report_count":0,
     "failed_first_move":{"length":2,"captures":0,"path":[12,16]}}

Do not overwrite existing history silently. The store API should support appending a new unresolved attempt record when
the user first makes a move attempt, then rewriting that same record to resolved state when the outcome becomes known.
Because JSONL is append-friendly but not in-place editable, implement the history store as full-file load plus atomic
rewrite. That is acceptable here because the expected history size is small and correctness matters more than
micro-optimization.

The store API should expose operations shaped roughly like this:

    CheckersPuzzleProgressStore *checkers_puzzle_progress_store_new(const char *state_dir);
    gboolean checkers_puzzle_progress_store_append_attempt(CheckersPuzzleProgressStore *store,
                                                           const CheckersPuzzleAttemptRecord *record,
                                                           GError **error);
    gboolean checkers_puzzle_progress_store_replace_attempt(CheckersPuzzleProgressStore *store,
                                                            const CheckersPuzzleAttemptRecord *record,
                                                            GError **error);
    GPtrArray *checkers_puzzle_progress_store_load_attempt_history(CheckersPuzzleProgressStore *store,
                                                                   GError **error);
    GPtrArray *checkers_puzzle_progress_store_collect_unsent_attempts(CheckersPuzzleProgressStore *store,
                                                                      GError **error);
    gboolean checkers_puzzle_progress_store_mark_reported(CheckersPuzzleProgressStore *store,
                                                          gint64 reported_unix_ms,
                                                          GError **error);

The next edit is in `src/window.c`. Add a small runtime field representing the currently active local attempt. This
must be separate from `puzzle_steps`. Extend `struct _GCheckersWindow` with:

    CheckersPuzzleProgressStore *puzzle_progress_store;
    gboolean puzzle_attempt_started;
    CheckersPuzzleAttemptRecord puzzle_attempt;
    char *puzzle_path;
    PlayerRuleset puzzle_ruleset;

If ownership becomes awkward, replace the inline record with a pointer and helper init/reset functions. The important
behavior is:

1. Entering puzzle mode does not immediately create an attempt record.
2. The first time the user actually tries a move in puzzle mode, create a new attempt with a fresh `attempt_id`,
   current timestamps, puzzle identity fields, unresolved result, and no failed move yet, then persist it
   immediately.
3. If the first move is wrong, mark the attempt as failure, set `failure_on_first_move = TRUE`, copy the wrong move
   into `failed_first_move`, set `finished_unix_ms`, replace the stored record, and keep puzzle mode behavior exactly
   as it is today.
4. If the user eventually completes the puzzle line, mark the attempt as success, set `finished_unix_ms`, replace the
   stored record, and leave `has_failed_first_move = FALSE`.
5. If the user makes at least one correct move, then later clicks `Analyze`, mark the attempt as `analyze`, set
   `finished_unix_ms`, and keep `failure_on_first_move = FALSE`.
6. If the user makes at least one correct move, then later leaves puzzle mode without finishing for another reason,
   mark the attempt as failure when puzzle mode is abandoned by `Next puzzle`, starting another puzzle through the
   variant chooser, new game, import, or window destruction. In that case `failure_on_first_move = FALSE` and there is
   no stored first wrong move.

This is the key lifecycle rule that the implementation must make explicit: only puzzle entries with at least one user
move attempt are recorded, but once recorded they must always end in either success, failure, or analyze before the
runtime state is discarded.

Add a small helper in `src/window.c` for each lifecycle transition rather than spreading history writes across many
branches. Use helpers with names like:

    static gboolean gcheckers_window_puzzle_attempt_ensure_started(GCheckersWindow *self,
                                                                   const CheckersMove *first_move_attempt);
    static gboolean gcheckers_window_puzzle_attempt_finish_success(GCheckersWindow *self);
    static gboolean gcheckers_window_puzzle_attempt_finish_failure(GCheckersWindow *self,
                                                                   gboolean failure_on_first_move,
                                                                   const CheckersMove *failed_first_move);
    static gboolean gcheckers_window_puzzle_attempt_finish_analyze(GCheckersWindow *self);
    static void gcheckers_window_puzzle_attempt_reset(GCheckersWindow *self);

Call them from:

- `gcheckers_window_apply_player_move()` for first-move start and wrong-move resolution.
- `gcheckers_window_play_next_puzzle_step_if_needed()` when `self->puzzle_finished` becomes true.
- `gcheckers_window_on_puzzle_analyze_clicked()` before leaving puzzle mode.
- `gcheckers_window_leave_puzzle_mode()` before clearing puzzle fields when the exit is not already classified as
  `analyze`.
- `gcheckers_window_enter_puzzle_mode_with_path()` when replacing one active puzzle with another after
  `gcheckers_window_start_random_puzzle_mode_for_ruleset()`.
- `gcheckers_window_dispose()` or `gcheckers_window_finalize()` as a final safety net.

The next edit is in `src/application.c` and `src/application.h`. The upload loop belongs at the application level,
because uploads are app-wide background work rather than window-local widget behavior. Extend
`struct _GCheckersApplication` to hold:

- one `CheckersPuzzleProgressStore *`
- one uploader object or runtime struct
- a timer source ID if periodic retries are used

Create and own the store in `gcheckers_application_startup()`. Expose an accessor in `src/application.h` so
`src/window.c` can retrieve the store from `gtk_widget_get_application()` safely, for example:

    CheckersPuzzleProgressStore *gcheckers_application_get_puzzle_progress_store(GCheckersApplication *self);

Also add application shutdown handling. Override `GApplicationClass.shutdown` if needed so the app gets one final
threshold check and optional best-effort flush before exit. Do not block quit indefinitely; a bounded best-effort
attempt is enough.

Implement upload support either inside `src/puzzle_progress.c` or in a small companion module
`src/puzzle_progress_upload.c`. Keep the boundary crisp: storage code should not know curl easy handles if that starts
to bloat the file. The uploader must:

- load full attempt history
- compute the currently unsent subset
- fetch the stable user ID
- decide whether the local threshold says "send now"
- build a full-history JSON document
- `POST` it to a configured URL
- record that a send happened successfully so current unsent records become "reported"

For the first implementation, "unsent" means `report_count == 0`. The decision to send should be:

- send when there are at least 10 unsent attempts, or
- send when the oldest unsent attempt is older than 24 hours and there are at least 5 unsent attempts

When a send happens, the whole local history should be posted, not just the unsent subset. After a successful send, do
not delete history. Instead, update local metadata on every record that had `report_count == 0` at send time:

- if `first_reported_unix_ms == 0`, set it to the current send timestamp
- increment `report_count`

Already reported records should stay in the history file unchanged unless the implementation decides to also maintain a
`last_reported_unix_ms` field later. That extra field is not required for the first version.

Use an environment variable for the endpoint during the first implementation pass:

    GCHECKERS_PUZZLE_REPORT_URL

If it is unset or empty, uploads are disabled but local recording still works. That keeps the feature safe for normal
development and test runs.

Assume this request shape unless product requirements change during implementation:

Request:

    {
      "schema_version": 1,
      "user_id": "3c98f7b2-...",
      "client": {
        "app_id": "io.github.jeromea.gcheckers",
        "app_version": "dev"
      },
      "attempts": [
        {
          "attempt_id": "8db8...",
          "puzzle_id": "international/puzzle-0007.sgf",
          "puzzle_number": 7,
          "puzzle_source_name": "puzzle-0007.sgf",
          "puzzle_ruleset": "international",
          "attacker": "white",
          "started_unix_ms": 1713300000000,
          "finished_unix_ms": 1713300005000,
          "result": "failure",
          "failure_on_first_move": true,
          "report_count": 0,
          "failed_first_move": {
            "length": 2,
            "captures": 0,
            "path": [12, 16]
          }
        }
      ]
    }

The response only needs to distinguish success from failure for the first version. The client does not need
per-attempt acknowledgment because it does not delete history. A simple HTTP success status is enough to mark the
current unsent set as reported in local metadata. The server handles deduplication across full-history resends.

Do not perform uploads on the GTK main thread synchronously. Use a worker thread around libcurl and marshal completion
back to the main loop with `g_main_context_invoke()` or an idle source. The UI requirement is simple: puzzle play must
not stall because the network is slow or down.

Start with two upload trigger points:

- an immediate opportunistic flush shortly after application startup
- a flush request each time a puzzle attempt becomes resolved

Optionally add a low-frequency retry timer such as every five minutes while the app is open. If added, keep it simple
and ensure only one upload job runs at a time. The uploader should skip starting a new job when one is already active.

Tests must be split by concern so the feature stays fast. Add a new focused test binary such as
`tests/test_puzzle_progress.c`. It should cover:

- creating the state directory and generating the stable user ID once
- reloading the same user ID on the next store open
- appending an unresolved attempt and replacing it with success
- replacing an attempt with first-move failure and preserving move encoding
- replacing an attempt with `analyze`
- collecting the unsent subset correctly from mixed reported and unreported history
- threshold decisions for `10 unsent` and `24h old + at least 5 unsent`
- marking a successful send as reported without deleting history
- payload generation producing the expected JSON for success, failure, and analyze records
- handling an empty or partially reported history without crashing

Do not force network access in unit tests. For upload behavior, add a tiny fake HTTP server script under `tests/` or
drive libcurl against a local loopback server launched from a shell test. If that becomes too heavy, unit-test payload
building and response parsing separately in C and keep the actual libcurl integration minimal.

Extend `tests/test_window.c` with puzzle integration coverage. Reuse the existing temporary puzzle helper functions and
the ruleset-aware chooser flow.
Add tests for:

- solving a puzzle records a resolved success attempt in local history and triggers a flush request stub
- a wrong first move records a failure with the expected first move path
- starting a puzzle, making one correct move, then hitting `Analyze` records an `analyze` result without a stored
  failed first move
- leaving puzzle mode without ever making a move records nothing

For these window tests, avoid real HTTP. Provide the window with a real local store but a stub uploader callback or a
store-only application configuration so the test can assert on-disk history contents deterministically.

Update `src/app_paths.h` and `src/app_paths.c` if necessary to add a writable helper such as:

    char *gcheckers_app_paths_get_user_state_subdir(const char *subdir_name, GError **error);

This helper should create the directory if it does not exist. Keeping writable-path creation in one place is better
than open-coding `g_mkdir_with_parents()` in multiple modules.

Update `Makefile` to compile the new source files into the app, test binaries, and any helper tests. If a dedicated
`test_puzzle_progress` binary is added, include it in the `test` target. Keep the test list focused; do not add a slow
integration test that depends on the public internet.

Finally, update `doc/OVERVIEW.md`. Add a section for puzzle progress reporting that explains where the history lives,
how window puzzle mode feeds it, how the application decides when to send a full report, and that the resulting data is
intended to support later puzzle-difficulty calibration in addition to reporting.

## Concrete Steps

All commands below assume the working directory is the repository root:

    cd /home/jerome/Data/gcheckers

Create the new module and tests, then wire them into the build:

    make test_puzzle_progress

Expected first successful run:

    cc ... -o build/tests/test_puzzle_progress tests/test_puzzle_progress.c ...
    ./build/tests/test_puzzle_progress
    ok 1 /puzzle-progress/user-id-persists
    ok 2 /puzzle-progress/append-and-replace-success
    ok 3 /puzzle-progress/first-move-failure-persists
    ok 4 /puzzle-progress/analyze-persists
    ok ...

Run the focused window test coverage for puzzle mode:

    make test_window
    env -u DISPLAY -u WAYLAND_DISPLAY -u GNOME_SETUP_DISPLAY ./build/tests/test_window -p /gcheckers-window/puzzle-mode

If the displayless path skips GTK-heavy puzzle tests on this machine, run only the non-skipped focused cases that cover
the new storage helpers, and record that in the `Surprises & Discoveries` section while keeping the store tests
mandatory.

Build all binaries before considering the feature complete:

    make all

Run the full test suite in the environment that is already used for reliable local runs in this repository:

    env -u DISPLAY -u WAYLAND_DISPLAY -u GNOME_SETUP_DISPLAY make test

Manual end-to-end exercise with local-only persistence:

    rm -rf /tmp/gcheckers-puzzle-progress
    mkdir -p /tmp/gcheckers-puzzle-progress
    GCHECKERS_PUZZLES_DIR=/path/to/test-puzzles \
    GCHECKERS_PUZZLE_PROGRESS_DIR=/tmp/gcheckers-puzzle-progress \
    ./build/bin/gcheckers

Expected observation:

- after entering puzzle mode and leaving without moving, no history file is created
- after a wrong first move, the history file contains one failure record with `failure_on_first_move: true`
- after solving a puzzle, the stored record is marked `success` and its `puzzle_id` includes the selected ruleset
- after clicking `Analyze` during a started puzzle, the stored record is marked `analyze`

Manual end-to-end exercise with a fake upload server:

    GCHECKERS_PUZZLE_REPORT_URL=http://127.0.0.1:18080/puzzle-report \
    GCHECKERS_PUZZLE_PROGRESS_DIR=/tmp/gcheckers-puzzle-progress \
    ./build/bin/gcheckers

Expected observation:

- once the threshold rule is satisfied, the full local history is posted as JSON
- successful upload marks previously unsent attempts as reported in local metadata
- local history remains on disk after upload
- restarting the app does not resend immediately unless the threshold rule is satisfied again for newly unsent data

## Validation and Acceptance

The feature is complete only when all of the following are true.

Running `make all` from the repository root succeeds. Running `env -u DISPLAY -u WAYLAND_DISPLAY -u GNOME_SETUP_DISPLAY
make test` succeeds, or any pre-existing unrelated GTK/display failures are explicitly documented in this ExecPlan with
the new puzzle progress tests still passing.

The focused store tests prove that:

- the same user ID is reused across restarts
- unresolved attempts can be appended and later replaced
- success, failure, and analyze results serialize and deserialize correctly
- a first-move failure stores the attempted move path
- threshold decisions are correct for unsent-count and age rules
- a successful send marks attempts as reported without deleting history

The focused puzzle-window tests prove that:

- solving a puzzle creates exactly one resolved success record
- an incorrect first move creates exactly one resolved failure record with the first move encoded
- clicking `Analyze` on a started puzzle records an `analyze` result without inventing a first wrong move
- abandoning a started puzzle for another reason records a failure without inventing a first wrong move
- visiting a puzzle without making any move records nothing

The manual upload test proves that:

- the client sends the stable `user_id` and the attempt batch to the configured server URL
- successful upload updates local report metadata but keeps local history
- restarting the application does not duplicate reporting decisions until enough new unsent data exists again
- failed uploads leave the history intact and the next flush retries it

## Idempotence and Recovery

This plan is intentionally additive and repeatable. Re-running the tests is safe. Rebuilding the history file by
appending and replacing records is safe because the implementation must rewrite the file atomically. If the app crashes
mid-write, the recovery behavior is to keep the previous intact history file and retry on next launch.

If a malformed history file is discovered during development, the store loader should fail with a `GError` and keep the
original file untouched. Do not silently drop lines. For manual recovery during development, move the bad file aside
instead of deleting it:

    mv ~/.local/share/gcheckers/puzzle-progress/attempt-history.jsonl \
       ~/.local/share/gcheckers/puzzle-progress/attempt-history.jsonl.bad

If server upload is unavailable, local recording still works and the history remains pending for the next threshold
check. That degraded mode is part of the design, not a failure.

## Artifacts and Notes

Important paths the implementation should use or add:

    src/puzzle_progress.h
    src/puzzle_progress.c
    tests/test_puzzle_progress.c
    src/application.c
    src/application.h
    src/window.c
    src/app_paths.c
    src/app_paths.h
    doc/OVERVIEW.md
    Makefile

Suggested environment variables:

    GCHECKERS_PUZZLE_PROGRESS_DIR
    GCHECKERS_PUZZLE_REPORT_URL

Suggested on-disk layout:

    ~/.local/share/gcheckers/puzzle-progress/attempt-history.jsonl

Suggested settings key:

    data/schemas/io.github.jeromea.gcheckers.gschema.xml -> puzzle-user-id

## Interfaces and Dependencies

In `src/puzzle_progress.h`, define a stable storage and upload interface that `src/window.c` and `src/application.c`
can share:

    typedef struct _CheckersPuzzleProgressStore CheckersPuzzleProgressStore;

    CheckersPuzzleProgressStore *checkers_puzzle_progress_store_new(const char *state_dir);
    void checkers_puzzle_progress_store_free(CheckersPuzzleProgressStore *store);

    char *checkers_puzzle_progress_store_get_or_create_user_id(CheckersPuzzleProgressStore *store,
                                                               GError **error);

    gboolean checkers_puzzle_progress_store_append_attempt(CheckersPuzzleProgressStore *store,
                                                           const CheckersPuzzleAttemptRecord *record,
                                                           GError **error);
    gboolean checkers_puzzle_progress_store_replace_attempt(CheckersPuzzleProgressStore *store,
                                                            const CheckersPuzzleAttemptRecord *record,
                                                            GError **error);
    GPtrArray *checkers_puzzle_progress_store_load_attempt_history(CheckersPuzzleProgressStore *store,
                                                                   GError **error);
    GPtrArray *checkers_puzzle_progress_store_collect_unsent_attempts(CheckersPuzzleProgressStore *store,
                                                                      GError **error);
    gboolean checkers_puzzle_progress_store_mark_reported(CheckersPuzzleProgressStore *store,
                                                          gint64 reported_unix_ms,
                                                          GError **error);

    char *checkers_puzzle_progress_build_upload_json(const char *user_id,
                                                     const GPtrArray *attempts);
    gboolean checkers_puzzle_progress_should_send_report(const GPtrArray *attempt_history,
                                                         gint64 now_unix_ms);

If upload logic needs its own object, define it in either `src/puzzle_progress.h` or a companion header:

    typedef struct _CheckersPuzzleProgressUploader CheckersPuzzleProgressUploader;

    CheckersPuzzleProgressUploader *checkers_puzzle_progress_uploader_new(CheckersPuzzleProgressStore *store,
                                                                          const char *report_url);
    void checkers_puzzle_progress_uploader_request_flush(CheckersPuzzleProgressUploader *uploader);
    void checkers_puzzle_progress_uploader_free(CheckersPuzzleProgressUploader *uploader);

Keep dependencies limited to GLib/GIO/libcurl already present in `Makefile`. Do not introduce public internet test
dependencies. If response parsing becomes cumbersome without a JSON parser, a narrowly scoped internal parser for the
upload-success response is acceptable for the first version, but the code must reject malformed input cleanly with
`GError`.

Change note: created this plan after surveying the existing puzzle runtime in `src/window.c`, the application lifecycle
in `src/application.c`, writable-path gaps in `src/app_paths.c`, and the current `Makefile`/test setup. It was later
reworked to treat `Analyze` as its own outcome, to prefer `GSettings` for the stable user ID, and to switch from
delete-on-ack queueing to append-only history with threshold-based full-report sends because those better match the
current product direction. It was also updated after puzzle mode became ruleset-aware so the plan now refers to the
variant chooser flow and to stable puzzle IDs derived from `puzzles/<short-name>/puzzle-####.sgf`. This revision also
records the completed implementation, validation results, and the headless GTK skip behavior observed on this machine.
