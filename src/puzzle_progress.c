#include "puzzle_progress.h"

#include "file_dialog_history.h"
#include "rulesets.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <string.h>

struct _CheckersPuzzleProgressStore {
  gint ref_count;
  GMutex mutex;
  char *state_dir;
};

enum {
  CHECKERS_PUZZLE_PROGRESS_SCHEMA_VERSION = 1,
  CHECKERS_PUZZLE_PROGRESS_REPORT_THRESHOLD_COUNT = 10,
  CHECKERS_PUZZLE_PROGRESS_REPORT_STALE_COUNT = 5,
  CHECKERS_PUZZLE_PROGRESS_REPORT_STALE_MS = 24 * 60 * 60 * G_GINT64_CONSTANT(1000),
};

static const char *checkers_puzzle_progress_user_id_key = "puzzle-user-id";
static const char *checkers_puzzle_progress_user_id_fallback_name = "user-id";
static const char *checkers_puzzle_progress_history_name = "attempt-history.jsonl";

static char *checkers_puzzle_progress_store_build_path(CheckersPuzzleProgressStore *store, const char *name) {
  g_return_val_if_fail(store != NULL, NULL);
  g_return_val_if_fail(name != NULL, NULL);

  return g_build_filename(store->state_dir, name, NULL);
}

static gboolean checkers_puzzle_progress_store_write_text(CheckersPuzzleProgressStore *store,
                                                          const char *path,
                                                          const char *text,
                                                          GError **error) {
  g_return_val_if_fail(store != NULL, FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(text != NULL, FALSE);

  if (!g_file_set_contents(path, text, -1, error)) {
    return FALSE;
  }

  return TRUE;
}

static gboolean checkers_puzzle_progress_append_json_string(GString *buffer, const char *value) {
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(value != NULL, FALSE);

  g_string_append_c(buffer, '"');
  for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; p++) {
    switch (*p) {
      case '\\':
        g_string_append(buffer, "\\\\");
        break;
      case '"':
        g_string_append(buffer, "\\\"");
        break;
      case '\b':
        g_string_append(buffer, "\\b");
        break;
      case '\f':
        g_string_append(buffer, "\\f");
        break;
      case '\n':
        g_string_append(buffer, "\\n");
        break;
      case '\r':
        g_string_append(buffer, "\\r");
        break;
      case '\t':
        g_string_append(buffer, "\\t");
        break;
      default:
        if (*p < 0x20) {
          g_string_append_printf(buffer, "\\u%04x", *p);
        } else {
          g_string_append_c(buffer, (char)*p);
        }
        break;
    }
  }
  g_string_append_c(buffer, '"');
  return TRUE;
}

static const char *checkers_puzzle_progress_result_to_string(CheckersPuzzleAttemptResult result) {
  switch (result) {
    case CHECKERS_PUZZLE_ATTEMPT_RESULT_UNRESOLVED:
      return "unresolved";
    case CHECKERS_PUZZLE_ATTEMPT_RESULT_SUCCESS:
      return "success";
    case CHECKERS_PUZZLE_ATTEMPT_RESULT_FAILURE:
      return "failure";
    case CHECKERS_PUZZLE_ATTEMPT_RESULT_ANALYZE:
      return "analyze";
    default:
      return NULL;
  }
}

static gboolean checkers_puzzle_progress_result_from_string(const char *text,
                                                            CheckersPuzzleAttemptResult *out_result) {
  g_return_val_if_fail(text != NULL, FALSE);
  g_return_val_if_fail(out_result != NULL, FALSE);

  if (g_strcmp0(text, "unresolved") == 0) {
    *out_result = CHECKERS_PUZZLE_ATTEMPT_RESULT_UNRESOLVED;
    return TRUE;
  }
  if (g_strcmp0(text, "success") == 0) {
    *out_result = CHECKERS_PUZZLE_ATTEMPT_RESULT_SUCCESS;
    return TRUE;
  }
  if (g_strcmp0(text, "failure") == 0) {
    *out_result = CHECKERS_PUZZLE_ATTEMPT_RESULT_FAILURE;
    return TRUE;
  }
  if (g_strcmp0(text, "analyze") == 0) {
    *out_result = CHECKERS_PUZZLE_ATTEMPT_RESULT_ANALYZE;
    return TRUE;
  }

  return FALSE;
}

static const char *checkers_puzzle_progress_color_to_string(CheckersColor color) {
  return color == CHECKERS_COLOR_BLACK ? "black" : "white";
}

static gboolean checkers_puzzle_progress_color_from_string(const char *text, CheckersColor *out_color) {
  g_return_val_if_fail(text != NULL, FALSE);
  g_return_val_if_fail(out_color != NULL, FALSE);

  if (g_strcmp0(text, "white") == 0) {
    *out_color = CHECKERS_COLOR_WHITE;
    return TRUE;
  }
  if (g_strcmp0(text, "black") == 0) {
    *out_color = CHECKERS_COLOR_BLACK;
    return TRUE;
  }

  return FALSE;
}

static gboolean checkers_puzzle_progress_skip_ws(const char **cursor) {
  g_return_val_if_fail(cursor != NULL, FALSE);
  g_return_val_if_fail(*cursor != NULL, FALSE);

  while (g_ascii_isspace(**cursor)) {
    (*cursor)++;
  }

  return TRUE;
}

static gboolean checkers_puzzle_progress_consume_char(const char **cursor, char expected) {
  g_return_val_if_fail(cursor != NULL, FALSE);
  g_return_val_if_fail(*cursor != NULL, FALSE);

  checkers_puzzle_progress_skip_ws(cursor);
  if (**cursor != expected) {
    return FALSE;
  }
  (*cursor)++;
  return TRUE;
}

static gboolean checkers_puzzle_progress_parse_json_string(const char **cursor, char **out_value) {
  g_return_val_if_fail(cursor != NULL, FALSE);
  g_return_val_if_fail(*cursor != NULL, FALSE);
  g_return_val_if_fail(out_value != NULL, FALSE);

  checkers_puzzle_progress_skip_ws(cursor);
  if (**cursor != '"') {
    return FALSE;
  }
  (*cursor)++;

  GString *buffer = g_string_new(NULL);
  while (**cursor != '\0' && **cursor != '"') {
    if (**cursor == '\\') {
      (*cursor)++;
      switch (**cursor) {
        case '"':
        case '\\':
        case '/':
          g_string_append_c(buffer, **cursor);
          break;
        case 'b':
          g_string_append_c(buffer, '\b');
          break;
        case 'f':
          g_string_append_c(buffer, '\f');
          break;
        case 'n':
          g_string_append_c(buffer, '\n');
          break;
        case 'r':
          g_string_append_c(buffer, '\r');
          break;
        case 't':
          g_string_append_c(buffer, '\t');
          break;
        default:
          g_string_free(buffer, TRUE);
          return FALSE;
      }
    } else {
      g_string_append_c(buffer, **cursor);
    }
    (*cursor)++;
  }

  if (**cursor != '"') {
    g_string_free(buffer, TRUE);
    return FALSE;
  }
  (*cursor)++;

  *out_value = g_string_free(buffer, FALSE);
  return TRUE;
}

static gboolean checkers_puzzle_progress_parse_int64(const char **cursor, gint64 *out_value) {
  g_return_val_if_fail(cursor != NULL, FALSE);
  g_return_val_if_fail(*cursor != NULL, FALSE);
  g_return_val_if_fail(out_value != NULL, FALSE);

  checkers_puzzle_progress_skip_ws(cursor);
  char *end = NULL;
  gint64 value = g_ascii_strtoll(*cursor, &end, 10);
  if (end == *cursor) {
    return FALSE;
  }
  *cursor = end;
  *out_value = value;
  return TRUE;
}

static gboolean checkers_puzzle_progress_parse_uint(const char **cursor, guint *out_value) {
  g_return_val_if_fail(out_value != NULL, FALSE);

  gint64 value = 0;
  if (!checkers_puzzle_progress_parse_int64(cursor, &value) || value < 0 || value > G_MAXUINT) {
    return FALSE;
  }

  *out_value = (guint)value;
  return TRUE;
}

static gboolean checkers_puzzle_progress_parse_bool(const char **cursor, gboolean *out_value) {
  g_return_val_if_fail(cursor != NULL, FALSE);
  g_return_val_if_fail(*cursor != NULL, FALSE);
  g_return_val_if_fail(out_value != NULL, FALSE);

  checkers_puzzle_progress_skip_ws(cursor);
  if (g_str_has_prefix(*cursor, "true")) {
    *cursor += strlen("true");
    *out_value = TRUE;
    return TRUE;
  }
  if (g_str_has_prefix(*cursor, "false")) {
    *cursor += strlen("false");
    *out_value = FALSE;
    return TRUE;
  }

  return FALSE;
}

static gboolean checkers_puzzle_progress_parse_move(const char **cursor, CheckersMove *out_move) {
  g_return_val_if_fail(cursor != NULL, FALSE);
  g_return_val_if_fail(*cursor != NULL, FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  *out_move = (CheckersMove){0};
  if (!checkers_puzzle_progress_consume_char(cursor, '{')) {
    return FALSE;
  }

  g_autofree char *key = NULL;
  guint length = 0;
  guint captures = 0;
  if (!checkers_puzzle_progress_parse_json_string(cursor, &key) || g_strcmp0(key, "length") != 0 ||
      !checkers_puzzle_progress_consume_char(cursor, ':') ||
      !checkers_puzzle_progress_parse_uint(cursor, &length) || !checkers_puzzle_progress_consume_char(cursor, ',')) {
    return FALSE;
  }

  g_clear_pointer(&key, g_free);
  if (!checkers_puzzle_progress_parse_json_string(cursor, &key) || g_strcmp0(key, "captures") != 0 ||
      !checkers_puzzle_progress_consume_char(cursor, ':') ||
      !checkers_puzzle_progress_parse_uint(cursor, &captures) || !checkers_puzzle_progress_consume_char(cursor, ',')) {
    return FALSE;
  }

  g_clear_pointer(&key, g_free);
  if (!checkers_puzzle_progress_parse_json_string(cursor, &key) || g_strcmp0(key, "path") != 0 ||
      !checkers_puzzle_progress_consume_char(cursor, ':') ||
      !checkers_puzzle_progress_consume_char(cursor, '[')) {
    return FALSE;
  }

  if (length > CHECKERS_MAX_MOVE_LENGTH) {
    return FALSE;
  }

  out_move->length = (uint8_t)length;
  out_move->captures = (uint8_t)captures;
  for (guint i = 0; i < length; i++) {
    guint path_value = 0;
    if (!checkers_puzzle_progress_parse_uint(cursor, &path_value) || path_value > G_MAXUINT8) {
      return FALSE;
    }
    out_move->path[i] = (uint8_t)path_value;
    if (i + 1 < length && !checkers_puzzle_progress_consume_char(cursor, ',')) {
      return FALSE;
    }
  }

  if (!checkers_puzzle_progress_consume_char(cursor, ']') ||
      !checkers_puzzle_progress_consume_char(cursor, '}')) {
    return FALSE;
  }

  return TRUE;
}

static gboolean checkers_puzzle_progress_parse_record_line(const char *line, CheckersPuzzleAttemptRecord *out_record) {
  g_return_val_if_fail(line != NULL, FALSE);
  g_return_val_if_fail(out_record != NULL, FALSE);

  *out_record = (CheckersPuzzleAttemptRecord){0};
  const char *cursor = line;
  g_autofree char *key = NULL;
  g_autofree char *string_value = NULL;
  guint schema_version = 0;

  if (!checkers_puzzle_progress_consume_char(&cursor, '{')) {
    return FALSE;
  }

  if (!checkers_puzzle_progress_parse_json_string(&cursor, &key) || g_strcmp0(key, "schema_version") != 0 ||
      !checkers_puzzle_progress_consume_char(&cursor, ':') ||
      !checkers_puzzle_progress_parse_uint(&cursor, &schema_version) ||
      schema_version != CHECKERS_PUZZLE_PROGRESS_SCHEMA_VERSION ||
      !checkers_puzzle_progress_consume_char(&cursor, ',')) {
    return FALSE;
  }

  g_clear_pointer(&key, g_free);
  if (!checkers_puzzle_progress_parse_json_string(&cursor, &key) || g_strcmp0(key, "attempt_id") != 0 ||
      !checkers_puzzle_progress_consume_char(&cursor, ':') ||
      !checkers_puzzle_progress_parse_json_string(&cursor, &out_record->attempt_id) ||
      !checkers_puzzle_progress_consume_char(&cursor, ',')) {
    return FALSE;
  }

  g_clear_pointer(&key, g_free);
  if (!checkers_puzzle_progress_parse_json_string(&cursor, &key) || g_strcmp0(key, "puzzle_id") != 0 ||
      !checkers_puzzle_progress_consume_char(&cursor, ':') ||
      !checkers_puzzle_progress_parse_json_string(&cursor, &out_record->puzzle_id) ||
      !checkers_puzzle_progress_consume_char(&cursor, ',')) {
    return FALSE;
  }

  g_clear_pointer(&key, g_free);
  if (!checkers_puzzle_progress_parse_json_string(&cursor, &key) || g_strcmp0(key, "puzzle_number") != 0 ||
      !checkers_puzzle_progress_consume_char(&cursor, ':') ||
      !checkers_puzzle_progress_parse_uint(&cursor, &out_record->puzzle_number) ||
      !checkers_puzzle_progress_consume_char(&cursor, ',')) {
    return FALSE;
  }

  g_clear_pointer(&key, g_free);
  if (!checkers_puzzle_progress_parse_json_string(&cursor, &key) ||
      g_strcmp0(key, "puzzle_source_name") != 0 || !checkers_puzzle_progress_consume_char(&cursor, ':') ||
      !checkers_puzzle_progress_parse_json_string(&cursor, &out_record->puzzle_source_name) ||
      !checkers_puzzle_progress_consume_char(&cursor, ',')) {
    return FALSE;
  }

  g_clear_pointer(&key, g_free);
  if (!checkers_puzzle_progress_parse_json_string(&cursor, &key) ||
      g_strcmp0(key, "puzzle_ruleset") != 0 || !checkers_puzzle_progress_consume_char(&cursor, ':') ||
      !checkers_puzzle_progress_parse_json_string(&cursor, &string_value) ||
      !checkers_puzzle_progress_consume_char(&cursor, ',')) {
    return FALSE;
  }
  if (!checkers_ruleset_find_by_short_name(string_value, &out_record->puzzle_ruleset)) {
    return FALSE;
  }

  g_clear_pointer(&key, g_free);
  g_clear_pointer(&string_value, g_free);
  if (!checkers_puzzle_progress_parse_json_string(&cursor, &key) || g_strcmp0(key, "attacker") != 0 ||
      !checkers_puzzle_progress_consume_char(&cursor, ':') ||
      !checkers_puzzle_progress_parse_json_string(&cursor, &string_value) ||
      !checkers_puzzle_progress_consume_char(&cursor, ',')) {
    return FALSE;
  }
  if (!checkers_puzzle_progress_color_from_string(string_value, &out_record->attacker)) {
    return FALSE;
  }

  g_clear_pointer(&key, g_free);
  g_clear_pointer(&string_value, g_free);
  if (!checkers_puzzle_progress_parse_json_string(&cursor, &key) ||
      g_strcmp0(key, "started_unix_ms") != 0 || !checkers_puzzle_progress_consume_char(&cursor, ':') ||
      !checkers_puzzle_progress_parse_int64(&cursor, &out_record->started_unix_ms) ||
      !checkers_puzzle_progress_consume_char(&cursor, ',')) {
    return FALSE;
  }

  g_clear_pointer(&key, g_free);
  if (!checkers_puzzle_progress_parse_json_string(&cursor, &key) ||
      g_strcmp0(key, "finished_unix_ms") != 0 || !checkers_puzzle_progress_consume_char(&cursor, ':') ||
      !checkers_puzzle_progress_parse_int64(&cursor, &out_record->finished_unix_ms) ||
      !checkers_puzzle_progress_consume_char(&cursor, ',')) {
    return FALSE;
  }

  g_clear_pointer(&key, g_free);
  if (!checkers_puzzle_progress_parse_json_string(&cursor, &key) || g_strcmp0(key, "result") != 0 ||
      !checkers_puzzle_progress_consume_char(&cursor, ':') ||
      !checkers_puzzle_progress_parse_json_string(&cursor, &string_value) ||
      !checkers_puzzle_progress_consume_char(&cursor, ',')) {
    return FALSE;
  }
  if (!checkers_puzzle_progress_result_from_string(string_value, &out_record->result)) {
    return FALSE;
  }

  g_clear_pointer(&key, g_free);
  g_clear_pointer(&string_value, g_free);
  if (!checkers_puzzle_progress_parse_json_string(&cursor, &key) ||
      g_strcmp0(key, "failure_on_first_move") != 0 || !checkers_puzzle_progress_consume_char(&cursor, ':') ||
      !checkers_puzzle_progress_parse_bool(&cursor, &out_record->failure_on_first_move) ||
      !checkers_puzzle_progress_consume_char(&cursor, ',')) {
    return FALSE;
  }

  g_clear_pointer(&key, g_free);
  if (!checkers_puzzle_progress_parse_json_string(&cursor, &key) ||
      g_strcmp0(key, "first_reported_unix_ms") != 0 || !checkers_puzzle_progress_consume_char(&cursor, ':') ||
      !checkers_puzzle_progress_parse_int64(&cursor, &out_record->first_reported_unix_ms) ||
      !checkers_puzzle_progress_consume_char(&cursor, ',')) {
    return FALSE;
  }

  g_clear_pointer(&key, g_free);
  if (!checkers_puzzle_progress_parse_json_string(&cursor, &key) || g_strcmp0(key, "report_count") != 0 ||
      !checkers_puzzle_progress_consume_char(&cursor, ':') ||
      !checkers_puzzle_progress_parse_uint(&cursor, &out_record->report_count)) {
    return FALSE;
  }

  checkers_puzzle_progress_skip_ws(&cursor);
  if (*cursor == ',') {
    cursor++;
    g_clear_pointer(&key, g_free);
    if (!checkers_puzzle_progress_parse_json_string(&cursor, &key) ||
        g_strcmp0(key, "failed_first_move") != 0 || !checkers_puzzle_progress_consume_char(&cursor, ':') ||
        !checkers_puzzle_progress_parse_move(&cursor, &out_record->failed_first_move)) {
      return FALSE;
    }
    out_record->has_failed_first_move = TRUE;
  }

  if (!checkers_puzzle_progress_consume_char(&cursor, '}')) {
    return FALSE;
  }

  checkers_puzzle_progress_skip_ws(&cursor);
  return *cursor == '\0';
}

static void checkers_puzzle_progress_append_move_json(GString *buffer, const CheckersMove *move) {
  g_return_if_fail(buffer != NULL);
  g_return_if_fail(move != NULL);

  g_string_append_printf(buffer,
                         "{\"length\":%u,\"captures\":%u,\"path\":[",
                         (guint)move->length,
                         (guint)move->captures);
  for (guint i = 0; i < move->length; i++) {
    if (i > 0) {
      g_string_append_c(buffer, ',');
    }
    g_string_append_printf(buffer, "%u", (guint)move->path[i]);
  }
  g_string_append(buffer, "]}");
}

static gboolean checkers_puzzle_progress_append_record_json(GString *buffer,
                                                            const CheckersPuzzleAttemptRecord *record,
                                                            gboolean include_schema_version) {
  g_return_val_if_fail(buffer != NULL, FALSE);
  g_return_val_if_fail(record != NULL, FALSE);
  g_return_val_if_fail(record->attempt_id != NULL, FALSE);
  g_return_val_if_fail(record->puzzle_id != NULL, FALSE);
  g_return_val_if_fail(record->puzzle_source_name != NULL, FALSE);

  const char *result = checkers_puzzle_progress_result_to_string(record->result);
  const char *attacker = checkers_puzzle_progress_color_to_string(record->attacker);
  const char *ruleset_short_name = checkers_ruleset_short_name(record->puzzle_ruleset);
  g_return_val_if_fail(result != NULL, FALSE);
  g_return_val_if_fail(ruleset_short_name != NULL, FALSE);
  g_return_val_if_fail(attacker != NULL, FALSE);

  g_string_append_c(buffer, '{');
  if (include_schema_version) {
    g_string_append_printf(buffer, "\"schema_version\":%u,", CHECKERS_PUZZLE_PROGRESS_SCHEMA_VERSION);
  }
  g_string_append(buffer, "\"attempt_id\":");
  checkers_puzzle_progress_append_json_string(buffer, record->attempt_id);
  g_string_append(buffer, ",\"puzzle_id\":");
  checkers_puzzle_progress_append_json_string(buffer, record->puzzle_id);
  g_string_append_printf(buffer, ",\"puzzle_number\":%u,\"puzzle_source_name\":", record->puzzle_number);
  checkers_puzzle_progress_append_json_string(buffer, record->puzzle_source_name);
  g_string_append(buffer, ",\"puzzle_ruleset\":");
  checkers_puzzle_progress_append_json_string(buffer, ruleset_short_name);
  g_string_append(buffer, ",\"attacker\":");
  checkers_puzzle_progress_append_json_string(buffer, attacker);
  g_string_append_printf(buffer,
                         ",\"started_unix_ms\":%" G_GINT64_FORMAT ",\"finished_unix_ms\":%" G_GINT64_FORMAT
                         ",\"result\":",
                         record->started_unix_ms,
                         record->finished_unix_ms);
  checkers_puzzle_progress_append_json_string(buffer, result);
  g_string_append_printf(buffer,
                         ",\"failure_on_first_move\":%s,\"first_reported_unix_ms\":%" G_GINT64_FORMAT
                         ",\"report_count\":%u",
                         record->failure_on_first_move ? "true" : "false",
                         record->first_reported_unix_ms,
                         record->report_count);
  if (record->has_failed_first_move) {
    g_string_append(buffer, ",\"failed_first_move\":");
    checkers_puzzle_progress_append_move_json(buffer, &record->failed_first_move);
  }
  g_string_append_c(buffer, '}');
  return TRUE;
}

static gboolean checkers_puzzle_progress_store_write_history_locked(CheckersPuzzleProgressStore *store,
                                                                   const GPtrArray *records,
                                                                   GError **error) {
  g_return_val_if_fail(store != NULL, FALSE);
  g_return_val_if_fail(records != NULL, FALSE);

  g_autofree char *history_path = checkers_puzzle_progress_store_build_path(store,
                                                                            checkers_puzzle_progress_history_name);
  g_autoptr(GString) content = g_string_new(NULL);
  for (guint i = 0; i < records->len; i++) {
    CheckersPuzzleAttemptRecord *record = g_ptr_array_index((GPtrArray *)records, i);
    if (!checkers_puzzle_progress_append_record_json(content, record, TRUE)) {
      g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Failed to serialize puzzle attempt record");
      return FALSE;
    }
    g_string_append_c(content, '\n');
  }

  return checkers_puzzle_progress_store_write_text(store, history_path, content->str, error);
}

static GPtrArray *checkers_puzzle_progress_store_load_attempt_history_locked(CheckersPuzzleProgressStore *store,
                                                                             GError **error) {
  g_return_val_if_fail(store != NULL, NULL);

  g_autofree char *history_path = checkers_puzzle_progress_store_build_path(store,
                                                                            checkers_puzzle_progress_history_name);
  if (!g_file_test(history_path, G_FILE_TEST_EXISTS)) {
    return g_ptr_array_new_with_free_func((GDestroyNotify)checkers_puzzle_attempt_record_free);
  }

  g_autofree char *contents = NULL;
  if (!g_file_get_contents(history_path, &contents, NULL, error)) {
    return NULL;
  }

  GPtrArray *records = g_ptr_array_new_with_free_func((GDestroyNotify)checkers_puzzle_attempt_record_free);
  g_auto(GStrv) lines = g_strsplit(contents, "\n", -1);
  for (guint i = 0; lines[i] != NULL; i++) {
    if (lines[i][0] == '\0') {
      continue;
    }

    CheckersPuzzleAttemptRecord *record = g_new0(CheckersPuzzleAttemptRecord, 1);
    if (!checkers_puzzle_progress_parse_record_line(lines[i], record)) {
      checkers_puzzle_attempt_record_free(record);
      g_ptr_array_unref(records);
      g_set_error(error,
                  G_FILE_ERROR,
                  G_FILE_ERROR_INVAL,
                  "Failed to parse puzzle attempt history line %u",
                  i + 1);
      return NULL;
    }
    g_ptr_array_add(records, record);
  }

  return records;
}

static gboolean checkers_puzzle_progress_record_is_unsent(const CheckersPuzzleAttemptRecord *record) {
  g_return_val_if_fail(record != NULL, FALSE);

  return checkers_puzzle_attempt_record_is_resolved(record) && record->report_count == 0;
}

CheckersPuzzleProgressStore *checkers_puzzle_progress_store_new(const char *state_dir) {
  g_return_val_if_fail(state_dir != NULL, NULL);

  CheckersPuzzleProgressStore *store = g_new0(CheckersPuzzleProgressStore, 1);
  store->ref_count = 1;
  g_mutex_init(&store->mutex);
  store->state_dir = g_strdup(state_dir);
  return store;
}

CheckersPuzzleProgressStore *checkers_puzzle_progress_store_ref(CheckersPuzzleProgressStore *store) {
  g_return_val_if_fail(store != NULL, NULL);

  g_atomic_int_inc(&store->ref_count);
  return store;
}

void checkers_puzzle_progress_store_unref(CheckersPuzzleProgressStore *store) {
  g_return_if_fail(store != NULL);

  if (!g_atomic_int_dec_and_test(&store->ref_count)) {
    return;
  }

  g_mutex_clear(&store->mutex);
  g_clear_pointer(&store->state_dir, g_free);
  g_free(store);
}

char *checkers_puzzle_progress_store_dup_state_dir(CheckersPuzzleProgressStore *store) {
  g_return_val_if_fail(store != NULL, NULL);

  g_mutex_lock(&store->mutex);
  g_autofree char *copy = g_strdup(store->state_dir);
  g_mutex_unlock(&store->mutex);
  return g_steal_pointer(&copy);
}

char *checkers_puzzle_progress_store_get_history_path(CheckersPuzzleProgressStore *store) {
  g_return_val_if_fail(store != NULL, NULL);

  g_mutex_lock(&store->mutex);
  g_autofree char *path = checkers_puzzle_progress_store_build_path(store,
                                                                    checkers_puzzle_progress_history_name);
  g_mutex_unlock(&store->mutex);
  return g_steal_pointer(&path);
}

char *checkers_puzzle_progress_store_get_or_create_user_id(CheckersPuzzleProgressStore *store, GError **error) {
  g_return_val_if_fail(store != NULL, NULL);

  g_mutex_lock(&store->mutex);

  g_autoptr(GSettings) settings = gcheckers_file_dialog_history_create_settings();
  if (settings != NULL) {
    g_autofree char *user_id = g_settings_get_string(settings, checkers_puzzle_progress_user_id_key);
    if (user_id != NULL && user_id[0] != '\0') {
      g_mutex_unlock(&store->mutex);
      return g_steal_pointer(&user_id);
    }

    user_id = g_uuid_string_random();
    if (g_settings_set_string(settings, checkers_puzzle_progress_user_id_key, user_id)) {
      g_mutex_unlock(&store->mutex);
      return g_steal_pointer(&user_id);
    }
  }

  g_autofree char *fallback_path = checkers_puzzle_progress_store_build_path(store,
                                                                              checkers_puzzle_progress_user_id_fallback_name);
  g_autofree char *fallback_text = NULL;
  if (g_file_get_contents(fallback_path, &fallback_text, NULL, NULL)) {
    g_strstrip(fallback_text);
    if (fallback_text[0] != '\0') {
      g_mutex_unlock(&store->mutex);
      return g_steal_pointer(&fallback_text);
    }
  }

  g_autofree char *generated = g_uuid_string_random();
  if (!checkers_puzzle_progress_store_write_text(store, fallback_path, generated, error)) {
    g_mutex_unlock(&store->mutex);
    return NULL;
  }

  g_mutex_unlock(&store->mutex);
  return g_steal_pointer(&generated);
}

gboolean checkers_puzzle_progress_store_append_attempt(CheckersPuzzleProgressStore *store,
                                                       const CheckersPuzzleAttemptRecord *record,
                                                       GError **error) {
  g_return_val_if_fail(store != NULL, FALSE);
  g_return_val_if_fail(record != NULL, FALSE);

  g_mutex_lock(&store->mutex);
  g_autoptr(GPtrArray) records = checkers_puzzle_progress_store_load_attempt_history_locked(store, error);
  if (records == NULL) {
    g_mutex_unlock(&store->mutex);
    return FALSE;
  }

  g_ptr_array_add(records, checkers_puzzle_attempt_record_copy(record));
  gboolean ok = checkers_puzzle_progress_store_write_history_locked(store, records, error);
  g_mutex_unlock(&store->mutex);
  return ok;
}

gboolean checkers_puzzle_progress_store_replace_attempt(CheckersPuzzleProgressStore *store,
                                                        const CheckersPuzzleAttemptRecord *record,
                                                        GError **error) {
  g_return_val_if_fail(store != NULL, FALSE);
  g_return_val_if_fail(record != NULL, FALSE);
  g_return_val_if_fail(record->attempt_id != NULL, FALSE);

  g_mutex_lock(&store->mutex);
  g_autoptr(GPtrArray) records = checkers_puzzle_progress_store_load_attempt_history_locked(store, error);
  if (records == NULL) {
    g_mutex_unlock(&store->mutex);
    return FALSE;
  }

  gboolean found = FALSE;
  for (guint i = 0; i < records->len; i++) {
    CheckersPuzzleAttemptRecord *existing = g_ptr_array_index(records, i);
    if (g_strcmp0(existing->attempt_id, record->attempt_id) != 0) {
      continue;
    }

    CheckersPuzzleAttemptRecord *replacement = checkers_puzzle_attempt_record_copy(record);
    checkers_puzzle_attempt_record_free(existing);
    g_ptr_array_index(records, i) = replacement;
    found = TRUE;
    break;
  }

  if (!found) {
    g_set_error(error,
                G_FILE_ERROR,
                G_FILE_ERROR_NOENT,
                "Missing puzzle attempt %s in local history",
                record->attempt_id);
    g_mutex_unlock(&store->mutex);
    return FALSE;
  }

  gboolean ok = checkers_puzzle_progress_store_write_history_locked(store, records, error);
  g_mutex_unlock(&store->mutex);
  return ok;
}

GPtrArray *checkers_puzzle_progress_store_load_attempt_history(CheckersPuzzleProgressStore *store, GError **error) {
  g_return_val_if_fail(store != NULL, NULL);

  g_mutex_lock(&store->mutex);
  GPtrArray *records = checkers_puzzle_progress_store_load_attempt_history_locked(store, error);
  g_mutex_unlock(&store->mutex);
  return records;
}

GPtrArray *checkers_puzzle_progress_store_collect_unsent_attempts(CheckersPuzzleProgressStore *store, GError **error) {
  g_return_val_if_fail(store != NULL, NULL);

  g_mutex_lock(&store->mutex);
  g_autoptr(GPtrArray) history = checkers_puzzle_progress_store_load_attempt_history_locked(store, error);
  if (history == NULL) {
    g_mutex_unlock(&store->mutex);
    return NULL;
  }

  GPtrArray *unsent = g_ptr_array_new_with_free_func((GDestroyNotify)checkers_puzzle_attempt_record_free);
  for (guint i = 0; i < history->len; i++) {
    CheckersPuzzleAttemptRecord *record = g_ptr_array_index(history, i);
    if (checkers_puzzle_progress_record_is_unsent(record)) {
      g_ptr_array_add(unsent, checkers_puzzle_attempt_record_copy(record));
    }
  }

  g_mutex_unlock(&store->mutex);
  return unsent;
}

gboolean checkers_puzzle_progress_store_mark_reported(CheckersPuzzleProgressStore *store,
                                                      gint64 reported_unix_ms,
                                                      GError **error) {
  g_return_val_if_fail(store != NULL, FALSE);

  g_mutex_lock(&store->mutex);
  g_autoptr(GPtrArray) history = checkers_puzzle_progress_store_load_attempt_history_locked(store, error);
  if (history == NULL) {
    g_mutex_unlock(&store->mutex);
    return FALSE;
  }

  gboolean changed = FALSE;
  for (guint i = 0; i < history->len; i++) {
    CheckersPuzzleAttemptRecord *record = g_ptr_array_index(history, i);
    if (!checkers_puzzle_progress_record_is_unsent(record)) {
      continue;
    }

    if (record->first_reported_unix_ms == 0) {
      record->first_reported_unix_ms = reported_unix_ms;
    }
    record->report_count++;
    changed = TRUE;
  }

  gboolean ok = TRUE;
  if (changed) {
    ok = checkers_puzzle_progress_store_write_history_locked(store, history, error);
  }
  g_mutex_unlock(&store->mutex);
  return ok;
}

CheckersPuzzleAttemptRecord *checkers_puzzle_attempt_record_copy(const CheckersPuzzleAttemptRecord *record) {
  g_return_val_if_fail(record != NULL, NULL);

  CheckersPuzzleAttemptRecord *copy = g_new0(CheckersPuzzleAttemptRecord, 1);
  *copy = *record;
  copy->attempt_id = g_strdup(record->attempt_id);
  copy->puzzle_id = g_strdup(record->puzzle_id);
  copy->puzzle_source_name = g_strdup(record->puzzle_source_name);
  return copy;
}

void checkers_puzzle_attempt_record_free(CheckersPuzzleAttemptRecord *record) {
  if (record == NULL) {
    return;
  }

  checkers_puzzle_attempt_record_clear(record);
  g_free(record);
}

void checkers_puzzle_attempt_record_clear(CheckersPuzzleAttemptRecord *record) {
  g_return_if_fail(record != NULL);

  g_clear_pointer(&record->attempt_id, g_free);
  g_clear_pointer(&record->puzzle_id, g_free);
  g_clear_pointer(&record->puzzle_source_name, g_free);
  *record = (CheckersPuzzleAttemptRecord){0};
}

gboolean checkers_puzzle_attempt_record_is_resolved(const CheckersPuzzleAttemptRecord *record) {
  g_return_val_if_fail(record != NULL, FALSE);

  return record->result == CHECKERS_PUZZLE_ATTEMPT_RESULT_SUCCESS ||
         record->result == CHECKERS_PUZZLE_ATTEMPT_RESULT_FAILURE ||
         record->result == CHECKERS_PUZZLE_ATTEMPT_RESULT_ANALYZE;
}

char *checkers_puzzle_progress_build_upload_json(const char *user_id, const GPtrArray *attempt_history) {
  g_return_val_if_fail(user_id != NULL, NULL);
  g_return_val_if_fail(attempt_history != NULL, NULL);

  g_autoptr(GString) json = g_string_new("{\"schema_version\":1,\"user_id\":");
  checkers_puzzle_progress_append_json_string(json, user_id);
  g_string_append(json, ",\"client\":{\"app_id\":\"io.github.jeromea.gcheckers\",\"app_version\":\"dev\"},\"attempts\":[");

  gboolean first = TRUE;
  for (guint i = 0; i < attempt_history->len; i++) {
    CheckersPuzzleAttemptRecord *record = g_ptr_array_index((GPtrArray *)attempt_history, i);
    if (!checkers_puzzle_attempt_record_is_resolved(record)) {
      continue;
    }
    if (!first) {
      g_string_append_c(json, ',');
    }
    first = FALSE;
    checkers_puzzle_progress_append_record_json(json, record, FALSE);
  }

  g_string_append(json, "]}");
  return g_string_free(g_steal_pointer(&json), FALSE);
}

gboolean checkers_puzzle_progress_should_send_report(const GPtrArray *attempt_history, gint64 now_unix_ms) {
  g_return_val_if_fail(attempt_history != NULL, FALSE);

  guint unsent_count = 0;
  gint64 oldest_unsent_ms = 0;
  for (guint i = 0; i < attempt_history->len; i++) {
    CheckersPuzzleAttemptRecord *record = g_ptr_array_index((GPtrArray *)attempt_history, i);
    if (!checkers_puzzle_progress_record_is_unsent(record)) {
      continue;
    }

    unsent_count++;
    gint64 candidate_time = record->finished_unix_ms > 0 ? record->finished_unix_ms : record->started_unix_ms;
    if (oldest_unsent_ms == 0 || candidate_time < oldest_unsent_ms) {
      oldest_unsent_ms = candidate_time;
    }
  }

  if (unsent_count >= CHECKERS_PUZZLE_PROGRESS_REPORT_THRESHOLD_COUNT) {
    return TRUE;
  }
  if (unsent_count >= CHECKERS_PUZZLE_PROGRESS_REPORT_STALE_COUNT && oldest_unsent_ms > 0 &&
      now_unix_ms - oldest_unsent_ms >= CHECKERS_PUZZLE_PROGRESS_REPORT_STALE_MS) {
    return TRUE;
  }

  return FALSE;
}
