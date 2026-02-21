#include "player_controls_panel.h"

enum {
  PLAYER_CONTROLS_PANEL_SIGNAL_CONTROL_CHANGED,
  PLAYER_CONTROLS_PANEL_SIGNAL_COUNT
};

static guint player_controls_panel_signals[PLAYER_CONTROLS_PANEL_SIGNAL_COUNT];

struct _PlayerControlsPanel {
  GtkBox parent_instance;
  GtkDropDown *white_control;
  GtkDropDown *black_control;
  GtkScale *computer_depth_scale;
};

G_DEFINE_TYPE(PlayerControlsPanel, player_controls_panel, GTK_TYPE_BOX)

static gboolean player_controls_panel_mode_valid(PlayerControlMode mode) {
  return mode == PLAYER_CONTROL_MODE_USER || mode == PLAYER_CONTROL_MODE_COMPUTER;
}

static gboolean player_controls_panel_computer_depth_valid(guint depth) {
  return depth <= PLAYER_COMPUTER_DEPTH_MAX;
}

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

static void player_controls_panel_on_computer_depth_value_changed(GtkRange * /*range*/, gpointer user_data) {
  PlayerControlsPanel *self = PLAYER_CONTROLS_PANEL(user_data);

  g_return_if_fail(PLAYER_IS_CONTROLS_PANEL(self));

  g_signal_emit(self, player_controls_panel_signals[PLAYER_CONTROLS_PANEL_SIGNAL_CONTROL_CHANGED], 0);
}

static void player_controls_panel_dispose(GObject *object) {
  PlayerControlsPanel *self = PLAYER_CONTROLS_PANEL(object);

  self->white_control = NULL;
  self->black_control = NULL;
  self->computer_depth_scale = NULL;

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

  GtkWidget *computer_level_label = gtk_label_new("Computer level");
  gtk_widget_set_halign(computer_level_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(controls_grid), computer_level_label, 0, 2, 1, 1);

  static const char *control_options[] = {"User", "Computer", NULL};
  self->white_control = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(control_options));
  self->black_control = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(control_options));
  self->computer_depth_scale = GTK_SCALE(
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, PLAYER_COMPUTER_DEPTH_MIN, PLAYER_COMPUTER_DEPTH_MAX, 1));
  gtk_scale_set_digits(self->computer_depth_scale, 0);
  gtk_scale_set_draw_value(self->computer_depth_scale, TRUE);
  gtk_widget_set_hexpand(GTK_WIDGET(self->computer_depth_scale), TRUE);
  gtk_widget_set_size_request(GTK_WIDGET(self->computer_depth_scale), 100, -1);
  player_controls_panel_set_mode(self, CHECKERS_COLOR_WHITE, PLAYER_CONTROL_MODE_USER);
  player_controls_panel_set_mode(self, CHECKERS_COLOR_BLACK, PLAYER_CONTROL_MODE_USER);
  player_controls_panel_set_computer_depth(self, 8);
  gtk_grid_attach(GTK_GRID(controls_grid), GTK_WIDGET(self->white_control), 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(controls_grid), GTK_WIDGET(self->black_control), 1, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(controls_grid), GTK_WIDGET(self->computer_depth_scale), 1, 2, 1, 1);

  g_signal_connect(self->white_control,
                   "notify::selected",
                   G_CALLBACK(player_controls_panel_on_selected_notify),
                   self);
  g_signal_connect(self->black_control,
                   "notify::selected",
                   G_CALLBACK(player_controls_panel_on_selected_notify),
                   self);
  g_signal_connect(self->computer_depth_scale,
                   "value-changed",
                   G_CALLBACK(player_controls_panel_on_computer_depth_value_changed),
                   self);
}

PlayerControlsPanel *player_controls_panel_new(void) {
  return g_object_new(PLAYER_TYPE_CONTROLS_PANEL, NULL);
}

GtkDropDown *player_controls_panel_get_drop_down(PlayerControlsPanel *self, CheckersColor color) {
  return player_controls_panel_get_control(self, color);
}

void player_controls_panel_set_selected(PlayerControlsPanel *self, CheckersColor color, guint selected) {
  g_return_if_fail(selected <= PLAYER_CONTROL_MODE_COMPUTER);

  GtkDropDown *control = player_controls_panel_get_control(self, color);
  g_return_if_fail(control != NULL);

  gtk_drop_down_set_selected(control, selected);
}

void player_controls_panel_set_mode(PlayerControlsPanel *self, CheckersColor color, PlayerControlMode mode) {
  g_return_if_fail(player_controls_panel_mode_valid(mode));

  player_controls_panel_set_selected(self, color, (guint)mode);
}

void player_controls_panel_set_all_user(PlayerControlsPanel *self) {
  g_return_if_fail(PLAYER_IS_CONTROLS_PANEL(self));

  player_controls_panel_set_mode(self, CHECKERS_COLOR_WHITE, PLAYER_CONTROL_MODE_USER);
  player_controls_panel_set_mode(self, CHECKERS_COLOR_BLACK, PLAYER_CONTROL_MODE_USER);
}

guint player_controls_panel_get_selected(PlayerControlsPanel *self, CheckersColor color) {
  GtkDropDown *control = player_controls_panel_get_control(self, color);
  g_return_val_if_fail(control != NULL, 0);

  return gtk_drop_down_get_selected(control);
}

PlayerControlMode player_controls_panel_get_mode(PlayerControlsPanel *self, CheckersColor color) {
  guint selected = player_controls_panel_get_selected(self, color);
  g_return_val_if_fail(selected <= PLAYER_CONTROL_MODE_COMPUTER, PLAYER_CONTROL_MODE_USER);

  return (PlayerControlMode)selected;
}

gboolean player_controls_panel_is_user_control(PlayerControlsPanel *self, CheckersColor color) {
  return player_controls_panel_get_mode(self, color) == PLAYER_CONTROL_MODE_USER;
}

void player_controls_panel_set_computer_depth(PlayerControlsPanel *self, guint depth) {
  g_return_if_fail(PLAYER_IS_CONTROLS_PANEL(self));
  g_return_if_fail(player_controls_panel_computer_depth_valid(depth));
  g_return_if_fail(self->computer_depth_scale != NULL);

  gtk_range_set_value(GTK_RANGE(self->computer_depth_scale), (gdouble)depth);
}

guint player_controls_panel_get_computer_depth(PlayerControlsPanel *self) {
  g_return_val_if_fail(PLAYER_IS_CONTROLS_PANEL(self), 8);
  g_return_val_if_fail(self->computer_depth_scale != NULL, 8);

  gdouble value = gtk_range_get_value(GTK_RANGE(self->computer_depth_scale));
  guint depth = (guint)value;

  if (!player_controls_panel_computer_depth_valid(depth)) {
    g_debug("Unexpected computer depth value");
    return 8;
  }

  return depth;
}
