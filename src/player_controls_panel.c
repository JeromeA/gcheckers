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
  GtkDropDown *computer_level_control;
  GtkDropDown *ruleset_control;
  GtkWidget *force_move_button;
};

G_DEFINE_TYPE(PlayerControlsPanel, player_controls_panel, GTK_TYPE_BOX)

static gboolean player_controls_panel_mode_valid(PlayerControlMode mode) {
  return mode == PLAYER_CONTROL_MODE_USER || mode == PLAYER_CONTROL_MODE_COMPUTER;
}

static gboolean player_controls_panel_computer_level_valid(PlayerComputerLevel level) {
  return level == PLAYER_COMPUTER_LEVEL_1_RANDOM || level == PLAYER_COMPUTER_LEVEL_2_DEPTH_4
      || level == PLAYER_COMPUTER_LEVEL_3_DEPTH_8;
}

static gboolean player_controls_panel_ruleset_valid(PlayerRuleset ruleset) {
  return ruleset == PLAYER_RULESET_AMERICAN || ruleset == PLAYER_RULESET_INTERNATIONAL;
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

static void player_controls_panel_on_force_move_clicked(GtkButton * /*button*/, gpointer user_data) {
  PlayerControlsPanel *self = PLAYER_CONTROLS_PANEL(user_data);

  g_return_if_fail(PLAYER_IS_CONTROLS_PANEL(self));

  g_signal_emit(self, player_controls_panel_signals[PLAYER_CONTROLS_PANEL_SIGNAL_FORCE_MOVE_REQUESTED], 0);
}

static void player_controls_panel_dispose(GObject *object) {
  PlayerControlsPanel *self = PLAYER_CONTROLS_PANEL(object);

  self->white_control = NULL;
  self->black_control = NULL;
  self->computer_level_control = NULL;
  self->ruleset_control = NULL;
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

  GtkWidget *computer_level_label = gtk_label_new("Computer level");
  gtk_widget_set_halign(computer_level_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(controls_grid), computer_level_label, 0, 2, 1, 1);

  GtkWidget *ruleset_label = gtk_label_new("Ruleset");
  gtk_widget_set_halign(ruleset_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(controls_grid), ruleset_label, 0, 3, 1, 1);

  static const char *control_options[] = {"User", "Computer", NULL};
  static const char *computer_level_options[] = {"Comp Level 1 (random)",
                                                 "Comp Level 2 (depth 4)",
                                                 "Comp Level 3 (depth 8)",
                                                 NULL};
  static const char *ruleset_options[] = {"American (8x8)", "International (10x10)", NULL};
  self->white_control = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(control_options));
  self->black_control = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(control_options));
  self->computer_level_control = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(computer_level_options));
  self->ruleset_control = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(ruleset_options));
  player_controls_panel_set_mode(self, CHECKERS_COLOR_WHITE, PLAYER_CONTROL_MODE_USER);
  player_controls_panel_set_mode(self, CHECKERS_COLOR_BLACK, PLAYER_CONTROL_MODE_USER);
  player_controls_panel_set_computer_level(self, PLAYER_COMPUTER_LEVEL_1_RANDOM);
  player_controls_panel_set_ruleset(self, PLAYER_RULESET_AMERICAN);
  gtk_grid_attach(GTK_GRID(controls_grid), GTK_WIDGET(self->white_control), 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(controls_grid), GTK_WIDGET(self->black_control), 1, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(controls_grid), GTK_WIDGET(self->computer_level_control), 1, 2, 1, 1);
  gtk_grid_attach(GTK_GRID(controls_grid), GTK_WIDGET(self->ruleset_control), 1, 3, 1, 1);

  g_signal_connect(self->white_control,
                   "notify::selected",
                   G_CALLBACK(player_controls_panel_on_selected_notify),
                   self);
  g_signal_connect(self->black_control,
                   "notify::selected",
                   G_CALLBACK(player_controls_panel_on_selected_notify),
                   self);
  g_signal_connect(self->computer_level_control,
                   "notify::selected",
                   G_CALLBACK(player_controls_panel_on_selected_notify),
                   self);
  g_signal_connect(self->ruleset_control,
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

void player_controls_panel_set_computer_level(PlayerControlsPanel *self, PlayerComputerLevel level) {
  g_return_if_fail(PLAYER_IS_CONTROLS_PANEL(self));
  g_return_if_fail(player_controls_panel_computer_level_valid(level));

  g_return_if_fail(self->computer_level_control != NULL);
  gtk_drop_down_set_selected(self->computer_level_control, (guint)level);
}

PlayerComputerLevel player_controls_panel_get_computer_level(PlayerControlsPanel *self) {
  g_return_val_if_fail(PLAYER_IS_CONTROLS_PANEL(self), PLAYER_COMPUTER_LEVEL_1_RANDOM);
  g_return_val_if_fail(self->computer_level_control != NULL, PLAYER_COMPUTER_LEVEL_1_RANDOM);

  guint selected = gtk_drop_down_get_selected(self->computer_level_control);
  g_return_val_if_fail(selected <= PLAYER_COMPUTER_LEVEL_3_DEPTH_8, PLAYER_COMPUTER_LEVEL_1_RANDOM);
  return (PlayerComputerLevel)selected;
}

gboolean player_controls_panel_computer_level_depth(PlayerComputerLevel level, guint *out_depth) {
  g_return_val_if_fail(out_depth != NULL, FALSE);

  switch (level) {
    case PLAYER_COMPUTER_LEVEL_1_RANDOM:
      *out_depth = 0;
      return TRUE;
    case PLAYER_COMPUTER_LEVEL_2_DEPTH_4:
      *out_depth = 4;
      return TRUE;
    case PLAYER_COMPUTER_LEVEL_3_DEPTH_8:
      *out_depth = 8;
      return TRUE;
    default:
      g_debug("Unexpected computer level value");
      return FALSE;
  }
}

void player_controls_panel_set_ruleset(PlayerControlsPanel *self, PlayerRuleset ruleset) {
  g_return_if_fail(PLAYER_IS_CONTROLS_PANEL(self));
  g_return_if_fail(player_controls_panel_ruleset_valid(ruleset));
  g_return_if_fail(self->ruleset_control != NULL);

  gtk_drop_down_set_selected(self->ruleset_control, (guint)ruleset);
}

PlayerRuleset player_controls_panel_get_ruleset(PlayerControlsPanel *self) {
  g_return_val_if_fail(PLAYER_IS_CONTROLS_PANEL(self), PLAYER_RULESET_AMERICAN);
  g_return_val_if_fail(self->ruleset_control != NULL, PLAYER_RULESET_AMERICAN);

  guint selected = gtk_drop_down_get_selected(self->ruleset_control);
  g_return_val_if_fail(selected <= PLAYER_RULESET_INTERNATIONAL, PLAYER_RULESET_AMERICAN);
  return (PlayerRuleset)selected;
}

void player_controls_panel_set_force_move_sensitive(PlayerControlsPanel *self, gboolean sensitive) {
  g_return_if_fail(PLAYER_IS_CONTROLS_PANEL(self));

  GtkWidget *button = player_controls_panel_get_force_move_button(self);
  g_return_if_fail(button != NULL);

  gtk_widget_set_sensitive(button, sensitive);
}
