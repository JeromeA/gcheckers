#include <gtk/gtk.h>

#include "active_game_backend.h"
#include "board_move_overlay.h"
#include "board_view.h"
#include "game_model.h"

static void test_board_view_skip(void) {
  g_test_skip("GTK display not available.");
}

static void test_board_view_collect_indexed_buttons(GtkWidget *root, GHashTable *buttons_by_index) {
  g_return_if_fail(GTK_IS_WIDGET(root));
  g_return_if_fail(buttons_by_index != NULL);

  if (GTK_IS_BUTTON(root)) {
    gpointer data = g_object_get_data(G_OBJECT(root), "board-index");
    if (data != NULL) {
      int index = GPOINTER_TO_INT(data) - 1;
      if (index >= 0) {
        g_hash_table_insert(buttons_by_index, GINT_TO_POINTER(index), root);
      }
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    test_board_view_collect_indexed_buttons(child, buttons_by_index);
  }
}

static gboolean test_board_view_count_square_click(guint /*index*/, guint /*button*/, gpointer user_data) {
  guint *count = user_data;
  g_return_val_if_fail(count != NULL, FALSE);
  *count += 1;
  return TRUE;
}

#if defined(GGAME_GAME_BOOP)
static void test_board_view_surface_read_pixel(cairo_surface_t *surface,
                                               int x,
                                               int y,
                                               guint8 *out_red,
                                               guint8 *out_green,
                                               guint8 *out_blue,
                                               guint8 *out_alpha) {
  g_return_if_fail(surface != NULL);
  g_return_if_fail(out_red != NULL);
  g_return_if_fail(out_green != NULL);
  g_return_if_fail(out_blue != NULL);
  g_return_if_fail(out_alpha != NULL);

  int width = cairo_image_surface_get_width(surface);
  int height = cairo_image_surface_get_height(surface);
  g_return_if_fail(x >= 0 && x < width);
  g_return_if_fail(y >= 0 && y < height);

  cairo_surface_flush(surface);
  unsigned char *data = cairo_image_surface_get_data(surface);
  int stride = cairo_image_surface_get_stride(surface);
  unsigned char *pixel = data + y * stride + x * 4;
  *out_blue = pixel[0];
  *out_green = pixel[1];
  *out_red = pixel[2];
  *out_alpha = pixel[3];
}

static void test_board_move_overlay_removed_markers_paint_above_off_board_arrows(void) {
  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 120, 120);
  cairo_t *cr = cairo_create(surface);
  BoopMoveOverlayInfo overlay_info = {
    .placed_square = BOOP_INVALID_SQUARE,
    .arrow_count = 1,
    .arrows =
        {
          {
            .from_square = 0,
            .to_square = BOOP_INVALID_SQUARE,
            .row_delta = -1,
            .col_delta = -1,
            .leaves_board = TRUE,
          },
        },
    .removed_square_count = 1,
    .removed_squares = {0},
  };

  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
  cairo_paint(cr);
  board_move_overlay_render_boop_overlay_info(cr, &overlay_info, BOOP_BOARD_SIZE, BOOP_BOARD_SIZE, 120, 120, 0);
  cairo_destroy(cr);

  guint8 red = 0;
  guint8 green = 0;
  guint8 blue = 0;
  guint8 alpha = 0;
  test_board_view_surface_read_pixel(surface, 10, 10, &red, &green, &blue, &alpha);
  g_assert_cmpuint(alpha, >, 0);
  g_assert_cmpuint(red, >, green);
  g_assert_cmpuint(red, >, blue);

  cairo_surface_destroy(surface);
}
#endif

static void test_board_move_overlay_winner_banner_text(void) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;

  g_assert_nonnull(backend);
#if defined(GGAME_GAME_BOOP)
  g_assert_null(board_move_overlay_get_winner_banner_text(backend, GAME_BACKEND_OUTCOME_ONGOING));
  g_assert_cmpstr(board_move_overlay_get_winner_banner_text(backend, GAME_BACKEND_OUTCOME_SIDE_0_WIN),
                  ==,
                  "Player 1 wins");
  g_assert_cmpstr(board_move_overlay_get_winner_banner_text(backend, GAME_BACKEND_OUTCOME_SIDE_1_WIN),
                  ==,
                  "Player 2 wins");
  g_assert_cmpstr(board_move_overlay_get_winner_banner_text(backend, GAME_BACKEND_OUTCOME_DRAW), ==, "Draw");
#else
  g_assert_null(board_move_overlay_get_winner_banner_text(backend, GAME_BACKEND_OUTCOME_ONGOING));
  g_assert_cmpstr(board_move_overlay_get_winner_banner_text(backend, GAME_BACKEND_OUTCOME_SIDE_0_WIN),
                  ==,
                  "White wins!");
  g_assert_cmpstr(board_move_overlay_get_winner_banner_text(backend, GAME_BACKEND_OUTCOME_SIDE_1_WIN),
                  ==,
                  "Black wins!");
  g_assert_cmpstr(board_move_overlay_get_winner_banner_text(backend, GAME_BACKEND_OUTCOME_DRAW), ==, "Draw!");
#endif
}

static void test_board_view_highlights_black_turn_moves(void) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  GGameModel *model = ggame_model_new(backend);
  BoardView *view = board_view_new();
  board_view_set_model(view, model);

  GameBackendMoveList white_moves = ggame_model_list_moves(model);
  g_assert_cmpuint(white_moves.count, >, 0);
  gconstpointer first_move = backend->move_list_get(&white_moves, 0);
  g_assert_nonnull(first_move);
  g_assert_true(ggame_model_apply_move(model, first_move));
  backend->move_list_free(&white_moves);

  gconstpointer position = ggame_model_peek_position(model);
  g_assert_nonnull(position);
  g_assert_cmpuint(backend->position_turn(position), ==, 1);

  board_view_clear_selection(view);
  board_view_update(view);

  GameBackendMoveList black_moves = ggame_model_list_moves(model);
  g_assert_cmpuint(black_moves.count, >, 0);

  gboolean expected_starts[128] = {FALSE};
  backend->square_grid_moves_collect_starts(&black_moves, expected_starts, G_N_ELEMENTS(expected_starts));

  GtkWidget *root = board_view_get_widget(view);
  g_assert_nonnull(root);

  g_autoptr(GHashTable) buttons_by_index =
      g_hash_table_new(g_direct_hash, g_direct_equal);
  test_board_view_collect_indexed_buttons(root, buttons_by_index);

  guint rows = backend->square_grid_rows(position);
  guint cols = backend->square_grid_cols(position);
  for (guint row = 0; row < rows; ++row) {
    for (guint col = 0; col < cols; ++col) {
      guint idx = 0;
      if (!backend->square_grid_square_playable(position, row, col) ||
          !backend->square_grid_square_index(position, row, col, &idx)) {
        continue;
      }

      GtkWidget *button = g_hash_table_lookup(buttons_by_index, GINT_TO_POINTER(idx));
      g_assert_nonnull(button);

      gboolean has_halo = gtk_widget_has_css_class(button, "board-halo");
      if (expected_starts[idx]) {
        g_assert_true(has_halo);
      } else {
        g_assert_false(has_halo);
      }
    }
  }

  backend->move_list_free(&black_moves);
  g_clear_object(&view);
  g_clear_object(&model);
}

static void test_board_view_updates_on_model_state_changed(void) {
  const GameBackend *backend = GGAME_ACTIVE_GAME_BACKEND;
  GGameModel *model = ggame_model_new(backend);
  BoardView *view = board_view_new();
  GtkWidget *root = NULL;
  g_autoptr(GHashTable) buttons_by_index = NULL;
  GameBackendMoveList white_moves = {0};
  GameBackendMoveList black_moves = {0};
  gconstpointer first_move = NULL;
  gconstpointer position = NULL;

  board_view_set_model(view, model);

  root = board_view_get_widget(view);
  g_assert_nonnull(root);
  buttons_by_index = g_hash_table_new(g_direct_hash, g_direct_equal);
  test_board_view_collect_indexed_buttons(root, buttons_by_index);

  white_moves = ggame_model_list_moves(model);
  g_assert_cmpuint(white_moves.count, >, 0);
  first_move = backend->move_list_get(&white_moves, 0);
  g_assert_nonnull(first_move);
  g_assert_true(ggame_model_apply_move(model, first_move));
  backend->move_list_free(&white_moves);

  position = ggame_model_peek_position(model);
  g_assert_nonnull(position);
  g_assert_cmpuint(backend->position_turn(position), ==, 1);

  black_moves = ggame_model_list_moves(model);
  g_assert_cmpuint(black_moves.count, >, 0);

  gboolean expected_starts[128] = {FALSE};
  backend->square_grid_moves_collect_starts(&black_moves, expected_starts, G_N_ELEMENTS(expected_starts));

  guint rows = backend->square_grid_rows(position);
  guint cols = backend->square_grid_cols(position);
  for (guint row = 0; row < rows; ++row) {
    for (guint col = 0; col < cols; ++col) {
      guint idx = 0;
      GtkWidget *button = NULL;
      gboolean has_halo = FALSE;

      if (!backend->square_grid_square_playable(position, row, col) ||
          !backend->square_grid_square_index(position, row, col, &idx)) {
        continue;
      }

      button = g_hash_table_lookup(buttons_by_index, GINT_TO_POINTER(idx));
      g_assert_nonnull(button);

      has_halo = gtk_widget_has_css_class(button, "board-halo");
      if (expected_starts[idx]) {
        g_assert_true(has_halo);
      } else {
        g_assert_false(has_halo);
      }
    }
  }

  backend->move_list_free(&black_moves);
  g_clear_object(&view);
  g_clear_object(&model);
}

static void test_board_view_repeated_primary_clicks_are_processed(void) {
  GGameModel *model = ggame_model_new(GGAME_ACTIVE_GAME_BACKEND);
  BoardView *view = board_view_new();
  board_view_set_model(view, model);

  guint click_count = 0;
  board_view_set_square_handler(view, test_board_view_count_square_click, &click_count);

  GtkWidget *root = board_view_get_widget(view);
  g_assert_nonnull(root);

  g_autoptr(GHashTable) buttons_by_index = g_hash_table_new(g_direct_hash, g_direct_equal);
  test_board_view_collect_indexed_buttons(root, buttons_by_index);

  GtkWidget *button = g_hash_table_lookup(buttons_by_index, GINT_TO_POINTER(0));
  g_assert_nonnull(button);
  g_assert_true(GTK_IS_BUTTON(button));

  g_signal_emit_by_name(button, "clicked");
  g_signal_emit_by_name(button, "clicked");

  g_assert_cmpuint(click_count, ==, 2);

  g_clear_object(&view);
  g_clear_object(&model);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  if (!gtk_init_check()) {
    g_test_add_func("/board-move-overlay/winner-banner-text", test_board_move_overlay_winner_banner_text);
#if defined(GGAME_GAME_BOOP)
    g_test_add_func("/board-move-overlay/removed-markers-paint-above-off-board-arrows",
                    test_board_move_overlay_removed_markers_paint_above_off_board_arrows);
#endif
    g_test_add_func("/board-view/highlights-black-turn-moves", test_board_view_skip);
    g_test_add_func("/board-view/updates-on-model-state-changed", test_board_view_skip);
    g_test_add_func("/board-view/repeated-primary-clicks-are-processed", test_board_view_skip);
    return g_test_run();
  }

  g_test_add_func("/board-move-overlay/winner-banner-text", test_board_move_overlay_winner_banner_text);
#if defined(GGAME_GAME_BOOP)
  g_test_add_func("/board-move-overlay/removed-markers-paint-above-off-board-arrows",
                  test_board_move_overlay_removed_markers_paint_above_off_board_arrows);
#endif
  g_test_add_func("/board-view/highlights-black-turn-moves",
                  test_board_view_highlights_black_turn_moves);
  g_test_add_func("/board-view/updates-on-model-state-changed",
                  test_board_view_updates_on_model_state_changed);
  g_test_add_func("/board-view/repeated-primary-clicks-are-processed",
                  test_board_view_repeated_primary_clicks_are_processed);
  return g_test_run();
}
