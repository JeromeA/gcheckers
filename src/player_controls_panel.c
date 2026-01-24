#include "player_controls_panel.h"

enum {
  PLAYER_CONTROLS_PANEL_SIGNAL_CONTROL_CHANGED,
  PLAYER_CONTROLS_PANEL_SIGNAL_FORCE_MOVE_REQUESTED,
  PLAYER_CONTROLS_PANEL_SIGNAL_COUNT
};

static guint player_controls_panel_signals[PLAYER_CONTROLS_PANEL_SIGNAL_COUNT];

struct _PlayerControlsPanel {
  GtkBox parent_instance;
  GtkDropDown *white_control;
  GtkDropDown *black_control;
  GtkWidget *force_move_button;
};

G_DEFINE_TYPE(PlayerControlsPanel, player_controls_panel, GTK_TYPE_BOX)

static GtkDropDown *player_controls_panel_get_control(PlayerControlsPanel *self, CheckersColor color) {
  g_return_val_if_fail(PLAYER_IS_CONTROLS_PANEL(self), NULL);
  g_return_val_if_fail(color == CHECKERS_COLOR_WHITE || color == CHECKERS_COLOR_BLACK, NULL);

  GtkDropDown *control = color == CHECKERS_COLOR_WHITE ? self->white_control : self->black_control;
  if (!control) {
    g_debug("Missing player control dropdown\n");
    return NULL;
  }

  return control;
}

static void player_controls_panel_on_selected_notify(GObject * /*object*/,
                                                     GParamSpec * /*pspec*/,
                                                     gpointer user_data) {
  PlayerControlsPanel *self = PLAYER_CONTROLS_PANEL(user_data);

  g_return_if_fail(PLAYER_IS_CONTROLS_PANEL(self));

  g_signal_emit(self, player_controls_panel_signals[PLAYER_CONTROLS_PANEL_SIGNAL_CONTROL_CHANGED], 0);
}

static void player_controls_panel_on_force_move_clicked(GtkButton * /*button*/, gpointer user_data) {
  PlayerControlsPanel *self = PLAYER_CONTROLS_PANEL(user_data);

  g_return_if_fail(PLAYER_IS_CONTROLS_PANEL(self));

  g_signal_emit(self, player_controls_panel_signals[PLAYER_CONTROLS_PANEL_SIGNAL_FORCE_MOVE_REQUESTED], 0);
}

static void player_controls_panel_dispose(GObject *object) {
  PlayerControlsPanel *self = PLAYER_CONTROLS_PANEL(object);

  self->white_control = NULL;
  self->black_control = NULL;
  self->force_move_button = NULL;

  G_OBJECT_CLASS(player_controls_panel_parent_class)->dispose(object);
}

static void player_controls_panel_class_init(PlayerControlsPanelClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = player_controls_panel_dispose;

  player_controls_panel_signals[PLAYER_CONTROLS_PANEL_SIGNAL_CONTROL_CHANGED] = g_signal_new(
      "control-changed",
      G_TYPE_FROM_CLASS(klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      NULL,
      G_TYPE_NONE,
      0);
  player_controls_panel_signals[PLAYER_CONTROLS_PANEL_SIGNAL_FORCE_MOVE_REQUESTED] = g_signal_new(
      "force-move-requested",
      G_TYPE_FROM_CLASS(klass),
      G_SIGNAL_RUN_LAST,
      0,
      NULL,
      NULL,
      NULL,
      G_TYPE_NONE,
      0);
}

static void player_controls_panel_init(PlayerControlsPanel *self) {
  gtk_orientable_set_orientation(GTK_ORIENTABLE(self), GTK_ORIENTATION_HORIZONTAL);
  gtk_box_set_spacing(GTK_BOX(self), 12);

  GtkWidget *controls_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(controls_grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(controls_grid), 8);
  gtk_box_append(GTK_BOX(self), controls_grid);

  GtkWidget *white_label = gtk_label_new("White");
  gtk_widget_set_halign(white_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(controls_grid), white_label, 0, 0, 1, 1);

  GtkWidget *black_label = gtk_label_new("Black");
  gtk_widget_set_halign(black_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(controls_grid), black_label, 0, 1, 1, 1);

  static const char *control_options[] = {"User", "Computer", NULL};
  self->white_control = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(control_options));
  self->black_control = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(control_options));
  gtk_drop_down_set_selected(self->white_control, 0);
  gtk_drop_down_set_selected(self->black_control, 1);
  gtk_grid_attach(GTK_GRID(controls_grid), GTK_WIDGET(self->white_control), 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(controls_grid), GTK_WIDGET(self->black_control), 1, 1, 1, 1);

  g_signal_connect(self->white_control,
                   "notify::selected",
                   G_CALLBACK(player_controls_panel_on_selected_notify),
                   self);
  g_signal_connect(self->black_control,
                   "notify::selected",
                   G_CALLBACK(player_controls_panel_on_selected_notify),
                   self);

  self->force_move_button = gtk_button_new_with_label("Force move");
  g_signal_connect(self->force_move_button,
                   "clicked",
                   G_CALLBACK(player_controls_panel_on_force_move_clicked),
                   self);
  gtk_box_append(GTK_BOX(self), self->force_move_button);
}

PlayerControlsPanel *player_controls_panel_new(void) {
  return g_object_new(PLAYER_TYPE_CONTROLS_PANEL, NULL);
}

GtkDropDown *player_controls_panel_get_drop_down(PlayerControlsPanel *self, CheckersColor color) {
  return player_controls_panel_get_control(self, color);
}

GtkWidget *player_controls_panel_get_force_move_button(PlayerControlsPanel *self) {
  g_return_val_if_fail(PLAYER_IS_CONTROLS_PANEL(self), NULL);

  if (!self->force_move_button) {
    g_debug("Missing force move button\n");
    return NULL;
  }

  return self->force_move_button;
}

void player_controls_panel_set_selected(PlayerControlsPanel *self, CheckersColor color, guint selected) {
  GtkDropDown *control = player_controls_panel_get_control(self, color);
  g_return_if_fail(control != NULL);

  gtk_drop_down_set_selected(control, selected);
}

guint player_controls_panel_get_selected(PlayerControlsPanel *self, CheckersColor color) {
  GtkDropDown *control = player_controls_panel_get_control(self, color);
  g_return_val_if_fail(control != NULL, 0);

  return gtk_drop_down_get_selected(control);
}

gboolean player_controls_panel_is_user_control(PlayerControlsPanel *self, CheckersColor color) {
  GtkDropDown *control = player_controls_panel_get_control(self, color);
  g_return_val_if_fail(control != NULL, TRUE);

  return gtk_drop_down_get_selected(control) == 0;
}

void player_controls_panel_set_force_move_sensitive(PlayerControlsPanel *self, gboolean sensitive) {
  g_return_if_fail(PLAYER_IS_CONTROLS_PANEL(self));

  GtkWidget *button = player_controls_panel_get_force_move_button(self);
  g_return_if_fail(button != NULL);

  gtk_widget_set_sensitive(button, sensitive);
}
