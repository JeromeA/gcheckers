#ifndef BOARD_SELECTION_CONTROLLER_H
#define BOARD_SELECTION_CONTROLLER_H

#include "checkers_model.h"

G_BEGIN_DECLS

#define BOARD_TYPE_SELECTION_CONTROLLER (board_selection_controller_get_type())

G_DECLARE_FINAL_TYPE(BoardSelectionController,
                     board_selection_controller,
                     BOARD,
                     SELECTION_CONTROLLER,
                     GObject)

BoardSelectionController *board_selection_controller_new(void);
void board_selection_controller_set_model(BoardSelectionController *self, GCheckersModel *model);
void board_selection_controller_clear(BoardSelectionController *self);
const uint8_t *board_selection_controller_peek_path(BoardSelectionController *self, uint8_t *length);

gboolean board_selection_controller_contains(BoardSelectionController *self, uint8_t index);

gboolean board_selection_controller_handle_click(BoardSelectionController *self, uint8_t index);

G_END_DECLS

#endif
