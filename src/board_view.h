#ifndef BOARD_VIEW_H
#define BOARD_VIEW_H

#include <gtk/gtk.h>

#include "game_model.h"

G_BEGIN_DECLS

typedef struct _GGameSgfController GGameSgfController;

#define BOARD_TYPE_VIEW (board_view_get_type())

G_DECLARE_FINAL_TYPE(BoardView, board_view, BOARD, VIEW, GObject)

typedef gboolean (*BoardViewMoveHandler)(gconstpointer move, gpointer user_data);
typedef gboolean (*BoardViewSquareHandler)(guint index, guint button, gpointer user_data);
typedef gboolean (*BoardViewMoveCandidatePreference)(gconstpointer move, gpointer user_data);
typedef gboolean (*BoardViewMoveCompletionConfirmation)(const GameBackendMoveBuilder *builder,
                                                        gconstpointer move,
                                                        gpointer user_data);
typedef void (*BoardViewSelectionChangedHandler)(gpointer user_data);
typedef void (*BoardViewBottomSideChangedHandler)(gpointer user_data);

BoardView *board_view_new(void);
GtkWidget *board_view_get_widget(BoardView *self);
void board_view_set_model(BoardView *self, gpointer model);
void board_view_set_sgf_controller(BoardView *self, GGameSgfController *controller);
void board_view_set_move_handler(BoardView *self, gpointer handler, gpointer user_data);
void board_view_set_move_candidate_preference(BoardView *self,
                                              BoardViewMoveCandidatePreference preference,
                                              gpointer user_data);
void board_view_set_move_completion_confirmation(BoardView *self,
                                                 BoardViewMoveCompletionConfirmation confirmation,
                                                 gpointer user_data);
void board_view_set_selection_changed_handler(BoardView *self,
                                              BoardViewSelectionChangedHandler handler,
                                              gpointer user_data);
void board_view_set_square_handler(BoardView *self, gpointer handler, gpointer user_data);
void board_view_update(BoardView *self);
void board_view_clear_selection(BoardView *self);
const GameBackendMoveBuilder *board_view_peek_move_builder(BoardView *self);
gboolean board_view_move_selection_completion_pending(BoardView *self);
gboolean board_view_confirm_move_selection(BoardView *self);
void board_view_set_input_enabled(BoardView *self, gboolean enabled);
void board_view_set_bottom_side_changed_handler(BoardView *self,
                                                BoardViewBottomSideChangedHandler handler,
                                                gpointer user_data);
void board_view_set_bottom_side(BoardView *self, guint bottom_side);
guint board_view_get_bottom_side(BoardView *self);
void board_view_set_banner_text(BoardView *self, const char *text);
void board_view_set_banner_text_red(BoardView *self, const char *text);

G_END_DECLS

#endif
