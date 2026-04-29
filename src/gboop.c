#include "active_game_backend.h"
#include "board_view.h"
#include "game_model.h"
#include "games/boop/boop_types.h"

#include <gtk/gtk.h>

typedef struct {
  GGameModel *model;
  BoardView *board_view;
  GtkWidget *status_label;
  GtkWidget *supply_panels[2];
  GtkWidget *promotion_buttons[2];
  GtkWidget *supply_pile_buttons[2][2];
  guint selected_rank[2];
} GBoopWindowState;

static const char *gboop_css =
    "window { background: #f4ead8; }"
    ".boop-shell { padding: 18px; }"
    ".boop-title { color: #2b2118; font-size: 28px; font-weight: 800; }"
    ".boop-status { color: #4a3a2a; font-size: 16px; }"
    ".boop-play-area { margin-top: 4px; }"
    ".boop-supply-panel { padding: 8px; }"
    ".boop-supply-active { background: rgba(15,61,102,0.10); border: 2px solid rgba(15,61,102,0.55); }"
    ".boop-supply-title { color: #2b2118; font-size: 15px; font-weight: 800; }"
    ".boop-pile-button { background: rgba(255,255,255,0.28); border: 2px solid transparent; border-radius: 0; }"
    ".boop-pile-button:hover { background: rgba(255,255,255,0.42); }"
    ".boop-pile-selected { border-color: #2f624d; box-shadow: 0 0 0 2px rgba(47,98,77,0.25); }"
    ".boop-pile-empty { opacity: 0.35; }"
    ".boop-promotion-button { margin-top: 6px; }"
    ".board { background: #0f3d66; padding: 8px; border-radius: 18px; }"
    ".board-square { border: 2px solid #0f3d66; border-radius: 8px; }"
    ".board-dark { background: #b9e4ff; background-image: none; color: #0f3d66; }"
    ".board-halo { outline: 4px solid rgba(23, 116, 87, 0.65); outline-offset: -4px; }"
    ".board-halo-selected { outline: 4px solid rgba(170, 73, 39, 0.88); outline-offset: -4px; }"
    ".piece-label { font-size: 28px; font-weight: 900; }"
    ".piece-side-0 { color: #74777c; }"
    ".piece-side-1 { color: #f0c64a; }"
    ".square-index { opacity: 0.35; font-size: 9px; }";

static void gboop_update_status(GBoopWindowState *state);

static guint gboop_pile_index_to_rank(guint pile_index) {
  return pile_index == 1 ? BOOP_PIECE_RANK_CAT : BOOP_PIECE_RANK_KITTEN;
}

static const char *gboop_supply_symbol(guint side, guint rank) {
  if (side == 0) {
    return rank == BOOP_PIECE_RANK_CAT ? "c" : "k";
  }

  return rank == BOOP_PIECE_RANK_CAT ? "C" : "K";
}

static guint gboop_supply_count(const BoopPosition *position, guint side, guint rank) {
  g_return_val_if_fail(position != NULL, 0);
  g_return_val_if_fail(side <= 1, 0);

  if (rank == BOOP_PIECE_RANK_CAT) {
    return position->cats_in_supply[side];
  }
  if (rank == BOOP_PIECE_RANK_KITTEN) {
    return position->kittens_in_supply[side];
  }

  return 0;
}

static gboolean gboop_supply_rank_available(const BoopPosition *position, guint side, guint rank) {
  return gboop_supply_count(position, side, rank) > 0;
}

static const BoopMoveBuilderState *gboop_peek_move_builder_state(GBoopWindowState *state) {
  g_return_val_if_fail(state != NULL, NULL);
  g_return_val_if_fail(BOARD_IS_VIEW(state->board_view), NULL);

  const GameBackendMoveBuilder *builder = board_view_peek_move_builder(state->board_view);
  if (builder == NULL || builder->builder_state == NULL ||
      builder->builder_state_size != sizeof(BoopMoveBuilderState)) {
    return NULL;
  }

  return builder->builder_state;
}

static gboolean gboop_promotion_selection_visible(GBoopWindowState *state, const BoopPosition *position, guint side) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(side <= 1, FALSE);

  if (position->outcome != GAME_BACKEND_OUTCOME_ONGOING || side != position->turn) {
    return FALSE;
  }

  const BoopMoveBuilderState *builder_state = gboop_peek_move_builder_state(state);
  if (builder_state == NULL) {
    return FALSE;
  }

  return builder_state->stage == BOOP_MOVE_BUILDER_STAGE_PROMOTION ||
      board_view_move_selection_completion_pending(state->board_view);
}

static gboolean gboop_defer_promotion_confirmation(const GameBackendMoveBuilder *builder,
                                                   gconstpointer move,
                                                   gpointer /*user_data*/) {
  g_return_val_if_fail(builder != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  const BoopMoveBuilderState *builder_state = builder->builder_state;
  g_return_val_if_fail(builder_state != NULL, FALSE);
  if (builder->builder_state_size != sizeof(BoopMoveBuilderState)) {
    g_debug("Unexpected boop move builder state size");
    return FALSE;
  }

  return builder_state->stage == BOOP_MOVE_BUILDER_STAGE_COMPLETE && builder_state->promotion_option_count > 0;
}

static void gboop_ensure_selected_rank(GBoopWindowState *state, const BoopPosition *position, guint side) {
  g_return_if_fail(state != NULL);
  g_return_if_fail(position != NULL);
  g_return_if_fail(side <= 1);

  if (gboop_supply_rank_available(position, side, state->selected_rank[side])) {
    return;
  }

  if (gboop_supply_rank_available(position, side, BOOP_PIECE_RANK_KITTEN)) {
    state->selected_rank[side] = BOOP_PIECE_RANK_KITTEN;
  } else if (gboop_supply_rank_available(position, side, BOOP_PIECE_RANK_CAT)) {
    state->selected_rank[side] = BOOP_PIECE_RANK_CAT;
  } else {
    state->selected_rank[side] = BOOP_PIECE_RANK_NONE;
  }
}

static GtkWidget *gboop_build_supply_pile(guint side, guint rank, guint count) {
  GtkWidget *fixed = gtk_fixed_new();

  gtk_widget_set_size_request(fixed, 88, 88);

  for (guint i = 0; i < count; ++i) {
    GtkWidget *piece = gtk_label_new(gboop_supply_symbol(side, rank));
    guint row = i / 4;
    guint col = i % 4;
    double x = 8.0 + (double)col * 16.0 + (double)row * 5.0;
    double y = 6.0 + (double)row * 22.0;

    gtk_widget_add_css_class(piece, "piece-label");
    gtk_widget_add_css_class(piece, side == 0 ? "piece-side-0" : "piece-side-1");
    gtk_fixed_put(GTK_FIXED(fixed), piece, x, y);
  }

  return fixed;
}

static void gboop_update_supply(GBoopWindowState *state) {
  g_return_if_fail(state != NULL);
  g_return_if_fail(GGAME_IS_MODEL(state->model));

  const BoopPosition *position = ggame_model_peek_position(state->model);
  g_return_if_fail(position != NULL);
  g_return_if_fail(position->turn <= 1);

  gboolean has_active_supply = position->outcome == GAME_BACKEND_OUTCOME_ONGOING;

  for (guint side = 0; side < 2; ++side) {
    gboolean is_active_side = has_active_supply && side == position->turn;

    if (state->supply_panels[side] != NULL) {
      if (is_active_side) {
        gtk_widget_add_css_class(state->supply_panels[side], "boop-supply-active");
      } else {
        gtk_widget_remove_css_class(state->supply_panels[side], "boop-supply-active");
      }
    }

    if (is_active_side) {
      gboop_ensure_selected_rank(state, position, side);
    }

    for (guint pile_index = 0; pile_index < 2; ++pile_index) {
      guint rank = gboop_pile_index_to_rank(pile_index);
      guint count = gboop_supply_count(position, side, rank);
      GtkWidget *button = state->supply_pile_buttons[side][pile_index];

      if (button == NULL) {
        continue;
      }

      gtk_button_set_child(GTK_BUTTON(button), gboop_build_supply_pile(side, rank, count));
      gtk_widget_set_can_target(button, is_active_side && count > 0);
      if (is_active_side && state->selected_rank[side] == rank) {
        gtk_widget_add_css_class(button, "boop-pile-selected");
      } else {
        gtk_widget_remove_css_class(button, "boop-pile-selected");
      }
      if (count == 0) {
        gtk_widget_add_css_class(button, "boop-pile-empty");
      } else {
        gtk_widget_remove_css_class(button, "boop-pile-empty");
      }
    }

    if (state->promotion_buttons[side] != NULL) {
      gboolean promotion_visible = gboop_promotion_selection_visible(state, position, side);
      gtk_widget_set_visible(state->promotion_buttons[side], promotion_visible);
      gtk_widget_set_sensitive(state->promotion_buttons[side],
                               promotion_visible && board_view_move_selection_completion_pending(state->board_view));
    }
  }
}

static gboolean gboop_prefer_selected_supply_candidate(gconstpointer move, gpointer user_data) {
  const BoopMove *boop_move = move;
  GBoopWindowState *state = user_data;
  const BoopPosition *position = NULL;
  guint side = 0;

  g_return_val_if_fail(boop_move != NULL, FALSE);
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(GGAME_IS_MODEL(state->model), FALSE);

  position = ggame_model_peek_position(state->model);
  g_return_val_if_fail(position != NULL, FALSE);

  side = position->turn;
  g_return_val_if_fail(side <= 1, FALSE);
  return boop_move->rank == state->selected_rank[side];
}

static void gboop_on_supply_pile_clicked(GtkButton *button, gpointer user_data) {
  GBoopWindowState *state = user_data;

  g_return_if_fail(GTK_IS_BUTTON(button));
  g_return_if_fail(state != NULL);

  guint side = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "boop-side")) - 1;
  guint rank = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "boop-rank"));
  const BoopPosition *position = ggame_model_peek_position(state->model);
  g_return_if_fail(position != NULL);
  g_return_if_fail(side <= 1);

  if (position->outcome != GAME_BACKEND_OUTCOME_ONGOING || side != position->turn) {
    return;
  }

  if (!gboop_supply_rank_available(position, side, rank)) {
    return;
  }

  state->selected_rank[side] = rank;
  board_view_clear_selection(state->board_view);
  gboop_update_supply(state);
}

static void gboop_on_confirm_promotions_clicked(GtkButton *button, gpointer user_data) {
  GBoopWindowState *state = user_data;

  g_return_if_fail(GTK_IS_BUTTON(button));
  g_return_if_fail(state != NULL);

  if (!board_view_confirm_move_selection(state->board_view)) {
    g_debug("Failed to confirm boop promotion selection");
  }
}

static void gboop_on_board_selection_changed(gpointer user_data) {
  GBoopWindowState *state = user_data;

  g_return_if_fail(state != NULL);
  gboop_update_status(state);
}

static GtkWidget *gboop_create_supply_panel(GBoopWindowState *state, guint side) {
  g_return_val_if_fail(state != NULL, NULL);
  g_return_val_if_fail(side <= 1, NULL);

  GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(panel, "boop-supply-panel");
  gtk_widget_set_valign(panel, GTK_ALIGN_CENTER);
  state->supply_panels[side] = panel;

  GtkWidget *title = gtk_label_new(side == 0 ? "Player 1" : "Player 2");
  gtk_widget_add_css_class(title, "boop-supply-title");
  gtk_box_append(GTK_BOX(panel), title);

  for (guint pile_index = 0; pile_index < 2; ++pile_index) {
    guint rank = gboop_pile_index_to_rank(pile_index);
    GtkWidget *button = gtk_button_new();

    gtk_widget_add_css_class(button, "boop-pile-button");
    gtk_widget_set_size_request(button, 96, 96);
    g_object_set_data(G_OBJECT(button), "boop-side", GUINT_TO_POINTER(side + 1));
    g_object_set_data(G_OBJECT(button), "boop-rank", GUINT_TO_POINTER(rank));
    g_signal_connect(button, "clicked", G_CALLBACK(gboop_on_supply_pile_clicked), state);
    state->supply_pile_buttons[side][pile_index] = button;
    gtk_box_append(GTK_BOX(panel), button);
  }

  GtkWidget *promotion_button = gtk_button_new_with_label("Confirm promotions");
  gtk_widget_add_css_class(promotion_button, "boop-promotion-button");
  gtk_widget_set_visible(promotion_button, FALSE);
  gtk_widget_set_sensitive(promotion_button, FALSE);
  g_signal_connect(promotion_button, "clicked", G_CALLBACK(gboop_on_confirm_promotions_clicked), state);
  state->promotion_buttons[side] = promotion_button;
  gtk_box_append(GTK_BOX(panel), promotion_button);

  return panel;
}

static void gboop_window_state_free(GBoopWindowState *state) {
  if (state == NULL) {
    return;
  }

  g_clear_object(&state->model);
  g_clear_object(&state->board_view);
  g_free(state);
}

static void gboop_update_status(GBoopWindowState *state) {
  g_return_if_fail(state != NULL);
  g_return_if_fail(GGAME_IS_MODEL(state->model));
  g_return_if_fail(GTK_IS_LABEL(state->status_label));

  const GameBackend *backend = ggame_model_peek_backend(state->model);
  gconstpointer position = ggame_model_peek_position(state->model);
  g_return_if_fail(backend != NULL);
  g_return_if_fail(position != NULL);

  GameBackendOutcome outcome = backend->position_outcome(position);
  gboop_update_supply(state);
  if (outcome != GAME_BACKEND_OUTCOME_ONGOING) {
    gtk_label_set_text(GTK_LABEL(state->status_label), backend->outcome_banner_text(outcome));
    board_view_set_banner_text(state->board_view, backend->outcome_banner_text(outcome));
    return;
  }

  guint side = backend->position_turn(position);
  g_autofree char *status = g_strdup_printf("%s to move", backend->side_label(side));
  gtk_label_set_text(GTK_LABEL(state->status_label), status);
  board_view_set_banner_text(state->board_view, NULL);
}

static gboolean gboop_apply_player_move(gconstpointer move, gpointer user_data) {
  GBoopWindowState *state = user_data;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(GGAME_IS_MODEL(state->model), FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  if (!ggame_model_apply_move(state->model, move)) {
    g_debug("Failed to apply boop move");
    return FALSE;
  }

  gboop_update_status(state);
  return TRUE;
}

static void gboop_on_model_state_changed(GGameModel * /*model*/, gpointer user_data) {
  GBoopWindowState *state = user_data;

  gboop_update_status(state);
}

static void gboop_on_new_game_clicked(GtkButton * /*button*/, gpointer user_data) {
  GBoopWindowState *state = user_data;

  g_return_if_fail(state != NULL);
  ggame_model_reset(state->model, NULL);
  board_view_clear_selection(state->board_view);
}

static void gboop_install_css(void) {
  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(provider, gboop_css);
  gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
}

static void gboop_on_activate(GtkApplication *app, gpointer /*user_data*/) {
  g_return_if_fail(GTK_IS_APPLICATION(app));

  gboop_install_css();

  GBoopWindowState *state = g_new0(GBoopWindowState, 1);
  g_return_if_fail(state != NULL);
  state->model = ggame_model_new(GGAME_ACTIVE_GAME_BACKEND);
  state->board_view = board_view_new();
  state->selected_rank[0] = BOOP_PIECE_RANK_KITTEN;
  state->selected_rank[1] = BOOP_PIECE_RANK_KITTEN;
  g_return_if_fail(GGAME_IS_MODEL(state->model));
  g_return_if_fail(BOARD_IS_VIEW(state->board_view));

  GtkWidget *window = gtk_application_window_new(app);
  g_return_if_fail(GTK_IS_APPLICATION_WINDOW(window));
  gtk_window_set_title(GTK_WINDOW(window), "Boop");
  gtk_window_set_default_size(GTK_WINDOW(window), 760, 680);
  g_object_set_data_full(G_OBJECT(window), "gboop-state", state, (GDestroyNotify)gboop_window_state_free);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(box, "boop-shell");
  gtk_window_set_child(GTK_WINDOW(window), box);

  GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_append(GTK_BOX(box), header);

  GtkWidget *title = gtk_label_new("Boop");
  gtk_widget_add_css_class(title, "boop-title");
  gtk_widget_set_hexpand(title, TRUE);
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_box_append(GTK_BOX(header), title);

  GtkWidget *new_game = gtk_button_new_with_label("New Game");
  gtk_box_append(GTK_BOX(header), new_game);

  state->status_label = gtk_label_new(NULL);
  gtk_widget_add_css_class(state->status_label, "boop-status");
  gtk_label_set_xalign(GTK_LABEL(state->status_label), 0.0f);
  gtk_box_append(GTK_BOX(box), state->status_label);

  GtkWidget *play_area = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
  gtk_widget_add_css_class(play_area, "boop-play-area");
  gtk_widget_set_hexpand(play_area, TRUE);
  gtk_widget_set_vexpand(play_area, TRUE);
  gtk_box_append(GTK_BOX(box), play_area);

  gtk_box_append(GTK_BOX(play_area), gboop_create_supply_panel(state, 0));

  board_view_set_model(state->board_view, state->model);
  board_view_set_move_handler(state->board_view, gboop_apply_player_move, state);
  board_view_set_move_candidate_preference(state->board_view, gboop_prefer_selected_supply_candidate, state);
  board_view_set_move_completion_confirmation(state->board_view, gboop_defer_promotion_confirmation, state);
  board_view_set_selection_changed_handler(state->board_view, gboop_on_board_selection_changed, state);
  GtkWidget *board = board_view_get_widget(state->board_view);
  gtk_widget_set_hexpand(board, TRUE);
  gtk_widget_set_vexpand(board, TRUE);
  gtk_box_append(GTK_BOX(play_area), board);

  gtk_box_append(GTK_BOX(play_area), gboop_create_supply_panel(state, 1));

  g_signal_connect(state->model, "state-changed", G_CALLBACK(gboop_on_model_state_changed), state);
  g_signal_connect(new_game, "clicked", G_CALLBACK(gboop_on_new_game_clicked), state);
  gboop_update_status(state);

  gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
  GtkApplication *app = gtk_application_new("io.github.jeromea.gboop", G_APPLICATION_DEFAULT_FLAGS);
  int status = 0;

  g_return_val_if_fail(GTK_IS_APPLICATION(app), 1);

  g_signal_connect(app, "activate", G_CALLBACK(gboop_on_activate), NULL);
  status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
