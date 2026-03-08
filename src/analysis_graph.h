#ifndef ANALYSIS_GRAPH_H
#define ANALYSIS_GRAPH_H

#include "sgf_tree.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define ANALYSIS_TYPE_GRAPH (analysis_graph_get_type())

G_DECLARE_FINAL_TYPE(AnalysisGraph, analysis_graph, ANALYSIS, GRAPH, GObject)

AnalysisGraph *analysis_graph_new(void);
GtkWidget *analysis_graph_get_widget(AnalysisGraph *self);
void analysis_graph_set_nodes(AnalysisGraph *self, GPtrArray *nodes, guint selected_index);
void analysis_graph_set_selected_index(AnalysisGraph *self, guint selected_index);
guint analysis_graph_get_selected_index(AnalysisGraph *self);
guint analysis_graph_get_node_count(AnalysisGraph *self);

G_END_DECLS

#endif
