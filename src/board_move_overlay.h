#ifndef BOARD_MOVE_OVERLAY_H
#define BOARD_MOVE_OVERLAY_H

#include <gtk/gtk.h>

#include "game_backend.h"
#include "game_model.h"
#include "games/boop/boop_game.h"

G_BEGIN_DECLS

typedef struct _GGameSgfController GGameSgfController;

#define BOARD_TYPE_MOVE_OVERLAY (board_move_overlay_get_type())

G_DECLARE_FINAL_TYPE(BoardMoveOverlay, board_move_overlay, BOARD, MOVE_OVERLAY, GObject)

typedef enum {
  BOARD_MOVE_OVERLAY_BANNER_COLOR_DEFAULT = 0,
  BOARD_MOVE_OVERLAY_BANNER_COLOR_RED,
} BoardMoveOverlayBannerColor;

BoardMoveOverlay *board_move_overlay_new(void);
GtkWidget *board_move_overlay_get_widget(BoardMoveOverlay *self);
const char *board_move_overlay_get_winner_banner_text(const GameBackend *backend, GameBackendOutcome outcome);
void board_move_overlay_render_boop_overlay_info(cairo_t *cr,
                                                 const BoopMoveOverlayInfo *overlay_info,
                                                 guint rows,
                                                 guint cols,
                                                 gint width,
                                                 gint height,
                                                 guint bottom_side);
void board_move_overlay_set_banner(BoardMoveOverlay *self, const char *text, BoardMoveOverlayBannerColor color);
void board_move_overlay_set_model(BoardMoveOverlay *self, GGameModel *model);
void board_move_overlay_set_sgf_controller(BoardMoveOverlay *self, GGameSgfController *controller);
void board_move_overlay_set_bottom_side(BoardMoveOverlay *self, guint bottom_side);
void board_move_overlay_queue_draw(BoardMoveOverlay *self);

G_END_DECLS

#endif
