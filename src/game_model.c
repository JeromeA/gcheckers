#include "game_model.h"

#include <glib.h>

struct _GGameModel {
  GObject parent_instance;
  const GameBackend *backend;
  gpointer position;
  const GameBackendVariant *variant;
};

G_DEFINE_TYPE(GGameModel, ggame_model, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_BACKEND,
  PROP_LAST,
};

enum {
  SIGNAL_STATE_CHANGED,
  SIGNAL_LAST,
};

static guint model_signals[SIGNAL_LAST] = {0};
static GParamSpec *properties[PROP_LAST] = {0};

static const GameBackendVariant *ggame_model_pick_initial_variant(const GameBackend *backend) {
  g_return_val_if_fail(backend != NULL, NULL);

  if (backend->variant_count == 0) {
    return NULL;
  }

  g_return_val_if_fail(backend->variant_at != NULL, NULL);

  return backend->variant_at(0);
}

static void ggame_model_emit_state_changed(GGameModel *self) {
  g_return_if_fail(GGAME_IS_MODEL(self));

  g_signal_emit(self, model_signals[SIGNAL_STATE_CHANGED], 0);
}

static void ggame_model_finalize(GObject *object) {
  GGameModel *self = GGAME_MODEL(object);

  if (self->position != NULL && self->backend != NULL && self->backend->position_clear != NULL) {
    self->backend->position_clear(self->position);
  }
  g_clear_pointer(&self->position, g_free);

  G_OBJECT_CLASS(ggame_model_parent_class)->finalize(object);
}

static void ggame_model_constructed(GObject *object) {
  GGameModel *self = GGAME_MODEL(object);

  G_OBJECT_CLASS(ggame_model_parent_class)->constructed(object);

  g_return_if_fail(self->backend != NULL);
  g_return_if_fail(self->backend->position_size > 0);
  g_return_if_fail(self->backend->position_init != NULL);
  g_return_if_fail(self->backend->position_clear != NULL);
  g_return_if_fail(self->backend->list_moves != NULL);
  g_return_if_fail(self->backend->move_list_free != NULL);
  g_return_if_fail(self->backend->move_list_get != NULL);
  g_return_if_fail(self->backend->moves_equal != NULL);
  g_return_if_fail(self->backend->apply_move != NULL);

  self->position = g_malloc0(self->backend->position_size);
  g_return_if_fail(self->position != NULL);

  self->variant = ggame_model_pick_initial_variant(self->backend);
  self->backend->position_init(self->position, self->variant);
}

static void ggame_model_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
  GGameModel *self = GGAME_MODEL(object);

  switch (property_id) {
    case PROP_BACKEND:
      self->backend = g_value_get_pointer(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void ggame_model_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
  GGameModel *self = GGAME_MODEL(object);

  switch (property_id) {
    case PROP_BACKEND:
      g_value_set_pointer(value, (gpointer) self->backend);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void ggame_model_class_init(GGameModelClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->constructed = ggame_model_constructed;
  object_class->finalize = ggame_model_finalize;
  object_class->set_property = ggame_model_set_property;
  object_class->get_property = ggame_model_get_property;

  properties[PROP_BACKEND] = g_param_spec_pointer("backend",
                                                  "Backend",
                                                  "Active backend used by this model.",
                                                  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS);
  g_object_class_install_property(object_class, PROP_BACKEND, properties[PROP_BACKEND]);

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

static void ggame_model_init(GGameModel *self) {
  self->backend = NULL;
  self->position = NULL;
  self->variant = NULL;
}

GGameModel *ggame_model_new(const GameBackend *backend) {
  g_return_val_if_fail(backend != NULL, NULL);

  return g_object_new(GGAME_TYPE_MODEL, "backend", backend, NULL);
}

void ggame_model_reset(GGameModel *self, const GameBackendVariant *variant_or_null) {
  g_return_if_fail(GGAME_IS_MODEL(self));
  g_return_if_fail(self->backend != NULL);
  g_return_if_fail(self->position != NULL);

  self->backend->position_clear(self->position);
  self->variant = variant_or_null != NULL ? variant_or_null : ggame_model_pick_initial_variant(self->backend);
  self->backend->position_init(self->position, self->variant);
  ggame_model_emit_state_changed(self);
}

gboolean ggame_model_set_position(GGameModel *self, gconstpointer position) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), FALSE);
  g_return_val_if_fail(self->backend != NULL, FALSE);
  g_return_val_if_fail(self->position != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(self->backend->position_copy != NULL, FALSE);

  self->backend->position_copy(self->position, position);
  ggame_model_emit_state_changed(self);
  return TRUE;
}

GameBackendMoveList ggame_model_list_moves(GGameModel *self) {
  GameBackendMoveList empty = {0};

  g_return_val_if_fail(GGAME_IS_MODEL(self), empty);
  g_return_val_if_fail(self->backend != NULL, empty);
  g_return_val_if_fail(self->position != NULL, empty);

  return self->backend->list_moves(self->position);
}

gboolean ggame_model_apply_move(GGameModel *self, gconstpointer move) {
  GameBackendMoveList moves = {0};
  gboolean applied = FALSE;

  g_return_val_if_fail(GGAME_IS_MODEL(self), FALSE);
  g_return_val_if_fail(self->backend != NULL, FALSE);
  g_return_val_if_fail(self->position != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  moves = self->backend->list_moves(self->position);
  for (gsize i = 0; i < moves.count; ++i) {
    const void *candidate = self->backend->move_list_get(&moves, i);

    if (candidate == NULL || !self->backend->moves_equal(candidate, move)) {
      continue;
    }

    applied = self->backend->apply_move(self->position, candidate);
    break;
  }

  self->backend->move_list_free(&moves);

  if (!applied) {
    g_debug("Attempted to apply a move that is not in the available move list");
    return FALSE;
  }

  ggame_model_emit_state_changed(self);
  return TRUE;
}

gconstpointer ggame_model_peek_position(GGameModel *self) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), NULL);

  return self->position;
}

const GameBackend *ggame_model_peek_backend(GGameModel *self) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), NULL);

  return self->backend;
}

const GameBackendVariant *ggame_model_peek_variant(GGameModel *self) {
  g_return_val_if_fail(GGAME_IS_MODEL(self), NULL);

  return self->variant;
}

char *ggame_model_format_status(GGameModel *self) {
  GameBackendMoveList moves = {0};
  const char *variant_name = "Default";
  char *status = NULL;

  g_return_val_if_fail(GGAME_IS_MODEL(self), NULL);
  g_return_val_if_fail(self->backend != NULL, NULL);

  if (self->variant != NULL && self->variant->name != NULL) {
    variant_name = self->variant->name;
  }

  moves = ggame_model_list_moves(self);
  status = g_strdup_printf("Game: %s\nVariant: %s\nMoves available: %" G_GSIZE_FORMAT,
                           self->backend->display_name,
                           variant_name,
                           moves.count);
  self->backend->move_list_free(&moves);

  return status;
}
