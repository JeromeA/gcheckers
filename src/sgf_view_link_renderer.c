#include "sgf_view_link_renderer.h"

struct _SgfViewLinkRenderer {
  GObject parent_instance;
};

G_DEFINE_TYPE(SgfViewLinkRenderer, sgf_view_link_renderer, G_TYPE_OBJECT)

static void sgf_view_link_renderer_class_init(SgfViewLinkRendererClass *klass) {
  (void)klass;
}

static void sgf_view_link_renderer_init(SgfViewLinkRenderer *self) {
  (void)self;
}

SgfViewLinkRenderer *sgf_view_link_renderer_new(void) {
  return g_object_new(SGF_TYPE_VIEW_LINK_RENDERER, NULL);
}

void sgf_view_link_renderer_draw(SgfViewLinkRenderer *self,
                                 GtkWidget *lines_area,
                                 GHashTable *node_widgets,
                                 SgfTree *tree,
                                 GArray *row_heights,
                                 cairo_t *cr,
                                 int width,
                                 int height) {
  g_return_if_fail(SGF_IS_VIEW_LINK_RENDERER(self));
  g_return_if_fail(GTK_IS_WIDGET(lines_area));
  g_return_if_fail(cr != NULL);

  if (!node_widgets || !tree || !row_heights || width <= 0 || height <= 0) {
    return;
  }
}
