#include "sgf_controller.h"
#include "ai_search.h"
#include "active_game_backend.h"
#include "games/checkers/checkers_backend.h"
#include "games/checkers/rulesets.h"
#include "sgf_io.h"
#include "sgf_move_props.h"

#include <string.h>

struct _GGameSgfController {
  GObject parent_instance;
  BoardView *board_view;
  GCheckersModel *checkers_model;
  GGameModel *game_model;
  SgfTree *sgf_tree;
  SgfView *sgf_view;
  gboolean is_replaying;
};

static GGameModel *ggame_sgf_controller_peek_active_game_model(GGameSgfController *self) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), NULL);

  return self->game_model;
}

static gboolean ggame_sgf_controller_sync_tree_ruleset_from_model(GGameSgfController *self) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);

  if (!SGF_IS_TREE(self->sgf_tree)) {
    return TRUE;
  }

  if (!GGAME_IS_MODEL(self->game_model)) {
    return TRUE;
  }

  return sgf_io_tree_set_variant(self->sgf_tree, ggame_model_peek_variant(self->game_model));
}

G_DEFINE_TYPE(GGameSgfController, ggame_sgf_controller, G_TYPE_OBJECT)

enum {
  SIGNAL_NODE_CHANGED,
  SIGNAL_MANUAL_REQUESTED,
  SIGNAL_LAST
};

static guint controller_signals[SIGNAL_LAST] = {0};

static void ggame_sgf_controller_emit_node_changed(GGameSgfController *self, const SgfNode *node) {
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(self));
  g_return_if_fail(node != NULL);

  g_signal_emit(self, controller_signals[SIGNAL_NODE_CHANGED], 0, node);
}

static SgfColor ggame_sgf_controller_color_from_side(guint side) {
  switch (side) {
    case 0:
      return SGF_COLOR_BLACK;
    case 1:
      return SGF_COLOR_WHITE;
    default:
      return SGF_COLOR_NONE;
  }
}

static void ggame_sgf_controller_disconnect_model(GGameSgfController *self) {
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(self));

  g_clear_object(&self->checkers_model);
  g_clear_object(&self->game_model);
}

static gboolean ggame_sgf_controller_extract_node_move(const SgfNode *node,
                                                       gpointer move,
                                                       gboolean *has_move) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(has_move != NULL, FALSE);

  SgfColor color = SGF_COLOR_NONE;
  g_autoptr(GError) error = NULL;
  if (!sgf_move_props_try_parse_node(node, &color, move, has_move, &error)) {
    g_debug("Failed to extract SGF move for node %u: %s",
            sgf_node_get_move_number(node),
            error != NULL ? error->message : "unknown error");
    return FALSE;
  }

  return TRUE;
}

static GPtrArray *ggame_sgf_controller_build_node_path(const SgfNode *node) {
  g_return_val_if_fail(node != NULL, NULL);

  GPtrArray *path = g_ptr_array_new();
  const SgfNode *cursor = node;
  while (cursor) {
    g_ptr_array_add(path, (gpointer)cursor);
    cursor = sgf_node_get_parent(cursor);
  }

  for (guint i = 0; i < path->len / 2; ++i) {
    gpointer tmp = g_ptr_array_index(path, i);
    g_ptr_array_index(path, i) = g_ptr_array_index(path, path->len - 1 - i);
    g_ptr_array_index(path, path->len - 1 - i) = tmp;
  }

  return path;
}

static gboolean ggame_sgf_controller_replay_node_path_into_position(const SgfNode *node,
                                                                    const GameBackend *backend,
                                                                    gpointer position,
                                                                    GError **error) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(backend->move_size > 0, FALSE);
  g_return_val_if_fail(backend->position_turn != NULL, FALSE);
  g_return_val_if_fail(backend->apply_move != NULL, FALSE);

  g_autoptr(GPtrArray) path = ggame_sgf_controller_build_node_path(node);
  if (path == NULL) {
    g_set_error_literal(error,
                        g_quark_from_static_string("gcheckers-sgf-controller-error"),
                        12,
                        "Unable to build SGF node path");
    return FALSE;
  }

  g_autofree guint8 *move = g_malloc0(backend->move_size);
  g_return_val_if_fail(move != NULL, FALSE);

  for (guint i = 0; i < path->len; ++i) {
    const SgfNode *step = g_ptr_array_index(path, i);
    gboolean has_move = FALSE;
    SgfColor color = SGF_COLOR_NONE;

    if (backend->sgf_apply_setup_node != NULL && !backend->sgf_apply_setup_node(position, step, error)) {
      return FALSE;
    }
    if (!sgf_move_props_try_parse_node(step, &color, move, &has_move, error)) {
      return FALSE;
    }
    if (!has_move) {
      continue;
    }

    SgfColor expected_color = ggame_sgf_controller_color_from_side(backend->position_turn(position));
    if (expected_color == SGF_COLOR_NONE || color != expected_color) {
      g_set_error(error,
                  g_quark_from_static_string("gcheckers-sgf-controller-error"),
                  15,
                  "Unexpected SGF side to move for node %u",
                  sgf_node_get_move_number(step));
      return FALSE;
    }
    if (!backend->apply_move(position, move)) {
      g_set_error(error,
                  g_quark_from_static_string("gcheckers-sgf-controller-error"),
                  14,
                  "Unable to replay SGF move %u",
                  sgf_node_get_move_number(step));
      return FALSE;
    }

    memset(move, 0, backend->move_size);
  }

  return TRUE;
}

gboolean ggame_sgf_controller_replay_node_into_game(const SgfNode *node, Game *game, GError **error) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(game != NULL, FALSE);

  return ggame_sgf_controller_replay_node_path_into_position(node, &checkers_game_backend, game, error);
}

gboolean ggame_sgf_controller_replay_node_into_position(const SgfNode *node,
                                                        const GameBackend *backend,
                                                        gpointer position,
                                                        GError **error) {
  return ggame_sgf_controller_replay_node_path_into_position(node, backend, position, error);
}

static gboolean ggame_sgf_controller_replay_to_node_checkers(GGameSgfController *self, const SgfNode *node) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self->checkers_model), FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  self->is_replaying = TRUE;
  gcheckers_model_reset(self->checkers_model);

  gboolean success = TRUE;
  Game game = {0};
  if (!gcheckers_model_copy_game(self->checkers_model, &game)) {
    g_debug("Failed to copy model game for SGF replay");
    self->is_replaying = FALSE;
    return FALSE;
  }

  g_autoptr(GError) replay_error = NULL;
  if (!ggame_sgf_controller_replay_node_into_game(node, &game, &replay_error)) {
    g_debug("Failed to replay SGF node %u: %s",
            sgf_node_get_move_number(node),
            replay_error != NULL ? replay_error->message : "unknown error");
    success = FALSE;
  }

  if (success && !gcheckers_model_set_state(self->checkers_model, &game.state)) {
    g_debug("Failed to publish replayed SGF state into model");
    success = FALSE;
  }
  if (success) {
    board_view_clear_selection(self->board_view);
  }

  game_destroy(&game);
  self->is_replaying = FALSE;
  return success;
}

static gboolean ggame_sgf_controller_replay_to_node_generic(GGameSgfController *self, const SgfNode *node) {
  GGameModel *game_model = NULL;
  const GameBackend *backend = NULL;
  const GameBackendVariant *variant = NULL;

  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  game_model = ggame_sgf_controller_peek_active_game_model(self);
  g_return_val_if_fail(GGAME_IS_MODEL(game_model), FALSE);

  backend = ggame_model_peek_backend(game_model);
  variant = ggame_model_peek_variant(game_model);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(backend->position_size > 0, FALSE);
  g_return_val_if_fail(backend->position_init != NULL, FALSE);
  g_return_val_if_fail(backend->position_clear != NULL, FALSE);

  self->is_replaying = TRUE;

  g_autofree guint8 *position = g_malloc0(backend->position_size);
  g_return_val_if_fail(position != NULL, FALSE);
  backend->position_init(position, variant);

  gboolean success = TRUE;
  g_autoptr(GError) replay_error = NULL;
  if (!ggame_sgf_controller_replay_node_into_position(node, backend, position, &replay_error)) {
    g_debug("Failed to replay SGF node %u: %s",
            sgf_node_get_move_number(node),
            replay_error != NULL ? replay_error->message : "unknown error");
    success = FALSE;
  }

  if (success && !ggame_model_set_position(game_model, position)) {
    g_debug("Failed to publish replayed SGF state into generic model");
    success = FALSE;
  }
  if (success) {
    board_view_clear_selection(self->board_view);
  }

  backend->position_clear(position);
  self->is_replaying = FALSE;
  return success;
}

static gboolean ggame_sgf_controller_replay_to_node(GGameSgfController *self, const SgfNode *node) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  if (GCHECKERS_IS_MODEL(self->checkers_model)) {
    return ggame_sgf_controller_replay_to_node_checkers(self, node);
  }

  return ggame_sgf_controller_replay_to_node_generic(self, node);
}

static gboolean ggame_sgf_controller_sync_model_for_transition_generic(GGameSgfController *self,
                                                                       const SgfNode *previous,
                                                                       const SgfNode *target) {
  GGameModel *game_model = NULL;
  const GameBackend *backend = NULL;
  gconstpointer position = NULL;

  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(target != NULL, FALSE);

  game_model = ggame_sgf_controller_peek_active_game_model(self);
  g_return_val_if_fail(GGAME_IS_MODEL(game_model), FALSE);

  backend = ggame_model_peek_backend(game_model);
  position = ggame_model_peek_position(game_model);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);

  if (previous == target) {
    return TRUE;
  }

  if (previous != NULL && sgf_node_get_parent(target) == previous) {
    g_autofree guint8 *move = g_malloc0(backend->move_size);
    gboolean has_move = FALSE;
    SgfColor color = SGF_COLOR_NONE;

    g_return_val_if_fail(move != NULL, FALSE);
    if (!sgf_move_props_try_parse_node(target, &color, move, &has_move, NULL)) {
      return FALSE;
    }
    if (!has_move) {
      return ggame_sgf_controller_replay_to_node(self, target);
    }
    if (color != ggame_sgf_controller_color_from_side(backend->position_turn(position))) {
      g_debug("Failed SGF side-to-move validation for generic transition");
      return FALSE;
    }
    if (!ggame_model_apply_move(game_model, move)) {
      g_debug("Failed to apply SGF transition move %u", sgf_node_get_move_number(target));
      return FALSE;
    }

    return TRUE;
  }

  return ggame_sgf_controller_replay_to_node(self, target);
}

static gboolean ggame_sgf_controller_sync_model_for_transition(GGameSgfController *self,
                                                               const SgfNode *previous,
                                                               const SgfNode *target) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(target != NULL, FALSE);

  if (GCHECKERS_IS_MODEL(self->checkers_model)) {
    if (previous == target) {
      return TRUE;
    }

    if (previous != NULL && sgf_node_get_parent(target) == previous) {
      CheckersMove move = {0};
      gboolean has_move = FALSE;
      if (!ggame_sgf_controller_extract_node_move(target, &move, &has_move)) {
        return FALSE;
      }
      if (!has_move) {
        return ggame_sgf_controller_replay_to_node(self, target);
      }

      if (!gcheckers_model_apply_move(self->checkers_model, &move)) {
        g_debug("Failed to apply SGF transition move %u", sgf_node_get_move_number(target));
        return FALSE;
      }

      return TRUE;
    }

    return ggame_sgf_controller_replay_to_node(self, target);
  }

  return ggame_sgf_controller_sync_model_for_transition_generic(self, previous, target);
}

static gboolean ggame_sgf_controller_move_current(GGameSgfController *self, const SgfNode *node) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  const SgfNode *previous = sgf_tree_get_current(self->sgf_tree);
  if (!sgf_tree_set_current(self->sgf_tree, node)) {
    g_debug("Failed to select SGF node");
    return FALSE;
  }

  if (!GCHECKERS_IS_MODEL(self->checkers_model) && !GGAME_IS_MODEL(self->game_model)) {
    if (previous != node) {
      ggame_sgf_controller_emit_node_changed(self, node);
    }
    return TRUE;
  }

  gboolean ok = ggame_sgf_controller_sync_model_for_transition(self, previous, node);
  if (ok && previous != node) {
    ggame_sgf_controller_emit_node_changed(self, node);
  }
  return ok;
}

static gboolean ggame_sgf_controller_navigate_to(GGameSgfController *self, const SgfNode *node) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  if (!ggame_sgf_controller_move_current(self, node)) {
    return FALSE;
  }

  g_signal_emit(self, controller_signals[SIGNAL_MANUAL_REQUESTED], 0, node);
  sgf_view_set_selected(self->sgf_view, node);
  return TRUE;
}

static void ggame_sgf_controller_on_node_selected(SgfView * /*view*/,
                                                      const SgfNode *node,
                                                      gpointer user_data) {
  GGameSgfController *self = GGAME_SGF_CONTROLLER(user_data);

  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(self));
  g_return_if_fail(node != NULL);

  if (!ggame_sgf_controller_move_current(self, node)) {
    return;
  }

  g_signal_emit(self, controller_signals[SIGNAL_MANUAL_REQUESTED], 0, node);
  sgf_view_set_selected(self->sgf_view, node);
}

static void ggame_sgf_controller_dispose(GObject *object) {
  GGameSgfController *self = GGAME_SGF_CONTROLLER(object);

  ggame_sgf_controller_disconnect_model(self);
  g_clear_object(&self->board_view);
  g_clear_object(&self->sgf_view);
  g_clear_object(&self->sgf_tree);

  G_OBJECT_CLASS(ggame_sgf_controller_parent_class)->dispose(object);
}

static void ggame_sgf_controller_class_init(GGameSgfControllerClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = ggame_sgf_controller_dispose;

  controller_signals[SIGNAL_NODE_CHANGED] = g_signal_new("node-changed",
                                                         G_TYPE_FROM_CLASS(klass),
                                                         G_SIGNAL_RUN_LAST,
                                                         0,
                                                         NULL,
                                                         NULL,
                                                         NULL,
                                                         G_TYPE_NONE,
                                                         1,
                                                         G_TYPE_POINTER);

  controller_signals[SIGNAL_MANUAL_REQUESTED] = g_signal_new("manual-requested",
                                                               G_TYPE_FROM_CLASS(klass),
                                                               G_SIGNAL_RUN_LAST,
                                                               0,
                                                               NULL,
                                                               NULL,
                                                               NULL,
                                                               G_TYPE_NONE,
                                                               1,
                                                               G_TYPE_POINTER);
}

static void ggame_sgf_controller_init(GGameSgfController *self) {
  self->sgf_tree = sgf_tree_new();
  self->sgf_view = sgf_view_new();
  sgf_view_set_tree(self->sgf_view, self->sgf_tree);
  g_signal_connect(self->sgf_view,
                   "node-selected",
                   G_CALLBACK(ggame_sgf_controller_on_node_selected),
                   self);

  self->is_replaying = FALSE;
}

GGameSgfController *ggame_sgf_controller_new(BoardView *board_view) {
  g_return_val_if_fail(BOARD_IS_VIEW(board_view), NULL);

  GGameSgfController *self = g_object_new(GGAME_TYPE_SGF_CONTROLLER, NULL);
  self->board_view = g_object_ref(board_view);
  return self;
}

void ggame_sgf_controller_set_model(GGameSgfController *self, GCheckersModel *model) {
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(model));

  if (self->checkers_model == model) {
    return;
  }

  ggame_sgf_controller_disconnect_model(self);
  self->checkers_model = g_object_ref(model);
  self->game_model = g_object_ref(gcheckers_model_peek_game_model(model));
}

void ggame_sgf_controller_set_game_model(GGameSgfController *self, GGameModel *model) {
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(self));
  g_return_if_fail(GGAME_IS_MODEL(model));

  if (self->game_model == model && self->checkers_model == NULL) {
    return;
  }

  ggame_sgf_controller_disconnect_model(self);
  self->game_model = g_object_ref(model);
}

void ggame_sgf_controller_new_game(GGameSgfController *self) {
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(self));

  sgf_tree_reset(self->sgf_tree);
  if (!ggame_sgf_controller_sync_tree_ruleset_from_model(self)) {
    g_debug("Failed to stamp SGF RU on new game tree");
  }
  sgf_view_set_tree(self->sgf_view, self->sgf_tree);
  board_view_clear_selection(self->board_view);
  self->is_replaying = FALSE;

  const SgfNode *root = sgf_tree_get_root(self->sgf_tree);
  if (root != NULL) {
    ggame_sgf_controller_emit_node_changed(self, root);
  }
}

gboolean ggame_sgf_controller_apply_move(GGameSgfController *self, gconstpointer move) {
  GGameModel *game_model = NULL;
  const GameBackend *backend = NULL;
  gconstpointer position = NULL;
  GameBackendMoveList moves = {0};
  gboolean found = FALSE;
  SgfColor color = SGF_COLOR_NONE;
  const SgfNode *previous = NULL;
  const SgfNode *node = NULL;
  char notation[128] = {0};
  g_autoptr(GError) error = NULL;

  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(move != NULL, FALSE);

  game_model = ggame_sgf_controller_peek_active_game_model(self);
  g_return_val_if_fail(GGAME_IS_MODEL(game_model), FALSE);

  backend = ggame_model_peek_backend(game_model);
  position = ggame_model_peek_position(game_model);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(backend->position_turn != NULL, FALSE);
  g_return_val_if_fail(backend->supports_move_list, FALSE);
  g_return_val_if_fail(backend->move_list_get != NULL, FALSE);
  g_return_val_if_fail(backend->move_list_free != NULL, FALSE);
  g_return_val_if_fail(backend->moves_equal != NULL, FALSE);

  moves = ggame_model_list_moves(game_model);
  for (gsize i = 0; i < moves.count; ++i) {
    const void *candidate = backend->move_list_get(&moves, i);

    if (candidate != NULL && backend->moves_equal(candidate, move)) {
      found = TRUE;
      break;
    }
  }

  if (!found) {
    g_debug("Attempted to apply move not present in current model move list");
    backend->move_list_free(&moves);
    return FALSE;
  }

  color = ggame_sgf_controller_color_from_side(backend->position_turn(position));
  if (color == SGF_COLOR_NONE) {
    g_debug("Failed to determine SGF side for current model position");
    backend->move_list_free(&moves);
    return FALSE;
  }

  if (!sgf_move_props_format_notation(move, notation, sizeof(notation), &error)) {
    g_debug("Failed to format SGF move notation: %s", error != NULL ? error->message : "unknown error");
    backend->move_list_free(&moves);
    return FALSE;
  }

  previous = sgf_tree_get_current(self->sgf_tree);
  node = sgf_tree_append_move(self->sgf_tree, color, notation);
  backend->move_list_free(&moves);

  if (node == NULL) {
    g_debug("Failed to append SGF move");
    return FALSE;
  }

  if (!sgf_tree_set_current(self->sgf_tree, node)) {
    g_debug("Failed to move SGF current to appended node");
    return FALSE;
  }

  if (!ggame_sgf_controller_sync_model_for_transition(self, previous, node)) {
    return FALSE;
  }

  ggame_sgf_controller_emit_node_changed(self, node);
  sgf_view_refresh(self->sgf_view);
  return TRUE;
}

gboolean ggame_sgf_controller_step_ai_move(GGameSgfController *self, guint depth, gpointer out_move) {
  GGameModel *game_model = NULL;
  const GameBackend *backend = NULL;
  gconstpointer position = NULL;

  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);

  game_model = ggame_sgf_controller_peek_active_game_model(self);
  g_return_val_if_fail(GGAME_IS_MODEL(game_model), FALSE);

  backend = ggame_model_peek_backend(game_model);
  position = ggame_model_peek_position(game_model);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);
  g_return_val_if_fail(backend->supports_ai_search, FALSE);

  g_autofree guint8 *move = g_malloc0(backend->move_size);
  g_return_val_if_fail(move != NULL, FALSE);

  if (!game_ai_search_choose_move(backend, position, depth, move)) {
    g_debug("Failed to choose SGF AI move from model");
    return FALSE;
  }

  gboolean applied = ggame_sgf_controller_apply_move(self, move);
  if (applied && out_move) {
    memcpy(out_move, move, backend->move_size);
  }

  return applied;
}

gboolean ggame_sgf_controller_rewind_to_start(GGameSgfController *self) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);

  const SgfNode *root = sgf_tree_get_root(self->sgf_tree);
  if (!root) {
    g_debug("Missing SGF root node");
    return FALSE;
  }

  if (sgf_tree_get_current(self->sgf_tree) == root) {
    return FALSE;
  }

  return ggame_sgf_controller_navigate_to(self, root);
}

gboolean ggame_sgf_controller_step_backward(GGameSgfController *self) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);

  const SgfNode *current = sgf_tree_get_current(self->sgf_tree);
  if (!current) {
    g_debug("Missing SGF current node");
    return FALSE;
  }

  const SgfNode *target = sgf_node_get_parent(current);
  if (!target) {
    return FALSE;
  }

  return ggame_sgf_controller_navigate_to(self, target);
}

gboolean ggame_sgf_controller_step_forward(GGameSgfController *self) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);

  const SgfNode *current = sgf_tree_get_current(self->sgf_tree);
  if (!current) {
    g_debug("Missing SGF current node");
    return FALSE;
  }

  const GPtrArray *children = sgf_node_get_children(current);
  if (!children || children->len == 0) {
    return FALSE;
  }

  const SgfNode *target = g_ptr_array_index((GPtrArray *)children, 0);
  g_return_val_if_fail(target != NULL, FALSE);
  return ggame_sgf_controller_navigate_to(self, target);
}

gboolean ggame_sgf_controller_step_forward_to_branch(GGameSgfController *self) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);

  const SgfNode *cursor = sgf_tree_get_current(self->sgf_tree);
  if (!cursor) {
    g_debug("Missing SGF current node");
    return FALSE;
  }

  gboolean moved = FALSE;
  while (TRUE) {
    const GPtrArray *children = sgf_node_get_children(cursor);
    if (!children || children->len == 0 || children->len >= 2) {
      break;
    }
    cursor = g_ptr_array_index((GPtrArray *)children, 0);
    g_return_val_if_fail(cursor != NULL, FALSE);
    moved = TRUE;
  }

  if (!moved) {
    return FALSE;
  }

  return ggame_sgf_controller_navigate_to(self, cursor);
}

gboolean ggame_sgf_controller_step_forward_to_end(GGameSgfController *self) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);

  const SgfNode *cursor = sgf_tree_get_current(self->sgf_tree);
  if (!cursor) {
    g_debug("Missing SGF current node");
    return FALSE;
  }

  gboolean moved = FALSE;
  while (TRUE) {
    const GPtrArray *children = sgf_node_get_children(cursor);
    if (!children || children->len == 0) {
      break;
    }
    cursor = g_ptr_array_index((GPtrArray *)children, 0);
    g_return_val_if_fail(cursor != NULL, FALSE);
    moved = TRUE;
  }

  if (!moved) {
    return FALSE;
  }

  return ggame_sgf_controller_navigate_to(self, cursor);
}

gboolean ggame_sgf_controller_load_file(GGameSgfController *self, const char *path, GError **error) {
  const SgfNode *selected = NULL;
  const GameBackendVariant *loaded_variant = NULL;

  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  g_autoptr(SgfTree) loaded = NULL;
  if (!sgf_io_load_file(path, &loaded, error)) {
    return FALSE;
  }

  g_clear_object(&self->sgf_tree);
  self->sgf_tree = g_steal_pointer(&loaded);
  sgf_view_set_tree(self->sgf_view, self->sgf_tree);
  board_view_clear_selection(self->board_view);

  selected = sgf_tree_get_current(self->sgf_tree);
  if (GCHECKERS_IS_MODEL(self->checkers_model)) {
    g_autoptr(GError) ruleset_error = NULL;
    if (!sgf_io_tree_get_variant(self->sgf_tree, &loaded_variant, &ruleset_error) || loaded_variant == NULL) {
      g_propagate_error(error, g_steal_pointer(&ruleset_error));
      return FALSE;
    }

    PlayerRuleset loaded_ruleset = PLAYER_RULESET_INTERNATIONAL;
    if (!checkers_ruleset_find_by_short_name(loaded_variant->short_name, &loaded_ruleset)) {
      g_set_error_literal(error,
                          g_quark_from_static_string("gcheckers-sgf-controller-error"),
                          3,
                          "Loaded SGF references an unknown ruleset");
      return FALSE;
    }

    const CheckersRules *rules = checkers_ruleset_get_rules(loaded_ruleset);
    if (rules == NULL) {
      g_set_error_literal(error,
                          g_quark_from_static_string("gcheckers-sgf-controller-error"),
                          3,
                          "Loaded SGF references an unknown ruleset");
      return FALSE;
    }
    gcheckers_model_set_rules(self->checkers_model, rules);

    if (selected == NULL || !ggame_sgf_controller_replay_to_node(self, selected)) {
      g_set_error_literal(error, g_quark_from_static_string("gcheckers-sgf-controller-error"), 1,
                          "Unable to replay loaded SGF tree");
      return FALSE;
    }
  } else if (GGAME_IS_MODEL(self->game_model)) {
    g_autoptr(GError) variant_error = NULL;

    if (!sgf_io_tree_get_variant(self->sgf_tree, &loaded_variant, &variant_error)) {
      g_propagate_error(error, g_steal_pointer(&variant_error));
      return FALSE;
    }

    ggame_model_reset(self->game_model, loaded_variant);
    if (selected == NULL || !ggame_sgf_controller_replay_to_node(self, selected)) {
      g_set_error_literal(error,
                          g_quark_from_static_string("gcheckers-sgf-controller-error"),
                          1,
                          "Unable to replay loaded SGF tree");
      return FALSE;
    }
  }

  if (selected != NULL) {
    ggame_sgf_controller_emit_node_changed(self, selected);
    g_signal_emit(self, controller_signals[SIGNAL_MANUAL_REQUESTED], 0, selected);
  }
  return TRUE;
}

gboolean ggame_sgf_controller_select_node(GGameSgfController *self, const SgfNode *node) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(node != NULL, FALSE);

  return ggame_sgf_controller_navigate_to(self, node);
}

gboolean ggame_sgf_controller_refresh_current_node(GGameSgfController *self) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self->checkers_model) || GGAME_IS_MODEL(self->game_model), FALSE);

  const SgfNode *current = sgf_tree_get_current(self->sgf_tree);
  if (current == NULL) {
    g_debug("Missing SGF current node");
    return FALSE;
  }

  if (!ggame_sgf_controller_replay_to_node(self, current)) {
    return FALSE;
  }

  sgf_view_refresh(self->sgf_view);
  return TRUE;
}

gboolean ggame_sgf_controller_get_current_node_move(GGameSgfController *self, gpointer out_move) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);
  g_return_val_if_fail(out_move != NULL, FALSE);

  const SgfNode *current = sgf_tree_get_current(self->sgf_tree);
  if (current == NULL || sgf_node_get_parent(current) == NULL) {
    return FALSE;
  }

  gboolean has_move = FALSE;
  if (!ggame_sgf_controller_extract_node_move(current, out_move, &has_move)) {
    return FALSE;
  }

  return has_move;
}

gboolean ggame_sgf_controller_save_file(GGameSgfController *self, const char *path, GError **error) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(SGF_IS_TREE(self->sgf_tree), FALSE);

  if (!ggame_sgf_controller_sync_tree_ruleset_from_model(self)) {
    g_set_error_literal(error,
                        g_quark_from_static_string("gcheckers-sgf-controller-error"),
                        5,
                        "Unable to determine SGF ruleset metadata");
    return FALSE;
  }

  return sgf_io_save_file(path, self->sgf_tree, error);
}

gboolean ggame_sgf_controller_save_position_file(GGameSgfController *self, const char *path, GError **error) {
  GGameModel *game_model = NULL;
  const GameBackend *backend = NULL;
  gconstpointer position = NULL;
  const GameBackendVariant *variant = NULL;
  g_autoptr(SgfTree) tree = NULL;
  SgfNode *root = NULL;

  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(GGAME_IS_MODEL(self->game_model), FALSE);

  game_model = ggame_sgf_controller_peek_active_game_model(self);
  g_return_val_if_fail(GGAME_IS_MODEL(game_model), FALSE);

  backend = ggame_model_peek_backend(game_model);
  position = ggame_model_peek_position(game_model);
  variant = ggame_model_peek_variant(game_model);
  g_return_val_if_fail(backend != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);

  if (backend->sgf_write_position_node == NULL) {
    g_set_error(error,
                g_quark_from_static_string("gcheckers-sgf-controller-error"),
                16,
                "%s does not support SGF position snapshots",
                backend->display_name != NULL ? backend->display_name : "The active backend");
    return FALSE;
  }

  tree = sgf_tree_new();
  if (!SGF_IS_TREE(tree)) {
    g_set_error_literal(error,
                        g_quark_from_static_string("gcheckers-sgf-controller-error"),
                        17,
                        "Failed to allocate SGF tree for position save");
    return FALSE;
  }

  root = (SgfNode *)sgf_tree_get_root(tree);
  if (root == NULL) {
    g_set_error_literal(error,
                        g_quark_from_static_string("gcheckers-sgf-controller-error"),
                        18,
                        "Missing SGF root node for position save");
    return FALSE;
  }

  if (!sgf_io_tree_set_variant(tree, variant)) {
    g_set_error_literal(error,
                        g_quark_from_static_string("gcheckers-sgf-controller-error"),
                        19,
                        "Unable to encode SGF RU value for position save");
    return FALSE;
  }
  if (!backend->sgf_write_position_node(position, root, error)) {
    return FALSE;
  }

  return sgf_io_save_file(path, tree, error);
}

GtkWidget *ggame_sgf_controller_get_widget(GGameSgfController *self) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), NULL);

  GtkWidget *widget = sgf_view_get_widget(self->sgf_view);
  if (!widget) {
    g_debug("Missing SGF view widget");
    return NULL;
  }

  return widget;
}

SgfTree *ggame_sgf_controller_get_tree(GGameSgfController *self) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), NULL);

  if (!self->sgf_tree) {
    g_debug("Missing SGF tree");
    return NULL;
  }

  return self->sgf_tree;
}

SgfView *ggame_sgf_controller_get_view(GGameSgfController *self) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), NULL);

  if (!self->sgf_view) {
    g_debug("Missing SGF view");
    return NULL;
  }

  return self->sgf_view;
}

gboolean ggame_sgf_controller_is_replaying(GGameSgfController *self) {
  g_return_val_if_fail(GGAME_IS_SGF_CONTROLLER(self), FALSE);

  return self->is_replaying;
}

void ggame_sgf_controller_force_layout_resync(GGameSgfController *self) {
  g_return_if_fail(GGAME_IS_SGF_CONTROLLER(self));

  if (!self->sgf_view) {
    g_debug("Missing SGF view for layout resync");
    return;
  }

  sgf_view_force_layout_sync(self->sgf_view);
}
