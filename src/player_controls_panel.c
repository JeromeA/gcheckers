#include "player_controls_panel.h"

struct _PlayerControlsPanel {
  GtkBox parent_instance;
};

G_DEFINE_TYPE(PlayerControlsPanel, player_controls_panel, GTK_TYPE_BOX)

static void player_controls_panel_class_init(PlayerControlsPanelClass * /*klass*/) {
}

static void player_controls_panel_init(PlayerControlsPanel *self) {
  g_return_if_fail(PLAYER_IS_CONTROLS_PANEL(self));

  gtk_orientable_set_orientation(GTK_ORIENTABLE(self), GTK_ORIENTATION_HORIZONTAL);
}

PlayerControlsPanel *player_controls_panel_new(void) {
  return g_object_new(PLAYER_TYPE_CONTROLS_PANEL, NULL);
}
