#ifndef GCHECKERS_PUZZLE_PROGRESS_H
#define GCHECKERS_PUZZLE_PROGRESS_H

#include "game.h"
#include "ruleset.h"

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
  CHECKERS_PUZZLE_ATTEMPT_RESULT_UNRESOLVED = 0,
  CHECKERS_PUZZLE_ATTEMPT_RESULT_SUCCESS,
  CHECKERS_PUZZLE_ATTEMPT_RESULT_FAILURE,
  CHECKERS_PUZZLE_ATTEMPT_RESULT_ANALYZE,
} CheckersPuzzleAttemptResult;

typedef enum {
  CHECKERS_PUZZLE_STATUS_UNTRIED = 0,
  CHECKERS_PUZZLE_STATUS_FAILED,
  CHECKERS_PUZZLE_STATUS_SOLVED,
} CheckersPuzzleStatus;

typedef struct {
  char *attempt_id;
  char *puzzle_id;
  guint puzzle_number;
  char *puzzle_source_name;
  PlayerRuleset puzzle_ruleset;
  CheckersColor attacker;
  gint64 started_unix_ms;
  gint64 finished_unix_ms;
  CheckersPuzzleAttemptResult result;
  gboolean failure_on_first_move;
  gboolean has_failed_first_move;
  CheckersMove failed_first_move;
  gint64 first_reported_unix_ms;
  guint report_count;
} CheckersPuzzleAttemptRecord;

typedef struct {
  char *puzzle_id;
  PlayerRuleset puzzle_ruleset;
  guint puzzle_number;
  CheckersPuzzleStatus status;
  gint64 last_finished_unix_ms;
} CheckersPuzzleStatusEntry;

typedef struct _CheckersPuzzleProgressStore CheckersPuzzleProgressStore;

CheckersPuzzleProgressStore *checkers_puzzle_progress_store_new(const char *state_dir);
CheckersPuzzleProgressStore *checkers_puzzle_progress_store_ref(CheckersPuzzleProgressStore *store);
void checkers_puzzle_progress_store_unref(CheckersPuzzleProgressStore *store);

char *checkers_puzzle_progress_store_dup_state_dir(CheckersPuzzleProgressStore *store);
char *checkers_puzzle_progress_store_get_history_path(CheckersPuzzleProgressStore *store);
char *checkers_puzzle_progress_store_get_status_path(CheckersPuzzleProgressStore *store);
char *checkers_puzzle_progress_store_get_or_create_user_id(CheckersPuzzleProgressStore *store, GError **error);

gboolean checkers_puzzle_progress_store_append_attempt(CheckersPuzzleProgressStore *store,
                                                       const CheckersPuzzleAttemptRecord *record,
                                                       GError **error);
gboolean checkers_puzzle_progress_store_replace_attempt(CheckersPuzzleProgressStore *store,
                                                        const CheckersPuzzleAttemptRecord *record,
                                                        GError **error);
GPtrArray *checkers_puzzle_progress_store_load_attempt_history(CheckersPuzzleProgressStore *store, GError **error);
GPtrArray *checkers_puzzle_progress_store_collect_unsent_attempts(CheckersPuzzleProgressStore *store, GError **error);
gboolean checkers_puzzle_progress_store_mark_reported(CheckersPuzzleProgressStore *store,
                                                      gint64 reported_unix_ms,
                                                      GError **error);
GHashTable *checkers_puzzle_progress_store_load_status_map(CheckersPuzzleProgressStore *store, GError **error);

CheckersPuzzleAttemptRecord *checkers_puzzle_attempt_record_copy(const CheckersPuzzleAttemptRecord *record);
void checkers_puzzle_attempt_record_free(CheckersPuzzleAttemptRecord *record);
void checkers_puzzle_attempt_record_clear(CheckersPuzzleAttemptRecord *record);
gboolean checkers_puzzle_attempt_record_is_resolved(const CheckersPuzzleAttemptRecord *record);

CheckersPuzzleStatusEntry *checkers_puzzle_status_entry_copy(const CheckersPuzzleStatusEntry *entry);
void checkers_puzzle_status_entry_free(CheckersPuzzleStatusEntry *entry);
CheckersPuzzleStatus checkers_puzzle_progress_reduce_status_for_attempts(const GPtrArray *attempts,
                                                                         const char *puzzle_id);
char *checkers_puzzle_progress_build_upload_json(const char *user_id, const GPtrArray *attempt_history);
gboolean checkers_puzzle_progress_should_send_report(const GPtrArray *attempt_history, gint64 now_unix_ms);

G_END_DECLS

#endif
