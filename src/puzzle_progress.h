#ifndef GGAME_PUZZLE_PROGRESS_H
#define GGAME_PUZZLE_PROGRESS_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
  GGAME_PUZZLE_ATTEMPT_RESULT_UNRESOLVED = 0,
  GGAME_PUZZLE_ATTEMPT_RESULT_SUCCESS,
  GGAME_PUZZLE_ATTEMPT_RESULT_FAILURE,
  GGAME_PUZZLE_ATTEMPT_RESULT_ANALYZE,
} GGamePuzzleAttemptResult;

typedef enum {
  GGAME_PUZZLE_STATUS_UNTRIED = 0,
  GGAME_PUZZLE_STATUS_FAILED,
  GGAME_PUZZLE_STATUS_SOLVED,
} GGamePuzzleStatus;

typedef struct {
  char *attempt_id;
  char *puzzle_id;
  guint puzzle_number;
  char *puzzle_source_name;
  char *puzzle_variant;
  guint attacker_side;
  gint64 started_unix_ms;
  gint64 finished_unix_ms;
  GGamePuzzleAttemptResult result;
  gboolean failure_on_first_move;
  gboolean has_failed_first_move;
  char *failed_first_move_text;
  gint64 first_reported_unix_ms;
  guint report_count;
} GGamePuzzleAttemptRecord;

typedef struct {
  char *puzzle_id;
  char *puzzle_variant;
  guint puzzle_number;
  GGamePuzzleStatus status;
  gint64 last_finished_unix_ms;
} GGamePuzzleStatusEntry;

typedef struct _GGamePuzzleProgressStore GGamePuzzleProgressStore;

GGamePuzzleProgressStore *ggame_puzzle_progress_store_new(const char *state_dir);
GGamePuzzleProgressStore *ggame_puzzle_progress_store_ref(GGamePuzzleProgressStore *store);
void ggame_puzzle_progress_store_unref(GGamePuzzleProgressStore *store);

char *ggame_puzzle_progress_store_dup_state_dir(GGamePuzzleProgressStore *store);
char *ggame_puzzle_progress_store_get_history_path(GGamePuzzleProgressStore *store);
char *ggame_puzzle_progress_store_get_status_path(GGamePuzzleProgressStore *store);
char *ggame_puzzle_progress_store_get_or_create_user_id(GGamePuzzleProgressStore *store, GError **error);

gboolean ggame_puzzle_progress_store_append_attempt(GGamePuzzleProgressStore *store,
                                                       const GGamePuzzleAttemptRecord *record,
                                                       GError **error);
gboolean ggame_puzzle_progress_store_replace_attempt(GGamePuzzleProgressStore *store,
                                                        const GGamePuzzleAttemptRecord *record,
                                                        GError **error);
GPtrArray *ggame_puzzle_progress_store_load_attempt_history(GGamePuzzleProgressStore *store, GError **error);
GPtrArray *ggame_puzzle_progress_store_collect_unsent_attempts(GGamePuzzleProgressStore *store, GError **error);
gboolean ggame_puzzle_progress_store_mark_reported(GGamePuzzleProgressStore *store,
                                                      gint64 reported_unix_ms,
                                                      GError **error);
gboolean ggame_puzzle_progress_store_clear_progress(GGamePuzzleProgressStore *store, GError **error);
GHashTable *ggame_puzzle_progress_store_load_status_map(GGamePuzzleProgressStore *store, GError **error);

GGamePuzzleAttemptRecord *ggame_puzzle_attempt_record_copy(const GGamePuzzleAttemptRecord *record);
void ggame_puzzle_attempt_record_free(GGamePuzzleAttemptRecord *record);
void ggame_puzzle_attempt_record_clear(GGamePuzzleAttemptRecord *record);
gboolean ggame_puzzle_attempt_record_is_resolved(const GGamePuzzleAttemptRecord *record);

GGamePuzzleStatusEntry *ggame_puzzle_status_entry_copy(const GGamePuzzleStatusEntry *entry);
void ggame_puzzle_status_entry_free(GGamePuzzleStatusEntry *entry);
GGamePuzzleStatus ggame_puzzle_progress_reduce_status_for_attempts(const GPtrArray *attempts,
                                                                         const char *puzzle_id);
char *ggame_puzzle_progress_build_upload_json(const char *user_id, const GPtrArray *attempt_history);
gboolean ggame_puzzle_progress_should_send_report(const GPtrArray *attempt_history, gint64 now_unix_ms);

G_END_DECLS

#endif
