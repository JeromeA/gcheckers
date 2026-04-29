#include "boop_controls.h"

#include <glib.h>

__attribute__((weak)) GtkWidget *gboop_controls_create_board_host(GGameModel *model, BoardView *board_view) {
  g_return_val_if_fail(model != NULL, NULL);
  g_return_val_if_fail(board_view != NULL, NULL);

  g_debug("Boop board-host UI hook is unavailable in this build context");
  return NULL;
}
