#ifndef PLAYER_CONTROLS_PANEL_H
#define PLAYER_CONTROLS_PANEL_H

#include "game.h"

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

typedef enum {
  PLAYER_COMPUTER_LEVEL_1_RANDOM = 0,
  PLAYER_COMPUTER_LEVEL_2_DEPTH_4 = 1,
  PLAYER_COMPUTER_LEVEL_3_DEPTH_8 = 2
} PlayerComputerLevel;

PlayerControlsPanel *player_controls_panel_new(void);
GtkDropDown *player_controls_panel_get_drop_down(PlayerControlsPanel *self, CheckersColor color);
GtkWidget *player_controls_panel_get_force_move_button(PlayerControlsPanel *self);
void player_controls_panel_set_selected(PlayerControlsPanel *self, CheckersColor color, guint selected);
void player_controls_panel_set_mode(PlayerControlsPanel *self, CheckersColor color, PlayerControlMode mode);
void player_controls_panel_set_all_user(PlayerControlsPanel *self);
guint player_controls_panel_get_selected(PlayerControlsPanel *self, CheckersColor color);
PlayerControlMode player_controls_panel_get_mode(PlayerControlsPanel *self, CheckersColor color);
gboolean player_controls_panel_is_user_control(PlayerControlsPanel *self, CheckersColor color);
void player_controls_panel_set_computer_level(PlayerControlsPanel *self, PlayerComputerLevel level);
PlayerComputerLevel player_controls_panel_get_computer_level(PlayerControlsPanel *self);
gboolean player_controls_panel_computer_level_depth(PlayerComputerLevel level, guint *out_depth);
void player_controls_panel_set_force_move_sensitive(PlayerControlsPanel *self, gboolean sensitive);

G_END_DECLS

#endif
