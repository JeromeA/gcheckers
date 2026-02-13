#include "player_controls_panel.h"

enum {
  PLAYER_CONTROLS_PANEL_SIGNAL_CONTROL_CHANGED,
  PLAYER_CONTROLS_PANEL_SIGNAL_FORCE_MOVE_REQUESTED,
  PLAYER_CONTROLS_PANEL_SIGNAL_COUNT
};

static guint player_controls_panel_signals[PLAYER_CONTROLS_PANEL_SIGNAL_COUNT];

struct _PlayerControlsPanel {
  GtkBox parent_instance;
  GtkWidget *force_move_button;
};

G_DEFINE_TYPE(PlayerControlsPanel, player_controls_panel, GTK_TYPE_BOX)

static void player_controls_panel_on_force_move_clicked(GtkButton * /*button*/, gpointer user_data) {
  PlayerControlsPanel *self = PLAYER_CONTROLS_PANEL(user_data);

  g_return_if_fail(PLAYER_IS_CONTROLS_PANEL(self));

  g_signal_emit(self, player_controls_panel_signals[PLAYER_CONTROLS_PANEL_SIGNAL_FORCE_MOVE_REQUESTED], 0);
}

static void player_controls_panel_dispose(GObject *object) {
  PlayerControlsPanel *self = PLAYER_CONTROLS_PANEL(object);

  self->force_move_button = NULL;

  G_OBJECT_CLASS(player_controls_panel_parent_class)->dispose(object);
}

static void player_controls_panel_class_init(PlayerControlsPanelClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = player_controls_panel_dispose;

  player_controls_panel_signals[PLAYER_CONTROLS_PANEL_SIGNAL_CONTROL_CHANGED] =
      g_signal_new("control-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE,
                   0);
  player_controls_panel_signals[PLAYER_CONTROLS_PANEL_SIGNAL_FORCE_MOVE_REQUESTED] =
      g_signal_new("force-move-requested", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                   G_TYPE_NONE, 0);
}

static void player_controls_panel_init(PlayerControlsPanel *self) {
  gtk_orientable_set_orientation(GTK_ORIENTABLE(self), GTK_ORIENTATION_HORIZONTAL);

  self->force_move_button = gtk_button_new_with_label("Force move");
  g_signal_connect(self->force_move_button, "clicked", G_CALLBACK(player_controls_panel_on_force_move_clicked), self);
  gtk_box_append(GTK_BOX(self), self->force_move_button);
}

PlayerControlsPanel *player_controls_panel_new(void) {
  return g_object_new(PLAYER_TYPE_CONTROLS_PANEL, NULL);
}

GtkDropDown *player_controls_panel_get_drop_down(PlayerControlsPanel *self, CheckersColor /*color*/) {
  g_return_val_if_fail(PLAYER_IS_CONTROLS_PANEL(self), NULL);

  g_debug("Player control dropdowns are disabled in this reduced reproduction\n");
  return NULL;
}

GtkWidget *player_controls_panel_get_force_move_button(PlayerControlsPanel *self) {
  g_return_val_if_fail(PLAYER_IS_CONTROLS_PANEL(self), NULL);

  if (!self->force_move_button) {
    g_debug("Missing force move button\n");
    return NULL;
  }

  return self->force_move_button;
}

void player_controls_panel_set_selected(PlayerControlsPanel *self, CheckersColor /*color*/, guint /*selected*/) {
  g_return_if_fail(PLAYER_IS_CONTROLS_PANEL(self));
}

void player_controls_panel_set_all_user(PlayerControlsPanel *self) {
  g_return_if_fail(PLAYER_IS_CONTROLS_PANEL(self));
}

guint player_controls_panel_get_selected(PlayerControlsPanel *self, CheckersColor /*color*/) {
  g_return_val_if_fail(PLAYER_IS_CONTROLS_PANEL(self), 0);

  return 1;
}

gboolean player_controls_panel_is_user_control(PlayerControlsPanel *self, CheckersColor /*color*/) {
  g_return_val_if_fail(PLAYER_IS_CONTROLS_PANEL(self), FALSE);

  return FALSE;
}

void player_controls_panel_set_force_move_sensitive(PlayerControlsPanel *self, gboolean sensitive) {
  g_return_if_fail(PLAYER_IS_CONTROLS_PANEL(self));

  GtkWidget *button = player_controls_panel_get_force_move_button(self);
  if (!button) {
    return;
  }
  gtk_widget_set_sensitive(button, sensitive);
}
