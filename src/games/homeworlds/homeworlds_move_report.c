#include "homeworlds_move_report.h"

#include "homeworlds_backend.h"
#include "homeworlds_game.h"

static gboolean homeworlds_move_report_moves_equal(gconstpointer left, gconstpointer right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  return homeworlds_moves_equal(left, right);
}

static GHashTable *homeworlds_move_report_build_move_set(const GameBackendMoveList *moves) {
  GHashTable *move_set = NULL;

  g_return_val_if_fail(moves != NULL, NULL);

  move_set = g_hash_table_new(homeworlds_move_hash, homeworlds_move_report_moves_equal);
  g_return_val_if_fail(move_set != NULL, NULL);

  for (gsize i = 0; i < moves->count; ++i) {
    const HomeworldsMove *candidate = homeworlds_game_backend.move_list_get(moves, i);

    if (candidate != NULL) {
      g_hash_table_add(move_set, (gpointer)candidate);
    }
  }

  return move_set;
}

static void homeworlds_move_report_append_move_list_text(GString *text,
                                                         const GameBackendMoveList *moves,
                                                         const char *title,
                                                         GHashTable *excluded_moves) {
  guint displayed = 0;

  g_return_if_fail(text != NULL);
  g_return_if_fail(moves != NULL);
  g_return_if_fail(title != NULL);

  g_string_append_printf(text, "%s:\n", title);
  for (gsize i = 0; i < moves->count; ++i) {
    const HomeworldsMove *move = homeworlds_game_backend.move_list_get(moves, i);
    char notation[128] = {0};

    if (move == NULL || (excluded_moves != NULL && g_hash_table_contains(excluded_moves, move))) {
      continue;
    }
    if (!homeworlds_move_format(move, notation, sizeof(notation))) {
      continue;
    }

    displayed++;
    g_string_append_printf(text, "%u. %s\n", displayed, notation);
  }

  if (displayed == 0) {
    g_string_append(text, "None\n");
  }
}

char *homeworlds_move_report_format(const HomeworldsPosition *position) {
  GameBackendMoveList good_moves = {0};
  GameBackendMoveList all_moves = {0};
  GString *text = NULL;
  g_autoptr(GHashTable) good_move_set = NULL;
  g_autofree char *good_title = NULL;
  g_autofree char *other_title = NULL;

  g_return_val_if_fail(position != NULL, NULL);

  if (position->phase == HOMEWORLDS_PHASE_FINISHED) {
    return g_strdup("No moves.");
  }
  if (position->phase != HOMEWORLDS_PHASE_PLAY) {
    return g_strdup("Move report is available during play.");
  }

  good_moves = homeworlds_game_backend.list_good_moves(position, 0);
  all_moves = homeworlds_position_list_all_moves(position);
  good_move_set = homeworlds_move_report_build_move_set(&good_moves);
  text = g_string_new(NULL);
  g_return_val_if_fail(text != NULL, NULL);

  good_title = g_strdup_printf("good_moves() (%zu)", good_moves.count);
  other_title = g_strdup_printf("all possible moves minus good_moves() (%zu total before filtering)", all_moves.count);
  homeworlds_move_report_append_move_list_text(text, &good_moves, good_title, NULL);
  g_string_append_c(text, '\n');
  homeworlds_move_report_append_move_list_text(text, &all_moves, other_title, good_move_set);

  homeworlds_game_backend.move_list_free(&good_moves);
  homeworlds_move_list_free(&all_moves);
  return g_string_free(text, FALSE);
}
