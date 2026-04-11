#ifndef CHECKERS_CREATE_PUZZLES_CLI_H
#define CHECKERS_CREATE_PUZZLES_CLI_H

#include <glib.h>

typedef enum {
  CHECKERS_CREATE_PUZZLES_MODE_GENERATE = 0,
  CHECKERS_CREATE_PUZZLES_MODE_CHECK_EXISTING,
} CheckersCreatePuzzlesCliMode;

typedef struct {
  CheckersCreatePuzzlesCliMode mode;
  guint depth;
  gboolean try_forced_mistakes;
  gboolean save_games;
  gboolean dry_run;
  const char *arg;
} CheckersCreatePuzzlesCliOptions;

gboolean checkers_create_puzzles_cli_parse(int argc,
                                           char **argv,
                                           guint default_depth,
                                           CheckersCreatePuzzlesCliOptions *out_options,
                                           char **out_error_message);

#endif
