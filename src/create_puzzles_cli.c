#include "create_puzzles_cli.h"

#include <string.h>

static gboolean checkers_create_puzzles_cli_parse_depth(const char *arg, guint *out_depth) {
  g_return_val_if_fail(arg != NULL, FALSE);
  g_return_val_if_fail(out_depth != NULL, FALSE);

  gchar *end = NULL;
  guint64 value = g_ascii_strtoull(arg, &end, 10);
  if (end == arg || end == NULL || *end != '\0' || value == 0 || value > G_MAXUINT) {
    return FALSE;
  }

  *out_depth = (guint)value;
  return TRUE;
}

gboolean checkers_create_puzzles_cli_parse(int argc,
                                           char **argv,
                                           guint default_depth,
                                           CheckersCreatePuzzlesCliOptions *out_options,
                                           char **out_error_message) {
  g_return_val_if_fail(argc >= 0, FALSE);
  g_return_val_if_fail(argv != NULL, FALSE);
  g_return_val_if_fail(default_depth > 0, FALSE);
  g_return_val_if_fail(out_options != NULL, FALSE);

  if (out_error_message != NULL) {
    *out_error_message = NULL;
  }

  *out_options = (CheckersCreatePuzzlesCliOptions) {
      .mode = CHECKERS_CREATE_PUZZLES_MODE_GENERATE,
      .depth = default_depth,
      .try_forced_mistakes = FALSE,
      .save_games = FALSE,
      .dry_run = FALSE,
      .arg = NULL,
  };

  for (gint i = 1; i < argc; ++i) {
    if (g_strcmp0(argv[i], "--depth") == 0) {
      if (i + 1 >= argc || !checkers_create_puzzles_cli_parse_depth(argv[i + 1], &out_options->depth)) {
        if (out_error_message != NULL) {
          *out_error_message = g_strdup("Invalid --depth value");
        }
        return FALSE;
      }
      i++;
      continue;
    }

    if (g_strcmp0(argv[i], "--synthetic-candidates") == 0) {
      out_options->try_forced_mistakes = TRUE;
      continue;
    }

    if (g_strcmp0(argv[i], "--save-games") == 0) {
      out_options->save_games = TRUE;
      continue;
    }

    if (g_strcmp0(argv[i], "--check-existing") == 0) {
      out_options->mode = CHECKERS_CREATE_PUZZLES_MODE_CHECK_EXISTING;
      continue;
    }

    if (g_strcmp0(argv[i], "--dry-run") == 0) {
      out_options->dry_run = TRUE;
      continue;
    }

    if (argv[i][0] == '-') {
      if (out_error_message != NULL) {
        *out_error_message = g_strdup_printf("Unknown option: %s", argv[i]);
      }
      return FALSE;
    }

    if (out_options->arg != NULL) {
      if (out_error_message != NULL) {
        *out_error_message = g_strdup("Expected a single puzzle count or SGF file");
      }
      return FALSE;
    }

    out_options->arg = argv[i];
  }

  if (out_options->mode == CHECKERS_CREATE_PUZZLES_MODE_CHECK_EXISTING) {
    if (out_options->try_forced_mistakes) {
      if (out_error_message != NULL) {
        *out_error_message = g_strdup("--synthetic-candidates is only valid when generating puzzles");
      }
      return FALSE;
    }
    if (out_options->save_games) {
      if (out_error_message != NULL) {
        *out_error_message = g_strdup("--save-games is only valid when generating puzzles");
      }
      return FALSE;
    }
  } else if (out_options->dry_run) {
    if (out_error_message != NULL) {
      *out_error_message = g_strdup("--dry-run is only valid with --check-existing");
    }
    return FALSE;
  }

  if (out_options->arg == NULL) {
    if (out_options->mode == CHECKERS_CREATE_PUZZLES_MODE_CHECK_EXISTING) {
      return TRUE;
    }
    if (out_error_message != NULL) {
      *out_error_message = g_strdup("Missing puzzle count or SGF file");
    }
    return FALSE;
  }

  return TRUE;
}
