#include "board_view.h"

#include "board_grid.h"
#include "board_move_overlay.h"
#include "board_selection_controller.h"
#include "piece_palette.h"
#include "widget_utils.h"

#include "board.h"

#include <stdbool.h>

struct _BoardView {
  GObject parent_instance;
  GCheckersModel *model;
  GtkWidget *root;
  BoardGrid *board_grid;
  BoardMoveOverlay *board_overlay;
  BoardSelectionController *selection_controller;
  PiecePalette *piece_palette;
  gboolean input_enabled;
};

G_DEFINE_TYPE(BoardView, board_view, G_TYPE_OBJECT)

static const int board_view_square_size = 63;

static void board_view_update_board(BoardView *self, const GameState *state) {
  g_return_if_fail(BOARD_IS_VIEW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));
  g_return_if_fail(state != NULL);

  MoveList moves = {0};
  gboolean moves_loaded = FALSE;
  gboolean highlight_moves = FALSE;
  bool playable_starts[CHECKERS_MAX_SQUARES] = {false};
  bool possible_destinations[CHECKERS_MAX_SQUARES] = {false};
  if (state->winner == CHECKERS_WINNER_NONE) {
    moves = gcheckers_model_list_moves(self->model);
    moves_loaded = TRUE;
    highlight_moves = moves.count > 0;
    if (highlight_moves) {
      game_moves_collect_starts(&moves, playable_starts);
      uint8_t selection_length = 0;
      const uint8_t *selection =
          board_selection_controller_peek_path(self->selection_controller, &selection_length);
      if (selection_length > 0) {
        game_moves_collect_next_destinations(&moves, selection, selection_length, possible_destinations);
      }
    }
  }

  guint board_size = state->board.board_size;
  int max_square = board_playable_squares(board_size);
  for (int idx = 0; idx < max_square; ++idx) {
    BoardSquare *square = board_grid_get_square(self->board_grid, (uint8_t)idx);
    if (!square) {
      continue;
    }

    CheckersPiece piece = board_get(&state->board, (uint8_t)idx);
    board_square_set_piece(square, piece, self->piece_palette);

    gboolean is_selected = board_selection_controller_contains(self->selection_controller, (uint8_t)idx);
    gboolean is_selectable = highlight_moves && playable_starts[idx];
    gboolean is_destination = highlight_moves && possible_destinations[idx];
    board_square_set_highlight(square, is_selected, is_selectable, is_destination);
  }

  if (moves_loaded) {
    movelist_free(&moves);
  }
}

static void board_view_update_sensitivity(BoardView *self, const GameState *state) {
  g_return_if_fail(BOARD_IS_VIEW(self));
  g_return_if_fail(state != NULL);

  gboolean can_play = state->winner == CHECKERS_WINNER_NONE && self->input_enabled;
  int max_square = board_playable_squares(state->board.board_size);
  for (int i = 0; i < max_square; ++i) {
    BoardSquare *square = board_grid_get_square(self->board_grid, (uint8_t)i);
    if (square) {
      gtk_widget_set_sensitive(board_square_get_widget(square), can_play);
    }
  }
}

static void board_view_on_square_clicked(GtkButton *button, gpointer user_data) {
  BoardView *self = BOARD_VIEW(user_data);

  g_return_if_fail(BOARD_IS_VIEW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));
  g_return_if_fail(GTK_IS_BUTTON(button));

  const GameState *state = gcheckers_model_peek_state(self->model);
  if (!state) {
    g_debug("Failed to fetch game state for click\n");
    return;
  }
  if (state->winner != CHECKERS_WINNER_NONE) {
    g_debug("Ignoring click after game end\n");
    return;
  }
  if (!self->input_enabled) {
    g_debug("Ignoring click while input is disabled\n");
    return;
  }

  gpointer data = g_object_get_data(G_OBJECT(button), "board-index");
  if (!data) {
    g_debug("Missing board index for clicked square\n");
    return;
  }
  int index = GPOINTER_TO_INT(data) - 1;
  if (index < 0) {
    g_debug("Invalid board index for clicked square\n");
    return;
  }

  if (board_selection_controller_handle_click(self->selection_controller, (uint8_t)index)) {
    board_view_update(self);
  }
}

static void board_view_build_board(BoardView *self) {
  g_return_if_fail(BOARD_IS_VIEW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));

  const GameState *state = gcheckers_model_peek_state(self->model);
  if (!state) {
    g_debug("Failed to fetch game state while building board\n");
    return;
  }

  guint board_size = state->board.board_size;
  if (board_size == 0) {
    g_debug("Board size was zero while building board\n");
    return;
  }

  board_grid_build(self->board_grid, board_size, G_CALLBACK(board_view_on_square_clicked), self);
}

GtkWidget *board_view_get_widget(BoardView *self) {
  g_return_val_if_fail(BOARD_IS_VIEW(self), NULL);

  return self->root;
}

void board_view_set_model(BoardView *self, GCheckersModel *model) {
  g_return_if_fail(BOARD_IS_VIEW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(model));

  if (self->model) {
    g_clear_object(&self->model);
  }

  self->model = g_object_ref(model);
  board_selection_controller_set_model(self->selection_controller, self->model);
  board_move_overlay_set_model(self->board_overlay, self->model);
  board_view_build_board(self);
  board_view_update(self);
}

void board_view_update(BoardView *self) {
  g_return_if_fail(BOARD_IS_VIEW(self));

  if (!self->model) {
    g_debug("Missing model while updating board view\n");
    return;
  }

  const GameState *state = gcheckers_model_peek_state(self->model);
  if (!state) {
    g_debug("Failed to fetch game state for board update\n");
    return;
  }

  board_view_update_board(self, state);
  board_view_update_sensitivity(self, state);

  if (self->board_overlay) {
    board_move_overlay_queue_draw(self->board_overlay);
  }
}

void board_view_clear_selection(BoardView *self) {
  g_return_if_fail(BOARD_IS_VIEW(self));

  board_selection_controller_clear(self->selection_controller);
  board_view_update(self);
}

void board_view_set_input_enabled(BoardView *self, gboolean enabled) {
  g_return_if_fail(BOARD_IS_VIEW(self));

  self->input_enabled = enabled;

  if (!self->model) {
    return;
  }

  const GameState *state = gcheckers_model_peek_state(self->model);
  if (!state) {
    g_debug("Failed to fetch game state while updating input sensitivity\n");
    return;
  }

  board_view_update_sensitivity(self, state);
}

static void board_view_dispose(GObject *object) {
  BoardView *self = BOARD_VIEW(object);

  gboolean root_removed = TRUE;
  if (self->root) {
    root_removed = gcheckers_widget_remove_from_parent(self->root);
    if (!root_removed && gtk_widget_get_parent(self->root)) {
      g_debug("Failed to remove board view root from parent during dispose\n");
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
  self->input_enabled = TRUE;
}

BoardView *board_view_new(void) {
  return g_object_new(BOARD_TYPE_VIEW, NULL);
}
