#ifndef PLAYER_CONTROLS_PANEL_H
#define PLAYER_CONTROLS_PANEL_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PLAYER_TYPE_CONTROLS_PANEL (player_controls_panel_get_type())

G_DECLARE_FINAL_TYPE(PlayerControlsPanel,
                     player_controls_panel,
                     PLAYER,
                     CONTROLS_PANEL,
                     GtkBox)

PlayerControlsPanel *player_controls_panel_new(void);
void player_controls_panel_set_force_move_sensitive(PlayerControlsPanel *self, gboolean sensitive);

G_END_DECLS

#endif
