#ifndef GGAME_CREATE_PUZZLES_PROGRESS_H
#define GGAME_CREATE_PUZZLES_PROGRESS_H

#include <glib.h>

G_BEGIN_DECLS

void ggame_create_puzzles_progress_log(const char *format, ...) G_GNUC_PRINTF(1, 2);
void ggame_create_puzzles_progress_start_self_play(guint depth);
void ggame_create_puzzles_progress_finish_self_play(guint moves, const char *outcome_label);
void ggame_create_puzzles_progress_consider_move(guint move_number, const char *move_text);

G_END_DECLS

#endif
