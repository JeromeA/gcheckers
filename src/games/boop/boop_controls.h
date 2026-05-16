#ifndef GGAME_BOOP_CONTROLS_H
#define GGAME_BOOP_CONTROLS_H

#include <glib-object.h>

#include "../../game_app_profile.h"

typedef struct _GtkWidget GtkWidget;
typedef struct _BoardView BoardView;
typedef struct _GGameModel GGameModel;

G_BEGIN_DECLS

GtkWidget *gboop_controls_create_board_host(GGameModel *model,
                                            BoardView *board_view,
                                            GGameAppMoveHandler move_handler,
                                            gpointer move_handler_data);

G_END_DECLS

#endif
