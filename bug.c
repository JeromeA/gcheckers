#include <gtk/gtk.h>

#include "src/game_app_profile.h"
#include "src/game_model.h"
#include "src/games/homeworlds/homeworlds_backend.h"
#include "src/games/homeworlds/homeworlds_view.h"
#include "src/sgf_autosave.h"
#include "src/window.h"

static GtkApplication *test_homeworlds_app = NULL;

#define BUG_ASSERT_TRUE(expr) \
  G_STMT_START { \
    if (!(expr)) { \
      g_error("Assertion failed: %s", #expr); \
    } \
  } G_STMT_END

#define BUG_ASSERT_FALSE(expr) BUG_ASSERT_TRUE(!(expr))

#define BUG_ASSERT_NONNULL(expr) \
  G_STMT_START { \
    gconstpointer bug_value = (expr); \
    if (bug_value == NULL) { \
      g_error("Assertion failed: %s != NULL", #expr); \
    } \
  } G_STMT_END

#define BUG_ASSERT_NO_ERROR(error) \
  G_STMT_START { \
    const GError *bug_error = (error); \
    if (bug_error != NULL) { \
      g_error("Unexpected error: %s", bug_error->message); \
    } \
  } G_STMT_END

#define BUG_ASSERT_CMPINT(left, op, right) \
  G_STMT_START { \
    gint64 bug_left = (left); \
    gint64 bug_right = (right); \
    if (!(bug_left op bug_right)) { \
      g_error("Assertion failed: %s (%" G_GINT64_FORMAT ") %s %s (%" G_GINT64_FORMAT ")", \
              #left, bug_left, #op, #right, bug_right); \
    } \
  } G_STMT_END

#define BUG_ASSERT_CMPUINT(left, op, right) \
  G_STMT_START { \
    guint64 bug_left = (left); \
    guint64 bug_right = (right); \
    if (!(bug_left op bug_right)) { \
      g_error("Assertion failed: %s (%" G_GUINT64_FORMAT ") %s %s (%" G_GUINT64_FORMAT ")", \
              #left, bug_left, #op, #right, bug_right); \
    } \
  } G_STMT_END

#define BUG_ASSERT_CMPFLOAT(left, op, right) \
  G_STMT_START { \
    double bug_left = (left); \
    double bug_right = (right); \
    if (!(bug_left op bug_right)) { \
      g_error("Assertion failed: %s (%g) %s %s (%g)", #left, bug_left, #op, #right, bug_right); \
    } \
  } G_STMT_END

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
    BUG_ASSERT_NONNULL(test_homeworlds_app);
    BUG_ASSERT_TRUE(g_application_register(G_APPLICATION(test_homeworlds_app), NULL, &error));
    BUG_ASSERT_NO_ERROR(error);
  }

  return g_object_ref(test_homeworlds_app);
}

static GGameWindow *test_homeworlds_create_window(GtkApplication **out_app, GGameModel **out_model) {
  GtkApplication *app = test_homeworlds_create_app();
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  GGameWindow *window = NULL;

  BUG_ASSERT_NONNULL(model);
  window = ggame_window_new(app, model);
  BUG_ASSERT_NONNULL(window);

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
  BUG_ASSERT_TRUE(GTK_IS_LABEL(label));
  BUG_ASSERT_TRUE(gtk_label_get_wrap(GTK_LABEL(label)));
  BUG_ASSERT_CMPUINT(gtk_label_get_wrap_mode(GTK_LABEL(label)), ==, PANGO_WRAP_WORD_CHAR);
  BUG_ASSERT_CMPUINT(gtk_label_get_natural_wrap_mode(GTK_LABEL(label)), ==, GTK_NATURAL_WRAP_WORD);
  BUG_ASSERT_CMPINT(gtk_label_get_width_chars(GTK_LABEL(label)), ==, -1);
  BUG_ASSERT_CMPINT(gtk_label_get_max_width_chars(GTK_LABEL(label)), ==, -1);
  BUG_ASSERT_CMPUINT(gtk_widget_get_halign(label), ==, GTK_ALIGN_FILL);
  BUG_ASSERT_TRUE(gtk_widget_get_hexpand(label));
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

  BUG_ASSERT_NONNULL(text_panel);
  BUG_ASSERT_NONNULL(text_panel_content);
  BUG_ASSERT_TRUE(GTK_IS_SCROLLED_WINDOW(text_panel));
  gtk_widget_get_size_request(text_panel, &width_request, NULL);
  gtk_widget_get_size_request(text_panel_content, &content_width_request, NULL);
  BUG_ASSERT_CMPINT(width_request, ==, 350);
  BUG_ASSERT_CMPINT(content_width_request, ==, 326);
  BUG_ASSERT_CMPINT(gtk_scrolled_window_get_min_content_width(GTK_SCROLLED_WINDOW(text_panel)), ==, 350);
  BUG_ASSERT_CMPINT(gtk_scrolled_window_get_max_content_width(GTK_SCROLLED_WINDOW(text_panel)), ==, 350);
  BUG_ASSERT_FALSE(gtk_scrolled_window_get_propagate_natural_width(GTK_SCROLLED_WINDOW(text_panel)));
  BUG_ASSERT_TRUE(gtk_scrolled_window_get_overlay_scrolling(GTK_SCROLLED_WINDOW(text_panel)));
  gtk_scrolled_window_get_policy(GTK_SCROLLED_WINDOW(text_panel), &horizontal_policy, &vertical_policy);
  BUG_ASSERT_CMPUINT(horizontal_policy, ==, GTK_POLICY_EXTERNAL);
  BUG_ASSERT_CMPUINT(vertical_policy, ==, GTK_POLICY_AUTOMATIC);

  homeworlds_view_free(view);
  g_object_unref(model);

  window = test_homeworlds_create_window(&app, &window_model);
  window_view = test_homeworlds_get_window_view(window);
  window_text_panel = test_homeworlds_find_widget_named(GTK_WIDGET(window), "homeworlds-text-panel");
  window_text_panel_content = test_homeworlds_find_widget_named(GTK_WIDGET(window), "homeworlds-text-panel-content");
  window_board = test_homeworlds_find_widget_named(GTK_WIDGET(window), "homeworlds-board");
  window_board_scroller = test_homeworlds_find_widget_named(GTK_WIDGET(window), "homeworlds-board-scroller");
  window_bank = test_homeworlds_find_widget_named(GTK_WIDGET(window), "homeworlds-board-bank");
  BUG_ASSERT_NONNULL(window_view);
  BUG_ASSERT_NONNULL(window_text_panel);
  BUG_ASSERT_NONNULL(window_text_panel_content);
  BUG_ASSERT_NONNULL(window_board);
  BUG_ASSERT_TRUE(GTK_IS_DRAWING_AREA(window_board));
  BUG_ASSERT_NONNULL(window_board_scroller);
  BUG_ASSERT_TRUE(GTK_IS_SCROLLED_WINDOW(window_board_scroller));
  BUG_ASSERT_NONNULL(window_bank);
  homeworlds_view_set_move_report_enabled(window_view, FALSE);
  gtk_window_set_default_size(GTK_WINDOW(window), 1200, 700);
  gtk_window_present(GTK_WINDOW(window));
  test_homeworlds_drain_main_context(64);

  board_viewport_width = gtk_widget_get_width(window_board_scroller);
  BUG_ASSERT_CMPINT(board_viewport_width, >, 0);
  BUG_ASSERT_CMPINT(gtk_drawing_area_get_content_width(GTK_DRAWING_AREA(window_board)), <=, board_viewport_width + 2);
  BUG_ASSERT_TRUE(gtk_widget_compute_bounds(window_bank, window_board_scroller, &bank_bounds));
  BUG_ASSERT_CMPFLOAT(bank_bounds.origin.x, >=, 0.0);
  BUG_ASSERT_CMPFLOAT(bank_bounds.origin.x + bank_bounds.size.width, <=, (double)board_viewport_width + 1.0);
  allocated_width = gtk_widget_get_width(window_text_panel);
  BUG_ASSERT_CMPINT(allocated_width, >, 0);
  BUG_ASSERT_TRUE(gtk_widget_compute_bounds(window_text_panel_content, window_text_panel, &content_bounds));
  BUG_ASSERT_CMPFLOAT(content_bounds.origin.x, >=, 0.0);
  BUG_ASSERT_CMPFLOAT(content_bounds.origin.x + content_bounds.size.width, <=, (double)allocated_width + 1.0);
  visual_choice_label = test_homeworlds_find_label_with_text(window_text_panel_content,
                                                            "Click a highlighted pyramid in the bank on the board.");
  BUG_ASSERT_NONNULL(visual_choice_label);
  test_homeworlds_assert_text_panel_label_wraps(visual_choice_label);
  BUG_ASSERT_TRUE(gtk_widget_compute_bounds(visual_choice_label, window_text_panel, &label_bounds));
  BUG_ASSERT_CMPFLOAT(label_bounds.origin.x + label_bounds.size.width, <=, (double)allocated_width + 1.0);
  for (guint step = 0; step < 3; step++) {
    BUG_ASSERT_TRUE(homeworlds_view_apply_candidate_at(window_view, 0));
    test_homeworlds_drain_main_context(64);
    BUG_ASSERT_CMPINT(gtk_widget_get_width(window_text_panel), ==, allocated_width);
    BUG_ASSERT_TRUE(gtk_widget_compute_bounds(window_text_panel_content, window_text_panel, &content_bounds));
    BUG_ASSERT_CMPFLOAT(content_bounds.origin.x + content_bounds.size.width, <=, (double)allocated_width + 1.0);
    if (step == 0) {
      catastrophe_reset_label = test_homeworlds_find_label_with_text(window_text_panel_content,
                                                                     "Reset the partial move before catastrophes.");
      BUG_ASSERT_NONNULL(catastrophe_reset_label);
      test_homeworlds_assert_text_panel_label_wraps(catastrophe_reset_label);
      BUG_ASSERT_TRUE(gtk_widget_compute_bounds(catastrophe_reset_label, window_text_panel, &label_bounds));
      BUG_ASSERT_CMPFLOAT(label_bounds.origin.x + label_bounds.size.width, <=, (double)allocated_width + 1.0);
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

  BUG_ASSERT_NONNULL(main_paned);
  BUG_ASSERT_TRUE(GTK_IS_PANED(main_paned));

  gtk_window_set_default_size(GTK_WINDOW(window), 2000, 700);
  gtk_window_present(GTK_WINDOW(window));
  test_homeworlds_drain_main_context(64);

  height = gtk_widget_get_height(main_paned);
  BUG_ASSERT_CMPINT(height, >, 0);
  BUG_ASSERT_CMPINT(target_position, >, height);

  gtk_paned_set_position(GTK_PANED(main_paned), target_position);
  test_homeworlds_drain_main_context(64);

  BUG_ASSERT_CMPINT(gtk_paned_get_position(GTK_PANED(main_paned)), >=, target_position);

  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);
}

int main(void) {
  g_autoptr(GError) autosave_error = NULL;
  g_autofree char *autosave_root = NULL;
  const GGameAppProfile *profile = NULL;

  g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);

  autosave_root = g_dir_make_tmp("ghomeworlds-window-autosave-XXXXXX", &autosave_error);
  BUG_ASSERT_NO_ERROR(autosave_error);
  BUG_ASSERT_NONNULL(autosave_root);
  g_setenv(SGF_AUTOSAVE_ENV, autosave_root, TRUE);

  gtk_init();

  profile = ggame_app_profile_get_by_kind(GGAME_APP_KIND_HOMEWORLDS);
  BUG_ASSERT_NONNULL(profile);
  BUG_ASSERT_TRUE(ggame_app_profile_set_active(profile));

  test_homeworlds_view_text_panel_has_fixed_width();
  test_homeworlds_window_main_split_can_exceed_height();

  g_clear_object(&test_homeworlds_app);
  return 0;
}
