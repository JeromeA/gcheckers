#ifndef HOMEWORLDS_SGF_POSITION_H
#define HOMEWORLDS_SGF_POSITION_H

#include "../../sgf_tree.h"
#include "homeworlds_types.h"

#include <glib.h>

G_BEGIN_DECLS

gboolean homeworlds_sgf_position_apply_setup_node(gpointer position, const SgfNode *node, GError **error);
gboolean homeworlds_sgf_position_write_position_node(gconstpointer position, SgfNode *node, GError **error);

G_END_DECLS

#endif
