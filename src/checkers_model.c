#include "ai_alpha_beta.h"
#include "checkers_model.h"

#include <glib.h>
#include <string.h>

struct _GCheckersModel {
  GObject parent_instance;
  Game game;
  CheckersMove last_move;
  gboolean has_last_move;
  CheckersAiTranspositionTable *analysis_tt;
};

G_DEFINE_TYPE(GCheckersModel, gcheckers_model, G_TYPE_OBJECT)

enum { SIGNAL_STATE_CHANGED, SIGNAL_LAST };

static guint model_signals[SIGNAL_LAST] = {0};
enum { GCHECKERS_MODEL_ANALYSIS_TT_SIZE_MB = 64 };

static void gcheckers_model_emit_state_changed(GCheckersModel *self) {
  g_return_if_fail(GCHECKERS_IS_MODEL(self));

  g_signal_emit(self, model_signals[SIGNAL_STATE_CHANGED], 0);
}

static void gcheckers_model_finalize(GObject *object) {
  GCheckersModel *self = GCHECKERS_MODEL(object);

  checkers_ai_tt_free(self->analysis_tt);
  game_destroy(&self->game);

  G_OBJECT_CLASS(gcheckers_model_parent_class)->finalize(object);
}

static void gcheckers_model_class_init(GCheckersModelClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->finalize = gcheckers_model_finalize;

  model_signals[SIGNAL_STATE_CHANGED] = g_signal_new("state-changed",
                                                    G_TYPE_FROM_CLASS(klass),
                                                    G_SIGNAL_RUN_LAST,
                                                    0,
                                                    NULL,
                                                    NULL,
                                                    NULL,
                                                    G_TYPE_NONE,
                                                    0);
}

static void gcheckers_model_init(GCheckersModel *self) {
  game_init(&self->game);
  self->has_last_move = FALSE;
  self->analysis_tt = checkers_ai_tt_new(GCHECKERS_MODEL_ANALYSIS_TT_SIZE_MB);
  if (self->analysis_tt == NULL) {
    g_debug("Failed to allocate model analysis TT, continuing without TT caching");
  }
}

GCheckersModel *gcheckers_model_new(void) {
  return g_object_new(GCHECKERS_TYPE_MODEL, NULL);
}

void gcheckers_model_reset(GCheckersModel *self) {
  g_return_if_fail(GCHECKERS_IS_MODEL(self));

  CheckersRules rules = self->game.rules;
  game_destroy(&self->game);
  game_init_with_rules(&self->game, &rules);
  self->has_last_move = FALSE;
  gcheckers_model_emit_state_changed(self);
}

void gcheckers_model_set_rules(GCheckersModel *self, const CheckersRules *rules) {
  g_return_if_fail(GCHECKERS_IS_MODEL(self));
  g_return_if_fail(rules != NULL);

  if (memcmp(&self->game.rules, rules, sizeof(*rules)) == 0) {
    return;
  }

  game_destroy(&self->game);
  game_init_with_rules(&self->game, rules);
  self->has_last_move = FALSE;
  gcheckers_model_emit_state_changed(self);
}

static void gcheckers_model_set_winner_for_no_moves(GCheckersModel *self) {
  g_return_if_fail(GCHECKERS_IS_MODEL(self));

  g_debug("No available moves for current player\n");
  self->game.state.winner = self->game.state.turn == CHECKERS_COLOR_WHITE ? CHECKERS_WINNER_BLACK
                                                                        : CHECKERS_WINNER_WHITE;
}

static gboolean gcheckers_model_apply_move_internal(GCheckersModel *self, const CheckersMove *move) {
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self), FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  if (game_apply_move(&self->game, move) != 0) {
    g_debug("Failed to apply move\n");
    return FALSE;
  }

  self->last_move = *move;
  self->has_last_move = TRUE;
  gcheckers_model_emit_state_changed(self);
  return TRUE;
}

MoveList gcheckers_model_list_moves(GCheckersModel *self) {
  MoveList empty = {0};

  g_return_val_if_fail(GCHECKERS_IS_MODEL(self), empty);

  return self->game.available_moves(&self->game);
}

static gboolean gcheckers_moves_match(const CheckersMove *left, const CheckersMove *right) {
  g_return_val_if_fail(left != NULL, FALSE);
  g_return_val_if_fail(right != NULL, FALSE);

  if (left->length != right->length || left->captures != right->captures) {
    return FALSE;
  }
  if (left->length == 0) {
    return TRUE;
  }
  return memcmp(left->path, right->path, left->length * sizeof(left->path[0])) == 0;
}

gboolean gcheckers_model_apply_move(GCheckersModel *self, const CheckersMove *move) {
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self), FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(move->length >= 2, FALSE);

  MoveList moves = self->game.available_moves(&self->game);
  if (moves.count == 0) {
    gcheckers_model_set_winner_for_no_moves(self);
    movelist_free(&moves);
    gcheckers_model_emit_state_changed(self);
    return FALSE;
  }

  gboolean applied = FALSE;
  for (size_t i = 0; i < moves.count; ++i) {
    if (gcheckers_moves_match(move, &moves.moves[i])) {
      applied = gcheckers_model_apply_move_internal(self, &moves.moves[i]);
      break;
    }
  }
  if (!applied) {
    g_debug("Attempted to apply a move that is not in the available move list\n");
  }

  movelist_free(&moves);
  return applied;
}

gboolean gcheckers_model_choose_best_move(GCheckersModel *self, guint max_depth, CheckersMove *out_move) {
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self), FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);
  g_return_val_if_fail(max_depth > 0, FALSE);

  return checkers_ai_alpha_beta_choose_move(&self->game, max_depth, out_move);
}

char *gcheckers_model_analyze_moves_text(GCheckersModel *self, guint max_depth) {
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self), NULL);
  g_return_val_if_fail(max_depth > 0, NULL);

  CheckersScoredMoveList scored_moves = {0};
  CheckersAiSearchStats stats = {0};
  checkers_ai_search_stats_clear(&stats);
  gboolean ok = checkers_ai_alpha_beta_analyze_moves_cancellable_with_tt(&self->game,
                                                                          max_depth,
                                                                          &scored_moves,
                                                                          NULL,
                                                                          NULL,
                                                                          NULL,
                                                                          NULL,
                                                                          self->analysis_tt,
                                                                          &stats);
  if (!ok) {
    return g_strdup("No legal moves to analyze.");
  }

  GString *text = g_string_new(NULL);
  g_string_append_printf(text, "Analysis depth: %u\n", max_depth);
  g_string_append_printf(text, "Nodes: %" G_GUINT64_FORMAT "\n", stats.nodes);
  g_string_append_printf(text, "TT hits: %" G_GUINT64_FORMAT "\n", stats.tt_hits);
  g_string_append_printf(text, "TT probes: %" G_GUINT64_FORMAT "\n", stats.tt_probes);
  gdouble ratio = stats.tt_probes == 0 ? 0.0 : (100.0 * (gdouble)stats.tt_hits) / (gdouble)stats.tt_probes;
  g_string_append_printf(text, "TT hit ratio: %.2f%%\n", ratio);
  g_string_append_printf(text, "TT cutoffs: %" G_GUINT64_FORMAT "\n", stats.tt_cutoffs);
  g_string_append(text, "Best to worst:\n");

  for (size_t i = 0; i < scored_moves.count; ++i) {
    char notation[128];
    if (!game_format_move_notation(&scored_moves.moves[i].move, notation, sizeof(notation))) {
      g_strlcpy(notation, "?", sizeof(notation));
    }
    g_string_append_printf(text, "%zu. %s : %d\n", i + 1, notation, scored_moves.moves[i].score);
  }

  checkers_scored_move_list_free(&scored_moves);
  return g_string_free(text, FALSE);
}

gboolean gcheckers_model_copy_game(GCheckersModel *self, Game *out_game) {
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self), FALSE);
  g_return_val_if_fail(out_game != NULL, FALSE);

  *out_game = self->game;
  return TRUE;
}

char *gcheckers_model_format_status(GCheckersModel *self) {
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self), NULL);

  const char *turn_label = self->game.state.turn == CHECKERS_COLOR_WHITE ? "White" : "Black";
  const char *winner_label = game_winner_label(self->game.state.winner);

  return g_strdup_printf("Turn: %s\nWinner: %s", turn_label, winner_label);
}

const GameState *gcheckers_model_peek_state(GCheckersModel *self) {
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self), NULL);

  return &self->game.state;
}

const CheckersMove *gcheckers_model_peek_last_move(GCheckersModel *self) {
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self), NULL);

  if (!self->has_last_move) {
    return NULL;
  }

  return &self->last_move;
}
