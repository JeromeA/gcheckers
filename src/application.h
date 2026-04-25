#ifndef GGAME_APPLICATION_H
#define GGAME_APPLICATION_H

#include "puzzle_progress.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GGAME_TYPE_APPLICATION (ggame_application_get_type())

G_DECLARE_FINAL_TYPE(GGameApplication, ggame_application, GGAME, APPLICATION, GtkApplication)

GGameApplication *ggame_application_new(void);
GGamePuzzleProgressStore *ggame_application_get_puzzle_progress_store(GGameApplication *self);
void ggame_application_request_puzzle_progress_flush(GGameApplication *self);

G_END_DECLS

#endif
