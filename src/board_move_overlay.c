#include "board_move_overlay.h"

#include "games/boop/boop_game.h"
#include "games/checkers/game.h"
#include "sgf_controller.h"
#include "widget_utils.h"

#include <math.h>

struct _BoardMoveOverlay {
  GObject parent_instance;
  GtkWidget *drawing_area;
  GGameModel *model;
  GGameSgfController *sgf_controller;
  guint bottom_side;
  char *banner_text;
  BoardMoveOverlayBannerColor banner_color;
};

G_DEFINE_TYPE(BoardMoveOverlay, board_move_overlay, G_TYPE_OBJECT)

static const double board_move_overlay_alpha = 0.5;
static const double board_move_overlay_stroke_width = 4.0;
static const double board_move_overlay_arrow_scale = 0.28;
static const double board_move_overlay_circle_radius_scale = 0.32;
static const double board_move_overlay_cross_scale = 0.26;
static const double board_move_overlay_banner_padding_x = 24.0;
static const double board_move_overlay_banner_padding_y = 14.0;
static const double board_move_overlay_banner_radius = 18.0;

static gboolean board_move_overlay_backend_matches(const GameBackend *backend, const char *backend_id) {
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(backend_id != NULL, FALSE);

  return g_strcmp0(backend->id, backend_id) == 0;
}

static void board_move_overlay_transform_point_for_bottom_side(double *row,
                                                               double *col,
                                                               guint rows,
                                                               guint cols,
                                                               guint bottom_side) {
  g_return_if_fail(row != NULL);
  g_return_if_fail(col != NULL);

  if (bottom_side == 0) {
    return;
  }

  *row = ((double)rows - 1.0) - *row;
  *col = ((double)cols - 1.0) - *col;
}

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

const char *board_move_overlay_get_winner_banner_text(const GameBackend *backend, GameBackendOutcome outcome) {
  g_return_val_if_fail(backend != NULL, NULL);
  g_return_val_if_fail(backend->outcome_banner_text != NULL, NULL);

  if (outcome == GAME_BACKEND_OUTCOME_ONGOING) {
    return NULL;
  }

  return backend->outcome_banner_text(outcome);
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

static void board_move_overlay_draw_circle(cairo_t *cr,
                                           double center_x,
                                           double center_y,
                                           double radius) {
  g_return_if_fail(cr != NULL);
  g_return_if_fail(radius > 0.0);

  cairo_arc(cr, center_x, center_y, radius, 0.0, G_PI * 2.0);
  cairo_stroke(cr);
}

static void board_move_overlay_draw_cross(cairo_t *cr,
                                          double center_x,
                                          double center_y,
                                          double half_size) {
  g_return_if_fail(cr != NULL);
  g_return_if_fail(half_size > 0.0);

  cairo_move_to(cr, center_x - half_size, center_y - half_size);
  cairo_line_to(cr, center_x + half_size, center_y + half_size);
  cairo_move_to(cr, center_x - half_size, center_y + half_size);
  cairo_line_to(cr, center_x + half_size, center_y - half_size);
  cairo_stroke(cr);
}

static void board_move_overlay_draw_checkers_last_move(BoardMoveOverlay *self,
                                                       const GameBackend *backend,
                                                       gconstpointer position,
                                                       cairo_t *cr,
                                                       guint rows,
                                                       guint cols,
                                                       gint width,
                                                       gint height) {
  CheckersMove move = {0};

  g_return_if_fail(self != NULL);
  g_return_if_fail(backend != NULL);
  g_return_if_fail(position != NULL);
  g_return_if_fail(cr != NULL);

  if (self->sgf_controller == NULL ||
      !ggame_sgf_controller_get_current_node_move(self->sgf_controller, &move) ||
      move.length < 2) {
    return;
  }

  guint path_length = 0;
  guint path[128] = {0};
  if (!backend->square_grid_move_get_path(&move, &path_length, path, G_N_ELEMENTS(path)) || path_length < 2) {
    return;
  }

  double cell_width = (double)width / cols;
  double cell_height = (double)height / rows;
  double arrow_size = fmin(cell_width, cell_height) * board_move_overlay_arrow_scale;

  cairo_save(cr);
  cairo_set_line_width(cr, board_move_overlay_stroke_width);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_set_source_rgba(cr, 0.2, 0.85, 0.2, board_move_overlay_alpha);

  for (guint i = 1; i < path_length; ++i) {
    guint start_row_index = 0;
    guint start_col_index = 0;
    guint end_row_index = 0;
    guint end_col_index = 0;

    if (!backend->square_grid_index_coord(position, path[i - 1], &start_row_index, &start_col_index) ||
        !backend->square_grid_index_coord(position, path[i], &end_row_index, &end_col_index)) {
      continue;
    }

    double start_row = start_row_index;
    double start_col = start_col_index;
    double end_row = end_row_index;
    double end_col = end_col_index;
    board_move_overlay_transform_point_for_bottom_side(&start_row, &start_col, rows, cols, self->bottom_side);
    board_move_overlay_transform_point_for_bottom_side(&end_row, &end_col, rows, cols, self->bottom_side);

    double start_x = (start_col + 0.5) * cell_width;
    double start_y = (start_row + 0.5) * cell_height;
    double end_x = (end_col + 0.5) * cell_width;
    double end_y = (end_row + 0.5) * cell_height;
    board_move_overlay_draw_arrow(cr, start_x, start_y, end_x, end_y, arrow_size);
  }

  cairo_restore(cr);
}

static gboolean board_move_overlay_replay_position_for_node(BoardMoveOverlay *self,
                                                            const SgfNode *node,
                                                            gpointer position,
                                                            GError **error) {
  const GameBackend *backend = NULL;

  g_return_val_if_fail(self != NULL, FALSE);
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(GGAME_IS_MODEL(self->model), FALSE);

  backend = ggame_model_peek_backend(self->model);
  g_return_val_if_fail(backend != NULL, FALSE);
  return ggame_sgf_controller_replay_node_into_position(node, backend, position, error);
}

void board_move_overlay_render_boop_overlay_info(cairo_t *cr,
                                                 const BoopMoveOverlayInfo *overlay_info,
                                                 guint rows,
                                                 guint cols,
                                                 gint width,
                                                 gint height,
                                                 guint bottom_side) {
  g_return_if_fail(cr != NULL);
  g_return_if_fail(overlay_info != NULL);
  g_return_if_fail(rows > 0);
  g_return_if_fail(cols > 0);
  g_return_if_fail(width > 0);
  g_return_if_fail(height > 0);

  double cell_width = (double)width / cols;
  double cell_height = (double)height / rows;
  double arrow_size = fmin(cell_width, cell_height) * board_move_overlay_arrow_scale;
  double circle_radius = fmin(cell_width, cell_height) * board_move_overlay_circle_radius_scale;
  double cross_half_size = fmin(cell_width, cell_height) * board_move_overlay_cross_scale;

  cairo_save(cr);
  cairo_set_line_width(cr, board_move_overlay_stroke_width);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_set_source_rgba(cr, 0.2, 0.85, 0.2, board_move_overlay_alpha);

  guint placed_row_index = 0;
  guint placed_col_index = 0;
  if (boop_square_to_coord(overlay_info->placed_square, &placed_row_index, &placed_col_index)) {
    double placed_row = placed_row_index;
    double placed_col = placed_col_index;
    board_move_overlay_transform_point_for_bottom_side(&placed_row, &placed_col, rows, cols, bottom_side);
    board_move_overlay_draw_circle(cr,
                                   (placed_col + 0.5) * cell_width,
                                   (placed_row + 0.5) * cell_height,
                                   circle_radius);
  }

  for (guint i = 0; i < overlay_info->arrow_count; ++i) {
    const BoopMoveOverlayArrow *arrow = &overlay_info->arrows[i];
    guint start_row_index = 0;
    guint start_col_index = 0;

    if (!boop_square_to_coord(arrow->from_square, &start_row_index, &start_col_index)) {
      continue;
    }

    double start_row = start_row_index;
    double start_col = start_col_index;
    double end_row = start_row + arrow->row_delta;
    double end_col = start_col + arrow->col_delta;
    if (!arrow->leaves_board) {
      guint end_row_index = 0;
      guint end_col_index = 0;
      if (!boop_square_to_coord(arrow->to_square, &end_row_index, &end_col_index)) {
        continue;
      }
      end_row = end_row_index;
      end_col = end_col_index;
    }

    board_move_overlay_transform_point_for_bottom_side(&start_row, &start_col, rows, cols, bottom_side);
    board_move_overlay_transform_point_for_bottom_side(&end_row, &end_col, rows, cols, bottom_side);

    board_move_overlay_draw_arrow(cr,
                                  (start_col + 0.5) * cell_width,
                                  (start_row + 0.5) * cell_height,
                                  (end_col + 0.5) * cell_width,
                                  (end_row + 0.5) * cell_height,
                                  arrow_size);
  }

  cairo_set_source_rgba(cr, 0.95, 0.2, 0.2, board_move_overlay_alpha + 0.15);
  for (guint i = 0; i < overlay_info->removed_square_count; ++i) {
    guint row_index = 0;
    guint col_index = 0;

    if (!boop_square_to_coord(overlay_info->removed_squares[i], &row_index, &col_index)) {
      continue;
    }

    double row = row_index;
    double col = col_index;
    board_move_overlay_transform_point_for_bottom_side(&row, &col, rows, cols, bottom_side);
    board_move_overlay_draw_cross(cr,
                                  (col + 0.5) * cell_width,
                                  (row + 0.5) * cell_height,
                                  cross_half_size);
  }

  cairo_restore(cr);
}

static void board_move_overlay_draw_boop_last_move(BoardMoveOverlay *self,
                                                   const GameBackend *backend,
                                                   cairo_t *cr,
                                                   guint rows,
                                                   guint cols,
                                                   gint width,
                                                   gint height) {
  BoopMove move = {0};
  BoopMoveOverlayInfo overlay_info = {0};
  const SgfNode *current = NULL;
  const SgfNode *parent = NULL;
  const GameBackendVariant *variant = NULL;

  g_return_if_fail(self != NULL);
  g_return_if_fail(backend != NULL);
  g_return_if_fail(cr != NULL);
  g_return_if_fail(self->sgf_controller != NULL);
  g_return_if_fail(GGAME_IS_MODEL(self->model));
  g_return_if_fail(backend->position_size > 0);
  g_return_if_fail(backend->position_init != NULL);
  g_return_if_fail(backend->position_clear != NULL);

  if (!ggame_sgf_controller_get_current_node_move(self->sgf_controller, &move)) {
    return;
  }

  current = sgf_tree_get_current(ggame_sgf_controller_get_tree(self->sgf_controller));
  if (current == NULL) {
    return;
  }

  parent = sgf_node_get_parent(current);
  if (parent == NULL) {
    return;
  }

  variant = ggame_model_peek_variant(self->model);
  g_autofree guint8 *before_position = g_malloc0(backend->position_size);
  g_return_if_fail(before_position != NULL);
  backend->position_init(before_position, variant);

  g_autoptr(GError) replay_error = NULL;
  if (!board_move_overlay_replay_position_for_node(self, parent, before_position, &replay_error)) {
    g_debug("Failed to reconstruct boop parent position for overlay: %s",
            replay_error != NULL ? replay_error->message : "unknown error");
    backend->position_clear(before_position);
    return;
  }

  if (!boop_move_describe_overlay((const BoopPosition *)before_position, &move, &overlay_info)) {
    backend->position_clear(before_position);
    return;
  }

  backend->position_clear(before_position);
  board_move_overlay_render_boop_overlay_info(cr, &overlay_info, rows, cols, width, height, self->bottom_side);
}

static void board_move_overlay_draw(GtkDrawingArea * /*area*/,
                                    cairo_t *cr,
                                    int width,
                                    int height,
                                    gpointer user_data) {
  BoardMoveOverlay *self = BOARD_MOVE_OVERLAY(user_data);
  const GameBackend *backend = NULL;
  gconstpointer position = NULL;

  g_return_if_fail(BOARD_IS_MOVE_OVERLAY(self));
  g_return_if_fail(GGAME_IS_MODEL(self->model));
  g_return_if_fail(cr != NULL);

  if (width <= 0 || height <= 0) {
    return;
  }

  backend = ggame_model_peek_backend(self->model);
  position = ggame_model_peek_position(self->model);
  if (backend == NULL || position == NULL) {
    return;
  }
  g_return_if_fail(backend->position_outcome != NULL);
  g_return_if_fail(backend->square_grid_rows != NULL);
  g_return_if_fail(backend->square_grid_cols != NULL);

  guint rows = backend->square_grid_rows(position);
  guint cols = backend->square_grid_cols(position);
  if (rows == 0 || cols == 0) {
    g_debug("Board grid dimensions were zero when drawing last move overlay");
    return;
  }

  GameBackendOutcome outcome = backend->position_outcome(position);
  const char *winner_banner = self->banner_text != NULL ? self->banner_text
                                                        : board_move_overlay_get_winner_banner_text(backend, outcome);

  if (board_move_overlay_backend_matches(backend, "checkers") &&
      backend->square_grid_index_coord != NULL &&
      backend->square_grid_move_get_path != NULL) {
    board_move_overlay_draw_checkers_last_move(self, backend, position, cr, rows, cols, width, height);
  }
  if (board_move_overlay_backend_matches(backend, "boop")) {
    board_move_overlay_draw_boop_last_move(self, backend, cr, rows, cols, width, height);
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
  double banner_x = ((double) width - banner_width) / 2.0;
  double banner_y = ((double) height - banner_height) / 2.0;

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

  if (self->drawing_area != NULL) {
    drawing_area_removed = ggame_widget_remove_from_parent(self->drawing_area);
    if (!drawing_area_removed && gtk_widget_get_parent(self->drawing_area) != NULL) {
      g_debug("Failed to remove board overlay drawing area from parent during dispose");
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
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self->drawing_area), board_move_overlay_draw, self, NULL);
  self->bottom_side = 0;
}

BoardMoveOverlay *board_move_overlay_new(void) {
  return g_object_new(BOARD_TYPE_MOVE_OVERLAY, NULL);
}

GtkWidget *board_move_overlay_get_widget(BoardMoveOverlay *self) {
  g_return_val_if_fail(BOARD_IS_MOVE_OVERLAY(self), NULL);

  return self->drawing_area;
}

void board_move_overlay_set_model(BoardMoveOverlay *self, GGameModel *model) {
  g_return_if_fail(BOARD_IS_MOVE_OVERLAY(self));
  g_return_if_fail(GGAME_IS_MODEL(model));

  g_set_object(&self->model, model);
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

void board_move_overlay_set_sgf_controller(BoardMoveOverlay *self, GGameSgfController *controller) {
  g_return_if_fail(BOARD_IS_MOVE_OVERLAY(self));
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(controller));

  g_set_object(&self->sgf_controller, controller);
}

void board_move_overlay_set_bottom_side(BoardMoveOverlay *self, guint bottom_side) {
  g_return_if_fail(BOARD_IS_MOVE_OVERLAY(self));

  if (self->bottom_side == bottom_side) {
    return;
  }

  self->bottom_side = bottom_side;
  board_move_overlay_queue_draw(self);
}

void board_move_overlay_queue_draw(BoardMoveOverlay *self) {
  g_return_if_fail(BOARD_IS_MOVE_OVERLAY(self));

  if (self->drawing_area != NULL) {
    gtk_widget_queue_draw(self->drawing_area);
  }
}
