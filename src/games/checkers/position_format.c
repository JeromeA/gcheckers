#include "position_format.h"

#include <glib.h>

char *checkers_position_format_line(const CheckersMove *line, guint line_length) {
  g_return_val_if_fail(line != NULL || line_length == 0, NULL);

  GString *formatted = g_string_new(NULL);
  for (guint i = 0; i < line_length; ++i) {
    if ((i % 2) == 0) {
      g_string_append_printf(formatted, "%u. ", (i / 2) + 1);
    }

    char move_notation[128];
    if (!game_format_move_notation(&line[i], move_notation, sizeof(move_notation))) {
      g_debug("Failed to format move in line formatter");
      g_string_append(formatted, "?");
    } else {
      g_string_append(formatted, move_notation);
    }

    if (i + 1 < line_length) {
      g_string_append_c(formatted, ' ');
    }
  }

  return g_string_free(formatted, FALSE);
}
