#ifndef CHECKERS_MODEL_H
#define CHECKERS_MODEL_H

#include "ai_alpha_beta.h"
#include "game.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define GCHECKERS_TYPE_MODEL (gcheckers_model_get_type())

G_DECLARE_FINAL_TYPE(GCheckersModel, gcheckers_model, GCHECKERS, MODEL, GObject)

GCheckersModel *gcheckers_model_new(void);
void gcheckers_model_reset(GCheckersModel *self);
void gcheckers_model_set_rules(GCheckersModel *self, const CheckersRules *rules);
MoveList gcheckers_model_list_moves(GCheckersModel *self);
gboolean gcheckers_model_apply_move(GCheckersModel *self, const CheckersMove *move);
gboolean gcheckers_model_choose_best_move(GCheckersModel *self, guint max_depth, CheckersMove *out_move);
gboolean gcheckers_model_analyze_moves(GCheckersModel *self,
                                       guint max_depth,
                                       CheckersScoredMoveList *out_moves,
                                       CheckersAiSearchStats *out_stats);
gboolean gcheckers_model_copy_game(GCheckersModel *self, Game *out_game);
gboolean gcheckers_model_set_state(GCheckersModel *self, const GameState *state);
char *gcheckers_model_format_status(GCheckersModel *self);
const GameState *gcheckers_model_peek_state(GCheckersModel *self);
const CheckersMove *gcheckers_model_peek_last_move(GCheckersModel *self);

G_END_DECLS

#endif
