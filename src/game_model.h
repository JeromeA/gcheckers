#ifndef GAME_MODEL_H
#define GAME_MODEL_H

#include "game_backend.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define GGAME_TYPE_MODEL (ggame_model_get_type())

G_DECLARE_FINAL_TYPE(GGameModel, ggame_model, GGAME, MODEL, GObject)

GGameModel *ggame_model_new(const GameBackend *backend);
void ggame_model_reset(GGameModel *self, const GameBackendVariant *variant_or_null);
gboolean ggame_model_set_position(GGameModel *self, gconstpointer position);
GameBackendMoveList ggame_model_list_moves(GGameModel *self);
gboolean ggame_model_apply_move(GGameModel *self, gconstpointer move);
gconstpointer ggame_model_peek_position(GGameModel *self);
const GameBackend *ggame_model_peek_backend(GGameModel *self);
const GameBackendVariant *ggame_model_peek_variant(GGameModel *self);
char *ggame_model_format_status(GGameModel *self);

G_END_DECLS

#endif
