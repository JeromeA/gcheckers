#ifndef CHECKERS_CREATE_PUZZLES_CLI_H
#define CHECKERS_CREATE_PUZZLES_CLI_H

#include <glib.h>

typedef struct {
  guint depth;
  gboolean try_forced_mistakes;
  const char *arg;
} CheckersCreatePuzzlesCliOptions;

gboolean checkers_create_puzzles_cli_parse(int argc,
                                           char **argv,
                                           guint default_depth,
                                           CheckersCreatePuzzlesCliOptions *out_options,
                                           char **out_error_message);

#endif
