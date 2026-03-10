#ifndef ANALYSIS_GRAPH_H
#define ANALYSIS_GRAPH_H

#include "sgf_tree.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define ANALYSIS_TYPE_GRAPH (analysis_graph_get_type())

G_DECLARE_FINAL_TYPE(AnalysisGraph, analysis_graph, ANALYSIS, GRAPH, GObject)

AnalysisGraph *analysis_graph_new(void);
GtkWidget *analysis_graph_get_widget(AnalysisGraph *self);
double analysis_graph_compress_score(double score);
void analysis_graph_compute_axis_range(double min_score,
                                       double max_score,
                                       gboolean has_score,
                                       double *out_min_axis_score,
                                       double *out_max_axis_score);
void analysis_graph_set_nodes(AnalysisGraph *self, GPtrArray *nodes, guint selected_index);
void analysis_graph_set_progress_node(AnalysisGraph *self, const SgfNode *node);
void analysis_graph_clear_progress_node(AnalysisGraph *self);
const SgfNode *analysis_graph_get_progress_node(AnalysisGraph *self);
void analysis_graph_set_selected_index(AnalysisGraph *self, guint selected_index);
guint analysis_graph_get_selected_index(AnalysisGraph *self);
guint analysis_graph_get_node_count(AnalysisGraph *self);

G_END_DECLS

#endif
