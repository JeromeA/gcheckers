#include "../src/app_paths.h"
#include "../src/puzzle_progress.h"
#include "test_profile_utils.h"

#include <glib.h>

static char *test_puzzle_progress_make_state_dir(void) {
  g_autoptr(GError) error = NULL;
  g_autofree char *root = g_dir_make_tmp("gcheckers-puzzle-progress-XXXXXX", &error);
  g_assert_no_error(error);
  g_assert_nonnull(root);

  g_setenv("GCHECKERS_PUZZLE_PROGRESS_DIR", root, TRUE);
  char *state_dir =
      ggame_app_paths_get_user_state_subdir("GCHECKERS_PUZZLE_PROGRESS_DIR", "puzzle-progress", &error);
  g_assert_no_error(error);
  g_assert_nonnull(state_dir);
  return state_dir;
}

static GGamePuzzleAttemptRecord test_puzzle_progress_make_record(const char *attempt_id,
                                                                    GGamePuzzleAttemptResult result) {
  GGamePuzzleAttemptRecord record = {0};
  record.attempt_id = g_strdup(attempt_id);
  record.puzzle_id = g_strdup("checkers/international/puzzle-0007.sgf");
  record.puzzle_number = 7;
  record.puzzle_source_name = g_strdup("puzzle-0007.sgf");
  record.puzzle_variant = g_strdup("international");
  record.attacker_side = 0;
  record.started_unix_ms = 1713300000000;
  record.finished_unix_ms = result == GGAME_PUZZLE_ATTEMPT_RESULT_UNRESOLVED ? 0 : 1713300005000;
  record.result = result;
  record.failure_on_first_move = FALSE;
  record.has_failed_first_move = FALSE;
  record.first_reported_unix_ms = 0;
  record.report_count = 0;
  return record;
}

static GGamePuzzleStatusEntry *test_puzzle_progress_lookup_status(GHashTable *status_map, const char *puzzle_id) {
  g_return_val_if_fail(status_map != NULL, NULL);
  g_return_val_if_fail(puzzle_id != NULL, NULL);

  return g_hash_table_lookup(status_map, puzzle_id);
}

static void test_puzzle_progress_user_id_persists(void) {
  g_setenv("GSETTINGS_BACKEND", "memory", TRUE);
  g_autofree char *state_dir = test_puzzle_progress_make_state_dir();
  GGamePuzzleProgressStore *store = ggame_puzzle_progress_store_new(state_dir);
  g_assert_nonnull(store);

  g_autoptr(GError) error = NULL;
  g_autofree char *first_id = ggame_puzzle_progress_store_get_or_create_user_id(store, &error);
  g_assert_no_error(error);
  g_assert_nonnull(first_id);

  g_autofree char *second_id = ggame_puzzle_progress_store_get_or_create_user_id(store, &error);
  g_assert_no_error(error);
  g_assert_cmpstr(first_id, ==, second_id);

  ggame_puzzle_progress_store_unref(store);
  g_unsetenv("GCHECKERS_PUZZLE_PROGRESS_DIR");
}

static void test_puzzle_progress_append_and_replace_success(void) {
  g_autofree char *state_dir = test_puzzle_progress_make_state_dir();
  GGamePuzzleProgressStore *store = ggame_puzzle_progress_store_new(state_dir);

  GGamePuzzleAttemptRecord unresolved =
      test_puzzle_progress_make_record("attempt-success", GGAME_PUZZLE_ATTEMPT_RESULT_UNRESOLVED);
  g_autoptr(GError) error = NULL;
  g_assert_true(ggame_puzzle_progress_store_append_attempt(store, &unresolved, &error));
  g_assert_no_error(error);

  GGamePuzzleAttemptRecord success =
      test_puzzle_progress_make_record("attempt-success", GGAME_PUZZLE_ATTEMPT_RESULT_SUCCESS);
  g_assert_true(ggame_puzzle_progress_store_replace_attempt(store, &success, &error));
  g_assert_no_error(error);

  g_autoptr(GPtrArray) history = ggame_puzzle_progress_store_load_attempt_history(store, &error);
  g_assert_no_error(error);
  g_assert_cmpuint(history->len, ==, 1);
  GGamePuzzleAttemptRecord *stored = g_ptr_array_index(history, 0);
  g_assert_cmpint(stored->result, ==, GGAME_PUZZLE_ATTEMPT_RESULT_SUCCESS);
  g_assert_false(stored->failure_on_first_move);

  ggame_puzzle_attempt_record_clear(&unresolved);
  ggame_puzzle_attempt_record_clear(&success);
  ggame_puzzle_progress_store_unref(store);
  g_unsetenv("GCHECKERS_PUZZLE_PROGRESS_DIR");
}

static void test_puzzle_progress_first_move_failure_persists(void) {
  g_autofree char *state_dir = test_puzzle_progress_make_state_dir();
  GGamePuzzleProgressStore *store = ggame_puzzle_progress_store_new(state_dir);

  GGamePuzzleAttemptRecord unresolved =
      test_puzzle_progress_make_record("attempt-failure", GGAME_PUZZLE_ATTEMPT_RESULT_UNRESOLVED);
  g_autoptr(GError) error = NULL;
  g_assert_true(ggame_puzzle_progress_store_append_attempt(store, &unresolved, &error));
  g_assert_no_error(error);

  GGamePuzzleAttemptRecord failure =
      test_puzzle_progress_make_record("attempt-failure", GGAME_PUZZLE_ATTEMPT_RESULT_FAILURE);
  failure.failure_on_first_move = TRUE;
  failure.has_failed_first_move = TRUE;
  failure.failed_first_move_text = g_strdup("13-17");
  g_assert_true(ggame_puzzle_progress_store_replace_attempt(store, &failure, &error));
  g_assert_no_error(error);

  g_autoptr(GPtrArray) history = ggame_puzzle_progress_store_load_attempt_history(store, &error);
  g_assert_no_error(error);
  GGamePuzzleAttemptRecord *stored = g_ptr_array_index(history, 0);
  g_assert_true(stored->failure_on_first_move);
  g_assert_true(stored->has_failed_first_move);
  g_assert_cmpstr(stored->failed_first_move_text, ==, "13-17");

  ggame_puzzle_attempt_record_clear(&unresolved);
  ggame_puzzle_attempt_record_clear(&failure);
  ggame_puzzle_progress_store_unref(store);
  g_unsetenv("GCHECKERS_PUZZLE_PROGRESS_DIR");
}

static void test_puzzle_progress_analyze_persists(void) {
  g_autofree char *state_dir = test_puzzle_progress_make_state_dir();
  GGamePuzzleProgressStore *store = ggame_puzzle_progress_store_new(state_dir);

  GGamePuzzleAttemptRecord unresolved =
      test_puzzle_progress_make_record("attempt-analyze", GGAME_PUZZLE_ATTEMPT_RESULT_UNRESOLVED);
  g_autoptr(GError) error = NULL;
  g_assert_true(ggame_puzzle_progress_store_append_attempt(store, &unresolved, &error));
  g_assert_no_error(error);

  GGamePuzzleAttemptRecord analyze =
      test_puzzle_progress_make_record("attempt-analyze", GGAME_PUZZLE_ATTEMPT_RESULT_ANALYZE);
  g_assert_true(ggame_puzzle_progress_store_replace_attempt(store, &analyze, &error));
  g_assert_no_error(error);

  g_autoptr(GPtrArray) history = ggame_puzzle_progress_store_load_attempt_history(store, &error);
  g_assert_no_error(error);
  GGamePuzzleAttemptRecord *stored = g_ptr_array_index(history, 0);
  g_assert_cmpint(stored->result, ==, GGAME_PUZZLE_ATTEMPT_RESULT_ANALYZE);
  g_assert_false(stored->has_failed_first_move);

  ggame_puzzle_attempt_record_clear(&unresolved);
  ggame_puzzle_attempt_record_clear(&analyze);
  ggame_puzzle_progress_store_unref(store);
  g_unsetenv("GCHECKERS_PUZZLE_PROGRESS_DIR");
}

static void test_puzzle_progress_collect_unsent_and_thresholds(void) {
  g_autoptr(GPtrArray) history =
      g_ptr_array_new_with_free_func((GDestroyNotify)ggame_puzzle_attempt_record_free);

  for (guint i = 0; i < 9; i++) {
    g_autofree char *attempt_id = g_strdup_printf("attempt-%u", i);
    GGamePuzzleAttemptRecord record =
        test_puzzle_progress_make_record(attempt_id, GGAME_PUZZLE_ATTEMPT_RESULT_SUCCESS);
    record.attempt_id = g_strdup_printf("attempt-%u", i);
    record.started_unix_ms += i;
    record.finished_unix_ms += i;
    g_ptr_array_add(history, ggame_puzzle_attempt_record_copy(&record));
    ggame_puzzle_attempt_record_clear(&record);
  }

  g_assert_false(ggame_puzzle_progress_should_send_report(history, 1713300005000));

  GGamePuzzleAttemptRecord tenth =
      test_puzzle_progress_make_record("attempt-10", GGAME_PUZZLE_ATTEMPT_RESULT_SUCCESS);
  g_ptr_array_add(history, ggame_puzzle_attempt_record_copy(&tenth));
  g_assert_true(ggame_puzzle_progress_should_send_report(history, 1713300005000));
  ggame_puzzle_attempt_record_clear(&tenth);

  g_ptr_array_set_size(history, 0);
  for (guint i = 0; i < 5; i++) {
    GGamePuzzleAttemptRecord record =
        test_puzzle_progress_make_record("stale", GGAME_PUZZLE_ATTEMPT_RESULT_SUCCESS);
    record.attempt_id = g_strdup_printf("stale-%u", i);
    record.started_unix_ms = 1000 + i;
    record.finished_unix_ms = 2000 + i;
    g_ptr_array_add(history, ggame_puzzle_attempt_record_copy(&record));
    ggame_puzzle_attempt_record_clear(&record);
  }
  g_assert_true(ggame_puzzle_progress_should_send_report(history,
                                                            2000 + 24 * 60 * 60 * G_GINT64_CONSTANT(1000)));
}

static void test_puzzle_progress_mark_reported_without_deleting_history(void) {
  g_autofree char *state_dir = test_puzzle_progress_make_state_dir();
  GGamePuzzleProgressStore *store = ggame_puzzle_progress_store_new(state_dir);
  g_autoptr(GError) error = NULL;

  GGamePuzzleAttemptRecord first =
      test_puzzle_progress_make_record("attempt-one", GGAME_PUZZLE_ATTEMPT_RESULT_SUCCESS);
  GGamePuzzleAttemptRecord second =
      test_puzzle_progress_make_record("attempt-two", GGAME_PUZZLE_ATTEMPT_RESULT_ANALYZE);
  second.report_count = 1;
  second.first_reported_unix_ms = 1713300008000;

  g_assert_true(ggame_puzzle_progress_store_append_attempt(store, &first, &error));
  g_assert_no_error(error);
  g_assert_true(ggame_puzzle_progress_store_append_attempt(store, &second, &error));
  g_assert_no_error(error);

  g_assert_true(ggame_puzzle_progress_store_mark_reported(store, 1713300009000, &error));
  g_assert_no_error(error);

  g_autoptr(GPtrArray) history = ggame_puzzle_progress_store_load_attempt_history(store, &error);
  g_assert_no_error(error);
  g_assert_cmpuint(history->len, ==, 2);
  GGamePuzzleAttemptRecord *stored_first = g_ptr_array_index(history, 0);
  GGamePuzzleAttemptRecord *stored_second = g_ptr_array_index(history, 1);
  g_assert_cmpuint(stored_first->report_count, ==, 1);
  g_assert_cmpint(stored_first->first_reported_unix_ms, ==, 1713300009000);
  g_assert_cmpuint(stored_second->report_count, ==, 1);
  g_assert_cmpint(stored_second->first_reported_unix_ms, ==, 1713300008000);

  ggame_puzzle_attempt_record_clear(&first);
  ggame_puzzle_attempt_record_clear(&second);
  ggame_puzzle_progress_store_unref(store);
  g_unsetenv("GCHECKERS_PUZZLE_PROGRESS_DIR");
}

static void test_puzzle_progress_build_upload_json_formats_expected_records(void) {
  g_autoptr(GPtrArray) history =
      g_ptr_array_new_with_free_func((GDestroyNotify)ggame_puzzle_attempt_record_free);

  GGamePuzzleAttemptRecord success =
      test_puzzle_progress_make_record("attempt-success", GGAME_PUZZLE_ATTEMPT_RESULT_SUCCESS);
  GGamePuzzleAttemptRecord failure =
      test_puzzle_progress_make_record("attempt-failure", GGAME_PUZZLE_ATTEMPT_RESULT_FAILURE);
  failure.failure_on_first_move = TRUE;
  failure.has_failed_first_move = TRUE;
  failure.failed_first_move_text = g_strdup("13-17");
  GGamePuzzleAttemptRecord analyze =
      test_puzzle_progress_make_record("attempt-analyze", GGAME_PUZZLE_ATTEMPT_RESULT_ANALYZE);

  g_ptr_array_add(history, ggame_puzzle_attempt_record_copy(&success));
  g_ptr_array_add(history, ggame_puzzle_attempt_record_copy(&failure));
  g_ptr_array_add(history, ggame_puzzle_attempt_record_copy(&analyze));

  g_autofree char *json = ggame_puzzle_progress_build_upload_json("user-123", history);
  g_assert_nonnull(json);
  g_assert_nonnull(strstr(json, "\"user_id\":\"user-123\""));
  g_assert_nonnull(strstr(json, "\"result\":\"success\""));
  g_assert_nonnull(strstr(json, "\"result\":\"failure\""));
  g_assert_nonnull(strstr(json, "\"result\":\"analyze\""));
  g_assert_nonnull(strstr(json, "\"failed_first_move\":\"13-17\""));

  ggame_puzzle_attempt_record_clear(&success);
  ggame_puzzle_attempt_record_clear(&failure);
  ggame_puzzle_attempt_record_clear(&analyze);
}

static void test_puzzle_progress_reduce_status_prefers_success(void) {
  g_autoptr(GPtrArray) history =
      g_ptr_array_new_with_free_func((GDestroyNotify)ggame_puzzle_attempt_record_free);

  GGamePuzzleAttemptRecord failure =
      test_puzzle_progress_make_record("attempt-failure", GGAME_PUZZLE_ATTEMPT_RESULT_FAILURE);
  GGamePuzzleAttemptRecord success =
      test_puzzle_progress_make_record("attempt-success", GGAME_PUZZLE_ATTEMPT_RESULT_SUCCESS);
  g_ptr_array_add(history, ggame_puzzle_attempt_record_copy(&failure));
  g_ptr_array_add(history, ggame_puzzle_attempt_record_copy(&success));

  g_assert_cmpint(ggame_puzzle_progress_reduce_status_for_attempts(history, "checkers/international/puzzle-0007.sgf"),
                  ==,
                  GGAME_PUZZLE_STATUS_SOLVED);

  ggame_puzzle_attempt_record_clear(&failure);
  ggame_puzzle_attempt_record_clear(&success);
}

static void test_puzzle_progress_status_map_tracks_terminal_results(void) {
  g_autofree char *state_dir = test_puzzle_progress_make_state_dir();
  GGamePuzzleProgressStore *store = ggame_puzzle_progress_store_new(state_dir);
  g_autoptr(GError) error = NULL;

  GGamePuzzleAttemptRecord unresolved =
      test_puzzle_progress_make_record("attempt-status", GGAME_PUZZLE_ATTEMPT_RESULT_UNRESOLVED);
  g_assert_true(ggame_puzzle_progress_store_append_attempt(store, &unresolved, &error));
  g_assert_no_error(error);

  g_autoptr(GHashTable) status_map = ggame_puzzle_progress_store_load_status_map(store, &error);
  g_assert_no_error(error);
  g_assert_null(test_puzzle_progress_lookup_status(status_map, "checkers/international/puzzle-0007.sgf"));

  GGamePuzzleAttemptRecord analyze =
      test_puzzle_progress_make_record("attempt-status", GGAME_PUZZLE_ATTEMPT_RESULT_ANALYZE);
  g_assert_true(ggame_puzzle_progress_store_replace_attempt(store, &analyze, &error));
  g_assert_no_error(error);

  g_clear_pointer(&status_map, g_hash_table_unref);
  status_map = ggame_puzzle_progress_store_load_status_map(store, &error);
  g_assert_no_error(error);
  GGamePuzzleStatusEntry *entry =
      test_puzzle_progress_lookup_status(status_map, "checkers/international/puzzle-0007.sgf");
  g_assert_nonnull(entry);
  g_assert_cmpint(entry->status, ==, GGAME_PUZZLE_STATUS_FAILED);

  GGamePuzzleAttemptRecord success =
      test_puzzle_progress_make_record("attempt-status", GGAME_PUZZLE_ATTEMPT_RESULT_SUCCESS);
  g_assert_true(ggame_puzzle_progress_store_replace_attempt(store, &success, &error));
  g_assert_no_error(error);

  g_clear_pointer(&status_map, g_hash_table_unref);
  status_map = ggame_puzzle_progress_store_load_status_map(store, &error);
  g_assert_no_error(error);
  entry = test_puzzle_progress_lookup_status(status_map, "checkers/international/puzzle-0007.sgf");
  g_assert_nonnull(entry);
  g_assert_cmpint(entry->status, ==, GGAME_PUZZLE_STATUS_SOLVED);

  ggame_puzzle_attempt_record_clear(&unresolved);
  ggame_puzzle_attempt_record_clear(&analyze);
  ggame_puzzle_attempt_record_clear(&success);
  ggame_puzzle_progress_store_unref(store);
  g_unsetenv("GCHECKERS_PUZZLE_PROGRESS_DIR");
}

static void test_puzzle_progress_status_map_rebuilds_from_corrupt_cache(void) {
  g_autofree char *state_dir = test_puzzle_progress_make_state_dir();
  GGamePuzzleProgressStore *store = ggame_puzzle_progress_store_new(state_dir);
  g_autoptr(GError) error = NULL;

  GGamePuzzleAttemptRecord success =
      test_puzzle_progress_make_record("attempt-corrupt", GGAME_PUZZLE_ATTEMPT_RESULT_SUCCESS);
  g_assert_true(ggame_puzzle_progress_store_append_attempt(store, &success, &error));
  g_assert_no_error(error);

  g_autofree char *status_path = ggame_puzzle_progress_store_get_status_path(store);
  g_assert_true(g_file_set_contents(status_path, "{not-json", -1, &error));
  g_assert_no_error(error);

  g_autoptr(GHashTable) status_map = ggame_puzzle_progress_store_load_status_map(store, &error);
  g_assert_no_error(error);
  GGamePuzzleStatusEntry *entry =
      test_puzzle_progress_lookup_status(status_map, "checkers/international/puzzle-0007.sgf");
  g_assert_nonnull(entry);
  g_assert_cmpint(entry->status, ==, GGAME_PUZZLE_STATUS_SOLVED);

  g_autofree char *rebuilt_text = NULL;
  g_assert_true(g_file_get_contents(status_path, &rebuilt_text, NULL, &error));
  g_assert_no_error(error);
  g_assert_nonnull(strstr(rebuilt_text, "\"status\":\"solved\""));

  ggame_puzzle_attempt_record_clear(&success);
  ggame_puzzle_progress_store_unref(store);
  g_unsetenv("GCHECKERS_PUZZLE_PROGRESS_DIR");
}

static void test_puzzle_progress_clear_progress_removes_history_and_status(void) {
  g_autofree char *state_dir = test_puzzle_progress_make_state_dir();
  GGamePuzzleProgressStore *store = ggame_puzzle_progress_store_new(state_dir);
  g_assert_nonnull(store);

  GGamePuzzleAttemptRecord success =
      test_puzzle_progress_make_record("attempt-clear", GGAME_PUZZLE_ATTEMPT_RESULT_SUCCESS);
  g_autoptr(GError) error = NULL;
  g_assert_true(ggame_puzzle_progress_store_append_attempt(store, &success, &error));
  g_assert_no_error(error);

  g_autoptr(GHashTable) status_map = ggame_puzzle_progress_store_load_status_map(store, &error);
  g_assert_no_error(error);
  g_assert_nonnull(test_puzzle_progress_lookup_status(status_map, "checkers/international/puzzle-0007.sgf"));

  g_assert_true(ggame_puzzle_progress_store_clear_progress(store, &error));
  g_assert_no_error(error);

  g_autoptr(GPtrArray) history = ggame_puzzle_progress_store_load_attempt_history(store, &error);
  g_assert_no_error(error);
  g_assert_cmpuint(history->len, ==, 0);

  g_clear_pointer(&status_map, g_hash_table_unref);
  status_map = ggame_puzzle_progress_store_load_status_map(store, &error);
  g_assert_no_error(error);
  g_assert_cmpuint(g_hash_table_size(status_map), ==, 0);

  ggame_puzzle_attempt_record_clear(&success);
  ggame_puzzle_progress_store_unref(store);
  g_unsetenv("GCHECKERS_PUZZLE_PROGRESS_DIR");
}

static void test_puzzle_progress_empty_history_is_safe(void) {
  g_autofree char *state_dir = test_puzzle_progress_make_state_dir();
  GGamePuzzleProgressStore *store = ggame_puzzle_progress_store_new(state_dir);

  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) history = ggame_puzzle_progress_store_load_attempt_history(store, &error);
  g_assert_no_error(error);
  g_assert_cmpuint(history->len, ==, 0);

  g_autoptr(GPtrArray) unsent = ggame_puzzle_progress_store_collect_unsent_attempts(store, &error);
  g_assert_no_error(error);
  g_assert_cmpuint(unsent->len, ==, 0);
  g_assert_false(ggame_puzzle_progress_should_send_report(history, g_get_real_time() / 1000));

  ggame_puzzle_progress_store_unref(store);
  g_unsetenv("GCHECKERS_PUZZLE_PROGRESS_DIR");
}

int main(int argc, char **argv) {
  ggame_test_init_profile(&argc, &argv, "checkers");
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/puzzle-progress/user-id-persists", test_puzzle_progress_user_id_persists);
  g_test_add_func("/puzzle-progress/append-and-replace-success", test_puzzle_progress_append_and_replace_success);
  g_test_add_func("/puzzle-progress/first-move-failure-persists", test_puzzle_progress_first_move_failure_persists);
  g_test_add_func("/puzzle-progress/analyze-persists", test_puzzle_progress_analyze_persists);
  g_test_add_func("/puzzle-progress/unsent-and-thresholds", test_puzzle_progress_collect_unsent_and_thresholds);
  g_test_add_func("/puzzle-progress/mark-reported", test_puzzle_progress_mark_reported_without_deleting_history);
  g_test_add_func("/puzzle-progress/build-upload-json", test_puzzle_progress_build_upload_json_formats_expected_records);
  g_test_add_func("/puzzle-progress/reduce-status-prefers-success",
                  test_puzzle_progress_reduce_status_prefers_success);
  g_test_add_func("/puzzle-progress/status-map-tracks-terminal-results",
                  test_puzzle_progress_status_map_tracks_terminal_results);
  g_test_add_func("/puzzle-progress/status-map-rebuilds-from-corrupt-cache",
                  test_puzzle_progress_status_map_rebuilds_from_corrupt_cache);
  g_test_add_func("/puzzle-progress/clear-progress", test_puzzle_progress_clear_progress_removes_history_and_status);
  g_test_add_func("/puzzle-progress/empty-history", test_puzzle_progress_empty_history_is_safe);
  return g_test_run();
}
