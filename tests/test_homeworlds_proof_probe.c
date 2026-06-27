#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

#ifndef HOMEWORLDS_PROOF_PROBE_PATH
#error "HOMEWORLDS_PROOF_PROBE_PATH must be defined"
#endif

static gchar *test_homeworlds_proof_probe_create_report(void) {
  const char *content =
      "Move report after 2 replayed moves:\n"
      "moves:\n"
      "1. G1R2b3\n"
      "2. Y3G2b3\n"
      "\n"
      "position:\n"
      "H2: b3 Y3G2 -\n"
      "\n"
      "H1: - G1R2 b3\n"
      "\n"
      "good_moves:\n"
      "1. H1b+\n"
      "good_moves_count: 1 move\n";
  g_autoptr(GError) error = NULL;
  gchar *path = NULL;
  gint fd = g_file_open_tmp("homeworlds-proof-probe-XXXXXX.txt", &path, &error);

  g_assert_no_error(error);
  g_assert_cmpint(fd, >=, 0);
  g_assert_cmpint(close(fd), ==, 0);
  g_assert_true(g_file_set_contents(path, content, -1, &error));
  g_assert_no_error(error);
  return path;
}

static void test_homeworlds_proof_probe_reads_report_row(void) {
  g_autofree gchar *report_path = test_homeworlds_proof_probe_create_report();
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  g_autoptr(GError) error = NULL;
  gint wait_status = 0;
  gchar *argv[] = {
    (gchar *)HOMEWORLDS_PROOF_PROBE_PATH,
    report_path,
    (gchar *)"1",
    NULL,
  };

  g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_assert_no_error(error);
  g_assert_true(g_spawn_check_wait_status(wait_status, &error));
  g_assert_no_error(error);
  g_assert_cmpstr(stderr_text, ==, "");
  g_assert_nonnull(strstr(stdout_text, "cutoff="));
  g_assert_nonnull(strstr(stdout_text, "trace: generated="));
  g_assert_nonnull(strstr(stdout_text, "\n1. H1b+\n"));
  g_assert_nonnull(strstr(stdout_text, "after H1b+"));
  g_assert_nonnull(strstr(stdout_text, "proof=not-active"));
  g_assert_nonnull(strstr(stdout_text, "complete: final_score="));
  g_assert_cmpint(g_remove(report_path), ==, 0);
}

static void test_homeworlds_proof_probe_prints_iteration_report(void) {
  g_autofree gchar *report_path = test_homeworlds_proof_probe_create_report();
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  g_autoptr(GError) error = NULL;
  gint wait_status = 0;
  gchar *argv[] = {
    (gchar *)HOMEWORLDS_PROOF_PROBE_PATH,
    (gchar *)"--iterations=12",
    report_path,
    NULL,
  };

  g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_assert_no_error(error);
  g_assert_true(g_spawn_check_wait_status(wait_status, &error));
  g_assert_no_error(error);
  g_assert_cmpstr(stderr_text, ==, "");
  g_assert_nonnull(strstr(stdout_text, "position:\n"));
  g_assert_nonnull(strstr(stdout_text, "Bank: "));
  g_assert_nonnull(strstr(stdout_text, "cutoff="));
  g_assert_nonnull(strstr(stdout_text, "#0 expansion:"));
  g_assert_nonnull(strstr(stdout_text, "directly collected root single-step moves"));
  g_assert_nonnull(strstr(stdout_text, "inside interval"));
  g_assert_null(strstr(stdout_text, "root expansion:"));
  g_assert_null(strstr(stdout_text, "Selected branches after #0 expansion"));
  g_assert_null(strstr(stdout_text, "finished traversal with selected score filter"));
  g_assert_null(strstr(stdout_text, "recursive pruning covered some descendants"));
  g_assert_null(strstr(stdout_text, "same-shape branches"));
  g_assert_null(strstr(stdout_text, "parent_" "delta"));
  g_assert_cmpint(g_remove(report_path), ==, 0);
}

static void test_homeworlds_proof_probe_rejects_missing_report_row(void) {
  g_autofree gchar *report_path = test_homeworlds_proof_probe_create_report();
  g_autofree gchar *stdout_text = NULL;
  g_autofree gchar *stderr_text = NULL;
  g_autoptr(GError) error = NULL;
  gint wait_status = 0;
  gchar *argv[] = {
    (gchar *)HOMEWORLDS_PROOF_PROBE_PATH,
    report_path,
    (gchar *)"2",
    NULL,
  };

  g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
                             &stdout_text, &stderr_text, &wait_status, &error));
  g_assert_no_error(error);
  g_assert_false(g_spawn_check_wait_status(wait_status, NULL));
  g_assert_cmpstr(stdout_text, ==, "");
  g_assert_nonnull(strstr(stderr_text, "requested report move rows were not found"));
  g_assert_cmpint(g_remove(report_path), ==, 0);
}

int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/homeworlds-proof-probe/reads-report-row", test_homeworlds_proof_probe_reads_report_row);
  g_test_add_func("/homeworlds-proof-probe/prints-iteration-report",
                  test_homeworlds_proof_probe_prints_iteration_report);
  g_test_add_func("/homeworlds-proof-probe/rejects-missing-report-row",
                  test_homeworlds_proof_probe_rejects_missing_report_row);

  return g_test_run();
}
