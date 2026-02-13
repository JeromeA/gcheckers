#include "board_selection_controller.h"

#include "game.h"

#include <string.h>

struct _BoardSelectionController {
  GObject parent_instance;
  GCheckersModel *model;
  uint8_t selected_path[CHECKERS_MAX_MOVE_LENGTH];
  uint8_t selected_length;
};

G_DEFINE_TYPE(BoardSelectionController, board_selection_controller, G_TYPE_OBJECT)

static void board_selection_controller_print_move(const char *label, const CheckersMove *move) {
  g_return_if_fail(label != NULL);
  g_return_if_fail(move != NULL);

  g_print("%s plays move with %u steps\n", label, (guint)move->length);
}

static gboolean board_selection_controller_move_has_prefix(const CheckersMove *move,
                                                           const uint8_t *path,
                                                           uint8_t length) {
  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  if (move->length < length) {
    return FALSE;
  }
  for (uint8_t i = 0; i < length; ++i) {
    if (move->path[i] != path[i]) {
      return FALSE;
    }
  }
  return TRUE;
}

static gboolean board_selection_controller_moves_have_prefix(const MoveList *moves,
                                                             const uint8_t *path,
                                                             uint8_t length) {
  g_return_val_if_fail(moves != NULL, FALSE);
  g_return_val_if_fail(path != NULL, FALSE);

  for (size_t i = 0; i < moves->count; ++i) {
    if (board_selection_controller_move_has_prefix(&moves->moves[i], path, length)) {
      return TRUE;
    }
  }
  return FALSE;
}

static const CheckersMove *board_selection_controller_find_exact_move(const MoveList *moves,
                                                                      const uint8_t *path,
                                                                      uint8_t length) {
  g_return_val_if_fail(moves != NULL, NULL);
  g_return_val_if_fail(path != NULL, NULL);

  for (size_t i = 0; i < moves->count; ++i) {
    if (moves->moves[i].length != length) {
      continue;
    }
    if (board_selection_controller_move_has_prefix(&moves->moves[i], path, length)) {
      return &moves->moves[i];
    }
  }
  return NULL;
}

static gboolean board_selection_controller_moves_start_with(const MoveList *moves, uint8_t index) {
  g_return_val_if_fail(moves != NULL, FALSE);

  uint8_t path = index;
  return board_selection_controller_moves_have_prefix(moves, &path, 1);
}

static void board_selection_controller_apply_player_move(BoardSelectionController *self, const CheckersMove *move) {
  g_return_if_fail(BOARD_IS_SELECTION_CONTROLLER(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(self->model));
  g_return_if_fail(move != NULL);

  if (!gcheckers_model_apply_move(self->model, move)) {
    g_debug("Failed to apply player move\n");
    return;
  }

  board_selection_controller_print_move("Player", move);
  board_selection_controller_clear(self);
}

static void board_selection_controller_dispose(GObject *object) {
  BoardSelectionController *self = BOARD_SELECTION_CONTROLLER(object);

  g_clear_object(&self->model);

  G_OBJECT_CLASS(board_selection_controller_parent_class)->dispose(object);
}

static void board_selection_controller_class_init(BoardSelectionControllerClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = board_selection_controller_dispose;
}

static void board_selection_controller_init(BoardSelectionController *self) {
  self->selected_length = 0;
}

BoardSelectionController *board_selection_controller_new(void) {
  return g_object_new(BOARD_TYPE_SELECTION_CONTROLLER, NULL);
}

void board_selection_controller_set_model(BoardSelectionController *self, GCheckersModel *model) {
  g_return_if_fail(BOARD_IS_SELECTION_CONTROLLER(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(model));

  if (self->model) {
    g_clear_object(&self->model);
  }

  self->model = g_object_ref(model);
  board_selection_controller_clear(self);
}

void board_selection_controller_clear(BoardSelectionController *self) {
  g_return_if_fail(BOARD_IS_SELECTION_CONTROLLER(self));

  self->selected_length = 0;
}

const uint8_t *board_selection_controller_peek_path(BoardSelectionController *self, uint8_t *length) {
  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), NULL);
  g_return_val_if_fail(length != NULL, NULL);

  *length = self->selected_length;
  return self->selected_path;
}

gboolean board_selection_controller_contains(BoardSelectionController *self, uint8_t index) {
  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), FALSE);

  for (uint8_t i = 0; i < self->selected_length; ++i) {
    if (self->selected_path[i] == index) {
      return TRUE;
    }
  }
  return FALSE;
}

gboolean board_selection_controller_handle_click(BoardSelectionController *self, uint8_t index) {
  g_return_val_if_fail(BOARD_IS_SELECTION_CONTROLLER(self), FALSE);
  g_return_val_if_fail(GCHECKERS_IS_MODEL(self->model), FALSE);

  MoveList moves = gcheckers_model_list_moves(self->model);
  if (moves.count == 0) {
    movelist_free(&moves);
    gcheckers_model_step_random_move(self->model, NULL);
    board_selection_controller_clear(self);
    return TRUE;
  }

  if (self->selected_length == 0) {
    if (!board_selection_controller_moves_start_with(&moves, index)) {
      movelist_free(&moves);
      return FALSE;
    }
    self->selected_path[0] = index;
    self->selected_length = 1;
    movelist_free(&moves);
    return TRUE;
  }

  if (self->selected_length >= CHECKERS_MAX_MOVE_LENGTH) {
    g_debug("Selection path length exceeded max move length\n");
    movelist_free(&moves);
    return FALSE;
  }

  uint8_t candidate[CHECKERS_MAX_MOVE_LENGTH];
  memcpy(candidate, self->selected_path, self->selected_length);
  candidate[self->selected_length] = index;
  uint8_t candidate_length = self->selected_length + 1;

  if (!board_selection_controller_moves_have_prefix(&moves, candidate, candidate_length)) {
    if (board_selection_controller_moves_start_with(&moves, index)) {
      self->selected_path[0] = index;
      self->selected_length = 1;
      movelist_free(&moves);
      return TRUE;
    }
    g_debug("Selected path is not in move list\n");
    movelist_free(&moves);
    return FALSE;
  }

  memcpy(self->selected_path, candidate, candidate_length);
  self->selected_length = candidate_length;

  const CheckersMove *match = board_selection_controller_find_exact_move(&moves, candidate, candidate_length);
  if (match) {
    board_selection_controller_apply_player_move(self, match);
  }

  movelist_free(&moves);
  return TRUE;
}
