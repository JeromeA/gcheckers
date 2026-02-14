#ifndef GCHECKERS_SGF_CONTROLLER_H
#define GCHECKERS_SGF_CONTROLLER_H

#include "sgf_tree.h"
#include "sgf_view.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GCHECKERS_TYPE_SGF_CONTROLLER (gcheckers_sgf_controller_get_type())

G_DECLARE_FINAL_TYPE(GCheckersSgfController,
                     gcheckers_sgf_controller,
                     GCHECKERS,
                     SGF_CONTROLLER,
                     GObject)

GCheckersSgfController *gcheckers_sgf_controller_new(void);
const SgfNode *gcheckers_sgf_controller_append_synthetic_move(GCheckersSgfController *self);
GtkWidget *gcheckers_sgf_controller_get_widget(GCheckersSgfController *self);

G_END_DECLS

#endif
