#ifndef GCHECKERS_APPLICATION_H
#define GCHECKERS_APPLICATION_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GCHECKERS_TYPE_APPLICATION (gcheckers_application_get_type())

G_DECLARE_FINAL_TYPE(GCheckersApplication, gcheckers_application, GCHECKERS, APPLICATION, GtkApplication)

GCheckersApplication *gcheckers_application_new(void);
void gcheckers_application_configure_automation(GCheckersApplication *self,
                                                guint exit_after_seconds,
                                                guint force_move_presses);

G_END_DECLS

#endif
