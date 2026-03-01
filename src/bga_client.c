#include "bga_client.h"

#include <curl/curl.h>

static const char *bga_client_home_url = "https://en.boardgamearena.com/";
static const char *bga_client_login_url = "https://en.boardgamearena.com/account/auth/loginUserWithPassword.html";
static const char *bga_client_gamestats_url_prefix = "https://boardgamearena.com/gamestats";
static const char *bga_client_history_url_prefix =
    "https://boardgamearena.com/gamestats/gamestats/getGames.html";

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

  *out_value = (int)g_ascii_strtoll(value, NULL, 10);
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

  size_t bytes = size * nmemb;
  if (bytes == 0) {
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

gboolean bga_client_session_fetch_checkers_history(BgaClientSession *session,
                                                   const char *user_id,
                                                   BgaHttpResponse *out_response,
                                                   GError **error) {
  g_return_val_if_fail(session != NULL, FALSE);
  g_return_val_if_fail(session->curl != NULL, FALSE);
  g_return_val_if_fail(user_id != NULL, FALSE);
  g_return_val_if_fail(out_response != NULL, FALSE);

  char *encoded_user_id = curl_easy_escape(session->curl, user_id, 0);
  if (encoded_user_id == NULL) {
    g_set_error(error, bga_client_error_quark(), 14, "Failed to encode BoardGameArena user id");
    return FALSE;
  }

  g_autofree char *gamestats_url = g_strdup_printf("%s?player=%s&opponent_id=0&game_id=74&finished=0",
                                                    bga_client_gamestats_url_prefix,
                                                    encoded_user_id);
  BgaHttpResponse gamestats_response = {0};
  if (!bga_client_http_request(session->curl, gamestats_url, NULL, NULL, &gamestats_response, error)) {
    bga_http_response_clear(&gamestats_response);
    curl_free(encoded_user_id);
    return FALSE;
  }

  g_autofree char *gamestats_request_token = NULL;
  if (!bga_client_extract_request_token(gamestats_response.body ? gamestats_response.body : "",
                                        &gamestats_request_token,
                                        error)) {
    g_debug("Unable to extract requestToken from gamestats response: %s",
            (error != NULL && *error != NULL) ? (*error)->message : "unknown error");
    bga_http_response_clear(&gamestats_response);
    curl_free(encoded_user_id);
    return FALSE;
  }
  g_free(session->request_token);
  session->request_token = g_strdup(gamestats_request_token);
  bga_http_response_clear(&gamestats_response);

  gint64 cache_buster = g_get_real_time() / 1000;
  g_autofree char *get_games_url = g_strdup_printf(
      "%s?player=%s&opponent_id=0&game_id=74&finished=0&updateStats=1&dojo.preventCache=%" G_GINT64_FORMAT,
      bga_client_history_url_prefix,
      encoded_user_id,
      cache_buster);
  curl_free(encoded_user_id);

  struct curl_slist *headers = NULL;
  if (session->request_token != NULL && session->request_token[0] != '\0') {
    g_autofree char *request_token_header = g_strdup_printf("X-Request-Token: %s", session->request_token);
    headers = curl_slist_append(headers, request_token_header);
  }
  headers = curl_slist_append(headers, "X-Requested-With: XMLHttpRequest");
  g_autofree char *referer_header = g_strdup_printf("Referer: %s", gamestats_url);
  headers = curl_slist_append(headers, referer_header);

  gboolean ok = bga_client_http_request(session->curl, get_games_url, NULL, headers, out_response, error);
  curl_slist_free_all(headers);
  return ok;
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

gboolean bga_client_parse_checkers_history_games(const char *body, GPtrArray **out_games, GError **error) {
  g_return_val_if_fail(body != NULL, FALSE);
  g_return_val_if_fail(out_games != NULL, FALSE);

  g_autoptr(GRegex) regex =
      g_regex_new("\\{[^\\{\\}]*\"table_id\"\\s*:\\s*\"[^\"]+\"[^\\{\\}]*\\}", G_REGEX_DOTALL, 0, NULL);
  g_autoptr(GMatchInfo) info = NULL;
  gboolean found = g_regex_match(regex, body, 0, &info);
  if (!found) {
    g_set_error(error, bga_client_error_quark(), 15, "No checkers games found in history response");
    return FALSE;
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
    summary->player_one = g_strdup(split_names[0] != NULL ? split_names[0] : "");
    summary->player_two = g_strdup(split_names[1] != NULL ? split_names[1] : "");
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
