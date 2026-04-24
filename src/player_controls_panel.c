#include "player_controls_panel.h"

enum {
  PLAYER_CONTROLS_PANEL_SIGNAL_CONTROL_CHANGED,
  PLAYER_CONTROLS_PANEL_SIGNAL_COUNT
};

static guint player_controls_panel_signals[PLAYER_CONTROLS_PANEL_SIGNAL_COUNT];

struct _PlayerControlsPanel {
  GtkBox parent_instance;
  GtkDropDown *side0_control;
  GtkDropDown *side1_control;
  GtkScale *computer_depth_scale;
  GtkLabel *side0_label;
  GtkLabel *side1_label;
};

G_DEFINE_TYPE(PlayerControlsPanel, player_controls_panel, GTK_TYPE_BOX)

static gboolean player_controls_panel_mode_valid(PlayerControlMode mode) {
  return mode == PLAYER_CONTROL_MODE_USER || mode == PLAYER_CONTROL_MODE_COMPUTER;
}

static gboolean player_controls_panel_computer_depth_valid(guint depth) {
  return depth <= PLAYER_COMPUTER_DEPTH_MAX;
}

static GtkDropDown *player_controls_panel_get_control(PlayerControlsPanel *self, guint side) {
  g_return_val_if_fail(PLAYER_IS_CONTROLS_PANEL(self), NULL);
  g_return_val_if_fail(side <= 1, NULL);

  GtkDropDown *control = side == 0 ? self->side0_control : self->side1_control;
  if (control == NULL) {
    g_debug("Missing player control dropdown");
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

  self->side0_control = NULL;
  self->side1_control = NULL;
  self->computer_depth_scale = NULL;
  self->side0_label = NULL;
  self->side1_label = NULL;

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

  GtkWidget *controls_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_append(GTK_BOX(self), controls_row);

  GtkWidget *side0_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  GtkWidget *side1_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  GtkWidget *computer_depth_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_box_append(GTK_BOX(controls_row), side0_box);
  gtk_box_append(GTK_BOX(controls_row), side1_box);
  gtk_box_append(GTK_BOX(controls_row), computer_depth_box);

  self->side0_label = GTK_LABEL(gtk_label_new("Side 1"));
  self->side1_label = GTK_LABEL(gtk_label_new("Side 2"));
  GtkWidget *computer_depth_label = gtk_label_new("Computer depth");
  gtk_widget_set_halign(GTK_WIDGET(self->side0_label), GTK_ALIGN_START);
  gtk_widget_set_halign(GTK_WIDGET(self->side1_label), GTK_ALIGN_START);
  gtk_widget_set_halign(computer_depth_label, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(side0_box), GTK_WIDGET(self->side0_label));
  gtk_box_append(GTK_BOX(side1_box), GTK_WIDGET(self->side1_label));
  gtk_box_append(GTK_BOX(computer_depth_box), computer_depth_label);

  static const char *control_options[] = {"User", "Computer", NULL};
  self->side0_control = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(control_options));
  self->side1_control = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(control_options));
  self->computer_depth_scale = GTK_SCALE(
      gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, PLAYER_COMPUTER_DEPTH_MIN, PLAYER_COMPUTER_DEPTH_MAX, 1));
  gtk_scale_set_digits(self->computer_depth_scale, 0);
  gtk_scale_set_draw_value(self->computer_depth_scale, TRUE);
  gtk_widget_set_hexpand(GTK_WIDGET(self->computer_depth_scale), TRUE);
  gtk_widget_set_size_request(GTK_WIDGET(self->computer_depth_scale), 100, -1);
  gtk_widget_set_size_request(GTK_WIDGET(self->side0_control), 100, -1);
  gtk_widget_set_size_request(GTK_WIDGET(self->side1_control), 100, -1);
  player_controls_panel_set_mode(self, 0, PLAYER_CONTROL_MODE_USER);
  player_controls_panel_set_mode(self, 1, PLAYER_CONTROL_MODE_USER);
  player_controls_panel_set_computer_depth(self, PLAYER_COMPUTER_DEPTH_DEFAULT);
  gtk_box_append(GTK_BOX(side0_box), GTK_WIDGET(self->side0_control));
  gtk_box_append(GTK_BOX(side1_box), GTK_WIDGET(self->side1_control));
  gtk_box_append(GTK_BOX(computer_depth_box), GTK_WIDGET(self->computer_depth_scale));

  g_signal_connect(self->side0_control,
                   "notify::selected",
                   G_CALLBACK(player_controls_panel_on_selected_notify),
                   self);
  g_signal_connect(self->side1_control,
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

GtkDropDown *player_controls_panel_get_drop_down(PlayerControlsPanel *self, guint side) {
  return player_controls_panel_get_control(self, side);
}

void player_controls_panel_set_selected(PlayerControlsPanel *self, guint side, guint selected) {
  g_return_if_fail(selected <= PLAYER_CONTROL_MODE_COMPUTER);

  GtkDropDown *control = player_controls_panel_get_control(self, side);
  g_return_if_fail(control != NULL);

  gtk_drop_down_set_selected(control, selected);
}

void player_controls_panel_set_mode(PlayerControlsPanel *self, guint side, PlayerControlMode mode) {
  g_return_if_fail(player_controls_panel_mode_valid(mode));

  player_controls_panel_set_selected(self, side, (guint) mode);
}

void player_controls_panel_set_all_user(PlayerControlsPanel *self) {
  g_return_if_fail(PLAYER_IS_CONTROLS_PANEL(self));

  player_controls_panel_set_mode(self, 0, PLAYER_CONTROL_MODE_USER);
  player_controls_panel_set_mode(self, 1, PLAYER_CONTROL_MODE_USER);
}

guint player_controls_panel_get_selected(PlayerControlsPanel *self, guint side) {
  GtkDropDown *control = player_controls_panel_get_control(self, side);
  g_return_val_if_fail(control != NULL, 0);

  return gtk_drop_down_get_selected(control);
}

PlayerControlMode player_controls_panel_get_mode(PlayerControlsPanel *self, guint side) {
  guint selected = player_controls_panel_get_selected(self, side);
  g_return_val_if_fail(selected <= PLAYER_CONTROL_MODE_COMPUTER, PLAYER_CONTROL_MODE_USER);

  return (PlayerControlMode) selected;
}

gboolean player_controls_panel_is_user_control(PlayerControlsPanel *self, guint side) {
  return player_controls_panel_get_mode(self, side) == PLAYER_CONTROL_MODE_USER;
}

void player_controls_panel_set_side_labels(PlayerControlsPanel *self,
                                           const char *side0_label,
                                           const char *side1_label) {
  g_return_if_fail(PLAYER_IS_CONTROLS_PANEL(self));

  gtk_label_set_text(self->side0_label, side0_label != NULL ? side0_label : "Side 1");
  gtk_label_set_text(self->side1_label, side1_label != NULL ? side1_label : "Side 2");
}

void player_controls_panel_set_computer_depth(PlayerControlsPanel *self, guint depth) {
  g_return_if_fail(PLAYER_IS_CONTROLS_PANEL(self));
  g_return_if_fail(player_controls_panel_computer_depth_valid(depth));
  g_return_if_fail(self->computer_depth_scale != NULL);

  gtk_range_set_value(GTK_RANGE(self->computer_depth_scale), (gdouble) depth);
}

guint player_controls_panel_get_computer_depth(PlayerControlsPanel *self) {
  g_return_val_if_fail(PLAYER_IS_CONTROLS_PANEL(self), PLAYER_COMPUTER_DEPTH_DEFAULT);
  g_return_val_if_fail(self->computer_depth_scale != NULL, PLAYER_COMPUTER_DEPTH_DEFAULT);

  gdouble value = gtk_range_get_value(GTK_RANGE(self->computer_depth_scale));
  guint depth = (guint) value;
  if (!player_controls_panel_computer_depth_valid(depth)) {
    g_debug("Unexpected computer depth value");
    return PLAYER_COMPUTER_DEPTH_DEFAULT;
  }

  return depth;
}
