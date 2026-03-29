#include <gtk/gtk.h>

#include "board.h"
#include "board_move_overlay.h"
#include "board_view.h"
#include "checkers_model.h"
#include "game.h"

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

static gboolean test_board_view_count_square_click(guint8 /*index*/, guint /*button*/, gpointer user_data) {
  guint *count = user_data;
  g_return_val_if_fail(count != NULL, FALSE);
  *count += 1;
  return TRUE;
}

static void test_board_move_overlay_winner_banner_text(void) {
  g_assert_null(board_move_overlay_get_winner_banner_text(CHECKERS_WINNER_NONE));
  g_assert_cmpstr(board_move_overlay_get_winner_banner_text(CHECKERS_WINNER_WHITE), ==, "White wins!");
  g_assert_cmpstr(board_move_overlay_get_winner_banner_text(CHECKERS_WINNER_BLACK), ==, "Black wins!");
  g_assert_cmpstr(board_move_overlay_get_winner_banner_text(CHECKERS_WINNER_DRAW), ==, "Draw!");
}

static void test_board_view_highlights_black_turn_moves(void) {
  GCheckersModel *model = gcheckers_model_new();
  BoardView *view = board_view_new();
  board_view_set_model(view, model);

  MoveList white_moves = gcheckers_model_list_moves(model);
  g_assert_cmpuint(white_moves.count, >, 0);
  g_assert_true(gcheckers_model_apply_move(model, &white_moves.moves[0]));
  movelist_free(&white_moves);

  const GameState *state = gcheckers_model_peek_state(model);
  g_assert_nonnull(state);
  g_assert_cmpint(state->turn, ==, CHECKERS_COLOR_BLACK);

  board_view_clear_selection(view);
  board_view_update(view);

  MoveList black_moves = gcheckers_model_list_moves(model);
  g_assert_cmpuint(black_moves.count, >, 0);

  bool expected_starts[CHECKERS_MAX_SQUARES] = {false};
  game_moves_collect_starts(&black_moves, expected_starts);

  GtkWidget *root = board_view_get_widget(view);
  g_assert_nonnull(root);

  g_autoptr(GHashTable) buttons_by_index =
      g_hash_table_new(g_direct_hash, g_direct_equal);
  test_board_view_collect_indexed_buttons(root, buttons_by_index);

  int squares = board_playable_squares(state->board.board_size);
  for (int idx = 0; idx < squares; ++idx) {
    GtkWidget *button = g_hash_table_lookup(buttons_by_index, GINT_TO_POINTER(idx));
    g_assert_nonnull(button);

    gboolean has_halo = gtk_widget_has_css_class(button, "board-halo");
    if (expected_starts[idx]) {
      g_assert_true(has_halo);
    } else {
      g_assert_false(has_halo);
    }
  }

  movelist_free(&black_moves);
  g_clear_object(&view);
  g_clear_object(&model);
}

static void test_board_view_repeated_primary_clicks_are_processed(void) {
  GCheckersModel *model = gcheckers_model_new();
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
    g_test_add_func("/board-view/highlights-black-turn-moves", test_board_view_skip);
    g_test_add_func("/board-view/repeated-primary-clicks-are-processed", test_board_view_skip);
    return g_test_run();
  }

  g_test_add_func("/board-move-overlay/winner-banner-text", test_board_move_overlay_winner_banner_text);
  g_test_add_func("/board-view/highlights-black-turn-moves",
                  test_board_view_highlights_black_turn_moves);
  g_test_add_func("/board-view/repeated-primary-clicks-are-processed",
                  test_board_view_repeated_primary_clicks_are_processed);
  return g_test_run();
}
