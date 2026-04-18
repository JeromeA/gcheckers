#include "../src/app_paths.h"
#include "../src/puzzle_progress.h"

#include <glib.h>

static char *test_puzzle_progress_make_state_dir(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root = g_dir_make_tmp("gcheckers-puzzle-progress-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_nonnull(root);

  g_setenv("GCHECKERS_PUZZLE_PROGRESS_DIR", root, TRUE);
  char *state_dir =
      gcheckers_app_paths_get_user_state_subdir("GCHECKERS_PUZZLE_PROGRESS_DIR", "puzzle-progress", &error);
  g_assert_no_error(error);
  g_assert_nonnull(state_dir);
  return state_dir;
}

static CheckersPuzzleAttemptRecord test_puzzle_progress_make_record(const char *attempt_id,
                                                                    CheckersPuzzleAttemptResult result) {
  CheckersPuzzleAttemptRecord record = {0};
  record.attempt_id = g_strdup(attempt_id);
  record.puzzle_id = g_strdup("international/puzzle-0007.sgf");
  record.puzzle_number = 7;
  record.puzzle_source_name = g_strdup("puzzle-0007.sgf");
  record.puzzle_ruleset = PLAYER_RULESET_INTERNATIONAL;
  record.attacker = CHECKERS_COLOR_WHITE;
  record.started_unix_ms = 1713300000000;
  record.finished_unix_ms = result == CHECKERS_PUZZLE_ATTEMPT_RESULT_UNRESOLVED ? 0 : 1713300005000;
  record.result = result;
  record.failure_on_first_move = FALSE;
  record.has_failed_first_move = FALSE;
  record.first_reported_unix_ms = 0;
  record.report_count = 0;
  return record;
}

static void test_puzzle_progress_user_id_persists(void) {
  g_setenv("GSETTINGS_BACKEND", "memory", TRUE);
  g_autofree char *state_dir = test_puzzle_progress_make_state_dir();
  CheckersPuzzleProgressStore *store = checkers_puzzle_progress_store_new(state_dir);
  g_assert_nonnull(store);

  g_autoptr(GError) error = NULL;
  g_autofree char *first_id = checkers_puzzle_progress_store_get_or_create_user_id(store, &error);
  g_assert_no_error(error);
  g_assert_nonnull(first_id);

  g_autofree char *second_id = checkers_puzzle_progress_store_get_or_create_user_id(store, &error);
  g_assert_no_error(error);
  g_assert_cmpstr(first_id, ==, second_id);

  checkers_puzzle_progress_store_unref(store);
  g_unsetenv("GCHECKERS_PUZZLE_PROGRESS_DIR");
}

static void test_puzzle_progress_append_and_replace_success(void) {
  g_autofree char *state_dir = test_puzzle_progress_make_state_dir();
  CheckersPuzzleProgressStore *store = checkers_puzzle_progress_store_new(state_dir);

  CheckersPuzzleAttemptRecord unresolved =
      test_puzzle_progress_make_record("attempt-success", CHECKERS_PUZZLE_ATTEMPT_RESULT_UNRESOLVED);
  g_autoptr(GError) error = NULL;
  g_assert_true(checkers_puzzle_progress_store_append_attempt(store, &unresolved, &error));
  g_assert_no_error(error);

  CheckersPuzzleAttemptRecord success =
      test_puzzle_progress_make_record("attempt-success", CHECKERS_PUZZLE_ATTEMPT_RESULT_SUCCESS);
  g_assert_true(checkers_puzzle_progress_store_replace_attempt(store, &success, &error));
  g_assert_no_error(error);

  g_autoptr(GPtrArray) history = checkers_puzzle_progress_store_load_attempt_history(store, &error);
  g_assert_no_error(error);
  g_assert_cmpuint(history->len, ==, 1);
  CheckersPuzzleAttemptRecord *stored = g_ptr_array_index(history, 0);
  g_assert_cmpint(stored->result, ==, CHECKERS_PUZZLE_ATTEMPT_RESULT_SUCCESS);
  g_assert_false(stored->failure_on_first_move);

  checkers_puzzle_attempt_record_clear(&unresolved);
  checkers_puzzle_attempt_record_clear(&success);
  checkers_puzzle_progress_store_unref(store);
  g_unsetenv("GCHECKERS_PUZZLE_PROGRESS_DIR");
}

static void test_puzzle_progress_first_move_failure_persists(void) {
  g_autofree char *state_dir = test_puzzle_progress_make_state_dir();
  CheckersPuzzleProgressStore *store = checkers_puzzle_progress_store_new(state_dir);

  CheckersPuzzleAttemptRecord unresolved =
      test_puzzle_progress_make_record("attempt-failure", CHECKERS_PUZZLE_ATTEMPT_RESULT_UNRESOLVED);
  g_autoptr(GError) error = NULL;
  g_assert_true(checkers_puzzle_progress_store_append_attempt(store, &unresolved, &error));
  g_assert_no_error(error);

  CheckersPuzzleAttemptRecord failure =
      test_puzzle_progress_make_record("attempt-failure", CHECKERS_PUZZLE_ATTEMPT_RESULT_FAILURE);
  failure.failure_on_first_move = TRUE;
  failure.has_failed_first_move = TRUE;
  failure.failed_first_move.length = 2;
  failure.failed_first_move.path[0] = 12;
  failure.failed_first_move.path[1] = 16;
  g_assert_true(checkers_puzzle_progress_store_replace_attempt(store, &failure, &error));
  g_assert_no_error(error);

  g_autoptr(GPtrArray) history = checkers_puzzle_progress_store_load_attempt_history(store, &error);
  g_assert_no_error(error);
  CheckersPuzzleAttemptRecord *stored = g_ptr_array_index(history, 0);
  g_assert_true(stored->failure_on_first_move);
  g_assert_true(stored->has_failed_first_move);
  g_assert_cmpuint(stored->failed_first_move.length, ==, 2);
  g_assert_cmpuint(stored->failed_first_move.path[0], ==, 12);
  g_assert_cmpuint(stored->failed_first_move.path[1], ==, 16);

  checkers_puzzle_attempt_record_clear(&unresolved);
  checkers_puzzle_attempt_record_clear(&failure);
  checkers_puzzle_progress_store_unref(store);
  g_unsetenv("GCHECKERS_PUZZLE_PROGRESS_DIR");
}

static void test_puzzle_progress_analyze_persists(void) {
  g_autofree char *state_dir = test_puzzle_progress_make_state_dir();
  CheckersPuzzleProgressStore *store = checkers_puzzle_progress_store_new(state_dir);

  CheckersPuzzleAttemptRecord unresolved =
      test_puzzle_progress_make_record("attempt-analyze", CHECKERS_PUZZLE_ATTEMPT_RESULT_UNRESOLVED);
  g_autoptr(GError) error = NULL;
  g_assert_true(checkers_puzzle_progress_store_append_attempt(store, &unresolved, &error));
  g_assert_no_error(error);

  CheckersPuzzleAttemptRecord analyze =
      test_puzzle_progress_make_record("attempt-analyze", CHECKERS_PUZZLE_ATTEMPT_RESULT_ANALYZE);
  g_assert_true(checkers_puzzle_progress_store_replace_attempt(store, &analyze, &error));
  g_assert_no_error(error);

  g_autoptr(GPtrArray) history = checkers_puzzle_progress_store_load_attempt_history(store, &error);
  g_assert_no_error(error);
  CheckersPuzzleAttemptRecord *stored = g_ptr_array_index(history, 0);
  g_assert_cmpint(stored->result, ==, CHECKERS_PUZZLE_ATTEMPT_RESULT_ANALYZE);
  g_assert_false(stored->has_failed_first_move);

  checkers_puzzle_attempt_record_clear(&unresolved);
  checkers_puzzle_attempt_record_clear(&analyze);
  checkers_puzzle_progress_store_unref(store);
  g_unsetenv("GCHECKERS_PUZZLE_PROGRESS_DIR");
}

static void test_puzzle_progress_collect_unsent_and_thresholds(void) {
  g_autoptr(GPtrArray) history =
      g_ptr_array_new_with_free_func((GDestroyNotify)checkers_puzzle_attempt_record_free);

  for (guint i = 0; i < 9; i++) {
    g_autofree char *attempt_id = g_strdup_printf("attempt-%u", i);
    CheckersPuzzleAttemptRecord record =
        test_puzzle_progress_make_record(attempt_id, CHECKERS_PUZZLE_ATTEMPT_RESULT_SUCCESS);
    record.attempt_id = g_strdup_printf("attempt-%u", i);
    record.started_unix_ms += i;
    record.finished_unix_ms += i;
    g_ptr_array_add(history, checkers_puzzle_attempt_record_copy(&record));
    checkers_puzzle_attempt_record_clear(&record);
  }

  g_assert_false(checkers_puzzle_progress_should_send_report(history, 1713300005000));

  CheckersPuzzleAttemptRecord tenth =
      test_puzzle_progress_make_record("attempt-10", CHECKERS_PUZZLE_ATTEMPT_RESULT_SUCCESS);
  g_ptr_array_add(history, checkers_puzzle_attempt_record_copy(&tenth));
  g_assert_true(checkers_puzzle_progress_should_send_report(history, 1713300005000));
  checkers_puzzle_attempt_record_clear(&tenth);

  g_ptr_array_set_size(history, 0);
  for (guint i = 0; i < 5; i++) {
    CheckersPuzzleAttemptRecord record =
        test_puzzle_progress_make_record("stale", CHECKERS_PUZZLE_ATTEMPT_RESULT_SUCCESS);
    record.attempt_id = g_strdup_printf("stale-%u", i);
    record.started_unix_ms = 1000 + i;
    record.finished_unix_ms = 2000 + i;
    g_ptr_array_add(history, checkers_puzzle_attempt_record_copy(&record));
    checkers_puzzle_attempt_record_clear(&record);
  }
  g_assert_true(checkers_puzzle_progress_should_send_report(history,
                                                            2000 + 24 * 60 * 60 * G_GINT64_CONSTANT(1000)));
}

static void test_puzzle_progress_mark_reported_without_deleting_history(void) {
  g_autofree char *state_dir = test_puzzle_progress_make_state_dir();
  CheckersPuzzleProgressStore *store = checkers_puzzle_progress_store_new(state_dir);
  g_autoptr(GError) error = NULL;

  CheckersPuzzleAttemptRecord first =
      test_puzzle_progress_make_record("attempt-one", CHECKERS_PUZZLE_ATTEMPT_RESULT_SUCCESS);
  CheckersPuzzleAttemptRecord second =
      test_puzzle_progress_make_record("attempt-two", CHECKERS_PUZZLE_ATTEMPT_RESULT_ANALYZE);
  second.report_count = 1;
  second.first_reported_unix_ms = 1713300008000;

  g_assert_true(checkers_puzzle_progress_store_append_attempt(store, &first, &error));
  g_assert_no_error(error);
  g_assert_true(checkers_puzzle_progress_store_append_attempt(store, &second, &error));
  g_assert_no_error(error);

  g_assert_true(checkers_puzzle_progress_store_mark_reported(store, 1713300009000, &error));
  g_assert_no_error(error);

  g_autoptr(GPtrArray) history = checkers_puzzle_progress_store_load_attempt_history(store, &error);
  g_assert_no_error(error);
  g_assert_cmpuint(history->len, ==, 2);
  CheckersPuzzleAttemptRecord *stored_first = g_ptr_array_index(history, 0);
  CheckersPuzzleAttemptRecord *stored_second = g_ptr_array_index(history, 1);
  g_assert_cmpuint(stored_first->report_count, ==, 1);
  g_assert_cmpint(stored_first->first_reported_unix_ms, ==, 1713300009000);
  g_assert_cmpuint(stored_second->report_count, ==, 1);
  g_assert_cmpint(stored_second->first_reported_unix_ms, ==, 1713300008000);

  checkers_puzzle_attempt_record_clear(&first);
  checkers_puzzle_attempt_record_clear(&second);
  checkers_puzzle_progress_store_unref(store);
  g_unsetenv("GCHECKERS_PUZZLE_PROGRESS_DIR");
}

static void test_puzzle_progress_build_upload_json_formats_expected_records(void) {
  g_autoptr(GPtrArray) history =
      g_ptr_array_new_with_free_func((GDestroyNotify)checkers_puzzle_attempt_record_free);

  CheckersPuzzleAttemptRecord success =
      test_puzzle_progress_make_record("attempt-success", CHECKERS_PUZZLE_ATTEMPT_RESULT_SUCCESS);
  CheckersPuzzleAttemptRecord failure =
      test_puzzle_progress_make_record("attempt-failure", CHECKERS_PUZZLE_ATTEMPT_RESULT_FAILURE);
  failure.failure_on_first_move = TRUE;
  failure.has_failed_first_move = TRUE;
  failure.failed_first_move.length = 2;
  failure.failed_first_move.path[0] = 12;
  failure.failed_first_move.path[1] = 16;
  CheckersPuzzleAttemptRecord analyze =
      test_puzzle_progress_make_record("attempt-analyze", CHECKERS_PUZZLE_ATTEMPT_RESULT_ANALYZE);

  g_ptr_array_add(history, checkers_puzzle_attempt_record_copy(&success));
  g_ptr_array_add(history, checkers_puzzle_attempt_record_copy(&failure));
  g_ptr_array_add(history, checkers_puzzle_attempt_record_copy(&analyze));

  g_autofree char *json = checkers_puzzle_progress_build_upload_json("user-123", history);
  g_assert_nonnull(json);
  g_assert_nonnull(strstr(json, "\"user_id\":\"user-123\""));
  g_assert_nonnull(strstr(json, "\"result\":\"success\""));
  g_assert_nonnull(strstr(json, "\"result\":\"failure\""));
  g_assert_nonnull(strstr(json, "\"result\":\"analyze\""));
  g_assert_nonnull(strstr(json, "\"failed_first_move\":{\"length\":2,\"captures\":0,\"path\":[12,16]}"));

  checkers_puzzle_attempt_record_clear(&success);
  checkers_puzzle_attempt_record_clear(&failure);
  checkers_puzzle_attempt_record_clear(&analyze);
}

static void test_puzzle_progress_empty_history_is_safe(void) {
  g_autofree char *state_dir = test_puzzle_progress_make_state_dir();
  CheckersPuzzleProgressStore *store = checkers_puzzle_progress_store_new(state_dir);

  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) history = checkers_puzzle_progress_store_load_attempt_history(store, &error);
  g_assert_no_error(error);
  g_assert_cmpuint(history->len, ==, 0);

  g_autoptr(GPtrArray) unsent = checkers_puzzle_progress_store_collect_unsent_attempts(store, &error);
  g_assert_no_error(error);
  g_assert_cmpuint(unsent->len, ==, 0);
  g_assert_false(checkers_puzzle_progress_should_send_report(history, g_get_real_time() / 1000));

  checkers_puzzle_progress_store_unref(store);
  g_unsetenv("GCHECKERS_PUZZLE_PROGRESS_DIR");
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/puzzle-progress/user-id-persists", test_puzzle_progress_user_id_persists);
  g_test_add_func("/puzzle-progress/append-and-replace-success", test_puzzle_progress_append_and_replace_success);
  g_test_add_func("/puzzle-progress/first-move-failure-persists", test_puzzle_progress_first_move_failure_persists);
  g_test_add_func("/puzzle-progress/analyze-persists", test_puzzle_progress_analyze_persists);
  g_test_add_func("/puzzle-progress/unsent-and-thresholds", test_puzzle_progress_collect_unsent_and_thresholds);
  g_test_add_func("/puzzle-progress/mark-reported", test_puzzle_progress_mark_reported_without_deleting_history);
  g_test_add_func("/puzzle-progress/build-upload-json", test_puzzle_progress_build_upload_json_formats_expected_records);
  g_test_add_func("/puzzle-progress/empty-history", test_puzzle_progress_empty_history_is_safe);
  return g_test_run();
}
