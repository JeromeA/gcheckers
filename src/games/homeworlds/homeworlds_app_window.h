#ifndef GHOMEWORLDS_APP_WINDOW_H
#define GHOMEWORLDS_APP_WINDOW_H

#include <glib.h>

typedef struct _GtkApplication GtkApplication;
typedef struct _GtkWindow GtkWindow;

G_BEGIN_DECLS

GtkWindow *ghomeworlds_app_window_create(GtkApplication *app);

G_END_DECLS

#endif
