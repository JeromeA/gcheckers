#include "position_format.h"
#include "position_predicate.h"
#include "position_search.h"

#include <glib.h>
#include <stdio.h>

typedef struct {
  guint match_count;
  CheckersEvalProfilePredicate *predicate;
} MatchPrinter;

static gboolean match_printer_print(const CheckersMove *line,
                                    guint line_length,
                                    CheckersEvalProfilePredicate *predicate,
                                    guint match_number) {
  g_return_val_if_fail(predicate != NULL, FALSE);

  gint score = 0;
  if (!checkers_eval_profile_predicate_get_last_non_zero_score(predicate, &score)) {
    g_debug("Missing cached score for match output");
    return FALSE;
  }

  char *line_text = checkers_position_format_line(line, line_length);
  if (line_text == NULL) {
    g_debug("Failed to format match line");
    return FALSE;
  }

  printf("%u) score=%d line=%s\n", match_number, score, line_text);
  g_free(line_text);
  return TRUE;
}

static void on_match_found(const Game */*position*/,
                           const CheckersMove *line,
                           guint line_length,
                           gpointer user_data) {
  g_return_if_fail(line != NULL || line_length == 0);
  g_return_if_fail(user_data != NULL);

  MatchPrinter *printer = user_data;
  printer->match_count++;
  if (!match_printer_print(line, line_length, printer->predicate, printer->match_count)) {
    g_debug("Failed to print matched position");
  }
}

int main(int /*argc*/, char **/*argv*/) {
  Game game;
  game_init(&game);

  CheckersPositionSearchOptions options = {
      .min_ply = 4,
      .max_ply = 4,
      .deduplicate_positions = TRUE,
  };

  CheckersEvalProfilePredicate predicate = {0};
  checkers_eval_profile_predicate_init(&predicate, 2, 4, 10);

  MatchPrinter printer = {
      .match_count = 0,
      .predicate = &predicate,
  };

  CheckersPositionSearchStats stats = {0};
  gboolean ok = checkers_position_search(&game,
                                         &options,
                                         checkers_position_predicate_eval_profile,
                                         &predicate,
                                         on_match_found,
                                         &printer,
                                         &stats);
  if (!ok) {
    g_debug("Position search failed");
    game_destroy(&game);
    return 1;
  }

  printf("Scanned %" G_GUINT64_FORMAT " positions, matched %" G_GUINT64_FORMAT "\n",
         stats.evaluated_positions,
         stats.matched_positions);

  game_destroy(&game);
  return 0;
}
