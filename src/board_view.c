#include "board_view.h"

#include "board_grid.h"
#include "board_move_overlay.h"
#include "board_selection_controller.h"
#include "games/checkers/checkers_model.h"
#include "piece_palette.h"
#include "sgf_controller.h"
#include "widget_utils.h"

#include <stdbool.h>

struct _BoardView {
  GObject parent_instance;
  GGameModel *model;
  gulong model_state_changed_handler_id;
  GtkWidget *root;
  BoardGrid *board_grid;
  BoardMoveOverlay *board_overlay;
  BoardSelectionController *selection_controller;
  PiecePalette *piece_palette;
  BoardViewMoveHandler move_handler;
  gpointer move_handler_data;
  BoardViewSelectionChangedHandler selection_changed_handler;
  gpointer selection_changed_handler_data;
  BoardViewBottomSideChangedHandler bottom_side_changed_handler;
  gpointer bottom_side_changed_handler_data;
  BoardViewSquareHandler square_handler;
  gpointer square_handler_data;
  gboolean input_enabled;
  guint bottom_side;
};

G_DEFINE_TYPE(BoardView, board_view, G_TYPE_OBJECT)

static const int board_view_square_size = 31;

static void board_view_disconnect_model(BoardView *self) {
  g_return_if_fail(BOARD_IS_VIEW(self));

  if (self->model != NULL && self->model_state_changed_handler_id != 0) {
    g_signal_handler_disconnect(self->model, self->model_state_changed_handler_id);
    self->model_state_changed_handler_id = 0;
  }
}

static void board_view_notify_selection_changed(BoardView *self) {
  g_return_if_fail(BOARD_IS_VIEW(self));

  if (self->selection_changed_handler != NULL) {
    self->selection_changed_handler(self->selection_changed_handler_data);
  }
}

static gconstpointer board_view_get_display_position(BoardView *self, gconstpointer model_position) {
  const GameBackend *backend = NULL;
  const GameBackendMoveBuilder *builder = NULL;
  gconstpointer preview_position = NULL;

  g_return_val_if_fail(BOARD_IS_VIEW(self), model_position);
  g_return_val_if_fail(model_position != NULL, NULL);
  g_return_val_if_fail(GGAME_IS_MODEL(self->model), model_position);

  backend = ggame_model_peek_backend(self->model);
  if (backend == NULL || backend->move_builder_preview_position == NULL) {
    return model_position;
  }

  builder = board_selection_controller_peek_builder(self->selection_controller);
  if (builder == NULL) {
    return model_position;
  }

  preview_position = backend->move_builder_preview_position(builder);
  return preview_position != NULL ? preview_position : model_position;
}

static void board_view_on_model_state_changed(GGameModel *model, gpointer user_data) {
  BoardView *self = BOARD_VIEW(user_data);

  g_return_if_fail(BOARD_IS_VIEW(self));
  g_return_if_fail(GGAME_IS_MODEL(model));

  if (self->model != model) {
    g_debug("Ignoring state change from a stale board view model");
    return;
  }

  board_view_update(self);
}

static void board_view_update_board(BoardView *self, gconstpointer position) {
  const GameBackend *backend = NULL;
  GameBackendMoveList moves = {0};
  gboolean moves_loaded = FALSE;
  gboolean highlight_moves = FALSE;
  gboolean playable_starts[128] = {FALSE};
  gboolean possible_destinations[128] = {FALSE};

  g_return_if_fail(BOARD_IS_VIEW(self));
  g_return_if_fail(GGAME_IS_MODEL(self->model));
  g_return_if_fail(position != NULL);

  backend = ggame_model_peek_backend(self->model);
  g_return_if_fail(backend != NULL);
  g_return_if_fail(backend->position_outcome != NULL);
  g_return_if_fail(backend->square_grid_piece_view != NULL);

  if (backend->position_outcome(position) == GAME_BACKEND_OUTCOME_ONGOING && backend->supports_move_builder) {
    highlight_moves = board_selection_controller_collect_highlights(self->selection_controller,
                                                                    playable_starts,
                                                                    possible_destinations,
                                                                    G_N_ELEMENTS(playable_starts));
  } else if (backend->position_outcome(position) == GAME_BACKEND_OUTCOME_ONGOING && backend->supports_move_list) {
    moves = ggame_model_list_moves(self->model);
    moves_loaded = TRUE;
    highlight_moves = moves.count > 0;
    if (highlight_moves) {
      guint selection_length = 0;
      const guint *selection = board_selection_controller_peek_path(self->selection_controller, &selection_length);

      g_return_if_fail(backend->square_grid_moves_collect_starts != NULL);
      g_return_if_fail(backend->square_grid_moves_collect_next_destinations != NULL);
      backend->square_grid_moves_collect_starts(&moves, playable_starts, G_N_ELEMENTS(playable_starts));
      if (selection_length > 0) {
        backend->square_grid_moves_collect_next_destinations(&moves,
                                                             selection,
                                                             selection_length,
                                                             possible_destinations,
                                                             G_N_ELEMENTS(possible_destinations));
      }
    }
  }

  guint rows = backend->square_grid_rows(position);
  guint cols = backend->square_grid_cols(position);
  for (guint row = 0; row < rows; ++row) {
    for (guint col = 0; col < cols; ++col) {
      guint index = 0;
      GameBackendSquarePieceView piece = {0};

      if (!backend->square_grid_square_playable(position, row, col) ||
          !backend->square_grid_square_index(position, row, col, &index)) {
        continue;
      }

      BoardSquare *square = board_grid_get_square(self->board_grid, index);
      if (square == NULL) {
        continue;
      }

      if (!backend->square_grid_piece_view(position, index, &piece)) {
        g_debug("Failed to get square piece view");
        continue;
      }

      board_square_set_piece(square, &piece, self->piece_palette);

      gboolean is_selected = board_selection_controller_contains(self->selection_controller, index);
      gboolean is_selectable = highlight_moves && index < G_N_ELEMENTS(playable_starts) && playable_starts[index];
      gboolean is_destination =
          highlight_moves && index < G_N_ELEMENTS(possible_destinations) && possible_destinations[index];
      board_square_set_highlight(square, is_selected, is_selectable, is_destination);
    }
  }

  if (moves_loaded) {
    backend->move_list_free(&moves);
  }
}

static void board_view_update_sensitivity(BoardView *self, gconstpointer position) {
  const GameBackend *backend = NULL;
  gboolean can_play = FALSE;
  guint rows = 0;
  guint cols = 0;

  g_return_if_fail(BOARD_IS_VIEW(self));
  g_return_if_fail(position != NULL);

  backend = ggame_model_peek_backend(self->model);
  g_return_if_fail(backend != NULL);

  can_play = backend->position_outcome(position) == GAME_BACKEND_OUTCOME_ONGOING && self->input_enabled;
  rows = backend->square_grid_rows(position);
  cols = backend->square_grid_cols(position);
  for (guint row = 0; row < rows; ++row) {
    for (guint col = 0; col < cols; ++col) {
      guint index = 0;

      if (!backend->square_grid_square_playable(position, row, col) ||
          !backend->square_grid_square_index(position, row, col, &index)) {
        continue;
      }

      BoardSquare *square = board_grid_get_square(self->board_grid, index);
      if (square != NULL) {
        gtk_widget_set_sensitive(board_square_get_widget(square), can_play);
      }
    }
  }
}

static gboolean board_view_handle_square_action(BoardView *self, GtkWidget *square_widget, guint button) {
  const GameBackend *backend = NULL;
  gconstpointer position = NULL;

  g_return_val_if_fail(BOARD_IS_VIEW(self), FALSE);
  g_return_val_if_fail(GGAME_IS_MODEL(self->model), FALSE);
  g_return_val_if_fail(GTK_IS_WIDGET(square_widget), FALSE);

  backend = ggame_model_peek_backend(self->model);
  position = ggame_model_peek_position(self->model);
  if (backend == NULL || position == NULL) {
    return FALSE;
  }

  gpointer data = g_object_get_data(G_OBJECT(square_widget), "board-index");
  if (data == NULL) {
    g_debug("Missing board index for square action");
    return FALSE;
  }

  gint index = GPOINTER_TO_INT(data) - 1;
  if (index < 0) {
    g_debug("Invalid board index for square action");
    return FALSE;
  }

  if (self->square_handler != NULL && self->square_handler((guint) index, button, self->square_handler_data)) {
    board_selection_controller_clear(self->selection_controller);
    board_view_update(self);
    board_view_notify_selection_changed(self);
    return TRUE;
  }

  if (button != GDK_BUTTON_PRIMARY ||
      backend->position_outcome(position) != GAME_BACKEND_OUTCOME_ONGOING ||
      !self->input_enabled) {
    return FALSE;
  }

  if (board_selection_controller_handle_click(self->selection_controller, (guint) index)) {
    board_view_update(self);
    board_view_notify_selection_changed(self);
    return TRUE;
  }

  return FALSE;
}

static void board_view_on_square_clicked(GtkButton *button, gpointer user_data) {
  BoardView *self = BOARD_VIEW(user_data);

  g_return_if_fail(BOARD_IS_VIEW(self));
  g_return_if_fail(GTK_IS_BUTTON(button));

  board_view_handle_square_action(self, GTK_WIDGET(button), GDK_BUTTON_PRIMARY);
}

static void board_view_on_square_secondary_pressed(GtkGestureClick *gesture,
                                                   gint n_press,
                                                   gdouble /*x*/,
                                                   gdouble /*y*/,
                                                   gpointer user_data) {
  BoardView *self = BOARD_VIEW(user_data);

  g_return_if_fail(BOARD_IS_VIEW(self));
  g_return_if_fail(GTK_IS_GESTURE_CLICK(gesture));

  if (n_press != 1) {
    return;
  }

  GtkWidget *square_widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
  g_return_if_fail(GTK_IS_WIDGET(square_widget));

  board_view_handle_square_action(self, square_widget, GDK_BUTTON_SECONDARY);
}

static void board_view_build_board(BoardView *self) {
  gconstpointer position = NULL;
  const GameBackend *backend = NULL;

  g_return_if_fail(BOARD_IS_VIEW(self));
  g_return_if_fail(GGAME_IS_MODEL(self->model));

  backend = ggame_model_peek_backend(self->model);
  position = ggame_model_peek_position(self->model);
  if (backend == NULL || position == NULL) {
    return;
  }

  g_return_if_fail(backend->supports_square_grid_board);

  board_grid_build(self->board_grid,
                   backend,
                   position,
                   self->bottom_side,
                   board_view_on_square_clicked,
                   board_view_on_square_secondary_pressed,
                   self);
}

GtkWidget *board_view_get_widget(BoardView *self) {
  g_return_val_if_fail(BOARD_IS_VIEW(self), NULL);

  return self->root;
}

void board_view_set_model(BoardView *self, gpointer model) {
  const GameBackend *backend = NULL;
  GGameModel *game_model = NULL;

  g_return_if_fail(BOARD_IS_VIEW(self));
  g_return_if_fail(model != NULL);

  if (GGAME_IS_MODEL(model)) {
    game_model = GGAME_MODEL(model);
  } else if (GCHECKERS_IS_MODEL(model)) {
    game_model = gcheckers_model_peek_game_model((GCheckersModel *) model);
  }

  g_return_if_fail(GGAME_IS_MODEL(game_model));
  board_view_disconnect_model(self);
  g_set_object(&self->model, game_model);

  backend = ggame_model_peek_backend(self->model);
  g_return_if_fail(backend != NULL);
  g_return_if_fail(backend->supports_square_grid_board);

  board_selection_controller_set_model(self->selection_controller, self->model);
  board_move_overlay_set_model(self->board_overlay, self->model);
  self->model_state_changed_handler_id = g_signal_connect(self->model,
                                                          "state-changed",
                                                          G_CALLBACK(board_view_on_model_state_changed),
                                                          self);
  board_view_build_board(self);
  board_view_update(self);
}

void board_view_set_sgf_controller(BoardView *self, GGameSgfController *controller) {
  g_return_if_fail(BOARD_IS_VIEW(self));
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(controller));

  board_move_overlay_set_sgf_controller(self->board_overlay, controller);
}

void board_view_set_move_handler(BoardView *self, gpointer handler, gpointer user_data) {
  g_return_if_fail(BOARD_IS_VIEW(self));

  self->move_handler = (BoardViewMoveHandler) handler;
  self->move_handler_data = user_data;
  board_selection_controller_set_move_handler(self->selection_controller,
                                              self->move_handler,
                                              self->move_handler_data);
}

void board_view_set_move_candidate_preference(BoardView *self,
                                              BoardViewMoveCandidatePreference preference,
                                              gpointer user_data) {
  g_return_if_fail(BOARD_IS_VIEW(self));

  board_selection_controller_set_candidate_preference(self->selection_controller,
                                                      preference,
                                                      user_data);
}

void board_view_set_move_completion_confirmation(BoardView *self,
                                                 BoardViewMoveCompletionConfirmation confirmation,
                                                 gpointer user_data) {
  g_return_if_fail(BOARD_IS_VIEW(self));

  board_selection_controller_set_completion_confirmation(self->selection_controller,
                                                         (BoardSelectionControllerCompletionConfirmation)confirmation,
                                                         user_data);
}

void board_view_set_selection_changed_handler(BoardView *self,
                                              BoardViewSelectionChangedHandler handler,
                                              gpointer user_data) {
  g_return_if_fail(BOARD_IS_VIEW(self));

  self->selection_changed_handler = handler;
  self->selection_changed_handler_data = user_data;
}

void board_view_set_square_handler(BoardView *self, gpointer handler, gpointer user_data) {
  g_return_if_fail(BOARD_IS_VIEW(self));

  self->square_handler = (BoardViewSquareHandler) handler;
  self->square_handler_data = user_data;
}

void board_view_update(BoardView *self) {
  const GameBackend *backend = NULL;
  gconstpointer position = NULL;
  gconstpointer display_position = NULL;

  g_return_if_fail(BOARD_IS_VIEW(self));

  if (self->model == NULL) {
    g_debug("Missing model while updating board view");
    return;
  }

  backend = ggame_model_peek_backend(self->model);
  position = ggame_model_peek_position(self->model);
  if (backend == NULL || position == NULL) {
    g_debug("Failed to fetch backend state for board update");
    return;
  }

  display_position = board_view_get_display_position(self, position);
  g_return_if_fail(display_position != NULL);

  if (board_grid_get_board_size(self->board_grid) != backend->square_grid_rows(display_position)) {
    board_selection_controller_clear(self->selection_controller);
    board_view_build_board(self);
  }

  board_view_update_board(self, display_position);
  board_view_update_sensitivity(self, display_position);
  board_move_overlay_queue_draw(self->board_overlay);
}

void board_view_clear_selection(BoardView *self) {
  g_return_if_fail(BOARD_IS_VIEW(self));

  board_selection_controller_clear(self->selection_controller);
  board_view_update(self);
  board_view_notify_selection_changed(self);
}

const GameBackendMoveBuilder *board_view_peek_move_builder(BoardView *self) {
  g_return_val_if_fail(BOARD_IS_VIEW(self), NULL);

  return board_selection_controller_peek_builder(self->selection_controller);
}

gboolean board_view_move_selection_completion_pending(BoardView *self) {
  g_return_val_if_fail(BOARD_IS_VIEW(self), FALSE);

  return board_selection_controller_completion_pending(self->selection_controller);
}

gboolean board_view_confirm_move_selection(BoardView *self) {
  gboolean confirmed = FALSE;

  g_return_val_if_fail(BOARD_IS_VIEW(self), FALSE);

  confirmed = board_selection_controller_confirm(self->selection_controller);
  board_view_update(self);
  board_view_notify_selection_changed(self);
  return confirmed;
}

void board_view_set_input_enabled(BoardView *self, gboolean enabled) {
  g_return_if_fail(BOARD_IS_VIEW(self));

  self->input_enabled = enabled;
  if (self->model != NULL) {
    board_view_update_sensitivity(self, ggame_model_peek_position(self->model));
  }
}

void board_view_set_bottom_side_changed_handler(BoardView *self,
                                                BoardViewBottomSideChangedHandler handler,
                                                gpointer user_data) {
  g_return_if_fail(BOARD_IS_VIEW(self));

  self->bottom_side_changed_handler = handler;
  self->bottom_side_changed_handler_data = user_data;
}

static void board_view_dispose(GObject *object) {
  BoardView *self = BOARD_VIEW(object);
  gboolean root_removed = TRUE;

  board_view_disconnect_model(self);
  if (self->root != NULL) {
    root_removed = ggame_widget_remove_from_parent(self->root);
    if (!root_removed && gtk_widget_get_parent(self->root) != NULL) {
      g_debug("Failed to remove board view root from parent during dispose");
    }
  }

  g_clear_object(&self->model);
  g_clear_object(&self->piece_palette);
  g_clear_object(&self->selection_controller);
  g_clear_object(&self->board_overlay);
  g_clear_object(&self->board_grid);
  if (root_removed) {
    g_clear_object(&self->root);
  } else {
    self->root = NULL;
  }

  G_OBJECT_CLASS(board_view_parent_class)->dispose(object);
}

static void board_view_class_init(BoardViewClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = board_view_dispose;
}

static void board_view_init(BoardView *self) {
  self->root = gtk_overlay_new();
  g_object_ref_sink(self->root);
  gtk_widget_set_hexpand(self->root, TRUE);
  gtk_widget_set_vexpand(self->root, TRUE);

  self->board_grid = board_grid_new(board_view_square_size);
  gtk_overlay_set_child(GTK_OVERLAY(self->root), board_grid_get_widget(self->board_grid));

  self->board_overlay = board_move_overlay_new();
  gtk_overlay_add_overlay(GTK_OVERLAY(self->root), board_move_overlay_get_widget(self->board_overlay));

  self->selection_controller = board_selection_controller_new();
  self->piece_palette = piece_palette_new_default();
  self->move_handler = NULL;
  self->move_handler_data = NULL;
  self->square_handler = NULL;
  self->square_handler_data = NULL;
  self->bottom_side_changed_handler = NULL;
  self->bottom_side_changed_handler_data = NULL;
  self->input_enabled = TRUE;
  self->bottom_side = 0;
  self->model_state_changed_handler_id = 0;
}

BoardView *board_view_new(void) {
  return g_object_new(BOARD_TYPE_VIEW, NULL);
}

void board_view_set_bottom_side(BoardView *self, guint bottom_side) {
  g_return_if_fail(BOARD_IS_VIEW(self));

  if (self->bottom_side == bottom_side) {
    return;
  }

  self->bottom_side = bottom_side;
  board_move_overlay_set_bottom_side(self->board_overlay, bottom_side);

  if (self->model != NULL) {
    board_view_build_board(self);
    board_view_update(self);
  }

  if (self->bottom_side_changed_handler != NULL) {
    self->bottom_side_changed_handler(self->bottom_side_changed_handler_data);
  }
}

guint board_view_get_bottom_side(BoardView *self) {
  g_return_val_if_fail(BOARD_IS_VIEW(self), 0);

  return self->bottom_side;
}

void board_view_set_banner_text(BoardView *self, const char *text) {
  g_return_if_fail(BOARD_IS_VIEW(self));

  board_move_overlay_set_banner(self->board_overlay, text, BOARD_MOVE_OVERLAY_BANNER_COLOR_DEFAULT);
}

void board_view_set_banner_text_red(BoardView *self, const char *text) {
  g_return_if_fail(BOARD_IS_VIEW(self));

  board_move_overlay_set_banner(self->board_overlay, text, BOARD_MOVE_OVERLAY_BANNER_COLOR_RED);
}
