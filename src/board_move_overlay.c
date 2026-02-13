#include "board_move_overlay.h"

#include "widget_utils.h"

struct _BoardMoveOverlay {
  GObject parent_instance;
  GtkWidget *drawing_area;
};

G_DEFINE_TYPE(BoardMoveOverlay, board_move_overlay, G_TYPE_OBJECT)

static void board_move_overlay_dispose(GObject *object) {
  BoardMoveOverlay *self = BOARD_MOVE_OVERLAY(object);

  gboolean drawing_area_removed = TRUE;
  if (self->drawing_area) {
    drawing_area_removed = gcheckers_widget_remove_from_parent(self->drawing_area);
    if (!drawing_area_removed && gtk_widget_get_parent(self->drawing_area)) {
      g_debug("Failed to remove board overlay drawing area from parent during dispose\n");
    }
  }

  if (drawing_area_removed) {
    g_clear_object(&self->drawing_area);
  } else {
    self->drawing_area = NULL;
  }

  G_OBJECT_CLASS(board_move_overlay_parent_class)->dispose(object);
}

static void board_move_overlay_class_init(BoardMoveOverlayClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = board_move_overlay_dispose;
}

static void board_move_overlay_init(BoardMoveOverlay *self) {
  self->drawing_area = gtk_drawing_area_new();
  g_object_ref_sink(self->drawing_area);
  gtk_widget_set_hexpand(self->drawing_area, TRUE);
  gtk_widget_set_vexpand(self->drawing_area, TRUE);
  gtk_widget_set_can_target(self->drawing_area, FALSE);
}

BoardMoveOverlay *board_move_overlay_new(void) {
  return g_object_new(BOARD_TYPE_MOVE_OVERLAY, NULL);
}

GtkWidget *board_move_overlay_get_widget(BoardMoveOverlay *self) {
  g_return_val_if_fail(BOARD_IS_MOVE_OVERLAY(self), NULL);

  return self->drawing_area;
}

void board_move_overlay_set_model(BoardMoveOverlay *self, GCheckersModel *model) {
  g_return_if_fail(BOARD_IS_MOVE_OVERLAY(self));
  g_return_if_fail(GCHECKERS_IS_MODEL(model));
}

void board_move_overlay_queue_draw(BoardMoveOverlay *self) {
  g_return_if_fail(BOARD_IS_MOVE_OVERLAY(self));
}
