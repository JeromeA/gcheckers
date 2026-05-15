#include "homeworlds_app_window.h"

#include "../../game_model.h"
#include "homeworlds_backend.h"
#include "homeworlds_game.h"
#include "homeworlds_view.h"

#include <gtk/gtk.h>

typedef struct {
  GGameModel *model;
  HomeworldsView *view;
  GtkWidget *status_label;
  GtkWidget *random_button;
} GHomeworldsAppWindowState;

static void ghomeworlds_app_window_update(GHomeworldsAppWindowState *state);

static void ghomeworlds_app_window_state_free(gpointer data) {
  GHomeworldsAppWindowState *state = data;

  if (state == NULL) {
    return;
  }

  homeworlds_view_free(state->view);
  g_clear_object(&state->model);
  g_free(state);
}

static const char *ghomeworlds_app_window_phase_label(HomeworldsPhase phase) {
  switch (phase) {
    case HOMEWORLDS_PHASE_SETUP:
      return "setup";
    case HOMEWORLDS_PHASE_PLAY:
      return "play";
    case HOMEWORLDS_PHASE_FINISHED:
      return "finished";
    default:
      return "unknown";
  }
}

static void ghomeworlds_app_window_new_game_clicked(GtkButton * /*button*/, gpointer user_data) {
  GHomeworldsAppWindowState *state = user_data;

  g_return_if_fail(state != NULL);
  g_return_if_fail(GGAME_IS_MODEL(state->model));

  ggame_model_reset(state->model, NULL);
  homeworlds_view_refresh(state->view);
  ghomeworlds_app_window_update(state);
}

static void ghomeworlds_app_window_reset_clicked(GtkButton * /*button*/, gpointer user_data) {
  GHomeworldsAppWindowState *state = user_data;

  g_return_if_fail(state != NULL);

  homeworlds_view_reset_selection(state->view);
  ghomeworlds_app_window_update(state);
}

static void ghomeworlds_app_window_random_clicked(GtkButton * /*button*/, gpointer user_data) {
  GHomeworldsAppWindowState *state = user_data;

  g_return_if_fail(state != NULL);

  if (!homeworlds_view_apply_random_move(state->view)) {
    gtk_label_set_text(GTK_LABEL(state->status_label), "Random AI could not find a legal move.");
  }
  ghomeworlds_app_window_update(state);
}

static void ghomeworlds_app_window_view_move_applied(HomeworldsView * /*view*/, gpointer user_data) {
  GHomeworldsAppWindowState *state = user_data;

  g_return_if_fail(state != NULL);

  ghomeworlds_app_window_update(state);
}

static void ghomeworlds_app_window_model_changed(GGameModel * /*model*/, gpointer user_data) {
  GHomeworldsAppWindowState *state = user_data;

  g_return_if_fail(state != NULL);

  homeworlds_view_refresh(state->view);
  ghomeworlds_app_window_update(state);
}

static void ghomeworlds_app_window_update(GHomeworldsAppWindowState *state) {
  const HomeworldsPosition *position = NULL;
  GameBackendOutcome outcome = GAME_BACKEND_OUTCOME_ONGOING;
  char *status = NULL;

  g_return_if_fail(state != NULL);
  g_return_if_fail(GGAME_IS_MODEL(state->model));

  position = ggame_model_peek_position(state->model);
  g_return_if_fail(position != NULL);

  outcome = homeworlds_position_outcome(position);
  if (outcome != GAME_BACKEND_OUTCOME_ONGOING) {
    const char *winner = homeworlds_game_backend.outcome_banner_text(outcome);
    status = g_strdup_printf("%s.", winner != NULL ? winner : "Game finished");
  } else {
    status = g_strdup_printf("Player %u to move, %s phase.",
                             (guint) position->turn + 1,
                             ghomeworlds_app_window_phase_label(position->phase));
  }

  gtk_label_set_text(GTK_LABEL(state->status_label), status);
  gtk_widget_set_sensitive(state->random_button,
                           outcome == GAME_BACKEND_OUTCOME_ONGOING &&
                               !homeworlds_view_has_partial_selection(state->view));
  g_free(status);
}

GtkWindow *ghomeworlds_app_window_create(GtkApplication *app) {
  GHomeworldsAppWindowState *state = NULL;
  GtkWidget *window = NULL;
  GtkWidget *root = NULL;
  GtkWidget *toolbar = NULL;
  GtkWidget *title = NULL;
  GtkWidget *button = NULL;

  g_return_val_if_fail(GTK_IS_APPLICATION(app), NULL);

  state = g_new0(GHomeworldsAppWindowState, 1);
  g_return_val_if_fail(state != NULL, NULL);
  state->model = ggame_model_new(&homeworlds_game_backend);
  g_return_val_if_fail(GGAME_IS_MODEL(state->model), NULL);
  state->view = homeworlds_view_new(state->model);
  g_return_val_if_fail(state->view != NULL, NULL);

  window = gtk_application_window_new(app);
  g_return_val_if_fail(GTK_IS_APPLICATION_WINDOW(window), NULL);
  g_object_set_data_full(G_OBJECT(window), "ghomeworlds-state", state, ghomeworlds_app_window_state_free);
  gtk_window_set_title(GTK_WINDOW(window), "Homeworlds");
  gtk_window_set_default_size(GTK_WINDOW(window), 1180, 760);

  root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_name(root, "homeworlds-window");
  gtk_widget_set_margin_top(root, 12);
  gtk_widget_set_margin_bottom(root, 12);
  gtk_widget_set_margin_start(root, 12);
  gtk_widget_set_margin_end(root, 12);
  gtk_window_set_child(GTK_WINDOW(window), root);

  toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_box_append(GTK_BOX(root), toolbar);

  title = gtk_label_new("Homeworlds");
  gtk_widget_add_css_class(title, "title-2");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_widget_set_hexpand(title, TRUE);
  gtk_box_append(GTK_BOX(toolbar), title);

  state->status_label = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(state->status_label), 1.0f);
  gtk_box_append(GTK_BOX(toolbar), state->status_label);

  button = gtk_button_new_with_label("New game");
  g_signal_connect(button, "clicked", G_CALLBACK(ghomeworlds_app_window_new_game_clicked), state);
  gtk_box_append(GTK_BOX(toolbar), button);

  button = gtk_button_new_with_label("Reset selection");
  g_signal_connect(button, "clicked", G_CALLBACK(ghomeworlds_app_window_reset_clicked), state);
  gtk_box_append(GTK_BOX(toolbar), button);

  state->random_button = gtk_button_new_with_label("Random AI");
  g_signal_connect(state->random_button, "clicked", G_CALLBACK(ghomeworlds_app_window_random_clicked), state);
  gtk_box_append(GTK_BOX(toolbar), state->random_button);

  gtk_box_append(GTK_BOX(root), homeworlds_view_get_widget(state->view));

  homeworlds_view_set_move_applied_callback(state->view, ghomeworlds_app_window_view_move_applied, state);
  g_signal_connect(state->model, "state-changed", G_CALLBACK(ghomeworlds_app_window_model_changed), state);
  ghomeworlds_app_window_update(state);

  return GTK_WINDOW(window);
}
