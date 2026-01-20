#include "gcheckers_window.h"

#include <string.h>

struct _GCheckersWindow {
  GtkApplicationWindow parent_instance;
  GCheckersModel *model;
  GtkWidget *status_label;
  GtkWidget *reset_button;
  GtkWidget *board_grid;
  GtkWidget *square_buttons[CHECKERS_MAX_SQUARES];
  guint board_size;
  uint8_t selected_path[CHECKERS_MAX_MOVE_LENGTH];
  uint8_t selected_length;
  gulong state_handler_id;
  gboolean ai_in_progress;
  GdkTexture *white_man_texture;
  GdkTexture *black_man_texture;
};

G_DEFINE_TYPE(GCheckersWindow, gcheckers_window, GTK_TYPE_APPLICATION_WINDOW)

static gboolean gcheckers_window_selection_contains(GCheckersWindow *self, uint8_t index) {
  g_return_val_if_fail(GCHECKERS_IS_WINDOW(self), FALSE);

  for (uint8_t i = 0; i < self->selected_length; ++i) {
    if (self->selected_path[i] == index) {
      return TRUE;
    }
  }
  return FALSE;
}

static const char *gcheckers_window_piece_symbol(CheckersPiece piece) {
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
      g_debug("gcheckers_window_piece_symbol received unknown piece %d\n", piece);
      return "?";
  }
}

static GdkTexture *gcheckers_window_build_man_texture(const char *fill_color, const char *stroke_color) {
  g_return_val_if_fail(fill_color != NULL, NULL);
  g_return_val_if_fail(stroke_color != NULL, NULL);

  g_autofree char *svg = g_strdup_printf(
      "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
      "<path fill='%s' stroke='%s' stroke-width='4' stroke-linecap='round' stroke-linejoin='round'"
      " d='M16 20 L48 20 L48 44 Q32 56 16 44 Z'/>"
      "<ellipse cx='32' cy='20' rx='16' ry='6' fill='%s' stroke='%s' stroke-width='4'"
      " stroke-linecap='round' stroke-linejoin='round'/>"
      "</svg>",
      fill_color,
      stroke_color,
      fill_color,
      stroke_color);
  if (!svg) {
    g_debug("Failed to allocate SVG for men\n");
    return NULL;
  }

  g_autoptr(GBytes) bytes = g_bytes_new(svg, strlen(svg));
  g_autoptr(GError) error = NULL;
  GdkTexture *texture = gdk_texture_new_from_bytes(bytes, &error);
  if (!texture) {
    g_debug("Failed to create texture for men: %s\n", error ? error->message : "unknown error");
  }
  return texture;
}

static void gcheckers_window_set_square_piece(GCheckersWindow *self, GtkWidget *button, CheckersPiece piece) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GTK_IS_BUTTON(button));

  GtkWidget *piece_stack = g_object_get_data(G_OBJECT(button), "piece-stack");
  GtkWidget *piece_picture = g_object_get_data(G_OBJECT(button), "piece-picture");
  GtkWidget *piece_label = g_object_get_data(G_OBJECT(button), "piece-label");

  g_return_if_fail(GTK_IS_STACK(piece_stack));
  g_return_if_fail(GTK_IS_PICTURE(piece_picture));
  g_return_if_fail(GTK_IS_LABEL(piece_label));

  const char *label_text = "";
  GdkTexture *texture = NULL;
  gboolean show_picture = FALSE;

  switch (piece) {
    case CHECKERS_PIECE_WHITE_MAN:
      texture = self->white_man_texture;
      show_picture = texture != NULL;
      label_text = show_picture ? "" : gcheckers_window_piece_symbol(piece);
      break;
    case CHECKERS_PIECE_BLACK_MAN:
      texture = self->black_man_texture;
      show_picture = texture != NULL;
      label_text = show_picture ? "" : gcheckers_window_piece_symbol(piece);
      break;
    case CHECKERS_PIECE_WHITE_KING:
    case CHECKERS_PIECE_BLACK_KING:
      label_text = gcheckers_window_piece_symbol(piece);
      break;
    case CHECKERS_PIECE_EMPTY:
      label_text = "";
      break;
    default:
      g_debug("gcheckers_window_set_square_piece received unknown piece %d\n", piece);
      label_text = "?";
      break;
  }

  if (show_picture) {
    gtk_picture_set_paintable(GTK_PICTURE(piece_picture), GDK_PAINTABLE(texture));
    gtk_stack_set_visible_child(GTK_STACK(piece_stack), piece_picture);
  } else {
    gtk_label_set_text(GTK_LABEL(piece_label), label_text);
    gtk_stack_set_visible_child(GTK_STACK(piece_stack), piece_label);
  }
  gtk_widget_set_visible(piece_stack, TRUE);
}

static GtkWidget *gcheckers_window_build_square_content(GtkWidget *button) {
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
  gtk_widget_set_size_request(piece_picture, 28, 28);
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
  gtk_widget_add_css_class(index_label, "square-index");

  gtk_overlay_set_child(GTK_OVERLAY(container), piece_stack);
  gtk_overlay_add_overlay(GTK_OVERLAY(container), index_label);

  g_object_set_data(G_OBJECT(button), "piece-stack", piece_stack);
  g_object_set_data(G_OBJECT(button), "piece-picture", piece_picture);
  g_object_set_data(G_OBJECT(button), "piece-label", piece_label);
  g_object_set_data(G_OBJECT(button), "index-label", index_label);

  return container;
}

static void gcheckers_window_print_move(const char *label, const CheckersMove *move) {
  g_return_if_fail(label != NULL);
  g_return_if_fail(move != NULL);

  char buffer[128];
  if (!game_format_move_notation(move, buffer, sizeof(buffer))) {
    g_debug("Failed to format move notation\n");
    return;
  }
  g_print("%s plays: %s\n", label, buffer);
}

static gboolean gcheckers_window_move_has_prefix(const CheckersMove *move, const uint8_t *path, uint8_t length) {
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

static gboolean gcheckers_window_moves_have_prefix(const MoveList *moves, const uint8_t *path, uint8_t length) {
  g_return_val_if_fail(moves != NULL, FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  for (size_t i = 0; i < moves->count; ++i) {
    if (gcheckers_window_move_has_prefix(&moves->moves[i], path, length)) {
      return TRUE;
    }
  }
  return FALSE;
}

static const CheckersMove *gcheckers_window_find_exact_move(const MoveList *moves,
                                                            const uint8_t *path,
                                                            uint8_t length) {
  g_return_val_if_fail(moves != NULL, NULL);
  g_return_val_if_fail(path != NULL, NULL);

  for (size_t i = 0; i < moves->count; ++i) {
    if (moves->moves[i].length != length) {
      continue;
    }
    if (gcheckers_window_move_has_prefix(&moves->moves[i], path, length)) {
      return &moves->moves[i];
    }
  }
  return NULL;
}

static gboolean gcheckers_window_moves_start_with(const MoveList *moves, uint8_t index) {
  g_return_val_if_fail(moves != NULL, FALSE);

  uint8_t path = index;
  return gcheckers_window_moves_have_prefix(moves, &path, 1);
}

static void gcheckers_window_update_board(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));

  const GameState *state = gcheckers_model_peek_state(self->model);
  if (!state) {
    g_debug("Failed to fetch game state for board update\n");
    return;
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
      gcheckers_window_set_square_piece(self, button, piece);

      GtkWidget *index_label = g_object_get_data(G_OBJECT(button), "index-label");
      g_return_if_fail(GTK_IS_LABEL(index_label));
      char label[8];
      g_snprintf(label, sizeof(label), "%d", idx + 1);
      gtk_label_set_text(GTK_LABEL(index_label), label);

      if (gcheckers_window_selection_contains(self, (uint8_t)idx)) {
        gtk_widget_add_css_class(button, "board-selected");
      } else {
        gtk_widget_remove_css_class(button, "board-selected");
      }
    }
  }
}

static void gcheckers_window_clear_selection(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  self->selected_length = 0;
  gcheckers_window_update_board(self);
}

static void gcheckers_window_apply_player_move(GCheckersWindow *self, const CheckersMove *move) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));
  g_return_if_fail(move != NULL);

  if (!gcheckers_model_apply_move(self->model, move)) {
    g_debug("Failed to apply player move\n");
    return;
  }

  gcheckers_window_print_move("Player", move);
  gcheckers_window_clear_selection(self);
}

static void gcheckers_window_maybe_play_ai_move(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));

  const GameState *state = gcheckers_model_peek_state(self->model);
  if (!state) {
    g_debug("Failed to fetch game state for AI move\n");
    return;
  }
  if (state->winner != CHECKERS_WINNER_NONE || state->turn != CHECKERS_COLOR_BLACK) {
    return;
  }
  if (self->ai_in_progress) {
    return;
  }

  self->ai_in_progress = TRUE;
  CheckersMove move;
  if (gcheckers_model_step_random_move(self->model, &move)) {
    gcheckers_window_print_move("AI", &move);
  }
  self->ai_in_progress = FALSE;
}

static void gcheckers_window_update_status(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));

  g_autofree char *status = gcheckers_model_format_status(self->model);
  if (!status) {
    g_debug("Failed to format status text\n");
    return;
  }
  gtk_label_set_text(GTK_LABEL(self->status_label), status);

  gcheckers_window_update_board(self);

  const GameState *state = gcheckers_model_peek_state(self->model);
  g_return_if_fail(state != NULL);
  gboolean can_play = state->winner == CHECKERS_WINNER_NONE && state->turn == CHECKERS_COLOR_WHITE;
  for (int i = 0; i < CHECKERS_MAX_SQUARES; ++i) {
    if (self->square_buttons[i]) {
      gtk_widget_set_sensitive(self->square_buttons[i], can_play);
    }
  }
}

static void gcheckers_window_on_state_changed(GCheckersModel *model, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_MODEL(model));
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));

  gcheckers_window_update_status(self);
  gcheckers_window_maybe_play_ai_move(self);
}

static void gcheckers_window_on_reset_clicked(GtkButton * /*button*/, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));

  gcheckers_model_reset(self->model);
  gcheckers_window_clear_selection(self);
}

static void gcheckers_window_on_square_clicked(GtkButton *button, gpointer user_data) {
  GCheckersWindow *self = GCHECKERS_WINDOW(user_data);

  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
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
  int index = GPOINTER_TO_INT(data);
  if (index < 0) {
    g_debug("Invalid board index for clicked square\n");
    return;
  }

  MoveList moves = gcheckers_model_list_moves(self->model);
  if (moves.count == 0) {
    movelist_free(&moves);
    gcheckers_model_step_random_move(self->model, NULL);
    gcheckers_window_clear_selection(self);
    return;
  }

  if (self->selected_length == 0) {
    if (!gcheckers_window_moves_start_with(&moves, (uint8_t)index)) {
      movelist_free(&moves);
      return;
    }
    self->selected_path[0] = (uint8_t)index;
    self->selected_length = 1;
    gcheckers_window_update_board(self);
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

  if (!gcheckers_window_moves_have_prefix(&moves, candidate, candidate_length)) {
    if (gcheckers_window_moves_start_with(&moves, (uint8_t)index)) {
      self->selected_path[0] = (uint8_t)index;
      self->selected_length = 1;
      gcheckers_window_update_board(self);
    } else {
      g_debug("Selected path is not in move list\n");
    }
    movelist_free(&moves);
    return;
  }

  memcpy(self->selected_path, candidate, candidate_length);
  self->selected_length = candidate_length;
  gcheckers_window_update_board(self);

  const CheckersMove *match = gcheckers_window_find_exact_move(&moves, candidate, candidate_length);
  if (match) {
    gcheckers_window_apply_player_move(self, match);
  }

  movelist_free(&moves);
}

static void gcheckers_window_build_board(GCheckersWindow *self) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
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

  gtk_grid_set_row_homogeneous(GTK_GRID(self->board_grid), TRUE);
  gtk_grid_set_column_homogeneous(GTK_GRID(self->board_grid), TRUE);

  for (int row = 0; row < (int)board_size; ++row) {
    for (int col = 0; col < (int)board_size; ++col) {
      GtkWidget *square = NULL;
      if ((row + col) % 2 == 0) {
        GtkWidget *label = gtk_label_new(" ");
        gtk_widget_set_size_request(label, 52, 52);
        gtk_widget_set_hexpand(label, TRUE);
        gtk_widget_set_vexpand(label, TRUE);
        gtk_widget_add_css_class(label, "board-light");
        square = label;
      } else {
        int8_t index = board_index_from_coord(row, col, board_size);
        GtkWidget *button = gtk_button_new();
        gtk_widget_set_size_request(button, 52, 52);
        gtk_widget_set_hexpand(button, TRUE);
        gtk_widget_set_vexpand(button, TRUE);
        gtk_widget_add_css_class(button, "board-dark");
        gtk_widget_add_css_class(button, "board-square");
        GtkWidget *content = gcheckers_window_build_square_content(button);
        gtk_button_set_child(GTK_BUTTON(button), content);
        g_object_set_data(G_OBJECT(button), "board-index", GINT_TO_POINTER(index));
        g_signal_connect(button, "clicked", G_CALLBACK(gcheckers_window_on_square_clicked), self);
        square = button;
        if (index >= 0 && index < CHECKERS_MAX_SQUARES) {
          self->square_buttons[index] = button;
        }
      }
      gtk_grid_attach(GTK_GRID(self->board_grid), square, col, row, 1, 1);
    }
  }
}

static void gcheckers_window_set_model(GCheckersWindow *self, GCheckersModel *model) {
  g_return_if_fail(GCHECKERS_IS_WINDOW(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(model));

  if (self->model) {
    if (self->state_handler_id != 0) {
      g_signal_handler_disconnect(self->model, self->state_handler_id);
      self->state_handler_id = 0;
    }
    g_clear_object(&self->model);
  }

  self->model = g_object_ref(model);
  self->state_handler_id = g_signal_connect(self->model,
                                            "state-changed",
                                            G_CALLBACK(gcheckers_window_on_state_changed),
                                            self);
  gcheckers_window_build_board(self);
  gcheckers_window_update_status(self);
}

static void gcheckers_window_dispose(GObject *object) {
  GCheckersWindow *self = GCHECKERS_WINDOW(object);

  if (self->model && self->state_handler_id != 0) {
    g_signal_handler_disconnect(self->model, self->state_handler_id);
    self->state_handler_id = 0;
  }
  g_clear_object(&self->model);
  g_clear_object(&self->white_man_texture);
  g_clear_object(&self->black_man_texture);

  G_OBJECT_CLASS(gcheckers_window_parent_class)->dispose(object);
}

static void gcheckers_window_class_init(GCheckersWindowClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = gcheckers_window_dispose;
}

static void gcheckers_window_init(GCheckersWindow *self) {
  gtk_window_set_title(GTK_WINDOW(self), "gcheckers");
  gtk_window_set_default_size(GTK_WINDOW(self), 600, 700);

  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(provider,
                                    ".board-light { background-color: #f5f5f5; border-radius: 0; }"
                                    ".board-dark { background-color: #2b2b2b; border-radius: 0; }"
                                    ".board-square { padding: 0; border-radius: 0; }"
                                    ".piece-label { font-size: 20px; }"
                                    ".square-index { font-size: 10px; margin-bottom: 4px; color: #111; }"
                                    ".board-dark .square-index { color: #f5f5f5; }"
                                    ".board-dark .piece-label { color: #f5f5f5; }"
                                    ".board-selected { outline: 2px solid #4a90e2; }");
  GdkDisplay *display = gdk_display_get_default();
  if (!display) {
    g_debug("Failed to fetch default display for CSS styling\n");
  } else {
    gtk_style_context_add_provider_for_display(display,
                                               GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  }
  g_object_unref(provider);

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(content, 16);
  gtk_widget_set_margin_bottom(content, 16);
  gtk_widget_set_margin_start(content, 16);
  gtk_widget_set_margin_end(content, 16);
  gtk_window_set_child(GTK_WINDOW(self), content);

  self->status_label = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(self->status_label), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(self->status_label), TRUE);
  gtk_box_append(GTK_BOX(content), self->status_label);

  self->board_grid = gtk_grid_new();
  gtk_widget_add_css_class(self->board_grid, "board");
  gtk_widget_set_hexpand(self->board_grid, TRUE);
  gtk_widget_set_vexpand(self->board_grid, TRUE);
  gtk_box_append(GTK_BOX(content), self->board_grid);

  GtkWidget *button_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_append(GTK_BOX(content), button_row);

  self->reset_button = gtk_button_new_with_label("Reset");
  g_signal_connect(self->reset_button, "clicked", G_CALLBACK(gcheckers_window_on_reset_clicked), self);
  gtk_box_append(GTK_BOX(button_row), self->reset_button);

  self->white_man_texture = gcheckers_window_build_man_texture("#ffffff", "#111111");
  self->black_man_texture = gcheckers_window_build_man_texture("#111111", "#ffffff");
}

GCheckersWindow *gcheckers_window_new(GtkApplication *app, GCheckersModel *model) {
  g_return_val_if_fail(GTK_IS_APPLICATION(app), NULL);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(model), NULL);

  GCheckersWindow *window = g_object_new(GCHECKERS_TYPE_WINDOW, "application", app, NULL);
  gcheckers_window_set_model(window, model);
  return window;
}
