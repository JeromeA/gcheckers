#include "analysis_graph.h"

#include <math.h>

struct _AnalysisGraph {
  GObject parent_instance;
  GtkWidget *area;
  GPtrArray *nodes;
  guint selected_index;
};

G_DEFINE_TYPE(AnalysisGraph, analysis_graph, G_TYPE_OBJECT)

enum {
  SIGNAL_NODE_ACTIVATED,
  SIGNAL_LAST
};

static guint analysis_graph_signals[SIGNAL_LAST] = {0};

static const double analysis_graph_margin_top = 10.0;
static const double analysis_graph_margin_bottom = 14.0;
static const double analysis_graph_margin_left = 10.0;
static const double analysis_graph_margin_right = 10.0;

static gboolean analysis_graph_get_node_score(const SgfNode *node, double *out_score) {
  g_return_val_if_fail(node != NULL, FALSE);
  g_return_val_if_fail(out_score != NULL, FALSE);

  g_autoptr(SgfNodeAnalysis) analysis = sgf_node_get_analysis(node);
  if (analysis == NULL || analysis->moves == NULL || analysis->moves->len == 0) {
    return FALSE;
  }

  const SgfNodeScoredMove *entry = g_ptr_array_index(analysis->moves, 0);
  if (entry == NULL) {
    return FALSE;
  }

  *out_score = (double)entry->score;
  return TRUE;
}

static guint analysis_graph_clamp_selected_index(AnalysisGraph *self, guint index) {
  g_return_val_if_fail(ANALYSIS_IS_GRAPH(self), 0);

  if (self->nodes == NULL || self->nodes->len == 0) {
    return 0;
  }

  guint max_index = self->nodes->len - 1;
  return index > max_index ? max_index : index;
}

static double analysis_graph_node_x(guint node_count, guint index, double left, double width) {
  if (node_count <= 1) {
    return left + (width / 2.0);
  }

  double t = (double)index / (double)(node_count - 1);
  return left + (t * width);
}

static void analysis_graph_draw_axis(cairo_t *cr,
                                     double left,
                                     double top,
                                     double width,
                                     double height,
                                     double zero_y) {
  g_return_if_fail(cr != NULL);

  cairo_set_source_rgba(cr, 0.65, 0.65, 0.65, 1.0);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, left, top);
  cairo_line_to(cr, left, top + height);
  cairo_move_to(cr, left, zero_y);
  cairo_line_to(cr, left + width, zero_y);
  cairo_stroke(cr);
}

static double analysis_graph_score_to_y(double score,
                                        double min_axis_score,
                                        double score_span,
                                        double bottom,
                                        double chart_height) {
  double normalized = (score - min_axis_score) / score_span;
  return bottom - (normalized * chart_height);
}

static void analysis_graph_draw(GtkDrawingArea * /*area*/,
                                cairo_t *cr,
                                int width,
                                int height,
                                gpointer user_data) {
  AnalysisGraph *self = ANALYSIS_GRAPH(user_data);
  g_return_if_fail(ANALYSIS_IS_GRAPH(self));
  g_return_if_fail(cr != NULL);

  const guint node_count = self->nodes != NULL ? self->nodes->len : 0;

  double left = analysis_graph_margin_left;
  double right = MAX(analysis_graph_margin_left + 1.0, (double)width - analysis_graph_margin_right);
  double top = analysis_graph_margin_top;
  double bottom = MAX(analysis_graph_margin_top + 1.0, (double)height - analysis_graph_margin_bottom);
  double chart_width = right - left;
  double chart_height = bottom - top;

  double min_score = 0.0;
  double max_score = 0.0;
  gboolean has_score = FALSE;
  for (guint i = 0; i < node_count; ++i) {
    const SgfNode *node = g_ptr_array_index(self->nodes, i);
    if (node == NULL) {
      continue;
    }

    double score = 0.0;
    if (!analysis_graph_get_node_score(node, &score)) {
      continue;
    }

    if (!has_score) {
      min_score = score;
      max_score = score;
      has_score = TRUE;
      continue;
    }

    min_score = MIN(min_score, score);
    max_score = MAX(max_score, score);
  }

  double min_axis_score = min_score;
  double max_axis_score = max_score;
  if (has_score) {
    if (fabs(max_axis_score - min_axis_score) < 0.000001) {
      if (fabs(min_axis_score) < 0.000001) {
        min_axis_score = -1.0;
        max_axis_score = 1.0;
      } else {
        min_axis_score -= 1.0;
        max_axis_score += 1.0;
      }
    } else {
      min_axis_score = MIN(min_axis_score, 0.0);
      max_axis_score = MAX(max_axis_score, 0.0);
    }
  }
  double score_span = max_axis_score - min_axis_score;
  if (score_span < 0.000001) {
    score_span = 1.0;
  }
  double zero_y = analysis_graph_score_to_y(0.0, min_axis_score, score_span, bottom, chart_height);
  zero_y = CLAMP(zero_y, top, bottom);

  cairo_set_source_rgba(cr, 0.98, 0.98, 0.98, 1.0);
  cairo_paint(cr);

  analysis_graph_draw_axis(cr, left, top, chart_width, chart_height, zero_y);

  if (node_count == 0) {
    return;
  }

  if (has_score) {
    cairo_set_source_rgba(cr, 0.13, 0.48, 0.75, 0.95);
    cairo_set_line_width(cr, 2.0);

    gboolean line_open = FALSE;
    for (guint i = 0; i < node_count; ++i) {
      const SgfNode *node = g_ptr_array_index(self->nodes, i);
      if (node == NULL) {
        if (line_open) {
          cairo_stroke(cr);
          line_open = FALSE;
        }
        continue;
      }

      double score = 0.0;
      if (!analysis_graph_get_node_score(node, &score)) {
        if (line_open) {
          cairo_stroke(cr);
          line_open = FALSE;
        }
        continue;
      }

      double x = analysis_graph_node_x(node_count, i, left, chart_width);
      double y = analysis_graph_score_to_y(score, min_axis_score, score_span, bottom, chart_height);
      if (!line_open) {
        cairo_move_to(cr, x, y);
        line_open = TRUE;
      } else {
        cairo_line_to(cr, x, y);
      }
    }
    if (line_open) {
      cairo_stroke(cr);
    }

    cairo_set_source_rgba(cr, 0.13, 0.48, 0.75, 1.0);
    for (guint i = 0; i < node_count; ++i) {
      const SgfNode *node = g_ptr_array_index(self->nodes, i);
      if (node == NULL) {
        continue;
      }

      double score = 0.0;
      if (!analysis_graph_get_node_score(node, &score)) {
        continue;
      }

      double x = analysis_graph_node_x(node_count, i, left, chart_width);
      double y = analysis_graph_score_to_y(score, min_axis_score, score_span, bottom, chart_height);

      cairo_arc(cr, x, y, 2.5, 0.0, 2.0 * G_PI);
      cairo_fill(cr);
    }
  }

  guint selected_index = analysis_graph_clamp_selected_index(self, self->selected_index);
  double bar_x = analysis_graph_node_x(node_count, selected_index, left, chart_width);
  cairo_set_source_rgba(cr, 0.9, 0.2, 0.2, 0.95);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, bar_x, top);
  cairo_line_to(cr, bar_x, bottom);
  cairo_stroke(cr);
}

static void analysis_graph_emit_selected_node(AnalysisGraph *self, guint index) {
  g_return_if_fail(ANALYSIS_IS_GRAPH(self));

  if (self->nodes == NULL || self->nodes->len == 0) {
    return;
  }

  guint clamped = analysis_graph_clamp_selected_index(self, index);
  const SgfNode *node = g_ptr_array_index(self->nodes, clamped);
  if (node == NULL) {
    return;
  }

  self->selected_index = clamped;
  gtk_widget_queue_draw(self->area);
  g_signal_emit(self, analysis_graph_signals[SIGNAL_NODE_ACTIVATED], 0, node);
}

static void analysis_graph_on_pressed(GtkGestureClick * /*gesture*/,
                                      gint n_press,
                                      gdouble x,
                                      gdouble /*y*/,
                                      gpointer user_data) {
  AnalysisGraph *self = ANALYSIS_GRAPH(user_data);
  g_return_if_fail(ANALYSIS_IS_GRAPH(self));

  if (n_press != 1 || self->nodes == NULL || self->nodes->len == 0) {
    return;
  }

  int width = gtk_widget_get_width(self->area);
  if (width <= 0) {
    return;
  }

  double left = analysis_graph_margin_left;
  double right = MAX(analysis_graph_margin_left + 1.0, (double)width - analysis_graph_margin_right);
  double chart_width = right - left;

  guint index = 0;
  if (self->nodes->len > 1) {
    double clamped = CLAMP(x, left, right);
    double t = (clamped - left) / chart_width;
    index = (guint)llround(t * (double)(self->nodes->len - 1));
  }

  analysis_graph_emit_selected_node(self, index);
}

static void analysis_graph_dispose(GObject *object) {
  AnalysisGraph *self = ANALYSIS_GRAPH(object);

  g_clear_pointer(&self->nodes, g_ptr_array_unref);
  self->area = NULL;

  G_OBJECT_CLASS(analysis_graph_parent_class)->dispose(object);
}

static void analysis_graph_class_init(AnalysisGraphClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->dispose = analysis_graph_dispose;

  analysis_graph_signals[SIGNAL_NODE_ACTIVATED] = g_signal_new("node-activated",
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

static void analysis_graph_init(AnalysisGraph *self) {
  self->nodes = g_ptr_array_new();
  self->selected_index = 0;

  self->area = gtk_drawing_area_new();
  gtk_widget_set_hexpand(self->area, TRUE);
  gtk_widget_set_vexpand(self->area, FALSE);
  gtk_widget_set_size_request(self->area, -1, 140);
  gtk_widget_add_css_class(self->area, "analysis-graph");
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(self->area), analysis_graph_draw, self, NULL);

  GtkGesture *click = gtk_gesture_click_new();
  gtk_widget_add_controller(self->area, GTK_EVENT_CONTROLLER(click));
  g_signal_connect(click, "pressed", G_CALLBACK(analysis_graph_on_pressed), self);
}

AnalysisGraph *analysis_graph_new(void) {
  return g_object_new(ANALYSIS_TYPE_GRAPH, NULL);
}

GtkWidget *analysis_graph_get_widget(AnalysisGraph *self) {
  g_return_val_if_fail(ANALYSIS_IS_GRAPH(self), NULL);

  if (self->area == NULL) {
    g_debug("Analysis graph widget is missing");
    return NULL;
  }

  return self->area;
}

void analysis_graph_set_nodes(AnalysisGraph *self, GPtrArray *nodes, guint selected_index) {
  g_return_if_fail(ANALYSIS_IS_GRAPH(self));

  g_clear_pointer(&self->nodes, g_ptr_array_unref);
  self->nodes = g_ptr_array_new();
  if (nodes != NULL) {
    for (guint i = 0; i < nodes->len; ++i) {
      g_ptr_array_add(self->nodes, g_ptr_array_index(nodes, i));
    }
  }

  self->selected_index = analysis_graph_clamp_selected_index(self, selected_index);
  gtk_widget_queue_draw(self->area);
}

void analysis_graph_set_selected_index(AnalysisGraph *self, guint selected_index) {
  g_return_if_fail(ANALYSIS_IS_GRAPH(self));

  self->selected_index = analysis_graph_clamp_selected_index(self, selected_index);
  gtk_widget_queue_draw(self->area);
}

guint analysis_graph_get_selected_index(AnalysisGraph *self) {
  g_return_val_if_fail(ANALYSIS_IS_GRAPH(self), 0);

  return self->selected_index;
}

guint analysis_graph_get_node_count(AnalysisGraph *self) {
  g_return_val_if_fail(ANALYSIS_IS_GRAPH(self), 0);

  if (self->nodes == NULL) {
    return 0;
  }

  return self->nodes->len;
}
