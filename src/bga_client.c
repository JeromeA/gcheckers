#include "bga_client.h"

#include "sgf_metadata.h"

#include <curl/curl.h>
#include <errno.h>
#include <string.h>

static const char *bga_client_home_url = "https://en.boardgamearena.com/";
static const char *bga_client_login_url = "https://en.boardgamearena.com/account/auth/loginUserWithPassword.html";
static const char *bga_client_gamestats_url_prefix = "https://boardgamearena.com/gamestats";
static const char *bga_client_history_url_prefix =
    "https://boardgamearena.com/gamestats/gamestats/getGames.html";
static const char *bga_client_table_url_prefix = "https://boardgamearena.com/table";
static const char *bga_client_game_review_url_prefix = "https://boardgamearena.com/gamereview";
static const char *bga_client_archive_request_url_prefix =
    "https://boardgamearena.com/gamereview/gamereview/requestTableArchive.html";
static const char *bga_client_archive_logs_url_prefix =
    "https://boardgamearena.com/archive/archive/logs.html";
static const guint bga_client_archive_ready_poll_count = 5;
static const guint bga_client_archive_ready_poll_usec = 250000;
static const guint bga_client_history_default_page_batch_size = 10;

static const char *bga_client_json_previous_object_start(const char *body, const char *position);
static const char *bga_client_json_object_end(const char *object_start);

struct _BgaClientSession {
  CURL *curl;
  char *request_token;
};

typedef struct {
  GString *buffer;
} BgaCurlWriteContext;

static void bga_client_save_debug_response(const char *url, const char *body) {
  g_return_if_fail(url != NULL);

  if (!g_strstr_len(url, -1, "boardgamearena.com")) {
    return;
  }

  static gint response_counter = 0;
  gint response_id = g_atomic_int_add(&response_counter, 1) + 1;

  g_autofree char *sanitized_url = g_strdup(url);
  for (char *c = sanitized_url; c != NULL && *c != '\0'; c++) {
    gboolean keep = g_ascii_isalnum(*c) || *c == '.';
    if (!keep) {
      *c = '_';
    }
  }

  g_autofree char *path =
      g_strdup_printf("/tmp/gcheckers-bga-%04d-%s.txt", response_id, sanitized_url ? sanitized_url : "response");
  g_autoptr(GError) write_error = NULL;
  gboolean written = g_file_set_contents(path, body ? body : "", -1, &write_error);
  if (!written) {
    g_debug("Failed to save BGA response to %s: %s", path, write_error ? write_error->message : "unknown error");
  }
}

static GQuark bga_client_error_quark(void) {
  return g_quark_from_static_string("bga-client-error");
}

static gboolean bga_client_json_extract_string(const char *body, const char *key, char **out_value) {
  g_return_val_if_fail(body != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(out_value != NULL, FALSE);

  g_autofree char *pattern = g_strdup_printf("\"%s\"\\s*:\\s*\"([^\"]*)\"", key);
  g_autoptr(GRegex) regex = g_regex_new(pattern, G_REGEX_MULTILINE, 0, NULL);
  g_autoptr(GMatchInfo) info = NULL;
  if (!g_regex_match(regex, body, 0, &info)) {
    return FALSE;
  }

  g_autofree char *value = g_match_info_fetch(info, 1);
  if (value == NULL) {
    return FALSE;
  }

  g_free(*out_value);
  *out_value = g_steal_pointer(&value);
  return TRUE;
}

static gboolean bga_client_json_extract_int(const char *body, const char *key, int *out_value) {
  g_return_val_if_fail(body != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(out_value != NULL, FALSE);

  g_autofree char *pattern = g_strdup_printf("\"%s\"\\s*:\\s*(\"?)([0-9]+)\\1", key);
  g_autoptr(GRegex) regex = g_regex_new(pattern, G_REGEX_MULTILINE, 0, NULL);
  g_autoptr(GMatchInfo) info = NULL;
  if (!g_regex_match(regex, body, 0, &info)) {
    return FALSE;
  }

  g_autofree char *value = g_match_info_fetch(info, 2);
  if (value == NULL) {
    return FALSE;
  }

  errno = 0;
  char *end = NULL;
  gint64 parsed = g_ascii_strtoll(value, &end, 10);
  if (end == value || end == NULL || *end != '\0' || errno == ERANGE ||
      parsed < G_MININT || parsed > G_MAXINT) {
    return FALSE;
  }

  *out_value = (int)parsed;
  return TRUE;
}

static gboolean bga_client_json_extract_bool(const char *body, const char *key, gboolean *out_value) {
  g_return_val_if_fail(body != NULL, FALSE);
  g_return_val_if_fail(key != NULL, FALSE);
  g_return_val_if_fail(out_value != NULL, FALSE);

  g_autofree char *pattern = g_strdup_printf("\"%s\"\\s*:\\s*(true|false)", key);
  g_autoptr(GRegex) regex = g_regex_new(pattern, G_REGEX_MULTILINE, 0, NULL);
  g_autoptr(GMatchInfo) info = NULL;
  if (!g_regex_match(regex, body, 0, &info)) {
    return FALSE;
  }

  g_autofree char *value = g_match_info_fetch(info, 1);
  if (value == NULL) {
    return FALSE;
  }

  *out_value = g_strcmp0(value, "true") == 0;
  return TRUE;
}

static gboolean bga_client_text_contains_casefolded(const char *body, const char *needle) {
  g_return_val_if_fail(body != NULL, FALSE);
  g_return_val_if_fail(needle != NULL, FALSE);

  g_autofree char *folded_body = g_utf8_casefold(body, -1);
  g_autofree char *folded_needle = g_utf8_casefold(needle, -1);
  if (folded_body == NULL || folded_needle == NULL) {
    return FALSE;
  }

  return g_strstr_len(folded_body, -1, folded_needle) != NULL;
}

gboolean bga_client_archive_review_is_waiting_for_generation(const char *body) {
  g_return_val_if_fail(body != NULL, FALSE);

  return bga_client_text_contains_casefolded(body, "Searching for the game archive");
}

gboolean bga_client_archive_logs_error_needs_generation(const char *body) {
  g_return_val_if_fail(body != NULL, FALSE);

  int status = -1;
  if (!bga_client_json_extract_int(body, "status", &status) || status != 0) {
    return FALSE;
  }

  g_autofree char *error = NULL;
  if (!bga_client_json_extract_string(body, "error", &error)) {
    return FALSE;
  }

  return bga_client_text_contains_casefolded(error, "Cannot find gamenotifs log file");
}

static gboolean bga_client_response_has_status_zero_error(const char *body, char **out_message) {
  g_return_val_if_fail(body != NULL, FALSE);

  int status = -1;
  if (!bga_client_json_extract_int(body, "status", &status) || status != 0) {
    return FALSE;
  }

  g_autofree char *error = NULL;
  g_autofree char *exception = NULL;
  bga_client_json_extract_string(body, "error", &error);
  bga_client_json_extract_string(body, "exception", &exception);

  if (out_message != NULL) {
    if (error != NULL && exception != NULL) {
      *out_message = g_strdup_printf("%s (%s)", error, exception);
    } else {
      *out_message = g_strdup(error ? error : exception ? exception : "BoardGameArena returned status 0");
    }
  }

  return TRUE;
}

static char *bga_client_format_history_start_at(const char *start_unix_text) {
  g_return_val_if_fail(start_unix_text != NULL, g_strdup(""));

  char *end = NULL;
  gint64 unix_seconds = g_ascii_strtoll(start_unix_text, &end, 10);
  if (end == start_unix_text || (end != NULL && *end != '\0') || unix_seconds < 0) {
    return g_strdup("");
  }

  g_autoptr(GDateTime) timestamp = g_date_time_new_from_unix_utc(unix_seconds);
  if (timestamp == NULL) {
    return g_strdup("");
  }

  return g_date_time_format(timestamp, "%Y-%m-%d %H:%M");
}

static char *bga_client_format_sgf_date(const char *unix_text) {
  g_return_val_if_fail(unix_text != NULL, NULL);

  char *end = NULL;
  gint64 unix_seconds = g_ascii_strtoll(unix_text, &end, 10);
  if (end == unix_text || (end != NULL && *end != '\0') || unix_seconds < 0) {
    return NULL;
  }

  g_autoptr(GDateTime) timestamp = g_date_time_new_from_unix_utc(unix_seconds);
  if (timestamp == NULL) {
    return NULL;
  }

  return g_date_time_format(timestamp, "%Y-%m-%d");
}

static char *bga_client_strdup_stripped(const char *text) {
  g_autofree char *copy = g_strdup(text != NULL ? text : "");
  g_strstrip(copy);
  return g_steal_pointer(&copy);
}

static char *bga_client_sanitize_filename_part(const char *text) {
  g_return_val_if_fail(text != NULL, NULL);

  GString *sanitized = g_string_new(NULL);
  for (const char *c = text; *c != '\0'; c++) {
    if (g_ascii_isalnum(*c) || *c == '-' || *c == '_') {
      g_string_append_c(sanitized, *c);
    } else {
      g_string_append_c(sanitized, '_');
    }
  }

  if (sanitized->len == 0) {
    g_string_append(sanitized, "unknown");
  }

  return g_string_free(sanitized, FALSE);
}

#define BGA_CLIENT_HOMEWORLDS_SYSTEM_LIMIT 16

typedef struct {
  int id;
  int internal_index;
  char *name;
} BgaHomeworldsArchiveSystem;

typedef struct {
  int id;
  int system_id;
  int side;
  char color;
  guint size;
  gboolean is_ship;
} BgaHomeworldsArchivePyramid;

typedef struct {
  int id;
  char *name;
  int side;
} BgaHomeworldsArchivePlayer;

typedef struct {
  GPtrArray *systems;
  GPtrArray *pyramids;
  gboolean internal_system_used[BGA_CLIENT_HOMEWORLDS_SYSTEM_LIMIT];
} BgaHomeworldsArchivePosition;

typedef struct {
  GString *sgf;
  GPtrArray *steps;
  GPtrArray *players;
  char *table_id;
  char *date;
  char *winner;
  BgaHomeworldsArchivePosition committed;
  BgaHomeworldsArchivePosition working;
  gboolean has_pending_turn;
  int current_side;
  int next_setup_side;
  int pending_discovery_system_id;
  guint pending_sacrifice_actions;
  char pending_discovery_star[3];
} BgaHomeworldsArchiveState;

static void bga_homeworlds_archive_system_free(gpointer data) {
  BgaHomeworldsArchiveSystem *system = data;
  if (system == NULL) {
    return;
  }

  g_free(system->name);
  g_free(system);
}

static void bga_homeworlds_archive_pyramid_free(gpointer data) {
  g_free(data);
}

static void bga_homeworlds_archive_player_free(gpointer data) {
  BgaHomeworldsArchivePlayer *player = data;
  if (player == NULL) {
    return;
  }

  g_free(player->name);
  g_free(player);
}

static void bga_homeworlds_archive_position_init(BgaHomeworldsArchivePosition *position) {
  g_return_if_fail(position != NULL);

  position->systems = g_ptr_array_new_with_free_func(bga_homeworlds_archive_system_free);
  position->pyramids = g_ptr_array_new_with_free_func(bga_homeworlds_archive_pyramid_free);
  memset(position->internal_system_used, 0, sizeof(position->internal_system_used));
}

static void bga_homeworlds_archive_position_reset(BgaHomeworldsArchivePosition *position) {
  g_return_if_fail(position != NULL);
  g_return_if_fail(position->systems != NULL);
  g_return_if_fail(position->pyramids != NULL);

  g_ptr_array_set_size(position->systems, 0);
  g_ptr_array_set_size(position->pyramids, 0);
  memset(position->internal_system_used, 0, sizeof(position->internal_system_used));
}

static void bga_homeworlds_archive_position_clear(BgaHomeworldsArchivePosition *position) {
  g_return_if_fail(position != NULL);

  g_clear_pointer(&position->systems, g_ptr_array_unref);
  g_clear_pointer(&position->pyramids, g_ptr_array_unref);
  memset(position->internal_system_used, 0, sizeof(position->internal_system_used));
}

static void bga_homeworlds_archive_position_copy(BgaHomeworldsArchivePosition *destination,
                                                 const BgaHomeworldsArchivePosition *source) {
  g_return_if_fail(destination != NULL);
  g_return_if_fail(source != NULL);
  g_return_if_fail(source->systems != NULL);
  g_return_if_fail(source->pyramids != NULL);

  bga_homeworlds_archive_position_reset(destination);
  memcpy(destination->internal_system_used,
         source->internal_system_used,
         sizeof(destination->internal_system_used));

  for (guint i = 0; i < source->systems->len; ++i) {
    const BgaHomeworldsArchiveSystem *source_system = g_ptr_array_index(source->systems, i);
    BgaHomeworldsArchiveSystem *system = g_new0(BgaHomeworldsArchiveSystem, 1);
    system->id = source_system->id;
    system->internal_index = source_system->internal_index;
    system->name = g_strdup(source_system->name);
    g_ptr_array_add(destination->systems, system);
  }

  for (guint i = 0; i < source->pyramids->len; ++i) {
    const BgaHomeworldsArchivePyramid *source_pyramid = g_ptr_array_index(source->pyramids, i);
    BgaHomeworldsArchivePyramid *pyramid = g_new0(BgaHomeworldsArchivePyramid, 1);
    *pyramid = *source_pyramid;
    g_ptr_array_add(destination->pyramids, pyramid);
  }
}

static void bga_homeworlds_archive_state_init(BgaHomeworldsArchiveState *state) {
  g_return_if_fail(state != NULL);

  state->sgf = g_string_new(NULL);
  state->steps = g_ptr_array_new_with_free_func(g_free);
  state->players = g_ptr_array_new_with_free_func(bga_homeworlds_archive_player_free);
  bga_homeworlds_archive_position_init(&state->committed);
  bga_homeworlds_archive_position_init(&state->working);
  state->current_side = -1;
  state->next_setup_side = 0;
  state->pending_discovery_system_id = -1;
}

static void bga_homeworlds_archive_state_clear(BgaHomeworldsArchiveState *state) {
  g_return_if_fail(state != NULL);

  if (state->sgf != NULL) {
    g_string_free(state->sgf, TRUE);
    state->sgf = NULL;
  }
  g_clear_pointer(&state->steps, g_ptr_array_unref);
  g_clear_pointer(&state->players, g_ptr_array_unref);
  g_clear_pointer(&state->table_id, g_free);
  g_clear_pointer(&state->date, g_free);
  g_clear_pointer(&state->winner, g_free);
  bga_homeworlds_archive_position_clear(&state->committed);
  bga_homeworlds_archive_position_clear(&state->working);
}

static gboolean bga_homeworlds_archive_parse_pyramid(const char *text, char *out_color, guint *out_size) {
  g_return_val_if_fail(text != NULL, FALSE);
  g_return_val_if_fail(out_color != NULL, FALSE);
  g_return_val_if_fail(out_size != NULL, FALSE);

  if (strlen(text) != 2) {
    return FALSE;
  }

  char color = (char)g_ascii_tolower(text[0]);
  if (color != 'r' && color != 'y' && color != 'g' && color != 'b') {
    return FALSE;
  }
  if (text[1] < '1' || text[1] > '3') {
    return FALSE;
  }

  *out_color = color;
  *out_size = (guint)(text[1] - '0');
  return TRUE;
}

static char *bga_homeworlds_archive_format_pyramid(const char *text, gboolean star) {
  char color = '\0';
  guint size = 0;
  if (!bga_homeworlds_archive_parse_pyramid(text, &color, &size)) {
    return NULL;
  }

  return g_strdup_printf("%c%u", star ? (char)g_ascii_toupper(color) : color, size);
}

static gboolean bga_homeworlds_archive_color_from_bga_value(const char *value, char *out_color) {
  g_return_val_if_fail(value != NULL, FALSE);
  g_return_val_if_fail(out_color != NULL, FALSE);

  if (g_strcmp0(value, "1") == 0 || g_ascii_strcasecmp(value, "red") == 0) {
    *out_color = 'r';
    return TRUE;
  }
  if (g_strcmp0(value, "2") == 0 || g_ascii_strcasecmp(value, "yellow") == 0) {
    *out_color = 'y';
    return TRUE;
  }
  if (g_strcmp0(value, "3") == 0 || g_ascii_strcasecmp(value, "green") == 0) {
    *out_color = 'g';
    return TRUE;
  }
  if (g_strcmp0(value, "4") == 0 || g_ascii_strcasecmp(value, "blue") == 0) {
    *out_color = 'b';
    return TRUE;
  }

  return FALSE;
}

static BgaHomeworldsArchiveSystem *bga_homeworlds_archive_position_find_system_by_id(
    BgaHomeworldsArchivePosition *position,
    int system_id) {
  g_return_val_if_fail(position != NULL, NULL);
  g_return_val_if_fail(position->systems != NULL, NULL);

  for (guint i = 0; i < position->systems->len; ++i) {
    BgaHomeworldsArchiveSystem *system = g_ptr_array_index(position->systems, i);
    if (system->id == system_id) {
      return system;
    }
  }

  return NULL;
}

static BgaHomeworldsArchiveSystem *bga_homeworlds_archive_position_find_system_by_name(
    BgaHomeworldsArchivePosition *position,
    const char *name) {
  g_return_val_if_fail(position != NULL, NULL);
  g_return_val_if_fail(position->systems != NULL, NULL);

  if (name == NULL || name[0] == '\0') {
    return NULL;
  }

  for (guint i = 0; i < position->systems->len; ++i) {
    BgaHomeworldsArchiveSystem *system = g_ptr_array_index(position->systems, i);
    if (g_strcmp0(system->name, name) == 0) {
      return system;
    }
  }

  return NULL;
}

static BgaHomeworldsArchivePyramid *bga_homeworlds_archive_position_find_pyramid(
    BgaHomeworldsArchivePosition *position,
    int pyramid_id) {
  g_return_val_if_fail(position != NULL, NULL);
  g_return_val_if_fail(position->pyramids != NULL, NULL);

  for (guint i = 0; i < position->pyramids->len; ++i) {
    BgaHomeworldsArchivePyramid *pyramid = g_ptr_array_index(position->pyramids, i);
    if (pyramid->id == pyramid_id) {
      return pyramid;
    }
  }

  return NULL;
}

static int bga_homeworlds_archive_position_find_first_free_system_index(
    BgaHomeworldsArchivePosition *position) {
  g_return_val_if_fail(position != NULL, -1);

  for (int i = 2; i < BGA_CLIENT_HOMEWORLDS_SYSTEM_LIMIT; ++i) {
    if (!position->internal_system_used[i]) {
      return i;
    }
  }

  return -1;
}

static char *bga_homeworlds_archive_format_system_label(int internal_index) {
  if (internal_index == 0) {
    return g_strdup("H1");
  }
  if (internal_index == 1) {
    return g_strdup("H2");
  }
  if (internal_index >= 2 && internal_index < BGA_CLIENT_HOMEWORLDS_SYSTEM_LIMIT) {
    return g_strdup_printf("S%d", internal_index - 2);
  }

  return NULL;
}

static char *bga_homeworlds_archive_system_label_by_id(BgaHomeworldsArchivePosition *position,
                                                       int system_id) {
  BgaHomeworldsArchiveSystem *system =
      bga_homeworlds_archive_position_find_system_by_id(position, system_id);
  if (system == NULL) {
    return NULL;
  }

  return bga_homeworlds_archive_format_system_label(system->internal_index);
}

static gboolean bga_homeworlds_archive_position_add_system(BgaHomeworldsArchivePosition *position,
                                                           int system_id,
                                                           int internal_index,
                                                           const char *name,
                                                           GError **error) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(system_id > 0, FALSE);
  g_return_val_if_fail(internal_index >= 0, FALSE);
  g_return_val_if_fail(internal_index < BGA_CLIENT_HOMEWORLDS_SYSTEM_LIMIT, FALSE);

  if (position->internal_system_used[internal_index]) {
    BgaHomeworldsArchiveSystem *existing =
        bga_homeworlds_archive_position_find_system_by_id(position, system_id);
    if (existing == NULL || existing->internal_index != internal_index) {
      g_set_error(error,
                  bga_client_error_quark(),
                  22,
                  "BoardGameArena archive maps two systems to internal Homeworlds slot %d",
                  internal_index);
      return FALSE;
    }
  }

  BgaHomeworldsArchiveSystem *system =
      bga_homeworlds_archive_position_find_system_by_id(position, system_id);
  if (system == NULL) {
    system = g_new0(BgaHomeworldsArchiveSystem, 1);
    system->id = system_id;
    system->internal_index = internal_index;
    g_ptr_array_add(position->systems, system);
  }
  position->internal_system_used[internal_index] = TRUE;
  g_free(system->name);
  system->name = g_strdup(name != NULL ? name : "");
  return TRUE;
}

static void bga_homeworlds_archive_position_remove_pyramid_by_index(
    BgaHomeworldsArchivePosition *position,
    guint index) {
  g_return_if_fail(position != NULL);
  g_return_if_fail(position->pyramids != NULL);
  g_return_if_fail(index < position->pyramids->len);

  g_ptr_array_remove_index(position->pyramids, index);
}

static void bga_homeworlds_archive_position_remove_pyramid_by_id(
    BgaHomeworldsArchivePosition *position,
    int pyramid_id) {
  g_return_if_fail(position != NULL);
  g_return_if_fail(position->pyramids != NULL);

  for (guint i = 0; i < position->pyramids->len; ++i) {
    const BgaHomeworldsArchivePyramid *pyramid = g_ptr_array_index(position->pyramids, i);
    if (pyramid->id == pyramid_id) {
      bga_homeworlds_archive_position_remove_pyramid_by_index(position, i);
      return;
    }
  }
}

static void bga_homeworlds_archive_position_add_pyramid(BgaHomeworldsArchivePosition *position,
                                                        int pyramid_id,
                                                        int system_id,
                                                        int side,
                                                        const char *pyramid_text,
                                                        gboolean is_ship) {
  g_return_if_fail(position != NULL);
  g_return_if_fail(pyramid_text != NULL);

  char color = '\0';
  guint size = 0;
  if (!bga_homeworlds_archive_parse_pyramid(pyramid_text, &color, &size)) {
    g_debug("Ignoring malformed BGA Homeworlds pyramid text '%s'", pyramid_text);
    return;
  }

  bga_homeworlds_archive_position_remove_pyramid_by_id(position, pyramid_id);
  BgaHomeworldsArchivePyramid *pyramid = g_new0(BgaHomeworldsArchivePyramid, 1);
  pyramid->id = pyramid_id;
  pyramid->system_id = system_id;
  pyramid->side = side;
  pyramid->color = color;
  pyramid->size = size;
  pyramid->is_ship = is_ship;
  g_ptr_array_add(position->pyramids, pyramid);
}

static void bga_homeworlds_archive_position_remove_system(BgaHomeworldsArchivePosition *position,
                                                          int system_id) {
  g_return_if_fail(position != NULL);
  g_return_if_fail(position->systems != NULL);
  g_return_if_fail(position->pyramids != NULL);

  for (guint i = 0; i < position->systems->len; ++i) {
    const BgaHomeworldsArchiveSystem *system = g_ptr_array_index(position->systems, i);
    if (system->id == system_id) {
      if (system->internal_index >= 0 && system->internal_index < BGA_CLIENT_HOMEWORLDS_SYSTEM_LIMIT) {
        position->internal_system_used[system->internal_index] = FALSE;
      }
      g_ptr_array_remove_index(position->systems, i);
      break;
    }
  }

  for (guint i = 0; i < position->pyramids->len;) {
    const BgaHomeworldsArchivePyramid *pyramid = g_ptr_array_index(position->pyramids, i);
    if (pyramid->system_id == system_id) {
      bga_homeworlds_archive_position_remove_pyramid_by_index(position, i);
    } else {
      i++;
    }
  }
}

static void bga_homeworlds_archive_position_remove_color(BgaHomeworldsArchivePosition *position,
                                                         int system_id,
                                                         char color) {
  g_return_if_fail(position != NULL);
  g_return_if_fail(position->pyramids != NULL);

  for (guint i = 0; i < position->pyramids->len;) {
    const BgaHomeworldsArchivePyramid *pyramid = g_ptr_array_index(position->pyramids, i);
    if (pyramid->system_id == system_id && pyramid->color == color) {
      bga_homeworlds_archive_position_remove_pyramid_by_index(position, i);
    } else {
      i++;
    }
  }
}

static BgaHomeworldsArchivePyramid *bga_homeworlds_archive_position_find_attacker(
    BgaHomeworldsArchivePosition *position,
    int system_id,
    int side,
    guint target_size) {
  g_return_val_if_fail(position != NULL, NULL);
  g_return_val_if_fail(position->pyramids != NULL, NULL);

  BgaHomeworldsArchivePyramid *best = NULL;
  for (guint i = 0; i < position->pyramids->len; ++i) {
    BgaHomeworldsArchivePyramid *pyramid = g_ptr_array_index(position->pyramids, i);
    if (!pyramid->is_ship || pyramid->system_id != system_id || pyramid->side != side ||
        pyramid->size < target_size) {
      continue;
    }
    if (best == NULL || pyramid->size < best->size) {
      best = pyramid;
    }
  }

  return best;
}

static void bga_homeworlds_archive_set_player_side(BgaHomeworldsArchiveState *state,
                                                   int player_id,
                                                   const char *player_name,
                                                   int side) {
  g_return_if_fail(state != NULL);
  g_return_if_fail(state->players != NULL);
  g_return_if_fail(side == 0 || side == 1);

  for (guint i = 0; i < state->players->len; ++i) {
    BgaHomeworldsArchivePlayer *player = g_ptr_array_index(state->players, i);
    if ((player_id > 0 && player->id == player_id) ||
        (player_name != NULL && player_name[0] != '\0' && g_strcmp0(player->name, player_name) == 0)) {
      player->id = player_id;
      player->side = side;
      if (player_name != NULL && player_name[0] != '\0') {
        g_free(player->name);
        player->name = g_strdup(player_name);
      }
      return;
    }
  }

  BgaHomeworldsArchivePlayer *player = g_new0(BgaHomeworldsArchivePlayer, 1);
  player->id = player_id;
  player->name = g_strdup(player_name != NULL ? player_name : "");
  player->side = side;
  g_ptr_array_add(state->players, player);
}

static const char *bga_homeworlds_archive_player_name_for_side(BgaHomeworldsArchiveState *state, int side) {
  g_return_val_if_fail(state != NULL, NULL);
  g_return_val_if_fail(state->players != NULL, NULL);
  g_return_val_if_fail(side == 0 || side == 1, NULL);

  for (guint i = 0; i < state->players->len; i++) {
    BgaHomeworldsArchivePlayer *player = g_ptr_array_index(state->players, i);
    if (player != NULL && player->side == side) {
      return player->name;
    }
  }

  return NULL;
}

static char *bga_homeworlds_archive_extract_first_json_string(const char *body, const char *key) {
  g_return_val_if_fail(body != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);

  g_autofree char *pattern = g_strdup_printf("\"%s\"\\s*:\\s*\"([^\"]+)\"", key);
  g_autoptr(GRegex) regex = g_regex_new(pattern, G_REGEX_DOTALL, 0, NULL);
  g_autoptr(GMatchInfo) info = NULL;
  if (!g_regex_match(regex, body, 0, &info)) {
    return NULL;
  }

  return g_match_info_fetch(info, 1);
}

static char *bga_homeworlds_archive_extract_date(const char *body) {
  g_return_val_if_fail(body != NULL, NULL);

  g_autofree char *time_text = bga_homeworlds_archive_extract_first_json_string(body, "time");
  if (time_text == NULL) {
    return NULL;
  }

  return bga_client_format_sgf_date(time_text);
}

static char *bga_homeworlds_archive_extract_winner(const char *body) {
  g_return_val_if_fail(body != NULL, NULL);

  g_autoptr(GRegex) rank_regex = g_regex_new("\"rank\"\\s*:\\s*1\\b", G_REGEX_DOTALL, 0, NULL);
  g_autoptr(GMatchInfo) match_info = NULL;
  if (!g_regex_match(rank_regex, body, 0, &match_info)) {
    return NULL;
  }

  while (g_match_info_matches(match_info)) {
    int start_pos = -1;
    int end_pos = -1;
    if (!g_match_info_fetch_pos(match_info, 0, &start_pos, &end_pos) || start_pos < 0 || end_pos < 0) {
      break;
    }

    const char *object_start = bga_client_json_previous_object_start(body, body + start_pos);
    const char *object_end = object_start != NULL ? bga_client_json_object_end(object_start) : NULL;
    if (object_start != NULL && object_end != NULL && object_end > object_start) {
      g_autofree char *entry_body = g_strndup(object_start, (gsize)(object_end - object_start));
      gboolean tied = FALSE;
      g_autofree char *name = NULL;
      if ((!bga_client_json_extract_bool(entry_body, "tie", &tied) || !tied) &&
          bga_client_json_extract_string(entry_body, "name", &name) &&
          name != NULL && name[0] != '\0') {
        return g_steal_pointer(&name);
      }
    }

    if (!g_match_info_next(match_info, NULL)) {
      break;
    }
  }

  return NULL;
}

static int bga_homeworlds_archive_player_side_from_event(BgaHomeworldsArchiveState *state,
                                                         const char *event_body) {
  g_return_val_if_fail(state != NULL, -1);
  g_return_val_if_fail(event_body != NULL, -1);

  int player_id = 0;
  if (!bga_client_json_extract_int(event_body, "player_id", &player_id)) {
    bga_client_json_extract_int(event_body, "homeplayer_id", &player_id);
  }
  g_autofree char *player_name = NULL;
  bga_client_json_extract_string(event_body, "player_name", &player_name);

  for (guint i = 0; i < state->players->len; ++i) {
    BgaHomeworldsArchivePlayer *player = g_ptr_array_index(state->players, i);
    if ((player_id > 0 && player->id == player_id) ||
        (player_name != NULL && player_name[0] != '\0' && g_strcmp0(player->name, player_name) == 0)) {
      return player->side;
    }
  }

  return -1;
}

static void bga_homeworlds_archive_append_sgf_value(GString *sgf, const char *value) {
  g_return_if_fail(sgf != NULL);
  g_return_if_fail(value != NULL);

  for (const char *c = value; *c != '\0'; c++) {
    if (*c == '\\' || *c == ']') {
      g_string_append_c(sgf, '\\');
    }
    g_string_append_c(sgf, *c);
  }
}

static void bga_homeworlds_archive_append_node(BgaHomeworldsArchiveState *state,
                                               int side,
                                               const char *move_text) {
  g_return_if_fail(state != NULL);
  g_return_if_fail(state->sgf != NULL);
  g_return_if_fail(side == 0 || side == 1);
  g_return_if_fail(move_text != NULL);

  g_string_append_printf(state->sgf, ";%c[", side == 0 ? 'B' : 'W');
  bga_homeworlds_archive_append_sgf_value(state->sgf, move_text);
  g_string_append_c(state->sgf, ']');
}

static char *bga_homeworlds_archive_format_sgf(BgaHomeworldsArchiveState *state) {
  g_return_val_if_fail(state != NULL, NULL);
  g_return_val_if_fail(state->sgf != NULL, NULL);

  GString *sgf = g_string_new("(;AP[gcheckers]CA[UTF-8]FF[4]GM[40]");
  if (state->date != NULL && state->date[0] != '\0') {
    g_string_append(sgf, GGAME_SGF_PROP_DATE "[");
    bga_homeworlds_archive_append_sgf_value(sgf, state->date);
    g_string_append_c(sgf, ']');
  }
  if (state->table_id != NULL && state->table_id[0] != '\0') {
    g_string_append(sgf, GGAME_SGF_PROP_BGA_TABLE_ID "[");
    bga_homeworlds_archive_append_sgf_value(sgf, state->table_id);
    g_string_append_c(sgf, ']');
  }
  const char *player_1_name = bga_homeworlds_archive_player_name_for_side(state, 0);
  const char *player_2_name = bga_homeworlds_archive_player_name_for_side(state, 1);
  if (player_1_name != NULL && player_1_name[0] != '\0') {
    g_string_append(sgf, "PB[");
    bga_homeworlds_archive_append_sgf_value(sgf, player_1_name);
    g_string_append_c(sgf, ']');
  }
  if (player_2_name != NULL && player_2_name[0] != '\0') {
    g_string_append(sgf, "PW[");
    bga_homeworlds_archive_append_sgf_value(sgf, player_2_name);
    g_string_append_c(sgf, ']');
  }
  if (state->winner != NULL && state->winner[0] != '\0') {
    g_string_append(sgf, GGAME_SGF_PROP_RESULT "[");
    bga_homeworlds_archive_append_sgf_value(sgf, state->winner);
    g_string_append_c(sgf, ']');
  }
  g_string_append(sgf, state->sgf->str);
  g_string_append_c(sgf, ')');
  return g_string_free(sgf, FALSE);
}

static gboolean bga_homeworlds_archive_begin_turn(BgaHomeworldsArchiveState *state,
                                                  int side,
                                                  GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);

  if (side != 0 && side != 1) {
    g_set_error(error, bga_client_error_quark(), 23, "Unable to map BoardGameArena player to Homeworlds side");
    return FALSE;
  }

  if (!state->has_pending_turn) {
    bga_homeworlds_archive_position_copy(&state->working, &state->committed);
    g_ptr_array_set_size(state->steps, 0);
    state->has_pending_turn = TRUE;
    state->current_side = side;
    state->pending_discovery_system_id = -1;
    state->pending_sacrifice_actions = 0;
    state->pending_discovery_star[0] = '\0';
    return TRUE;
  }

  if (state->current_side != side) {
    g_set_error(error,
                bga_client_error_quark(),
                24,
                "BoardGameArena archive changed active player before finishing a Homeworlds move");
    return FALSE;
  }

  return TRUE;
}

static void bga_homeworlds_archive_discard_pending_turn(BgaHomeworldsArchiveState *state) {
  g_return_if_fail(state != NULL);

  if (!state->has_pending_turn) {
    return;
  }

  bga_homeworlds_archive_position_copy(&state->working, &state->committed);
  g_ptr_array_set_size(state->steps, 0);
  state->has_pending_turn = FALSE;
  state->current_side = -1;
  state->pending_discovery_system_id = -1;
  state->pending_sacrifice_actions = 0;
  state->pending_discovery_star[0] = '\0';
}

static void bga_homeworlds_archive_consume_forced_action(BgaHomeworldsArchiveState *state) {
  g_return_if_fail(state != NULL);

  if (state->pending_sacrifice_actions > 0) {
    state->pending_sacrifice_actions--;
  }
}

static gboolean bga_homeworlds_archive_finalize_pending_turn(BgaHomeworldsArchiveState *state,
                                                             GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);

  if (!state->has_pending_turn) {
    return TRUE;
  }

  if (state->steps->len == 0) {
    g_ptr_array_add(state->steps, g_strdup("pass"));
  }
  while (state->pending_sacrifice_actions > 0) {
    g_ptr_array_add(state->steps, g_strdup("pass"));
    state->pending_sacrifice_actions--;
  }

  GString *move = g_string_new(NULL);
  for (guint i = 0; i < state->steps->len; ++i) {
    if (i > 0) {
      g_string_append_c(move, ' ');
    }
    g_string_append(move, g_ptr_array_index(state->steps, i));
  }

  if (move->len == 0) {
    g_string_free(move, TRUE);
    g_set_error(error, bga_client_error_quark(), 25, "BoardGameArena archive produced an empty Homeworlds move");
    return FALSE;
  }

  bga_homeworlds_archive_append_node(state, state->current_side, move->str);
  g_string_free(move, TRUE);
  bga_homeworlds_archive_position_copy(&state->committed, &state->working);
  g_ptr_array_set_size(state->steps, 0);
  state->has_pending_turn = FALSE;
  state->current_side = -1;
  state->pending_discovery_system_id = -1;
  state->pending_sacrifice_actions = 0;
  state->pending_discovery_star[0] = '\0';
  return TRUE;
}

static gboolean bga_homeworlds_archive_event_get_system_id(BgaHomeworldsArchivePosition *position,
                                                           const char *event_body,
                                                           int *out_system_id) {
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(event_body != NULL, FALSE);
  g_return_val_if_fail(out_system_id != NULL, FALSE);

  if (bga_client_json_extract_int(event_body, "system_id", out_system_id)) {
    return TRUE;
  }

  g_autofree char *system_name = NULL;
  if (!bga_client_json_extract_string(event_body, "system_name", &system_name)) {
    bga_client_json_extract_string(event_body, "old_system_name", &system_name);
  }
  BgaHomeworldsArchiveSystem *system =
      bga_homeworlds_archive_position_find_system_by_name(position, system_name);
  if (system == NULL) {
    return FALSE;
  }

  *out_system_id = system->id;
  return TRUE;
}

static gboolean bga_homeworlds_archive_process_create(BgaHomeworldsArchiveState *state,
                                                      const char *event_body,
                                                      GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(event_body != NULL, FALSE);

  if (state->next_setup_side > 1) {
    g_set_error(error, bga_client_error_quark(), 26, "BoardGameArena archive contains too many Homeworlds setups");
    return FALSE;
  }

  int side = state->next_setup_side;
  int system_id = 0;
  int homeplayer_id = 0;
  int star1_id = 0;
  int star2_id = 0;
  int ship_id = 0;
  g_autofree char *player_name = NULL;
  g_autofree char *system_name = NULL;
  g_autofree char *star1_text = NULL;
  g_autofree char *star2_text = NULL;
  g_autofree char *ship_text = NULL;
  if (!bga_client_json_extract_int(event_body, "system_id", &system_id) ||
      !bga_client_json_extract_int(event_body, "homeplayer_id", &homeplayer_id) ||
      !bga_client_json_extract_int(event_body, "star1_id", &star1_id) ||
      !bga_client_json_extract_int(event_body, "star2_id", &star2_id) ||
      !bga_client_json_extract_int(event_body, "ship_id", &ship_id) ||
      !bga_client_json_extract_string(event_body, "player_name", &player_name) ||
      !bga_client_json_extract_string(event_body, "system_name", &system_name) ||
      !bga_client_json_extract_string(event_body, "star1_str", &star1_text) ||
      !bga_client_json_extract_string(event_body, "star2_str", &star2_text) ||
      !bga_client_json_extract_string(event_body, "ship_str", &ship_text)) {
    g_set_error(error, bga_client_error_quark(), 27, "Unable to parse BoardGameArena Homeworlds setup event");
    return FALSE;
  }

  g_autofree char *star1 = bga_homeworlds_archive_format_pyramid(star1_text, TRUE);
  g_autofree char *star2 = bga_homeworlds_archive_format_pyramid(star2_text, TRUE);
  g_autofree char *ship = bga_homeworlds_archive_format_pyramid(ship_text, FALSE);
  if (star1 == NULL || star2 == NULL || ship == NULL) {
    g_set_error(error, bga_client_error_quark(), 28, "BoardGameArena Homeworlds setup contains malformed pyramids");
    return FALSE;
  }

  bga_homeworlds_archive_set_player_side(state, homeplayer_id, player_name, side);
  if (!bga_homeworlds_archive_position_add_system(&state->committed, system_id, side, system_name, error)) {
    return FALSE;
  }
  bga_homeworlds_archive_position_add_pyramid(&state->committed, star1_id, system_id, -1, star1_text, FALSE);
  bga_homeworlds_archive_position_add_pyramid(&state->committed, star2_id, system_id, -1, star2_text, FALSE);
  bga_homeworlds_archive_position_add_pyramid(&state->committed, ship_id, system_id, side, ship_text, TRUE);

  g_autofree char *move_text = g_strdup_printf("%s%s%s", star1, star2, ship);
  bga_homeworlds_archive_append_node(state, side, move_text);
  state->next_setup_side++;
  return TRUE;
}

static gboolean bga_homeworlds_archive_process_build(BgaHomeworldsArchiveState *state,
                                                     const char *event_body,
                                                     GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(event_body != NULL, FALSE);

  int side = bga_homeworlds_archive_player_side_from_event(state, event_body);
  if (!bga_homeworlds_archive_begin_turn(state, side, error)) {
    return FALSE;
  }

  int system_id = 0;
  int ship_id = 0;
  g_autofree char *ship_text = NULL;
  if (!bga_homeworlds_archive_event_get_system_id(&state->working, event_body, &system_id) ||
      !bga_client_json_extract_int(event_body, "ship_id", &ship_id) ||
      !bga_client_json_extract_string(event_body, "ship_str", &ship_text)) {
    g_set_error(error, bga_client_error_quark(), 29, "Unable to parse BoardGameArena Homeworlds build event");
    return FALSE;
  }

  char color = '\0';
  guint size = 0;
  if (!bga_homeworlds_archive_parse_pyramid(ship_text, &color, &size)) {
    g_set_error(error, bga_client_error_quark(), 30, "BoardGameArena Homeworlds build has malformed ship");
    return FALSE;
  }

  g_autofree char *system_label = bga_homeworlds_archive_system_label_by_id(&state->working, system_id);
  if (system_label == NULL) {
    g_set_error(error, bga_client_error_quark(), 31, "BoardGameArena Homeworlds build references an unknown system");
    return FALSE;
  }

  g_ptr_array_add(state->steps, g_strdup_printf("%s%c+", system_label, color));
  bga_homeworlds_archive_position_add_pyramid(&state->working, ship_id, system_id, side, ship_text, TRUE);
  bga_homeworlds_archive_consume_forced_action(state);
  (void)size;
  return TRUE;
}

static gboolean bga_homeworlds_archive_process_trade(BgaHomeworldsArchiveState *state,
                                                     const char *event_body,
                                                     GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(event_body != NULL, FALSE);

  int side = bga_homeworlds_archive_player_side_from_event(state, event_body);
  if (!bga_homeworlds_archive_begin_turn(state, side, error)) {
    return FALSE;
  }

  int old_ship_id = 0;
  int new_ship_id = 0;
  g_autofree char *old_ship_text = NULL;
  g_autofree char *new_ship_text = NULL;
  if (!bga_client_json_extract_int(event_body, "old_ship_id", &old_ship_id) ||
      !bga_client_json_extract_int(event_body, "new_ship_id", &new_ship_id) ||
      !bga_client_json_extract_string(event_body, "old_ship_str", &old_ship_text) ||
      !bga_client_json_extract_string(event_body, "new_ship_str", &new_ship_text)) {
    g_set_error(error, bga_client_error_quark(), 32, "Unable to parse BoardGameArena Homeworlds trade event");
    return FALSE;
  }

  BgaHomeworldsArchivePyramid *old_ship_state =
      bga_homeworlds_archive_position_find_pyramid(&state->working, old_ship_id);
  if (old_ship_state == NULL) {
    g_set_error(error, bga_client_error_quark(), 33, "BoardGameArena Homeworlds trade references an unknown ship");
    return FALSE;
  }
  int system_id = old_ship_state->system_id;

  char new_color = '\0';
  guint new_size = 0;
  if (!bga_homeworlds_archive_parse_pyramid(new_ship_text, &new_color, &new_size)) {
    g_set_error(error, bga_client_error_quark(), 34, "BoardGameArena Homeworlds trade has malformed new ship");
    return FALSE;
  }

  g_autofree char *system_label =
      bga_homeworlds_archive_system_label_by_id(&state->working, system_id);
  g_autofree char *old_ship_label = bga_homeworlds_archive_format_pyramid(old_ship_text, FALSE);
  if (system_label == NULL || old_ship_label == NULL) {
    g_set_error(error, bga_client_error_quark(), 35, "BoardGameArena Homeworlds trade cannot be formatted");
    return FALSE;
  }

  g_ptr_array_add(state->steps, g_strdup_printf("%s%s=%c", system_label, old_ship_label, new_color));
  bga_homeworlds_archive_position_remove_pyramid_by_id(&state->working, old_ship_id);
  bga_homeworlds_archive_position_add_pyramid(&state->working, new_ship_id, system_id, side, new_ship_text, TRUE);
  bga_homeworlds_archive_consume_forced_action(state);
  (void)new_size;
  return TRUE;
}

static gboolean bga_homeworlds_archive_process_discover(BgaHomeworldsArchiveState *state,
                                                        const char *event_body,
                                                        GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(event_body != NULL, FALSE);

  int side = bga_homeworlds_archive_player_side_from_event(state, event_body);
  if (!bga_homeworlds_archive_begin_turn(state, side, error)) {
    return FALSE;
  }

  int system_id = 0;
  int star_id = 0;
  g_autofree char *system_name = NULL;
  g_autofree char *star_text = NULL;
  if (!bga_client_json_extract_int(event_body, "system_id", &system_id) ||
      !bga_client_json_extract_int(event_body, "star_id", &star_id) ||
      !bga_client_json_extract_string(event_body, "system_name", &system_name) ||
      !bga_client_json_extract_string(event_body, "star_str", &star_text)) {
    g_set_error(error, bga_client_error_quark(), 36, "Unable to parse BoardGameArena Homeworlds discover event");
    return FALSE;
  }

  int internal_index = bga_homeworlds_archive_position_find_first_free_system_index(&state->working);
  if (internal_index < 0) {
    g_set_error(error, bga_client_error_quark(), 37, "BoardGameArena Homeworlds archive exceeds system slots");
    return FALSE;
  }
  if (!bga_homeworlds_archive_position_add_system(&state->working,
                                                  system_id,
                                                  internal_index,
                                                  system_name,
                                                  error)) {
    return FALSE;
  }
  bga_homeworlds_archive_position_add_pyramid(&state->working, star_id, system_id, -1, star_text, FALSE);

  g_autofree char *star = bga_homeworlds_archive_format_pyramid(star_text, TRUE);
  if (star == NULL) {
    g_set_error(error, bga_client_error_quark(), 38, "BoardGameArena Homeworlds discover has malformed star");
    return FALSE;
  }
  g_strlcpy(state->pending_discovery_star, star, sizeof(state->pending_discovery_star));
  state->pending_discovery_system_id = system_id;
  return TRUE;
}

static gboolean bga_homeworlds_archive_process_move(BgaHomeworldsArchiveState *state,
                                                    const char *event_body,
                                                    GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(event_body != NULL, FALSE);

  int side = bga_homeworlds_archive_player_side_from_event(state, event_body);
  if (!bga_homeworlds_archive_begin_turn(state, side, error)) {
    return FALSE;
  }

  int destination_system_id = 0;
  int ship_id = 0;
  g_autofree char *ship_text = NULL;
  if (!bga_client_json_extract_int(event_body, "system_id", &destination_system_id) ||
      !bga_client_json_extract_int(event_body, "ship_id", &ship_id) ||
      !bga_client_json_extract_string(event_body, "ship_str", &ship_text)) {
    g_set_error(error, bga_client_error_quark(), 39, "Unable to parse BoardGameArena Homeworlds move event");
    return FALSE;
  }

  BgaHomeworldsArchivePyramid *ship =
      bga_homeworlds_archive_position_find_pyramid(&state->working, ship_id);
  if (ship == NULL) {
    g_set_error(error, bga_client_error_quark(), 40, "BoardGameArena Homeworlds move references an unknown ship");
    return FALSE;
  }

  g_autofree char *source_label = bga_homeworlds_archive_system_label_by_id(&state->working, ship->system_id);
  g_autofree char *destination_label =
      bga_homeworlds_archive_system_label_by_id(&state->working, destination_system_id);
  g_autofree char *ship_label = bga_homeworlds_archive_format_pyramid(ship_text, FALSE);
  if (source_label == NULL || destination_label == NULL || ship_label == NULL) {
    g_set_error(error, bga_client_error_quark(), 41, "BoardGameArena Homeworlds move cannot be formatted");
    return FALSE;
  }

  if (state->pending_discovery_system_id == destination_system_id) {
    g_ptr_array_add(state->steps,
                    g_strdup_printf("%s%s>%s(%s)",
                                    source_label,
                                    ship_label,
                                    destination_label,
                                    state->pending_discovery_star));
    state->pending_discovery_system_id = -1;
    state->pending_discovery_star[0] = '\0';
  } else {
    g_ptr_array_add(state->steps, g_strdup_printf("%s%s>%s", source_label, ship_label, destination_label));
  }

  ship->system_id = destination_system_id;
  bga_homeworlds_archive_consume_forced_action(state);
  return TRUE;
}

static gboolean bga_homeworlds_archive_process_sacrifice(BgaHomeworldsArchiveState *state,
                                                         const char *event_body,
                                                         GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(event_body != NULL, FALSE);

  int side = bga_homeworlds_archive_player_side_from_event(state, event_body);
  if (!bga_homeworlds_archive_begin_turn(state, side, error)) {
    return FALSE;
  }

  int ship_id = 0;
  g_autofree char *ship_text = NULL;
  if (!bga_client_json_extract_int(event_body, "ship_id", &ship_id) ||
      !bga_client_json_extract_string(event_body, "ship_str", &ship_text)) {
    g_set_error(error, bga_client_error_quark(), 42, "Unable to parse BoardGameArena Homeworlds sacrifice event");
    return FALSE;
  }

  BgaHomeworldsArchivePyramid *ship =
      bga_homeworlds_archive_position_find_pyramid(&state->working, ship_id);
  if (ship == NULL) {
    g_set_error(error, bga_client_error_quark(), 43, "BoardGameArena Homeworlds sacrifice references an unknown ship");
    return FALSE;
  }

  g_autofree char *system_label = bga_homeworlds_archive_system_label_by_id(&state->working, ship->system_id);
  g_autofree char *ship_label = bga_homeworlds_archive_format_pyramid(ship_text, FALSE);
  if (system_label == NULL || ship_label == NULL) {
    g_set_error(error, bga_client_error_quark(), 44, "BoardGameArena Homeworlds sacrifice cannot be formatted");
    return FALSE;
  }

  g_ptr_array_add(state->steps, g_strdup_printf("%s%s-", system_label, ship_label));
  state->pending_sacrifice_actions = ship->size;
  bga_homeworlds_archive_position_remove_pyramid_by_id(&state->working, ship_id);
  return TRUE;
}

static gboolean bga_homeworlds_archive_process_capture(BgaHomeworldsArchiveState *state,
                                                       const char *event_body,
                                                       GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(event_body != NULL, FALSE);

  int side = bga_homeworlds_archive_player_side_from_event(state, event_body);
  if (!bga_homeworlds_archive_begin_turn(state, side, error)) {
    return FALSE;
  }

  int target_id = 0;
  g_autofree char *target_text = NULL;
  if (!bga_client_json_extract_int(event_body, "target_id", &target_id) ||
      !bga_client_json_extract_string(event_body, "target_str", &target_text)) {
    g_set_error(error, bga_client_error_quark(), 45, "Unable to parse BoardGameArena Homeworlds capture event");
    return FALSE;
  }

  BgaHomeworldsArchivePyramid *target =
      bga_homeworlds_archive_position_find_pyramid(&state->working, target_id);
  if (target == NULL) {
    g_set_error(error, bga_client_error_quark(), 46, "BoardGameArena Homeworlds capture references an unknown ship");
    return FALSE;
  }

  char target_color = '\0';
  guint target_size = 0;
  if (!bga_homeworlds_archive_parse_pyramid(target_text, &target_color, &target_size)) {
    g_set_error(error, bga_client_error_quark(), 47, "BoardGameArena Homeworlds capture has malformed target");
    return FALSE;
  }
  BgaHomeworldsArchivePyramid *attacker =
      bga_homeworlds_archive_position_find_attacker(&state->working, target->system_id, side, target_size);
  if (attacker == NULL) {
    g_set_error(error, bga_client_error_quark(), 48, "BoardGameArena Homeworlds capture has no legal attacker");
    return FALSE;
  }

  g_autofree char *system_label = bga_homeworlds_archive_system_label_by_id(&state->working, target->system_id);
  g_autofree char *attacker_text = g_strdup_printf("%c%u", attacker->color, attacker->size);
  g_autofree char *target_label = bga_homeworlds_archive_format_pyramid(target_text, FALSE);
  if (system_label == NULL || target_label == NULL) {
    g_set_error(error, bga_client_error_quark(), 49, "BoardGameArena Homeworlds capture cannot be formatted");
    return FALSE;
  }

  g_ptr_array_add(state->steps, g_strdup_printf("%s%sx%s", system_label, attacker_text, target_label));
  target->side = side;
  bga_homeworlds_archive_consume_forced_action(state);
  (void)target_color;
  return TRUE;
}

static gboolean bga_homeworlds_archive_process_catastrophe(BgaHomeworldsArchiveState *state,
                                                           const char *event_body,
                                                           GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(event_body != NULL, FALSE);

  int side = bga_homeworlds_archive_player_side_from_event(state, event_body);
  if (!bga_homeworlds_archive_begin_turn(state, side, error)) {
    return FALSE;
  }

  int system_id = 0;
  g_autofree char *color_value = NULL;
  g_autofree char *color_name = NULL;
  if (!bga_homeworlds_archive_event_get_system_id(&state->working, event_body, &system_id)) {
    bga_client_json_extract_string(event_body, "color_name", &color_name);
  }
  if (!bga_client_json_extract_string(event_body, "color", &color_value)) {
    int color_number = 0;
    if (bga_client_json_extract_int(event_body, "color", &color_number)) {
      color_value = g_strdup_printf("%d", color_number);
    } else {
      bga_client_json_extract_string(event_body, "color_name", &color_name);
    }
  }
  if (color_value == NULL && color_name != NULL) {
    color_value = g_steal_pointer(&color_name);
  }
  if (system_id <= 0 || color_value == NULL) {
    g_set_error(error, bga_client_error_quark(), 50, "Unable to parse BoardGameArena Homeworlds catastrophe event");
    return FALSE;
  }

  char color = '\0';
  if (!bga_homeworlds_archive_color_from_bga_value(color_value, &color)) {
    g_set_error(error, bga_client_error_quark(), 51, "BoardGameArena Homeworlds catastrophe has unknown color");
    return FALSE;
  }

  g_autofree char *system_label = bga_homeworlds_archive_system_label_by_id(&state->working, system_id);
  if (system_label == NULL) {
    g_set_error(error, bga_client_error_quark(), 52, "BoardGameArena Homeworlds catastrophe references unknown system");
    return FALSE;
  }

  g_ptr_array_add(state->steps, g_strdup_printf("%s%c!", system_label, color));
  bga_homeworlds_archive_position_remove_color(&state->working, system_id, color);
  return TRUE;
}

static gboolean bga_homeworlds_archive_process_fade(BgaHomeworldsArchiveState *state,
                                                    const char *event_body) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(event_body != NULL, FALSE);

  BgaHomeworldsArchivePosition *position = state->has_pending_turn ? &state->working : &state->committed;
  int system_id = 0;
  if (!bga_homeworlds_archive_event_get_system_id(position, event_body, &system_id)) {
    return TRUE;
  }

  bga_homeworlds_archive_position_remove_system(position, system_id);
  return TRUE;
}

static gboolean bga_homeworlds_archive_process_pass(BgaHomeworldsArchiveState *state,
                                                    const char *event_body,
                                                    GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(event_body != NULL, FALSE);

  int side = bga_homeworlds_archive_player_side_from_event(state, event_body);
  if (!state->has_pending_turn && !bga_homeworlds_archive_begin_turn(state, side, error)) {
    return FALSE;
  }

  return bga_homeworlds_archive_finalize_pending_turn(state, error);
}

static gboolean bga_homeworlds_archive_process_event(BgaHomeworldsArchiveState *state,
                                                     const char *event_type,
                                                     const char *event_body,
                                                     GError **error) {
  g_return_val_if_fail(state != NULL, FALSE);
  g_return_val_if_fail(event_type != NULL, FALSE);
  g_return_val_if_fail(event_body != NULL, FALSE);

  if (g_strcmp0(event_type, "notif_create") == 0) {
    return bga_homeworlds_archive_process_create(state, event_body, error);
  }
  if (g_strcmp0(event_type, "notif_build") == 0) {
    return bga_homeworlds_archive_process_build(state, event_body, error);
  }
  if (g_strcmp0(event_type, "notif_trade") == 0) {
    return bga_homeworlds_archive_process_trade(state, event_body, error);
  }
  if (g_strcmp0(event_type, "notif_discover") == 0) {
    return bga_homeworlds_archive_process_discover(state, event_body, error);
  }
  if (g_strcmp0(event_type, "notif_move") == 0) {
    return bga_homeworlds_archive_process_move(state, event_body, error);
  }
  if (g_strcmp0(event_type, "notif_sacrifice") == 0) {
    return bga_homeworlds_archive_process_sacrifice(state, event_body, error);
  }
  if (g_strcmp0(event_type, "notif_capture") == 0) {
    return bga_homeworlds_archive_process_capture(state, event_body, error);
  }
  if (g_strcmp0(event_type, "notif_catastrophe") == 0) {
    return bga_homeworlds_archive_process_catastrophe(state, event_body, error);
  }
  if (g_strcmp0(event_type, "notif_fade") == 0) {
    return bga_homeworlds_archive_process_fade(state, event_body);
  }
  if (g_strcmp0(event_type, "notif_pass") == 0) {
    return bga_homeworlds_archive_process_pass(state, event_body, error);
  }
  if (g_strcmp0(event_type, "notif_restart") == 0) {
    bga_homeworlds_archive_discard_pending_turn(state);
    return TRUE;
  }

  return TRUE;
}

static const char *bga_client_json_previous_object_start(const char *body, const char *position) {
  g_return_val_if_fail(body != NULL, NULL);
  g_return_val_if_fail(position != NULL, NULL);

  for (const char *cursor = position; cursor >= body; cursor--) {
    if (*cursor == '{') {
      return cursor;
    }
    if (cursor == body) {
      break;
    }
  }

  return NULL;
}

static const char *bga_client_json_object_end(const char *object_start) {
  g_return_val_if_fail(object_start != NULL, NULL);
  g_return_val_if_fail(*object_start == '{', NULL);

  guint depth = 0;
  gboolean in_string = FALSE;
  gboolean escaped = FALSE;
  for (const char *cursor = object_start; *cursor != '\0'; cursor++) {
    if (in_string) {
      if (escaped) {
        escaped = FALSE;
      } else if (*cursor == '\\') {
        escaped = TRUE;
      } else if (*cursor == '"') {
        in_string = FALSE;
      }
      continue;
    }

    if (*cursor == '"') {
      in_string = TRUE;
    } else if (*cursor == '{') {
      depth++;
    } else if (*cursor == '}') {
      depth--;
      if (depth == 0) {
        return cursor + 1;
      }
    }
  }

  return NULL;
}

static gboolean bga_client_global_init(void) {
  static gsize initialized = 0;
  static gboolean init_ok = FALSE;

  if (g_once_init_enter(&initialized)) {
    init_ok = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    g_once_init_leave(&initialized, 1);
  }

  return init_ok;
}

static size_t bga_client_write_cb(void *contents, size_t size, size_t nmemb, void *user_data) {
  BgaCurlWriteContext *ctx = user_data;
  g_return_val_if_fail(ctx != NULL, 0);
  g_return_val_if_fail(ctx->buffer != NULL, 0);
  g_return_val_if_fail(contents != NULL || size == 0 || nmemb == 0, 0);

  if (size != 0 && nmemb > G_MAXSIZE / size) {
    return 0;
  }
  size_t bytes = size * nmemb;
  if (bytes == 0) {
    return 0;
  }
  if (bytes > (size_t)G_MAXSSIZE) {
    return 0;
  }

  g_string_append_len(ctx->buffer, contents, (gssize)bytes);
  return bytes;
}

static gboolean bga_client_http_request(CURL *curl,
                                        const char *url,
                                        const char *post_fields,
                                        struct curl_slist *headers,
                                        BgaHttpResponse *out_response,
                                        GError **error) {
  g_return_val_if_fail(curl != NULL, FALSE);
  g_return_val_if_fail(url != NULL, FALSE);
  g_return_val_if_fail(out_response != NULL, FALSE);

  g_autoptr(GString) response_buffer = g_string_new(NULL);
  BgaCurlWriteContext write_ctx = {
    .buffer = response_buffer,
  };

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, bga_client_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_ctx);
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  if (post_fields != NULL) {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 0L);
  } else {
    curl_easy_setopt(curl, CURLOPT_POST, 0L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, NULL);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  }

  CURLcode rc = curl_easy_perform(curl);
  if (rc != CURLE_OK) {
    g_set_error(error, bga_client_error_quark(), 1, "Curl request failed: %s", curl_easy_strerror(rc));
    return FALSE;
  }

  long status = 0;
  rc = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  if (rc != CURLE_OK) {
    g_set_error(error, bga_client_error_quark(), 2, "Failed to read HTTP status: %s", curl_easy_strerror(rc));
    return FALSE;
  }

  g_free(out_response->body);
  out_response->body = g_string_free(g_steal_pointer(&response_buffer), FALSE);
  out_response->http_status = status;
  bga_client_save_debug_response(url, out_response->body);
  return TRUE;
}

static gboolean bga_client_http_status_is_success(const BgaHttpResponse *response,
                                                  const char *description,
                                                  int error_code,
                                                  GError **error) {
  g_return_val_if_fail(response != NULL, FALSE);
  g_return_val_if_fail(description != NULL, FALSE);

  if (response->http_status >= 200 && response->http_status < 300) {
    return TRUE;
  }

  g_set_error(error,
              bga_client_error_quark(),
              error_code,
              "%s failed with HTTP %ld",
              description,
              response->http_status);
  return FALSE;
}

static gint64 bga_client_dojo_prevent_cache(void) {
  return g_get_real_time() / 1000;
}

static void bga_client_session_update_request_token_from_body(BgaClientSession *session,
                                                              const char *body,
                                                              const char *description) {
  g_return_if_fail(session != NULL);
  g_return_if_fail(body != NULL);
  g_return_if_fail(description != NULL);

  g_autofree char *request_token = NULL;
  g_autoptr(GError) token_error = NULL;
  if (bga_client_extract_request_token(body, &request_token, &token_error)) {
    g_free(session->request_token);
    session->request_token = g_steal_pointer(&request_token);
  } else {
    g_debug("Unable to extract requestToken from %s response: %s",
            description,
            token_error ? token_error->message : "unknown error");
  }
}

static struct curl_slist *bga_client_session_build_xhr_headers(BgaClientSession *session,
                                                               const char *referer_url,
                                                               gboolean json_response) {
  g_return_val_if_fail(session != NULL, NULL);

  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "X-Requested-With: XMLHttpRequest");
  if (json_response) {
    headers = curl_slist_append(headers, "Accept: application/json, text/javascript, */*; q=0.01");
  } else {
    headers = curl_slist_append(headers, "Accept: text/html, */*; q=0.01");
  }

  if (referer_url != NULL && referer_url[0] != '\0') {
    g_autofree char *referer_header = g_strdup_printf("Referer: %s", referer_url);
    headers = curl_slist_append(headers, referer_header);
  }
  if (session->request_token != NULL && session->request_token[0] != '\0') {
    g_autofree char *request_token_header = g_strdup_printf("X-Request-Token: %s", session->request_token);
    headers = curl_slist_append(headers, request_token_header);
  }

  return headers;
}

BgaClientSession *bga_client_session_new(GError **error) {
  if (!bga_client_global_init()) {
    g_set_error(error, bga_client_error_quark(), 5, "Failed to initialize libcurl");
    return NULL;
  }

  BgaClientSession *session = g_new0(BgaClientSession, 1);
  session->curl = curl_easy_init();
  if (session->curl == NULL) {
    g_set_error(error, bga_client_error_quark(), 6, "Failed to allocate libcurl handle");
    g_free(session);
    return NULL;
  }

  curl_easy_setopt(session->curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(session->curl, CURLOPT_USERAGENT, "gcheckers/1.0");
  curl_easy_setopt(session->curl, CURLOPT_COOKIEFILE, "");
  curl_easy_setopt(session->curl, CURLOPT_TIMEOUT, 30L);
  return session;
}

void bga_client_session_free(BgaClientSession *session) {
  if (session == NULL) {
    return;
  }

  if (session->curl != NULL) {
    curl_easy_cleanup(session->curl);
    session->curl = NULL;
  }
  g_clear_pointer(&session->request_token, g_free);
  g_free(session);
}

gboolean bga_client_extract_request_token(const char *body, char **out_token, GError **error) {
  g_return_val_if_fail(body != NULL, FALSE);
  g_return_val_if_fail(out_token != NULL, FALSE);

  g_autoptr(GRegex) regex = g_regex_new("requestToken\\s*:\\s*'([^']+)'", G_REGEX_MULTILINE, 0, NULL);
  g_autoptr(GMatchInfo) info = NULL;
  gboolean found = g_regex_match(regex, body, 0, &info);
  if (!found) {
    g_set_error(error, bga_client_error_quark(), 3, "Unable to find requestToken in BoardGameArena page");
    return FALSE;
  }

  g_autofree char *selected_token = NULL;
  guint match_count = 0;
  while (g_match_info_matches(info)) {
    g_autofree char *token = g_match_info_fetch(info, 1);
    if (token == NULL || *token == '\0') {
      g_set_error(error, bga_client_error_quark(), 4, "Extracted requestToken is empty");
      return FALSE;
    }

    match_count++;
    if (selected_token == NULL) {
      selected_token = g_strdup(token);
    } else if (g_strcmp0(selected_token, token) != 0) {
      g_set_error(error,
                  bga_client_error_quark(),
                  11,
                  "Ambiguous requestToken candidates detected (first='%s', another='%s', matches=%u)",
                  selected_token,
                  token,
                  match_count);
      return FALSE;
    }

    if (!g_match_info_next(info, NULL)) {
      break;
    }
  }

  g_free(*out_token);
  *out_token = g_steal_pointer(&selected_token);
  return TRUE;
}

gboolean bga_client_session_fetch_homepage_and_request_token(BgaClientSession *session,
                                                             char **out_homepage_body,
                                                             char **out_token,
                                                             GError **error) {
  g_return_val_if_fail(session != NULL, FALSE);
  g_return_val_if_fail(session->curl != NULL, FALSE);
  g_return_val_if_fail(out_token != NULL, FALSE);

  BgaHttpResponse response = {0};
  gboolean ok = bga_client_http_request(session->curl,
                                        bga_client_home_url,
                                        NULL,
                                        NULL,
                                        &response,
                                        error);
  if (!ok) {
    bga_http_response_clear(&response);
    return FALSE;
  }
  if (response.http_status < 200 || response.http_status >= 300) {
    g_set_error(error,
                bga_client_error_quark(),
                7,
                "BoardGameArena home page request failed with HTTP %ld",
                response.http_status);
    bga_http_response_clear(&response);
    return FALSE;
  }

  if (out_homepage_body != NULL) {
    g_free(*out_homepage_body);
    *out_homepage_body = g_strdup(response.body ? response.body : "");
  }

  ok = bga_client_extract_request_token(response.body ? response.body : "", out_token, error);
  if (ok) {
    g_free(session->request_token);
    session->request_token = g_strdup(*out_token);
  }
  bga_http_response_clear(&response);
  return ok;
}

gboolean bga_client_fetch_request_token(char **out_token, GError **error) {
  return bga_client_fetch_homepage_and_request_token(NULL, out_token, error);
}

gboolean bga_client_fetch_homepage_and_request_token(char **out_homepage_body,
                                                     char **out_token,
                                                     GError **error) {
  g_return_val_if_fail(out_token != NULL, FALSE);

  BgaClientSession *session = bga_client_session_new(error);
  if (session == NULL) {
    return FALSE;
  }

  gboolean ok =
      bga_client_session_fetch_homepage_and_request_token(session, out_homepage_body, out_token, error);
  bga_client_session_free(session);
  return ok;
}

gboolean bga_client_session_login_with_password(BgaClientSession *session,
                                                const BgaCredentials *credentials,
                                                const char *request_token,
                                                BgaHttpResponse *out_response,
                                                GError **error) {
  g_return_val_if_fail(session != NULL, FALSE);
  g_return_val_if_fail(session->curl != NULL, FALSE);
  g_return_val_if_fail(credentials != NULL, FALSE);
  g_return_val_if_fail(credentials->username != NULL, FALSE);
  g_return_val_if_fail(credentials->password != NULL, FALSE);
  g_return_val_if_fail(request_token != NULL, FALSE);
  g_return_val_if_fail(out_response != NULL, FALSE);

  char *encoded_username = curl_easy_escape(session->curl, credentials->username, 0);
  char *encoded_password = curl_easy_escape(session->curl, credentials->password, 0);
  char *encoded_token = curl_easy_escape(session->curl, request_token, 0);
  if (!encoded_username || !encoded_password || !encoded_token) {
    g_set_error(error, bga_client_error_quark(), 10, "Failed to escape login fields");
    if (encoded_username != NULL) {
      curl_free(encoded_username);
    }
    if (encoded_password != NULL) {
      curl_free(encoded_password);
    }
    if (encoded_token != NULL) {
      curl_free(encoded_token);
    }
    return FALSE;
  }

  const char *remember_value = credentials->remember_me ? "true" : "false";
  g_autofree char *post_fields = g_strdup_printf("username=%s&password=%s&remember_me=%s&request_token=%s",
                                                  encoded_username,
                                                  encoded_password,
                                                  remember_value,
                                                  encoded_token);
  curl_free(encoded_username);
  curl_free(encoded_password);
  curl_free(encoded_token);

  g_autofree char *request_token_header = g_strdup_printf("X-Request-Token: %s", request_token);
  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, request_token_header);
  g_free(session->request_token);
  session->request_token = g_strdup(request_token);
  gboolean ok = bga_client_http_request(session->curl,
                                        bga_client_login_url,
                                        post_fields,
                                        headers,
                                        out_response,
                                        error);
  curl_slist_free_all(headers);
  return ok;
}

static gboolean bga_client_session_prepare_game_history_fetch(BgaClientSession *session,
                                                              const char *encoded_user_id,
                                                              guint game_id,
                                                              char **out_referer_url,
                                                              GError **error) {
  g_return_val_if_fail(session != NULL, FALSE);
  g_return_val_if_fail(session->curl != NULL, FALSE);
  g_return_val_if_fail(encoded_user_id != NULL, FALSE);
  g_return_val_if_fail(game_id > 0, FALSE);
  g_return_val_if_fail(out_referer_url != NULL, FALSE);

  g_autofree char *gamestats_url = g_strdup_printf("%s?player=%s&opponent_id=0&game_id=%u&finished=0",
                                                    bga_client_gamestats_url_prefix,
                                                    encoded_user_id,
                                                    game_id);
  BgaHttpResponse gamestats_response = {0};
  if (!bga_client_http_request(session->curl, gamestats_url, NULL, NULL, &gamestats_response, error)) {
    bga_http_response_clear(&gamestats_response);
    return FALSE;
  }

  g_autofree char *gamestats_request_token = NULL;
  if (!bga_client_extract_request_token(gamestats_response.body ? gamestats_response.body : "",
                                        &gamestats_request_token,
                                        error)) {
    g_debug("Unable to extract requestToken from gamestats response: %s",
            (error != NULL && *error != NULL) ? (*error)->message : "unknown error");
    bga_http_response_clear(&gamestats_response);
    return FALSE;
  }
  g_free(session->request_token);
  session->request_token = g_strdup(gamestats_request_token);
  bga_http_response_clear(&gamestats_response);

  g_free(*out_referer_url);
  *out_referer_url = g_steal_pointer(&gamestats_url);
  return TRUE;
}

static gboolean bga_client_session_fetch_game_history_page(BgaClientSession *session,
                                                           const char *encoded_user_id,
                                                           guint game_id,
                                                           guint page,
                                                           gboolean update_stats,
                                                           const char *referer_url,
                                                           BgaHttpResponse *out_response,
                                                           GError **error) {
  g_return_val_if_fail(session != NULL, FALSE);
  g_return_val_if_fail(session->curl != NULL, FALSE);
  g_return_val_if_fail(encoded_user_id != NULL, FALSE);
  g_return_val_if_fail(game_id > 0, FALSE);
  g_return_val_if_fail(page > 0, FALSE);
  g_return_val_if_fail(referer_url != NULL, FALSE);
  g_return_val_if_fail(out_response != NULL, FALSE);

  gint64 cache_buster = g_get_real_time() / 1000;
  g_autofree char *get_games_url = page == 1
      ? g_strdup_printf(
            "%s?player=%s&opponent_id=0&game_id=%u&finished=0&updateStats=%u&dojo.preventCache=%" G_GINT64_FORMAT,
            bga_client_history_url_prefix,
            encoded_user_id,
            game_id,
            update_stats ? 1 : 0,
            cache_buster)
      : g_strdup_printf(
            "%s?player=%s&opponent_id=0&game_id=%u&finished=0&page=%u&updateStats=%u&dojo.preventCache=%"
            G_GINT64_FORMAT,
            bga_client_history_url_prefix,
            encoded_user_id,
            game_id,
            page,
            update_stats ? 1 : 0,
            cache_buster);

  struct curl_slist *headers = NULL;
  if (session->request_token != NULL && session->request_token[0] != '\0') {
    g_autofree char *request_token_header = g_strdup_printf("X-Request-Token: %s", session->request_token);
    headers = curl_slist_append(headers, request_token_header);
  }
  headers = curl_slist_append(headers, "X-Requested-With: XMLHttpRequest");
  g_autofree char *referer_header = g_strdup_printf("Referer: %s", referer_url);
  headers = curl_slist_append(headers, referer_header);

  gboolean ok = bga_client_http_request(session->curl, get_games_url, NULL, headers, out_response, error);
  curl_slist_free_all(headers);
  return ok;
}

gboolean bga_client_session_fetch_game_history(BgaClientSession *session,
                                               const char *user_id,
                                               guint game_id,
                                               BgaHttpResponse *out_response,
                                               GError **error) {
  g_return_val_if_fail(session != NULL, FALSE);
  g_return_val_if_fail(session->curl != NULL, FALSE);
  g_return_val_if_fail(user_id != NULL, FALSE);
  g_return_val_if_fail(game_id > 0, FALSE);
  g_return_val_if_fail(out_response != NULL, FALSE);

  char *encoded_user_id = curl_easy_escape(session->curl, user_id, 0);
  if (encoded_user_id == NULL) {
    g_set_error(error, bga_client_error_quark(), 14, "Failed to encode BoardGameArena user id");
    return FALSE;
  }

  g_autofree char *referer_url = NULL;
  gboolean ok = bga_client_session_prepare_game_history_fetch(session, encoded_user_id, game_id, &referer_url, error) &&
      bga_client_session_fetch_game_history_page(session,
                                                 encoded_user_id,
                                                 game_id,
                                                 1,
                                                 TRUE,
                                                 referer_url,
                                                 out_response,
                                                 error);
  curl_free(encoded_user_id);
  return ok;
}

gboolean bga_client_save_archive_logs_debug_page(const char *table_id,
                                                 const char *body,
                                                 char **out_path,
                                                 GError **error) {
  g_return_val_if_fail(table_id != NULL, FALSE);
  g_return_val_if_fail(out_path != NULL, FALSE);

  static gint response_counter = 0;
  gint response_id = g_atomic_int_add(&response_counter, 1) + 1;
  gint64 timestamp_us = g_get_real_time();
  g_autofree char *safe_table_id = bga_client_sanitize_filename_part(table_id);
  g_autofree char *path = g_strdup_printf("/tmp/gcheckers-bga-archive-logs-%s-%" G_GINT64_FORMAT "-%d.html",
                                          safe_table_id,
                                          timestamp_us,
                                          response_id);

  if (!g_file_set_contents(path, body ? body : "", -1, error)) {
    return FALSE;
  }

  *out_path = g_steal_pointer(&path);
  return TRUE;
}

static gboolean bga_client_session_fetch_archive_refresh(BgaClientSession *session,
                                                         const char *game_review_refresh_url,
                                                         const char *referer_url,
                                                         BgaHttpResponse *out_response,
                                                         GError **error) {
  g_return_val_if_fail(session != NULL, FALSE);
  g_return_val_if_fail(game_review_refresh_url != NULL, FALSE);
  g_return_val_if_fail(referer_url != NULL, FALSE);
  g_return_val_if_fail(out_response != NULL, FALSE);

  struct curl_slist *headers = bga_client_session_build_xhr_headers(session, referer_url, FALSE);
  gboolean ok = bga_client_http_request(session->curl,
                                        game_review_refresh_url,
                                        NULL,
                                        headers,
                                        out_response,
                                        error);
  curl_slist_free_all(headers);
  if (!ok) {
    return FALSE;
  }
  if (!bga_client_http_status_is_success(out_response,
                                         "BoardGameArena game review refresh request",
                                         18,
                                         error)) {
    return FALSE;
  }

  bga_client_session_update_request_token_from_body(session,
                                                    out_response->body ? out_response->body : "",
                                                    "game review refresh");
  return TRUE;
}

static gboolean bga_client_session_request_archive_generation(BgaClientSession *session,
                                                              const char *archive_request_url,
                                                              const char *referer_url,
                                                              GError **error) {
  g_return_val_if_fail(session != NULL, FALSE);
  g_return_val_if_fail(archive_request_url != NULL, FALSE);
  g_return_val_if_fail(referer_url != NULL, FALSE);

  BgaHttpResponse response = {0};
  struct curl_slist *headers = bga_client_session_build_xhr_headers(session, referer_url, TRUE);
  gboolean ok = bga_client_http_request(session->curl, archive_request_url, NULL, headers, &response, error);
  curl_slist_free_all(headers);
  if (!ok) {
    bga_http_response_clear(&response);
    return FALSE;
  }

  if (!bga_client_http_status_is_success(&response,
                                         "BoardGameArena table archive generation request",
                                         20,
                                         error)) {
    bga_http_response_clear(&response);
    return FALSE;
  }

  g_autofree char *status_zero_message = NULL;
  if (bga_client_response_has_status_zero_error(response.body ? response.body : "", &status_zero_message)) {
    g_set_error(error,
                bga_client_error_quark(),
                21,
                "BoardGameArena table archive generation failed: %s",
                status_zero_message);
    bga_http_response_clear(&response);
    return FALSE;
  }

  bga_http_response_clear(&response);
  return TRUE;
}

static gboolean bga_client_session_prepare_archive(BgaClientSession *session,
                                                   const char *game_review_refresh_url,
                                                   const char *archive_request_url,
                                                   const char *referer_url,
                                                   gboolean archive_request_already_sent,
                                                   GError **error) {
  g_return_val_if_fail(session != NULL, FALSE);
  g_return_val_if_fail(game_review_refresh_url != NULL, FALSE);
  g_return_val_if_fail(archive_request_url != NULL, FALSE);
  g_return_val_if_fail(referer_url != NULL, FALSE);

  gboolean requested_generation = archive_request_already_sent;
  for (guint attempt = 0; attempt < bga_client_archive_ready_poll_count; ++attempt) {
    BgaHttpResponse response = {0};
    if (!bga_client_session_fetch_archive_refresh(session,
                                                  game_review_refresh_url,
                                                  referer_url,
                                                  &response,
                                                  error)) {
      bga_http_response_clear(&response);
      return FALSE;
    }

    gboolean waiting =
        bga_client_archive_review_is_waiting_for_generation(response.body ? response.body : "");
    bga_http_response_clear(&response);
    if (!waiting) {
      return TRUE;
    }

    if (!requested_generation) {
      if (!bga_client_session_request_archive_generation(session, archive_request_url, referer_url, error)) {
        return FALSE;
      }
      requested_generation = TRUE;
    } else if (attempt + 1 < bga_client_archive_ready_poll_count) {
      g_usleep(bga_client_archive_ready_poll_usec);
    }
  }

  g_set_error(error,
              bga_client_error_quark(),
              22,
              "BoardGameArena is still preparing the game archive");
  return FALSE;
}

static gboolean bga_client_session_fetch_archive_logs_once(BgaClientSession *session,
                                                           const char *archive_logs_url,
                                                           const char *referer_url,
                                                           BgaHttpResponse *out_response,
                                                           GError **error) {
  g_return_val_if_fail(session != NULL, FALSE);
  g_return_val_if_fail(archive_logs_url != NULL, FALSE);
  g_return_val_if_fail(referer_url != NULL, FALSE);
  g_return_val_if_fail(out_response != NULL, FALSE);

  struct curl_slist *headers = bga_client_session_build_xhr_headers(session, referer_url, TRUE);
  gboolean ok = bga_client_http_request(session->curl, archive_logs_url, NULL, headers, out_response, error);
  curl_slist_free_all(headers);
  if (!ok) {
    return FALSE;
  }

  return bga_client_http_status_is_success(out_response,
                                           "BoardGameArena archive logs request",
                                           19,
                                           error);
}

gboolean bga_client_session_fetch_archive_logs(BgaClientSession *session,
                                               const char *table_id,
                                               BgaHttpResponse *out_response,
                                               char **out_debug_path,
                                               GError **error) {
  g_return_val_if_fail(session != NULL, FALSE);
  g_return_val_if_fail(session->curl != NULL, FALSE);
  g_return_val_if_fail(table_id != NULL, FALSE);
  g_return_val_if_fail(table_id[0] != '\0', FALSE);
  g_return_val_if_fail(out_response != NULL, FALSE);
  g_return_val_if_fail(out_debug_path != NULL, FALSE);

  char *encoded_table_id = curl_easy_escape(session->curl, table_id, 0);
  if (encoded_table_id == NULL) {
    g_set_error(error, bga_client_error_quark(), 17, "Failed to encode BoardGameArena table id");
    return FALSE;
  }

  g_autofree char *table_url = g_strdup_printf("%s?table=%s",
                                               bga_client_table_url_prefix,
                                               encoded_table_id);
  g_autofree char *table_refresh_url =
      g_strdup_printf("%s?table=%s&refreshtemplate=1&dojo.preventCache=%" G_GINT64_FORMAT,
                      bga_client_table_url_prefix,
                      encoded_table_id,
                      bga_client_dojo_prevent_cache());
  g_autofree char *game_review_url = g_strdup_printf("%s?table=%s",
                                                     bga_client_game_review_url_prefix,
                                                     encoded_table_id);
  g_autofree char *game_review_refresh_url =
      g_strdup_printf("%s?table=%s&refreshtemplate=1&dojo.preventCache=%" G_GINT64_FORMAT,
                      bga_client_game_review_url_prefix,
                      encoded_table_id,
                      bga_client_dojo_prevent_cache());
  g_autofree char *archive_request_url =
      g_strdup_printf("%s?table=%s&dojo.preventCache=%" G_GINT64_FORMAT,
                      bga_client_archive_request_url_prefix,
                      encoded_table_id,
                      bga_client_dojo_prevent_cache());
  g_autofree char *archive_logs_url =
      g_strdup_printf("%s?table=%s&translated=true&dojo.preventCache=%" G_GINT64_FORMAT,
                      bga_client_archive_logs_url_prefix,
                      encoded_table_id,
                      bga_client_dojo_prevent_cache());
  curl_free(encoded_table_id);

  BgaHttpResponse table_response = {0};
  if (!bga_client_http_request(session->curl, table_url, NULL, NULL, &table_response, error)) {
    bga_http_response_clear(&table_response);
    return FALSE;
  }
  if (!bga_client_http_status_is_success(&table_response,
                                         "BoardGameArena table page request",
                                         18,
                                         error)) {
    bga_http_response_clear(&table_response);
    return FALSE;
  }
  bga_client_session_update_request_token_from_body(session,
                                                    table_response.body ? table_response.body : "",
                                                    "table page");
  bga_http_response_clear(&table_response);

  BgaHttpResponse table_refresh_response = {0};
  struct curl_slist *table_refresh_headers = bga_client_session_build_xhr_headers(session, table_url, FALSE);
  gboolean table_refreshed = bga_client_http_request(session->curl,
                                                     table_refresh_url,
                                                     NULL,
                                                     table_refresh_headers,
                                                     &table_refresh_response,
                                                     error);
  curl_slist_free_all(table_refresh_headers);
  if (!table_refreshed) {
    bga_http_response_clear(&table_refresh_response);
    return FALSE;
  }
  if (!bga_client_http_status_is_success(&table_refresh_response,
                                         "BoardGameArena table refresh request",
                                         18,
                                         error)) {
    bga_http_response_clear(&table_refresh_response);
    return FALSE;
  }
  bga_client_session_update_request_token_from_body(session,
                                                    table_refresh_response.body ? table_refresh_response.body : "",
                                                    "table refresh");
  bga_http_response_clear(&table_refresh_response);

  if (!bga_client_session_prepare_archive(session,
                                          game_review_refresh_url,
                                          archive_request_url,
                                          table_url,
                                          FALSE,
                                          error)) {
    return FALSE;
  }

  if (!bga_client_session_fetch_archive_logs_once(session,
                                                  archive_logs_url,
                                                  game_review_url,
                                                  out_response,
                                                  error)) {
    return FALSE;
  }

  if (bga_client_archive_logs_error_needs_generation(out_response->body ? out_response->body : "")) {
    g_debug("BoardGameArena archive logs were not materialized yet; requesting table archive and retrying");
    bga_http_response_clear(out_response);
    if (!bga_client_session_request_archive_generation(session, archive_request_url, game_review_url, error)) {
      return FALSE;
    }
    if (!bga_client_session_prepare_archive(session,
                                            game_review_refresh_url,
                                            archive_request_url,
                                            game_review_url,
                                            TRUE,
                                            error)) {
      return FALSE;
    }
    if (!bga_client_session_fetch_archive_logs_once(session,
                                                    archive_logs_url,
                                                    game_review_url,
                                                    out_response,
                                                    error)) {
      return FALSE;
    }
  }

  g_autofree char *status_zero_message = NULL;
  if (bga_client_response_has_status_zero_error(out_response->body ? out_response->body : "",
                                               &status_zero_message)) {
    g_set_error(error,
                bga_client_error_quark(),
                23,
                "BoardGameArena archive logs response failed: %s",
                status_zero_message);
    return FALSE;
  }

  return bga_client_save_archive_logs_debug_page(table_id, out_response->body, out_debug_path, error);
}

gboolean bga_client_parse_homeworlds_archive_logs_sgf(const char *body,
                                                      const char *table_id,
                                                      char **out_sgf,
                                                      GError **error) {
  g_return_val_if_fail(body != NULL, FALSE);
  g_return_val_if_fail(out_sgf != NULL, FALSE);

  g_autoptr(GRegex) type_regex = g_regex_new("\"type\"\\s*:\\s*\"(notif_[^\"]+)\"",
                                             G_REGEX_DOTALL,
                                             0,
                                             NULL);
  g_autoptr(GMatchInfo) match_info = NULL;
  if (!g_regex_match(type_regex, body, 0, &match_info)) {
    g_set_error(error, bga_client_error_quark(), 20, "No BoardGameArena Homeworlds notifications found");
    return FALSE;
  }

  BgaHomeworldsArchiveState state = {0};
  bga_homeworlds_archive_state_init(&state);
  state.date = bga_homeworlds_archive_extract_date(body);
  state.table_id = table_id != NULL && table_id[0] != '\0'
      ? g_strdup(table_id)
      : bga_homeworlds_archive_extract_first_json_string(body, "table_id");
  state.winner = bga_homeworlds_archive_extract_winner(body);
  guint processed_notifications = 0;
  gboolean ok = TRUE;

  while (g_match_info_matches(match_info)) {
    int start_pos = -1;
    int end_pos = -1;
    if (!g_match_info_fetch_pos(match_info, 0, &start_pos, &end_pos) || start_pos < 0 || end_pos < 0) {
      ok = FALSE;
      g_set_error(error, bga_client_error_quark(), 21, "Unable to locate BoardGameArena notification object");
      break;
    }

    const char *object_start = bga_client_json_previous_object_start(body, body + start_pos);
    const char *object_end = object_start != NULL ? bga_client_json_object_end(object_start) : NULL;
    if (object_start == NULL || object_end == NULL || object_end <= object_start) {
      ok = FALSE;
      g_set_error(error, bga_client_error_quark(), 21, "Unable to isolate BoardGameArena notification object");
      break;
    }

    g_autofree char *event_body = g_strndup(object_start, (gsize)(object_end - object_start));
    g_autofree char *event_type = NULL;
    if (!bga_client_json_extract_string(event_body, "type", &event_type) ||
        !bga_homeworlds_archive_process_event(&state, event_type, event_body, error)) {
      ok = FALSE;
      break;
    }
    processed_notifications++;

    if (!g_match_info_next(match_info, NULL)) {
      break;
    }
  }

  if (ok && !bga_homeworlds_archive_finalize_pending_turn(&state, error)) {
    ok = FALSE;
  }
  if (ok && (processed_notifications == 0 || state.next_setup_side < 2)) {
    g_set_error(error,
                bga_client_error_quark(),
                53,
                "BoardGameArena archive did not contain a complete Homeworlds game");
    ok = FALSE;
  }

  if (!ok) {
    bga_homeworlds_archive_state_clear(&state);
    return FALSE;
  }

  g_free(*out_sgf);
  *out_sgf = bga_homeworlds_archive_format_sgf(&state);
  bga_homeworlds_archive_state_clear(&state);
  return TRUE;
}

gboolean bga_client_login_with_password(const BgaCredentials *credentials,
                                        const char *request_token,
                                        BgaHttpResponse *out_response,
                                        GError **error) {
  g_return_val_if_fail(credentials != NULL, FALSE);
  g_return_val_if_fail(request_token != NULL, FALSE);
  g_return_val_if_fail(out_response != NULL, FALSE);

  BgaClientSession *session = bga_client_session_new(error);
  if (session == NULL) {
    return FALSE;
  }

  gboolean ok = bga_client_session_login_with_password(session, credentials, request_token, out_response, error);
  bga_client_session_free(session);
  return ok;
}

gboolean bga_client_parse_login_response(const char *body, BgaLoginResult *out_result, GError **error) {
  g_return_val_if_fail(body != NULL, FALSE);
  g_return_val_if_fail(out_result != NULL, FALSE);

  out_result->kind = BGA_LOGIN_RESULT_PARSE_ERROR;
  g_clear_pointer(&out_result->error, g_free);
  g_clear_pointer(&out_result->exception, g_free);
  g_clear_pointer(&out_result->message, g_free);
  g_clear_pointer(&out_result->user_id, g_free);

  int status = -1;
  if (!bga_client_json_extract_int(body, "status", &status)) {
    g_set_error(error, bga_client_error_quark(), 12, "Unable to parse login response status");
    return FALSE;
  }

  if (status == 0) {
    out_result->kind = BGA_LOGIN_RESULT_STATUS_ZERO;
    bga_client_json_extract_string(body, "error", &out_result->error);
    bga_client_json_extract_string(body, "exception", &out_result->exception);
    return TRUE;
  }

  gboolean success = FALSE;
  if (!bga_client_json_extract_bool(body, "success", &success)) {
    g_set_error(error, bga_client_error_quark(), 13, "Unable to parse login response success field");
    return FALSE;
  }

  if (!success) {
    out_result->kind = BGA_LOGIN_RESULT_SUCCESS_FALSE;
    bga_client_json_extract_string(body, "message", &out_result->message);
    return TRUE;
  }

  out_result->kind = BGA_LOGIN_RESULT_SUCCESS_TRUE;
  bga_client_json_extract_string(body, "user_id", &out_result->user_id);
  return TRUE;
}

void bga_http_response_clear(BgaHttpResponse *response) {
  g_return_if_fail(response != NULL);

  g_free(response->body);
  response->body = NULL;
  response->http_status = 0;
}

void bga_login_result_clear(BgaLoginResult *result) {
  g_return_if_fail(result != NULL);

  g_clear_pointer(&result->error, g_free);
  g_clear_pointer(&result->exception, g_free);
  g_clear_pointer(&result->message, g_free);
  g_clear_pointer(&result->user_id, g_free);
  result->kind = BGA_LOGIN_RESULT_PARSE_ERROR;
}

void bga_history_game_summary_free(BgaHistoryGameSummary *summary) {
  if (summary == NULL) {
    return;
  }

  g_free(summary->table_id);
  g_free(summary->start_at);
  g_free(summary->player_one);
  g_free(summary->player_two);
  g_free(summary);
}

static BgaHistoryGameSummary *bga_history_game_summary_copy(const BgaHistoryGameSummary *summary) {
  g_return_val_if_fail(summary != NULL, NULL);

  BgaHistoryGameSummary *copy = g_new0(BgaHistoryGameSummary, 1);
  copy->table_id = g_strdup(summary->table_id);
  copy->start_at = g_strdup(summary->start_at);
  copy->player_one = g_strdup(summary->player_one);
  copy->player_two = g_strdup(summary->player_two);
  return copy;
}

static guint bga_history_game_summaries_append_unique_until_known(GPtrArray *destination,
                                                                  GHashTable *known_table_ids,
                                                                  GHashTable *stop_table_ids,
                                                                  GPtrArray *source,
                                                                  gboolean *out_stopped_on_known_table) {
  g_return_val_if_fail(destination != NULL, 0);
  g_return_val_if_fail(known_table_ids != NULL, 0);
  g_return_val_if_fail(source != NULL, 0);

  guint added = 0;
  if (out_stopped_on_known_table != NULL) {
    *out_stopped_on_known_table = FALSE;
  }

  for (guint i = 0; i < source->len; ++i) {
    const BgaHistoryGameSummary *summary = g_ptr_array_index(source, i);
    if (summary == NULL || summary->table_id == NULL || summary->table_id[0] == '\0') {
      continue;
    }
    if (stop_table_ids != NULL && g_hash_table_contains(stop_table_ids, summary->table_id)) {
      if (out_stopped_on_known_table != NULL) {
        *out_stopped_on_known_table = TRUE;
      }
      break;
    }
    if (g_hash_table_contains(known_table_ids, summary->table_id)) {
      continue;
    }

    BgaHistoryGameSummary *copy = bga_history_game_summary_copy(summary);
    g_hash_table_add(known_table_ids, g_strdup(copy->table_id));
    g_ptr_array_add(destination, copy);
    added++;
  }
  return added;
}

static gboolean bga_client_history_response_has_tables_array(const char *body) {
  g_return_val_if_fail(body != NULL, FALSE);

  g_autoptr(GRegex) regex = g_regex_new("\"tables\"\\s*:\\s*\\[", G_REGEX_DOTALL, 0, NULL);
  return g_regex_match(regex, body, 0, NULL);
}

gboolean bga_client_parse_history_games(const char *body, GPtrArray **out_games, GError **error) {
  g_return_val_if_fail(body != NULL, FALSE);
  g_return_val_if_fail(out_games != NULL, FALSE);

  g_autoptr(GRegex) regex =
      g_regex_new("\\{[^\\{\\}]*\"table_id\"\\s*:\\s*\"[^\"]+\"[^\\{\\}]*\\}", G_REGEX_DOTALL, 0, NULL);
  g_autoptr(GMatchInfo) info = NULL;
  gboolean found = g_regex_match(regex, body, 0, &info);
  if (!found) {
    if (!bga_client_history_response_has_tables_array(body)) {
      g_set_error(error, bga_client_error_quark(), 15, "No games found in history response");
      return FALSE;
    }

    if (*out_games != NULL) {
      g_ptr_array_unref(*out_games);
    }
    *out_games = g_ptr_array_new_with_free_func((GDestroyNotify)bga_history_game_summary_free);
    return TRUE;
  }

  GPtrArray *games = g_ptr_array_new_with_free_func((GDestroyNotify)bga_history_game_summary_free);
  while (g_match_info_matches(info)) {
    g_autofree char *entry_body = g_match_info_fetch(info, 0);
    if (entry_body == NULL) {
      break;
    }

    g_autofree char *table_id = NULL;
    g_autofree char *start = NULL;
    g_autofree char *player_names = NULL;
    if (!bga_client_json_extract_string(entry_body, "table_id", &table_id) ||
        !bga_client_json_extract_string(entry_body, "start", &start) ||
        !bga_client_json_extract_string(entry_body, "player_names", &player_names)) {
      if (!g_match_info_next(info, NULL)) {
        break;
      }
      continue;
    }

    g_auto(GStrv) split_names = g_strsplit(player_names, ",", 2);
    BgaHistoryGameSummary *summary = g_new0(BgaHistoryGameSummary, 1);
    summary->table_id = g_strdup(table_id);
    summary->start_at = bga_client_format_history_start_at(start);
    summary->player_one = bga_client_strdup_stripped(split_names[0]);
    summary->player_two = bga_client_strdup_stripped(split_names[1]);
    g_ptr_array_add(games, summary);

    if (!g_match_info_next(info, NULL)) {
      break;
    }
  }

  if (games->len == 0) {
    g_ptr_array_unref(games);
    g_set_error(error, bga_client_error_quark(), 16, "No parsed games found in history response");
    return FALSE;
  }

  if (*out_games != NULL) {
    g_ptr_array_unref(*out_games);
  }
  *out_games = games;
  return TRUE;
}

gboolean bga_client_parse_history_total_games(const char *body, guint *out_total, GError **error) {
  g_return_val_if_fail(body != NULL, FALSE);
  g_return_val_if_fail(out_total != NULL, FALSE);

  g_autoptr(GRegex) regex =
      g_regex_new("\"general\"\\s*:\\s*\\{[^\\{\\}]*\"played\"\\s*:\\s*\"?([0-9]+)\"?",
                  G_REGEX_DOTALL,
                  0,
                  NULL);
  g_autoptr(GMatchInfo) info = NULL;
  if (!g_regex_match(regex, body, 0, &info)) {
    g_set_error(error, bga_client_error_quark(), 53, "No total game count found in history response");
    return FALSE;
  }

  g_autofree char *value = g_match_info_fetch(info, 1);
  if (value == NULL) {
    g_set_error(error, bga_client_error_quark(), 54, "Malformed total game count in history response");
    return FALSE;
  }

  errno = 0;
  char *end = NULL;
  guint64 parsed = g_ascii_strtoull(value, &end, 10);
  if (end == value || end == NULL || *end != '\0' || errno == ERANGE || parsed > G_MAXUINT) {
    g_set_error(error, bga_client_error_quark(), 54, "Malformed total game count in history response");
    return FALSE;
  }

  *out_total = (guint)parsed;
  return TRUE;
}

gboolean bga_client_session_fetch_game_history_pages(BgaClientSession *session,
                                                     const char *user_id,
                                                     guint game_id,
                                                     guint first_page,
                                                     guint max_pages,
                                                     GHashTable *stop_table_ids,
                                                     GPtrArray **out_games,
                                                     BgaHistoryFetchResult *out_result,
                                                     GError **error) {
  g_return_val_if_fail(session != NULL, FALSE);
  g_return_val_if_fail(session->curl != NULL, FALSE);
  g_return_val_if_fail(user_id != NULL, FALSE);
  g_return_val_if_fail(game_id > 0, FALSE);
  g_return_val_if_fail(first_page > 0, FALSE);
  g_return_val_if_fail(max_pages > 0, FALSE);
  g_return_val_if_fail(out_games != NULL, FALSE);

  char *encoded_user_id = curl_easy_escape(session->curl, user_id, 0);
  if (encoded_user_id == NULL) {
    g_set_error(error, bga_client_error_quark(), 14, "Failed to encode BoardGameArena user id");
    return FALSE;
  }

  g_autofree char *referer_url = NULL;
  if (!bga_client_session_prepare_game_history_fetch(session, encoded_user_id, game_id, &referer_url, error)) {
    curl_free(encoded_user_id);
    return FALSE;
  }

  g_autoptr(GPtrArray) games = g_ptr_array_new_with_free_func((GDestroyNotify)bga_history_game_summary_free);
  g_autoptr(GHashTable) known_table_ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  guint expected_total = 0;
  gboolean has_expected_total = FALSE;
  BgaHistoryFetchResult result = {
    .first_page = first_page,
    .next_page = first_page,
  };

  for (guint page = first_page; page < first_page + max_pages; ++page) {
    BgaHttpResponse response = {0};
    if (!bga_client_session_fetch_game_history_page(session,
                                                    encoded_user_id,
                                                    game_id,
                                                    page,
                                                    page == 1,
                                                    referer_url,
                                                    &response,
                                                    error)) {
      bga_http_response_clear(&response);
      curl_free(encoded_user_id);
      return FALSE;
    }

    g_autoptr(GPtrArray) page_games = NULL;
    gboolean parsed = bga_client_parse_history_games(response.body ? response.body : "", &page_games, error);
    if (parsed && page == 1) {
      g_autoptr(GError) total_error = NULL;
      has_expected_total =
          bga_client_parse_history_total_games(response.body ? response.body : "", &expected_total, &total_error);
      if (!has_expected_total) {
        g_debug("BoardGameArena history response did not include a total game count: %s",
                total_error != NULL ? total_error->message : "unknown error");
      }
    }
    bga_http_response_clear(&response);
    if (!parsed) {
      curl_free(encoded_user_id);
      return FALSE;
    }

    result.pages_fetched++;
    result.next_page = page + 1;
    result.has_total_games = has_expected_total;
    result.total_games = expected_total;

    gboolean stopped_on_known_table = FALSE;
    guint added = bga_history_game_summaries_append_unique_until_known(games,
                                                                       known_table_ids,
                                                                       stop_table_ids,
                                                                       page_games,
                                                                       &stopped_on_known_table);
    if (stopped_on_known_table) {
      result.stopped_on_known_table = TRUE;
      break;
    }
    if (has_expected_total && games->len >= expected_total) {
      result.reached_end = TRUE;
      break;
    }
    if (page_games->len == 0 || added == 0) {
      result.reached_end = TRUE;
      break;
    }
  }

  curl_free(encoded_user_id);

  if (*out_games != NULL) {
    g_ptr_array_unref(*out_games);
  }
  *out_games = g_steal_pointer(&games);
  if (out_result != NULL) {
    *out_result = result;
  }
  return TRUE;
}

gboolean bga_client_session_fetch_all_game_history(BgaClientSession *session,
                                                   const char *user_id,
                                                   guint game_id,
                                                   GPtrArray **out_games,
                                                   GError **error) {
  return bga_client_session_fetch_game_history_pages(session,
                                                     user_id,
                                                     game_id,
                                                     1,
                                                     bga_client_history_default_page_batch_size,
                                                     NULL,
                                                     out_games,
                                                     NULL,
                                                     error);
}
