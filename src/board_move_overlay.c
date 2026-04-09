#include "board_move_overlay.h"

#include "board.h"
#include "widget_utils.h"

#include <math.h>

struct _BoardMoveOverlay {
  GObject parent_instance;
  GtkWidget *drawing_area;
  GCheckersModel *model;
  GCheckersSgfController *sgf_controller;
  CheckersColor bottom_color;
  char *banner_text;
  BoardMoveOverlayBannerColor banner_color;
};

G_DEFINE_TYPE(BoardMoveOverlay, board_move_overlay, G_TYPE_OBJECT)

static const double board_move_overlay_alpha = 0.5;
static const double board_move_overlay_stroke_width = 4.0;
static const double board_move_overlay_arrow_scale = 0.28;
static const double board_move_overlay_banner_padding_x = 24.0;
static const double board_move_overlay_banner_padding_y = 14.0;
static const double board_move_overlay_banner_radius = 18.0;

static void board_move_overlay_draw_rounded_rect(cairo_t *cr,
                                                 double x,
                                                 double y,
                                                 double width,
                                                 double height,
                                                 double radius) {
  g_return_if_fail(cr != NULL);

  double capped_radius = radius;
  if (capped_radius > width / 2.0) {
    capped_radius = width / 2.0;
  }
  if (capped_radius > height / 2.0) {
    capped_radius = height / 2.0;
  }

  cairo_new_sub_path(cr);
  cairo_arc(cr, x + width - capped_radius, y + capped_radius, capped_radius, -G_PI_2, 0.0);
  cairo_arc(cr, x + width - capped_radius, y + height - capped_radius, capped_radius, 0.0, G_PI_2);
  cairo_arc(cr, x + capped_radius, y + height - capped_radius, capped_radius, G_PI_2, G_PI);
  cairo_arc(cr, x + capped_radius, y + capped_radius, capped_radius, G_PI, 3.0 * G_PI_2);
  cairo_close_path(cr);
}

const char *board_move_overlay_get_winner_banner_text(CheckersWinner winner) {
  switch (winner) {
    case CHECKERS_WINNER_WHITE:
      return "White wins!";
    case CHECKERS_WINNER_BLACK:
      return "Black wins!";
    case CHECKERS_WINNER_DRAW:
      return "Draw!";
    case CHECKERS_WINNER_NONE:
    default:
      return NULL;
  }
}

static void board_move_overlay_draw_arrow(cairo_t *cr,
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

static void board_move_overlay_draw(GtkDrawingArea * /*area*/,
                                    cairo_t *cr,
                                    int width,
                                    int height,
                                    gpointer user_data) {
  BoardMoveOverlay *self = BOARD_MOVE_OVERLAY(user_data);

  g_return_if_fail(BOARD_IS_MOVE_OVERLAY(self));
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

  const char *winner_banner = self->banner_text != NULL ? self->banner_text
                                                        : board_move_overlay_get_winner_banner_text(state->winner);

  CheckersMove move = {0};
  if (self->sgf_controller != NULL &&
      gcheckers_sgf_controller_get_current_node_move(self->sgf_controller, &move) &&
      move.length >= 2) {
    double cell_width = (double)width / state->board.board_size;
    double cell_height = (double)height / state->board.board_size;
    double arrow_size = fmin(cell_width, cell_height) * board_move_overlay_arrow_scale;

    cairo_save(cr);
    cairo_set_line_width(cr, board_move_overlay_stroke_width);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_source_rgba(cr, 0.2, 0.85, 0.2, board_move_overlay_alpha);

    for (uint8_t i = 1; i < move.length; ++i) {
      int start_row = 0;
      int start_col = 0;
      int end_row = 0;
      int end_col = 0;
      board_coord_from_index(move.path[i - 1], &start_row, &start_col, state->board.board_size);
      board_coord_from_index(move.path[i], &end_row, &end_col, state->board.board_size);
      board_coord_transform_for_bottom_color(&start_row, &start_col, state->board.board_size, self->bottom_color);
      board_coord_transform_for_bottom_color(&end_row, &end_col, state->board.board_size, self->bottom_color);

      double start_x = ((double)start_col + 0.5) * cell_width;
      double start_y = ((double)start_row + 0.5) * cell_height;
      double end_x = ((double)end_col + 0.5) * cell_width;
      double end_y = ((double)end_row + 0.5) * cell_height;

      board_move_overlay_draw_arrow(cr, start_x, start_y, end_x, end_y, arrow_size);
    }

    cairo_restore(cr);
  }

  if (winner_banner == NULL) {
    return;
  }

  cairo_save(cr);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, MAX(24.0, MIN(width, height) * 0.14));

  cairo_text_extents_t extents = {0};
  cairo_text_extents(cr, winner_banner, &extents);

  double banner_width = extents.width + board_move_overlay_banner_padding_x * 2.0;
  double banner_height = extents.height + board_move_overlay_banner_padding_y * 2.0;
  double banner_x = ((double)width - banner_width) / 2.0;
  double banner_y = ((double)height - banner_height) / 2.0;

  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.68);
  board_move_overlay_draw_rounded_rect(cr,
                                       banner_x,
                                       banner_y,
                                       banner_width,
                                       banner_height,
                                       board_move_overlay_banner_radius);
  cairo_fill(cr);

  double text_x = banner_x + board_move_overlay_banner_padding_x - extents.x_bearing;
  double text_y = banner_y + board_move_overlay_banner_padding_y - extents.y_bearing;
  cairo_move_to(cr, text_x, text_y);
  if (self->banner_text != NULL && self->banner_color == BOARD_MOVE_OVERLAY_BANNER_COLOR_RED) {
    cairo_set_source_rgb(cr, 0.95, 0.2, 0.2);
  } else {
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
  }
  cairo_show_text(cr, winner_banner);
  cairo_restore(cr);
}

static void board_move_overlay_dispose(GObject *object) {
  BoardMoveOverlay *self = BOARD_MOVE_OVERLAY(object);

  gboolean drawing_area_removed = TRUE;
  if (self->drawing_area) {
    drawing_area_removed = gcheckers_widget_remove_from_parent(self->drawing_area);
    if (!drawing_area_removed && gtk_widget_get_parent(self->drawing_area)) {
      g_debug("Failed to remove board overlay drawing area from parent during dispose\n");
    }
  }

  g_clear_object(&self->model);
  g_clear_object(&self->sgf_controller);
  g_clear_pointer(&self->banner_text, g_free);
  if (drawing_area_removed) {
    g_clear_object(&self->drawing_area);
  } else {
    self->drawing_area = NULL;
  }

  G_OBJECT_CLASS(board_move_overlay_parent_class)->dispose(object);
}

static void board_move_overlay_class_init(BoardMoveOverlayClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = board_move_overlay_dispose;
}

static void board_move_overlay_init(BoardMoveOverlay *self) {
  self->drawing_area = gtk_drawing_area_new();
  g_object_ref_sink(self->drawing_area);
  gtk_widget_set_hexpand(self->drawing_area, TRUE);
  gtk_widget_set_vexpand(self->drawing_area, TRUE);
  gtk_widget_set_can_target(self->drawing_area, FALSE);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self->drawing_area),
                                 board_move_overlay_draw,
                                 self,
                                 NULL);
  self->bottom_color = CHECKERS_COLOR_WHITE;
}

BoardMoveOverlay *board_move_overlay_new(void) {
  return g_object_new(BOARD_TYPE_MOVE_OVERLAY, NULL);
}

GtkWidget *board_move_overlay_get_widget(BoardMoveOverlay *self) {
  g_return_val_if_fail(BOARD_IS_MOVE_OVERLAY(self), NULL);

  return self->drawing_area;
}

void board_move_overlay_set_model(BoardMoveOverlay *self, GCheckersModel *model) {
  g_return_if_fail(BOARD_IS_MOVE_OVERLAY(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(model));

  if (self->model) {
    g_clear_object(&self->model);
  }

  self->model = g_object_ref(model);
}

void board_move_overlay_set_banner(BoardMoveOverlay *self,
                                   const char *text,
                                   BoardMoveOverlayBannerColor color) {
  g_return_if_fail(BOARD_IS_MOVE_OVERLAY(self));

  if (g_strcmp0(self->banner_text, text) == 0 && self->banner_color == color) {
    return;
  }

  g_free(self->banner_text);
  self->banner_text = g_strdup(text);
  self->banner_color = text != NULL ? color : BOARD_MOVE_OVERLAY_BANNER_COLOR_DEFAULT;
  board_move_overlay_queue_draw(self);
}

void board_move_overlay_set_sgf_controller(BoardMoveOverlay *self, GCheckersSgfController *controller) {
  g_return_if_fail(BOARD_IS_MOVE_OVERLAY(self));
  g_return_if_fail(GCHECKERS_IS_SGF_CONTROLLER(controller));

  if (self->sgf_controller != NULL) {
    g_clear_object(&self->sgf_controller);
  }

  self->sgf_controller = g_object_ref(controller);
}

void board_move_overlay_set_bottom_color(BoardMoveOverlay *self, CheckersColor bottom_color) {
  g_return_if_fail(BOARD_IS_MOVE_OVERLAY(self));
  g_return_if_fail(bottom_color == CHECKERS_COLOR_WHITE || bottom_color == CHECKERS_COLOR_BLACK);

  if (self->bottom_color == bottom_color) {
    return;
  }

  self->bottom_color = bottom_color;
  board_move_overlay_queue_draw(self);
}

void board_move_overlay_queue_draw(BoardMoveOverlay *self) {
  g_return_if_fail(BOARD_IS_MOVE_OVERLAY(self));

  if (self->drawing_area) {
    gtk_widget_queue_draw(self->drawing_area);
  }
}
