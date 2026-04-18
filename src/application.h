#ifndef GCHECKERS_APPLICATION_H
#define GCHECKERS_APPLICATION_H

#include "puzzle_progress.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GCHECKERS_TYPE_APPLICATION (gcheckers_application_get_type())

G_DECLARE_FINAL_TYPE(GCheckersApplication, gcheckers_application, GCHECKERS, APPLICATION, GtkApplication)

GCheckersApplication *gcheckers_application_new(void);
CheckersPuzzleProgressStore *gcheckers_application_get_puzzle_progress_store(GCheckersApplication *self);
void gcheckers_application_request_puzzle_progress_flush(GCheckersApplication *self);

G_END_DECLS

#endif
