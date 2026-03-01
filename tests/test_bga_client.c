#include <glib.h>

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

static void test_bga_client_parse_checkers_history_games(void) {
  const char *body =
      "{"
      "\"status\":1,"
      "\"data\":{"
      "\"tables\":["
      "{"
      "\"table_id\":\"769024787\","
      "\"start\":\"1764696665\","
      "\"player_names\":\"capable ladybug,JeromeLon\""
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

  gboolean ok = bga_client_parse_checkers_history_games(body, &games, &error);
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
  g_test_add_func("/bga-client/parse-checkers-history-games",
                  test_bga_client_parse_checkers_history_games);
  g_test_add_func("/bga-client/live-login-logs-response", test_bga_client_live_login_logs_response);
  return g_test_run();
}
