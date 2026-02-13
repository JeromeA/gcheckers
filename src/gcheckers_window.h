#ifndef GCHECKERS_WINDOW_H
#define GCHECKERS_WINDOW_H

#include "checkers_model.h"
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GCHECKERS_TYPE_WINDOW (gcheckers_window_get_type())

G_DECLARE_FINAL_TYPE(GCheckersWindow, gcheckers_window, GCHECKERS, WINDOW, GtkApplicationWindow)

GCheckersWindow *gcheckers_window_new(GtkApplication *app, GCheckersModel *model);

G_END_DECLS

#endif
