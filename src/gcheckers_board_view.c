#include "gcheckers_board_view.h"
#include "board.h"
#include "gcheckers_man_paintable.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

struct _GCheckersBoardView {
  GObject parent_instance;
  GCheckersModel *model;
  GtkWidget *root;
  GtkWidget *board_grid;
  GtkWidget *board_drawing_area;
  GtkWidget *square_buttons[CHECKERS_MAX_SQUARES];
  guint board_size;
  uint8_t selected_path[CHECKERS_MAX_MOVE_LENGTH];
  uint8_t selected_length;
  GdkPaintable *white_man_paintable;
  GdkPaintable *black_man_paintable;
  GdkPaintable *white_king_paintable;
  GdkPaintable *black_king_paintable;
};

G_DEFINE_TYPE(GCheckersBoardView, gcheckers_board_view, G_TYPE_OBJECT)

static const int gcheckers_board_view_square_size = 63;
static const double gcheckers_board_view_last_move_alpha = 0.5;
static const double gcheckers_board_view_last_move_stroke_width = 4.0;
static const double gcheckers_board_view_last_move_arrow_scale = 0.28;

static gboolean gcheckers_board_view_selection_contains(GCheckersBoardView *self, uint8_t index) {
  g_return_val_if_fail(GCHECKERS_IS_BOARD_VIEW(self), FALSE);

  for (uint8_t i = 0; i < self->selected_length; ++i) {
    if (self->selected_path[i] == index) {
      return TRUE;
    }
  }
  return FALSE;
}

static void gcheckers_board_view_draw_arrow(cairo_t *cr,
                                            double start_x,
                                            double start_y,
                                            double end_x,
                                            double end_y,
                                            double arrow_size) {
  g_return_if_fail(cr != NULL);

  double dx = end_x - start_x;
  double dy = end_y - start_y;
  double distance = sqrt(dx * dx + dy * dy);
  if (distance <= 0.0) {
    return;
  }

  double angle = atan2(dy, dx);
  double head_length = arrow_size;
  if (head_length > distance * 0.6) {
    head_length = distance * 0.6;
  }
  double head_width = head_length * 0.6;

  double left_x = end_x - head_length * cos(angle) + head_width * sin(angle);
  double left_y = end_y - head_length * sin(angle) - head_width * cos(angle);
  double right_x = end_x - head_length * cos(angle) - head_width * sin(angle);
  double right_y = end_y - head_length * sin(angle) + head_width * cos(angle);

  cairo_move_to(cr, start_x, start_y);
  cairo_line_to(cr, end_x, end_y);
  cairo_stroke(cr);

  cairo_move_to(cr, end_x, end_y);
  cairo_line_to(cr, left_x, left_y);
  cairo_line_to(cr, right_x, right_y);
  cairo_close_path(cr);
  cairo_fill(cr);
}

static void gcheckers_board_view_draw_last_move(GtkDrawingArea * /*area*/,
                                                cairo_t *cr,
                                                int width,
                                                int height,
                                                gpointer user_data) {
  GCheckersBoardView *self = GCHECKERS_BOARD_VIEW(user_data);

  g_return_if_fail(GCHECKERS_IS_BOARD_VIEW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));
  g_return_if_fail(cr != NULL);

  if (width <= 0 || height <= 0) {
    return;
  }

  const GameState *state = gcheckers_model_peek_state(self->model);
  if (!state) {
    g_debug("Failed to fetch game state for last move overlay\n");
    return;
  }
  if (state->board.board_size == 0) {
    g_debug("Board size was zero when drawing last move overlay\n");
    return;
  }

  const CheckersMove *last_move = gcheckers_model_peek_last_move(self->model);
  if (!last_move || last_move->length < 2) {
    return;
  }

  double cell_width = (double)width / state->board.board_size;
  double cell_height = (double)height / state->board.board_size;
  double arrow_size = fmin(cell_width, cell_height) * gcheckers_board_view_last_move_arrow_scale;

  cairo_save(cr);
  cairo_set_line_width(cr, gcheckers_board_view_last_move_stroke_width);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_set_source_rgba(cr, 0.2, 0.85, 0.2, gcheckers_board_view_last_move_alpha);

  for (uint8_t i = 1; i < last_move->length; ++i) {
    int start_row = 0;
    int start_col = 0;
    int end_row = 0;
    int end_col = 0;
    board_coord_from_index(last_move->path[i - 1], &start_row, &start_col, state->board.board_size);
    board_coord_from_index(last_move->path[i], &end_row, &end_col, state->board.board_size);

    double start_x = ((double)start_col + 0.5) * cell_width;
    double start_y = ((double)start_row + 0.5) * cell_height;
    double end_x = ((double)end_col + 0.5) * cell_width;
    double end_y = ((double)end_row + 0.5) * cell_height;

    gcheckers_board_view_draw_arrow(cr, start_x, start_y, end_x, end_y, arrow_size);
  }

  cairo_restore(cr);
}

static const char *gcheckers_board_view_piece_symbol(CheckersPiece piece) {
  switch (piece) {
    case CHECKERS_PIECE_WHITE_MAN:
      return "⛀";
    case CHECKERS_PIECE_WHITE_KING:
      return "⛁";
    case CHECKERS_PIECE_BLACK_MAN:
      return "⛂";
    case CHECKERS_PIECE_BLACK_KING:
      return "⛃";
    case CHECKERS_PIECE_EMPTY:
      return "·";
    default:
      g_debug("gcheckers_board_view_piece_symbol received unknown piece %d\n", piece);
      return "?";
  }
}

static GdkPaintable *gcheckers_board_view_build_piece_paintable(const char *fill_color,
                                                                const char *stroke_color,
                                                                guint layer_count) {
  g_return_val_if_fail(fill_color != NULL, NULL);
  g_return_val_if_fail(stroke_color != NULL, NULL);

  GdkRGBA fill;
  GdkRGBA stroke;

  if (!gdk_rgba_parse(&fill, fill_color)) {
    g_debug("Failed to parse fill color for piece: %s\n", fill_color);
    return NULL;
  }
  if (!gdk_rgba_parse(&stroke, stroke_color)) {
    g_debug("Failed to parse stroke color for piece: %s\n", stroke_color);
    return NULL;
  }

  return gcheckers_man_paintable_new(&fill, &stroke, layer_count);
}

static GdkPaintable *gcheckers_board_view_build_man_paintable(const char *fill_color, const char *stroke_color) {
  return gcheckers_board_view_build_piece_paintable(fill_color, stroke_color, 1);
}

static GdkPaintable *gcheckers_board_view_build_king_paintable(const char *fill_color, const char *stroke_color) {
  return gcheckers_board_view_build_piece_paintable(fill_color, stroke_color, 2);
}

static void gcheckers_board_view_set_square_piece(GCheckersBoardView *self,
                                                  GtkWidget *button,
                                                  CheckersPiece piece) {
  g_return_if_fail(GCHECKERS_IS_BOARD_VIEW(self));
  g_return_if_fail(GTK_IS_BUTTON(button));

  GtkWidget *piece_stack = g_object_get_data(G_OBJECT(button), "piece-stack");
  GtkWidget *piece_picture = g_object_get_data(G_OBJECT(button), "piece-picture");
  GtkWidget *piece_label = g_object_get_data(G_OBJECT(button), "piece-label");

  g_return_if_fail(GTK_IS_STACK(piece_stack));
  g_return_if_fail(GTK_IS_PICTURE(piece_picture));
  g_return_if_fail(GTK_IS_LABEL(piece_label));

  const char *label_text = "";
  GdkPaintable *paintable = NULL;
  gboolean show_picture = FALSE;

  switch (piece) {
    case CHECKERS_PIECE_WHITE_MAN:
      paintable = self->white_man_paintable;
      show_picture = paintable != NULL;
      label_text = show_picture ? "" : gcheckers_board_view_piece_symbol(piece);
      break;
    case CHECKERS_PIECE_BLACK_MAN:
      paintable = self->black_man_paintable;
      show_picture = paintable != NULL;
      label_text = show_picture ? "" : gcheckers_board_view_piece_symbol(piece);
      break;
    case CHECKERS_PIECE_WHITE_KING:
      paintable = self->white_king_paintable;
      show_picture = paintable != NULL;
      label_text = show_picture ? "" : gcheckers_board_view_piece_symbol(piece);
      break;
    case CHECKERS_PIECE_BLACK_KING:
      paintable = self->black_king_paintable;
      show_picture = paintable != NULL;
      label_text = show_picture ? "" : gcheckers_board_view_piece_symbol(piece);
      break;
    case CHECKERS_PIECE_EMPTY:
      gtk_widget_set_visible(piece_stack, FALSE);
      return;
    default:
      g_debug("gcheckers_board_view_set_square_piece received unknown piece %d\n", piece);
      label_text = "?";
      break;
  }

  if (show_picture) {
    gtk_picture_set_paintable(GTK_PICTURE(piece_picture), paintable);
    gtk_stack_set_visible_child(GTK_STACK(piece_stack), piece_picture);
  } else {
    gtk_label_set_text(GTK_LABEL(piece_label), label_text);
    gtk_stack_set_visible_child(GTK_STACK(piece_stack), piece_label);
  }
  gtk_widget_set_visible(piece_stack, TRUE);
}

static GtkWidget *gcheckers_board_view_build_square_content(GtkWidget *button) {
  g_return_val_if_fail(GTK_IS_BUTTON(button), NULL);

  GtkWidget *container = gtk_overlay_new();
  gtk_widget_set_hexpand(container, TRUE);
  gtk_widget_set_vexpand(container, TRUE);

  GtkWidget *piece_stack = gtk_stack_new();
  gtk_stack_set_hhomogeneous(GTK_STACK(piece_stack), TRUE);
  gtk_stack_set_vhomogeneous(GTK_STACK(piece_stack), TRUE);
  gtk_widget_set_hexpand(piece_stack, TRUE);
  gtk_widget_set_vexpand(piece_stack, TRUE);

  GtkWidget *piece_picture = gtk_picture_new();
  gtk_picture_set_content_fit(GTK_PICTURE(piece_picture), GTK_CONTENT_FIT_CONTAIN);
  gtk_widget_set_size_request(piece_picture, gcheckers_board_view_square_size, gcheckers_board_view_square_size);
  gtk_widget_add_css_class(piece_picture, "piece-picture");
  gtk_stack_add_named(GTK_STACK(piece_stack), piece_picture, "picture");

  GtkWidget *piece_label = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(piece_label), 0.5f);
  gtk_widget_add_css_class(piece_label, "piece-label");
  gtk_stack_add_named(GTK_STACK(piece_stack), piece_label, "label");

  GtkWidget *index_label = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(index_label), 0.5f);
  gtk_widget_set_halign(index_label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(index_label, GTK_ALIGN_END);
  gtk_widget_set_margin_bottom(index_label, 2);
  gtk_widget_add_css_class(index_label, "square-index");

  gtk_overlay_set_child(GTK_OVERLAY(container), piece_stack);
  gtk_overlay_add_overlay(GTK_OVERLAY(container), index_label);

  g_object_set_data(G_OBJECT(button), "piece-stack", piece_stack);
  g_object_set_data(G_OBJECT(button), "piece-picture", piece_picture);
  g_object_set_data(G_OBJECT(button), "piece-label", piece_label);
  g_object_set_data(G_OBJECT(button), "index-label", index_label);

  return container;
}

static void gcheckers_board_view_print_move(const char *label, const CheckersMove *move) {
  g_return_if_fail(label != NULL);
  g_return_if_fail(move != NULL);

  char buffer[128];
  if (!game_format_move_notation(move, buffer, sizeof(buffer))) {
    g_debug("Failed to format move notation\n");
    return;
  }
  g_print("%s plays: %s\n", label, buffer);
}

static gboolean gcheckers_board_view_move_has_prefix(const CheckersMove *move,
                                                     const uint8_t *path,
                                                     uint8_t length) {
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  if (move->length < length) {
    return FALSE;
  }
  for (uint8_t i = 0; i < length; ++i) {
    if (move->path[i] != path[i]) {
      return FALSE;
    }
  }
  return TRUE;
}

static gboolean gcheckers_board_view_moves_have_prefix(const MoveList *moves, const uint8_t *path, uint8_t length) {
  g_return_val_if_fail(moves != NULL, FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  for (size_t i = 0; i < moves->count; ++i) {
    if (gcheckers_board_view_move_has_prefix(&moves->moves[i], path, length)) {
      return TRUE;
    }
  }
  return FALSE;
}

static const CheckersMove *gcheckers_board_view_find_exact_move(const MoveList *moves,
                                                                const uint8_t *path,
                                                                uint8_t length) {
  g_return_val_if_fail(moves != NULL, NULL);
  g_return_val_if_fail(path != NULL, NULL);

  for (size_t i = 0; i < moves->count; ++i) {
    if (moves->moves[i].length != length) {
      continue;
    }
    if (gcheckers_board_view_move_has_prefix(&moves->moves[i], path, length)) {
      return &moves->moves[i];
    }
  }
  return NULL;
}

static gboolean gcheckers_board_view_moves_start_with(const MoveList *moves, uint8_t index) {
  g_return_val_if_fail(moves != NULL, FALSE);

  uint8_t path = index;
  return gcheckers_board_view_moves_have_prefix(moves, &path, 1);
}

static void gcheckers_board_view_update_board(GCheckersBoardView *self, const GameState *state) {
  g_return_if_fail(GCHECKERS_IS_BOARD_VIEW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));
  g_return_if_fail(state != NULL);

  MoveList moves = {0};
  gboolean moves_loaded = FALSE;
  gboolean highlight_moves = FALSE;
  bool playable_starts[CHECKERS_MAX_SQUARES] = {false};
  bool possible_destinations[CHECKERS_MAX_SQUARES] = {false};
  if (state->winner == CHECKERS_WINNER_NONE && state->turn == CHECKERS_COLOR_WHITE) {
    moves = gcheckers_model_list_moves(self->model);
    moves_loaded = TRUE;
    highlight_moves = moves.count > 0;
    if (highlight_moves) {
      game_moves_collect_starts(&moves, playable_starts);
      if (self->selected_length > 0) {
        game_moves_collect_next_destinations(&moves,
                                             self->selected_path,
                                             self->selected_length,
                                             possible_destinations);
      }
    }
  }

  guint board_size = state->board.board_size;
  int max_square = board_playable_squares(board_size);
  for (int row = 0; row < (int)board_size; ++row) {
    for (int col = 0; col < (int)board_size; ++col) {
      if ((row + col) % 2 == 0) {
        continue;
      }

      int8_t idx = board_index_from_coord(row, col, board_size);
      if (idx < 0 || idx >= max_square) {
        continue;
      }
      GtkWidget *button = self->square_buttons[idx];
      if (!button) {
        continue;
      }

      CheckersPiece piece = board_get(&state->board, (uint8_t)idx);
      gcheckers_board_view_set_square_piece(self, button, piece);

      GtkWidget *index_label = g_object_get_data(G_OBJECT(button), "index-label");
      g_return_if_fail(GTK_IS_LABEL(index_label));
      char label[8];
      g_snprintf(label, sizeof(label), "%d", idx + 1);
      gtk_label_set_text(GTK_LABEL(index_label), label);

      gboolean is_selected = gcheckers_board_view_selection_contains(self, (uint8_t)idx);
      gboolean is_selectable = highlight_moves && playable_starts[idx];
      gboolean is_destination = highlight_moves && possible_destinations[idx];
      if (is_selected) {
        gtk_widget_add_css_class(button, "board-halo-selected");
        gtk_widget_remove_css_class(button, "board-halo");
      } else if (is_selectable || is_destination) {
        gtk_widget_add_css_class(button, "board-halo");
        gtk_widget_remove_css_class(button, "board-halo-selected");
      } else {
        gtk_widget_remove_css_class(button, "board-halo");
        gtk_widget_remove_css_class(button, "board-halo-selected");
      }
    }
  }

  if (moves_loaded) {
    movelist_free(&moves);
  }
}

static void gcheckers_board_view_update_sensitivity(GCheckersBoardView *self, const GameState *state) {
  g_return_if_fail(GCHECKERS_IS_BOARD_VIEW(self));
  g_return_if_fail(state != NULL);

  gboolean can_play = state->winner == CHECKERS_WINNER_NONE && state->turn == CHECKERS_COLOR_WHITE;
  for (int i = 0; i < CHECKERS_MAX_SQUARES; ++i) {
    if (self->square_buttons[i]) {
      gtk_widget_set_sensitive(self->square_buttons[i], can_play);
    }
  }
}

void gcheckers_board_view_clear_selection(GCheckersBoardView *self) {
  g_return_if_fail(GCHECKERS_IS_BOARD_VIEW(self));

  self->selected_length = 0;
  gcheckers_board_view_update(self);
}

static void gcheckers_board_view_apply_player_move(GCheckersBoardView *self, const CheckersMove *move) {
  g_return_if_fail(GCHECKERS_IS_BOARD_VIEW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));
  g_return_if_fail(move != NULL);

  if (!gcheckers_model_apply_move(self->model, move)) {
    g_debug("Failed to apply player move\n");
    return;
  }

  gcheckers_board_view_print_move("Player", move);
  gcheckers_board_view_clear_selection(self);
}

static void gcheckers_board_view_on_square_clicked(GtkButton *button, gpointer user_data) {
  GCheckersBoardView *self = GCHECKERS_BOARD_VIEW(user_data);

  g_return_if_fail(GCHECKERS_IS_BOARD_VIEW(self));
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
  if (state->turn != CHECKERS_COLOR_WHITE) {
    g_debug("Ignoring click while waiting for AI\n");
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

  MoveList moves = gcheckers_model_list_moves(self->model);
  if (moves.count == 0) {
    movelist_free(&moves);
    gcheckers_model_step_random_move(self->model, NULL);
    gcheckers_board_view_clear_selection(self);
    return;
  }

  if (self->selected_length == 0) {
    if (!gcheckers_board_view_moves_start_with(&moves, (uint8_t)index)) {
      movelist_free(&moves);
      return;
    }
    self->selected_path[0] = (uint8_t)index;
    self->selected_length = 1;
    gcheckers_board_view_update(self);
    movelist_free(&moves);
    return;
  }

  if (self->selected_length >= CHECKERS_MAX_MOVE_LENGTH) {
    g_debug("Selection path length exceeded max move length\n");
    movelist_free(&moves);
    return;
  }

  uint8_t candidate[CHECKERS_MAX_MOVE_LENGTH];
  memcpy(candidate, self->selected_path, self->selected_length);
  candidate[self->selected_length] = (uint8_t)index;
  uint8_t candidate_length = self->selected_length + 1;

  if (!gcheckers_board_view_moves_have_prefix(&moves, candidate, candidate_length)) {
    if (gcheckers_board_view_moves_start_with(&moves, (uint8_t)index)) {
      self->selected_path[0] = (uint8_t)index;
      self->selected_length = 1;
      gcheckers_board_view_update(self);
    } else {
      g_debug("Selected path is not in move list\n");
    }
    movelist_free(&moves);
    return;
  }

  memcpy(self->selected_path, candidate, candidate_length);
  self->selected_length = candidate_length;
  gcheckers_board_view_update(self);

  const CheckersMove *match = gcheckers_board_view_find_exact_move(&moves, candidate, candidate_length);
  if (match) {
    gcheckers_board_view_apply_player_move(self, match);
  }

  movelist_free(&moves);
}

static void gcheckers_board_view_clear_grid(GCheckersBoardView *self) {
  g_return_if_fail(GCHECKERS_IS_BOARD_VIEW(self));
  g_return_if_fail(GTK_IS_GRID(self->board_grid));

  GtkWidget *child = gtk_widget_get_first_child(self->board_grid);
  while (child) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_grid_remove(GTK_GRID(self->board_grid), child);
    child = next;
  }
}

static void gcheckers_board_view_build_board(GCheckersBoardView *self) {
  g_return_if_fail(GCHECKERS_IS_BOARD_VIEW(self));
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

  self->board_size = board_size;
  memset(self->square_buttons, 0, sizeof(self->square_buttons));
  gcheckers_board_view_clear_grid(self);

  gtk_grid_set_row_homogeneous(GTK_GRID(self->board_grid), TRUE);
  gtk_grid_set_column_homogeneous(GTK_GRID(self->board_grid), TRUE);
  gtk_grid_set_row_spacing(GTK_GRID(self->board_grid), 1);
  gtk_grid_set_column_spacing(GTK_GRID(self->board_grid), 1);

  for (int row = 0; row < (int)board_size; ++row) {
    for (int col = 0; col < (int)board_size; ++col) {
      GtkWidget *square = NULL;
      if ((row + col) % 2 == 0) {
        GtkWidget *label = gtk_label_new(" ");
        gtk_widget_set_size_request(label, gcheckers_board_view_square_size, gcheckers_board_view_square_size);
        gtk_widget_set_hexpand(label, TRUE);
        gtk_widget_set_vexpand(label, TRUE);
        gtk_widget_add_css_class(label, "board-light");
        square = label;
      } else {
        int8_t index = board_index_from_coord(row, col, board_size);
        GtkWidget *button = gtk_button_new();
        gtk_widget_set_size_request(button, gcheckers_board_view_square_size, gcheckers_board_view_square_size);
        gtk_widget_set_hexpand(button, TRUE);
        gtk_widget_set_vexpand(button, TRUE);
        gtk_widget_add_css_class(button, "board-dark");
        gtk_widget_add_css_class(button, "board-square");
        GtkWidget *content = gcheckers_board_view_build_square_content(button);
        gtk_button_set_child(GTK_BUTTON(button), content);
        if (index < 0) {
          g_debug("Invalid board index while building board\n");
        } else {
          g_object_set_data(G_OBJECT(button), "board-index", GINT_TO_POINTER(index + 1));
        }
        g_signal_connect(button, "clicked", G_CALLBACK(gcheckers_board_view_on_square_clicked), self);
        square = button;
        if (index >= 0 && index < CHECKERS_MAX_SQUARES) {
          self->square_buttons[index] = button;
        }
      }
      gtk_grid_attach(GTK_GRID(self->board_grid), square, col, row, 1, 1);
    }
  }
}

GtkWidget *gcheckers_board_view_get_widget(GCheckersBoardView *self) {
  g_return_val_if_fail(GCHECKERS_IS_BOARD_VIEW(self), NULL);

  return self->root;
}

void gcheckers_board_view_set_model(GCheckersBoardView *self, GCheckersModel *model) {
  g_return_if_fail(GCHECKERS_IS_BOARD_VIEW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(model));

  if (self->model) {
    g_clear_object(&self->model);
  }

  self->model = g_object_ref(model);
  gcheckers_board_view_build_board(self);
  gcheckers_board_view_update(self);
}

void gcheckers_board_view_update(GCheckersBoardView *self) {
  g_return_if_fail(GCHECKERS_IS_BOARD_VIEW(self));

  if (!self->model) {
    g_debug("Missing model while updating board view\n");
    return;
  }

  const GameState *state = gcheckers_model_peek_state(self->model);
  if (!state) {
    g_debug("Failed to fetch game state for board update\n");
    return;
  }

  gcheckers_board_view_update_board(self, state);
  gcheckers_board_view_update_sensitivity(self, state);

  if (self->board_drawing_area) {
    gtk_widget_queue_draw(self->board_drawing_area);
  }
}

static void gcheckers_board_view_dispose(GObject *object) {
  GCheckersBoardView *self = GCHECKERS_BOARD_VIEW(object);

  if (self->root && gtk_widget_get_parent(self->root)) {
    gtk_widget_unparent(self->root);
  }

  g_clear_object(&self->model);
  g_clear_object(&self->white_man_paintable);
  g_clear_object(&self->black_man_paintable);
  g_clear_object(&self->white_king_paintable);
  g_clear_object(&self->black_king_paintable);
  g_clear_object(&self->root);

  G_OBJECT_CLASS(gcheckers_board_view_parent_class)->dispose(object);
}

static void gcheckers_board_view_class_init(GCheckersBoardViewClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = gcheckers_board_view_dispose;
}

static void gcheckers_board_view_init(GCheckersBoardView *self) {
  self->root = gtk_overlay_new();
  g_object_ref_sink(self->root);
  gtk_widget_set_hexpand(self->root, TRUE);
  gtk_widget_set_vexpand(self->root, TRUE);

  self->board_grid = gtk_grid_new();
  gtk_widget_add_css_class(self->board_grid, "board");
  gtk_widget_set_hexpand(self->board_grid, TRUE);
  gtk_widget_set_vexpand(self->board_grid, TRUE);
  gtk_overlay_set_child(GTK_OVERLAY(self->root), self->board_grid);

  self->board_drawing_area = gtk_drawing_area_new();
  gtk_widget_set_hexpand(self->board_drawing_area, TRUE);
  gtk_widget_set_vexpand(self->board_drawing_area, TRUE);
  gtk_widget_set_can_target(self->board_drawing_area, FALSE);
  gtk_overlay_add_overlay(GTK_OVERLAY(self->root), self->board_drawing_area);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self->board_drawing_area),
                                 gcheckers_board_view_draw_last_move,
                                 self,
                                 NULL);

  self->white_man_paintable = gcheckers_board_view_build_man_paintable("#ffffff", "#111111");
  self->black_man_paintable = gcheckers_board_view_build_man_paintable("#111111", "#ffffff");
  self->white_king_paintable = gcheckers_board_view_build_king_paintable("#ffffff", "#111111");
  self->black_king_paintable = gcheckers_board_view_build_king_paintable("#111111", "#ffffff");
}

GCheckersBoardView *gcheckers_board_view_new(void) {
  return g_object_new(GCHECKERS_TYPE_BOARD_VIEW, NULL);
}
