#include "checkers_model.h"

#include <glib.h>
#include <string.h>

struct _GCheckersModel {
  GObject parent_instance;
  Game game;
  GRand *rng;
};

G_DEFINE_TYPE(GCheckersModel, gcheckers_model, G_TYPE_OBJECT)

enum { SIGNAL_STATE_CHANGED, SIGNAL_LAST };

static guint model_signals[SIGNAL_LAST] = {0};

static void gcheckers_model_emit_state_changed(GCheckersModel *self) {
  g_return_if_fail(GCHECKERS_IS_MODEL(self));

  g_signal_emit(self, model_signals[SIGNAL_STATE_CHANGED], 0);
}

static void gcheckers_model_finalize(GObject *object) {
  GCheckersModel *self = GCHECKERS_MODEL(object);

  game_destroy(&self->game);
  if (self->rng) {
    g_rand_free(self->rng);
    self->rng = NULL;
  }

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
  self->rng = g_rand_new();
}

GCheckersModel *gcheckers_model_new(void) {
  return g_object_new(GCHECKERS_TYPE_MODEL, NULL);
}

void gcheckers_model_reset(GCheckersModel *self) {
  g_return_if_fail(GCHECKERS_IS_MODEL(self));

  game_destroy(&self->game);
  game_init(&self->game);
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

  gcheckers_model_emit_state_changed(self);
  return TRUE;
}

MoveList gcheckers_model_list_moves(GCheckersModel *self) {
  MoveList empty = {0};

  g_return_val_if_fail(GCHECKERS_IS_MODEL(self), empty);

  return game_list_available_moves(&self->game);
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

  MoveList moves = game_list_available_moves(&self->game);
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

gboolean gcheckers_model_step_random_move(GCheckersModel *self, CheckersMove *out_move) {
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self), FALSE);

  MoveList moves = game_list_available_moves(&self->game);
  if (moves.count == 0) {
    gcheckers_model_set_winner_for_no_moves(self);
    movelist_free(&moves);
    gcheckers_model_emit_state_changed(self);
    return FALSE;
  }

  guint choice = g_rand_int_range(self->rng, 0, (gint)moves.count);
  if (out_move) {
    *out_move = moves.moves[choice];
  }
  gboolean applied = gcheckers_model_apply_move_internal(self, &moves.moves[choice]);
  movelist_free(&moves);
  return applied;
}


const GameState *gcheckers_model_peek_state(GCheckersModel *self) {
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self), NULL);

  return &self->game.state;
}

const CheckersMove *gcheckers_model_peek_last_move(GCheckersModel *self) {
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self), NULL);

  if (self->game.history_size == 0) {
    return NULL;
  }

  return &self->game.history[self->game.history_size - 1];
}

guint gcheckers_model_get_history_size(GCheckersModel *self) {
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self), 0);

  return (guint)self->game.history_size;
}
