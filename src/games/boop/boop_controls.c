#include "boop_controls.h"

#include "boop_types.h"
#include "board_view.h"
#include "game_model.h"

#include <gtk/gtk.h>

typedef struct {
  GGameModel *model;
  BoardView *board_view;
  GtkWidget *supply_panels[2];
  GtkWidget *promotion_buttons[2];
  GtkWidget *supply_pile_buttons[2][2];
  GtkWidget *coordinate_row_labels[BOOP_BOARD_SIZE];
  GtkWidget *coordinate_column_labels[BOOP_BOARD_SIZE];
  guint selected_rank[2];
  gulong model_state_changed_handler_id;
} GBoopControlsState;

static const char *gboop_controls_css =
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
    ".board-square .board-square-index { opacity: 0; }"
    ".square-index { background-color: rgba(0,0,0,0.65); color: #fff; font-size: 9px; }"
    ".boop-coordinate { margin: 0 2px; }";

static const int gboop_controls_board_border_padding = 8;
static const int gboop_controls_coordinate_track_size = 12;
static const int gboop_controls_board_square_spacing = 1;

static guint gboop_controls_pile_index_to_rank(guint pile_index) {
  return pile_index == 1 ? BOOP_PIECE_RANK_CAT : BOOP_PIECE_RANK_KITTEN;
}

static const char *gboop_controls_supply_symbol(guint side, guint rank) {
  if (side == 0) {
    return rank == BOOP_PIECE_RANK_CAT ? "c" : "k";
  }

  return rank == BOOP_PIECE_RANK_CAT ? "C" : "K";
}

static guint gboop_controls_supply_count(const BoopPosition *position, guint side, guint rank) {
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

static gboolean gboop_controls_supply_rank_available(const BoopPosition *position, guint side, guint rank) {
  return gboop_controls_supply_count(position, side, rank) > 0;
}

static const BoopMoveBuilderState *gboop_controls_peek_move_builder_state(GBoopControlsState *state) {
  g_return_val_if_fail(state != NULL, NULL);
  g_return_val_if_fail(BOARD_IS_VIEW(state->board_view), NULL);

  const GameBackendMoveBuilder *builder = board_view_peek_move_builder(state->board_view);
  if (builder == NULL || builder->builder_state == NULL ||
      builder->builder_state_size != sizeof(BoopMoveBuilderState)) {
    return NULL;
  }

  return builder->builder_state;
}

static gboolean gboop_controls_promotion_selection_visible(GBoopControlsState *state,
                                                           const BoopPosition *position,
                                                           guint side) {
  const BoopMoveBuilderState *builder_state = NULL;

  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(side <= 1, FALSE);

  if (position->outcome != GAME_BACKEND_OUTCOME_ONGOING || side != position->turn) {
    return FALSE;
  }

  builder_state = gboop_controls_peek_move_builder_state(state);
  if (builder_state == NULL) {
    return FALSE;
  }

  return builder_state->stage == BOOP_MOVE_BUILDER_STAGE_PROMOTION ||
         board_view_move_selection_completion_pending(state->board_view);
}

static gboolean gboop_controls_defer_promotion_confirmation(const GameBackendMoveBuilder *builder,
                                                            gconstpointer move,
                                                            gpointer /*user_data*/) {
  const BoopMoveBuilderState *builder_state = NULL;

  g_return_val_if_fail(builder != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  builder_state = builder->builder_state;
  g_return_val_if_fail(builder_state != NULL, FALSE);
  if (builder->builder_state_size != sizeof(BoopMoveBuilderState)) {
    g_debug("Unexpected boop move builder state size");
    return FALSE;
  }

  return builder_state->stage == BOOP_MOVE_BUILDER_STAGE_COMPLETE && builder_state->promotion_option_count > 0;
}

static void gboop_controls_ensure_selected_rank(GBoopControlsState *state,
                                                const BoopPosition *position,
                                                guint side) {
  g_return_if_fail(state != NULL);
  g_return_if_fail(position != NULL);
  g_return_if_fail(side <= 1);

  if (gboop_controls_supply_rank_available(position, side, state->selected_rank[side])) {
    return;
  }

  if (gboop_controls_supply_rank_available(position, side, BOOP_PIECE_RANK_KITTEN)) {
    state->selected_rank[side] = BOOP_PIECE_RANK_KITTEN;
  } else if (gboop_controls_supply_rank_available(position, side, BOOP_PIECE_RANK_CAT)) {
    state->selected_rank[side] = BOOP_PIECE_RANK_CAT;
  } else {
    state->selected_rank[side] = BOOP_PIECE_RANK_NONE;
  }
}

static GtkWidget *gboop_controls_build_supply_pile(guint side, guint rank, guint count) {
  GtkWidget *fixed = gtk_fixed_new();

  gtk_widget_set_size_request(fixed, 88, 88);

  for (guint i = 0; i < count; ++i) {
    GtkWidget *piece = gtk_label_new(gboop_controls_supply_symbol(side, rank));
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

static void gboop_controls_update_supply(GBoopControlsState *state) {
  const BoopPosition *position = NULL;
  gboolean has_active_supply = FALSE;

  g_return_if_fail(state != NULL);
  g_return_if_fail(GGAME_IS_MODEL(state->model));

  position = ggame_model_peek_position(state->model);
  g_return_if_fail(position != NULL);
  g_return_if_fail(position->turn <= 1);

  has_active_supply = position->outcome == GAME_BACKEND_OUTCOME_ONGOING;

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
      gboop_controls_ensure_selected_rank(state, position, side);
    }

    for (guint pile_index = 0; pile_index < 2; ++pile_index) {
      guint rank = gboop_controls_pile_index_to_rank(pile_index);
      guint count = gboop_controls_supply_count(position, side, rank);
      GtkWidget *button = state->supply_pile_buttons[side][pile_index];

      if (button == NULL) {
        continue;
      }

      gtk_button_set_child(GTK_BUTTON(button), gboop_controls_build_supply_pile(side, rank, count));
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
      gboolean promotion_visible = gboop_controls_promotion_selection_visible(state, position, side);
      gtk_widget_set_visible(state->promotion_buttons[side], promotion_visible);
      gtk_widget_set_sensitive(state->promotion_buttons[side],
                               promotion_visible && board_view_move_selection_completion_pending(state->board_view));
    }
  }
}

static char gboop_controls_column_coordinate(guint display_col, guint bottom_side) {
  guint logical_col = 0;

  g_return_val_if_fail(display_col < BOOP_BOARD_SIZE, 'a');

  logical_col = display_col;
  if (bottom_side != 0) {
    logical_col = BOOP_BOARD_SIZE - 1 - display_col;
  }

  return (char)('a' + logical_col);
}

static guint gboop_controls_row_coordinate(guint display_row, guint bottom_side) {
  guint logical_row = 0;

  g_return_val_if_fail(display_row < BOOP_BOARD_SIZE, 1);

  logical_row = BOOP_BOARD_SIZE - 1 - display_row;
  if (bottom_side != 0) {
    logical_row = display_row;
  }

  return logical_row + 1;
}

static void gboop_controls_update_coordinates(GBoopControlsState *state) {
  guint bottom_side = 0;

  g_return_if_fail(state != NULL);
  g_return_if_fail(BOARD_IS_VIEW(state->board_view));

  bottom_side = board_view_get_bottom_side(state->board_view);
  for (guint i = 0; i < BOOP_BOARD_SIZE; ++i) {
    char column_label[2] = {0};
    char row_label[8] = {0};

    if (GTK_IS_LABEL(state->coordinate_column_labels[i])) {
      column_label[0] = gboop_controls_column_coordinate(i, bottom_side);
      gtk_label_set_text(GTK_LABEL(state->coordinate_column_labels[i]), column_label);
    }

    if (GTK_IS_LABEL(state->coordinate_row_labels[i])) {
      g_snprintf(row_label, sizeof(row_label), "%u", gboop_controls_row_coordinate(i, bottom_side));
      gtk_label_set_text(GTK_LABEL(state->coordinate_row_labels[i]), row_label);
    }
  }
}

static void gboop_controls_on_board_view_bottom_side_changed(gpointer user_data) {
  GBoopControlsState *state = user_data;

  g_return_if_fail(state != NULL);

  gboop_controls_update_coordinates(state);
}

static gboolean gboop_controls_prefer_selected_supply_candidate(gconstpointer move, gpointer user_data) {
  const BoopMove *boop_move = move;
  GBoopControlsState *state = user_data;
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

static void gboop_controls_on_supply_pile_clicked(GtkButton *button, gpointer user_data) {
  GBoopControlsState *state = user_data;
  const BoopPosition *position = NULL;
  guint side = 0;
  guint rank = 0;

  g_return_if_fail(GTK_IS_BUTTON(button));
  g_return_if_fail(state != NULL);

  side = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "boop-side")) - 1;
  rank = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "boop-rank"));
  position = ggame_model_peek_position(state->model);
  g_return_if_fail(position != NULL);
  g_return_if_fail(side <= 1);

  if (position->outcome != GAME_BACKEND_OUTCOME_ONGOING || side != position->turn) {
    return;
  }

  if (!gboop_controls_supply_rank_available(position, side, rank)) {
    return;
  }

  state->selected_rank[side] = rank;
  board_view_clear_selection(state->board_view);
  gboop_controls_update_supply(state);
}

static void gboop_controls_on_confirm_promotions_clicked(GtkButton *button, gpointer user_data) {
  GBoopControlsState *state = user_data;

  g_return_if_fail(GTK_IS_BUTTON(button));
  g_return_if_fail(state != NULL);

  if (!board_view_confirm_move_selection(state->board_view)) {
    g_debug("Failed to confirm boop promotion selection");
  }
}

static void gboop_controls_on_board_selection_changed(gpointer user_data) {
  GBoopControlsState *state = user_data;

  g_return_if_fail(state != NULL);
  gboop_controls_update_supply(state);
}

static void gboop_controls_on_model_state_changed(GGameModel *model, gpointer user_data) {
  GBoopControlsState *state = user_data;

  g_return_if_fail(GGAME_IS_MODEL(model));
  g_return_if_fail(state != NULL);

  gboop_controls_update_supply(state);
}

static GtkWidget *gboop_controls_create_supply_panel(GBoopControlsState *state, guint side) {
  GtkWidget *panel = NULL;

  g_return_val_if_fail(state != NULL, NULL);
  g_return_val_if_fail(side <= 1, NULL);

  panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(panel, "boop-supply-panel");
  gtk_widget_set_valign(panel, GTK_ALIGN_CENTER);
  g_object_set_data(G_OBJECT(panel), "boop-side", GUINT_TO_POINTER(side + 1));
  state->supply_panels[side] = panel;

  GtkWidget *title = gtk_label_new(side == 0 ? "Player 1" : "Player 2");
  gtk_widget_add_css_class(title, "boop-supply-title");
  gtk_box_append(GTK_BOX(panel), title);

  for (guint pile_index = 0; pile_index < 2; ++pile_index) {
    guint rank = gboop_controls_pile_index_to_rank(pile_index);
    GtkWidget *button = gtk_button_new();

    gtk_widget_add_css_class(button, "boop-pile-button");
    gtk_widget_set_size_request(button, 96, 96);
    g_object_set_data(G_OBJECT(button), "boop-side", GUINT_TO_POINTER(side + 1));
    g_object_set_data(G_OBJECT(button), "boop-rank", GUINT_TO_POINTER(rank));
    g_signal_connect(button, "clicked", G_CALLBACK(gboop_controls_on_supply_pile_clicked), state);
    state->supply_pile_buttons[side][pile_index] = button;
    gtk_box_append(GTK_BOX(panel), button);
  }

  GtkWidget *promotion_button = gtk_button_new_with_label("Confirm promotions");
  gtk_widget_add_css_class(promotion_button, "boop-promotion-button");
  gtk_widget_set_visible(promotion_button, FALSE);
  gtk_widget_set_sensitive(promotion_button, FALSE);
  g_signal_connect(promotion_button, "clicked", G_CALLBACK(gboop_controls_on_confirm_promotions_clicked), state);
  state->promotion_buttons[side] = promotion_button;
  gtk_box_append(GTK_BOX(panel), promotion_button);

  return panel;
}

static GtkWidget *gboop_controls_create_coordinate_label(const char *data_key, guint ordinal) {
  GtkWidget *label = gtk_label_new(NULL);

  gtk_label_set_xalign(GTK_LABEL(label), 0.5f);
  gtk_widget_set_hexpand(label, TRUE);
  gtk_widget_set_vexpand(label, TRUE);
  gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_can_target(label, FALSE);
  gtk_widget_add_css_class(label, "square-index");
  gtk_widget_add_css_class(label, "boop-coordinate");
  g_object_set_data(G_OBJECT(label), data_key, GUINT_TO_POINTER(ordinal));

  return label;
}

static GtkWidget *gboop_controls_create_board_frame(GBoopControlsState *state, BoardView *board_view) {
  GtkWidget *board_root = NULL;
  GtkWidget *row_box = NULL;
  GtkWidget *column_box = NULL;
  GtkWidget *board_aspect = NULL;

  g_return_val_if_fail(state != NULL, NULL);
  g_return_val_if_fail(BOARD_IS_VIEW(board_view), NULL);
  board_root = board_view_get_widget(board_view);
  g_return_val_if_fail(GTK_IS_OVERLAY(board_root), NULL);

  row_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_size_request(row_box, gboop_controls_coordinate_track_size, -1);
  gtk_widget_set_vexpand(row_box, TRUE);
  gtk_widget_set_halign(row_box, GTK_ALIGN_START);
  gtk_widget_set_valign(row_box, GTK_ALIGN_FILL);
  gtk_widget_set_margin_start(row_box, 0);
  gtk_widget_set_margin_top(row_box, gboop_controls_board_border_padding);
  gtk_widget_set_margin_bottom(row_box, gboop_controls_board_border_padding);
  gtk_widget_set_can_target(row_box, FALSE);
  gtk_box_set_spacing(GTK_BOX(row_box), gboop_controls_board_square_spacing);
  gtk_box_set_homogeneous(GTK_BOX(row_box), TRUE);
  for (guint i = 0; i < BOOP_BOARD_SIZE; ++i) {
    GtkWidget *label = gboop_controls_create_coordinate_label("boop-coordinate-row", i + 1);
    state->coordinate_row_labels[i] = label;
    gtk_box_append(GTK_BOX(row_box), label);
  }

  board_aspect = gtk_aspect_frame_new(0.5f, 0.5f, 1.0f, FALSE);
  gtk_widget_set_hexpand(board_aspect, TRUE);
  gtk_widget_set_vexpand(board_aspect, TRUE);
  gtk_aspect_frame_set_child(GTK_ASPECT_FRAME(board_aspect), board_root);

  column_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_size_request(column_box, -1, gboop_controls_coordinate_track_size);
  gtk_widget_set_hexpand(column_box, TRUE);
  gtk_widget_set_halign(column_box, GTK_ALIGN_FILL);
  gtk_widget_set_valign(column_box, GTK_ALIGN_END);
  gtk_widget_set_margin_start(column_box, gboop_controls_board_border_padding);
  gtk_widget_set_margin_end(column_box, gboop_controls_board_border_padding);
  gtk_widget_set_margin_bottom(column_box, 0);
  gtk_widget_set_can_target(column_box, FALSE);
  gtk_box_set_spacing(GTK_BOX(column_box), gboop_controls_board_square_spacing);
  gtk_box_set_homogeneous(GTK_BOX(column_box), TRUE);
  for (guint i = 0; i < BOOP_BOARD_SIZE; ++i) {
    GtkWidget *label = gboop_controls_create_coordinate_label("boop-coordinate-column", i + 1);
    state->coordinate_column_labels[i] = label;
    gtk_box_append(GTK_BOX(column_box), label);
  }
  gtk_overlay_add_overlay(GTK_OVERLAY(board_root), row_box);
  gtk_overlay_add_overlay(GTK_OVERLAY(board_root), column_box);
  gtk_overlay_set_measure_overlay(GTK_OVERLAY(board_root), row_box, FALSE);
  gtk_overlay_set_measure_overlay(GTK_OVERLAY(board_root), column_box, FALSE);
  gtk_overlay_set_clip_overlay(GTK_OVERLAY(board_root), row_box, FALSE);
  gtk_overlay_set_clip_overlay(GTK_OVERLAY(board_root), column_box, FALSE);

  return board_aspect;
}

static void gboop_controls_state_free(gpointer data) {
  GBoopControlsState *state = data;

  if (state == NULL) {
    return;
  }

  if (state->model != NULL && state->model_state_changed_handler_id != 0) {
    g_signal_handler_disconnect(state->model, state->model_state_changed_handler_id);
  }
  if (state->board_view != NULL) {
    board_view_set_move_candidate_preference(state->board_view, NULL, NULL);
    board_view_set_move_completion_confirmation(state->board_view, NULL, NULL);
    board_view_set_selection_changed_handler(state->board_view, NULL, NULL);
    board_view_set_bottom_side_changed_handler(state->board_view, NULL, NULL);
  }

  g_clear_object(&state->model);
  g_clear_object(&state->board_view);
  g_free(state);
}

static void gboop_controls_install_css(void) {
  static gsize initialized = 0;

  if (!g_once_init_enter(&initialized)) {
    return;
  }

  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(provider, gboop_controls_css);

  GdkDisplay *display = gdk_display_get_default();
  if (display == NULL) {
    g_debug("Failed to fetch default display for boop CSS");
    g_object_unref(provider);
    g_once_init_leave(&initialized, 1);
    return;
  }

  gtk_style_context_add_provider_for_display(display,
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
  g_object_unref(provider);
  g_once_init_leave(&initialized, 1);
}

GtkWidget *gboop_controls_create_board_host(GGameModel *model, BoardView *board_view) {
  GBoopControlsState *state = NULL;
  GtkWidget *host = NULL;
  GtkWidget *board_frame = NULL;

  g_return_val_if_fail(GGAME_IS_MODEL(model), NULL);
  g_return_val_if_fail(BOARD_IS_VIEW(board_view), NULL);

  gboop_controls_install_css();

  state = g_new0(GBoopControlsState, 1);
  g_return_val_if_fail(state != NULL, NULL);

  state->model = g_object_ref(model);
  state->board_view = g_object_ref(board_view);
  state->selected_rank[0] = BOOP_PIECE_RANK_KITTEN;
  state->selected_rank[1] = BOOP_PIECE_RANK_KITTEN;

  host = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
  gtk_widget_add_css_class(host, "boop-play-area");
  gtk_widget_set_hexpand(host, TRUE);
  gtk_widget_set_vexpand(host, TRUE);
  g_object_set_data_full(G_OBJECT(host), "boop-controls-state", state, gboop_controls_state_free);

  GtkWidget *side0_panel = gboop_controls_create_supply_panel(state, 0);
  GtkWidget *side1_panel = gboop_controls_create_supply_panel(state, 1);
  g_object_set_data(G_OBJECT(host), "boop-supply-panel-0", side0_panel);
  g_object_set_data(G_OBJECT(host), "boop-supply-panel-1", side1_panel);
  g_object_set_data(G_OBJECT(host), "boop-promotion-button-0", state->promotion_buttons[0]);
  g_object_set_data(G_OBJECT(host), "boop-promotion-button-1", state->promotion_buttons[1]);
  gtk_box_append(GTK_BOX(host), side0_panel);

  board_frame = gboop_controls_create_board_frame(state, board_view);
  gboop_controls_update_coordinates(state);
  gtk_box_append(GTK_BOX(host), board_frame);

  gtk_box_append(GTK_BOX(host), side1_panel);

  board_view_set_move_candidate_preference(board_view, gboop_controls_prefer_selected_supply_candidate, state);
  board_view_set_move_completion_confirmation(board_view, gboop_controls_defer_promotion_confirmation, state);
  board_view_set_selection_changed_handler(board_view, gboop_controls_on_board_selection_changed, state);

  state->model_state_changed_handler_id = g_signal_connect(state->model,
                                                           "state-changed",
                                                           G_CALLBACK(gboop_controls_on_model_state_changed),
                                                           state);
  board_view_set_bottom_side_changed_handler(state->board_view,
                                             gboop_controls_on_board_view_bottom_side_changed,
                                             state);
  gboop_controls_update_supply(state);

  return host;
}
