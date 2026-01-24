#include "board_move_overlay.h"

#include "board.h"
#include "widget_utils.h"

#include <math.h>

struct _BoardMoveOverlay {
  GObject parent_instance;
  GtkWidget *drawing_area;
  GCheckersModel *model;
};

G_DEFINE_TYPE(BoardMoveOverlay, board_move_overlay, G_TYPE_OBJECT)

static const double board_move_overlay_alpha = 0.5;
static const double board_move_overlay_stroke_width = 4.0;
static const double board_move_overlay_arrow_scale = 0.28;

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

  const CheckersMove *last_move = gcheckers_model_peek_last_move(self->model);
  if (!last_move || last_move->length < 2) {
    return;
  }

  double cell_width = (double)width / state->board.board_size;
  double cell_height = (double)height / state->board.board_size;
  double arrow_size = fmin(cell_width, cell_height) * board_move_overlay_arrow_scale;

  cairo_save(cr);
  cairo_set_line_width(cr, board_move_overlay_stroke_width);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_set_source_rgba(cr, 0.2, 0.85, 0.2, board_move_overlay_alpha);

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

    board_move_overlay_draw_arrow(cr, start_x, start_y, end_x, end_y, arrow_size);
  }

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

void board_move_overlay_queue_draw(BoardMoveOverlay *self) {
  g_return_if_fail(BOARD_IS_MOVE_OVERLAY(self));

  if (self->drawing_area) {
    gtk_widget_queue_draw(self->drawing_area);
  }
}
