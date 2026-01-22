#ifndef CHECKERS_MODEL_H
#define CHECKERS_MODEL_H

#include "game.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define GCHECKERS_TYPE_MODEL (gcheckers_model_get_type())

G_DECLARE_FINAL_TYPE(GCheckersModel, gcheckers_model, GCHECKERS, MODEL, GObject)

GCheckersModel *gcheckers_model_new(void);
void gcheckers_model_reset(GCheckersModel *self);
MoveList gcheckers_model_list_moves(GCheckersModel *self);
gboolean gcheckers_model_apply_move(GCheckersModel *self, const CheckersMove *move);
gboolean gcheckers_model_step_random_move(GCheckersModel *self, CheckersMove *out_move);
char *gcheckers_model_format_status(GCheckersModel *self);
const GameState *gcheckers_model_peek_state(GCheckersModel *self);
const CheckersMove *gcheckers_model_peek_last_move(GCheckersModel *self);
guint gcheckers_model_get_history_size(GCheckersModel *self);

G_END_DECLS

#endif
