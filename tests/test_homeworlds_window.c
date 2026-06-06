#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <string.h>

#include "../src/application.h"
#include "../src/common_settings.h"
#include "../src/game_app_profile.h"
#include "../src/game_model.h"
#include "../src/games/homeworlds/homeworlds_backend.h"
#include "../src/games/homeworlds/homeworlds_game.h"
#include "../src/games/homeworlds/homeworlds_view.h"
#include "../src/player_controls_panel.h"
#include "../src/sgf_autosave.h"
#include "../src/sgf_controller.h"
#include "../src/sgf_tree.h"
#include "../src/style.h"
#include "../src/window.h"

static void test_homeworlds_window_skip(void) {
  g_test_skip("GTK display not available.");
}

static GtkApplication *test_homeworlds_app = NULL;

static void test_homeworlds_window_reset_layout_settings(void) {
  g_autoptr(GSettings) settings = ggame_common_settings_create();

  if (!G_IS_SETTINGS(settings)) {
    return;
  }

  g_settings_set_boolean(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_LAYOUT_SAVED, FALSE);
  g_settings_set_boolean(settings, GGAME_COMMON_SETTINGS_KEY_WINDOW_SHOW_MOVE_REPORT, TRUE);
}

static void test_homeworlds_window_wait_for_draw(gpointer window) {
  g_return_if_fail(GTK_IS_WINDOW(window));

  if (!gtk_widget_get_mapped(GTK_WIDGET(window))) {
    gtk_window_present(GTK_WINDOW(window));
  }
  gtk_test_widget_wait_for_draw(GTK_WIDGET(window));
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

static char *test_homeworlds_get_text_view_text(GtkWidget *text_view) {
  GtkTextBuffer *buffer = NULL;
  GtkTextIter start_iter;
  GtkTextIter end_iter;

  g_return_val_if_fail(GTK_IS_TEXT_VIEW(text_view), NULL);

  buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
  g_return_val_if_fail(GTK_IS_TEXT_BUFFER(buffer), NULL);

  gtk_text_buffer_get_bounds(buffer, &start_iter, &end_iter);
  return gtk_text_buffer_get_text(buffer, &start_iter, &end_iter, FALSE);
}

static GtkWidget *test_homeworlds_find_widget_for_action(GtkWidget *root, const char *action_name) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(action_name != NULL, NULL);

  if (GTK_IS_ACTIONABLE(root)) {
    const char *widget_action = gtk_actionable_get_action_name(GTK_ACTIONABLE(root));
    if (widget_action != NULL && g_strcmp0(widget_action, action_name) == 0) {
      return root;
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkWidget *match = test_homeworlds_find_widget_for_action(child, action_name);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GMenuModel *test_homeworlds_find_submenu(GMenuModel *menu, const char *label) {
  g_return_val_if_fail(G_IS_MENU_MODEL(menu), NULL);
  g_return_val_if_fail(label != NULL, NULL);

  gint items = g_menu_model_get_n_items(menu);
  for (gint i = 0; i < items; i++) {
    g_autoptr(GVariant) item_label = g_menu_model_get_item_attribute_value(menu, i, G_MENU_ATTRIBUTE_LABEL, NULL);

    if (item_label != NULL && g_strcmp0(g_variant_get_string(item_label, NULL), label) == 0) {
      return g_menu_model_get_item_link(menu, i, G_MENU_LINK_SUBMENU);
    }
  }

  return NULL;
}

static gboolean test_homeworlds_menu_contains_item(GMenuModel *menu, const char *label) {
  g_return_val_if_fail(G_IS_MENU_MODEL(menu), FALSE);
  g_return_val_if_fail(label != NULL, FALSE);

  gint items = g_menu_model_get_n_items(menu);
  for (gint i = 0; i < items; i++) {
    g_autoptr(GVariant) item_label = g_menu_model_get_item_attribute_value(menu, i, G_MENU_ATTRIBUTE_LABEL, NULL);
    g_autoptr(GMenuModel) section = NULL;

    if (item_label != NULL && g_strcmp0(g_variant_get_string(item_label, NULL), label) == 0) {
      return TRUE;
    }

    section = g_menu_model_get_item_link(menu, i, G_MENU_LINK_SECTION);
    if (section != NULL && test_homeworlds_menu_contains_item(section, label)) {
      return TRUE;
    }
  }

  return FALSE;
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

static void test_homeworlds_remove_bank_piece(HomeworldsPosition *position, HomeworldsPyramid pyramid) {
  g_return_if_fail(position != NULL);
  g_return_if_fail(homeworlds_pyramid_is_valid(pyramid));

  for (guint slot = 0; slot < HOMEWORLDS_BANK_SLOT_COUNT; ++slot) {
    if (position->bank[slot] != pyramid) {
      continue;
    }

    position->bank[slot] = 0;
    return;
  }

  g_assert_not_reached();
}

static guint test_homeworlds_count_widgets_with_data(GtkWidget *root, const char *key) {
  guint count = 0;

  g_return_val_if_fail(GTK_IS_WIDGET(root), 0);
  g_return_val_if_fail(key != NULL, 0);

  if (g_object_get_data(G_OBJECT(root), key) != NULL) {
    count++;
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    count += test_homeworlds_count_widgets_with_data(child, key);
  }

  return count;
}

static guint test_homeworlds_count_bank_buttons_with_css_class(GtkWidget *root, const char *css_class) {
  guint count = 0;

  g_return_val_if_fail(GTK_IS_WIDGET(root), 0);
  g_return_val_if_fail(css_class != NULL, 0);

  if (g_object_get_data(G_OBJECT(root), "homeworlds-board-bank-choice") != NULL &&
      gtk_widget_has_css_class(root, css_class)) {
    count++;
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    count += test_homeworlds_count_bank_buttons_with_css_class(child, css_class);
  }

  return count;
}

static guint test_homeworlds_count_bank_buttons_without_compact_style(GtkWidget *root) {
  guint count = 0;

  g_return_val_if_fail(GTK_IS_WIDGET(root), 0);

  if (g_object_get_data(G_OBJECT(root), "homeworlds-board-bank-choice") != NULL &&
      (!gtk_widget_has_css_class(root, "homeworlds-bank-pile") || gtk_widget_has_css_class(root, "flat"))) {
    count++;
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    count += test_homeworlds_count_bank_buttons_without_compact_style(child);
  }

  return count;
}

static guint test_homeworlds_count_active_ship_highlights(GtkWidget *root) {
  return test_homeworlds_count_widgets_with_data(root, "homeworlds-board-active-ship");
}

static guint test_homeworlds_count_labels_with_text(GtkWidget *root, const char *text) {
  guint count = 0;

  g_return_val_if_fail(GTK_IS_WIDGET(root), 0);
  g_return_val_if_fail(text != NULL, 0);

  if (GTK_IS_LABEL(root) && g_strcmp0(gtk_label_get_text(GTK_LABEL(root)), text) == 0) {
    count++;
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    count += test_homeworlds_count_labels_with_text(child, text);
  }

  return count;
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

static GtkButton *test_homeworlds_find_button_with_label(GtkWidget *root, const char *label) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(label != NULL, NULL);

  if (GTK_IS_BUTTON(root)) {
    const char *button_label = gtk_button_get_label(GTK_BUTTON(root));

    if (button_label != NULL && g_strcmp0(button_label, label) == 0) {
      return GTK_BUTTON(root);
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkButton *match = test_homeworlds_find_button_with_label(child, label);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkWindow *test_homeworlds_find_toplevel_by_title(const char *title) {
  g_return_val_if_fail(title != NULL, NULL);

  GListModel *toplevels = gtk_window_get_toplevels();
  guint count = g_list_model_get_n_items(toplevels);
  for (guint i = 0; i < count; ++i) {
    GtkWindow *window = g_list_model_get_item(toplevels, i);
    if (!GTK_IS_WINDOW(window)) {
      if (window != NULL) {
        g_object_unref(window);
      }
      continue;
    }

    const char *window_title = gtk_window_get_title(window);
    if (window_title != NULL && g_strcmp0(window_title, title) == 0) {
      return window;
    }
    g_object_unref(window);
  }

  return NULL;
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

static GtkButton *test_homeworlds_find_selectable_bank_button(GtkWidget *root) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);

  if (GTK_IS_BUTTON(root) &&
      g_object_get_data(G_OBJECT(root), "homeworlds-board-bank-choice") != NULL &&
      gtk_widget_get_sensitive(root)) {
    return GTK_BUTTON(root);
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkButton *match = test_homeworlds_find_selectable_bank_button(child);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkButton *test_homeworlds_find_selectable_ship_button(GtkWidget *root) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);

  if (GTK_IS_BUTTON(root) &&
      g_object_get_data(G_OBJECT(root), "homeworlds-board-ship-choice") != NULL &&
      gtk_widget_get_sensitive(root)) {
    return GTK_BUTTON(root);
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkButton *match = test_homeworlds_find_selectable_ship_button(child);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkButton *test_homeworlds_find_ship_button_for(GtkWidget *root,
                                                       guint system_index,
                                                       HomeworldsPyramid pyramid) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, NULL);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), NULL);

  if (GTK_IS_BUTTON(root) &&
      g_object_get_data(G_OBJECT(root), "homeworlds-board-ship-choice") != NULL) {
    const HomeworldsMoveCandidate *candidate = g_object_get_data(G_OBJECT(root), "homeworlds-candidate");

    if (candidate != NULL &&
        candidate->data.kind == HOMEWORLDS_CANDIDATE_SELECT_SHIP &&
        candidate->data.system_index == system_index &&
        candidate->data.pyramid == pyramid) {
      return GTK_BUTTON(root);
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkButton *match = test_homeworlds_find_ship_button_for(child, system_index, pyramid);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkButton *test_homeworlds_find_selectable_system_button(GtkWidget *root) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);

  if (GTK_IS_BUTTON(root) &&
      g_object_get_data(G_OBJECT(root), "homeworlds-board-system-choice") != NULL &&
      gtk_widget_get_sensitive(root)) {
    return GTK_BUTTON(root);
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkButton *match = test_homeworlds_find_selectable_system_button(child);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkButton *test_homeworlds_find_cancel_choice_button(GtkWidget *root) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);

  if (GTK_IS_BUTTON(root) &&
      g_object_get_data(G_OBJECT(root), "homeworlds-cancel-choice") != NULL) {
    return GTK_BUTTON(root);
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkButton *match = test_homeworlds_find_cancel_choice_button(child);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkButton *test_homeworlds_find_finish_catastrophes_pass_button(GtkWidget *root) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);

  if (GTK_IS_BUTTON(root) &&
      g_object_get_data(G_OBJECT(root), "homeworlds-finish-catastrophes-pass") != NULL) {
    return GTK_BUTTON(root);
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkButton *match = test_homeworlds_find_finish_catastrophes_pass_button(child);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkButton *test_homeworlds_find_catastrophe_button(GtkWidget *root,
                                                          guint system_index,
                                                          HomeworldsColor color) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT, NULL);
  g_return_val_if_fail(color <= HOMEWORLDS_COLOR_BLUE, NULL);

  if (GTK_IS_BUTTON(root) &&
      GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(root), "homeworlds-system-index")) == system_index + 1 &&
      GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(root), "homeworlds-color")) == color + 1) {
    return GTK_BUTTON(root);
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkButton *match = test_homeworlds_find_catastrophe_button(child, system_index, color);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static GtkButton *test_homeworlds_find_bank_button_for_pyramid(GtkWidget *root, HomeworldsPyramid pyramid) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);
  g_return_val_if_fail(homeworlds_pyramid_is_valid(pyramid), NULL);

  if (GTK_IS_BUTTON(root) &&
      GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(root), "homeworlds-bank-pyramid")) == pyramid) {
    return GTK_BUTTON(root);
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkButton *match = test_homeworlds_find_bank_button_for_pyramid(child, pyramid);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static gboolean test_homeworlds_widget_is_visual_choice(GtkWidget *widget) {
  g_return_val_if_fail(GTK_IS_WIDGET(widget), FALSE);

  return g_object_get_data(G_OBJECT(widget), "homeworlds-board-bank-choice") != NULL ||
         g_object_get_data(G_OBJECT(widget), "homeworlds-board-ship-choice") != NULL ||
         g_object_get_data(G_OBJECT(widget), "homeworlds-board-system-choice") != NULL;
}

static guint test_homeworlds_count_non_visual_candidate_buttons(GtkWidget *root, HomeworldsCandidateKind kind) {
  guint count = 0;

  g_return_val_if_fail(GTK_IS_WIDGET(root), 0);

  if (GTK_IS_BUTTON(root) &&
      !test_homeworlds_widget_is_visual_choice(root)) {
    const HomeworldsMoveCandidate *candidate = g_object_get_data(G_OBJECT(root), "homeworlds-candidate");
    if (candidate != NULL && candidate->data.kind == kind) {
      count++;
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    count += test_homeworlds_count_non_visual_candidate_buttons(child, kind);
  }

  return count;
}

static GtkButton *test_homeworlds_find_non_visual_action_button(GtkWidget *root, HomeworldsStepKind action) {
  g_return_val_if_fail(GTK_IS_WIDGET(root), NULL);

  if (GTK_IS_BUTTON(root) &&
      !test_homeworlds_widget_is_visual_choice(root)) {
    const HomeworldsMoveCandidate *candidate = g_object_get_data(G_OBJECT(root), "homeworlds-candidate");
    if (candidate != NULL &&
        candidate->data.kind == HOMEWORLDS_CANDIDATE_ACTION &&
        candidate->data.target_color == action) {
      return GTK_BUTTON(root);
    }
  }

  for (GtkWidget *child = gtk_widget_get_first_child(root); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    GtkButton *match = test_homeworlds_find_non_visual_action_button(child, action);
    if (match != NULL) {
      return match;
    }
  }

  return NULL;
}

static void test_homeworlds_assert_action_button_label(GtkWidget *root,
                                                       HomeworldsStepKind action,
                                                       const char *expected_label) {
  GtkButton *button = NULL;

  g_return_if_fail(GTK_IS_WIDGET(root));
  g_return_if_fail(expected_label != NULL);

  button = test_homeworlds_find_non_visual_action_button(root, action);
  g_assert_nonnull(button);
  g_assert_cmpstr(gtk_button_get_label(button), ==, expected_label);
}

static HomeworldsMove test_homeworlds_setup_move(HomeworldsPyramid first_star,
                                                 HomeworldsPyramid second_star,
                                                 HomeworldsPyramid ship) {
  return (HomeworldsMove){
    .kind = HOMEWORLDS_MOVE_KIND_SETUP,
    .setup_stars = {first_star, second_star},
    .setup_ship = ship,
  };
}

static void test_homeworlds_prepare_play_position(HomeworldsPosition *position) {
  HomeworldsMove player_1 = test_homeworlds_setup_move(
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE));
  HomeworldsMove player_2 = test_homeworlds_setup_move(
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE),
      homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE));

  g_return_if_fail(position != NULL);

  homeworlds_position_init(position);
  g_assert_true(homeworlds_position_apply_move(position, &player_1));
  g_assert_true(homeworlds_position_apply_move(position, &player_2));
}

static HomeworldsSystemRef test_homeworlds_homeworld_ref(guint side) {
  g_assert_cmpuint(side, <, 2);

  return (HomeworldsSystemRef){
    .kind = HOMEWORLDS_SYSTEM_REF_HOMEWORLD,
    .homeworld_side = (guint8)side,
  };
}

static HomeworldsSystemRef test_homeworlds_system_ref(guint system_index) {
  g_assert_cmpuint(system_index, <, HOMEWORLDS_SYSTEM_SLOT_COUNT);

  return (HomeworldsSystemRef){
    .kind = HOMEWORLDS_SYSTEM_REF_SYSTEM,
    .system_index = (guint8)system_index,
  };
}

static HomeworldsShipRef test_homeworlds_ship_ref(HomeworldsSystemRef system, HomeworldsPyramid ship) {
  g_assert_true(homeworlds_pyramid_is_valid(ship));

  return (HomeworldsShipRef){
    .system = system,
    .ship = ship,
  };
}

static void test_homeworlds_collect_previous_markers_for_move(
    const HomeworldsPosition *before,
    const HomeworldsMove *move,
    HomeworldsPosition *out_after,
    HomeworldsViewPreviousMoveMarker *out_markers,
    gsize *out_marker_count) {
  g_return_if_fail(before != NULL);
  g_return_if_fail(move != NULL);
  g_return_if_fail(out_after != NULL);
  g_return_if_fail(out_markers != NULL);
  g_return_if_fail(out_marker_count != NULL);

  *out_after = *before;
  g_assert_true(homeworlds_position_apply_move(out_after, move));
  g_assert_true(homeworlds_view_collect_previous_move_markers(before,
                                                             out_after,
                                                             move,
                                                             before->turn,
                                                             out_markers,
                                                             HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_CAPACITY,
                                                             out_marker_count));
}

static void test_homeworlds_assert_previous_marker(const HomeworldsViewPreviousMoveMarker *marker,
                                                   HomeworldsViewPreviousMoveMarkerKind kind,
                                                   guint system_index,
                                                   gboolean is_ship,
                                                   guint side,
                                                   guint slot,
                                                   HomeworldsPyramid pyramid,
                                                   HomeworldsColor color) {
  g_assert_nonnull(marker);
  g_assert_cmpuint(marker->kind, ==, kind);
  g_assert_cmpuint(marker->system_index, ==, system_index);
  g_assert_cmpuint(marker->is_ship, ==, is_ship);
  g_assert_cmpuint(marker->side, ==, side);
  g_assert_cmpuint(marker->slot, ==, slot);
  g_assert_cmpuint(marker->pyramid, ==, pyramid);
  g_assert_cmpuint(marker->color, ==, color);
}

static void test_homeworlds_view_previous_move_markers_describe_ship_actions(void) {
  HomeworldsSystemRef homeworld_1 = test_homeworlds_homeworld_ref(0);
  HomeworldsPyramid green_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE);
  HomeworldsPyramid blue_small = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL);
  HomeworldsViewPreviousMoveMarker markers[HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_CAPACITY] = {0};
  HomeworldsPosition before = {0};
  HomeworldsPosition after = {0};
  gsize marker_count = 0;

  test_homeworlds_prepare_play_position(&before);
  HomeworldsPyramid built_ship = 0;
  g_assert_true(homeworlds_system_find_smallest_bank_ship(&before, HOMEWORLDS_COLOR_GREEN, &built_ship));
  HomeworldsMove build = {
    .kind = HOMEWORLDS_MOVE_KIND_TURN,
    .step_count = 1,
    .steps =
        {
          {
            .kind = HOMEWORLDS_STEP_BUILD,
            .actor.system = homeworld_1,
            .target_color = HOMEWORLDS_COLOR_GREEN,
          },
        },
  };
  test_homeworlds_collect_previous_markers_for_move(&before, &build, &after, markers, &marker_count);
  g_assert_cmpuint(marker_count, ==, 1);
  test_homeworlds_assert_previous_marker(&markers[0],
                                         HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_BUILD,
                                         0,
                                         TRUE,
                                         0,
                                         1,
                                         built_ship,
                                         HOMEWORLDS_COLOR_GREEN);

  test_homeworlds_prepare_play_position(&before);
  HomeworldsMove trade = {
    .kind = HOMEWORLDS_MOVE_KIND_TURN,
    .step_count = 1,
    .steps =
        {
          {
            .kind = HOMEWORLDS_STEP_TRADE,
            .actor = test_homeworlds_ship_ref(homeworld_1, green_large),
            .target_color = HOMEWORLDS_COLOR_BLUE,
          },
        },
  };
  test_homeworlds_collect_previous_markers_for_move(&before, &trade, &after, markers, &marker_count);
  g_assert_cmpuint(marker_count, ==, 1);
  test_homeworlds_assert_previous_marker(&markers[0],
                                         HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_TRADE,
                                         0,
                                         TRUE,
                                         0,
                                         0,
                                         homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE),
                                         HOMEWORLDS_COLOR_GREEN);

  test_homeworlds_prepare_play_position(&before);
  before.systems[0].ships[1][0] = blue_small;
  test_homeworlds_remove_bank_piece(&before, blue_small);
  homeworlds_position_rebuild_color_counts(&before);
  HomeworldsMove capture = {
    .kind = HOMEWORLDS_MOVE_KIND_TURN,
    .step_count = 1,
    .steps =
        {
          {
            .kind = HOMEWORLDS_STEP_ATTACK,
            .actor = test_homeworlds_ship_ref(homeworld_1, green_large),
            .target_ship = test_homeworlds_ship_ref(homeworld_1, blue_small),
          },
        },
  };
  test_homeworlds_collect_previous_markers_for_move(&before, &capture, &after, markers, &marker_count);
  g_assert_cmpuint(marker_count, ==, 1);
  test_homeworlds_assert_previous_marker(&markers[0],
                                         HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_CAPTURE,
                                         0,
                                         TRUE,
                                         0,
                                         1,
                                         blue_small,
                                         HOMEWORLDS_COLOR_RED);
}

static void test_homeworlds_view_previous_move_markers_describe_catastrophes(void) {
  HomeworldsViewPreviousMoveMarker markers[HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_CAPACITY] = {0};
  HomeworldsPosition before = {0};
  HomeworldsPosition after = {0};
  gsize marker_count = 0;
  HomeworldsPyramid yellow_small = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL);
  HomeworldsPyramid blue_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE);
  HomeworldsPyramid green_small = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL);
  HomeworldsPyramid red_small = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL);
  HomeworldsPyramid red_medium = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM);
  HomeworldsPyramid red_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE);

  test_homeworlds_prepare_play_position(&before);
  before.systems[2].stars[0] = yellow_small;
  before.systems[2].stars[1] = blue_large;
  before.systems[2].ships[0][0] = green_small;
  before.systems[2].ships[0][1] = red_small;
  before.systems[2].ships[0][2] = red_medium;
  before.systems[2].ships[1][0] = red_large;
  before.systems[2].ships[1][1] = red_small;
  test_homeworlds_remove_bank_piece(&before, yellow_small);
  test_homeworlds_remove_bank_piece(&before, blue_large);
  test_homeworlds_remove_bank_piece(&before, green_small);
  test_homeworlds_remove_bank_piece(&before, red_small);
  test_homeworlds_remove_bank_piece(&before, red_medium);
  test_homeworlds_remove_bank_piece(&before, red_large);
  test_homeworlds_remove_bank_piece(&before, red_small);
  homeworlds_position_rebuild_color_counts(&before);

  HomeworldsMove catastrophe = {
    .kind = HOMEWORLDS_MOVE_KIND_TURN,
    .step_count = 1,
    .steps =
        {
          {
            .kind = HOMEWORLDS_STEP_CATASTROPHE,
            .target_system = test_homeworlds_system_ref(2),
            .target_color = HOMEWORLDS_COLOR_RED,
          },
        },
  };
  test_homeworlds_collect_previous_markers_for_move(&before, &catastrophe, &after, markers, &marker_count);
  g_assert_cmpuint(marker_count, ==, 1);
  test_homeworlds_assert_previous_marker(&markers[0],
                                         HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_CATASTROPHE,
                                         2,
                                         FALSE,
                                         0,
                                         HOMEWORLDS_INVALID_INDEX,
                                         0,
                                         HOMEWORLDS_COLOR_RED);
}

static void test_homeworlds_view_previous_move_markers_skip_sacrifice_step(void) {
  HomeworldsSystemRef homeworld_1 = test_homeworlds_homeworld_ref(0);
  HomeworldsSystemRef target_system = test_homeworlds_system_ref(2);
  HomeworldsPyramid green_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE);
  HomeworldsPyramid red_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE);
  HomeworldsPyramid yellow_small = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL);
  HomeworldsViewPreviousMoveMarker markers[HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_CAPACITY] = {0};
  HomeworldsPosition before = {0};
  HomeworldsPosition after = {0};
  gsize marker_count = 0;

  test_homeworlds_prepare_play_position(&before);
  before.systems[0].ships[0][1] = yellow_small;
  before.systems[2].stars[0] = red_large;
  test_homeworlds_remove_bank_piece(&before, yellow_small);
  test_homeworlds_remove_bank_piece(&before, red_large);
  homeworlds_position_rebuild_color_counts(&before);

  HomeworldsMove sacrifice_move = {
    .kind = HOMEWORLDS_MOVE_KIND_TURN,
    .step_count = 2,
    .steps =
        {
          {
            .kind = HOMEWORLDS_STEP_SACRIFICE,
            .actor = test_homeworlds_ship_ref(homeworld_1, yellow_small),
          },
          {
            .kind = HOMEWORLDS_STEP_MOVE,
            .actor = test_homeworlds_ship_ref(homeworld_1, green_large),
            .target_system = target_system,
          },
        },
  };
  test_homeworlds_collect_previous_markers_for_move(&before, &sacrifice_move, &after, markers, &marker_count);
  g_assert_cmpuint(marker_count, ==, 1);
  test_homeworlds_assert_previous_marker(&markers[0],
                                         HOMEWORLDS_VIEW_PREVIOUS_MOVE_MARKER_MOVE,
                                         2,
                                         TRUE,
                                         0,
                                         0,
                                         green_large,
                                         HOMEWORLDS_COLOR_YELLOW);
}

static void test_homeworlds_prepare_compact_row_position(HomeworldsPosition *position) {
  g_return_if_fail(position != NULL);

  homeworlds_position_init(position);
  position->phase = HOMEWORLDS_PHASE_PLAY;
  position->turn = 0;
  position->systems[0].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL);
  position->systems[0].ships[0][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE);
  position->systems[1].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL);
  position->systems[1].ships[1][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE);
  homeworlds_position_rebuild_color_counts(position);
}

static void test_homeworlds_prepare_connected_sparse_row_position(HomeworldsPosition *position) {
  g_return_if_fail(position != NULL);

  homeworlds_position_init(position);
  position->phase = HOMEWORLDS_PHASE_PLAY;
  position->turn = 0;
  position->systems[1].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM);
  position->systems[1].stars[1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE);
  position->systems[1].ships[1][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE);
  position->systems[1].ships[1][1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL);
  position->systems[2].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL);
  position->systems[2].ships[1][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL);
  position->systems[3].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE);
  position->systems[3].ships[0][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL);
  position->systems[0].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL);
  position->systems[0].stars[1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM);
  position->systems[0].ships[0][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE);
  position->systems[0].ships[0][1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL);
  homeworlds_position_rebuild_color_counts(position);
}

static void test_homeworlds_make_wide_system(HomeworldsSystem *system, HomeworldsColor star_color) {
  g_return_if_fail(system != NULL);
  g_return_if_fail(star_color <= HOMEWORLDS_COLOR_BLUE);

  system->stars[0] = homeworlds_pyramid_make(star_color, HOMEWORLDS_SIZE_LARGE);
  for (guint slot = 0; slot < 6; ++slot) {
    system->ships[0][slot] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE);
  }
  homeworlds_system_rebuild_color_counts(system);
}

static void test_homeworlds_view_homeworld_layout_uses_player_perspective(void) {
  HomeworldsViewHomeworldLayout player_1 = {0};
  HomeworldsViewHomeworldLayout player_2 = {0};
  HomeworldsViewHomeworldLayout invalid = {0};

  g_assert_true(homeworlds_view_calculate_homeworld_layout(0, 100.0, 200.0, &player_1));
  g_assert_true(homeworlds_view_calculate_homeworld_layout(1, 100.0, 200.0, &player_2));
  g_assert_false(homeworlds_view_calculate_homeworld_layout(2, 100.0, 200.0, &invalid));

  g_assert_true(player_1.ship_points_up);
  g_assert_false(player_2.ship_points_up);
  g_assert_cmpfloat(player_1.ship_y, ==, player_1.star_y);
  g_assert_cmpfloat(player_2.ship_y, ==, player_2.star_y);
  g_assert_cmpfloat(player_1.ship_x, >, player_1.star_x[1]);
  g_assert_cmpfloat(player_2.ship_x, <, player_2.star_x[0]);
}

static void test_homeworlds_view_system_layout_groups_by_reachability(void) {
  HomeworldsPosition position = {0};
  double player_1_x = 0.0;
  double player_1_y = 0.0;
  double player_2_x = 0.0;
  double player_2_y = 0.0;
  double small_x = 0.0;
  double small_y = 0.0;
  double medium_x = 0.0;
  double medium_y = 0.0;
  double large_x = 0.0;
  double large_y = 0.0;
  double compact_a_x = 0.0;
  double compact_a_y = 0.0;
  double compact_b_x = 0.0;
  double compact_b_y = 0.0;
  double compact_c_x = 0.0;
  double compact_c_y = 0.0;

  position.phase = HOMEWORLDS_PHASE_PLAY;
  position.systems[0].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL);
  position.systems[0].stars[1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM);
  position.systems[1].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM);
  position.systems[1].stars[1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE);
  position.systems[2].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE);
  position.systems[3].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL);
  position.systems[4].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_MEDIUM);
  homeworlds_position_rebuild_color_counts(&position);

  g_assert_true(homeworlds_view_calculate_system_center(&position, 0, 900.0, 600.0, &player_1_x, &player_1_y));
  g_assert_true(homeworlds_view_calculate_system_center(&position, 1, 900.0, 600.0, &player_2_x, &player_2_y));
  g_assert_true(homeworlds_view_calculate_system_center(&position, 2, 900.0, 600.0, &large_x, &large_y));
  g_assert_true(homeworlds_view_calculate_system_center(&position, 3, 900.0, 600.0, &small_x, &small_y));
  g_assert_true(homeworlds_view_calculate_system_center(&position, 4, 900.0, 600.0, &medium_x, &medium_y));

  g_assert_cmpfloat(player_2_y, <, small_y);
  g_assert_cmpfloat(small_y, <, medium_y);
  g_assert_cmpfloat(medium_y, <, large_y);
  g_assert_cmpfloat(large_y, <, player_1_y);

  position.systems[1].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL);
  position.systems[1].stars[1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM);
  homeworlds_system_rebuild_color_counts(&position.systems[1]);
  g_assert_true(homeworlds_view_calculate_system_center(&position, 2, 900.0, 600.0, &compact_a_x, &compact_a_y));
  g_assert_true(homeworlds_view_calculate_system_center(&position, 3, 900.0, 600.0, &compact_b_x, &compact_b_y));
  g_assert_true(homeworlds_view_calculate_system_center(&position, 4, 900.0, 600.0, &compact_c_x, &compact_c_y));
  g_assert_cmpfloat(compact_a_y, ==, compact_b_y);
  g_assert_cmpfloat(compact_b_y, ==, compact_c_y);
  g_assert_cmpfloat_with_epsilon(compact_b_x - compact_a_x, compact_c_x - compact_b_x, 0.001);
  g_assert_cmpfloat(compact_c_x, <, 628.0);
}

static void test_homeworlds_view_connected_sparse_rows_skip_empty_middle_gap(void) {
  HomeworldsPosition position = {0};
  double player_1_x = 0.0;
  double player_1_y = 0.0;
  double player_2_x = 0.0;
  double player_2_y = 0.0;
  double yellow_small_x = 0.0;
  double yellow_small_y = 0.0;
  double blue_large_x = 0.0;
  double blue_large_y = 0.0;
  double medium_x = 0.0;
  double medium_y = 0.0;
  double player_2_to_yellow = 0.0;
  double yellow_to_blue = 0.0;
  double blue_to_player_1 = 0.0;

  test_homeworlds_prepare_connected_sparse_row_position(&position);

  g_assert_true(homeworlds_view_calculate_system_center(&position, 0, 900.0, 600.0, &player_1_x, &player_1_y));
  g_assert_true(homeworlds_view_calculate_system_center(&position, 1, 900.0, 600.0, &player_2_x, &player_2_y));
  g_assert_true(homeworlds_view_calculate_system_center(&position, 2, 900.0, 600.0, &yellow_small_x, &yellow_small_y));
  g_assert_true(homeworlds_view_calculate_system_center(&position, 3, 900.0, 600.0, &blue_large_x, &blue_large_y));

  player_2_to_yellow = yellow_small_y - player_2_y;
  yellow_to_blue = blue_large_y - yellow_small_y;
  blue_to_player_1 = player_1_y - blue_large_y;
  g_assert_cmpfloat_with_epsilon(player_2_to_yellow, yellow_to_blue, 0.001);
  g_assert_cmpfloat_with_epsilon(yellow_to_blue, blue_to_player_1, 0.001);

  position.systems[4].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM);
  homeworlds_system_rebuild_color_counts(&position.systems[4]);
  g_assert_true(homeworlds_view_calculate_system_center(&position, 2, 900.0, 600.0, &yellow_small_x, &yellow_small_y));
  g_assert_true(homeworlds_view_calculate_system_center(&position, 3, 900.0, 600.0, &blue_large_x, &blue_large_y));
  g_assert_true(homeworlds_view_calculate_system_center(&position, 4, 900.0, 600.0, &medium_x, &medium_y));
  g_assert_cmpfloat(yellow_small_y, <, medium_y);
  g_assert_cmpfloat(medium_y, <, blue_large_y);
}

static void test_homeworlds_view_row_layout_accounts_for_system_width(void) {
  HomeworldsPosition position = {0};
  double wide_x = 0.0;
  double wide_y = 0.0;
  double narrow_x = 0.0;
  double narrow_y = 0.0;

  test_homeworlds_prepare_compact_row_position(&position);
  test_homeworlds_make_wide_system(&position.systems[2], HOMEWORLDS_COLOR_RED);
  position.systems[3].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL);
  position.systems[3].ships[0][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL);
  homeworlds_system_rebuild_color_counts(&position.systems[3]);

  g_assert_true(homeworlds_view_calculate_system_center(&position, 2, 1000.0, 600.0, &wide_x, &wide_y));
  g_assert_true(homeworlds_view_calculate_system_center(&position, 3, 1000.0, 600.0, &narrow_x, &narrow_y));

  g_assert_cmpfloat(wide_y, ==, narrow_y);
  g_assert_cmpfloat(narrow_x, >, wide_x);
  g_assert_cmpfloat(narrow_x - wide_x, >, 240.0);
}

static void test_homeworlds_view_board_content_width_expands_for_wide_rows(void) {
  HomeworldsPosition position = {0};
  double content_width = 0.0;
  int content_size_width = 0;
  int content_size_height = 0;

  test_homeworlds_prepare_compact_row_position(&position);
  for (guint system_index = 2; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    test_homeworlds_make_wide_system(&position.systems[system_index],
                                     (HomeworldsColor)(system_index % (HOMEWORLDS_COLOR_BLUE + 1)));
  }

  content_width = homeworlds_view_calculate_board_content_width(&position, 900.0);
  g_assert_cmpfloat(content_width, >, 900.0);
  g_assert_true(homeworlds_view_calculate_board_content_size(&position,
                                                            900.0,
                                                            700.0,
                                                            &content_size_width,
                                                            &content_size_height));
  g_assert_cmpint(content_size_width, >, 900);
  g_assert_cmpint(content_size_height, ==, 700);
}

static void test_homeworlds_view_board_content_size_matches_viewport_when_rows_fit(void) {
  HomeworldsPosition position = {0};
  int content_width = 0;
  int content_height = 0;

  test_homeworlds_prepare_compact_row_position(&position);

  g_assert_true(homeworlds_view_calculate_board_content_size(&position,
                                                            900.0,
                                                            700.0,
                                                            &content_width,
                                                            &content_height));
  g_assert_cmpint(content_width, ==, 900);
  g_assert_cmpint(content_height, ==, 700);
  g_assert_cmpfloat(homeworlds_view_calculate_board_content_width(&position, 900.0), ==, 900.0);
}

static void test_homeworlds_view_board_content_height_expands_for_tall_rows(void) {
  HomeworldsPosition position = {0};
  int content_width = 0;
  int content_height = 0;

  position.phase = HOMEWORLDS_PHASE_PLAY;
  position.systems[0].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL);
  position.systems[0].stars[1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM);
  position.systems[1].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_MEDIUM);
  position.systems[1].stars[1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE);
  test_homeworlds_make_wide_system(&position.systems[2], HOMEWORLDS_COLOR_RED);
  test_homeworlds_make_wide_system(&position.systems[3], HOMEWORLDS_COLOR_RED);
  test_homeworlds_make_wide_system(&position.systems[4], HOMEWORLDS_COLOR_GREEN);
  homeworlds_position_rebuild_color_counts(&position);

  g_assert_true(homeworlds_view_calculate_board_content_size(&position,
                                                            1200.0,
                                                            220.0,
                                                            &content_width,
                                                            &content_height));
  g_assert_cmpint(content_width, >=, 1200);
  g_assert_cmpint(content_height, >, 220);
}

static void test_homeworlds_view_board_is_horizontally_scrollable(void) {
  HomeworldsPosition position = {0};
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = test_homeworlds_view_new_without_move_report(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  GtkWidget *board = test_homeworlds_find_widget_named(root, "homeworlds-board");
  GtkWidget *board_scroller = test_homeworlds_find_widget_named(root, "homeworlds-board-scroller");
  const GGameAppProfile *profile = ggame_app_profile_get_by_kind(GGAME_APP_KIND_HOMEWORLDS);
  GtkPolicyType horizontal_policy = GTK_POLICY_NEVER;
  GtkPolicyType vertical_policy = GTK_POLICY_NEVER;

  g_assert_nonnull(profile);
  g_assert_cmpint(profile->layout.default_board_panel_width, >=, 900);
  g_assert_cmpint(profile->layout.minimum_board_panel_width, >=, 700);
  g_assert_cmpint(profile->layout.minimum_board_panel_width, <, profile->layout.default_board_panel_width);
  g_assert_nonnull(board);
  g_assert_nonnull(board_scroller);
  g_assert_true(GTK_IS_SCROLLED_WINDOW(board_scroller));
  gtk_scrolled_window_get_policy(GTK_SCROLLED_WINDOW(board_scroller), &horizontal_policy, &vertical_policy);
  g_assert_cmpuint(horizontal_policy, ==, GTK_POLICY_AUTOMATIC);
  g_assert_cmpuint(vertical_policy, ==, GTK_POLICY_AUTOMATIC);
  g_assert_cmpint(gtk_scrolled_window_get_min_content_width(GTK_SCROLLED_WINDOW(board_scroller)), >=, 400);

  test_homeworlds_prepare_compact_row_position(&position);
  for (guint system_index = 2; system_index < HOMEWORLDS_SYSTEM_SLOT_COUNT; ++system_index) {
    test_homeworlds_make_wide_system(&position.systems[system_index],
                                     (HomeworldsColor)(system_index % (HOMEWORLDS_COLOR_BLUE + 1)));
  }
  g_assert_true(ggame_model_set_position(model, &position));
  g_assert_cmpint(gtk_drawing_area_get_content_width(GTK_DRAWING_AREA(board)), >, 900);

  homeworlds_view_free(view);
  g_object_unref(model);
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
  test_homeworlds_window_wait_for_draw(window);

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
    test_homeworlds_window_wait_for_draw(window);
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

static void test_homeworlds_view_piece_metrics_keep_pyramids_tall(void) {
  HomeworldsViewPyramidMetrics small = {0};
  HomeworldsViewPyramidMetrics medium = {0};
  HomeworldsViewPyramidMetrics large = {0};
  double pyramid_ratio = 0.0;
  double star_ratio = 0.0;

  g_assert_true(homeworlds_view_pyramid_metrics(HOMEWORLDS_SIZE_SMALL, &small));
  g_assert_true(homeworlds_view_pyramid_metrics(HOMEWORLDS_SIZE_MEDIUM, &medium));
  g_assert_true(homeworlds_view_pyramid_metrics(HOMEWORLDS_SIZE_LARGE, &large));

  pyramid_ratio = medium.height / small.height;
  star_ratio = homeworlds_view_star_side(HOMEWORLDS_SIZE_MEDIUM) /
               homeworlds_view_star_side(HOMEWORLDS_SIZE_SMALL);
  g_assert_cmpfloat_with_epsilon(small.height, 36.0, 0.001);
  g_assert_cmpfloat_with_epsilon(homeworlds_view_star_side(HOMEWORLDS_SIZE_SMALL), 20.0, 0.001);
  g_assert_cmpfloat_with_epsilon(homeworlds_view_pip_radius(), medium.height * 0.055, 0.001);
  g_assert_cmpfloat_with_epsilon(small.height / small.base, 2.0, 0.001);
  g_assert_cmpfloat_with_epsilon(medium.height / medium.base, 2.0, 0.001);
  g_assert_cmpfloat_with_epsilon(large.height / large.base, 2.0, 0.001);
  g_assert_cmpfloat_with_epsilon(large.height / medium.height, pyramid_ratio, 0.001);
  g_assert_cmpfloat_with_epsilon(homeworlds_view_star_side(HOMEWORLDS_SIZE_LARGE) /
                                 homeworlds_view_star_side(HOMEWORLDS_SIZE_MEDIUM),
                                 star_ratio,
                                 0.001);
  g_assert_cmpfloat(small.height, <, medium.height);
  g_assert_cmpfloat(medium.height, <, large.height);
}

static void test_homeworlds_window_replaces_skeleton(void) {
  GtkApplication *app = NULL;
  GGameModel *model = NULL;
  GGameWindow *window = NULL;
  GtkWidget *root = NULL;
  GtkWidget *board_panel = NULL;
  const GGameAppProfile *profile = ggame_app_profile_get_by_kind(GGAME_APP_KIND_HOMEWORLDS);
  gint board_panel_width_request = -1;

  window = test_homeworlds_create_window(&app, &model);
  root = GTK_WIDGET(window);
  board_panel = g_object_get_data(G_OBJECT(window), "board-panel");

  g_assert_nonnull(profile);
  g_assert_nonnull(ggame_window_get_sgf_controller(window));
  g_assert_nonnull(test_homeworlds_find_widget_named(root, "homeworlds-view"));
  g_assert_nonnull(test_homeworlds_find_widget_named(root, "homeworlds-board"));
  g_assert_nonnull(test_homeworlds_find_widget_named(root, "homeworlds-board-bank"));
  g_assert_nonnull(board_panel);
  gtk_widget_get_size_request(board_panel, &board_panel_width_request, NULL);
  g_assert_cmpint(board_panel_width_request, ==, profile->layout.minimum_board_panel_width);
  g_assert_cmpint(board_panel_width_request, <, profile->layout.default_board_panel_width);
  g_assert_nonnull(test_homeworlds_find_widget_for_action(root, "app.new-game"));
  g_assert_nonnull(test_homeworlds_find_widget_for_action(root, "win.game-force-move"));
  g_assert_nonnull(test_homeworlds_find_widget_for_action(root, "win.navigation-step-forward"));
  g_assert_nonnull(test_homeworlds_find_widget_for_action(root, "win.navigation-step-backward"));
  g_assert_nonnull(test_homeworlds_find_widget_for_action(root, "win.navigation-rewind"));

  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
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
  test_homeworlds_window_wait_for_draw(window);

  height = gtk_widget_get_height(main_paned);
  g_assert_cmpint(height, >, 0);
  g_assert_cmpint(target_position, >, height);

  gtk_paned_set_position(GTK_PANED(main_paned), target_position);
  test_homeworlds_window_wait_for_draw(window);

  g_assert_cmpint(gtk_paned_get_position(GTK_PANED(main_paned)), >=, target_position);

  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);
}

static void test_homeworlds_view_board_content_size_tracks_viewport(void) {
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = test_homeworlds_view_new_without_move_report(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  GtkWidget *board = NULL;
  GtkWidget *board_scroller = NULL;
  GtkAdjustment *hadjustment = NULL;
  GtkAdjustment *vadjustment = NULL;

  board = test_homeworlds_find_widget_named(root, "homeworlds-board");
  board_scroller = test_homeworlds_find_widget_named(root, "homeworlds-board-scroller");

  g_assert_nonnull(board);
  g_assert_true(GTK_IS_DRAWING_AREA(board));
  g_assert_nonnull(board_scroller);
  g_assert_true(GTK_IS_SCROLLED_WINDOW(board_scroller));
  hadjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(board_scroller));
  g_assert_nonnull(hadjustment);
  vadjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(board_scroller));
  g_assert_nonnull(vadjustment);

  gtk_adjustment_configure(hadjustment, 0.0, 0.0, 1200.0, 1.0, 100.0, 1200.0);
  gtk_adjustment_configure(vadjustment, 0.0, 0.0, 500.0, 1.0, 100.0, 500.0);
  g_assert_cmpint(gtk_drawing_area_get_content_width(GTK_DRAWING_AREA(board)), ==, 1200);
  g_assert_cmpint(gtk_drawing_area_get_content_height(GTK_DRAWING_AREA(board)), ==, 500);

  gtk_adjustment_configure(hadjustment, 0.0, 0.0, 700.0, 1.0, 100.0, 700.0);
  gtk_adjustment_configure(vadjustment, 0.0, 0.0, 400.0, 1.0, 100.0, 400.0);
  g_assert_cmpint(gtk_drawing_area_get_content_width(GTK_DRAWING_AREA(board)), ==, 700);
  g_assert_cmpint(gtk_drawing_area_get_content_height(GTK_DRAWING_AREA(board)), ==, 400);

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_window_defaults_to_minimum_computer_depth(void) {
  GtkApplication *app = NULL;
  GGameModel *model = NULL;
  GGameWindow *window = test_homeworlds_create_window(&app, &model);
  PlayerControlsPanel *panel = ggame_window_get_controls_panel(window);

  g_assert_nonnull(panel);
  g_assert_cmpuint(player_controls_panel_get_mode(panel, 0), ==, PLAYER_CONTROL_MODE_USER);
  g_assert_cmpuint(player_controls_panel_get_mode(panel, 1), ==, PLAYER_CONTROL_MODE_COMPUTER);
  g_assert_cmpuint(player_controls_panel_get_computer_depth(panel), ==, PLAYER_COMPUTER_DEPTH_MIN);

  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);
}

static void test_homeworlds_window_setup_moves_are_recorded_in_sgf(void) {
  GtkApplication *app = NULL;
  GGameModel *model = NULL;
  GGameWindow *window = test_homeworlds_create_window(&app, &model);
  HomeworldsView *view = test_homeworlds_get_window_view(window);
  GGameSgfController *controller = ggame_window_get_sgf_controller(window);
  SgfTree *tree = ggame_sgf_controller_get_tree(controller);
  const SgfNode *current = NULL;
  const HomeworldsPosition *position = NULL;

  g_assert_nonnull(view);
  g_assert_nonnull(controller);
  g_assert_nonnull(tree);

  homeworlds_view_set_move_report_enabled(view, FALSE);

  for (guint i = 0; i < 6; ++i) {
    g_assert_true(homeworlds_view_apply_candidate_at(view, 0));
  }

  position = ggame_model_peek_position(model);
  g_assert_nonnull(position);
  g_assert_cmpuint(position->phase, ==, HOMEWORLDS_PHASE_PLAY);

  current = sgf_tree_get_current(tree);
  g_assert_nonnull(current);
  g_assert_cmpuint(sgf_node_get_move_number(current), ==, 2);
  g_assert_cmpstr(homeworlds_view_get_last_move_text(view), !=, "None");

  g_assert_true(ggame_sgf_controller_rewind_to_start(controller));
  g_assert_cmpstr(homeworlds_view_get_last_move_text(view), ==, "None");
  position = ggame_model_peek_position(model);
  g_assert_nonnull(position);
  g_assert_cmpuint(position->phase, ==, HOMEWORLDS_PHASE_SETUP);
  g_assert_cmpuint(position->turn, ==, 0);

  g_assert_true(ggame_sgf_controller_step_forward(controller));
  g_assert_cmpstr(homeworlds_view_get_last_move_text(view), !=, "None");
  position = ggame_model_peek_position(model);
  g_assert_nonnull(position);
  g_assert_cmpuint(position->phase, ==, HOMEWORLDS_PHASE_SETUP);
  g_assert_cmpuint(position->turn, ==, 1);

  g_assert_true(ggame_sgf_controller_step_forward(controller));
  g_assert_cmpstr(homeworlds_view_get_last_move_text(view), !=, "None");
  position = ggame_model_peek_position(model);
  g_assert_nonnull(position);
  g_assert_cmpuint(position->phase, ==, HOMEWORLDS_PHASE_PLAY);

  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);
}

static void test_homeworlds_window_catastrophe_prefix_records_single_sgf_move(void) {
  GtkApplication *app = NULL;
  GGameModel *model = NULL;
  GGameWindow *window = test_homeworlds_create_window(&app, &model);
  HomeworldsView *view = test_homeworlds_get_window_view(window);
  GtkWidget *root = GTK_WIDGET(window);
  PlayerControlsPanel *panel = ggame_window_get_controls_panel(window);
  GGameSgfController *controller = ggame_window_get_sgf_controller(window);
  SgfTree *tree = ggame_sgf_controller_get_tree(controller);
  HomeworldsPosition position = {0};
  HomeworldsMove move = {0};
  GtkButton *catastrophe_button = NULL;
  GtkButton *pass_button = NULL;

  g_assert_nonnull(view);
  g_assert_nonnull(panel);
  g_assert_nonnull(controller);
  g_assert_nonnull(tree);

  homeworlds_view_set_move_report_enabled(view, FALSE);
  player_controls_panel_set_mode(panel, 1, PLAYER_CONTROL_MODE_USER);
  test_homeworlds_prepare_play_position(&position);
  position.systems[2].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL);
  position.systems[2].ships[0][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL);
  position.systems[2].ships[0][1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_MEDIUM);
  position.systems[2].ships[1][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE);
  position.systems[2].ships[1][1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL);
  homeworlds_system_rebuild_color_counts(&position.systems[2]);
  g_assert_true(ggame_model_set_position(model, &position));

  catastrophe_button = test_homeworlds_find_catastrophe_button(root, 2, HOMEWORLDS_COLOR_BLUE);
  g_assert_nonnull(catastrophe_button);
  g_assert_cmpstr(gtk_button_get_label(catastrophe_button), ==, "Catastrophe blue at S0");
  g_signal_emit_by_name(catastrophe_button, "clicked");
  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 0);

  pass_button = test_homeworlds_find_non_visual_action_button(root, HOMEWORLDS_STEP_PASS);
  g_assert_nonnull(pass_button);
  g_signal_emit_by_name(pass_button, "clicked");

  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 1);
  g_assert_true(ggame_sgf_controller_get_current_node_move(controller, &move));
  g_assert_cmpuint(move.kind, ==, HOMEWORLDS_MOVE_KIND_TURN);
  g_assert_cmpuint(move.step_count, ==, 2);
  g_assert_cmpuint(move.steps[0].kind, ==, HOMEWORLDS_STEP_CATASTROPHE);
  g_assert_cmpuint(move.steps[1].kind, ==, HOMEWORLDS_STEP_PASS);

  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);
}

static void test_homeworlds_window_end_move_catastrophe_requires_choice(void) {
  GtkApplication *app = NULL;
  GGameModel *model = NULL;
  GGameWindow *window = test_homeworlds_create_window(&app, &model);
  HomeworldsView *view = test_homeworlds_get_window_view(window);
  GtkWidget *root = GTK_WIDGET(window);
  PlayerControlsPanel *panel = ggame_window_get_controls_panel(window);
  GGameSgfController *controller = ggame_window_get_sgf_controller(window);
  SgfTree *tree = ggame_sgf_controller_get_tree(controller);
  HomeworldsPosition position = {0};
  HomeworldsMove move = {0};
  GtkButton *ship_button = NULL;
  GtkButton *build_button = NULL;
  GtkButton *catastrophe_button = NULL;
  HomeworldsPyramid red_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE);
  HomeworldsPyramid red_medium = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_MEDIUM);
  HomeworldsPyramid green_small = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL);
  HomeworldsPyramid blue_small = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL);
  HomeworldsPyramid blue_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE);
  HomeworldsPyramid yellow_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE);

  g_assert_nonnull(view);
  g_assert_nonnull(panel);
  g_assert_nonnull(controller);
  g_assert_nonnull(tree);

  homeworlds_view_set_move_report_enabled(view, FALSE);
  player_controls_panel_set_mode(panel, 1, PLAYER_CONTROL_MODE_USER);
  homeworlds_position_init(&position);
  position.phase = HOMEWORLDS_PHASE_PLAY;
  position.turn = 0;
  memset(position.systems, 0, sizeof(position.systems));
  position.systems[0] = (HomeworldsSystem){
    .stars = {
      green_small,
      red_medium,
    },
    .ships = {
      [0] = {
        red_large,
        red_medium,
      },
    },
  };
  position.systems[1] = (HomeworldsSystem){
    .stars = {
      blue_small,
      yellow_large,
    },
    .ships = {
      [1] = {
        blue_large,
      },
    },
  };
  homeworlds_position_rebuild_color_counts(&position);
  test_homeworlds_remove_bank_piece(&position, green_small);
  test_homeworlds_remove_bank_piece(&position, red_medium);
  test_homeworlds_remove_bank_piece(&position, red_large);
  test_homeworlds_remove_bank_piece(&position, red_medium);
  test_homeworlds_remove_bank_piece(&position, blue_small);
  test_homeworlds_remove_bank_piece(&position, yellow_large);
  test_homeworlds_remove_bank_piece(&position, blue_large);
  g_assert_true(ggame_model_set_position(model, &position));

  ship_button = test_homeworlds_find_ship_button_for(root, 0, red_large);
  g_assert_nonnull(ship_button);
  g_signal_emit_by_name(ship_button, "clicked");
  build_button = test_homeworlds_find_non_visual_action_button(root, HOMEWORLDS_STEP_BUILD);
  g_assert_nonnull(build_button);
  g_signal_emit_by_name(build_button, "clicked");

  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 0);
  g_assert_nonnull(test_homeworlds_find_finish_catastrophes_pass_button(root));
  catastrophe_button = test_homeworlds_find_catastrophe_button(root, 0, HOMEWORLDS_COLOR_RED);
  g_assert_nonnull(catastrophe_button);
  g_assert_cmpstr(gtk_button_get_label(catastrophe_button), ==, "Catastrophe red at H1");
  g_signal_emit_by_name(catastrophe_button, "clicked");

  g_assert_cmpuint(sgf_node_get_move_number(sgf_tree_get_current(tree)), ==, 1);
  g_assert_true(ggame_sgf_controller_get_current_node_move(controller, &move));
  g_assert_cmpuint(move.kind, ==, HOMEWORLDS_MOVE_KIND_TURN);
  g_assert_cmpuint(move.step_count, ==, 2);
  g_assert_cmpuint(move.steps[0].kind, ==, HOMEWORLDS_STEP_BUILD);
  g_assert_cmpuint(move.steps[1].kind, ==, HOMEWORLDS_STEP_CATASTROPHE);
  g_assert_cmpuint(move.steps[1].target_color, ==, HOMEWORLDS_COLOR_RED);

  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);
}

static void test_homeworlds_view_setup_uses_board_bank_buttons(void) {
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = test_homeworlds_view_new_without_move_report(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  const HomeworldsPosition *position = NULL;
  HomeworldsPyramid large_star = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE);
  HomeworldsPyramid small_ship = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_SMALL);
  GtkButton *button = NULL;

  g_assert_cmpuint(test_homeworlds_count_widgets_with_data(root, "homeworlds-board-bank-choice"), ==, 12);

  button = test_homeworlds_find_bank_button_for_pyramid(root, large_star);
  g_assert_nonnull(button);
  g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(button)));
  g_assert_true(gtk_widget_has_css_class(GTK_WIDGET(button), "homeworlds-bank-choice"));
  g_signal_emit_by_name(button, "clicked");

  button = test_homeworlds_find_bank_button_for_pyramid(root, large_star);
  g_assert_nonnull(button);
  g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(button)));
  g_assert_true(gtk_widget_has_css_class(GTK_WIDGET(button), "homeworlds-bank-choice"));
  g_signal_emit_by_name(button, "clicked");

  button = test_homeworlds_find_bank_button_for_pyramid(root, small_ship);
  g_assert_nonnull(button);
  g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(button)));
  g_assert_true(gtk_widget_has_css_class(GTK_WIDGET(button), "homeworlds-bank-choice"));
  g_signal_emit_by_name(button, "clicked");

  for (guint i = 0; i < 3; ++i) {
    button = test_homeworlds_find_selectable_bank_button(root);
    g_assert_nonnull(button);
    g_signal_emit_by_name(button, "clicked");
  }

  position = ggame_model_peek_position(model);
  g_assert_nonnull(position);
  g_assert_cmpuint(position->phase, ==, HOMEWORLDS_PHASE_PLAY);

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_view_bank_layout_is_compact_and_centered(void) {
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = test_homeworlds_view_new_without_move_report(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  GtkWidget *bank = test_homeworlds_find_widget_named(root, "homeworlds-board-bank");
  GtkWidget *title = test_homeworlds_find_widget_named(root, "homeworlds-bank-title");
  GtkWidget *grid = test_homeworlds_find_widget_named(root, "homeworlds-bank-grid");
  GtkWidget *first_grid_child = NULL;
  GtkWidget *small_button = NULL;
  GtkWidget *large_button = NULL;
  HomeworldsPyramid red_small = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_SMALL);
  HomeworldsPyramid red_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_RED, HOMEWORLDS_SIZE_LARGE);
  int small_width = 0;
  int large_width = 0;

  g_assert_nonnull(bank);
  g_assert_cmpuint(gtk_widget_get_valign(bank), ==, GTK_ALIGN_CENTER);
  g_assert_nonnull(title);
  g_assert_true(gtk_widget_has_css_class(title, "homeworlds-bank-title"));
  g_assert_nonnull(grid);
  g_assert_true(GTK_IS_GRID(grid));
  g_assert_cmpuint(gtk_grid_get_column_spacing(GTK_GRID(grid)), ==, 7);
  g_assert_cmpuint(test_homeworlds_count_labels_with_text(bank, "R"), ==, 0);
  g_assert_cmpuint(test_homeworlds_count_labels_with_text(bank, "V"), ==, 0);
  g_assert_cmpuint(test_homeworlds_count_labels_with_text(bank, "B"), ==, 0);
  g_assert_cmpuint(test_homeworlds_count_labels_with_text(bank, "Y"), ==, 0);
  g_assert_cmpuint(test_homeworlds_count_labels_with_text(bank, "x1"), ==, 0);
  g_assert_cmpuint(test_homeworlds_count_labels_with_text(bank, "x2"), ==, 0);
  g_assert_cmpuint(test_homeworlds_count_labels_with_text(bank, "x3"), ==, 0);
  g_assert_cmpuint(test_homeworlds_count_bank_buttons_without_compact_style(bank), ==, 0);
  g_assert_cmpuint(test_homeworlds_count_bank_buttons_with_css_class(bank, "homeworlds-bank-choice"), ==, 12);
  first_grid_child = gtk_grid_get_child_at(GTK_GRID(grid), 0, 0);
  g_assert_nonnull(first_grid_child);
  g_assert_cmpuint(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(first_grid_child), "homeworlds-bank-pyramid")),
                   ==,
                   red_small);

  small_button = GTK_WIDGET(test_homeworlds_find_bank_button_for_pyramid(root, red_small));
  large_button = GTK_WIDGET(test_homeworlds_find_bank_button_for_pyramid(root, red_large));
  g_assert_nonnull(small_button);
  g_assert_nonnull(large_button);
  gtk_widget_get_size_request(small_button, &small_width, NULL);
  gtk_widget_get_size_request(large_button, &large_width, NULL);
  g_assert_cmpint(small_width, <, large_width);

  homeworlds_view_free(view);
  g_object_unref(model);
}

static gint test_homeworlds_widget_natural_width(GtkWidget *widget) {
  gint natural = 0;

  g_return_val_if_fail(GTK_IS_WIDGET(widget), 0);

  gtk_widget_measure(widget, GTK_ORIENTATION_HORIZONTAL, -1, NULL, &natural, NULL, NULL);
  return natural;
}

static void test_homeworlds_view_bank_layout_stays_compact_after_setup(void) {
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = test_homeworlds_view_new_without_move_report(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  GtkWidget *bank = NULL;
  gint setup_bank_width = 0;

  g_assert_nonnull(model);
  g_assert_nonnull(view);
  g_assert_nonnull(root);
  ggame_style_init();

  bank = test_homeworlds_find_widget_named(root, "homeworlds-board-bank");
  g_assert_nonnull(bank);
  setup_bank_width = test_homeworlds_widget_natural_width(bank);
  g_assert_cmpint(setup_bank_width, >, 0);
  g_assert_cmpuint(test_homeworlds_count_bank_buttons_without_compact_style(bank), ==, 0);
  g_assert_cmpuint(test_homeworlds_count_bank_buttons_with_css_class(bank, "homeworlds-bank-choice"), ==, 12);

  for (guint step = 0; step < 6; step++) {
    g_assert_true(homeworlds_view_apply_candidate_at(view, 0));
  }

  bank = test_homeworlds_find_widget_named(root, "homeworlds-board-bank");
  g_assert_nonnull(bank);
  g_assert_cmpint(test_homeworlds_widget_natural_width(bank), <=, setup_bank_width);
  g_assert_cmpuint(test_homeworlds_count_bank_buttons_without_compact_style(bank), ==, 0);
  g_assert_cmpuint(test_homeworlds_count_bank_buttons_with_css_class(bank, "homeworlds-bank-choice"), ==, 0);

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_view_uses_board_ship_buttons(void) {
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = test_homeworlds_view_new_without_move_report(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  GtkButton *button = NULL;

  for (guint i = 0; i < 6; ++i) {
    g_assert_true(homeworlds_view_apply_candidate_at(view, 0));
  }

  g_assert_cmpuint(homeworlds_view_get_candidate_count(view), >, 0);
  g_assert_cmpuint(test_homeworlds_count_non_visual_candidate_buttons(root, HOMEWORLDS_CANDIDATE_SELECT_SHIP), ==, 0);

  button = test_homeworlds_find_selectable_ship_button(root);
  g_assert_nonnull(button);
  g_assert_true(gtk_widget_has_css_class(GTK_WIDGET(button), "homeworlds-board-choice"));
  g_signal_emit_by_name(button, "clicked");
  g_assert_true(homeworlds_view_has_partial_selection(view));

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_view_action_buttons_use_plain_labels(void) {
  HomeworldsPosition position = {0};
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = test_homeworlds_view_new_without_move_report(model);
  GtkWidget *root = homeworlds_view_get_widget(view);

  test_homeworlds_prepare_play_position(&position);
  memset(position.systems[0].ships[0], 0, sizeof(position.systems[0].ships[0]));
  position.systems[0].ships[0][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE);
  position.systems[0].ships[0][1] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE);
  homeworlds_system_rebuild_color_counts(&position.systems[0]);
  g_assert_true(ggame_model_set_position(model, &position));

  test_homeworlds_assert_action_button_label(root, HOMEWORLDS_STEP_PASS, "Pass");

  g_assert_true(homeworlds_view_apply_candidate_at(view, 0));
  test_homeworlds_assert_action_button_label(root, HOMEWORLDS_STEP_ATTACK, "Capture");
  test_homeworlds_assert_action_button_label(root, HOMEWORLDS_STEP_MOVE, "Move");
  test_homeworlds_assert_action_button_label(root, HOMEWORLDS_STEP_BUILD, "Build");
  test_homeworlds_assert_action_button_label(root, HOMEWORLDS_STEP_TRADE, "Trade");

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_view_build_action_is_available_from_each_green_ship(void) {
  HomeworldsPosition position = {0};
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = test_homeworlds_view_new_without_move_report(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  HomeworldsPyramid green_small = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_SMALL);
  HomeworldsPyramid green_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE);
  GtkButton *button = NULL;

  test_homeworlds_prepare_play_position(&position);
  memset(position.systems[0].ships[0], 0, sizeof(position.systems[0].ships[0]));
  position.systems[0].ships[0][0] = green_large;
  position.systems[0].ships[0][1] = green_small;
  homeworlds_system_rebuild_color_counts(&position.systems[0]);
  g_assert_true(ggame_model_set_position(model, &position));

  button = test_homeworlds_find_ship_button_for(root, 0, green_large);
  g_assert_nonnull(button);
  g_signal_emit_by_name(button, "clicked");
  g_assert_nonnull(test_homeworlds_find_non_visual_action_button(root, HOMEWORLDS_STEP_BUILD));
  homeworlds_view_reset_selection(view);

  button = test_homeworlds_find_ship_button_for(root, 0, green_small);
  g_assert_nonnull(button);
  g_signal_emit_by_name(button, "clicked");
  g_assert_nonnull(test_homeworlds_find_non_visual_action_button(root, HOMEWORLDS_STEP_BUILD));

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_view_choice_list_has_cancel_button(void) {
  HomeworldsPosition position = {0};
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = test_homeworlds_view_new_without_move_report(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  GtkButton *button = NULL;

  test_homeworlds_prepare_play_position(&position);
  g_assert_true(ggame_model_set_position(model, &position));
  g_assert_null(test_homeworlds_find_cancel_choice_button(root));

  g_assert_true(homeworlds_view_apply_candidate_at(view, 0));
  g_assert_true(homeworlds_view_has_partial_selection(view));

  button = test_homeworlds_find_cancel_choice_button(root);
  g_assert_nonnull(button);
  g_assert_cmpstr(gtk_button_get_label(button), ==, "Cancel");

  g_signal_emit_by_name(button, "clicked");
  g_assert_false(homeworlds_view_has_partial_selection(view));
  g_assert_null(test_homeworlds_find_cancel_choice_button(root));
  g_assert_nonnull(test_homeworlds_find_selectable_ship_button(root));

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_view_build_has_no_second_step_highlight(void) {
  HomeworldsPosition position = {0};
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = test_homeworlds_view_new_without_move_report(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  GtkButton *button = NULL;

  test_homeworlds_prepare_play_position(&position);
  g_assert_true(ggame_model_set_position(model, &position));

  g_assert_true(homeworlds_view_apply_candidate_at(view, 0));
  button = test_homeworlds_find_non_visual_action_button(root, HOMEWORLDS_STEP_BUILD);
  g_assert_nonnull(button);
  g_signal_emit_by_name(button, "clicked");

  g_assert_false(homeworlds_view_has_partial_selection(view));
  g_assert_cmpuint(test_homeworlds_count_active_ship_highlights(root), ==, 0);

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_view_trade_targets_use_bank_buttons(void) {
  HomeworldsPosition position = {0};
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = test_homeworlds_view_new_without_move_report(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  HomeworldsPyramid blue_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE);
  GtkButton *button = NULL;

  test_homeworlds_prepare_play_position(&position);
  g_assert_true(ggame_model_set_position(model, &position));

  g_assert_true(homeworlds_view_apply_candidate_at(view, 0));
  button = test_homeworlds_find_non_visual_action_button(root, HOMEWORLDS_STEP_TRADE);
  g_assert_nonnull(button);
  g_signal_emit_by_name(button, "clicked");

  g_assert_cmpuint(test_homeworlds_count_active_ship_highlights(root), ==, 1);
  g_assert_cmpuint(test_homeworlds_count_non_visual_candidate_buttons(root, HOMEWORLDS_CANDIDATE_TRADE_COLOR), ==, 0);
  button = test_homeworlds_find_bank_button_for_pyramid(root, blue_large);
  g_assert_nonnull(button);
  g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(button)));
  g_assert_true(gtk_widget_has_css_class(GTK_WIDGET(button), "homeworlds-bank-choice"));
  g_signal_emit_by_name(button, "clicked");
  g_assert_false(homeworlds_view_has_partial_selection(view));
  g_assert_cmpuint(test_homeworlds_count_active_ship_highlights(root), ==, 0);

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_view_attack_targets_use_board_buttons(void) {
  HomeworldsPosition position = {0};
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = test_homeworlds_view_new_without_move_report(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  GtkButton *button = NULL;

  test_homeworlds_prepare_play_position(&position);
  position.systems[0].ships[1][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_SMALL);
  homeworlds_system_rebuild_color_counts(&position.systems[0]);
  g_assert_true(ggame_model_set_position(model, &position));

  g_assert_true(homeworlds_view_apply_candidate_at(view, 0));
  button = test_homeworlds_find_non_visual_action_button(root, HOMEWORLDS_STEP_ATTACK);
  g_assert_nonnull(button);
  g_signal_emit_by_name(button, "clicked");

  g_assert_cmpuint(test_homeworlds_count_active_ship_highlights(root), ==, 1);
  g_assert_cmpuint(test_homeworlds_count_non_visual_candidate_buttons(root, HOMEWORLDS_CANDIDATE_ATTACK_TARGET), ==, 0);
  button = test_homeworlds_find_selectable_ship_button(root);
  g_assert_nonnull(button);
  g_assert_true(gtk_widget_has_css_class(GTK_WIDGET(button), "homeworlds-board-choice"));
  g_signal_emit_by_name(button, "clicked");
  g_assert_false(homeworlds_view_has_partial_selection(view));
  g_assert_cmpuint(test_homeworlds_count_active_ship_highlights(root), ==, 0);

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_view_move_targets_use_board_and_bank_buttons(void) {
  HomeworldsPosition position = {0};
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = test_homeworlds_view_new_without_move_report(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  HomeworldsPyramid blue_large = homeworlds_pyramid_make(HOMEWORLDS_COLOR_BLUE, HOMEWORLDS_SIZE_LARGE);
  GtkButton *button = NULL;

  test_homeworlds_prepare_play_position(&position);
  position.systems[0].ships[0][0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_YELLOW, HOMEWORLDS_SIZE_LARGE);
  position.systems[2].stars[0] = homeworlds_pyramid_make(HOMEWORLDS_COLOR_GREEN, HOMEWORLDS_SIZE_LARGE);
  homeworlds_position_rebuild_color_counts(&position);
  g_assert_true(ggame_model_set_position(model, &position));

  g_assert_true(homeworlds_view_apply_candidate_at(view, 0));
  button = test_homeworlds_find_non_visual_action_button(root, HOMEWORLDS_STEP_MOVE);
  g_assert_nonnull(button);
  g_signal_emit_by_name(button, "clicked");

  g_assert_cmpuint(test_homeworlds_count_active_ship_highlights(root), ==, 1);
  g_assert_cmpuint(test_homeworlds_count_non_visual_candidate_buttons(root, HOMEWORLDS_CANDIDATE_MOVE_TARGET), ==, 0);
  button = test_homeworlds_find_selectable_system_button(root);
  g_assert_nonnull(button);
  g_assert_true(gtk_widget_has_css_class(GTK_WIDGET(button), "homeworlds-board-choice"));

  button = test_homeworlds_find_bank_button_for_pyramid(root, blue_large);
  g_assert_nonnull(button);
  g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(button)));
  g_assert_true(gtk_widget_has_css_class(GTK_WIDGET(button), "homeworlds-bank-choice"));

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_view_advances_setup(void) {
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = test_homeworlds_view_new_without_move_report(model);
  const HomeworldsPosition *position = NULL;

  for (guint i = 0; i < 6; ++i) {
    g_assert_cmpuint(homeworlds_view_get_candidate_count(view), >, 0);
    g_assert_true(homeworlds_view_apply_candidate_at(view, 0));
  }

  position = ggame_model_peek_position(model);
  g_assert_nonnull(position);
  g_assert_cmpuint(position->phase, ==, HOMEWORLDS_PHASE_PLAY);
  g_assert_true(homeworlds_system_has_star(&position->systems[0]));
  g_assert_true(homeworlds_system_has_star(&position->systems[1]));
  g_assert_cmpuint(homeworlds_system_ship_count_for_side(&position->systems[0], 0), ==, 1);
  g_assert_cmpuint(homeworlds_system_ship_count_for_side(&position->systems[1], 1), ==, 1);

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_view_move_report_lists_good_and_other_moves(void) {
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = homeworlds_view_new(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  GtkWidget *move_report = NULL;
  HomeworldsPosition position = {0};
  g_autofree char *text = NULL;
  const char *count_header = NULL;
  const char *good_count = NULL;
  const char *all_count = NULL;
  const char *good_list = NULL;
  const char *other_list = NULL;

  test_homeworlds_prepare_play_position(&position);
  g_assert_true(ggame_model_set_position(model, &position));
  move_report = test_homeworlds_find_widget_named(root, "homeworlds-move-report");
  g_assert_nonnull(move_report);
  g_assert_true(GTK_IS_TEXT_VIEW(move_report));
  g_assert_false(gtk_text_view_get_editable(GTK_TEXT_VIEW(move_report)));
  g_assert_false(gtk_text_view_get_cursor_visible(GTK_TEXT_VIEW(move_report)));
  g_assert_cmpint(gtk_text_view_get_wrap_mode(GTK_TEXT_VIEW(move_report)), ==, GTK_WRAP_WORD_CHAR);

  text = test_homeworlds_get_text_view_text(move_report);
  count_header = strstr(text, "Move counts:\n");
  good_count = strstr(text, "good_moves(): ");
  all_count = strstr(text, "all moves: ");
  good_list = strstr(text, "\ngood_moves():\n");
  other_list = strstr(text, "\nall possible moves minus good_moves():\n");
  g_assert_nonnull(count_header);
  g_assert_nonnull(good_count);
  g_assert_nonnull(all_count);
  g_assert_nonnull(good_list);
  g_assert_nonnull(other_list);
  g_assert_true(count_header < good_list);
  g_assert_true(good_count < good_list);
  g_assert_true(all_count < good_list);
  g_assert_true(good_list < other_list);
  g_assert_nonnull(strstr(text, "pass"));
  g_assert_nonnull(strstr(text, "H1g3-"));

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_view_move_report_can_be_disabled(void) {
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  HomeworldsView *view = homeworlds_view_new(model);
  GtkWidget *root = homeworlds_view_get_widget(view);
  GtkWidget *move_report = NULL;
  HomeworldsPosition position = {0};
  g_autofree char *text = NULL;

  move_report = test_homeworlds_find_widget_named(root, "homeworlds-move-report");
  g_assert_nonnull(move_report);
  g_assert_true(GTK_IS_TEXT_VIEW(move_report));

  homeworlds_view_set_move_report_enabled(view, FALSE);
  g_assert_false(homeworlds_view_get_move_report_enabled(view));
  test_homeworlds_prepare_play_position(&position);
  g_assert_true(ggame_model_set_position(model, &position));

  text = test_homeworlds_get_text_view_text(move_report);
  g_assert_cmpstr(text, ==, "Move report disabled.");
  g_assert_null(strstr(text, "good_moves()"));
  g_assert_null(strstr(text, "H1g3-"));

  homeworlds_view_set_move_report_enabled(view, TRUE);
  g_assert_true(homeworlds_view_get_move_report_enabled(view));
  g_clear_pointer(&text, g_free);
  text = test_homeworlds_get_text_view_text(move_report);
  g_assert_nonnull(strstr(text, "good_moves()"));
  g_assert_nonnull(strstr(text, "H1g3-"));

  homeworlds_view_free(view);
  g_object_unref(model);
}

static void test_homeworlds_board_host_initial_move_report_state_is_applied(void) {
  GGameModel *model = ggame_model_new(&homeworlds_game_backend);
  GGameAppBoardHostOptions options = {
    .move_report_enabled = FALSE,
  };
  GtkWidget *host = NULL;
  HomeworldsView *view = NULL;
  GtkWidget *move_report = NULL;
  HomeworldsPosition position = {0};
  g_autofree char *text = NULL;

  test_homeworlds_prepare_play_position(&position);
  g_assert_true(ggame_model_set_position(model, &position));

  host = homeworlds_view_create_board_host(model, NULL, NULL, NULL, &options);
  g_assert_nonnull(host);
  g_object_ref_sink(host);

  view = g_object_get_data(G_OBJECT(host), "homeworlds-view-state");
  g_assert_nonnull(view);
  g_assert_false(homeworlds_view_get_move_report_enabled(view));

  move_report = test_homeworlds_find_widget_named(host, "homeworlds-move-report");
  g_assert_nonnull(move_report);
  g_assert_true(GTK_IS_TEXT_VIEW(move_report));

  text = test_homeworlds_get_text_view_text(move_report);
  g_assert_cmpstr(text, ==, "Move report disabled.");
  g_assert_null(strstr(text, "good_moves()"));
  g_assert_null(strstr(text, "H1g3-"));

  g_object_unref(host);
  g_object_unref(model);
}

static void test_homeworlds_window_move_report_action_toggles_view(void) {
  GtkApplication *app = NULL;
  GGameModel *model = NULL;
  GGameWindow *window = test_homeworlds_create_window(&app, &model);
  HomeworldsView *view = test_homeworlds_get_window_view(window);
  GtkWidget *root = homeworlds_view_get_widget(view);
  GtkWidget *move_report = NULL;
  HomeworldsPosition position = {0};
  GAction *action = NULL;
  g_autoptr(GVariant) state = NULL;
  g_autofree char *text = NULL;

  test_homeworlds_prepare_play_position(&position);
  g_assert_true(ggame_model_set_position(model, &position));

  move_report = test_homeworlds_find_widget_named(root, "homeworlds-move-report");
  g_assert_nonnull(move_report);
  g_assert_true(GTK_IS_TEXT_VIEW(move_report));
  action = g_action_map_lookup_action(G_ACTION_MAP(window), "view-show-move-report");
  g_assert_nonnull(action);
  state = g_action_get_state(action);
  g_assert_nonnull(state);
  g_assert_true(g_variant_get_boolean(state));
  g_assert_true(g_action_get_enabled(action));
  g_assert_true(homeworlds_view_get_move_report_enabled(view));
  text = test_homeworlds_get_text_view_text(move_report);
  g_assert_nonnull(strstr(text, "good_moves()"));

  g_action_group_change_action_state(G_ACTION_GROUP(window),
                                     "view-show-move-report",
                                     g_variant_new_boolean(FALSE));
  g_assert_false(homeworlds_view_get_move_report_enabled(view));
  g_clear_pointer(&text, g_free);
  text = test_homeworlds_get_text_view_text(move_report);
  g_assert_cmpstr(text, ==, "Move report disabled.");

  g_assert_true(ggame_model_set_position(model, &position));
  g_clear_pointer(&text, g_free);
  text = test_homeworlds_get_text_view_text(move_report);
  g_assert_cmpstr(text, ==, "Move report disabled.");
  g_assert_null(strstr(text, "good_moves()"));

  g_action_group_change_action_state(G_ACTION_GROUP(window),
                                     "view-show-move-report",
                                     g_variant_new_boolean(TRUE));
  g_assert_true(homeworlds_view_get_move_report_enabled(view));
  g_clear_pointer(&text, g_free);
  text = test_homeworlds_get_text_view_text(move_report);
  g_assert_nonnull(strstr(text, "good_moves()"));
  g_assert_nonnull(strstr(text, "H1g3-"));

  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);
}

static void test_homeworlds_application_view_menu_has_move_report(void) {
  g_autoptr(GGameApplication) app = ggame_application_new();
  g_autoptr(GError) error = NULL;
  GMenuModel *menubar = NULL;
  g_autoptr(GMenuModel) view_menu = NULL;

  g_assert_nonnull(app);
  g_assert_true(g_application_register(G_APPLICATION(app), NULL, &error));
  g_assert_no_error(error);

  menubar = gtk_application_get_menubar(GTK_APPLICATION(app));
  g_assert_nonnull(menubar);
  view_menu = test_homeworlds_find_submenu(menubar, "View");
  g_assert_nonnull(view_menu);
  g_assert_true(test_homeworlds_menu_contains_item(view_menu, "Show move report"));
}

static void test_homeworlds_view_board_system_title_uses_player_names(void) {
  g_autoptr(SgfTree) tree = sgf_tree_new();
  SgfNode *root = (SgfNode *)sgf_tree_get_root(tree);
  g_assert_nonnull(root);
  g_assert_true(sgf_node_add_property(root, "PB", "Alice"));
  g_assert_true(sgf_node_add_property(root, "PW", "Bob"));

  g_autofree char *homeworld_1 = homeworlds_view_format_board_system_title(0, root);
  g_autofree char *homeworld_2 = homeworlds_view_format_board_system_title(1, root);
  g_autofree char *neutral_system = homeworlds_view_format_board_system_title(2, root);
  g_assert_cmpstr(homeworld_1, ==, "H1 (Alice)");
  g_assert_cmpstr(homeworld_2, ==, "H2 (Bob)");
  g_assert_cmpstr(neutral_system, ==, "S0");
}

static void test_homeworlds_import_dialog_starts_with_board_game_arena(void) {
  GtkApplication *app = NULL;
  GGameModel *model = NULL;
  GGameWindow *window = test_homeworlds_create_window(&app, &model);

  ggame_window_present_import_dialog(window);

  GtkWindow *dialog = test_homeworlds_find_toplevel_by_title("Import games");
  g_assert_nonnull(dialog);
  test_homeworlds_window_wait_for_draw(dialog);

  GtkButton *next_button = test_homeworlds_find_button_with_label(GTK_WIDGET(dialog), "Fetch game history");
  g_assert_nonnull(next_button);
  g_assert_true(gtk_widget_get_sensitive(GTK_WIDGET(next_button)));
  g_assert_nonnull(test_homeworlds_find_label_with_text(GTK_WIDGET(dialog), "Email"));
  g_assert_nonnull(test_homeworlds_find_label_with_text(GTK_WIDGET(dialog), "Password"));
  g_assert_nonnull(test_homeworlds_find_label_with_text(GTK_WIDGET(dialog), "BoardGameArena Homeworlds history"));

  GtkButton *back_button = test_homeworlds_find_button_with_label(GTK_WIDGET(dialog), "Back");
  g_assert_nonnull(back_button);
  g_assert_false(gtk_widget_get_sensitive(GTK_WIDGET(back_button)));

  GtkButton *cancel_button = test_homeworlds_find_button_with_label(GTK_WIDGET(dialog), "Cancel");
  g_assert_nonnull(cancel_button);
  g_signal_emit_by_name(cancel_button, "clicked");
  g_assert_null(test_homeworlds_find_toplevel_by_title("Import games"));

  g_clear_object(&dialog);
  gtk_window_destroy(GTK_WINDOW(window));
  g_object_unref(model);
  g_object_unref(app);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_autoptr(GError) autosave_error = NULL;
  g_autofree char *autosave_root = g_dir_make_tmp("ghomeworlds-window-autosave-XXXXXX", &autosave_error);
  g_assert_no_error(autosave_error);
  g_assert_nonnull(autosave_root);
  g_setenv(SGF_AUTOSAVE_ENV, autosave_root, TRUE);

  const GGameAppProfile *profile = ggame_app_profile_get_by_kind(GGAME_APP_KIND_HOMEWORLDS);
  int result = 0;

  g_assert_nonnull(profile);
  g_assert_true(ggame_app_profile_set_active(profile));
  test_homeworlds_window_reset_layout_settings();

  g_test_add_func("/homeworlds/view/homeworld-layout", test_homeworlds_view_homeworld_layout_uses_player_perspective);
  g_test_add_func("/homeworlds/view/system-layout", test_homeworlds_view_system_layout_groups_by_reachability);
  g_test_add_func("/homeworlds/view/connected-sparse-row-layout",
                  test_homeworlds_view_connected_sparse_rows_skip_empty_middle_gap);
  g_test_add_func("/homeworlds/view/width-aware-row-layout",
                  test_homeworlds_view_row_layout_accounts_for_system_width);
  g_test_add_func("/homeworlds/view/board-width-expands-for-wide-rows",
                  test_homeworlds_view_board_content_width_expands_for_wide_rows);
  g_test_add_func("/homeworlds/view/board-size-matches-viewport-when-rows-fit",
                  test_homeworlds_view_board_content_size_matches_viewport_when_rows_fit);
  g_test_add_func("/homeworlds/view/board-height-expands-for-tall-rows",
                  test_homeworlds_view_board_content_height_expands_for_tall_rows);
  g_test_add_func("/homeworlds/view/piece-metrics", test_homeworlds_view_piece_metrics_keep_pyramids_tall);
  g_test_add_func("/homeworlds/view/previous-move-markers-ship-actions",
                  test_homeworlds_view_previous_move_markers_describe_ship_actions);
  g_test_add_func("/homeworlds/view/previous-move-markers-catastrophes",
                  test_homeworlds_view_previous_move_markers_describe_catastrophes);
  g_test_add_func("/homeworlds/view/previous-move-markers-skip-sacrifice",
                  test_homeworlds_view_previous_move_markers_skip_sacrifice_step);
  if (!gtk_init_check()) {
    g_test_add_func("/homeworlds/window/replaces-skeleton", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/window/main-split-can-exceed-height", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/window/defaults-to-minimum-computer-depth", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/window/setup-recorded-in-sgf", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/setup-bank-buttons", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/bank-layout", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/bank-layout-stays-compact-after-setup", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/board-ship-buttons", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/action-button-labels", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/build-action-from-each-green-ship", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/choice-list-cancel", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/build-no-second-step-highlight", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/trade-bank-buttons", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/attack-board-buttons", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/move-board-bank-buttons", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/advances-setup", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/move-report", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/move-report-toggle", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/move-report-initial-state", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/board-scrollable", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/text-panel-fixed-width", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/board-content-size-tracks-viewport", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/window/move-report-action", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/window/view-menu-move-report", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/view/board-system-title-player-names", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/window/import-dialog-starts-with-bga", test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/window/catastrophe-prefix-records-single-sgf-move",
                    test_homeworlds_window_skip);
    g_test_add_func("/homeworlds/window/end-move-catastrophe-requires-choice",
                    test_homeworlds_window_skip);
  } else {
    g_test_add_func("/homeworlds/window/replaces-skeleton", test_homeworlds_window_replaces_skeleton);
    g_test_add_func("/homeworlds/window/main-split-can-exceed-height",
                    test_homeworlds_window_main_split_can_exceed_height);
    g_test_add_func("/homeworlds/window/defaults-to-minimum-computer-depth",
                    test_homeworlds_window_defaults_to_minimum_computer_depth);
    g_test_add_func("/homeworlds/window/setup-recorded-in-sgf",
                    test_homeworlds_window_setup_moves_are_recorded_in_sgf);
    g_test_add_func("/homeworlds/view/setup-bank-buttons", test_homeworlds_view_setup_uses_board_bank_buttons);
    g_test_add_func("/homeworlds/view/bank-layout", test_homeworlds_view_bank_layout_is_compact_and_centered);
    g_test_add_func("/homeworlds/view/bank-layout-stays-compact-after-setup",
                    test_homeworlds_view_bank_layout_stays_compact_after_setup);
    g_test_add_func("/homeworlds/view/board-ship-buttons", test_homeworlds_view_uses_board_ship_buttons);
    g_test_add_func("/homeworlds/view/action-button-labels", test_homeworlds_view_action_buttons_use_plain_labels);
    g_test_add_func("/homeworlds/view/build-action-from-each-green-ship",
                    test_homeworlds_view_build_action_is_available_from_each_green_ship);
    g_test_add_func("/homeworlds/view/choice-list-cancel",
                    test_homeworlds_view_choice_list_has_cancel_button);
    g_test_add_func("/homeworlds/view/build-no-second-step-highlight",
                    test_homeworlds_view_build_has_no_second_step_highlight);
    g_test_add_func("/homeworlds/view/trade-bank-buttons", test_homeworlds_view_trade_targets_use_bank_buttons);
    g_test_add_func("/homeworlds/view/attack-board-buttons", test_homeworlds_view_attack_targets_use_board_buttons);
    g_test_add_func("/homeworlds/view/move-board-bank-buttons",
                    test_homeworlds_view_move_targets_use_board_and_bank_buttons);
    g_test_add_func("/homeworlds/view/advances-setup", test_homeworlds_view_advances_setup);
    g_test_add_func("/homeworlds/view/move-report", test_homeworlds_view_move_report_lists_good_and_other_moves);
    g_test_add_func("/homeworlds/view/move-report-toggle", test_homeworlds_view_move_report_can_be_disabled);
    g_test_add_func("/homeworlds/view/move-report-initial-state",
                    test_homeworlds_board_host_initial_move_report_state_is_applied);
    g_test_add_func("/homeworlds/view/board-scrollable", test_homeworlds_view_board_is_horizontally_scrollable);
    g_test_add_func("/homeworlds/view/text-panel-fixed-width", test_homeworlds_view_text_panel_has_fixed_width);
    g_test_add_func("/homeworlds/view/board-content-size-tracks-viewport",
                    test_homeworlds_view_board_content_size_tracks_viewport);
    g_test_add_func("/homeworlds/window/move-report-action", test_homeworlds_window_move_report_action_toggles_view);
    g_test_add_func("/homeworlds/window/view-menu-move-report",
                    test_homeworlds_application_view_menu_has_move_report);
    g_test_add_func("/homeworlds/view/board-system-title-player-names",
                    test_homeworlds_view_board_system_title_uses_player_names);
    g_test_add_func("/homeworlds/window/import-dialog-starts-with-bga",
                    test_homeworlds_import_dialog_starts_with_board_game_arena);
    g_test_add_func("/homeworlds/window/catastrophe-prefix-records-single-sgf-move",
                    test_homeworlds_window_catastrophe_prefix_records_single_sgf_move);
    g_test_add_func("/homeworlds/window/end-move-catastrophe-requires-choice",
                    test_homeworlds_window_end_move_catastrophe_requires_choice);
  }

  result = g_test_run();
  g_clear_object(&test_homeworlds_app);
  return result;
}
