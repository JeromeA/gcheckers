#ifndef PLAYER_CONTROLS_PANEL_H
#define PLAYER_CONTROLS_PANEL_H

#include "game.h"
#include "ruleset.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PLAYER_TYPE_CONTROLS_PANEL (player_controls_panel_get_type())

G_DECLARE_FINAL_TYPE(PlayerControlsPanel,
                     player_controls_panel,
                     PLAYER,
                     CONTROLS_PANEL,
                     GtkBox)

typedef enum {
  PLAYER_CONTROL_MODE_USER = 0,
  PLAYER_CONTROL_MODE_COMPUTER = 1
} PlayerControlMode;

#define PLAYER_COMPUTER_DEPTH_MIN 0
#define PLAYER_COMPUTER_DEPTH_MAX 16
#define PLAYER_COMPUTER_DEPTH_DEFAULT 6

PlayerControlsPanel *player_controls_panel_new(void);
GtkDropDown *player_controls_panel_get_drop_down(PlayerControlsPanel *self, CheckersColor color);
void player_controls_panel_set_selected(PlayerControlsPanel *self, CheckersColor color, guint selected);
void player_controls_panel_set_mode(PlayerControlsPanel *self, CheckersColor color, PlayerControlMode mode);
void player_controls_panel_set_all_user(PlayerControlsPanel *self);
guint player_controls_panel_get_selected(PlayerControlsPanel *self, CheckersColor color);
PlayerControlMode player_controls_panel_get_mode(PlayerControlsPanel *self, CheckersColor color);
gboolean player_controls_panel_is_user_control(PlayerControlsPanel *self, CheckersColor color);
void player_controls_panel_set_computer_depth(PlayerControlsPanel *self, guint depth);
guint player_controls_panel_get_computer_depth(PlayerControlsPanel *self);

G_END_DECLS

#endif
