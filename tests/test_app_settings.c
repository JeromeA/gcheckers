#include "../src/app_settings.h"

#include <glib.h>

static void test_app_settings_defaults(void) {
  g_setenv("GSETTINGS_BACKEND", "memory", TRUE);

  g_autoptr(GSettings) settings = ggame_app_settings_create();
  g_assert_nonnull(settings);
  g_assert_true(g_settings_get_boolean(settings, GCHECKERS_APP_SETTINGS_KEY_SEND_PUZZLE_USAGE));
  g_assert_true(g_settings_get_boolean(settings, GCHECKERS_APP_SETTINGS_KEY_SEND_APPLICATION_USAGE));
  g_assert_false(ggame_app_settings_get_privacy_settings_shown(settings));
}

static void test_app_settings_mark_privacy_settings_shown(void) {
  g_setenv("GSETTINGS_BACKEND", "memory", TRUE);

  g_autoptr(GSettings) settings = ggame_app_settings_create();
  g_assert_nonnull(settings);

  g_assert_true(ggame_app_settings_mark_privacy_settings_shown(settings));
  g_assert_true(ggame_app_settings_get_privacy_settings_shown(settings));
}

static void test_app_settings_round_trip(void) {
  g_setenv("GSETTINGS_BACKEND", "memory", TRUE);

  g_autoptr(GSettings) settings = ggame_app_settings_create();
  g_assert_nonnull(settings);

  g_assert_true(g_settings_set_boolean(settings, GCHECKERS_APP_SETTINGS_KEY_SEND_PUZZLE_USAGE, FALSE));
  g_assert_true(g_settings_set_boolean(settings, GCHECKERS_APP_SETTINGS_KEY_SEND_APPLICATION_USAGE, TRUE));
  g_assert_true(ggame_app_settings_mark_privacy_settings_shown(settings));

  g_assert_false(g_settings_get_boolean(settings, GCHECKERS_APP_SETTINGS_KEY_SEND_PUZZLE_USAGE));
  g_assert_true(g_settings_get_boolean(settings, GCHECKERS_APP_SETTINGS_KEY_SEND_APPLICATION_USAGE));
  g_assert_true(ggame_app_settings_get_privacy_settings_shown(settings));
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/app-settings/defaults", test_app_settings_defaults);
  g_test_add_func("/app-settings/privacy-settings-shown", test_app_settings_mark_privacy_settings_shown);
  g_test_add_func("/app-settings/round-trip", test_app_settings_round_trip);
  return g_test_run();
}
