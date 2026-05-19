#include "homeworlds_view.h"

#include <glib.h>

__attribute__((weak)) GtkWidget *homeworlds_view_create_board_host(GGameModel *model,
                                                                   BoardView * /*board_view*/,
                                                                   GGameAppMoveHandler /*move_handler*/,
                                                                   gpointer /*move_handler_data*/,
                                                                   const GGameAppBoardHostOptions * /*options*/) {
  g_return_val_if_fail(model != NULL, NULL);

  g_debug("Homeworlds board-host UI hook is unavailable in this build context");
  return NULL;
}

__attribute__((weak)) void homeworlds_view_sync_board_host_node(GtkWidget *board_host, const SgfNode *node) {
  g_return_if_fail(board_host != NULL);
  g_return_if_fail(node != NULL);
}

__attribute__((weak)) void homeworlds_view_set_board_host_move_report_enabled(GtkWidget *board_host,
                                                                              gboolean /*enabled*/) {
  g_return_if_fail(board_host != NULL);
}
