#include "man_paintable.h"

#include <math.h>

static const double gcheckers_man_viewbox_size = 64.0;
static const double gcheckers_man_center = gcheckers_man_viewbox_size / 2.0;
static const double gcheckers_man_height_scale = 0.7;
static const double gcheckers_man_center_y = gcheckers_man_center;
static const double gcheckers_man_base_width = 54.0;
static const double gcheckers_man_base_height = 10.0 * gcheckers_man_height_scale;
static const double gcheckers_man_outer_radius_y = 12.0 * gcheckers_man_height_scale;
static const double gcheckers_man_top_inner_radius_x = 14.0;
static const double gcheckers_man_top_inner_radius_y = 5.0 * gcheckers_man_height_scale;
static const double gcheckers_king_stack_offset = gcheckers_man_outer_radius_y;

typedef struct _GCheckersManPaintable {
  GObject parent_instance;
  GdkRGBA fill_color;
  GdkRGBA stroke_color;
  guint layer_count;
} GCheckersManPaintable;

typedef struct _GCheckersManPaintableClass {
  GObjectClass parent_class;
} GCheckersManPaintableClass;

#define GCHECKERS_TYPE_MAN_PAINTABLE (gcheckers_man_paintable_get_type())
#define GCHECKERS_MAN_PAINTABLE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GCHECKERS_TYPE_MAN_PAINTABLE, GCheckersManPaintable))

static void gcheckers_man_paintable_snapshot(GdkPaintable *paintable,
                                             GdkSnapshot *snapshot,
                                             double width,
                                             double height);
static int gcheckers_man_paintable_get_intrinsic_width(GdkPaintable *paintable);
static int gcheckers_man_paintable_get_intrinsic_height(GdkPaintable *paintable);
static double gcheckers_man_paintable_get_intrinsic_aspect_ratio(GdkPaintable *paintable);

static void gcheckers_man_paintable_paintable_init(GdkPaintableInterface *iface) {
  iface->snapshot = gcheckers_man_paintable_snapshot;
  iface->get_intrinsic_width = gcheckers_man_paintable_get_intrinsic_width;
  iface->get_intrinsic_height = gcheckers_man_paintable_get_intrinsic_height;
  iface->get_intrinsic_aspect_ratio = gcheckers_man_paintable_get_intrinsic_aspect_ratio;
}

G_DEFINE_TYPE_WITH_CODE(GCheckersManPaintable,
                        gcheckers_man_paintable,
                        G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(GDK_TYPE_PAINTABLE, gcheckers_man_paintable_paintable_init))

static void gcheckers_man_paintable_class_init(GCheckersManPaintableClass * /*klass*/) {
}

static void gcheckers_man_paintable_init(GCheckersManPaintable * /*self*/) {
}

static void gcheckers_man_paintable_draw_base(GCheckersManPaintable *self, cairo_t *cr, double y_offset) {
  g_return_if_fail(self != NULL);
  g_return_if_fail(cr != NULL);

  const double top_y = gcheckers_man_center_y + y_offset;
  const double base_height = gcheckers_man_base_height;
  const double bottom_y = top_y + base_height;
  const double radius_x = gcheckers_man_base_width / 2.0;
  const double left_x = gcheckers_man_center - radius_x;
  const double right_x = gcheckers_man_center + radius_x;

  cairo_move_to(cr, left_x, top_y);
  cairo_line_to(cr, right_x, top_y);
  cairo_line_to(cr, right_x, bottom_y);
  cairo_save(cr);
  cairo_translate(cr, gcheckers_man_center, bottom_y);
  cairo_scale(cr, radius_x, gcheckers_man_outer_radius_y);
  cairo_arc(cr, 0.0, 0.0, 1.0, 0.0, G_PI);
  cairo_restore(cr);
  cairo_close_path(cr);
  gdk_cairo_set_source_rgba(cr, &self->fill_color);
  cairo_fill_preserve(cr);
  gdk_cairo_set_source_rgba(cr, &self->stroke_color);
  cairo_stroke(cr);
}

static void gcheckers_man_paintable_draw_top(GCheckersManPaintable *self, cairo_t *cr, double y_offset) {
  g_return_if_fail(self != NULL);
  g_return_if_fail(cr != NULL);

  cairo_save(cr);
  cairo_translate(cr, gcheckers_man_center, gcheckers_man_center_y + y_offset);
  cairo_scale(cr, gcheckers_man_base_width / 2.0, gcheckers_man_outer_radius_y);
  cairo_arc(cr, 0.0, 0.0, 1.0, 0.0, 2.0 * G_PI);
  cairo_restore(cr);
  gdk_cairo_set_source_rgba(cr, &self->fill_color);
  cairo_fill_preserve(cr);
  gdk_cairo_set_source_rgba(cr, &self->stroke_color);
  cairo_stroke(cr);

  cairo_save(cr);
  cairo_translate(cr, gcheckers_man_center, gcheckers_man_center_y + y_offset);
  cairo_scale(cr, gcheckers_man_top_inner_radius_x, gcheckers_man_top_inner_radius_y);
  cairo_arc(cr, 0.0, 0.0, 1.0, 0.0, 2.0 * G_PI);
  cairo_restore(cr);
  gdk_cairo_set_source_rgba(cr, &self->stroke_color);
  cairo_stroke(cr);
}

void gcheckers_man_paintable_draw(cairo_t *cr,
                                  double width,
                                  double height,
                                  const GdkRGBA *fill_color,
                                  const GdkRGBA *stroke_color,
                                  guint layer_count) {
  g_return_if_fail(cr != NULL);
  g_return_if_fail(fill_color != NULL);
  g_return_if_fail(stroke_color != NULL);
  g_return_if_fail(layer_count > 0);

  GCheckersManPaintable state = {0};
  state.fill_color = *fill_color;
  state.stroke_color = *stroke_color;
  state.layer_count = layer_count;

  cairo_save(cr);
  cairo_set_line_width(cr, 2.0);
  cairo_scale(cr, width / gcheckers_man_viewbox_size, height / gcheckers_man_viewbox_size);
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

  for (guint layer = 0; layer < state.layer_count; ++layer) {
    double y_offset = -(double)layer * gcheckers_king_stack_offset;
    gcheckers_man_paintable_draw_base(&state, cr, y_offset);
    gcheckers_man_paintable_draw_top(&state, cr, y_offset);
  }

  cairo_restore(cr);
}

static void gcheckers_man_paintable_snapshot(GdkPaintable *paintable,
                                             GdkSnapshot *snapshot,
                                             double width,
                                             double height) {
  g_return_if_fail(GDK_IS_PAINTABLE(paintable));
  g_return_if_fail(GDK_IS_SNAPSHOT(snapshot));

  if (width <= 0.0 || height <= 0.0) {
    return;
  }

  GCheckersManPaintable *self = GCHECKERS_MAN_PAINTABLE(paintable);
  graphene_rect_t bounds = GRAPHENE_RECT_INIT(0.0f, 0.0f, (float)width, (float)height);
  cairo_t *cr = gtk_snapshot_append_cairo(snapshot, &bounds);

  gcheckers_man_paintable_draw(cr, width, height, &self->fill_color, &self->stroke_color, self->layer_count);
  cairo_destroy(cr);
}

static int gcheckers_man_paintable_get_intrinsic_width(GdkPaintable *paintable) {
  g_return_val_if_fail(GDK_IS_PAINTABLE(paintable), 0);
  return (int)gcheckers_man_viewbox_size;
}

static int gcheckers_man_paintable_get_intrinsic_height(GdkPaintable *paintable) {
  g_return_val_if_fail(GDK_IS_PAINTABLE(paintable), 0);
  return (int)gcheckers_man_viewbox_size;
}

static double gcheckers_man_paintable_get_intrinsic_aspect_ratio(GdkPaintable *paintable) {
  g_return_val_if_fail(GDK_IS_PAINTABLE(paintable), 0.0);
  return 1.0;
}

GdkPaintable *gcheckers_man_paintable_new(const GdkRGBA *fill_color,
                                          const GdkRGBA *stroke_color,
                                          guint layer_count) {
  g_return_val_if_fail(fill_color != NULL, NULL);
  g_return_val_if_fail(stroke_color != NULL, NULL);
  g_return_val_if_fail(layer_count > 0, NULL);

  GCheckersManPaintable *paintable = g_object_new(gcheckers_man_paintable_get_type(), NULL);
  paintable->fill_color = *fill_color;
  paintable->stroke_color = *stroke_color;
  paintable->layer_count = layer_count;
  return GDK_PAINTABLE(paintable);
}
