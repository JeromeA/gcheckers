#include "create_puzzles_progress.h"

#include <stdarg.h>

void ggame_create_puzzles_progress_log(const char *format, ...) {
  g_return_if_fail(format != NULL);

  va_list args;
  va_start(args, format);
  g_autofree char *message = g_strdup_vprintf(format, args);
  va_end(args);

  g_print("%s\n", message);
}

void ggame_create_puzzles_progress_start_self_play(guint depth) {
  ggame_create_puzzles_progress_log("Playing game at depth %u...", depth);
}

void ggame_create_puzzles_progress_finish_self_play(guint moves, const char *outcome_label) {
  ggame_create_puzzles_progress_log("Played game ended after %u moves (winner=%s)",
                                    moves,
                                    outcome_label != NULL ? outcome_label : "unknown");
}

void ggame_create_puzzles_progress_consider_move(guint move_number, const char *move_text) {
  g_return_if_fail(move_number > 0);

  gboolean has_move_text = move_text != NULL && move_text[0] != '\0';
  ggame_create_puzzles_progress_log("Considering move #%u%s%s",
                                    move_number,
                                    has_move_text ? " " : "",
                                    has_move_text ? move_text : "");
}
