A checkers program for playing, studying, and replaying games.

Solve tactical puzzles, analyze positions with move-by-move scores, and browse SGF variations without leaving the same
desktop app.

| Puzzle analysis | Play against the computer | Variation review |
| --- | --- | --- |
| ![Analysis view](doc/Analysis.png) | ![Play against the computer](doc/Play_against_computer.png) | ![Variation browser](doc/Variations.png) |

`gcheckers` focuses on the parts that matter once a plain board is not enough anymore:

  - puzzle solving and verification
  - engine analysis for the current position or a full game
  - SGF review with branching variations
  - normal play against another human or the computer

Main features:

  - hot-seat play for two human players
  - play against the computer
  - computer analysis of the current position or a full recorded game
  - SGF variation exploration and navigation

Build dependencies:

  - pkg-config
  - glib-2.0 development headers
  - gobject-2.0 development headers
  - GTK 4 development headers
  - libcurl development headers
  - glib-compile-schemas

Build the project:

  make

Run the GTK application:

  ./gcheckers
