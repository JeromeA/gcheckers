#include <gtk/gtk.h>

#include "src/game_app_profile.h"
#include "src/game_model.h"
#include "src/games/homeworlds/homeworlds_backend.h"
#include "src/games/homeworlds/homeworlds_view.h"
#include "src/sgf_autosave.h"
#include "src/window.h"

static GtkApplication *test_homeworlds_app = NULL;

static void test_homeworlds_skip(void) {
  g_test_skip("Skipped by minimal reproducer.");
}

static void test_homeworlds_drain_main_context(guint max_iterations) {
  g_return_if_fail(max_iterations > 0);

  for (guint i = 0; i < max_iterations; i++) {
    if (!g_main_context_iteration(NULL, FALSE)) {
      return;
    }
  }

  g_debug("Main context still busy after %u iterations\n", max_iterations);
}

static GtkWidget *test_homeworlds_find_widget_named(GtkWidget *root, const char *name) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(name != NULL, NULL);

  if (g_strcmp0(gtk_widget_get_name(root), name) == 0) {
    return root;
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkWidget *match = test_homeworlds_find_widget_named(child, name);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkWidget *test_homeworlds_find_label_with_text(GtkWidget *root, const char *text) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(text != NULL, NULL);

  if (GTK_IS_LABEL(root) && g_strcmp0(gtk_label_get_text(GTK_LABEL(root)), text) == 0) {
    return root;
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkWidget *match = test_homeworlds_find_label_with_text(child, text);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkApplication *test_homeworlds_create_app(void) {
  if (test_homeworlds_app == NULL) {
    GError *error = NULL;

    test_homeworlds_app = gtk_application_new("io.github.jeromea.ghomeworlds.test",
                                              G_APPLICATION_DEFAULT_FLAGS | G_APPLICATION_NON_UNIQUE);
    g_assert_nonnull(test_homeworlds_app);
    g_assert_true(g_application_register(G_APPLICATION(test_homeworlds_app), NULL, &error));
    g_assert_no_error(error);
  }

  return g_object_ref(test_homeworlds_app);
}

static GGameWindow *test_homeworlds_create_window(GtkApplication **out_app, GGameModel **out_model) {
  GtkApplication *app = test_homeworlds_create_app();
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  GGameWindow *window = NULL;

  g_assert_nonnull(model);
  window = ggame_window_new(app, model);
  g_assert_nonnull(window);

  if (out_app != NULL) {
    *out_app = app;
  } else {
    g_object_unref(app);
  }
  if (out_model != NULL) {
    *out_model = model;
  } else {
    g_object_unref(model);
  }

  return window;
}

static HomeworldsView *test_homeworlds_get_window_view(GGameWindow *window) {
  GtkWidget *view_widget = NULL;
  HomeworldsView *view = NULL;

  g_return_val_if_fail(GGAME_IS_WINDOW(window), NULL);

  view_widget = test_homeworlds_find_widget_named(GTK_WIDGET(window), "homeworlds-view");
  g_return_val_if_fail(GTK_IS_WIDGET(view_widget), NULL);

  view = g_object_get_data(G_OBJECT(view_widget), "homeworlds-view-state");
  g_return_val_if_fail(view != NULL, NULL);
  return view;
}

static HomeworldsView *test_homeworlds_view_new_without_move_report(GGameModel *model) {
  HomeworldsView *view = NULL;

  g_return_val_if_fail(GGAME_IS_MODEL(model), NULL);

  view = homeworlds_view_new(model);
  g_return_val_if_fail(view != NULL, NULL);
  homeworlds_view_set_move_report_enabled(view, FALSE);
  return view;
}

static void test_homeworlds_assert_text_panel_label_wraps(GtkWidget *label) {
  g_assert_true(GTK_IS_LABEL(label));
  g_assert_true(gtk_label_get_wrap(GTK_LABEL(label)));
  g_assert_cmpuint(gtk_label_get_wrap_mode(GTK_LABEL(label)), ==, PANGO_WRAP_WORD_CHAR);
  g_assert_cmpuint(gtk_label_get_natural_wrap_mode(GTK_LABEL(label)), ==, GTK_NATURAL_WRAP_WORD);
  g_assert_cmpint(gtk_label_get_width_chars(GTK_LABEL(label)), ==, -1);
  g_assert_cmpint(gtk_label_get_max_width_chars(GTK_LABEL(label)), ==, -1);
  g_assert_cmpuint(gtk_widget_get_halign(label), ==, GTK_ALIGN_FILL);
  g_assert_true(gtk_widget_get_hexpand(label));
}

static void test_homeworlds_view_text_panel_has_fixed_width(void) {
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = test_homeworlds_view_new_without_move_report(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  GtkWidget *text_panel = test_homeworlds_find_widget_named(root, "homeworlds-text-panel");
  GtkWidget *text_panel_content = test_homeworlds_find_widget_named(root, "homeworlds-text-panel-content");
  GtkApplication *app = NULL;
  GGameModel *window_model = NULL;
  GGameWindow *window = NULL;
  HomeworldsView *window_view = NULL;
  GtkWidget *window_text_panel = NULL;
  GtkWidget *window_text_panel_content = NULL;
  GtkWidget *window_board = NULL;
  GtkWidget *window_board_scroller = NULL;
  GtkWidget *window_bank = NULL;
  graphene_rect_t content_bounds = {0};
  graphene_rect_t label_bounds = {0};
  graphene_rect_t bank_bounds = {0};
  GtkWidget *catastrophe_reset_label = NULL;
  GtkWidget *visual_choice_label = NULL;
  GtkPolicyType horizontal_policy = GTK_POLICY_AUTOMATIC;
  GtkPolicyType vertical_policy = GTK_POLICY_NEVER;
  gint width_request = -1;
  gint content_width_request = -1;
  gint allocated_width = 0;
  gint board_viewport_width = 0;

  g_assert_nonnull(text_panel);
  g_assert_nonnull(text_panel_content);
  g_assert_true(GTK_IS_SCROLLED_WINDOW(text_panel));
  gtk_widget_get_size_request(text_panel, &width_request, NULL);
  gtk_widget_get_size_request(text_panel_content, &content_width_request, NULL);
  g_assert_cmpint(width_request, ==, 350);
  g_assert_cmpint(content_width_request, ==, 326);
  g_assert_cmpint(gtk_scrolled_window_get_min_content_width(GTK_SCROLLED_WINDOW(text_panel)), ==, 350);
  g_assert_cmpint(gtk_scrolled_window_get_max_content_width(GTK_SCROLLED_WINDOW(text_panel)), ==, 350);
  g_assert_false(gtk_scrolled_window_get_propagate_natural_width(GTK_SCROLLED_WINDOW(text_panel)));
  g_assert_true(gtk_scrolled_window_get_overlay_scrolling(GTK_SCROLLED_WINDOW(text_panel)));
  gtk_scrolled_window_get_policy(GTK_SCROLLED_WINDOW(text_panel), &horizontal_policy, &vertical_policy);
  g_assert_cmpuint(horizontal_policy, ==, GTK_POLICY_EXTERNAL);
  g_assert_cmpuint(vertical_policy, ==, GTK_POLICY_AUTOMATIC);

  homeworlds_view_free(view);
  g_object_unref(model);

  window = test_homeworlds_create_window(&app, &window_model);
  window_view = test_homeworlds_get_window_view(window);
  window_text_panel = test_homeworlds_find_widget_named(GTK_WIDGET(window), "homeworlds-text-panel");
  window_text_panel_content = test_homeworlds_find_widget_named(GTK_WIDGET(window), "homeworlds-text-panel-content");
  window_board = test_homeworlds_find_widget_named(GTK_WIDGET(window), "homeworlds-board");
  window_board_scroller = test_homeworlds_find_widget_named(GTK_WIDGET(window), "homeworlds-board-scroller");
  window_bank = test_homeworlds_find_widget_named(GTK_WIDGET(window), "homeworlds-board-bank");
  g_assert_nonnull(window_view);
  g_assert_nonnull(window_text_panel);
  g_assert_nonnull(window_text_panel_content);
  g_assert_nonnull(window_board);
  g_assert_true(GTK_IS_DRAWING_AREA(window_board));
  g_assert_nonnull(window_board_scroller);
  g_assert_true(GTK_IS_SCROLLED_WINDOW(window_board_scroller));
  g_assert_nonnull(window_bank);
  homeworlds_view_set_move_report_enabled(window_view, FALSE);
  gtk_window_set_default_size(GTK_WINDOW(window), 1200, 700);
  gtk_window_present(GTK_WINDOW(window));
  test_homeworlds_drain_main_context(64);

  board_viewport_width = gtk_widget_get_width(window_board_scroller);
  g_assert_cmpint(board_viewport_width, >, 0);
  g_assert_cmpint(gtk_drawing_area_get_content_width(GTK_DRAWING_AREA(window_board)), <=, board_viewport_width + 2);
  g_assert_true(gtk_widget_compute_bounds(window_bank, window_board_scroller, &bank_bounds));
  g_assert_cmpfloat(bank_bounds.origin.x, >=, 0.0);
  g_assert_cmpfloat(bank_bounds.origin.x + bank_bounds.size.width, <=, (double)board_viewport_width + 1.0);
  allocated_width = gtk_widget_get_width(window_text_panel);
  g_assert_cmpint(allocated_width, >, 0);
  g_assert_true(gtk_widget_compute_bounds(window_text_panel_content, window_text_panel, &content_bounds));
  g_assert_cmpfloat(content_bounds.origin.x, >=, 0.0);
  g_assert_cmpfloat(content_bounds.origin.x + content_bounds.size.width, <=, (double)allocated_width + 1.0);
  visual_choice_label = test_homeworlds_find_label_with_text(window_text_panel_content,
                                                            "Click a highlighted pyramid in the bank on the board.");
  g_assert_nonnull(visual_choice_label);
  test_homeworlds_assert_text_panel_label_wraps(visual_choice_label);
  g_assert_true(gtk_widget_compute_bounds(visual_choice_label, window_text_panel, &label_bounds));
  g_assert_cmpfloat(label_bounds.origin.x + label_bounds.size.width, <=, (double)allocated_width + 1.0);
  for (guint step = 0; step < 3; step++) {
    g_assert_true(homeworlds_view_apply_candidate_at(window_view, 0));
    test_homeworlds_drain_main_context(64);
    g_assert_cmpint(gtk_widget_get_width(window_text_panel), ==, allocated_width);
    g_assert_true(gtk_widget_compute_bounds(window_text_panel_content, window_text_panel, &content_bounds));
    g_assert_cmpfloat(content_bounds.origin.x + content_bounds.size.width, <=, (double)allocated_width + 1.0);
    if (step == 0) {
      catastrophe_reset_label = test_homeworlds_find_label_with_text(window_text_panel_content,
                                                                     "Reset the partial move before catastrophes.");
      g_assert_nonnull(catastrophe_reset_label);
      test_homeworlds_assert_text_panel_label_wraps(catastrophe_reset_label);
      g_assert_true(gtk_widget_compute_bounds(catastrophe_reset_label, window_text_panel, &label_bounds));
      g_assert_cmpfloat(label_bounds.origin.x + label_bounds.size.width, <=, (double)allocated_width + 1.0);
    }
  }

  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(window_model);
  g_object_unref(app);
}

static void test_homeworlds_window_main_split_can_exceed_height(void) {
  GtkApplication *app = NULL;
  GGameModel *model = NULL;
  GGameWindow *window = NULL;
  GtkWidget *main_paned = NULL;
  gint height = 0;
  gint target_position = 1400;

  window = test_homeworlds_create_window(&app, &model);
  main_paned = g_object_get_data(G_OBJECT(window), "main-paned");

  g_assert_nonnull(main_paned);
  g_assert_true(GTK_IS_PANED(main_paned));

  gtk_window_set_default_size(GTK_WINDOW(window), 2000, 700);
  gtk_window_present(GTK_WINDOW(window));
  test_homeworlds_drain_main_context(64);

  height = gtk_widget_get_height(main_paned);
  g_assert_cmpint(height, >, 0);
  g_assert_cmpint(target_position, >, height);

  gtk_paned_set_position(GTK_PANED(main_paned), target_position);
  test_homeworlds_drain_main_context(64);

  g_assert_cmpint(gtk_paned_get_position(GTK_PANED(main_paned)), >=, target_position);

  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);
}

int main(int argc, char **argv) {
  g_autoptr(GError) autosave_error = NULL;
  g_autofree char *autosave_root = NULL;
  const GGameAppProfile *profile = NULL;
  int result = 0;

  g_test_init(&argc, &argv, NULL);

  autosave_root = g_dir_make_tmp("ghomeworlds-window-autosave-XXXXXX", &autosave_error);
  g_assert_no_error(autosave_error);
  g_assert_nonnull(autosave_root);
  g_setenv(SGF_AUTOSAVE_ENV, autosave_root, TRUE);

  gtk_init();

  profile = ggame_app_profile_get_by_kind(GGAME_APP_KIND_HOMEWORLDS);
  g_assert_nonnull(profile);
  g_assert_true(ggame_app_profile_set_active(profile));

  g_test_add_func("/homeworlds/view/homeworld-layout", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/system-layout", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/connected-sparse-row-layout", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/width-aware-row-layout", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/board-width-expands-for-wide-rows", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/board-size-matches-viewport-when-rows-fit", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/board-height-expands-for-tall-rows", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/piece-metrics", test_homeworlds_skip);
  g_test_add_func("/homeworlds/window/replaces-skeleton", test_homeworlds_skip);
  g_test_add_func("/homeworlds/window/main-split-can-exceed-height",
                  test_homeworlds_window_main_split_can_exceed_height);
  g_test_add_func("/homeworlds/window/defaults-to-minimum-computer-depth", test_homeworlds_skip);
  g_test_add_func("/homeworlds/window/setup-recorded-in-sgf", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/setup-bank-buttons", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/bank-layout", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/bank-layout-stays-compact-after-setup", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/board-ship-buttons", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/action-button-labels", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/build-action-from-each-green-ship", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/choice-list-cancel", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/build-no-second-step-highlight", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/trade-bank-buttons", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/attack-board-buttons", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/move-board-bank-buttons", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/advances-setup", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/move-report", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/move-report-toggle", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/move-report-initial-state", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/board-scrollable", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/text-panel-fixed-width", test_homeworlds_view_text_panel_has_fixed_width);
  g_test_add_func("/homeworlds/view/board-content-size-tracks-viewport", test_homeworlds_skip);
  g_test_add_func("/homeworlds/window/move-report-action", test_homeworlds_skip);
  g_test_add_func("/homeworlds/window/view-menu-move-report", test_homeworlds_skip);
  g_test_add_func("/homeworlds/view/board-system-title-player-names", test_homeworlds_skip);
  g_test_add_func("/homeworlds/window/import-dialog-starts-with-bga", test_homeworlds_skip);
  g_test_add_func("/homeworlds/window/catastrophe-prefix-records-single-sgf-move", test_homeworlds_skip);
  g_test_add_func("/homeworlds/window/end-move-catastrophe-requires-choice", test_homeworlds_skip);

  result = g_test_run();
  g_clear_object(&test_homeworlds_app);
  return result;
}
