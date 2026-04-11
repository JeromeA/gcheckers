A checkers program for playing, solving puzzles, and replaying games.

Play against the computer or another human, solve tactical puzzles, analyze full games with move-by-move scores, and
browse SGF variations without leaving the same desktop app.

| Play against the computer | Puzzle mode | Full-game analysis | Variation review |
| --- | --- | --- | --- |
| ![Play against the computer](doc/Play_against_computer.png) | ![Puzzle mode](doc/Puzzle.png) | ![Analysis view](doc/Analysis.png) | ![Variation browser](doc/Variations.png) |

`gcheckers` focuses on the parts that matter once a plain board is not enough anymore:

  - normal play against another human or the computer
  - puzzle solving and verification
  - engine analysis for the current position or a full game
  - SGF review with branching variations

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
