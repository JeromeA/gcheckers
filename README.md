A checkers program.

Dependencies:

  - glib-2.0 development headers (pkg-config is used to locate it)
  - GTK 4 Broadway daemon and Chromium for screenshot automation (run ./tools/setup.sh on Debian/Ubuntu)

Build the library and CLI:

  make

Run the text-mode game:

  ./checkers

Run the GTK app:

  ./gcheckers

Automation flags for the GTK app:

  - `--exit-after-seconds=N` exits automatically after N seconds.
  - `--auto-force-moves=N` presses Force move N times.

Run the inconsistency automation test:

  make test-inconsistency
