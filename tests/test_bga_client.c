#include <glib.h>
#include <glib/gstdio.h>

#include "../src/bga_client.h"

static void test_bga_client_extract_request_token(void) {
  const char *sample = "window.foo = { requestToken: 'wsWcpiIKAbLdCWN' };";
  g_autofree char *token = NULL;
  g_autoptr(GError) error = NULL;

  gboolean ok = bga_client_extract_request_token(sample, &token, &error);
  g_assert_no_error(error);
  g_assert_true(ok);
  g_assert_cmpstr(token, ==, "wsWcpiIKAbLdCWN");
}

static void test_bga_client_extract_request_token_allows_duplicate_value_matches(void) {
  const char *sample =
      "requestToken: 'wsWcpiIKAbLdCWN'; var x = 1; requestToken:'wsWcpiIKAbLdCWN';";
  g_autofree char *token = NULL;
  g_autoptr(GError) error = NULL;

  gboolean ok = bga_client_extract_request_token(sample, &token, &error);
  g_assert_no_error(error);
  g_assert_true(ok);
  g_assert_cmpstr(token, ==, "wsWcpiIKAbLdCWN");
}

static void test_bga_client_extract_request_token_rejects_ambiguous_matches(void) {
  const char *sample = "requestToken: 'firstToken'; requestToken: 'secondToken';";
  g_autofree char *token = NULL;
  g_autoptr(GError) error = NULL;

  gboolean ok = bga_client_extract_request_token(sample, &token, &error);
  g_assert_false(ok);
  g_assert_error(error, g_quark_from_static_string("bga-client-error"), 11);
}

static void test_bga_client_parse_login_response_status_zero(void) {
  const char *body =
      "{\"status\":\"0\",\"exception\":\"Bga\\\\Exceptions\\\\Account\\\\InvalidTokenException\","
      "\"error\":\"BGA service error\",\"expected\":1,\"code\":806,\"args\":null}";
  BgaLoginResult result = {0};
  g_autoptr(GError) error = NULL;

  gboolean ok = bga_client_parse_login_response(body, &result, &error);
  g_assert_no_error(error);
  g_assert_true(ok);
  g_assert_cmpint(result.kind, ==, BGA_LOGIN_RESULT_STATUS_ZERO);
  g_assert_cmpstr(result.error, ==, "BGA service error");
  g_assert_cmpstr(result.exception, ==, "Bga\\\\Exceptions\\\\Account\\\\InvalidTokenException");
  g_assert_null(result.message);
  g_assert_null(result.user_id);
  bga_login_result_clear(&result);
}

static void test_bga_client_parse_login_response_success_false(void) {
  const char *body =
      "{\"status\":1,\"data\":{\"success\":false,\"message\":\"Bad value for: username\"}}";
  BgaLoginResult result = {0};
  g_autoptr(GError) error = NULL;

  gboolean ok = bga_client_parse_login_response(body, &result, &error);
  g_assert_no_error(error);
  g_assert_true(ok);
  g_assert_cmpint(result.kind, ==, BGA_LOGIN_RESULT_SUCCESS_FALSE);
  g_assert_cmpstr(result.message, ==, "Bad value for: username");
  g_assert_null(result.error);
  g_assert_null(result.exception);
  g_assert_null(result.user_id);
  bga_login_result_clear(&result);
}

static void test_bga_client_parse_login_response_success_true(void) {
  const char *body =
      "{\"status\":1,\"data\":{\"success\":true,\"username\":\"JeromeLon\",\"user_id\":\"85752713\","
      "\"avatar\":\"cc3ac43c85\",\"is_premium\":\"1\",\"partner_event\":[]}}";
  BgaLoginResult result = {0};
  g_autoptr(GError) error = NULL;

  gboolean ok = bga_client_parse_login_response(body, &result, &error);
  g_assert_no_error(error);
  g_assert_true(ok);
  g_assert_cmpint(result.kind, ==, BGA_LOGIN_RESULT_SUCCESS_TRUE);
  g_assert_cmpstr(result.user_id, ==, "85752713");
  g_assert_null(result.error);
  g_assert_null(result.exception);
  g_assert_null(result.message);
  bga_login_result_clear(&result);
}

static void test_bga_client_parse_login_response_rejects_oversized_status(void) {
  const char *body = "{\"status\":999999999999999999999,\"data\":{\"success\":true}}";
  BgaLoginResult result = {0};
  g_autoptr(GError) error = NULL;

  gboolean ok = bga_client_parse_login_response(body, &result, &error);
  g_assert_false(ok);
  g_assert_error(error, g_quark_from_static_string("bga-client-error"), 12);
  g_assert_cmpint(result.kind, ==, BGA_LOGIN_RESULT_PARSE_ERROR);

  bga_login_result_clear(&result);
}

static void test_bga_client_parse_history_games(void) {
  const char *body =
      "{"
      "\"status\":1,"
      "\"data\":{"
      "\"tables\":["
      "{"
      "\"table_id\":\"769024787\","
      "\"start\":\"1764696665\","
      "\"player_names\":\" capable ladybug , JeromeLon \""
      "},"
      "{"
      "\"table_id\":\"761272836\","
      "\"start\":\"1763246194\","
      "\"player_names\":\"JeromeLon,N057r4d4mu5Pi\""
      "}"
      "]"
      "}"
      "}";
  g_autoptr(GPtrArray) games = NULL;
  g_autoptr(GError) error = NULL;

  gboolean ok = bga_client_parse_history_games(body, &games, &error);
  g_assert_no_error(error);
  g_assert_true(ok);
  g_assert_nonnull(games);
  g_assert_cmpuint(games->len, ==, 2);

  BgaHistoryGameSummary *first = g_ptr_array_index(games, 0);
  g_assert_nonnull(first);
  g_assert_cmpstr(first->table_id, ==, "769024787");
  g_assert_cmpstr(first->start_at, ==, "2025-12-02 17:31");
  g_assert_cmpstr(first->player_one, ==, "capable ladybug");
  g_assert_cmpstr(first->player_two, ==, "JeromeLon");

  BgaHistoryGameSummary *second = g_ptr_array_index(games, 1);
  g_assert_nonnull(second);
  g_assert_cmpstr(second->table_id, ==, "761272836");
  g_assert_cmpstr(second->start_at, ==, "2025-11-15 22:36");
  g_assert_cmpstr(second->player_one, ==, "JeromeLon");
  g_assert_cmpstr(second->player_two, ==, "N057r4d4mu5Pi");
}

static void test_bga_client_save_archive_logs_debug_page(void) {
  g_autofree char *path = NULL;
  g_autoptr(GError) error = NULL;

  gboolean ok = bga_client_save_archive_logs_debug_page("716050283/not-file", "<html>archive</html>", &path, &error);
  g_assert_no_error(error);
  g_assert_true(ok);
  g_assert_nonnull(path);
  g_assert_true(g_str_has_prefix(path, "/tmp/gcheckers-bga-archive-logs-716050283_not-file-"));
  g_assert_true(g_str_has_suffix(path, ".html"));

  g_autofree char *contents = NULL;
  g_assert_true(g_file_get_contents(path, &contents, NULL, &error));
  g_assert_no_error(error);
  g_assert_cmpstr(contents, ==, "<html>archive</html>");

  g_assert_cmpint(g_remove(path), ==, 0);
}

static void test_bga_client_archive_generation_detection(void) {
  const char *waiting_body =
      "<div class=\"archive-status\">Searching for the game archive. Please wait...</div>";
  const char *ready_body = "<div class=\"archive-status\">Archive ready</div>";
  const char *missing_logs_body =
      "{\"status\":\"0\",\"exception\":\"feException\","
      "\"error\":\"Cannot find gamenotifs log file of an archived table (reference: MU 28\\/05 11:35:56)\","
      "\"expected\":0,\"code\":100,\"args\":null}";

  g_assert_true(bga_client_archive_review_is_waiting_for_generation(waiting_body));
  g_assert_false(bga_client_archive_review_is_waiting_for_generation(ready_body));
  g_assert_true(bga_client_archive_logs_error_needs_generation(missing_logs_body));
  g_assert_false(bga_client_archive_logs_error_needs_generation("{\"status\":1,\"data\":{\"logs\":[]}}"));
}

static void test_bga_client_parse_homeworlds_archive_logs_sgf(void) {
  g_autofree char *body = NULL;
  g_autoptr(GError) error = NULL;
  gboolean loaded = g_file_get_contents("tests/fixtures/bga-homeworlds-archive-logs-716050283.json",
                                        &body,
                                        NULL,
                                        &error);
  g_assert_no_error(error);
  g_assert_true(loaded);

  g_autofree char *sgf = NULL;
  gboolean parsed = bga_client_parse_homeworlds_archive_logs_sgf(body, &sgf, &error);
  g_assert_no_error(error);
  g_assert_true(parsed);
  g_assert_nonnull(sgf);

  g_assert_true(g_str_has_prefix(sgf,
                                 "(;AP[gcheckers]CA[UTF-8]FF[4]GM[40]PB[SenetMaster]PW[JeromeLon]"
                                 ";B[G3Y2b3];W[Y1B2g3]"));
  g_assert_nonnull(g_strstr_len(sgf, -1, ";B[H1b1>S0(G1)]"));
  g_assert_nonnull(g_strstr_len(sgf, -1, ";W[H2b1>S1(G3)]"));
  g_assert_nonnull(g_strstr_len(sgf, -1, ";W[S2b2- H2r1=y pass]"));
  g_assert_null(g_strstr_len(sgf, -1, "H2g3- H2g+ H2y+ H2g+"));
  g_assert_nonnull(g_strstr_len(sgf, -1, ";W[H2y+]"));
  g_assert_nonnull(g_strstr_len(sgf, -1, ";B[H2g+ H2g!]"));
}

static void test_bga_client_live_login_logs_response(void) {
  const char *username = g_getenv("GCHECKERS_BGA_USERNAME");
  const char *password = g_getenv("GCHECKERS_BGA_PASSWORD");
  if (username == NULL || *username == '\0' || password == NULL || *password == '\0') {
    g_test_skip("Set GCHECKERS_BGA_USERNAME and GCHECKERS_BGA_PASSWORD to run live BGA login test.");
    return;
  }

  g_autoptr(GError) error = NULL;
  BgaClientSession *session = bga_client_session_new(&error);
  g_assert_no_error(error);
  g_assert_nonnull(session);

  g_autofree char *homepage_body = NULL;
  g_autofree char *request_token = NULL;
  gboolean fetched =
      bga_client_session_fetch_homepage_and_request_token(session, &homepage_body, &request_token, &error);
  g_assert_no_error(error);
  g_assert_true(fetched);
  g_assert_nonnull(homepage_body);
  g_assert_nonnull(request_token);

  BgaCredentials credentials = {
    .username = username,
    .password = password,
    .remember_me = TRUE,
  };
  BgaHttpResponse response = {0};
  gboolean logged_in =
      bga_client_session_login_with_password(session, &credentials, request_token, &response, &error);
  g_assert_no_error(error);
  g_assert_true(logged_in);

  const char *homepage_path = g_getenv("GCHECKERS_BGA_HOME_RESULT_PATH");
  if (homepage_path == NULL || *homepage_path == '\0') {
    homepage_path = "/tmp/gcheckers-bga-home-response.html";
  }
  g_autoptr(GError) homepage_write_error = NULL;
  gboolean homepage_written = g_file_set_contents(homepage_path, homepage_body, -1, &homepage_write_error);
  g_assert_no_error(homepage_write_error);
  g_assert_true(homepage_written);

  const char *result_path = g_getenv("GCHECKERS_BGA_RESULT_PATH");
  if (result_path == NULL || *result_path == '\0') {
    result_path = "/tmp/gcheckers-bga-login-response.html";
  }
  g_autoptr(GError) write_error = NULL;
  gboolean written = g_file_set_contents(result_path, response.body ? response.body : "", -1, &write_error);
  g_assert_no_error(write_error);
  g_assert_true(written);

  g_print("BGA request token: %s\n", request_token);
  g_print("BGA login HTTP status: %ld\n", response.http_status);
  g_print("BGA home response saved to: %s\n", homepage_path);
  g_print("BGA login response saved to: %s\n", result_path);
  g_print("BGA login response body:\n%s\n", response.body ? response.body : "");
  g_assert_nonnull(response.body);
  bga_http_response_clear(&response);
  bga_client_session_free(session);
}

static void test_bga_client_live_archive_logs_fetch_sequence(void) {
  const char *username = g_getenv("GCHECKERS_BGA_USERNAME");
  const char *password = g_getenv("GCHECKERS_BGA_PASSWORD");
  const char *table_id = g_getenv("GCHECKERS_BGA_TABLE_ID");
  if (username == NULL || *username == '\0' ||
      password == NULL || *password == '\0' ||
      table_id == NULL || *table_id == '\0') {
    g_test_skip("Set GCHECKERS_BGA_USERNAME, GCHECKERS_BGA_PASSWORD, and GCHECKERS_BGA_TABLE_ID to run live test.");
    return;
  }

  g_autoptr(GError) error = NULL;
  BgaClientSession *session = bga_client_session_new(&error);
  g_assert_no_error(error);
  g_assert_nonnull(session);

  g_autofree char *request_token = NULL;
  gboolean fetched = bga_client_session_fetch_homepage_and_request_token(session, NULL, &request_token, &error);
  g_assert_no_error(error);
  g_assert_true(fetched);

  BgaCredentials credentials = {
    .username = username,
    .password = password,
    .remember_me = TRUE,
  };
  BgaHttpResponse login_response = {0};
  gboolean logged_in =
      bga_client_session_login_with_password(session, &credentials, request_token, &login_response, &error);
  g_assert_no_error(error);
  g_assert_true(logged_in);
  bga_http_response_clear(&login_response);

  g_autofree char *debug_path = NULL;
  BgaHttpResponse logs_response = {0};
  gboolean imported =
      bga_client_session_fetch_archive_logs(session, table_id, &logs_response, &debug_path, &error);
  g_assert_no_error(error);
  g_assert_true(imported);
  g_assert_cmpint(logs_response.http_status, >=, 200);
  g_assert_cmpint(logs_response.http_status, <, 300);
  g_assert_nonnull(debug_path);
  g_assert_nonnull(logs_response.body);
  g_assert_false(g_strrstr(logs_response.body, "\"status\":\"0\"") != NULL);
  g_print("BGA archive logs saved to: %s\n", debug_path);

  bga_http_response_clear(&logs_response);
  bga_client_session_free(session);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/bga-client/extract-request-token", test_bga_client_extract_request_token);
  g_test_add_func("/bga-client/extract-request-token-duplicate-values",
                  test_bga_client_extract_request_token_allows_duplicate_value_matches);
  g_test_add_func("/bga-client/extract-request-token-ambiguous-values",
                  test_bga_client_extract_request_token_rejects_ambiguous_matches);
  g_test_add_func("/bga-client/parse-login-response-status-zero",
                  test_bga_client_parse_login_response_status_zero);
  g_test_add_func("/bga-client/parse-login-response-success-false",
                  test_bga_client_parse_login_response_success_false);
  g_test_add_func("/bga-client/parse-login-response-success-true",
                  test_bga_client_parse_login_response_success_true);
  g_test_add_func("/bga-client/parse-login-response-rejects-oversized-status",
                  test_bga_client_parse_login_response_rejects_oversized_status);
  g_test_add_func("/bga-client/parse-history-games", test_bga_client_parse_history_games);
  g_test_add_func("/bga-client/save-archive-logs-debug-page", test_bga_client_save_archive_logs_debug_page);
  g_test_add_func("/bga-client/archive-generation-detection", test_bga_client_archive_generation_detection);
  g_test_add_func("/bga-client/parse-homeworlds-archive-logs-sgf",
                  test_bga_client_parse_homeworlds_archive_logs_sgf);
  g_test_add_func("/bga-client/live-login-logs-response", test_bga_client_live_login_logs_response);
  g_test_add_func("/bga-client/live-archive-logs-fetch-sequence",
                  test_bga_client_live_archive_logs_fetch_sequence);
  return g_test_run();
}
