#ifndef BOOP_SGF_POSITION_H
#define BOOP_SGF_POSITION_H

#include "../../sgf_tree.h"

#include <glib.h>

G_BEGIN_DECLS

gboolean boop_sgf_position_apply_setup_node(gpointer position, const SgfNode *node, GError **error);
gboolean boop_sgf_position_write_position_node(gconstpointer position, SgfNode *node, GError **error);

G_END_DECLS

#endif
