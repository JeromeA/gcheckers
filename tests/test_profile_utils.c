#include "test_profile_utils.h"

#include <string.h>

const char *ggame_test_profile_kind_name(GGameAppKind kind) {
  switch (kind) {
    case GGAME_APP_KIND_CHECKERS:
      return "checkers";
    case GGAME_APP_KIND_HOMEWORLDS:
      return "homeworlds";
    case GGAME_APP_KIND_BOOP:
      return "boop";
    default:
      return "unknown";
  }
}

static gboolean ggame_test_take_profile_argument(int *argc, char ***argv, const char **out_profile_id) {
  g_return_val_if_fail(argc != NULL, FALSE);
  g_return_val_if_fail(argv != NULL, FALSE);
  g_return_val_if_fail(*argv != NULL, FALSE);
  g_return_val_if_fail(out_profile_id != NULL, FALSE);

  for (int i = 1; i < *argc; ++i) {
    const char *arg = (*argv)[i];
    if (arg == NULL) {
      continue;
    }

    if (g_str_has_prefix(arg, "--profile=")) {
      *out_profile_id = arg + strlen("--profile=");
      for (int j = i; j + 1 < *argc; ++j) {
        (*argv)[j] = (*argv)[j + 1];
      }
      (*argv)[*argc - 1] = NULL;
      *argc -= 1;
      return TRUE;
    }

    if (strcmp(arg, "--profile") == 0) {
      g_return_val_if_fail(i + 1 < *argc, FALSE);
      *out_profile_id = (*argv)[i + 1];
      for (int j = i; j + 2 < *argc; ++j) {
        (*argv)[j] = (*argv)[j + 2];
      }
      (*argv)[*argc - 2] = NULL;
      (*argv)[*argc - 1] = NULL;
      *argc -= 2;
      return TRUE;
    }
  }

  return FALSE;
}

void ggame_test_init_profile(int *argc, char ***argv, const char *default_profile_id) {
  const char *profile_id = default_profile_id;
  g_return_if_fail(argc != NULL);
  g_return_if_fail(argv != NULL);
  g_return_if_fail(default_profile_id != NULL);

  if (!ggame_test_take_profile_argument(argc, argv, &profile_id) &&
      g_getenv("GGAME_TEST_PROFILE") != NULL &&
      g_getenv("GGAME_TEST_PROFILE")[0] != '\0') {
    profile_id = g_getenv("GGAME_TEST_PROFILE");
  }

  const GGameAppProfile *profile = ggame_app_profile_lookup_by_id(profile_id);
  g_return_if_fail(profile != NULL);
  g_return_if_fail(ggame_app_profile_set_active(profile));
}

gboolean ggame_test_require_profile_kind(GGameAppKind kind) {
  const GGameAppProfile *profile = ggame_active_app_profile();
  g_return_val_if_fail(profile != NULL, FALSE);

  if (profile->kind == kind) {
    return TRUE;
  }

  g_autofree char *message = g_strdup_printf("Requires %s profile", ggame_test_profile_kind_name(kind));
  g_test_skip(message);
  return FALSE;
}
